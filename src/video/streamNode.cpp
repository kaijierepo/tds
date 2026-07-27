// ============================================================================
// streamNode.cpp - StreamNode 核心层
// 包含：平台初始化、编解码工具、HTTP 解析、TCP Connection 类、生命周期管理、
//       状态机/重连、URL 解析、工具函数、日志
// ============================================================================

#include "streamNode.h"
#include "streamServer.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <climits>
#include <cstdarg>
#include <ctime>
#include <algorithm>
#include <random>
#include <cassert>
#include <regex>
#include <logger.h>

// 引入 MD5 辅助类（用于 md5Hex）
#include "../common/md5.h"



// 静态初始化
#ifdef _WIN32
class WinsockInitializer {
public:
    WinsockInitializer() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~WinsockInitializer() {
        WSACleanup();
    }
};
static WinsockInitializer winsock_init;
#endif

// Base64解码（用于解析 sprop-parameter-sets，也供 streamNode_rtsp.cpp 的 parseSDP 使用）
std::vector<uint8_t> base64Decode(const std::string& input) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::vector<uint8_t> out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)chars[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back((uint8_t)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// MD5辅助函数 (使用项目自带的 MD5 类)
std::string StreamNode::md5Hex(const std::string& input) {
    MD5 md5;
    return md5(input);
}

// Base64编码辅助函数
std::string StreamNode::base64Encode(const std::string& input) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string encoded;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    size_t in_len = input.size();
    const char* bytes_to_encode = input.c_str();

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                encoded += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; j < i + 1; j++)
            encoded += base64_chars[char_array_4[j]];

        while (i++ < 3)
            encoded += '=';
    }

    return encoded;
}

// HTTP包验证函数
size_t IsValidPkt_HTTP(std::string& strData, size_t iLen) {
    size_t iPos_contentLengthLineStart = strData.find("Content-Length:");
    if (iPos_contentLengthLineStart == std::string::npos) {
        auto iDataLen = strData.length();
        if (iDataLen >= 4) {
            std::string tail = strData.substr(iDataLen - 4, 4);
            if (tail == "\r\n\r\n")
                return iLen;
            else
                return 0;
        }
    }
    else {
        size_t iPos_contentLengthLineEnd = strData.find("\r\n", iPos_contentLengthLineStart);
        if (iPos_contentLengthLineEnd == std::string::npos)
            return 0;

        std::string strLen = strData.substr(iPos_contentLengthLineStart + 15, iPos_contentLengthLineEnd - (iPos_contentLengthLineStart + 15));
        size_t iContentLen = atoi(strLen.c_str());

        size_t iBodyStart = 0;
        for (size_t i = iPos_contentLengthLineEnd; i + 3 < iLen; i++) {
            if (strData[i] == '\r' &&
                strData[i + 1] == '\n' &&
                strData[i + 2] == '\r' &&
                strData[i + 3] == '\n') {
                iBodyStart = i + 4;
                break;
            }
        }

        if (iBodyStart == 0)
            return 0;

        if (iLen >= iBodyStart + iContentLen)
            return iBodyStart + iContentLen;

        return 0;
    }
    return 0;
}

// ============================================================================
// Connection 实现
// ============================================================================

Connection::Connection() : sockfd_(kInvalidSocket) {
}

Connection::~Connection() {
    disconnect();
}

bool Connection::connect(const std::string& host, int port, int timeout_ms) {
    disconnect();
    last_error_ = 0;

    // 创建socket
    sockfd_ = static_cast<SocketHandle>(socket(AF_INET, SOCK_STREAM, 0));
    if (sockfd_ == kInvalidSocket) {
        last_error_ = SOCKET_ERROR_NUM;
        return false;
    }

    // 设置超时
    setSocketTimeout(timeout_ms);

    // 解析主机名
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // 尝试直接解析为IP地址
    if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
        // 如果不是IP地址，进行DNS解析
        struct hostent* server = gethostbyname(host.c_str());
        if (!server) {
            last_error_ = SOCKET_ERROR_NUM;
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(sockfd_));
            sockfd_ = kInvalidSocket;
            return false;
        }
        memcpy(&serv_addr.sin_addr, server->h_addr, server->h_length);
    }

    // 连接
    if (::connect(static_cast<SOCKET_TYPE>(sockfd_), (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        last_error_ = SOCKET_ERROR_NUM;
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(sockfd_));
        sockfd_ = kInvalidSocket;
        return false;
    }

    // 禁用 Nagle 算法，确保请求立即发送
    int nodelay = 1;
    //setsockopt(static_cast<SOCKET_TYPE>(sockfd_), IPPROTO_TCP, TCP_NODELAY,
    //    reinterpret_cast<char*>(&nodelay), sizeof(nodelay));

    host_ = host;
    port_ = port;

    // 恢复为阻塞模式
    setSocketTimeout(0);
    last_error_ = 0;

    return true;
}

