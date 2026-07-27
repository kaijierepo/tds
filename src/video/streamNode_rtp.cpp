// ============================================================================
// streamNode_rtp.cpp - RTP 流传输层
// 包含：RTP 接收/解析/序列化、缓冲区管理、RTP 分发/转发（含 WebRTC SRTP）、
//       录像（H.264 录制到文件）
// ============================================================================

#include "streamNode.h"
#include "streamServer.h"
#include "streamNode_webrtc.h"
#include "dtls_transport.h"
#include "srtp_protect.h"
#include <fstream>
#include <cstring>
#include <functional>
#include <logger.h>

// ============================================================================
// RTP 缓冲区管理
// ============================================================================

int StreamNode::getBufferedSeconds()
{
    // 计算当前 rtp_buffer_ 中的数据覆盖的大致秒数
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (rtp_buffer_.empty()) return 0;

    // 以最早包和最新包的 RTP timestamp 差值为基准计算（无符号，支持回绕）
    uint32_t newest_ts = rtp_buffer_.back()->timestamp;
    uint32_t oldest_ts = rtp_buffer_.front()->timestamp;
    uint32_t diff = newest_ts - oldest_ts; // 无符号差值，处理 32 位回绕

    // 获取时钟频率（ticks per second），RTCP/SDP 中给出，视频常见为 90000
    uint32_t clock = (session_origin_pull_.clock_rate > 0) ? static_cast<uint32_t>(session_origin_pull_.clock_rate) : 90000u;
    if (clock == 0) clock = 90000u;

    // 整数秒（截断子秒）。如果存在刻度差但小于1秒，返回1以提示非空缓冲
    int seconds = static_cast<int>(diff / clock);
    if (diff > 0 && seconds == 0) seconds = 1;
    return seconds;
}

void StreamNode::addToRtpBuffer(std::shared_ptr<RTPPacket> pPkt)
{
    // 校验输入
    if (!pPkt) return;

    // 使用共享指针直接保存引用，无需深拷贝对象。调用者共享同一份数据，
    // 引用计数自动管理生命周期，可安全地在多个消费者间传递。
    std::lock_guard<std::mutex> lock(queue_mutex_);
    rtp_buffer_.push_back(pPkt);

    // 根据 rtp_buffer_max_seconds_ 修剪缓冲区，尽量保持约定秒数的媒体数据。
    if (rtp_buffer_max_seconds_ > 0 && !rtp_buffer_.empty()) {
        uint32_t newest_ts = rtp_buffer_.back()->timestamp;

        // 获取时钟频率（每秒刻度数），若 SDP 未提供则默认使用常见的视频值 90000Hz。
        uint32_t clock = (session_origin_pull_.clock_rate > 0) ?
            static_cast<uint32_t>(session_origin_pull_.clock_rate) : 90000u;
        if (clock == 0) clock = 90000u;

        // 当最旧包到最新包的时间差超过配置的秒数窗口时，逐个删除最旧包。
        while (!rtp_buffer_.empty()) {
            auto oldest = rtp_buffer_.front();
            uint32_t oldest_ts = oldest->timestamp;

            // 无符号相减可以按模 2^32 处理 timestamp 回绕情况。
            uint32_t diff = newest_ts - oldest_ts;

            // 将刻度差转换为整秒（会截断小数部分）。
            uint32_t seconds = diff / clock;

            if (seconds > static_cast<uint32_t>(rtp_buffer_max_seconds_)) {
                // 包已超出时间窗口 -> 从缓冲中移除。
                rtp_buffer_.erase(rtp_buffer_.begin());
            }
            else {
                // 当前最旧包在窗口内，停止修剪。
                break;
            }
        }
    }

    // 额外的保护：强制限制缓冲包数量到 max_queue_size_，以防 timestamp
    // 逻辑失效或时钟信息错误导致内存无限增长。
    while (rtp_buffer_.size() > max_queue_size_) {
        rtp_buffer_.erase(rtp_buffer_.begin());
    }
}

// ============================================================================
// RTP 工作线程
// ============================================================================

void StreamNode::rtpHandleThread() {
    StreamNode::doRtpRecv();
}

bool StreamNode::checkIsIdrNalu(const RTPPacket& packet) {
    bool isIdrNalu = false;
    if (!packet.payload.empty()) {
        uint8_t nalHeader = packet.payload[0];
        uint8_t nalType = nalHeader & 0x1F;
        if (nalType == NAL_TYPE_IDR) {
            isIdrNalu = true;
        }
        else if (nalType == NAL_TYPE_FU_A && packet.payload.size() > 1) {
            // FU-A: 第二个字节是 FU header，其中低 5 位是 NAL type
            // FU-A: 单个nalu拆多个rtp packet
            uint8_t fuHeader = packet.payload[1];
            uint8_t fuNalType = fuHeader & 0x1F;
            if (fuNalType == NAL_TYPE_IDR) {
                isIdrNalu = true;
            }
        }
        else if (nalType == NAL_TYPE_STAP_A && packet.payload.size() > 2) {
            // STAP-A: [NAL header(1B)][NALU1 size(2B)][NALU1 data...]...
            // STAP-A: 一个rtp packet多个nalu，若含SPS/PPS则忽略整个包
            uint8_t firstNalType = packet.payload[3] & 0x1F;
            if (firstNalType == NAL_TYPE_IDR) {
                isIdrNalu = true;
            }
        }
    }
    return isIdrNalu;
}

