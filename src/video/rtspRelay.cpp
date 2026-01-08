#include "rtspRelay.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <algorithm>
#include <random>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define SOCKET_ERROR_NUM WSAGetLastError()
    #define CLOSE_SOCKET closesocket
    #define SOCKET_TYPE SOCKET
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/select.h>
    #include <sys/time.h>
    #include <fcntl.h>
    #include <errno.h>
    #define SOCKET_ERROR_NUM errno
    #define CLOSE_SOCKET close
    #define SOCKET_TYPE int
    #define INVALID_SOCKET_VALUE -1
#endif

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

// ============================================================================
// Connection 实现
// ============================================================================

RTSPRelay::Connection::Connection() : sockfd_(-1) {
}

RTSPRelay::Connection::~Connection() {
    disconnect();
}

bool RTSPRelay::Connection::connect(const std::string& host, int port, int timeout_ms) {
    disconnect();
    
    // 创建socket
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
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
            CLOSE_SOCKET(sockfd_);
            sockfd_ = -1;
            return false;
        }
        memcpy(&serv_addr.sin_addr, server->h_addr, server->h_length);
    }
    
    // 连接
    if (::connect(sockfd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        CLOSE_SOCKET(sockfd_);
        sockfd_ = -1;
        return false;
    }
    
    host_ = host;
    port_ = port;
    
    // 恢复为阻塞模式
    setSocketTimeout(0);
    
    return true;
}

void RTSPRelay::Connection::disconnect() {
    if (sockfd_ >= 0) {
        CLOSE_SOCKET(sockfd_);
        sockfd_ = -1;
    }
    host_.clear();
    port_ = 0;
}

bool RTSPRelay::Connection::isConnected() const {
    return sockfd_ >= 0;
}

size_t RTSPRelay::Connection::send(const void* data, size_t size, int timeout_ms) {
    if (sockfd_ < 0) return -1;
    
    if (timeout_ms > 0) {
        setSocketTimeout(timeout_ms);
    }
    
    size_t sent = ::send(sockfd_, (const char*)data, size, 0);
    
    if (timeout_ms > 0) {
        setSocketTimeout(0);
    }
    
    return sent;
}

size_t RTSPRelay::Connection::receive(void* buffer, size_t size, int timeout_ms) {
    if (sockfd_ < 0) return -1;
    
    if (timeout_ms > 0) {
        setSocketTimeout(timeout_ms);
    }
    
    size_t received = ::recv(sockfd_, (char*)buffer, size, 0);
    
    if (timeout_ms > 0) {
        setSocketTimeout(0);
    }
    
    return received;
}