void Connection::disconnect() {
    if (sockfd_ != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(sockfd_));
        sockfd_ = kInvalidSocket;
    }
    host_.clear();
    port_ = 0;
    last_error_ = 0;
}

bool Connection::isConnected() const {
    return sockfd_ != kInvalidSocket;
}

int Connection::send(const void* data, size_t size, int timeout_ms) {
    if (sockfd_ == kInvalidSocket) return -1;

    if (timeout_ms > 0) {
        setSocketTimeout(timeout_ms);
    }

    const char* bytes = static_cast<const char*>(data);
    size_t total_sent = 0;

    while (total_sent < size) {
        size_t remaining = size - total_sent;
        int chunk = static_cast<int>((remaining > static_cast<size_t>(INT_MAX)) ? static_cast<size_t>(INT_MAX) : remaining);

        int n = static_cast<int>(::send(static_cast<SOCKET_TYPE>(sockfd_),
            bytes + total_sent, chunk, 0));

        if (n <= 0) {
            last_error_ = (n < 0) ? SOCKET_ERROR_NUM : 0;
            if (timeout_ms > 0) {
                setSocketTimeout(0);
            }
            return (total_sent > 0) ? static_cast<int>(total_sent) : n;
        }

        total_sent += static_cast<size_t>(n);
    }

    if (timeout_ms > 0) {
        setSocketTimeout(0);
    }

    last_error_ = 0;
    return static_cast<int>(total_sent);
}

int Connection::receive(void* buffer, size_t size, int timeout_ms) {
    if (sockfd_ == kInvalidSocket) return -1;

    if (timeout_ms > 0) {
        setSocketTimeout(timeout_ms);
    }

    int received = static_cast<int>(::recv(static_cast<SOCKET_TYPE>(sockfd_),
        (char*)buffer, static_cast<int>(size), 0));
    if (received <= 0) {
        last_error_ = (received < 0) ? SOCKET_ERROR_NUM : 0;
    }
    else {
        last_error_ = 0;
    }

    if (timeout_ms > 0) {
        setSocketTimeout(0);
    }

    return received;
}

int Connection::receiveHttpResp(std::string& response, int timeout_ms) {
    if (sockfd_ == kInvalidSocket) return -1;

    int total = 0;
    char buf[1000] = { 0 };

    // 清除超时设置，使用 select() 来等待
    setSocketTimeout(0);

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd_, &readfds);
        
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        int select_result = ::select(static_cast<int>(sockfd_) + 1, &readfds, nullptr, nullptr, &tv);
        if (select_result <= 0) {
            // select 超时或错误
            last_error_ = (select_result < 0) ? SOCKET_ERROR_NUM : 0;
            // 即使超时，如果有部分数据也要尝试处理
            if (total > 0 && IsValidPkt_HTTP(response, static_cast<size_t>(total))) {
                last_error_ = 0;
                return total;
            }
            return (total > 0) ? total : -1;
        }

        // socket 可读
        int n = static_cast<int>(::recv(static_cast<SOCKET_TYPE>(sockfd_), buf, (int)sizeof(buf), 0));
        if (n <= 0) {
            last_error_ = (n < 0) ? SOCKET_ERROR_NUM : 0;
            if (total > 0 && IsValidPkt_HTTP(response, static_cast<size_t>(total))) {
                last_error_ = 0;
                return total;
            }
            return (total > 0) ? total : n;
        }

        total += n;
        response.append(buf, n);

        if (IsValidPkt_HTTP(response, static_cast<size_t>(total))) {
            break;
        }
    }

    last_error_ = 0;
    return total;
}