void StreamNode::doRtpRecv() {
    setState(SESSION_STATE::SESSION_STREAMING, "Streaming started");
    bool pullUDP = (session_origin_pull_.transport_mode == TransportMode::UDP);
    LOG("[keyinfo][StreamNode]tag=%s,Pull Success,rtp handle thread start,mode:%s",config_.tag.c_str(),pullUDP ? "UDP" : "TCP");

    std::vector<uint8_t> buffer(session_origin_pull_.buffer_size_);
    std::string src_ip;
    int src_port = 0;

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.last_frame_time = std::chrono::steady_clock::now();
    }

    // TCP拉流模式下的状态
    int tcpRtpChannel = 0;
    int tcpRtcpChannel = 1;
    std::vector<uint8_t> tcpBuffer;
    bool waitingForRtpData = true;

    while (running_ && !stopping_) {
        int received = 0;
        
        if (pullUDP) {
            // UDP拉流 最多阻塞1秒 configureUDPSocket 中设置了1秒超时
            received = receiveUDPData(buffer.data(), buffer.size(), src_ip, src_port);
        }
        else {
            // TCP拉流：通过RTSP连接接收RTP数据
            if (!session_origin_pull_.conn_ || !session_origin_pull_.conn_->isConnected()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // 接收数据
            char tmpBuf[2048] = {0};
            int n = session_origin_pull_.conn_->receive(tmpBuf, sizeof(tmpBuf), 100);
            
            if (n > 0) {
                // 添加到缓冲区
                tcpBuffer.insert(tcpBuffer.end(), (uint8_t*)tmpBuf, (uint8_t*)tmpBuf + n);
                
                // 处理RTP包 ( interleaved = $ + channel + len + data )
                while (tcpBuffer.size() >= 4) {
                    if (tcpBuffer[0] != 0x24) {
                        // 不是interleaved标记，跳过
                        tcpBuffer.erase(tcpBuffer.begin());
                        continue;
                    }
                    
                    int channel = tcpBuffer[1];
                    int len = (tcpBuffer[2] << 8) | tcpBuffer[3];
                    
                    if (tcpBuffer.size() < 4 + len) {
                        // 数据不完整，等待更多数据
                        break;
                    }
                    
                    // 检查是否是RTP数据 (channel 0)
                    if (channel == tcpRtpChannel) {
                        // 复制RTP数据
                        memcpy(buffer.data(), &tcpBuffer[4], len);
                        received = len;
                    }
                    
                    // 移除已处理的数据
                    tcpBuffer.erase(tcpBuffer.begin(), tcpBuffer.begin() + 4 + len);
                }
            }
            else if (n < 0) {
                // 接收错误
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (received > 12) {  // RTP包最小12字节头
            const auto now = std::chrono::steady_clock::now();

            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_received += received;
                stats_.frames_received++;
                stats_.last_frame_time = now;
            }

            // 解析RTP包
            auto pPkt = std::make_shared<RTPPacket>();
            RTPPacket& packet = *pPkt;
            if (packet.parse(buffer.data(), received)) {
                // 捕获实际 SSRC（用于 SDP 声明，只记录一次）
                if (session_origin_pull_.video_ssrc == 0 && packet.ssrc != 0) {
                    session_origin_pull_.video_ssrc = packet.ssrc;
                    LOG("[StreamNode] Captured video SSRC=%u", packet.ssrc);
                }

                // 放入缓存
                addToRtpBuffer(pPkt);

                // 转发推流
                if (isPushing_) {
                    forwardRTPPacket(packet);
                }

                //发送给拉流客户端
                sendRTPPacketToClients(packet);


                // 录制：推入队列，由独立 I/O 线程异步写盘（与实时收包线程解耦）
                {
                    std::lock_guard<std::recursive_mutex> lock(rec_mutex_);
                    if (!rec_ctrl_.recording) continue;  // 双重检查：锁获取期间 recording 可能已被 stopRecord 置 false
                    if (rec_ctrl_.firstWrite && !rec_ctrl_.preRecordingDone) {
                        //从rtp_buffer_取出rec_ctrl_.preSeconds的数据并录制（仅一次）
                        std::vector<std::shared_ptr<RTPPacket>> pre_packets;
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            uint32_t _clock = (session_origin_pull_.clock_rate > 0) ? static_cast<uint32_t>(session_origin_pull_.clock_rate) : 90000u;
                            for (auto it = rtp_buffer_.rbegin(); it != rtp_buffer_.rend(); ++it) {
                                if (packet.timestamp - (*it)->timestamp <= static_cast<uint64_t>(rec_ctrl_.preSeconds) * _clock) {
                                    pre_packets.push_back(*it);
                                }
                                else {
                                    break;
                                }
                            }
                        }
                        for (auto it = pre_packets.rbegin(); it != pre_packets.rend(); ++it) {
                            recordRTPPacket(*it);
                        }
                        rec_ctrl_.preRecordingDone = true;
                    }
                    // 无论是否写过预录数据，当前包都要录制
                    recordRTPPacket(pPkt);
                }

                //logVerbose((pullUDP ? "UDP" : "TCP") + std::string("->RTP: seq=") + 
                //    std::to_string(packet.sequence_number) +
                //    " ts=" + std::to_string(packet.timestamp) +
                //    " size=" + std::to_string(received));
            }
        }
        else if (received > 0 && received<=12) {
            printf("[StreamNode]rtp handle thread,wrong recv len");
        }
        else {
            // 检查RTP超时
            auto now = std::chrono::steady_clock::now();
            std::chrono::steady_clock::time_point last_frame_time;
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                last_frame_time = stats_.last_frame_time;
            }

            if (now - last_frame_time > std::chrono::milliseconds(session_origin_pull_.rtp_timeout_)) {
                logError("RTP timeout detected");
                setError("RTP timeout", 1001);
                break;
            }
        }
    }

    isPulling_ = false;
    LOG("[StreamNode]Pull thread stopped,tag= %s ",config_.tag.c_str());
}

