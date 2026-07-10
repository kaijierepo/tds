// ============================================================================
// streamNode_socket.cpp - Socket 操作层
// 包含：UDP Socket 创建/管理（拉流/推流/服务端）、配置、数据收发
// ============================================================================

#include "streamNode.h"
#include "streamServer.h"
#include <cstring>
#include <logger.h>

// ============================================================================
// UDP传输函数
// ============================================================================

bool StreamNode::createUDPPullSocket() {
    // 我们需要为 RTP 和 RTCP 创建一对连续的端口以便向服务器声明 "client_port=RTP-RTCP"
    // 尝试多次分配以找到一对可用的连续端口
    const int max_attempts = 10;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // 创建RTP socket并绑定到系统分配的端口（0）
        SocketHandle rtp_sock = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
        if (rtp_sock == kInvalidSocket) {
            logError("Failed to create UDP pull socket (rtp)");
            return false;
        }

        if (!configureUDPSocket(rtp_sock, false)) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            continue;
        }

        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        local_addr.sin_port = htons(0); // 让系统分配端口

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

        if (!configureUDPSocket(rtcp_sock, false)) {
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
            session_origin_pull_.rtp_socket = rtp_sock;
            session_origin_pull_.rtcp_socket = rtcp_sock;
            session_origin_pull_.client_rtp_port = rtp_port;
            session_origin_pull_.client_rtcp_port = rtcp_port;

            logInfo("UDP pull sockets created: rtp_fd=" + std::to_string(session_origin_pull_.rtp_socket) +
                    " rtp_port=" + std::to_string(session_origin_pull_.client_rtp_port) +
                    " rtcp_fd=" + std::to_string(session_origin_pull_.rtcp_socket) +
                    " rtcp_port=" + std::to_string(session_origin_pull_.client_rtcp_port));
            return true;
        }

        // 如果绑定失败，释放并重试（可能下一个端口被占用）
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_sock));
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
    }

    logError("Failed to create consecutive UDP pull sockets for RTP/RTCP");
    session_origin_pull_.rtp_socket = kInvalidSocket;
    session_origin_pull_.rtcp_socket = kInvalidSocket;
    session_origin_pull_.client_rtp_port = 0;
    session_origin_pull_.client_rtcp_port = 0;
    return false;
}

// 修改 createUDPPushSocket()：分配并 bind 一对连续端口（RTP/RTCP）
bool StreamNode::createUDPPushSocket() {
    const int max_attempts = 10;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // 创建RTP socket并绑定到系统分配的端口（0）
        SocketHandle rtp_sock = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
        if (rtp_sock == kInvalidSocket) {
            logError("Failed to create UDP push socket (rtp)");
            return false;
        }

        if (!configureUDPSocket(rtp_sock, false)) {
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

        if (!configureUDPSocket(rtcp_sock, false)) {
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
            session_relay_push_.rtp_socket = rtp_sock;
            session_relay_push_.rtcp_socket = rtcp_sock;
            session_relay_push_.client_rtp_port = rtp_port;
            session_relay_push_.client_rtcp_port = rtcp_port;

            logInfo("UDP push sockets created: rtp_fd=" + std::to_string(session_relay_push_.rtp_socket) +
                " rtp_port=" + std::to_string(session_relay_push_.client_rtp_port = rtp_port) +
                " rtcp_fd=" + std::to_string(session_relay_push_.rtcp_socket) +
                " rtcp_port=" + std::to_string(session_relay_push_.client_rtcp_port));
            return true;
        }

        // 绑定失败，释放并重试
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_sock));
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
    }

    logError("Failed to create consecutive UDP push sockets for RTP/RTCP");
    session_relay_push_.rtp_socket = kInvalidSocket;
    session_relay_push_.rtcp_socket = kInvalidSocket;
    session_relay_push_.client_rtp_port = 0;
    session_relay_push_.client_rtcp_port = 0;
    return false;
}
bool StreamNode::createUDPServerSocket(STREAM_SESSION& streamInfo)
{
    const int max_attempts = 10;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // 创建RTP socket并绑定到系统分配的端口（0）
        SocketHandle rtp_sock = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
        if (rtp_sock == kInvalidSocket) {
            logError("Failed to create UDP push socket (rtp)");
            return false;
        }

        if (!configureUDPSocket(rtp_sock, false)) {
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

        if (!configureUDPSocket(rtcp_sock, false)) {
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
            streamInfo.rtp_socket = rtp_sock;
            streamInfo.rtcp_socket = rtcp_sock;
            streamInfo.server_rtp_port = rtp_port;
            streamInfo.server_rtcp_port = rtcp_port;

            logInfo("UDP server sockets created: rtp_fd=" + std::to_string(streamInfo.rtp_socket) +
                " rtp_port=" + std::to_string(streamInfo.server_rtp_port = rtp_port) +
                " rtcp_fd=" + std::to_string(streamInfo.rtcp_socket) +
                " rtcp_port=" + std::to_string(streamInfo.server_rtcp_port = rtcp_port
                ));
            return true;
        }

        // 绑定失败，释放并重试
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_sock));
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
    }

    logError("Failed to create consecutive UDP push sockets for RTP/RTCP");
    streamInfo.rtp_socket = kInvalidSocket;
    streamInfo.rtcp_socket = kInvalidSocket;
    streamInfo.client_rtp_port = 0;
    streamInfo.client_rtcp_port = 0;
    return false;
}
void StreamNode::closeUDPSockets() {
    if (session_origin_pull_.rtp_socket != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(session_origin_pull_.rtp_socket));
        session_origin_pull_.rtp_socket = kInvalidSocket;
    }
    if (session_origin_pull_.rtcp_socket != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(session_origin_pull_.rtcp_socket));
        session_origin_pull_.rtcp_socket = kInvalidSocket;
    }


    if (session_relay_push_.rtp_socket != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(session_relay_push_.rtp_socket));
        session_relay_push_.rtp_socket = kInvalidSocket;
    }
    if (session_relay_push_.rtcp_socket != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(session_relay_push_.rtcp_socket));
        session_relay_push_.rtcp_socket = kInvalidSocket;
    }


    session_origin_pull_.client_rtp_port = 0;
    session_origin_pull_.client_rtcp_port = 0;
    session_relay_push_.client_rtp_port = 0;
    session_relay_push_.client_rtcp_port = 0;
}

