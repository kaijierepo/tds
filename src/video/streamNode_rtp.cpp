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

void StreamNode::doRtpRecv() {
    setState(State::PLAYING, "Streaming started");
    bool pullUDP = (config_.pull_mode == TransportMode::UDP);
    LOG("[keyinfo][StreamNode]tag=%s,Pull Success,rtp handle thread start,mode:%s",config_.tag.c_str(),pullUDP ? "UDP" : "TCP");

    std::vector<uint8_t> buffer(config_.buffer_size);
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
            if (!source_conn_ || !source_conn_->isConnected()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // 接收数据
            char tmpBuf[2048] = {0};
            int n = source_conn_->receive(tmpBuf, sizeof(tmpBuf), 100);
            
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


                // 录制到磁盘（加锁保护 rec_ctrl_ 和 record_batch_buffer_，与 rpc_startRecord/rpc_stopRecord 互斥）
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

            if (now - last_frame_time > std::chrono::milliseconds(config_.rtp_timeout)) {
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
    std::vector<std::shared_ptr<StreamNode::STREAM_SESSION>> playClients;
    session_list_client_pull_mutex_.lock();
    playClients = session_list_client_pull_;
    session_list_client_pull_mutex_.unlock();
    // 序列化RTP包
    auto data = packet.serialize();

    for (size_t i = 0; i < playClients.size(); i++) {
        auto& sp = playClients[i];
        if (!sp) continue;
        StreamNode::STREAM_SESSION& client = *sp;
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
    {
        // 检测当前包是否包含 IDR NAL（用于在 IDR 前插入 SPS/PPS）
        bool isIdr = false;
        if (!packet.payload.empty()) {
            uint8_t nalHeader = packet.payload[0];
            uint8_t nalType = nalHeader & 0x1F;
            if (nalType == NAL_TYPE_IDR) {
                isIdr = true;
            } else if (nalType == NAL_TYPE_FU_A && packet.payload.size() > 1) {
                // FU-A: 第二个字节是 FU header，其中低 5 位是 NAL type
                uint8_t fuHeader = packet.payload[1];
                uint8_t fuNalType = fuHeader & 0x1F;
                bool start = (fuHeader & 0x80) != 0; // S 位
                bool end = (fuHeader & 0x40) != 0;   // E 位
				if (fuNalType == NAL_TYPE_IDR && start) { // 只有 FU-A 的第一个包（S=1）才算是 IDR 的开始
                    isIdr = true;
                }
            } else if (nalType == NAL_TYPE_STAP_A && packet.payload.size() > 2) {
                // STAP-A: 跳过第一个 NALU 的长度字段(2B)检查
                uint8_t firstNalType = packet.payload[2] & 0x1F;
                if (firstNalType == NAL_TYPE_IDR) {
                    isIdr = true;
                }
            }
        }

        // 获取 client_sessions_ 快照（避免持锁遍历）
        std::vector<std::shared_ptr<STREAM_SESSION>> sessions;
        {
            std::lock_guard<std::mutex> lock(session_list_client_pull_mutex_);
            sessions = session_list_client_pull_;
        }
        for (auto& session : sessions) {
            if (!session || !session->is_webrtc) continue;
            // state: 3=SRTP激活（is_webrtc 下 S3_SRTP_ACTIVE 即为激活态）
            if (session->state != SESSION_STATE::S3_SRTP_ACTIVE) continue;

            // 通过 SessionDtlsState 正确访问 DTLS 和 SRTP 上下文
            auto* dtlsState = static_cast<SessionDtlsState*>(session->dtls_transport_);
            if (!dtlsState || !dtlsState->srtp_ready) continue;
            if (!dtlsState->dtls.isPeerSet()) continue;

            DtlsTransport& dtls = dtlsState->dtls;
            SrptProtect::Context& srtpCtx = dtlsState->srtp_ctx;

            // 初始化 per-session 序列号（以原始流第一个包的 seq 为基准）
            if (!dtlsState->seq_inited) {
                dtlsState->local_seq = packet.sequence_number;
                dtlsState->seq_inited = true;
            }


            // 第一个 IDR 到达前：丢弃所有非 IDR 包，避免浏览器收到无法解码的数据
            // 浏览器需要 SPS/PPS + IDR 才能初始化解码器，在此之前收到的数据全部无效
            if (!session->last_was_idr_ && !isIdr) {
                continue;
            }

            // 如果当前包是 IDR 且 session 有 SPS/PPS，且尚未发送过，则先发送 SPS/PPS RTP 包
            // sps_pps_sent_ 确保 SPS/PPS 仅在首次 IDR 前注入一次（后续 GOP 的 IDR 不需要重复注入）
            if (!session->sps_pps_sent_ && isIdr 
                && !session->sps.empty() && !session->pps.empty()) {
                // 辅助函数：发送单个 NAL 的 RTP 包，使用 per-session 独立序列号
                auto sendSingleNalRtp = [&](const std::vector<uint8_t>& nal) {
                    std::vector<uint8_t> nalData(12 + nal.size());
                    nalData[0] = 0x80;  // V=2, P=0, X=0, CC=0
                    nalData[1] = (0 << 7) | (session->payload_type & 0x7F);  // marker=0, 使用会话 PT
                    nalData[2] = (dtlsState->local_seq >> 8) & 0xFF;
                    nalData[3] = dtlsState->local_seq & 0xFF;
                    nalData[4] = (packet.timestamp >> 24) & 0xFF;
                    nalData[5] = (packet.timestamp >> 16) & 0xFF;
                    nalData[6] = (packet.timestamp >> 8) & 0xFF;
                    nalData[7] = packet.timestamp & 0xFF;
                    nalData[8]  = (packet.ssrc >> 24) & 0xFF;
                    nalData[9]  = (packet.ssrc >> 16) & 0xFF;
                    nalData[10] = (packet.ssrc >> 8) & 0xFF;
                    nalData[11] = packet.ssrc & 0xFF;
                    memcpy(&nalData[12], nal.data(), nal.size());

                    dtlsState->local_seq++;  // 递增序列号

                    auto srtpPkt = SrptProtect::protect(srtpCtx, nalData);
                    if (!srtpPkt.empty()) {
                        const struct sockaddr_in& peerAddr = dtls.getPeerAddr();
                        sendto(session->rtp_socket,
                            (const char*)srtpPkt.data(), (int)srtpPkt.size(), 0,
                            (const struct sockaddr*)&peerAddr, sizeof(peerAddr));
                    }
                };

                sendSingleNalRtp(session->sps);
                sendSingleNalRtp(session->pps);
                session->sps_pps_sent_ = true;
                LOG("SRTP: injected SPS (%zu bytes, NAL type=0x%02x) + PPS (%zu bytes, NAL type=0x%02x) before first IDR, seq_start=%u",
                    session->sps.size(),
                    session->sps.empty() ? 0 : (session->sps[0] & 0x1F),
                    session->pps.size(),
                    session->pps.empty() ? 0 : (session->pps[0] & 0x1F),
                    dtlsState->local_seq - 2);
            }
            session->last_was_idr_ = isIdr;

            // 用 per-session 独立序列号+PT 替换原始 seq/PT 后发送
            std::vector<uint8_t> rtpVec(data.begin(), data.end());
            rtpVec[1] = (rtpVec[1] & 0x80) | (session->payload_type & 0x7F);  // 保留 marker, 重映射 PT
            rtpVec[2] = (dtlsState->local_seq >> 8) & 0xFF;
            rtpVec[3] = dtlsState->local_seq & 0xFF;
            dtlsState->local_seq++;

            std::vector<uint8_t> srtpPkt = SrptProtect::protect(srtpCtx, rtpVec);
            if (srtpPkt.empty()) continue;

            const struct sockaddr_in& peerAddr = dtls.getPeerAddr();
            int sent = sendto(session->rtp_socket,
                (const char*)srtpPkt.data(), (int)srtpPkt.size(), 0,
                (const struct sockaddr*)&peerAddr, sizeof(peerAddr));

            // 前 5 次打印发送状态
            static int srtp_send_count = 0;
            if (srtp_send_count < 5) {
                char ipbuf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &peerAddr.sin_addr, ipbuf, sizeof(ipbuf));
                LOG("SRTP send #%d: sent=%d/%zu to %s:%u",
                    srtp_send_count, sent, srtpPkt.size(),
                    ipbuf, ntohs(peerAddr.sin_port));
                srtp_send_count++;
            }

            if (sent > 0) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_forwarded += srtpPkt.size();
                stats_.frames_forwarded++;
            }
        }
    }
}

void StreamNode::forwardRTPPacket(const RTPPacket & packet) {
    // 序列化RTP包
    auto data = packet.serialize();
    
    if (config_.push_mode == TransportMode::UDP) {
        // UDP推流
        if (sendUDPDataToSession(data.data(), data.size(),session_relay_push_)) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.bytes_forwarded += data.size();
            stats_.frames_forwarded++;
        }
    }
    else {
        // TCP推流：发送到目标RTSP服务器（通过RTSP控制的连接）
        if (target_conn_ && target_conn_->isConnected()) {
            // RTP over RTSP: 插入 $ (0x24) + channel + length
            uint8_t rtpOverTcp[4] = { 0x24, 0x00, 0x00, 0x00 };  // channel 0, length待定
            rtpOverTcp[2] = (data.size() >> 8) & 0xFF;
            rtpOverTcp[3] = data.size() & 0xFF;
            
            std::vector<uint8_t> tcpPacket;
            tcpPacket.insert(tcpPacket.end(), rtpOverTcp, rtpOverTcp + 4);
            tcpPacket.insert(tcpPacket.end(), data.begin(), data.end());
            
            int sent = target_conn_->send(tcpPacket.data(), tcpPacket.size());
            if (sent > 0) {
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

// h264文件分析工具 https://nalu.qer.im/
void StreamNode::recordRTPPacket(std::shared_ptr<RTPPacket> pPkt) {
    std::lock_guard<std::recursive_mutex> lock(rec_mutex_);  // 与 rpc_stopRecord 互斥，可被 doRtpRecv 重入
    if (!rec_ctrl_.recording || rec_ctrl_.path.empty()) return;

    std::vector<std::shared_ptr<RTPPacket>> to_write;
    record_batch_buffer_.push_back(pPkt);
    if (record_batch_buffer_.size() < 20) {
        return;
    }

    // 交换出待写入队列，清空原队列
    to_write.swap(record_batch_buffer_);

    // 打开文件（追加二进制）
    std::ofstream ofs(rec_ctrl_.path, std::ios::binary | std::ios::app);
    if (!ofs) {
        logError("Failed to open record file: " + rec_ctrl_.path);
        return;
    }

    for (auto p : to_write) {
        const std::vector<uint8_t>& payload = p->payload;
        if (payload.empty()) {
            continue;
        }

        uint8_t nal_unit_type = payload[0] & 0x1F;

        if (nal_unit_type == NAL_TYPE_STAP_A && payload.size() >= 2) {
            size_t off = 1;
            while (off + 2 <= payload.size()) {
                uint16_t L = (payload[off] << 8) | payload[off + 1];
                off += 2;
                if (L == 0) continue;
                if (off + L > payload.size()) break; // 不完整，退出
                const uint8_t* subNal = &payload[off];
                uint8_t sub_nal_type = subNal[0] & 0x1F;
				writeNALtoFile(sub_nal_type,(char*)subNal, L, ofs);
                off += L;
            }
        }
        else if (nal_unit_type == NAL_TYPE_FU_A && payload.size() >= 2) {
            uint8_t fu_header = payload[1];
            bool start = (fu_header & 0x80) != 0; // S 位
			bool end = (fu_header & 0x40) != 0;   // E 位
			uint8_t fu_a_org_type = fu_header & 0x1F;
            uint8_t nal_header = (payload[0] & 0xE0) | fu_a_org_type;

            if (start) {
                rec_ctrl_.fu_a_buffer_.clear();
				rec_ctrl_.fu_a_buffer_.push_back(nal_header); // 重建的 NAL 头
            }
            rec_ctrl_.fu_a_buffer_.insert(rec_ctrl_.fu_a_buffer_.end(), payload.begin() + 2, payload.end());

            if (end) {
                writeNALtoFile(fu_a_org_type, rec_ctrl_.fu_a_buffer_.data(), rec_ctrl_.fu_a_buffer_.size(), ofs);
            }
        }
        else {
            writeNALtoFile(nal_unit_type, (char*)payload.data(), payload.size(), ofs);
        }
    }

    ofs.flush();
}

void StreamNode::flushRecordBuffer() {
    std::lock_guard<std::recursive_mutex> lock(rec_mutex_);  // 与 doRtpRecv 互斥，可被 rpc_stopRecord 重入
    if (record_batch_buffer_.empty()) return;

    std::vector<std::shared_ptr<RTPPacket>> to_write;
    to_write.swap(record_batch_buffer_);

    std::ofstream ofs(rec_ctrl_.path, std::ios::binary | std::ios::app);
    if (!ofs) {
        logError("flushRecordBuffer: Failed to open record file: " + rec_ctrl_.path);
        return;
    }

    for (auto p : to_write) {
        const std::vector<uint8_t>& payload = p->payload;
        if (payload.empty()) continue;

        uint8_t nal_unit_type = payload[0] & 0x1F;

        if (nal_unit_type == NAL_TYPE_STAP_A && payload.size() >= 2) {
            size_t off = 1;
            while (off + 2 <= payload.size()) {
                uint16_t L = (payload[off] << 8) | payload[off + 1];
                off += 2;
                if (L == 0) continue;
                if (off + L > payload.size()) break;
                const uint8_t* subNal = &payload[off];
                writeNALtoFile(subNal[0] & 0x1F, (char*)subNal, L, ofs);
                off += L;
            }
        }
        else if (nal_unit_type == NAL_TYPE_FU_A && payload.size() >= 2) {
            uint8_t fu_header = payload[1];
            bool end = (fu_header & 0x40) != 0;
            uint8_t fu_a_org_type = fu_header & 0x1F;
            uint8_t nal_header = (payload[0] & 0xE0) | fu_a_org_type;

            bool start = (fu_header & 0x80) != 0;
            if (start) {
                rec_ctrl_.fu_a_buffer_.clear();
                rec_ctrl_.fu_a_buffer_.push_back(nal_header);
            }
            rec_ctrl_.fu_a_buffer_.insert(rec_ctrl_.fu_a_buffer_.end(), payload.begin() + 2, payload.end());

            if (end) {
                writeNALtoFile(fu_a_org_type, rec_ctrl_.fu_a_buffer_.data(), rec_ctrl_.fu_a_buffer_.size(), ofs);
            }
        }
        else {
            writeNALtoFile(nal_unit_type, (char*)payload.data(), payload.size(), ofs);
        }
    }

    ofs.flush();
    LOG("[StreamNode] Flushed %zu buffered packets to record file, tag=%s",
        to_write.size(), config_.tag.c_str());
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