// ============================================================================
// RTP 分发与转发
// ============================================================================

void StreamNode::sendRTPPacketToClients(const RTPPacket& packet) {
    if (packet.payload.empty())
        return;

    std::vector<std::shared_ptr<STREAM_SESSION>> playClients;
    session_list_client_pull_mutex_.lock();
    playClients = session_list_client_pull_;
    session_list_client_pull_mutex_.unlock();
    // 序列化RTP包
    auto data = packet.serialize();

    // 检测当前包是否包含 IDR NAL（用于在 IDR 前插入 SPS/PPS）
    bool isIdrNalu = checkIsIdrNalu(packet);
    bool isLastIdrNalu = last_nalu_was_idr_;
    last_nalu_was_idr_ = isIdrNalu;

    // 关键帧缓存：跟踪最新 IDR 帧的 RTP 数据，新会话首次发送时使用
    if (isLastIdrNalu == false && isIdrNalu == true) {
        keyframe_cache_.clear();
    }
    if (isIdrNalu) {
		keyframe_cache_.push_back(data);
    }

	// rtsp客户端发送路径：遍历所有拉流客户端，按其传输模式（UDP/TCP）发送 RTP 数据
    for (size_t i = 0; i < playClients.size(); i++) {
        auto& sp = playClients[i];
        if (!sp) continue;
        STREAM_SESSION& client = *sp;
        // WebRTC 客户端走 SRTP 路径，此处跳过明文发送
        if (client.is_webrtc) continue;
        if (client.transport_mode == TransportMode::UDP) {
            // UDP推流（RTSP 明文）
            if (sendUDPDataToSession(data.data(), data.size(), client)) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_forwarded += data.size();
                stats_.frames_forwarded++;
            }
        }
        else if (client.transport_mode == TransportMode::TCP) {
            // TCP interleaved 发送：$channel length data 格式
            if (client.tcp_socket != kInvalidSocket && client.interleaved_rtp >= 0) {
                uint8_t header[4];
                header[0] = 0x24;  // $ magic byte
                header[1] = (uint8_t)(client.interleaved_rtp & 0xFF);
                header[2] = (uint8_t)((data.size() >> 8) & 0xFF);
                header[3] = (uint8_t)(data.size() & 0xFF);

#ifdef _WIN32
                int sent = ::send(client.tcp_socket, (const char*)header, 4, 0);
                if (sent == 4) {
                    sent = ::send(client.tcp_socket, (const char*)data.data(),
                        (int)data.size(), 0);
                }
#else
                int sent = ::send(client.tcp_socket, header, 4, MSG_NOSIGNAL);
                if (sent == 4) {
                    sent = ::send(client.tcp_socket, data.data(), data.size(), MSG_NOSIGNAL);
                }
#endif
                if (sent == (int)data.size()) {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.bytes_forwarded += data.size();
                    stats_.frames_forwarded++;
                }
            }
        }
    }

    // === WebRTC SRTP 发送路径 ===
    // 遍历所有 client_sessions_，对 DTLS+SRTP 已完成的会话通过 SRTP 加密后发送视频
    uint8_t nalType = packet.payload[0] & 0x1F;

    for (auto& session : playClients) {
        if (!session || !session->is_webrtc) continue;
        // state: streaming=SRTP激活
        if (session->state_ != SESSION_STATE::SESSION_STREAMING) continue;

        // 通过 SessionDtlsState 正确访问 DTLS 和 SRTP 上下文
        // dtls_transport_ 为 shared_ptr（由 ICE 线程管理），原子加载快照后持有引用，
        // 避免 ICE 线程释放后发送线程解引用产生 Use-After-Free
        std::shared_ptr<SessionDtlsState> dtlsState_shared = std::atomic_load(&session->dtls_transport_);
        SessionDtlsState* dtlsState = dtlsState_shared.get();
        if (!dtlsState || !dtlsState->srtp_ready) continue;
        if (!dtlsState->dtls.isPeerSet()) continue;

        DtlsTransport& dtls = dtlsState->dtls;
        SrptProtect::Context& srtpCtx = dtlsState->srtp_ctx;

        // 从源流动态缓存 SPS/PPS（当相机 SDP 不含 sprop-parameter-sets 时，
        // 后续的 IDR 前插入和首次发送逻辑依赖缓存的 SPS/PPS）
        // 注意：SPS/PPS 可能以 Single NAL、STAP-A 或 FU-A 格式到达
        if (nalType == NAL_TYPE_SPS) {
            session->sps = packet.payload;
        } else if (nalType == NAL_TYPE_PPS) {
            session->pps = packet.payload;
        } else if (nalType == NAL_TYPE_STAP_A && packet.payload.size() > 3) {
            // STAP-A: [STAP-A header(1B)] [NALU1 size(2B)] [NALU1 data] ...
            size_t off = 1;
            while (off + 2 <= packet.payload.size()) {
                uint16_t L = (packet.payload[off] << 8) | packet.payload[off + 1];
                off += 2;
                if (L == 0 || off + L > packet.payload.size()) break;
                uint8_t subType = packet.payload[off] & 0x1F;
                if (subType == NAL_TYPE_SPS) {
                    session->sps = std::vector<uint8_t>(
                        packet.payload.begin() + off,
                        packet.payload.begin() + off + L);
                } else if (subType == NAL_TYPE_PPS) {
                    session->pps = std::vector<uint8_t>(
                        packet.payload.begin() + off,
                        packet.payload.begin() + off + L);
                }
                off += L;
            }
        } else if (nalType == NAL_TYPE_FU_A && packet.payload.size() > 1) {
            // FU-A 极少用于 SPS/PPS（SPS/PPS 通常很小无需分片），暂不处理
        }

        // 初始化 per-session 序列号（以原始流第一个包的 seq 为基准）
        if (!dtlsState->seq_inited) {
            dtlsState->local_seq = packet.sequence_number;
            dtlsState->seq_inited = true;
        }

        auto now_steady = std::chrono::steady_clock::now();

        // 确定本会话使用的 SSRC，必须与 SDP Answer 中声明的 a=ssrc: 一致
        uint32_t sessionSsrc = session->video_ssrc ? session->video_ssrc : 1;

        // 安全 MTU 上限：单个 RTP 负载（不含 12B RTP 头与 SRTP 认证标签）最大字节数。
        // 取 1180 可保证叠加 SRTP 标签后在标准/隧道 MTU 下均不超 1500，避免弱网大包被丢弃。
        const size_t kMaxRtpPayload = 1180;

        // 关键帧最大发送间隔(ms)：超过则主动重发缓存的 IDR，限制解码卡顿的最长自愈时间，
        // 不依赖浏览器是否发送 PLI。源端 GOP 已足够密时本逻辑不会触发。
        const int kMaxIdrGapMs = 2000;

        // 发送一个已构建好的 RTP 包（rtp[0..1] 含 V/PT/marker，rtp[4..11] 含 ts/ssrc 已填）：
        // 在此填入 per-session 序列号、SRTP 加密并发送，随后序列号自增。
        auto sendBuiltRtp = [&](std::vector<uint8_t>& rtp) {
            uint16_t sentSeq = dtlsState->local_seq;
            rtp[2] = (dtlsState->local_seq >> 8) & 0xFF;
            rtp[3] = dtlsState->local_seq & 0xFF;
            dtlsState->local_seq++;
            auto srtpPkt = SrptProtect::protect(srtpCtx, rtp);
            if (srtpPkt.empty()) return;

            // 缓存已加密的 SRTP 包，供收到 NACK 时原样重传（修复弱网单包丢失导致的花屏）
            {
                std::lock_guard<std::mutex> lk(dtlsState->rtx_mutex_);
                RtxCachedPacket& slot =
                    dtlsState->rtx_buf_[sentSeq % SessionDtlsState::kRtxBufSize];
                slot.seq = sentSeq;
                slot.valid = true;
                slot.data = srtpPkt;
            }

            const struct sockaddr_in& peerAddr = dtls.getPeerAddr();
            int sent = sendto(session->rtp_socket,
                (const char*)srtpPkt.data(), (int)srtpPkt.size(), 0,
                (const struct sockaddr*)&peerAddr, sizeof(peerAddr));
            if (sent < 0) {
#ifdef _WIN32
                int err = WSAGetLastError();
                LOG("[SRTP] sendto FAILED, err=%d, pkt=%zu", err, srtpPkt.size());
#else
                LOG("[SRTP] sendto FAILED, errno=%d, pkt=%zu", errno, srtpPkt.size());
#endif
            }
            if (sent > 0) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_forwarded += srtpPkt.size();
                stats_.frames_forwarded++;
            }
        };

        // 把一个 H.264 NALU（含其 NAL 头字节）重新打包成一个或多个 RTP 包发送：
        // Single NAL（≤kMaxRtpPayload）直接发送；更大则用 FU-A 切分。
        // STAP-A 聚合包先拆成单 NAL 逐发；相机已 FU-A 分片但单片仍过大的，按片再切小。
        std::function<void(const std::vector<uint8_t>&, uint32_t, bool)> sendH264Nalu =
            [&](const std::vector<uint8_t>& nalu, uint32_t ts, bool marker) {
            if (nalu.empty()) return;
            uint8_t nt = nalu[0] & 0x1F;

            // STAP-A：拆成多个单 NAL 逐发（marker 仅落在最后一个子 NALU 上）
            if (nt == NAL_TYPE_STAP_A) {
                size_t off = 1;
                while (off + 2 <= nalu.size()) {
                    uint16_t L = (uint16_t(nalu[off]) << 8) | uint16_t(nalu[off + 1]);
                    off += 2;
                    if (L == 0 || off + L > nalu.size()) break;
                    bool isLast = (off + L >= nalu.size());
                    std::vector<uint8_t> sub(nalu.begin() + off, nalu.begin() + off + L);
                    sendH264Nalu(sub, ts, marker && isLast);
                    off += L;
                }
                return;
            }

            // 相机已 FU-A 分片：单 RTP 包内负载(含 2B FU 头)未超上限则原样重映射发送；
            // 仍过大（弱网隧道场景）时按片再切成更小的 FU-A。
            if (nt == NAL_TYPE_FU_A) {
                if (nalu.size() <= kMaxRtpPayload) {
                    std::vector<uint8_t> rtp(12 + nalu.size());
                    rtp[0] = 0x80;
                    rtp[1] = (marker ? 0x80 : 0x00) | (session->payload_type & 0x7F);
                    rtp[4] = (ts >> 24) & 0xFF; rtp[5] = (ts >> 16) & 0xFF;
                    rtp[6] = (ts >> 8) & 0xFF;  rtp[7] = ts & 0xFF;
                    rtp[8] = (sessionSsrc >> 24) & 0xFF; rtp[9] = (sessionSsrc >> 16) & 0xFF;
                    rtp[10] = (sessionSsrc >> 8) & 0xFF;  rtp[11] = sessionSsrc & 0xFF;
                    memcpy(&rtp[12], nalu.data(), nalu.size());
                    sendBuiltRtp(rtp);
                }
                else {
                    uint8_t nri = nalu[0] & 0x60;
                    uint8_t fuType = nalu[1] & 0x1F;
                    bool camS = (nalu[1] & 0x80) != 0;
                    bool camE = (nalu[1] & 0x40) != 0;
                    size_t dataLen = nalu.size() - 2;
                    size_t chunkMax = kMaxRtpPayload - 2;
                    size_t pos = 0;
                    while (pos < dataLen) {
                        size_t frag = (dataLen - pos > chunkMax) ? chunkMax : (dataLen - pos);
                        bool isFirst = (pos == 0) && camS;
                        bool isLast = (pos + frag >= dataLen) && camE;
                        std::vector<uint8_t> rtp(12 + 2 + frag);
                        rtp[0] = 0x80;
                        rtp[1] = ((isLast && marker) ? 0x80 : 0x00) | (session->payload_type & 0x7F);
                        rtp[4] = (ts >> 24) & 0xFF; rtp[5] = (ts >> 16) & 0xFF;
                        rtp[6] = (ts >> 8) & 0xFF;  rtp[7] = ts & 0xFF;
                        rtp[8] = (sessionSsrc >> 24) & 0xFF; rtp[9] = (sessionSsrc >> 16) & 0xFF;
                        rtp[10] = (sessionSsrc >> 8) & 0xFF;  rtp[11] = sessionSsrc & 0xFF;
                        rtp[12] = 0x1C | nri;   // FU indicator, type=28
                        rtp[13] = (uint8_t)(fuType | (isFirst ? 0x80 : 0) | (isLast ? 0x40 : 0));
                        memcpy(&rtp[14], &nalu[2 + pos], frag);
                        sendBuiltRtp(rtp);
                        pos += frag;
                    }
                }
                return;
            }

            // Single NAL（或未识别类型）：过大则 FU-A 切分，否则直接发送
            if (nalu.size() <= kMaxRtpPayload) {
                std::vector<uint8_t> rtp(12 + nalu.size());
                rtp[0] = 0x80;
                rtp[1] = (marker ? 0x80 : 0x00) | (session->payload_type & 0x7F);
                rtp[4] = (ts >> 24) & 0xFF; rtp[5] = (ts >> 16) & 0xFF;
                rtp[6] = (ts >> 8) & 0xFF;  rtp[7] = ts & 0xFF;
                rtp[8] = (sessionSsrc >> 24) & 0xFF; rtp[9] = (sessionSsrc >> 16) & 0xFF;
                rtp[10] = (sessionSsrc >> 8) & 0xFF;  rtp[11] = sessionSsrc & 0xFF;
                memcpy(&rtp[12], nalu.data(), nalu.size());
                sendBuiltRtp(rtp);
            }
            else {
                uint8_t nri = nalu[0] & 0x60;
                uint8_t nalType = nalu[0] & 0x1F;
                size_t chunkMax = kMaxRtpPayload - 2;
                size_t offset = 1;
                bool first = true;
                while (offset < nalu.size()) {
                    size_t remain = nalu.size() - offset;
                    size_t frag = (remain > chunkMax) ? chunkMax : remain;
                    bool isLast = (offset + frag >= nalu.size());
                    std::vector<uint8_t> rtp(12 + 2 + frag);
                    rtp[0] = 0x80;
                    rtp[1] = ((isLast && marker) ? 0x80 : 0x00) | (session->payload_type & 0x7F);
                    rtp[4] = (ts >> 24) & 0xFF; rtp[5] = (ts >> 16) & 0xFF;
                    rtp[6] = (ts >> 8) & 0xFF;  rtp[7] = ts & 0xFF;
                    rtp[8] = (sessionSsrc >> 24) & 0xFF; rtp[9] = (sessionSsrc >> 16) & 0xFF;
                    rtp[10] = (sessionSsrc >> 8) & 0xFF;  rtp[11] = sessionSsrc & 0xFF;
                    rtp[12] = 0x1C | nri;   // FU indicator, type=28
                    rtp[13] = (uint8_t)(nalType | (first ? 0x80 : 0) | (isLast ? 0x40 : 0));
                    memcpy(&rtp[14], &nalu[offset], frag);
                    sendBuiltRtp(rtp);
                    offset += frag;
                    first = false;
                }
            }
        };

        // 发送单个 NAL（SPS/PPS 等小包），marker=0
        auto sendSingleNalRtp = [&](const std::vector<uint8_t>& nal, uint32_t ts) {
            sendH264Nalu(nal, ts, false);
        };

        // 重发缓存的关键帧（SPS+PPS+IDR 分片）。缓存的是相机原始 RTP 包（含 12B 头），
        // 需解析出 NALU 后按安全 MTU 重新分片发送，避免大 IDR 包在弱网被 MTU 丢弃。
        auto resendKeyframe = [&]() {
            if (keyframe_cache_.empty()) return;
            if (!session->sps.empty()) sendSingleNalRtp(session->sps, packet.timestamp);
            if (!session->pps.empty()) sendSingleNalRtp(session->pps, packet.timestamp);
            RTPPacket kfPkt;
            for (auto& kfData : keyframe_cache_) {
                if (!kfPkt.parse(kfData.data(), kfData.size())) continue;
                sendH264Nalu(kfPkt.payload, packet.timestamp, kfPkt.marker);
            }
        };

        // 响应浏览器 PLI/FIR：ICE 线程检测到关键帧请求后置位，转发线程在此重发关键帧
        if (dtlsState->request_keyframe_resend_) {
            dtlsState->request_keyframe_resend_ = false;
            resendKeyframe();
            dtlsState->last_idr_sent_time_ = now_steady;
            LOG("[WebRTC] keyframe resend triggered by client feedback");
        }

        // 主动重发：距上次发送关键帧超过阈值即重发缓存 IDR，限制解码卡顿的最长自愈时间，
        // 不依赖浏览器是否发送 PLI（部分浏览器/场景下不会发）。源端 GOP 足够密时不触发。
        if (now_steady - dtlsState->last_idr_sent_time_ >= std::chrono::milliseconds(kMaxIdrGapMs)) {
            resendKeyframe();
            dtlsState->last_idr_sent_time_ = now_steady;
            LOG("[WebRTC] periodic keyframe resend (gap>%dms)", kMaxIdrGapMs);
        }

        // 新会话首次发送：SPS + PPS + 缓存的关键帧 RTP 数据
        if (session->is_first_send_) {
            session->is_first_send_ = false;
            resendKeyframe();
            dtlsState->last_idr_sent_time_ = now_steady;
            LOG("first send: sps/pps + %zu keyframe pkts for session", keyframe_cache_.size());
        }
        // 每个IDR之前发送 SPS/PPS
        else if (isIdrNalu && isLastIdrNalu == false && !session->sps.empty() && !session->pps.empty()) {
            sendSingleNalRtp(session->sps, packet.timestamp);
            sendSingleNalRtp(session->pps, packet.timestamp);
            dtlsState->last_idr_sent_time_ = now_steady;
            LOG("send sps/pps");
        }

        // 重新分片发送本帧 H.264 负载（安全 MTU，解决弱网大包被丢弃导致的冻结/花屏）
        sendH264Nalu(packet.payload, packet.timestamp, packet.marker);

    }
}

