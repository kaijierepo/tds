#include "pch.h"
#include "streamServer.h"
#include "logger.h"
#include <fstream>
#include <filesystem>
#include <atomic>

// ============================================================================
// 本地文件流服务
// 将本地 h264 文件读取并封装为 RTP 流，通过 RTSP 服务端对外提供
// ============================================================================

// 本地文件流上下文：存储解析后的 NAL 单元和线程控制
struct LocalFileStreamCtx {
	std::vector<std::vector<uint8_t>> nals;  // 解析后的 NAL 单元（Annex B → 裸 NAL）
	std::vector<uint8_t> sps;
	std::vector<uint8_t> pps;
	int payload_type = 96;
	int clock_rate = 90000;
	int fps = 25;                              // 视频帧率，用于时间戳计算和帧间隔 sleep
	std::thread feed_thread_;
	std::atomic<bool> running_{false};
		std::shared_ptr<StreamNode> node;  // 关联的 StreamNode（shared_ptr 防止 feed 线程中悬空指针）
	};

static std::mutex g_localStreamMutex;
static std::map<std::string, std::shared_ptr<LocalFileStreamCtx>> g_localStreams;  // key=tag

// Windows 下将 UTF-8 路径转为宽字符（std::ifstream 的 string 重载使用系统 locale，中文字符需 wchar_t 重载）
#ifdef _WIN32
static std::wstring pathToWide(const std::string& utf8) {
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	if (len <= 0) return std::wstring();
	std::wstring w(len - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], len);
	return w;
}
#endif

// 解析 h264 Annex B 文件，提取 NAL 单元
static bool parseH264File(const std::string& filePath,
	std::vector<std::vector<uint8_t>>& nals,
	std::vector<uint8_t>& sps, std::vector<uint8_t>& pps)
{
#ifdef _WIN32
	std::ifstream file(pathToWide(filePath), std::ios::binary | std::ios::ate);
#else
	std::ifstream file(filePath, std::ios::binary | std::ios::ate);
#endif
	if (!file.is_open()) {
		LOG("[LocalFileStream] Failed to open file: %s", filePath.c_str());
		return false;
	}
	std::streamsize fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> buffer((size_t)fileSize);
	if (!file.read((char*)buffer.data(), fileSize)) {
		LOG("[LocalFileStream] Failed to read file: %s", filePath.c_str());
		return false;
	}
	//LOG("[LocalFileStream] Read file %s, size=%lld bytes", filePath.c_str(), (long long)fileSize);

	// 按 Annex B 起始码 0x00000001 或 0x000001 拆分 NAL 单元
	size_t pos = 0;
	while (pos < buffer.size()) {
		// 跳过前导的 0x00 字节
		while (pos < buffer.size() && buffer[pos] == 0x00) pos++;
		if (pos >= buffer.size()) break;

		// 跳过 0x01 起始码标记
		if (pos < buffer.size() && buffer[pos] == 0x01) pos++;
		else break;

		// 查找下一个起始码位置
		size_t nextStart = buffer.size();
		for (size_t i = pos; i + 3 < buffer.size(); i++) {
			if (buffer[i] == 0x00 && buffer[i+1] == 0x00) {
				if (buffer[i+2] == 0x01) {
					nextStart = i;
					break;
				}
				if (i + 3 < buffer.size() && buffer[i+2] == 0x00 && buffer[i+3] == 0x01) {
					nextStart = i;
					break;
				}
			}
		}

		std::vector<uint8_t> nal(buffer.begin() + (long long)pos, buffer.begin() + (long long)nextStart);
		if (!nal.empty()) {
			uint8_t nalType = nal[0] & 0x1F;
			if (nalType == 7) {  // SPS
				sps = nal;
			} else if (nalType == 8) {  // PPS
				pps = nal;
			}
			nals.push_back(std::move(nal));
		}
		pos = nextStart;
	}

	//LOG("[LocalFileStream] Parsed %zu NALs (SPS=%zu bytes, PPS=%zu bytes)",
	//	nals.size(), sps.size(), pps.size());
	return !nals.empty();
}

