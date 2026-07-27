// ============================================================================
// streamSession_socket.cpp - Socket 操作自由函数实现
// ============================================================================

#include "streamSession_socket.h"
#include <cstring>
#include <logger.h>

#ifndef _WIN32
#include <netdb.h>
#endif

// ============================================================================
// configureUDPSocket — 配置 UDP socket 选项
// ============================================================================

bool configureUDPSocket(SocketHandle sock, bool is_multicast, STREAM_SESSION& session) {
    if (sock == kInvalidSocket) return false;

#ifdef _WIN32
    DWORD tv = 1000;  // 1秒超时
    if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_RCVTIMEO,
                   (const char*)&tv, sizeof(tv)) != 0) {
        LOG("Failed to set UDP socket timeout");
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_RCVTIMEO,
                   (const char*)&tv, sizeof(tv)) != 0) {
        LOG("Failed to set UDP socket timeout");
        return false;
    }
#endif

    // 设置TTL
    if (session.udp_ttl > 0) {
        int ttl = session.udp_ttl;
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), IPPROTO_IP, IP_TTL,
                       (const char*)&ttl, sizeof(ttl)) != 0) {
            LOG("Failed to set UDP TTL");
        }
    }

    // 设置ToS
    if (session.udp_tos > 0) {
        int tos = session.udp_tos;
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), IPPROTO_IP, IP_TOS,
                       (const char*)&tos, sizeof(tos)) != 0) {
            LOG("Failed to set UDP ToS");
        }
    }

    // 设置组播回环
    if (is_multicast) {
        char loop = session.udp_multicast_loop ? 1 : 0;
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), IPPROTO_IP, IP_MULTICAST_LOOP,
                       &loop, sizeof(loop)) != 0) {
            LOG("Failed to set UDP multicast loop");
        }
    }

    // 设置接收缓冲区大小
    if (session.udp_recv_buffer_size > 0) {
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_RCVBUF,
                       (const char*)&session.udp_recv_buffer_size, sizeof(session.udp_recv_buffer_size)) != 0) {
            LOG("Failed to set UDP recv buffer size");
        }
    }

    // 设置发送缓冲区大小
    if (session.udp_send_buffer_size > 0) {
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_SNDBUF,
                       (const char*)&session.udp_send_buffer_size, sizeof(session.udp_send_buffer_size)) != 0) {
            LOG("Failed to set UDP send buffer size");
        }
    }

    // 允许地址重用（用于快速重启）
    int reuse = 1;
    if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_REUSEADDR,
                   (const char*)&reuse, sizeof(reuse)) != 0) {
        LOG("Failed to set UDP socket reuse");
    }

    return true;
}

// ============================================================================
// createUDPConsecutiveSockets — 创建一对连续端口 (RTP + RTCP)
// isServer: true=设置 server_rtp_port/server_rtcp_port, false=设置 client_rtp_port/client_rtcp_port
// ============================================================================

bool createUDPConsecutiveSockets(STREAM_SESSION& session, bool isServer) {
    const int max_attempts = 10;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // 创建RTP socket并绑定到系统分配的端口（0）
        SocketHandle rtp_sock = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
        if (rtp_sock == kInvalidSocket) {
            LOG("Failed to create UDP socket (rtp)");
            return false;
        }

        if (!configureUDPSocket(rtp_sock, false, session)) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            continue;
        }

        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        local_addr.sin_port = htons(0); // 系统分配端口

        if (::bind(static_cast<SOCKET_TYPE>(rtp_sock), (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            continue;
        }

        // 获取分配的端口
        socklen_t len = sizeof(local_addr);
        if (getsockname(static_cast<SOCKET_TYPE>(rtp_sock), (struct sockaddr*)&local_addr, &len) != 0) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            continue;
        }

        int rtp_port = ntohs(local_addr.sin_port);
        int rtcp_port = rtp_port + 1;

        // 创建RTCP socket并绑定到 rtp_port + 1
        SocketHandle rtcp_sock = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
        if (rtcp_sock == kInvalidSocket) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            continue;
        }

        if (!configureUDPSocket(rtcp_sock, false, session)) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_sock));
            continue;
        }

        struct sockaddr_in rtcp_addr;
        memset(&rtcp_addr, 0, sizeof(rtcp_addr));
        rtcp_addr.sin_family = AF_INET;
        rtcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        rtcp_addr.sin_port = htons(rtcp_port);

        if (::bind(static_cast<SOCKET_TYPE>(rtcp_sock), (struct sockaddr*)&rtcp_addr, sizeof(rtcp_addr)) == 0) {
            // 成功获取到一对连续端口
            session.rtp_socket = rtp_sock;
            session.rtcp_socket = rtcp_sock;

            if (isServer) {
                session.server_rtp_port = rtp_port;
                session.server_rtcp_port = rtcp_port;

                char buf[256];
                snprintf(buf, sizeof(buf),
                    "UDP server sockets created: rtp_fd=%d rtp_port=%d rtcp_fd=%d rtcp_port=%d",
                    (int)session.rtp_socket, session.server_rtp_port,
                    (int)session.rtcp_socket, session.server_rtcp_port);
                LOG(buf);
            } else {
                session.client_rtp_port = rtp_port;
                session.client_rtcp_port = rtcp_port;

                char buf[256];
                snprintf(buf, sizeof(buf),
                    "UDP client sockets created: rtp_fd=%d rtp_port=%d rtcp_fd=%d rtcp_port=%d",
                    (int)session.rtp_socket, session.client_rtp_port,
                    (int)session.rtcp_socket, session.client_rtcp_port);
                LOG(buf);
            }

            return true;
        }

        // 绑定失败，释放并重试
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_sock));
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
    }

    LOG("Failed to create consecutive UDP sockets for RTP/RTCP");
    session.rtp_socket = kInvalidSocket;
    session.rtcp_socket = kInvalidSocket;
    if (isServer) {
        session.server_rtp_port = 0;
        session.server_rtcp_port = 0;
    } else {
        session.client_rtp_port = 0;
        session.client_rtcp_port = 0;
    }
    return false;
}