void StreamNode::forwardRTPPacket(const RTPPacket & packet) {
    // 序列化RTP包
    auto data = packet.serialize();
    
    if (session_relay_push_.transport_mode == TransportMode::UDP) {
        // UDP推流
        if (sendUDPDataToSession(data.data(), data.size(),session_relay_push_)) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.bytes_forwarded += data.size();
            stats_.frames_forwarded++;
        }
    }
    else {
        // TCP推流：发送到目标RTSP服务器（通过RTSP控制的连接）
        if (session_relay_push_.conn_ && session_relay_push_.conn_->isConnected()) {
            // RTP over RTSP: 插入 $ (0x24) + channel + length
            uint8_t rtpOverTcp[4] = { 0x24, 0x00, 0x00, 0x00 };  // channel 0, length待定
            rtpOverTcp[2] = (data.size() >> 8) & 0xFF;
            rtpOverTcp[3] = data.size() & 0xFF;
            
            std::vector<uint8_t> tcpPacket;
            tcpPacket.insert(tcpPacket.end(), rtpOverTcp, rtpOverTcp + 4);
            tcpPacket.insert(tcpPacket.end(), data.begin(), data.end());
            
            int sent = session_relay_push_.conn_->send(tcpPacket.data(), tcpPacket.size());
            if (sent > 0) {
                session_relay_push_.rtpBytesSended += data.size();
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_forwarded += data.size();
                stats_.frames_forwarded++;
            }
        }
    }
}