// ============================================================
// H.264 SPS VUI 定时信息解析
// ============================================================

// 简易比特流读取器（用于 SPS 解析，仅需前向读取）
struct H264BitReader {
	const uint8_t* data;
	size_t size;
	size_t bytePos;
	int bitPos;  // 0=MSB, 7=LSB

	H264BitReader(const uint8_t* d, size_t s)
		: data(d), size(s), bytePos(0), bitPos(0) {}

	int readBit() {
		if (bytePos >= size) return 0;
		int bit = (data[bytePos] >> (7 - bitPos)) & 1;
		if (++bitPos >= 8) { bitPos = 0; bytePos++; }
		return bit;
	}

	unsigned int readBits(int n) {
		unsigned int val = 0;
		for (int i = 0; i < n; i++)
			val = (val << 1) | readBit();
		return val;
	}

	// 读取无符号指数哥伦布编码值
	unsigned int readUE() {
		int leadingZeros = 0;
		while (readBit() == 0) {
			leadingZeros++;
			if (bytePos >= size && bitPos == 0) return 0;
		}
		if (leadingZeros == 0) return 0;
		return (1u << leadingZeros) - 1 + readBits(leadingZeros);
	}

	// 读取有符号指数哥伦布编码值
	int readSE() {
		unsigned int codeNum = readUE();
		return (codeNum & 1) ? (int)((codeNum + 1) >> 1) : -(int)(codeNum >> 1);
	}
};