bool Connection::setSocketTimeout(int timeout_ms) {
    if (sockfd_ == kInvalidSocket) return false;

#ifdef _WIN32
    DWORD tv = timeout_ms;
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
#endif

    int result = setsockopt(static_cast<SOCKET_TYPE>(sockfd_), SOL_SOCKET, SO_RCVTIMEO,
        (const char*)&tv, sizeof(tv));
    if (result == 0) {
        result = setsockopt(static_cast<SOCKET_TYPE>(sockfd_), SOL_SOCKET, SO_SNDTIMEO,
            (const char*)&tv, sizeof(tv));
    }

    if (result != 0) {
        last_error_ = SOCKET_ERROR_NUM;
    }
    return result == 0;
}

// ============================================================================
// StreamNode 实现 - 生命周期与回调
// ============================================================================

StreamNode::StreamNode() {
    open_time_ = std::chrono::system_clock::now();
    stats_.start_time = std::chrono::steady_clock::now();
    stats_.last_frame_time = std::chrono::steady_clock::now();
    session_relay_push_.transport_mode = TransportMode::TCP;
}

StreamNode::~StreamNode() {
    stop();
}

bool StreamNode::start(const Config& config) {
    if (running_) {
        return false;
    }

    config_ = config;
    running_ = true;
    stopping_ = false;
    isPulling_ = false;
    isPushing_ = false;

    // 清除之前的认证信息
    source_auth_.clear();
    target_auth_.clear();

    // 初始化认证信息（从 STREAM_SESSION 读取）
    if (!session_origin_pull_.server_username_.empty()) {
        source_auth_.username = session_origin_pull_.server_username_;
        source_auth_.password = session_origin_pull_.server_password_;
    }

    if (!session_relay_push_.server_username_.empty()) {
        target_auth_.username = session_relay_push_.server_username_;
        target_auth_.password = session_relay_push_.server_password_;
    }

    control_thread_ = std::thread(&StreamNode::controlThread, this);
	control_thread_.detach();

    LOG("[StreamNode] StreamNode started,tag=%s,src=%s,target=%s",config_.tag.c_str(), session_origin_pull_.server_url_.c_str(), session_relay_push_.server_url_.c_str());

    return true;
}

void StreamNode::stop() {
    if (!running_) return;

    stopping_ = true;
    running_ = false;

    cv_.notify_all();

    // 停止所有 ICE 线程
    stopAllRtcHandleThreads();

    if (rtp_handle_thread_.joinable()) {
        rtp_handle_thread_.join();
    }

    if (control_thread_.joinable()) {
        control_thread_.join();
    }

    // 停止录像 I/O 线程（先置 false 唤醒，再 join 等待退出）
    if (record_io_running_) {
        record_io_running_ = false;
        record_queue_cv_.notify_one();
        if (record_io_thread_.joinable()) {
            record_io_thread_.join();
        }
    }

    teardown();

    setState(SESSION_STATE::SESSION_IDLE, "Stopped");
}


SESSION_STATE StreamNode::getState() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return session_origin_pull_.state_;
}

bool StreamNode::isRunning() {
    return running_;
}

StreamNode::Statistics StreamNode::getStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    auto now = std::chrono::steady_clock::now();
    auto sinceLastFrame = std::chrono::duration<double>(now - stats_.last_frame_time).count();

    if (sinceLastFrame > 2.0) {
        stats_.fps = 0.0;
        stats_.bitrate = 0.0;
    } else {
        auto elapsed = std::chrono::duration<double>(now - stats_.start_time).count();
        if (elapsed > 0) {
            stats_.fps = stats_.frames_received / elapsed;
            stats_.bitrate = (stats_.bytes_received * 8 / 1000.0) / elapsed;
        }
    }

    return stats_;
}

void StreamNode::setStatusCallback(StatusCallback cb) {
    status_callback_ = cb;
}

void StreamNode::setFrameCallback(FrameCallback cb) {

}

void StreamNode::setErrorCallback(ErrorCallback cb) {
    error_callback_ = cb;
}

// ============================================================================
// 工具函数 - URL解析
// ============================================================================