// ============================================================================
// RTP 包解析与序列化
// ============================================================================

bool StreamNode::RTPPacket::parse(const uint8_t * data, size_t size) {
    if (size < 12) return false;

    version = (data[0] >> 6) & 0x03;
    padding = (data[0] >> 5) & 0x01;
    extension = (data[0] >> 4) & 0x01;
    csrc_count = data[0] & 0x0F;
    marker = (data[1] >> 7) & 0x01;
    payload_type = data[1] & 0x7F;

    sequence_number = (data[2] << 8) | data[3];
    timestamp = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    ssrc = (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];

    size_t header_size = 12 + (csrc_count * 4);
    if (extension) {
        if (size < header_size + 4) return false;
        uint16_t extension_length = (data[header_size + 2] << 8) | data[header_size + 3];
        header_size += 4 + extension_length * 4;
    }

    if (size <= header_size) return false;

    payload.assign(data + header_size, data + size);
    return true;
}

std::vector<uint8_t> StreamNode::RTPPacket::serialize() const {
    std::vector<uint8_t> data(12 + csrc_count * 4 + payload.size());

    data[0] = (version << 6) | (padding << 5) | (extension << 4) | csrc_count;
    data[1] = (marker << 7) | (payload_type & 0x7F);

    data[2] = (sequence_number >> 8) & 0xFF;
    data[3] = sequence_number & 0xFF;

    data[4] = (timestamp >> 24) & 0xFF;
    data[5] = (timestamp >> 16) & 0xFF;
    data[6] = (timestamp >> 8) & 0xFF;
    data[7] = timestamp & 0xFF;

    data[8] = (ssrc >> 24) & 0xFF;
    data[9] = (ssrc >> 16) & 0xFF;
    data[10] = (ssrc >> 8) & 0xFF;
    data[11] = ssrc & 0xFF;

    // 复制负载
    if (!payload.empty()) {
        memcpy(data.data() + 12, payload.data(), payload.size());
    }

    return data;
}