// 从 SPS NAL 中解析 VUI timing info，计算视频帧率
// spsNal: 裸 NAL（含 NAL header 字节）
// 返回 fps（如 29.97, 25.0, 30.0），解析失败返回 0.0
static double parseSpsFps(const std::vector<uint8_t>& spsNal) {
	if (spsNal.size() < 10) return 0.0;

	// 1) 移除 emulation prevention bytes (0x00 0x00 0x03)
	std::vector<uint8_t> rbsp;
	rbsp.reserve(spsNal.size());
	for (size_t i = 0; i < spsNal.size(); i++) {
		if (i >= 2 && spsNal[i-2] == 0 && spsNal[i-1] == 0 && spsNal[i] == 3) {
			continue;  // 跳过 0x03
		}
		rbsp.push_back(spsNal[i]);
	}
	if (rbsp.size() < 10) return 0.0;

	H264BitReader br(rbsp.data(), rbsp.size());

	// 2) 解析 NAL header（1 字节）
	br.readBits(8);  // forbidden_zero_bit(1) + nal_ref_idc(2) + nal_unit_type(5)

	// 3) 解析 SPS RBSP
	unsigned int profile_idc = br.readBits(8);
	br.readBits(8);  // constraint_set_flags
	br.readBits(8);  // level_idc
	br.readUE();     // seq_parameter_set_id

	// 高档次有额外 chroma 格式参数
	if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
		profile_idc == 244 || profile_idc == 44 || profile_idc == 83 ||
		profile_idc == 86 || profile_idc == 118 || profile_idc == 128 ||
		profile_idc == 138 || profile_idc == 139 || profile_idc == 134 || profile_idc == 135) {
		unsigned int chroma_format_idc = br.readUE();
		if (chroma_format_idc == 3)
			br.readBits(1);  // separate_colour_plane_flag
		br.readUE();  // bit_depth_luma_minus8
		br.readUE();  // bit_depth_chroma_minus8
		br.readBits(1);  // qpprime_y_zero_transform_bypass_flag
		unsigned int seq_scaling_matrix_present_flag = br.readBits(1);
		if (seq_scaling_matrix_present_flag) {
			unsigned int maxLists = (chroma_format_idc == 3) ? 12 : 8;
			for (unsigned int i = 0; i < maxLists; i++) {
				if (i < rbsp.size()) {
					unsigned int present = br.readBits(1);
					if (present) {
						int size = (i < 6) ? 16 : 64;
						int lastScale = 8, nextScale = 8;
						for (int j = 0; j < size; j++) {
							if (nextScale != 0) {
								int deltaScale = br.readSE();
								nextScale = (lastScale + deltaScale + 256) & 0xFF;
							}
							if (nextScale == 0) { /* keep lastScale */ }
							else lastScale = nextScale;
						}
					}
				}
			}
		}
	}

	// 4) 所有档次共有的字段
	br.readUE();  // log2_max_frame_num_minus4
	unsigned int pic_order_cnt_type = br.readUE();
	if (pic_order_cnt_type == 0) {
		br.readUE();  // log2_max_pic_order_cnt_lsb_minus4
	} else if (pic_order_cnt_type == 1) {
		br.readBits(1);  // delta_pic_order_always_zero_flag
		br.readSE();     // offset_for_non_ref_pic
		br.readSE();     // offset_for_top_to_bottom_field
		unsigned int num_ref_frames_in_poc_cycle = br.readUE();
		for (unsigned int i = 0; i < num_ref_frames_in_poc_cycle; i++) {
			br.readSE();  // offset_for_ref_frame[i]
		}
	}
	// pic_order_cnt_type == 2: no additional data

	br.readUE();  // max_num_ref_frames
	br.readBits(1);  // gaps_in_frame_num_value_allowed_flag
	br.readUE();  // pic_width_in_mbs_minus1
	br.readUE();  // pic_height_in_map_units_minus1

	unsigned int frame_mbs_only_flag = br.readBits(1);
	if (!frame_mbs_only_flag) {
		br.readBits(1);  // mb_adaptive_frame_field_flag
	}
	br.readBits(1);  // direct_8x8_inference_flag
	unsigned int frame_cropping_flag = br.readBits(1);
	if (frame_cropping_flag) {
		br.readUE();  // frame_crop_left_offset
		br.readUE();  // frame_crop_right_offset
		br.readUE();  // frame_crop_top_offset
		br.readUE();  // frame_crop_bottom_offset
	}

	// 5) VUI 参数
	unsigned int vui_parameters_present_flag = br.readBits(1);
	if (!vui_parameters_present_flag) return 0.0;

	// aspect_ratio_info_present_flag
	if (br.readBits(1)) {
		unsigned int aspect_ratio_idc = br.readBits(8);
		if (aspect_ratio_idc == 255) {
			br.readBits(16);
			br.readBits(16);
		}
	}

	if (br.readBits(1)) br.readBits(1);  // overscan_info

	if (br.readBits(1)) {  // video_signal_type_present_flag
		br.readBits(3);  // video_format
		br.readBits(1);  // video_full_range_flag
		if (br.readBits(1)) {  // colour_description_present_flag
			br.readBits(8);
			br.readBits(8);
			br.readBits(8);
		}
	}

	if (br.readBits(1)) {  // chroma_loc_info_present_flag
		br.readUE();
		br.readUE();
	}

	// 6) 目标：timing info
	unsigned int timing_info_present_flag = br.readBits(1);
	if (!timing_info_present_flag) return 0.0;

	unsigned int num_units_in_tick = br.readBits(32);
	unsigned int time_scale = br.readBits(32);

	if (num_units_in_tick == 0) return 0.0;

	// 帧率 = time_scale / (2 * num_units_in_tick)
	// 2x 因子：H.264 clock tick 以 field 为单位，一个 frame = 2 fields
	double fps = (double)time_scale / (2.0 * (double)num_units_in_tick);

	return (fps >= 1.0 && fps <= 120.0) ? fps : 0.0;
}