bool StreamNode::URLComponents::parse(const std::string & url, URLComponents & components) {
    URLComponents out;

    size_t protocol_end = url.find("://");
    if (protocol_end == std::string::npos) return false;

    out.protocol = url.substr(0, protocol_end);
    std::string rest = url.substr(protocol_end + 3);

    size_t slash_pos = rest.find('/');
    std::string authority = (slash_pos == std::string::npos) ? rest : rest.substr(0, slash_pos);
    out.path = (slash_pos == std::string::npos) ? "/" : rest.substr(slash_pos);

    size_t at_pos = authority.rfind('@');
    std::string host_port = (at_pos == std::string::npos) ? authority : authority.substr(at_pos + 1);
    if (host_port.empty()) return false;

    auto default_port = [&out]() -> int {
        if (out.protocol == "rtsp") return 554;
        if (out.protocol == "rtsps") return 322;
        if (out.protocol == "http") return 80;
        if (out.protocol == "https") return 443;
        return 0;
        };

    out.port = default_port();

    // IPv6: [::1]:554
    if (host_port.front() == '[') {
        size_t end = host_port.find(']');
        if (end == std::string::npos) return false;
        out.host = host_port.substr(1, end - 1);

        if (end + 1 < host_port.size()) {
            if (host_port[end + 1] != ':') return false;
            std::string port_str = host_port.substr(end + 2);
            if (port_str.empty()) return false;
            try {
                out.port = std::stoi(port_str);
            }
            catch (...) {
                return false;
            }
        }
    }
    else {
        size_t colon_pos = host_port.rfind(':');
        if (colon_pos != std::string::npos) {
            if (host_port.find(':') != colon_pos) return false;
            out.host = host_port.substr(0, colon_pos);
            std::string port_str = host_port.substr(colon_pos + 1);
            if (port_str.empty()) return false;
            try {
                out.port = std::stoi(port_str);
            }
            catch (...) {
                return false;
            }
        }
        else {
            out.host = host_port;
        }
    }

    if (out.host.empty()) return false;
    if (out.port <= 0 || out.port > 65535) return false;

    components = out;
    return true;
}

// ============================================================================
// 工具函数 - 字符串解析
// ============================================================================

std::string StreamNode::extractSessionID(const std::string & response) {
    size_t pos = response.find("Session: ");
    if (pos == std::string::npos) return "";

    size_t end = response.find("\r\n", pos);
    std::string session_line = response.substr(pos, end - pos);

    std::string session = session_line.substr(9);

    size_t timeout_pos = session.find(';');
    if (timeout_pos != std::string::npos) {
        session = session.substr(0, timeout_pos);
    }

    return session;
}

std::string StreamNode::extractTransport(const std::string & response) {
    size_t pos = response.find("Transport: ");
    if (pos == std::string::npos) return "";

    size_t end = response.find("\r\n", pos);
    return response.substr(pos + 11, end - pos - 11);
}

std::string StreamNode::generateCSeq() {
    static std::atomic<int> counter{ 1 };
    return std::to_string(counter++);
}

// ============================================================================
// 状态机与错误处理
// ============================================================================

void StreamNode::setError(const std::string & error, int code) {

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.errors++;
    }

    if (error_callback_) {
        error_callback_(error, code);
    }

    setState(SESSION_STATE::SESSION_ERROR, error);
}

void StreamNode::setState(SESSION_STATE new_state, const std::string & msg) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        session_origin_pull_.state_ = new_state;
    }

    if (status_callback_) {
        status_callback_(new_state, msg);
    }

    cv_.notify_all();
}

bool StreamNode::shouldReconnect(STREAM_SESSION& session) const {
    if (session.max_retries_ > 0 && session.retry_count_ >= session.max_retries_) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - session.last_reconnect_time_ < std::chrono::milliseconds(session.retry_interval_)) {
        return false;
    }

    return true;
}

void StreamNode::doReconnect(STREAM_SESSION& session) {
    session.retry_count_++;
    session.last_reconnect_time_ = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.reconnect_count++;
    }

    if (session.max_retries_ <= 0) {
        setState(SESSION_STATE::SESSION_RECONNECTING, "Reconnecting (attempt " + std::to_string(session.retry_count_) + "/unlimited)");
    }
    else {
        setState(SESSION_STATE::SESSION_RECONNECTING,
            "Reconnecting (attempt " + std::to_string(session.retry_count_) +
            "/" + std::to_string(session.max_retries_) + ")");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(session.retry_interval_));
}

// ============================================================================
// 日志函数
// ============================================================================

void StreamNode::logInfo(const std::string & msg) const {
    std::cout << "[INFO] " << msg << std::endl;
}

