// ============================================================================
// streamSession_socket.h - Socket 操作自由函数（与 StreamNode 解耦）
// ============================================================================
#pragma once

#include "streamSession.h"

// 配置 UDP socket（超时/TTL/ToS/缓冲区/地址重用）
bool configureUDPSocket(SocketHandle sock, bool is_multicast, STREAM_SESSION& session);

// 为一组 session 创建连续的 RTP+RTCP socket 对
// isServer: true=设置 server_rtp_port/server_rtcp_port, false=设置 client_rtp_port/client_rtcp_port
bool createUDPConsecutiveSockets(STREAM_SESSION& session, bool isServer);

// 关闭一个 session 的 RTP/RTCP socket
void closeSessionSockets(STREAM_SESSION& session);

// 发送 UDP 数据到 session 的远端
bool sendUDPDataToSession(const uint8_t* data, size_t size, STREAM_SESSION& rtspSession);

// 从 session 的 RTP socket 接收 UDP 数据
int receiveUDPDataFromSession(uint8_t* buffer, size_t size, std::string& src_ip, int& src_port, STREAM_SESSION& session);