// 从 slice NAL 中提取 first_mb_in_slice（用于判断是否为新帧）
// 返回 0 = 新帧的第一个 slice，>0 = 同一帧的延续 slice
static unsigned int getFirstMbInSlice(const std::vector<uint8_t>& nal) {
	if (nal.size() < 2) return 0;
	// 简单 Exp-Golomb 解析器，只读 first_mb_in_slice
	size_t bytePos = 1;  // 跳过 NAL header 字节
	int bitPos = 0;
	auto readBit = [&]() -> int {
		if (bytePos >= nal.size()) return 0;
		int bit = (nal[bytePos] >> (7 - bitPos)) & 1;
		if (++bitPos >= 8) { bitPos = 0; bytePos++; }
		return bit;
	};
	int leadingZeros = 0;
	while (readBit() == 0) {
		leadingZeros++;
		if (bytePos >= nal.size() && bitPos == 0) return 0;
	}
	if (leadingZeros == 0) return 0;
	unsigned int val = 0;
	for (int i = 0; i < leadingZeros; i++)
		val = (val << 1) | readBit();
	return (1u << leadingZeros) - 1 + val;
}

// 本地文件 RTP 喂流线程：循环读取 NAL，封装为 RTP 包，写入 StreamNode 缓冲区并分发给客户端
static void localFileFeedLoop(std::shared_ptr<LocalFileStreamCtx> ctx) {
	if (!ctx || !ctx->node) return;

	const auto& nals = ctx->nals;
	if (nals.empty()) return;

	// 提前拷贝 tag，因为 ctx->node 是裸指针，循环结束后可能已被其他线程销毁
	const std::string tag = ctx->node->config_.tag;

	uint16_t seq = 0;
	uint32_t timestamp = 0;
	uint32_t ssrc = 0x4C4F4341;  // "LOCA" 标识本地文件源
	uint32_t clockRate = (uint32_t)ctx->clock_rate;


	//LOG("[LocalFileStream] Feed loop started: tag=%s, nals=%zu",
	//	tag.c_str(), nals.size());

	size_t nalIdx = 0;
	bool wasIdle = true;  // 跟踪是否处于空闲态（无客户端），用于在首个客户端连接时重置启动时间
	while (ctx->running_ && ctx->node->running_) {
		// 没有客户端时：空转，不做任何操作，等待客户端连接
		bool hasClients = false;
		{
			ctx->node->session_list_client_pull_mutex_.lock();
			hasClients = !ctx->node->session_list_client_pull_.empty();
			ctx->node->session_list_client_pull_mutex_.unlock();
		}
		if (!hasClients) {
			// 空转：什么也不干，立即再次检查（busy-wait）
			// 重置播放位置，确保有客户端时从头开始
			nalIdx = 0;
			timestamp = 0;
			wasIdle = true;
			continue;
		}

		// 从空闲态切换到活跃态时，重置启动时间为当前时间，确保统计中 startTime 反映实际媒体开始播放的时刻
		if (wasIdle) {
			std::lock_guard<std::mutex> lock(ctx->node->stats_mutex_);
			ctx->node->stats_.start_time = std::chrono::steady_clock::now();
			ctx->node->stats_.last_frame_time = std::chrono::steady_clock::now();
			wasIdle = false;
		}

		const auto& nal = nals[nalIdx];
		uint8_t nalType = nal.empty() ? 0 : (nal[0] & 0x1F);
		uint8_t nri = nal.empty() ? 0 : ((nal[0] & 0x60) >> 5);

		// SPS/PPS 参数集：发送但不推进时间戳、不 sleep（花屏修复：确保客户端收到带内参数集）
		if (nalType == 7 || nalType == 8) {
			auto pkt = std::make_shared<StreamNode::RTPPacket>();
			pkt->version = 2;
			pkt->payload_type = (uint8_t)ctx->payload_type;
			pkt->sequence_number = seq++;
			pkt->timestamp = timestamp;
			pkt->ssrc = ssrc;
			pkt->marker = false;
			pkt->payload = nal;
			ctx->node->addToRtpBuffer(pkt);
			ctx->node->sendRTPPacketToClients(*pkt);
			nalIdx = (nalIdx + 1) % nals.size();
			continue;
		}

		if (nal.size() <= 1400) {
			// 单包模式
			auto pkt = std::make_shared<StreamNode::RTPPacket>();
			pkt->version = 2;
			pkt->payload_type = (uint8_t)ctx->payload_type;
			pkt->sequence_number = seq++;
			pkt->timestamp = timestamp;
			pkt->ssrc = ssrc;
			pkt->marker = true;
			pkt->payload = nal;

			ctx->node->addToRtpBuffer(pkt);
			ctx->node->sendRTPPacketToClients(*pkt);
		} else {
			// FU-A 分片模式
			size_t offset = 1;  // 跳过 NAL header
			bool first = true;
			while (offset < nal.size()) {
				size_t chunkSize = (std::min)((size_t)1400, nal.size() - offset);
				bool last = (offset + chunkSize >= nal.size());

				auto fragPkt = std::make_shared<StreamNode::RTPPacket>();
				fragPkt->version = 2;
				fragPkt->payload_type = (uint8_t)ctx->payload_type;
				fragPkt->sequence_number = seq++;
				fragPkt->timestamp = timestamp;
				fragPkt->ssrc = ssrc;
				fragPkt->marker = last;

				// FU indicator: F=0, NRI=原始NRI, Type=FU-A(28)
				uint8_t fuIndicator = (0 << 7) | ((nri & 0x3) << 5) | 28;
				// FU header: S=1(first) or 0, E=1(last) or 0, R=0, Type=原始NAL类型
				uint8_t fuHeader = (first ? 0x80 : 0x00) | (last ? 0x40 : 0x00) | (nalType & 0x1F);

				fragPkt->payload.resize(2 + chunkSize);
				fragPkt->payload[0] = fuIndicator;
				fragPkt->payload[1] = fuHeader;
				memcpy(&fragPkt->payload[2], &nal[offset], chunkSize);

				ctx->node->addToRtpBuffer(fragPkt);
				ctx->node->sendRTPPacketToClients(*fragPkt);
				offset += chunkSize;
				first = false;
			}
		}

		// 每个帧 NAL（type 1 或 5）独立推进时间戳 + sleep
		// 原始 H.264 文件通常没有 AUD/SEI 间隔，每个 slice NAL 就是独立的一帧
		if ((nalType == 1 || nalType == 5) && getFirstMbInSlice(nal) == 0) {
			timestamp += clockRate / ctx->fps;
			std::this_thread::sleep_for(std::chrono::milliseconds(1000 / ctx->fps));
		}

		// 诊断进度（每 1000 帧）
		static int frameCounter = 0;
		frameCounter++;
		if ((frameCounter % 1000) == 0) {
			LOG("[LocalFileStream] Progress: tag=%s, nalIdx=%zu/%zu, ts=%u, nalType=%u",
				tag.c_str(), nalIdx, nals.size(), timestamp, nalType);
		}

		// 循环播放
		nalIdx = (nalIdx + 1) % nals.size();
		if (nalIdx == 0) {
			// 循环一轮后重新从 0 开始 timestamp，避免溢出
			timestamp = 0;
		}
	}

	//LOG("[LocalFileStream] Feed loop ended: tag=%s", tag.c_str());
}