bool StreamNode::configureUDPSocket(SocketHandle sock, bool is_multicast) {
    if (sock == kInvalidSocket) return false;

#ifdef _WIN32
    DWORD tv = 1000;  // 1秒超时
    if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_RCVTIMEO, 
                   (const char*)&tv, sizeof(tv)) != 0) {
        logError("Failed to set UDP socket timeout");
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_RCVTIMEO, 
                   (const char*)&tv, sizeof(tv)) != 0) {
        logError("Failed to set UDP socket timeout");
        return false;
    }
#endif

    // 设置TTL
    if (config_.udp_ttl > 0) {
        int ttl = config_.udp_ttl;
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), IPPROTO_IP, IP_TTL,
                       (const char*)&ttl, sizeof(ttl)) != 0) {
            logError("Failed to set UDP TTL");
        }
    }

    // 设置ToS
    if (config_.udp_tos > 0) {
        int tos = config_.udp_tos;
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), IPPROTO_IP, IP_TOS,
                       (const char*)&tos, sizeof(tos)) != 0) {
            logError("Failed to set UDP ToS");
        }
    }

    // 设置组播回环
    if (is_multicast) {
        char loop = config_.udp_multicast_loop ? 1 : 0;
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), IPPROTO_IP, IP_MULTICAST_LOOP,
                       &loop, sizeof(loop)) != 0) {
            logError("Failed to set UDP multicast loop");
        }
    }

    // 设置接收缓冲区大小
    if (config_.udp_recv_buffer_size > 0) {
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_RCVBUF,
                       (const char*)&config_.udp_recv_buffer_size, sizeof(config_.udp_recv_buffer_size)) != 0) {
            logError("Failed to set UDP recv buffer size");
        }
    }

    // 设置发送缓冲区大小
    if (config_.udp_send_buffer_size > 0) {
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_SNDBUF,
                       (const char*)&config_.udp_send_buffer_size, sizeof(config_.udp_send_buffer_size)) != 0) {
            logError("Failed to set UDP send buffer size");
        }
    }

    // 允许地址重用（用于快速重启）
    int reuse = 1;
    if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_REUSEADDR,
                   (const char*)&reuse, sizeof(reuse)) != 0) {
        logError("Failed to set UDP socket reuse");
    }

    return true;
}

bool StreamNode::sendUDPDataToSession(const uint8_t* data, size_t size, STREAM_SESSION& rtspSession) {
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
            logError("Failed to resolve target host: " + rtspSession.remote_host);
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.errors++;
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
        logVerbose("UDP sendto error: sent=" + std::to_string(sent) +
            " expected=" + std::to_string(size) +
            " err=" + std::to_string(err));
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.errors++;
        return false;
    }

    return true;
}


int StreamNode::receiveUDPData(uint8_t* buffer, size_t size, std::string& src_ip, int& src_port) {
    if (session_origin_pull_.rtp_socket == kInvalidSocket) {
        return -1;
    }

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    int received = recvfrom(static_cast<SOCKET_TYPE>(session_origin_pull_.rtp_socket),
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
            logError("UDP recvfrom error: " + std::to_string(err));
        }
#else
        int err = errno;
        if (err != EAGAIN && err != EWOULDBLOCK) {
            logError("UDP recvfrom error: " + std::to_string(err));
        }
#endif
    }

    return received;
}