// ============================================================================
// NAL 类型描述
// ============================================================================

std::string getNALTypeDesc(unsigned char nal_type) {
    switch (nal_type) {
    case 0: return "Unspecified (0)";
    case 1: return "Non-IDR slice (Coded slice of a non-IDR picture)";
    case 2: return "Partition A (coded slice data partition A)";
    case 3: return "Partition B (coded slice data partition B)";
    case 4: return "Partition C (coded slice data partition C)";
    case 5: return "IDR slice (Instantaneous Decoding Refresh)";
    case 6: return "SEI (Supplemental enhancement information)";
    case 7: return "SPS (Sequence Parameter Set)";
    case 8: return "PPS (Picture Parameter Set)";
    case 9: return "AUD (Access Unit Delimiter)";
    case 10: return "End of sequence";
    case 11: return "End of stream";
    case 12: return "Filler data";
    case 13: return "Reserved (13)";
    case 14: return "Prefix NALU (SVAC) / Prefix";
    case 15: return "Subset SPS (SVAC)";
    case 16: return "Reserved (16)";
    case 17: return "Reserved (17)";
    case 18: return "Reserved (18)";
    case 19: return "Slice extension";
    case 20: return "Slice extension for 3D";
    case 21: return "Slice extension depth";
    case 22: return "Reserved (22)";
    case 23: return "Reserved (23)";
    case 24: return "STAP-A (Single-time aggregation packet)";
    case 25: return "STAP-B (Single-time aggregation packet, with DON)";
    case 26: return "MTAP16 (Multi-time aggregation packet, 16-bit offsets)";
    case 27: return "MTAP24 (Multi-time aggregation packet, 24-bit offsets)";
    case 28: return "FU-A (Fragmentation Unit A)";
    case 29: return "FU-B (Fragmentation Unit B)";
    case 30: return "Unspecified (30)";
    case 31: return "Unspecified (31)";
    default: {
        return std::string("Unknown/Unspecified NAL type: ") + std::to_string((int)nal_type);
    }
    }
}