bool StreamServer::serveLocalStreamFile(const std::string& filePath, const std::string& url) {
	// 检查是否已存在同名流
	{
		std::lock_guard<std::mutex> lock(g_localStreamMutex);
		if (g_localStreams.find(url) != g_localStreams.end()) {
			LOG("[LocalFileStream] Stream already exists for url: %s", url.c_str());
			return false;
		}
	}

	// 判断文件类型：.h264 直接解析，.mp4 暂不支持
	std::string ext;
	size_t dotPos = filePath.rfind('.');
	if (dotPos != std::string::npos) {
		ext = filePath.substr(dotPos);
		for (auto& c : ext) c = (char)tolower((unsigned char)c);
	}

	std::vector<std::vector<uint8_t>> nals;
	std::vector<uint8_t> sps, pps;

	if (ext == ".h264") {
		if (!parseH264File(filePath, nals, sps, pps)) {
			return false;
		}
	} else if (ext == ".mp4") {
		LOG("[LocalFileStream] MP4 format not yet supported: %s", filePath.c_str());
		return false;
	} else {
		// 尝试按 h264 裸流解析
		if (!parseH264File(filePath, nals, sps, pps)) {
			return false;
		}
	}

	// 创建 StreamNode
	auto node = std::make_shared<StreamNode>();
	StreamNode::Config cfg;
	cfg.streamUrl = url;
	cfg.origin_pull_url = "file://" + filePath;
	cfg.relay_push_url = "";
	cfg.retry_interval = 0;
	cfg.max_retries = 0;
	cfg.rtp_timeout = 0;

	node->config_ = cfg;

	// 设置 pull_session_ 的编码信息
	node->session_origin_pull_.codec = "H264";
	node->session_origin_pull_.payload_type = 96;
	node->session_origin_pull_.clock_rate = 90000;
	node->session_origin_pull_.sps = sps;
	node->session_origin_pull_.pps = pps;
	node->session_origin_pull_.video_ssrc = 0x4C4F4341;

	// 构建 fmtp（包含 sprop-parameter-sets）
	if (!sps.empty() && !pps.empty()) {
		std::string spsB64 = StreamNode::base64Encode(std::string((char*)sps.data(), sps.size()));
		std::string ppsB64 = StreamNode::base64Encode(std::string((char*)pps.data(), pps.size()));
		// sps 存储完整 NAL 单元（含 NAL header），profile-level-id 对应 SPS RBSP 第 1-3 字节
		char profileId[8];
		if (sps.size() >= 4) {
			snprintf(profileId, sizeof(profileId), "%02X%02X%02X", sps[1], sps[2], sps[3]);
		} else {
			snprintf(profileId, sizeof(profileId), "42C01F");
		}
		node->session_origin_pull_.fmtp = std::string("profile-level-id=") + profileId
			+ ";packetization-mode=1;sprop-parameter-sets="
			+ spsB64 + "," + ppsB64;
	}

	node->session_origin_pull_.session_type_ = ORIGIN_PULL;
	node->session_origin_pull_.control_url = "trackID=0";
	node->isPulling_ = true;
	node->running_ = true;
	node->state_ = StreamNode::State::PLAYING;

	// 继续持有 node 引用，供下方 ctx->node 使用
	// （三个 shared_ptr 共同管理生命周期：map、局部变量 node、ctx->node）

	// 加入全局 map
	{
		std::lock_guard<std::mutex> lock(nodeLock_);
		m_mapStreamNodes[url] = node;
	}

	// 创建本地流上下文并启动喂流线程
	auto ctx = std::make_shared<LocalFileStreamCtx>();
	ctx->nals = std::move(nals);
	ctx->sps = sps;
	ctx->pps = pps;
	ctx->payload_type = 96;
	ctx->clock_rate = 90000;
	// 从 SPS VUI 自动检测帧率，检测失败使用默认值 25
	{
		double detectedFps = parseSpsFps(sps);
		if (detectedFps > 0.0) {
			ctx->fps = (int)(detectedFps + 0.5);
			LOG("[LocalFileStream] Detected fps=%d from SPS VUI", ctx->fps);
		} else {
			LOG("[LocalFileStream] SPS VUI timing not available, using default fps=%d", ctx->fps);
		}
	}
	ctx->node = node;
		ctx->running_ = true;
	
		{
			std::lock_guard<std::mutex> lock(g_localStreamMutex);
			g_localStreams[url] = ctx;
		}
	
		ctx->feed_thread_ = std::thread(localFileFeedLoop, ctx);
		ctx->feed_thread_.detach();

	//LOG("[LocalFileStream] Started serving: file=%s, url=%s, tag=%s, nals=%zu",
	//	filePath.c_str(), url.c_str(), tag.c_str(), ctx->nals.size());
	return true;
}