void StreamNode::logError(const std::string & msg) const {
    std::cerr << "[ERROR] " << msg << std::endl;
}

void StreamNode::logDebug(const std::string & msg) const {
    if (config_.verbose) {
        std::cout << "[DEBUG] " << msg << std::endl;
    }
}

void StreamNode::logVerbose(const std::string & msg) const {
    if (config_.verbose) {
        std::cout << "[VERBOSE] " << msg << std::endl;
    }
}

// ============================================================================
// RTSP URL 认证信息提取
// ============================================================================

/**
 * @brief 从RTSP URL中提取用户名和密码
 * @param session 会话结构体（读取 server_url_，写入 server_username_/server_password_）
 * @return 解析成功返回true，失败返回false
 */

bool StreamNode::extractRtspAuthInfo(STREAM_SESSION& session) {
    // 正则表达式匹配RTSP URL格式：rtsp://[user:pass@]host[:port]/path
    const std::regex rtspRegex(R"(^rtsp://([^:]+):([^@]+)@.*$)");
    std::smatch matchResult;

    // 匹配URL并提取用户名和密码
    if (std::regex_match(session.server_url_, matchResult, rtspRegex)) {
        if (matchResult.size() >= 3) {
            session.server_username_ = matchResult[1].str();
            session.server_password_ = matchResult[2].str();
            return true;
        }
    }

    // 若未匹配到（URL无账号密码），清空用户名密码
    session.server_username_ = "";
    session.server_password_ = "";
    return false;
}

std::string StreamNode::getSessionTypeDesc(STREAM_SESSION_TYPE sessionType)
{
    if (sessionType == STREAM_SESSION_TYPE::ORIGIN_PULL) {
        return "origin_pull";
    }
    else if (sessionType == STREAM_SESSION_TYPE::RELAY_PUSH) {
        return "relay_push";
    }
    else if (sessionType == STREAM_SESSION_TYPE::CLIENT_RTSP_PULL) {
        return "client_rtsp_pull";
    }
    else if (sessionType == STREAM_SESSION_TYPE::CLIENT_RTSP_PUBLISH) {
        return "client_rtsp_publish";
    }
    else if (sessionType == STREAM_SESSION_TYPE::CLIENT_WEBRTC_PULL) {
        return "client_webrtc_pull";
    }
    else if (sessionType == STREAM_SESSION_TYPE::CLIENT_WEBRTC_PUBLISH) {
        return "client_webrtc_publish";
    }

    return "unknown";
}

// ============================================================================
// ICE-Lite + DTLS + SRTP (WebRTC) — 每客户端一线程
// 控制流与线程管理放在 StreamNode 核心层；具体协议处理见 streamSession_webrtc.cpp
// ============================================================================