// ============================================================================
// 录像（H.264 写入文件）
// ============================================================================

// 将 RTP 包推入录像队列（由独立 I/O 线程异步写盘，与实时收包线程解耦）
void StreamNode::recordRTPPacket(std::shared_ptr<RTPPacket> pPkt) {
    if (!pPkt) return;
    {
        std::lock_guard<std::mutex> lock(record_queue_mutex_);
        record_queue_.push(pPkt);
    }
    record_queue_cv_.notify_one();
}

void StreamNode::flushRecordBuffer() {
    record_io_running_ = false;
    record_queue_cv_.notify_one();
    if (record_io_thread_.joinable()) {
        record_io_thread_.join();
    }
}

// 将单个 RTP 包中的 NAL 单元写入文件（由 I/O 线程调用，不含文件打开/关闭）
void StreamNode::writeRTPPacketToFile(std::shared_ptr<RTPPacket> pPkt, std::ofstream& ofs) {
    const std::vector<uint8_t>& payload = pPkt->payload;
    if (payload.empty()) return;

    uint8_t nal_unit_type = payload[0] & 0x1F;

    if (nal_unit_type == NAL_TYPE_STAP_A && payload.size() >= 2) {
        size_t off = 1;
        while (off + 2 <= payload.size()) {
            uint16_t L = (payload[off] << 8) | payload[off + 1];
            off += 2;
            if (L == 0) continue;
            if (off + L > payload.size()) break;
            const uint8_t* subNal = &payload[off];
            uint8_t sub_nal_type = subNal[0] & 0x1F;
            writeNALtoFile(sub_nal_type, (char*)subNal, L, ofs);
            off += L;
        }
    }
    else if (nal_unit_type == NAL_TYPE_FU_A && payload.size() >= 2) {
        uint8_t fu_header = payload[1];
        bool start = (fu_header & 0x80) != 0;
        bool end   = (fu_header & 0x40) != 0;
        uint8_t fu_a_org_type = fu_header & 0x1F;
        uint8_t nal_header = (payload[0] & 0xE0) | fu_a_org_type;

        if (start) {
            rec_ctrl_.fu_a_buffer_.clear();
            rec_ctrl_.fu_a_buffer_.push_back(nal_header);
        }
        rec_ctrl_.fu_a_buffer_.insert(rec_ctrl_.fu_a_buffer_.end(),
            payload.begin() + 2, payload.end());

        if (end) {
            writeNALtoFile(fu_a_org_type, rec_ctrl_.fu_a_buffer_.data(),
                rec_ctrl_.fu_a_buffer_.size(), ofs);
        }
    }
    else {
        writeNALtoFile(nal_unit_type, (char*)payload.data(), payload.size(), ofs);
    }
}