// ============================================================================
// 默认文件夹流服务：递归遍历 tds.exe 同级目录下的 rtsp 文件夹，
// 按文件夹路径结构作为 RTSP url，对外提供所有 h264 文件流媒体服务。
// 启动时立即解析文件、创建 StreamNode、启动喂流线程（线程内判断客户端，空转等待）。
// ============================================================================
void StreamServer::serveDefaultFolder(const std::string& folderName) {
	std::string baseDir = fs::appPath() + "/" + folderName;

	if (!std::filesystem::exists(baseDir) || !std::filesystem::is_directory(baseDir)) {
		LOG("[DefaultFolder] Folder not found: %s, skip", baseDir.c_str());
		return;
	}

	LOG("[DefaultFolder] Scanning folder: %s", baseDir.c_str());

	int count = 0;
	try {
		for (const auto& entry : std::filesystem::recursive_directory_iterator(baseDir)) {
			if (!entry.is_regular_file()) continue;

			std::string filePath = entry.path().string();
			// 只处理 .h264 文件（忽略大小写）
			if (filePath.size() < 5) continue;
			std::string ext = filePath.substr(filePath.size() - 5);
			for (size_t j = 0; j < ext.size(); j++)
				ext[j] = (char)tolower((unsigned char)ext[j]);
			if (ext != ".h264") continue;

			// 获取相对路径，去除 .h264 后缀作为 tag
			std::string relPath = filePath.substr(baseDir.size());
			while (!relPath.empty() && (relPath[0] == '/' || relPath[0] == '\\')) {
				relPath = relPath.substr(1);
			}
			for (size_t j = 0; j < relPath.size(); j++) {
				if (relPath[j] == '\\') relPath[j] = '/';
			}
			if (relPath.size() > 5)
				relPath = relPath.substr(0, relPath.size() - 5);

			std::string tag = relPath;
			std::string url = "/" + relPath;

			// 注册文件映射（保留兼容）
			{
				std::lock_guard<std::mutex> lock(m_localFileMapMutex_);
				m_localFileMap[url] = filePath;
			}
			LOG("[DefaultFolder] Mapped: %s -> %s", filePath.c_str(), url.c_str());

			// 立即创建 StreamNode 并启动喂流线程（线程内部判断客户端，空转等待）
			serveLocalStreamFile(filePath, url);
			count++;
		}
	} catch (const std::exception& e) {
		LOG("[DefaultFolder] Error scanning: %s", e.what());
	}

	LOG("[DefaultFolder] Registered %d h264 file streams from %s", count, baseDir.c_str());
}