void StreamNode::startRtcSessionHandleThread(std::shared_ptr<STREAM_SESSION> session) {
    if (!session || session->rtp_socket == kInvalidSocket || !session->is_webrtc) {
        return;
    }

    // 设置 socket 接收超时为 1 秒，保证 stop 时能及时退出
#ifdef _WIN32
    int timeout_ms = 1000;
    setsockopt(static_cast<SOCKET_TYPE>(session->rtp_socket), SOL_SOCKET, SO_RCVTIMEO,
               (const char*)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval tv = {1, 0};
    setsockopt(session->rtp_socket, SOL_SOCKET, SO_RCVTIMEO,
               (const char*)&tv, sizeof(tv));
#endif

    session->rtc_handle_thread_running_ = true;
    session->rtc_handle_thread_ = std::thread(&StreamNode::rtcSessionHandleThread, this, session);

    logInfo("ICE thread started for socket fd=" + std::to_string(session->rtp_socket)
            + " ufrag=" + session->ice_ufrag);
}

void StreamNode::stopAllRtcHandleThreads() {
    // 获取所有 client_sessions_ 快照，停止其中的 WebRTC ICE 线程
    std::vector<std::shared_ptr<STREAM_SESSION>> sessions;
    {
        std::lock_guard<std::mutex> lock(session_list_client_pull_mutex_);
        sessions = session_list_client_pull_;
    }

    for (auto& s : sessions) {
        if (!s || !s->is_webrtc) continue;

        s->rtc_handle_thread_running_ = false;
        if (s->rtc_handle_thread_.joinable()) {
            s->rtc_handle_thread_.join();
        }
        // 清理 DTLS 状态（ICE 线程退出时已释放局部 shared_ptr，这里丢弃会话持有的引用）
        std::atomic_store(&s->dtls_transport_, std::shared_ptr<SessionDtlsState>());
        logInfo("ICE thread stopped for socket fd=" + std::to_string(s->rtp_socket));
    }
}

void StreamNode::rtcSessionHandleThread(std::shared_ptr<STREAM_SESSION> session) {
    uint8_t buf[2048];

    // 初始化本会话的 DTLS 状态（shared_ptr 管理，RTP 发送线程可安全持有引用）
    std::shared_ptr<SessionDtlsState> dtls_state = std::make_shared<SessionDtlsState>();
    std::atomic_store(&session->dtls_transport_, dtls_state);
    session->srtp_context_   = &dtls_state->srtp_ctx;

    // 使用 StreamServer 的共享证书初始化 DTLS
    extern StreamServer streamSrv;
    if (streamSrv.m_dtlsCertPem.empty() || streamSrv.m_dtlsKeyPem.empty()) {
        LOG("[ICE] WARNING: No DTLS cert configured, DTLS disabled");
        // 暂不初始化 DTLS，只处理 STUN
    } else {
        dtls_state->dtls_initialized = dtls_state->dtls.init(
            streamSrv.m_dtlsCertPem, streamSrv.m_dtlsKeyPem);
        if (dtls_state->dtls_initialized) {
            dtls_state->dtls.setSocket(session->rtp_socket, {}); // peer 会在首包时由 recvfrom 设置
            dtls_state->dtls.startHandshake();
        }
    }

    auto dtls_start = std::chrono::steady_clock::now();

    while (session->rtc_handle_thread_running_) {
        struct sockaddr_in peer;
        socklen_t peerLen = sizeof(peer);
        int len = recvfrom(static_cast<SOCKET_TYPE>(session->rtp_socket),
                           (char*)buf, sizeof(buf), 0,
                           (struct sockaddr*)&peer, &peerLen);

        // 检查 DTLS 握手是否超时（8秒内 state 未到 streaming）
        if (session->state_ >= SESSION_STATE::SESSION_HANDSHAKING && session->state_ < SESSION_STATE::SESSION_STREAMING) {
            auto now = std::chrono::steady_clock::now();
            if (now - dtls_start > std::chrono::seconds(8)) {
                LOG("[ICE] DTLS handshake timeout (8s), state=%d",
                    (int)session->state_);
                session->rtc_handle_thread_running_ = false;
                break;
            }
        }

        // 检查session->last_stun_bind_req_time 是否过去超过20秒，是则退出线程
        if (session->last_stun_bind_req_time.time_since_epoch().count() > 0) {
            auto now = std::chrono::system_clock::now();
            if (now - session->last_stun_bind_req_time > std::chrono::seconds(20)) {
                LOG("[ICE] STUN keep-alive timeout (20s), client disconnected");
                session->rtc_handle_thread_running_ = false;
                break;
            }
        }

        if (len < 0) {
            continue;  // 超时，继续循环
        }
        if (len < 1) continue;

        uint8_t firstByte = buf[0];

        // ---- 协议分流 ----
        // STUN:  0x00 (Binding Request) 或 0x01 (Binding Success/Error)
        // DTLS:  0x14 (ChangeCipherSpec), 0x15 (Alert), 0x16 (Handshake), 0x17 (AppData)
        // RTP/RTCP: 高 2 位为 0b10 (V=2), 即首字节 0x80~0xBF

        // === STUN ===
        if (firstByte == 0x00 || firstByte == 0x01) {
            sessionHandleSTUN(session, buf, len, peer, dtls_start);
        }
        // === DTLS ===
        else if (firstByte >= 0x14 && firstByte <= 0x18) {
            sessionHandleDTLS(session, dtls_state.get(), buf, len, peer, dtls_start);
        }
        // === SRTP / SRTCP (来自客户端的加密 RTP/RTCP) ===
        else if ((firstByte & 0xC0) == 0x80 && dtls_state->srtp_ready) {
            if (len < 2) continue;
            // 浏览器作为纯接收端只回传 RTCP 反馈。首包 PT(明文,buf[1]) 落在 200..206
            // 即为 RTCP compound，交由 SRTCP 处理解密后解析 PLI/FIR/NACK。
            uint8_t pt = buf[1];
            if (pt >= 200 && pt <= 206) {
                sessionHandleSRTCP(session, dtls_state.get(), buf, len, peer);
            }
            else {
                // 客户端 RTP（纯接收场景一般不会出现），保持原样静默解密
                std::vector<uint8_t> srtpPkt(buf, buf + len);
                std::vector<uint8_t> rtpPkt;
                SrptProtect::unprotect(dtls_state->recv_ctx, srtpPkt, rtpPkt);
            }
        }
        else {
            // 未知协议，打印前40字节以诊断
            char hx[128] = {};
            int hoff = 0;
            for (int i = 0; i < len && i < 40 && hoff < 100; i++)
                hoff += sprintf(hx + hoff, "%02x", (unsigned char)buf[i]);
            LOG("[ICE] Unknown packet from %s:%d, len=%d, firstByte=0x%02x, "
                "srtp_ready=%d, dtls_state=%p, hex=%s",
                inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), len, firstByte,
                (int)dtls_state->srtp_ready, (void*)dtls_state.get(), hx);
            // 诊断：打印所有 client_pull 会话，确认是否存在多个会话并存、哪个 srtp_ready=false
            {
                static long unknownCount = 0;
                if ((++unknownCount % 100) == 1) {
                    std::lock_guard<std::mutex> lock(session_list_client_pull_mutex_);
                    LOG("[ICE] client_pull session count = %d",
                        (int)session_list_client_pull_.size());
                    for (auto& s : session_list_client_pull_) {
                        std::shared_ptr<SessionDtlsState> st_shared =
                            std::atomic_load(&s->dtls_transport_);
                        SessionDtlsState* st = st_shared.get();
                        LOG("[ICE]   session fd=%d srtp_ready=%d state_=%d",
                            (int)s->rtp_socket, st ? (int)st->srtp_ready : -1,
                            (int)s->state_);
                    }
                }
            }
        }
    }

    // 清理本会话的 DTLS 状态：丢弃会话持有的引用即可，
    // 对象由本函数的局部 shared_ptr dtls_state 在函数返回时释放（不再裸 delete）
    std::atomic_store(&session->dtls_transport_, std::shared_ptr<SessionDtlsState>());
    session->srtp_context_   = nullptr;

    // 从 session_list_client_pull_ 中移除本会话
    {
        std::lock_guard<std::mutex> lock(session_list_client_pull_mutex_);
        auto& sessions = session_list_client_pull_;
        sessions.erase(
            std::remove_if(sessions.begin(), sessions.end(),
                [&](const std::shared_ptr<STREAM_SESSION>& s) {
                    return s->rtp_socket == session->rtp_socket;
                }),
            sessions.end());
    }
    logInfo("ICE thread exiting, session removed from client_pull list, socket fd="
            + std::to_string(session->rtp_socket));

    // 线程自身退出时无法 join 自己；若 thread 对象仍 joinable，析构会 terminate。
    // 在线程返回前 detach，使其与 thread 对象分离，避免后续析构 session 时崩溃。
    if (session->rtc_handle_thread_.joinable()) {
        session->rtc_handle_thread_.detach();
    }
}

// ============================================================================
// Socket 操作 — 薄包装器，委托到 STREAM_SESSION 成员函数
// ============================================================================

bool StreamNode::createUDPPullSocket() {
    return session_origin_pull_.createUDPConsecutiveSockets(false);
}

bool StreamNode::createUDPPushSocket() {
    return session_relay_push_.createUDPConsecutiveSockets(false);
}

bool StreamNode::createUDPServerSocket(STREAM_SESSION& streamInfo) {
    return streamInfo.createUDPConsecutiveSockets(true);
}

void StreamNode::closeUDPSockets() {
    session_origin_pull_.closeSockets();
    session_relay_push_.closeSockets();
}

bool StreamNode::sendUDPDataToSession(const uint8_t* data, size_t size, STREAM_SESSION& rtspSession) {
    bool ok = rtspSession.sendUDPData(data, size);
    if (!ok) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.errors++;
    }
    return ok;
}

int StreamNode::receiveUDPData(uint8_t* buffer, size_t size, std::string& src_ip, int& src_port) {
    return session_origin_pull_.receiveUDPData(buffer, size, src_ip, src_port);
}