// ============================================================================
// closeSessionSockets — 关闭一个 session 的 RTP/RTCP socket
// ============================================================================

void closeSessionSockets(STREAM_SESSION& session) {
    if (session.rtp_socket != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(session.rtp_socket));
        session.rtp_socket = kInvalidSocket;
    }
    if (session.rtcp_socket != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(session.rtcp_socket));
        session.rtcp_socket = kInvalidSocket;
    }
    session.client_rtp_port = 0;
    session.client_rtcp_port = 0;
}

// ============================================================================
// sendUDPDataToSession — 发送 UDP 数据到 session 远端
// ============================================================================

bool sendUDPDataToSession(const uint8_t* data, size_t size, STREAM_SESSION& rtspSession) {
    int remoteRtpPort = 0;
    int remoteRtcpPort = 0;
    if (rtspSession.session_type_ == ORIGIN_PULL || rtspSession.session_type_ == RELAY_PUSH) {
        remoteRtpPort = rtspSession.server_rtp_port;
        remoteRtcpPort = rtspSession.server_rtcp_port;
    }
    else {
        remoteRtpPort = rtspSession.client_rtp_port;
        remoteRtcpPort = rtspSession.client_rtcp_port;
    }

    if (rtspSession.rtp_socket == kInvalidSocket || remoteRtpPort == 0) {
        return false;
    }

    // 解析目标地址
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(remoteRtpPort);

    if (inet_pton(AF_INET, rtspSession.remote_host.c_str(), &target_addr.sin_addr) <= 0) {
        struct hostent* server = gethostbyname(rtspSession.remote_host.c_str());
        if (!server) {
            char buf[256];
            snprintf(buf, sizeof(buf), "Failed to resolve target host: %s",
                rtspSession.remote_host.c_str());
            LOG(buf);
            return false;
        }
        memcpy(&target_addr.sin_addr, server->h_addr, server->h_length);
    }

    // 发送UDP数据
    int sent = sendto(static_cast<SOCKET_TYPE>(rtspSession.rtp_socket),
        (const char*)data, (int)size, 0,
        (struct sockaddr*)&target_addr, sizeof(target_addr));

    if (sent != static_cast<int>(size)) {
#ifdef _WIN32
        int err = WSAGetLastError();
#else
        int err = errno;
#endif
        char buf[128];
        snprintf(buf, sizeof(buf),
            "UDP sendto error: sent=%d expected=%d err=%d",
            sent, (int)size, err);
        LOG(buf);
        return false;
    }
    rtspSession.rtpBytesSended += sent;
    return true;
}

// ============================================================================
// receiveUDPDataFromSession — 从 session 的 RTP socket 接收数据
// ============================================================================

int receiveUDPDataFromSession(uint8_t* buffer, size_t size, std::string& src_ip, int& src_port, STREAM_SESSION& session) {
    if (session.rtp_socket == kInvalidSocket) {
        return -1;
    }

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    int received = recvfrom(static_cast<SOCKET_TYPE>(session.rtp_socket),
                             (char*)buffer, (int)size, 0,
                             (struct sockaddr*)&from_addr, &from_len);

    if (received > 0) {
        char ip_str[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
        src_ip = ip_str;
        src_port = ntohs(from_addr.sin_port);
        return received;
    }
    else if (received < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAETIMEDOUT && err != WSAEWOULDBLOCK && err != WSAECONNRESET) {
            char buf[64];
            snprintf(buf, sizeof(buf), "UDP recvfrom error: %d", err);
            LOG(buf);
        }
#else
        int err = errno;
        if (err != EAGAIN && err != EWOULDBLOCK) {
            char buf[64];
            snprintf(buf, sizeof(buf), "UDP recvfrom error: %d", err);
            LOG(buf);
        }
#endif
    }

    return received;
}