// 录像独立 I/O 线程：从队列取包，批量写盘，常驻文件句柄
void StreamNode::recordIoThread() {
    LOG("[StreamNode] Record I/O thread started, tag=%s", config_.tag.c_str());

    std::ofstream ofs;  // 常驻文件句柄，不再每批重开
    std::vector<std::shared_ptr<RTPPacket>> batch;

    while (record_io_running_) {
        {
            std::unique_lock<std::mutex> lock(record_queue_mutex_);
            record_queue_cv_.wait_for(lock, std::chrono::milliseconds(500), [this] {
                return !record_queue_.empty() || !record_io_running_;
            });

            if (record_queue_.empty()) continue;

            while (!record_queue_.empty()) {
                batch.push_back(std::move(record_queue_.front()));
                record_queue_.pop();
            }
        }

        // 懒打开：首次写数据时才打开文件
        if (!ofs.is_open() && !rec_ctrl_.path.empty()) {
            ofs.open(rec_ctrl_.path, std::ios::binary | std::ios::app);
            if (!ofs) {
                logError("Record I/O: Failed to open file: " + rec_ctrl_.path);
                batch.clear();
                continue;
            }
        }

        for (auto& p : batch) {
            writeRTPPacketToFile(p, ofs);
        }
        ofs.flush();
        batch.clear();
    }

    // 退出前排空队列中剩余数据
    {
        std::lock_guard<std::mutex> lock(record_queue_mutex_);
        while (!record_queue_.empty()) {
            batch.push_back(std::move(record_queue_.front()));
            record_queue_.pop();
        }
    }

    if (!batch.empty()) {
        if (!ofs.is_open() && !rec_ctrl_.path.empty()) {
            ofs.open(rec_ctrl_.path, std::ios::binary | std::ios::app);
        }
        if (ofs.is_open()) {
            for (auto& p : batch) {
                writeRTPPacketToFile(p, ofs);
            }
            ofs.flush();
        }
    }

    if (ofs.is_open()) {
        ofs.flush();
        ofs.close();
    }

    LOG("[StreamNode] Record I/O thread stopped, tag=%s", config_.tag.c_str());
}

void StreamNode::writeNALtoFile(uint8_t nal_type, char* nal, size_t size, std::ofstream& ofs) {
    if (rec_ctrl_.firstWrite && nal_type == NAL_TYPE_NON_IDR) {
        return;
    }
    rec_ctrl_.firstWrite = false;

    const uint8_t start_code[4] = { 0x00, 0x00, 0x00, 0x01 };
    ofs.write((const char*)start_code, sizeof(start_code));
    ofs.write((const char*)nal, size);
}