// 按需加载本地文件流：有客户端拉流时才读文件、创建 StreamNode、启动喂流线程
std::shared_ptr<StreamNode> StreamServer::loadLocalFileStream(const std::string& tag) {
	// 先检查是否已存在
	auto existing = getStreamNodeByTag(tag);
	if (existing) return existing;

	// 从映射表查找文件路径
	std::string filePath;
	{
		std::lock_guard<std::mutex> lock(m_localFileMapMutex_);
		// 尝试 /tag 格式匹配
		std::string key = "/" + tag;
		auto it = m_localFileMap.find(key);
		if (it != m_localFileMap.end()) {
			filePath = it->second;
		}
	}
	if (filePath.empty()) return nullptr;

	// 解析文件
	std::vector<std::vector<uint8_t>> nals;
	std::vector<uint8_t> sps, pps;
	if (!parseH264File(filePath, nals, sps, pps)) {
		LOG("[LocalFileStream] Failed to parse: %s", filePath.c_str());
		return nullptr;
	}

	// 创建 StreamNode
	auto node = std::make_shared<StreamNode>();
	StreamNode::Config cfg;
	cfg.tag = tag;
	cfg.origin_pull_url = "file://" + filePath;
	cfg.relay_push_url = "";
	cfg.retry_interval = 0;
	cfg.max_retries = 0;
	cfg.rtp_timeout = 0;
	node->config_ = cfg;

	node->session_origin_pull_.codec = "H264";
	node->session_origin_pull_.payload_type = 96;
	node->session_origin_pull_.clock_rate = 90000;
	node->session_origin_pull_.sps = sps;
	node->session_origin_pull_.pps = pps;
	node->session_origin_pull_.video_ssrc = 0x4C4F4341;

	if (!sps.empty() && !pps.empty()) {
		std::string spsB64 = StreamNode::base64Encode(std::string((char*)sps.data(), sps.size()));
		std::string ppsB64 = StreamNode::base64Encode(std::string((char*)pps.data(), pps.size()));
		// sps 存储完整 NAL 单元（含 NAL header），profile-level-id 对应 SPS RBSP 第 1-3 字节
		char profileId[8];
		if (sps.size() >= 4) {
			snprintf(profileId, sizeof(profileId), "%02X%02X%02X", sps[1], sps[2], sps[3]);
		} else {
			snprintf(profileId, sizeof(profileId), "42C01F");
		}
		node->session_origin_pull_.fmtp = std::string("profile-level-id=") + profileId
			+ ";packetization-mode=1;sprop-parameter-sets="
			+ spsB64 + "," + ppsB64;
	}

	node->session_origin_pull_.session_type_ = ORIGIN_PULL;
	node->session_origin_pull_.control_url = "trackID=0";
	node->isPulling_ = true;
	node->running_ = true;
	node->state_ = StreamNode::State::PLAYING;

	// 继续持有 node 引用，供下方 ctx->node 使用
	// （三个 shared_ptr 共同管理生命周期：map、局部变量 node、ctx->node）
	{
		std::lock_guard<std::mutex> lock(nodeLock_);
		m_mapStreamNodes[tag] = node;
	}

	// 创建本地流上下文并启动喂流线程
	auto ctx = std::make_shared<LocalFileStreamCtx>();
	ctx->nals = std::move(nals);
	ctx->sps = sps;
	ctx->pps = pps;
	ctx->payload_type = 96;
	ctx->clock_rate = 90000;
	// 从 SPS VUI 自动检测帧率，检测失败使用默认值 25
	{
		double detectedFps = parseSpsFps(sps);
		if (detectedFps > 0.0) {
			ctx->fps = (int)(detectedFps + 0.5);
			LOG("[LocalFileStream] Detected fps=%d from SPS VUI", ctx->fps);
		} else {
			LOG("[LocalFileStream] SPS VUI timing not available, using default fps=%d", ctx->fps);
		}
	}
	ctx->node = node;
	ctx->running_ = true;

	{
		std::lock_guard<std::mutex> lock(g_localStreamMutex);
		g_localStreams[tag] = ctx;
	}

	ctx->feed_thread_ = std::thread(localFileFeedLoop, ctx);
	ctx->feed_thread_.detach();

	//LOG("[LocalFileStream] Loaded on demand: file=%s, tag=%s, nals=%zu",
	//	filePath.c_str(), tag.c_str(), ctx->nals.size());
	return node;
}

// 检查并停止没有客户端的本地文件流
void StreamServer::cleanupIdleLocalStream(const std::string& tag) {
	auto node = getStreamNodeByTag(tag);
	if (!node) return;

	// 检查是否还有客户端
	node->session_list_client_pull_mutex_.lock();
	bool hasClients = !node->session_list_client_pull_.empty();
	node->session_list_client_pull_mutex_.unlock();

	if (hasClients) return;

	// 没有客户端了，停止喂流线程
	// 只处理本地文件流：推流(push)创建的 StreamNode 不在此管理
	bool isLocalStream = false;
	{
		std::lock_guard<std::mutex> lock(g_localStreamMutex);
		auto it = g_localStreams.find(tag);
		if (it != g_localStreams.end()) {
			it->second->running_ = false;
			g_localStreams.erase(it);
			isLocalStream = true;
		}
	}

	if (!isLocalStream) {
		LOG("[LocalFileStream] Tag=%s is not a local file stream, skip cleanup", tag.c_str());
		return;
	}

	// 移除 StreamNode
	{
		std::lock_guard<std::mutex> lock(nodeLock_);
		m_mapStreamNodes.erase(tag);
	}

	LOG("[LocalFileStream] Cleaned idle stream: tag=%s", tag.c_str());
}