size_t IsValidPkt_HTTP(string& strData, size_t iLen)
{
    size_t iPos_contentLengthLineStart = strData.find("Content-Length:"); //15
    //没有http body的情况
    if (iPos_contentLengthLineStart == string::npos)
    {
        auto iDataLen = strData.length();
        if (iDataLen >= 4)
        {
            string tail = strData.substr(iDataLen - 4, 4);
            if (tail == "\r\n\r\n")
                return iLen;
            else
                return 0;
        }
    }
    else
    {
        size_t iPos_contentLengthLineEnd = strData.find("\r\n", iPos_contentLengthLineStart);
        if (iPos_contentLengthLineEnd == string::npos)
            return 0;

        string strLen = strData.substr(iPos_contentLengthLineStart + 15, iPos_contentLengthLineEnd - (iPos_contentLengthLineStart + 15));
        size_t iContentLen = atoi(strLen.c_str());

        size_t iBodyStart = 0;
        for (size_t i = iPos_contentLengthLineEnd; i + 3 < iLen; i++)
        {
            if (strData[i] == '\r' &&
                strData[i + 1] == '\n' &&
                strData[i + 2] == '\r' &&
                strData[i + 3] == '\n'
                )
            {
                iBodyStart = i + 4;
                break; //找到header后面的空行 ，后面就是body。必须break。因为body数据里面可能也有两个换行
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



size_t RTSPRelay::Connection::receiveHttpResp(std::string& response, int timeout_ms) {
    if (sockfd_ < 0) return -1;
    
    size_t total = 0;
    char buf[1000] = { 0 };
    
    if (timeout_ms > 0) {
        setSocketTimeout(timeout_ms);
    }
    
    while (true) {
        int n = ::recv(sockfd_, buf, 1000, 0);
        if (n <= 0) {
            if (timeout_ms > 0) setSocketTimeout(0);
            return (total > 0) ? total : n;
        }
        
        total += n;
        response.append(buf, n);

        if (IsValidPkt_HTTP(response, total)) {
            break;
        }
    }
    
    if (timeout_ms > 0) {
        setSocketTimeout(0);
    }
    
    return total;
}

bool RTSPRelay::Connection::setSocketTimeout(int timeout_ms) {
    if (sockfd_ < 0) return false;
    
#ifdef _WIN32
    DWORD tv = timeout_ms;
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
#endif
    
    int result = setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, 
                           (const char*)&tv, sizeof(tv));
    if (result == 0) {
        result = setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, 
                           (const char*)&tv, sizeof(tv));
    }
    
    return result == 0;
}

// ============================================================================
// RTSPRelay 实现
// ============================================================================

RTSPRelay::RTSPRelay() {
    stats_.start_time = std::chrono::steady_clock::now();
    stats_.last_frame_time = std::chrono::steady_clock::now();
}

RTSPRelay::~RTSPRelay() {
    stop();
}

bool RTSPRelay::start(const Config& config) {
    if (running_) {
        logError("Already running");
        return false;
    }
    
    config_ = config;
    running_ = true;
    stopping_ = false;
    retry_count_ = 0;
    
    setState(State::CONNECTING, "Starting RTSP relay");
    
    worker_thread_ = std::thread(&RTSPRelay::workerThread, this);
    stats_thread_ = std::thread(&RTSPRelay::statsThread, this);
    
    logInfo("RTSP relay started");
    logInfo("Source: " + config_.source_url);
    logInfo("Target: " + config_.target_url);
    
    return true;
}

void RTSPRelay::stop() {
    if (!running_) return;
    
    stopping_ = true;
    running_ = false;
    
    cv_.notify_all();
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    if (rtp_thread_.joinable()) {
        rtp_thread_.join();
    }
    
    if (control_thread_.joinable()) {
        control_thread_.join();
    }
    
    if (stats_thread_.joinable()) {
        stats_thread_.join();
    }
    
    if (source_conn_) {
        source_conn_->disconnect();
    }
    
    if (target_conn_) {
        target_conn_->disconnect();
    }
    
    if (rtp_socket_ >= 0) {
        CLOSE_SOCKET(rtp_socket_);
        rtp_socket_ = -1;
    }
    
    if (rtcp_socket_ >= 0) {
        CLOSE_SOCKET(rtcp_socket_);
        rtcp_socket_ = -1;
    }
    
    setState(State::IDLE, "Stopped");
    logInfo("RTSP relay stopped");
}

void RTSPRelay::restart() {
    stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    start(config_);
}

RTSPRelay::State RTSPRelay::getState(){
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

bool RTSPRelay::isRunning(){
    return running_;
}

RTSPRelay::Statistics RTSPRelay::getStatistics(){
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    // 计算实时统计
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - stats_.last_frame_time).count();
    
    if (elapsed > 0) {
        stats_.fps = stats_.frames_received / elapsed;
        stats_.bitrate = (stats_.bytes_received * 8 / 1000.0) / elapsed;
    }
    
    return stats_;
}

void RTSPRelay::setStatusCallback(StatusCallback cb) {
    status_callback_ = cb;
}

void RTSPRelay::setFrameCallback(FrameCallback cb) {
    frame_callback_ = cb;
}

void RTSPRelay::setErrorCallback(ErrorCallback cb) {
    error_callback_ = cb;
}

void RTSPRelay::workerThread() {
    logInfo("Worker thread started");
    
    while (running_ && !stopping_) {
        try {
            if (!connectToSource()) {
                if (shouldReconnect()) {
                    doReconnect();
                    continue;
                } else {
                    break;
                }
            }
            
            if (!setupStreams()) {
                if (shouldReconnect()) {
                    doReconnect();
                    continue;
                } else {
                    break;
                }
            }
            
            // 启动RTP接收线程
            rtp_thread_ = std::thread(&RTSPRelay::rtpThread, this);
            control_thread_ = std::thread(&RTSPRelay::controlThread, this);
            
            setState(State::PLAYING, "Streaming started");
            
            // 等待结束
            while (running_ && !stopping_) {
                std::unique_lock<std::mutex> lock(state_mutex_);
                cv_.wait_for(lock, std::chrono::seconds(1));
                
                // 检查RTP超时
                auto now = std::chrono::steady_clock::now();
                if (now - last_rtp_time_ > std::chrono::milliseconds(config_.rtp_timeout)) {
                    logError("RTP timeout detected");
                    setError("RTP timeout", 1001);
                    break;
                }
            }
            
            if (rtp_thread_.joinable()) {
                rtp_thread_.join();
            }
            
            if (control_thread_.joinable()) {
                control_thread_.join();
            }
            
            teardown();
            
        } catch (const std::exception& e) {
            setError(std::string("Worker thread exception: ") + e.what(), 1000);
        }
        
        if (running_ && !stopping_ && shouldReconnect()) {
            doReconnect();
        }
    }
    
    logInfo("Worker thread stopped");
}

bool RTSPRelay::connectToSource() {
    setState(State::CONNECTING, "Connecting to source");
    
    // 解析源URL
    URLComponents src_url;
    if (!URLComponents::parse(config_.source_url, src_url)) {
        setError("Invalid source URL format", 2001);
        return false;
    }
    
    // 连接到源服务器
    source_conn_ = std::make_unique<Connection>();
    if (!source_conn_->connect(src_url.host, src_url.port)) {
        setError("Failed to connect to source server: " + src_url.host + ":" + std::to_string(src_url.port), 2002);
        return false;
    }
    
    logInfo("Connected to source server");
    
    // 发送DESCRIBE
    std::string sdp;
    if (!rtspDescribe(*source_conn_, config_.source_url, sdp, source_session_)) {
        setError("DESCRIBE failed", 2003);
        return false;
    }
    
    // 解析SDP
    if (!parseSDP(sdp, source_video_info_, source_audio_info_)) {
        setError("Failed to parse SDP", 2004);
        return false;
    }
    
    setState(State::CONNECTED, "Source connected");
    return true;
}

bool RTSPRelay::setupStreams() {
    setState(State::CONNECTING, "Setting up streams");
    
    // 创建RTP接收套接字
    rtp_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (rtp_socket_ < 0) {
        setError("Failed to create RTP socket", 3001);
        return false;
    }
    
    // 绑定到任意端口
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(50000);
    
    if (::bind(rtp_socket_, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        setError("Failed to bind RTP socket", 3002);
        return false;
    }
    
    // 获取绑定的端口
    socklen_t len = sizeof(local_addr);
    getsockname(rtp_socket_, (struct sockaddr*)&local_addr, &len);
    int rtp_port = ntohs(local_addr.sin_port);
    
    source_video_info_.client_port = std::to_string(rtp_port) + "-" + std::to_string(rtp_port + 1);
    
    // 发送SETUP到源
    if (!rtspSetup(*source_conn_, config_.source_url, source_session_, source_video_info_)) {
        setError("SETUP failed for source", 3003);
        return false;
    }
    
    // 解析传输信息
    std::istringstream transport_stream(source_video_info_.transport);
    std::string token;
    while (std::getline(transport_stream, token, ';')) {
        if (token.find("server_port=") != std::string::npos) {
            size_t pos = token.find('=');
            source_video_info_.server_port = token.substr(pos + 1);
            
            // 解析RTP端口
            size_t dash = source_video_info_.server_port.find('-');
            if (dash != std::string::npos) {
                source_video_info_.source_rtp_port = std::stoi(source_video_info_.server_port.substr(0, dash));
            }
        } else if (token.find("source=") != std::string::npos) {
            size_t pos = token.find('=');
            source_video_info_.source_host = token.substr(pos + 1);
        }
    }
    
    // 发送PLAY
    if (!rtspPlay(*source_conn_, config_.source_url, source_session_)) {
        setError("PLAY failed", 3004);
        return false;
    }

    //Play成功后表示拉流成功
    //下面开始推流
    
    // 连接到目标服务器
    URLComponents target_url;
    if (!URLComponents::parse(config_.target_url, target_url)) {
        setError("Invalid target URL format", 3005);
        return false;
    }
    
    target_conn_ = std::make_unique<Connection>();
    if (!target_conn_->connect(target_url.host, target_url.port)) {
        setError("Failed to connect to target server", 3006);
        return false;
    }
    
    // 生成目标SDP
    target_video_info_ = source_video_info_;
    target_video_info_.control_url = config_.target_url;
    
    std::string target_sdp = generateSDP(target_video_info_, StreamInfo());
    
    // 发送ANNOUNCE到目标
    if (!rtspAnnounce(*target_conn_, config_.target_url, target_sdp, target_session_)) {
        setError("ANNOUNCE failed", 3007);
        return false;
    }
    
    // 发送SETUP到目标
    if (!rtspSetup(*target_conn_, config_.target_url, target_session_, target_video_info_)) {
        setError("SETUP failed for target", 3008);
        return false;
    }
    
    // 发送RECORD到目标
    if (!rtspRecord(*target_conn_, config_.target_url, target_session_)) {
        setError("RECORD failed", 3009);
        return false;
    }
    
    return true;
}

void RTSPRelay::rtpThread() {
    logInfo("RTP thread started");
    
    std::vector<uint8_t> buffer(config_.buffer_size);
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    last_rtp_time_ = std::chrono::steady_clock::now();
    
    while (running_ && !stopping_) {
        // 设置接收超时
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(rtp_socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        
        long received = recvfrom(rtp_socket_, (char*)buffer.data(), buffer.size(), 0,
                                   (struct sockaddr*)&from_addr, &from_len);
        
        if (received > 0) {
            last_rtp_time_ = std::chrono::steady_clock::now();
            
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_received += received;
                stats_.frames_received++;
                stats_.last_frame_time = last_rtp_time_;
            }
            
            // 解析RTP包
            RTPPacket packet;
            if (packet.parse(buffer.data(), received)) {
                // 转发到目标
                forwardRTPPacket(packet);
                
                // 调用回调
                if (frame_callback_) {
                    frame_callback_(packet.payload.data(), packet.payload.size(), packet.timestamp);
                }
                
                logVerbose("RTP packet: seq=" + std::to_string(packet.sequence_number) +
                          " ts=" + std::to_string(packet.timestamp) +
                          " size=" + std::to_string(received));
            }
        } else if (received < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err != WSAETIMEDOUT && err != WSAEWOULDBLOCK) {
#else
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
#endif
                logError("recvfrom error: " + std::to_string(err));
                break;
            }
        }
    }
    
    logInfo("RTP thread stopped");
}

void RTSPRelay::controlThread() {
    logInfo("Control thread started");
    
    while (running_ && !stopping_) {
        // 定期发送RTCP接收报告（简化版本）
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 这里可以添加RTCP处理逻辑
        logVerbose("Control thread heartbeat");
    }
    
    logInfo("Control thread stopped");
}

void RTSPRelay::statsThread() {
    logInfo("Statistics thread started");
    
    while (running_ && !stopping_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        auto stats = getStatistics();
        
        if (config_.verbose) {
            std::stringstream ss;
            ss << "Stats: frames=" << stats.frames_received
               << " forwarded=" << stats.frames_forwarded
               << " fps=" << std::fixed << std::setprecision(1) << stats.fps
               << " bitrate=" << stats.bitrate << "kbps"
               << " errors=" << stats.errors;
            logDebug(ss.str());
        }
    }
    
    logInfo("Statistics thread stopped");
}

bool RTSPRelay::rtspDescribe(Connection& conn, const std::string& url, 
                            std::string& sdp, std::string& session) {
    std::stringstream request;
    request << "DESCRIBE " << url << " RTSP/1.0\r\n"
            << "CSeq: " << generateCSeq() << "\r\n"
            << "User-Agent: RTSPRelay/1.0\r\n"
            << "Accept: application/sdp\r\n"
            << "\r\n";
    
    if (conn.send(request.str().c_str(), request.str().size()) <= 0) {
        return false;
    }
    
    // 读取响应
    std::string response;
    
    conn.receiveHttpResp(response, 5000);
    
    if (response.find("200 OK") == std::string::npos) {
        logError("DESCRIBE failed: " + response.substr(0, 100));
        return false;
    }
    
    // 提取SDP
    size_t sdp_start = response.find("\r\n\r\n");
    if (sdp_start != std::string::npos) {
        sdp = response.substr(sdp_start + 4);
    }
    
    // 提取Session ID
    /* SDP格式
        v=0
        o=- 17080127654019669005 17080127654019669005 IN IP4 DESKTOP-IKTCMTL
        s=Unnamed
        i=N/A
        c=IN IP4 0.0.0.0
        t=0 0
        a=tool:vlc 3.0.21
        a=recvonly
        a=type:broadcast
        a=charset:UTF-8
        a=control:rtsp://127.0.0.1:8554/1
        m=video 0 RTP/AVP 96
        b=RR:0
        a=rtpmap:96 H264/90000
        a=fmtp:96 packetization-mode=1;profile-level-id=64001e;sprop-parameter-sets=Z2QAHqzZQKAv+XAWoMAgqAAAH0gAB1MEeLFssA==,aOvjyyLA;
        a=control:rtsp://127.0.0.1:8554/1/trackID=352
    */
    session = extractSessionID(response);
    
    return true;
}

bool RTSPRelay::rtspSetup(Connection& conn, const std::string& url, 
                         const std::string& session, StreamInfo& stream) {
    std::stringstream request;
    request << "SETUP " << (stream.control_url.empty() ? url : stream.control_url) << " RTSP/1.0\r\n"
            << "CSeq: " << generateCSeq() << "\r\n"
            << "User-Agent: RTSPRelay/1.0\r\n";
    
    if (!session.empty()) {
        request << "Session: " << session << "\r\n";
    }
    
    request << "Transport: RTP/AVP;unicast;client_port=" << stream.client_port << "\r\n"
            << "\r\n";
    
    if (conn.send(request.str().c_str(), request.str().size()) <= 0) {
        return false;
    }
    
    std::string response;
    conn.receiveHttpResp(response, 5000);
    
    if (response.find("200 OK") == std::string::npos) {
        logError("SETUP failed: " + response.substr(0, 100));
        return false;
    }
    
    stream.transport = extractTransport(response);
    
    std::string new_session = extractSessionID(response);
    if (!new_session.empty()) {
        if (session.empty()) {
            const_cast<std::string&>(session) = new_session;
        }
    }
    
    return true;
}

bool RTSPRelay::rtspPlay(Connection& conn, const std::string& url, 
                        const std::string& session) {
    std::stringstream request;
    request << "PLAY " << url << " RTSP/1.0\r\n"
            << "CSeq: " << generateCSeq() << "\r\n"
            << "User-Agent: RTSPRelay/1.0\r\n"
            << "Session: " << session << "\r\n"
            << "Range: npt=0.000-\r\n"
            << "\r\n";
    
    if (conn.send(request.str().c_str(), request.str().size()) <= 0) {
        return false;
    }
    
    std::string response;
    conn.receiveHttpResp(response, 5000);
    
    if (response.find("200 OK") == std::string::npos) {
        logError("PLAY failed: " + response.substr(0, 100));
        return false;
    }
    
    return true;
}

bool RTSPRelay::rtspTeardown(Connection& conn, const std::string& url, 
                            const std::string& session) {
    std::stringstream request;
    request << "TEARDOWN " << url << " RTSP/1.0\r\n"
            << "CSeq: " << generateCSeq() << "\r\n"
            << "User-Agent: RTSPRelay/1.0\r\n"
            << "Session: " << session << "\r\n"
            << "\r\n";
    
    if (conn.send(request.str().c_str(), request.str().size()) <= 0) {
        return false;
    }
    
    std::string response;
    conn.receiveHttpResp(response, 5000);
    
    return true;
}

bool RTSPRelay::rtspAnnounce(Connection& conn, const std::string& url, 
                            const std::string& sdp, std::string& session) {
    std::stringstream request;
    request << "ANNOUNCE " << url << " RTSP/1.0\r\n"
            << "CSeq: " << generateCSeq() << "\r\n"
            << "User-Agent: RTSPRelay/1.0\r\n"
            << "Content-Type: application/sdp\r\n"
            << "Content-Length: " << sdp.size() << "\r\n"
            << "\r\n"
            << sdp;
    
    if (conn.send(request.str().c_str(), request.str().size()) <= 0) {
        return false;
    }
    
    std::string response;
    conn.receiveHttpResp(response, 5000);
    
    if (response.find("200 OK") == std::string::npos) {
        logError("ANNOUNCE failed: " + response.substr(0, 100));
        return false;
    }
    
    session = extractSessionID(response);
    
    return true;
}

bool RTSPRelay::rtspRecord(Connection& conn, const std::string& url, 
                          const std::string& session) {
    std::stringstream request;
    request << "RECORD " << url << " RTSP/1.0\r\n"
            << "CSeq: " << generateCSeq() << "\r\n"
            << "User-Agent: RTSPRelay/1.0\r\n"
            << "Session: " << session << "\r\n"
            << "Range: npt=0.000-\r\n"
            << "\r\n";
    
    if (conn.send(request.str().c_str(), request.str().size()) <= 0) {
        return false;
    }
    
    std::string response;
    conn.receiveHttpResp(response, 5000);
    
    if (response.find("200 OK") == std::string::npos) {
        logError("RECORD failed: " + response.substr(0, 100));
        return false;
    }
    
    return true;
}

void RTSPRelay::teardown() {
    if (source_conn_ && !source_session_.empty()) {
        rtspTeardown(*source_conn_, config_.source_url, source_session_);
    }
    
    if (target_conn_ && !target_session_.empty()) {
        rtspTeardown(*target_conn_, config_.target_url, target_session_);
    }
    
    if (source_conn_) {
        source_conn_->disconnect();
    }
    
    if (target_conn_) {
        target_conn_->disconnect();
    }
    
    source_session_.clear();
    target_session_.clear();
}

bool RTSPRelay::parseSDP(const std::string& sdp, StreamInfo& video_info, StreamInfo& audio_info) {
    std::istringstream ss(sdp);
    std::string line;
    StreamInfo* current_info = nullptr;
    
    while (std::getline(ss, line)) {
        if (line.length() < 2 || line[1] != '=') continue;
        
        char type = line[0];
        std::string value = line.substr(2,line.length() - 3); //末尾 \r不要
        
        switch (type) {
            case 'm': {  // 媒体行
                std::istringstream mstream(value);
                std::string media_type, port_str, proto, fmt;
                mstream >> media_type >> port_str >> proto >> fmt;
                
                if (media_type == "video") {
                    current_info = &video_info;
                    video_info.payload_type = std::stoi(fmt);
                } else if (media_type == "audio") {
                    current_info = &audio_info;
                    audio_info.payload_type = std::stoi(fmt);
                } else {
                    current_info = nullptr;
                }
                break;
            }
            
            case 'a':  // 属性
                if (!current_info) break;
                
                if (value.find("rtpmap:") == 0) {
                    size_t colon = value.find(':');
                    size_t space = value.find(' ', colon);
                    size_t slash = value.find('/', space);
                    
                    if (slash != std::string::npos) {
                        std::string codec_str = value.substr(space + 1, slash - space - 1);
                        current_info->codec = codec_str;
                        
                        std::string rate_str = value.substr(slash + 1);
                        size_t second_slash = rate_str.find('/');
                        if (second_slash != std::string::npos) {
                            rate_str = rate_str.substr(0, second_slash);
                        }
                        current_info->clock_rate = std::stoi(rate_str);
                    }
                } 
                else if (value.find("fmtp:") == 0) {
                    size_t fmtp_start = value.find(' ');
                    if (fmtp_start != std::string::npos) {
                        current_info->fmtp = value.substr(fmtp_start + 1);
                    }
                }
                else if (value.find("control:") == 0) {
                    current_info->control_url = value.substr(8);
                }
                break;
        }
    }
    
    return true;
}

std::string RTSPRelay::generateSDP(const StreamInfo& video_info, const StreamInfo& audio_info) {
    std::stringstream sdp;
    
    sdp << "v=0\r\n"
        << "o=- 0 0 IN IP4 0.0.0.0\r\n"
        << "s=RTSP Relay Stream\r\n"
        << "c=IN IP4 0.0.0.0\r\n"
        << "t=0 0\r\n";
    
    if (video_info.payload_type > 0) {
        sdp << "m=video 0 RTP/AVP " << video_info.payload_type << "\r\n"
            << "a=rtpmap:" << video_info.payload_type << " " 
            << video_info.codec << "/" << video_info.clock_rate << "\r\n";
        
        if (!video_info.fmtp.empty()) {
            sdp << "a=fmtp:" << video_info.payload_type << " " << video_info.fmtp << "\r\n";
        }
        
        sdp << "a=control:" << video_info.control_url << "\r\n";
    }
    
    /*
    if (audio_info.payload_type > 0) {
        sdp << "m=audio 0 RTP/AVP " << audio_info.payload_type << "\r\n"
            << "a=rtpmap:" << audio_info.payload_type << " " 
            << audio_info.codec << "/" << audio_info.clock_rate << "\r\n";
        
        if (!audio_info.fmtp.empty()) {
            sdp << "a=fmtp:" << audio_info.payload_type << " " << audio_info.fmtp << "\r\n";
        }
        
        sdp << "a=control:" << audio_info.control_url << "\r\n";
    }*/
    
    return sdp.str();
}

bool RTSPRelay::URLComponents::parse(const std::string& url, URLComponents& components) {
    size_t protocol_end = url.find("://");
    if (protocol_end == std::string::npos) {
        return false;
    }
    
    components.protocol = url.substr(0, protocol_end);
    
    std::string rest = url.substr(protocol_end + 3);
    
    // 查找路径
    size_t slash_pos = rest.find('/');
    std::string host_port;
    
    if (slash_pos != std::string::npos) {
        host_port = rest.substr(0, slash_pos);
        components.path = rest.substr(slash_pos);
    } else {
        host_port = rest;
        components.path = "/";
    }
    
    // 查找端口
    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        components.host = host_port.substr(0, colon_pos);
        std::string port_str = host_port.substr(colon_pos + 1);
        components.port = std::stoi(port_str);
    } else {
        components.host = host_port;
        components.port = (components.protocol == "rtsp") ? 554 : 80;
    }
    
    return true;
}

void RTSPRelay::forwardRTPPacket(const RTPPacket& packet) {
    if (!target_conn_ || target_video_info_.source_rtp_port == 0) {
        return;
    }
    
    // 构建目标地址
    URLComponents target_url;
    if (!URLComponents::parse(config_.target_url, target_url)) {
        return;
    }
    
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(target_video_info_.source_rtp_port);
    
    if (inet_pton(AF_INET, target_url.host.c_str(), &target_addr.sin_addr) <= 0) {
        struct hostent* server = gethostbyname(target_url.host.c_str());
        if (!server) return;
        memcpy(&target_addr.sin_addr, server->h_addr, server->h_length);
    }
    
    // 序列化并发送RTP包
    auto data = packet.serialize();
    sendto(rtp_socket_, (const char*)data.data(), data.size(), 0,
           (struct sockaddr*)&target_addr, sizeof(target_addr));
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.bytes_forwarded += data.size();
    stats_.frames_forwarded++;
}

bool RTSPRelay::RTPPacket::parse(const uint8_t* data, size_t size) {
    if (size < 12) return false;  // RTP头部至少12字节
    
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

std::vector<uint8_t> RTSPRelay::RTPPacket::serialize() const {
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

std::string RTSPRelay::extractSessionID(const std::string& response) {
    size_t pos = response.find("Session: ");
    if (pos == std::string::npos) return "";
    
    size_t end = response.find("\r\n", pos);
    std::string session_line = response.substr(pos, end - pos);
    
    std::string session = session_line.substr(9);
    
    // 移除超时部分
    size_t timeout_pos = session.find(';');
    if (timeout_pos != std::string::npos) {
        session = session.substr(0, timeout_pos);
    }
    
    return session;
}

std::string RTSPRelay::extractTransport(const std::string& response) {
    size_t pos = response.find("Transport: ");
    if (pos == std::string::npos) return "";
    
    size_t end = response.find("\r\n", pos);
    return response.substr(pos + 11, end - pos - 11);
}

std::string RTSPRelay::generateCSeq() {
    static std::atomic<int> counter{1};
    return std::to_string(counter++);
}

void RTSPRelay::setError(const std::string& error, int code) {
    logError("Error [" + std::to_string(code) + "]: " + error);
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.errors++;
    }
    
    if (error_callback_) {
        error_callback_(error, code);
    }
    
    setState(State::S_ERROR, error);
}

void RTSPRelay::setState(State new_state, const std::string& msg) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = new_state;
    }
    
    if (status_callback_) {
        status_callback_(new_state, msg);
    }
    
    logInfo("State changed to " + std::to_string(static_cast<int>(new_state)) + ": " + msg);
    cv_.notify_all();
}

bool RTSPRelay::shouldReconnect() const {
    if (retry_count_ >= config_.max_retries) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    if (now - last_reconnect_time_ < std::chrono::milliseconds(config_.retry_interval)) {
        return false;
    }
    
    return true;
}

void RTSPRelay::doReconnect() {
    retry_count_++;
    last_reconnect_time_ = std::chrono::steady_clock::now();
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.reconnect_count++;
    
    setState(State::RECONNECTING, 
            "Reconnecting (attempt " + std::to_string(retry_count_) + 
            "/" + std::to_string(config_.max_retries) + ")");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(config_.retry_interval));
}

void RTSPRelay::logInfo(const std::string& msg) const {
    std::cout << "[INFO] " << msg << std::endl;
}

void RTSPRelay::logError(const std::string& msg) const {
    std::cerr << "[ERROR] " << msg << std::endl;
}

void RTSPRelay::logDebug(const std::string& msg) const {
    if (config_.verbose) {
        std::cout << "[DEBUG] " << msg << std::endl;
    }
}

void RTSPRelay::logVerbose(const std::string& msg) const {
    if (config_.verbose) {
        std::cout << "[VERBOSE] " << msg << std::endl;
    }
}