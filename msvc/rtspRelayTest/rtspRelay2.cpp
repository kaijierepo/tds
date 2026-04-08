#include "rtspRelay2.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace {

inline void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
}

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string bytesToHex(const char* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len && i < 256; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)data[i];
        if ((i + 1) % 16 == 0) oss << "\n";
        else if ((i + 1) % 8 == 0) oss << "  ";
        else oss << " ";
    }
    return oss.str();
}

} // anonymous namespace

namespace rtsp {

Relay::Relay() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

Relay::~Relay() {
    stop();
    WSACleanup();
}

void Relay::logVerbose(const std::string& msg) {
    if (config_.verbose) {
        if (verbose_cb_) {
            verbose_cb_(msg.c_str());
        }
    }
}

void Relay::logError(const std::string& msg) {
    last_error_ = msg;
    if (verbose_cb_) {
        std::string err = "[ERROR] " + msg;
        verbose_cb_(err.c_str());
    }
}

bool Relay::connectToServer(const std::string& host, int port) {
    disconnect();
    
    sockfd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd_ == INVALID_SOCKET) {
        logError("socket() failed: " + std::to_string(WSAGetLastError()));
        return false;
    }
    
    // Disable Nagle algorithm
    int nodelay = 1;
    setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        struct hostent* he = gethostbyname(host.c_str());
        if (!he) {
            logError("gethostbyname() failed for: " + host);
            disconnect();
            return false;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    
    DWORD timeout = config_.timeout_ms;
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    if (connect(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        logError("connect() failed: " + std::to_string(err));
        disconnect();
        return false;
    }
    
    logVerbose("Connected to " + host + ":" + std::to_string(port));
    return true;
}

void Relay::disconnect() {
    if (sockfd_ != INVALID_SOCKET) {
        closesocket(sockfd_);
        sockfd_ = INVALID_SOCKET;
    }
}

bool Relay::sendRequest(const std::string& request) {
    if (sockfd_ == INVALID_SOCKET) {
        logError("Not connected");
        return false;
    }
    
    int sent = send(sockfd_, request.c_str(), (int)request.length(), 0);
    if (sent == SOCKET_ERROR) {
        logError("send() failed: " + std::to_string(WSAGetLastError()));
        return false;
    }
    
    logVerbose(">> TX " + std::to_string(sent) + " bytes:\n" + request);
    return true;
}

int Relay::receiveResponse(std::string& response, int timeout_ms) {
    response.clear();
    if (sockfd_ == INVALID_SOCKET) {
        return -1;
    }
    
    char buffer[8192];
    int total = 0;
    fd_set readfds;
    struct timeval tv;
    
    auto start = GetTickCount64();
    
    while (true) {
        DWORD remaining = (DWORD)(timeout_ms - (GetTickCount64() - start));
        if (remaining > (DWORD)timeout_ms) remaining = timeout_ms;
        
        FD_ZERO(&readfds);
        FD_SET(sockfd_, &readfds);
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;
        
        int ret = select(0, &readfds, NULL, NULL, &tv);
        if (ret <= 0) {
            if (total > 0) {
                logVerbose("<< RX partial " + std::to_string(total) + " bytes (timeout)");
                return total;
            }
            return -1;
        }
        
        int n = recv(sockfd_, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            int err = WSAGetLastError();
            if (total > 0) {
                logVerbose("<< RX " + std::to_string(total) + " bytes");
                return total;
            }
            logError("recv() failed: " + std::to_string(err));
            return -1;
        }
        
        buffer[n] = '\0';
        response.append(buffer, n);
        total += n;
        
        // Check if we have complete RTSP response
        size_t header_end = response.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            size_t cl_pos = response.find("Content-Length:");
            if (cl_pos != std::string::npos && cl_pos < header_end) {
                size_t val_start = response.find_first_not_of(" \t", cl_pos + 15);
                size_t val_end = response.find("\r\n", val_start);
                std::string cl_str = response.substr(val_start, val_end - val_start);
                int content_length = std::stoi(cl_str);
                
                size_t body_start = header_end + 4;
                size_t body_len = response.length() - body_start;
                
                if ((int)body_len >= content_length) {
                    logVerbose("<< RX complete " + std::to_string(total) + " bytes");
                    return total;
                }
            } else {
                logVerbose("<< RX " + std::to_string(total) + " bytes");
                return total;
            }
        }
        
        if (GetTickCount64() - start > (DWORD)timeout_ms) {
            break;
        }
    }
    
    logVerbose("<< RX " + std::to_string(total) + " bytes (timeout)");
    return total;
}

std::string Relay::extractSession(const std::string& response) {
    size_t pos = toLower(response).find("session:");
    if (pos == std::string::npos) return "";
    
    pos += 8;
    while (pos < response.length() && (response[pos] == ' ' || response[pos] == '\t')) pos++;
    
    size_t end = pos;
    while (end < response.length() && response[end] != ';' && response[end] != '\r' && response[end] != '\n') end++;
    
    std::string session = response.substr(pos, end - pos);
    trim(session);
    return session;
}

bool Relay::parseUrl(const std::string& url, std::string& host, int& port, std::string& path) {
    if (url.substr(0, 7) != "rtsp://") {
        logError("Invalid URL format: " + url);
        return false;
    }
    
    std::string rest = url.substr(7);
    
    size_t colon_pos = rest.find(':');
    size_t slash_pos = rest.find('/');
    
    if (colon_pos != std::string::npos && (slash_pos == std::string::npos || colon_pos < slash_pos)) {
        host = rest.substr(0, colon_pos);
        
        if (slash_pos != std::string::npos) {
            port = std::stoi(rest.substr(colon_pos + 1, slash_pos - colon_pos - 1));
            path = rest.substr(slash_pos);
        } else {
            port = std::stoi(rest.substr(colon_pos + 1));
            path = "/";
        }
    } else {
        if (slash_pos != std::string::npos) {
            host = rest.substr(0, slash_pos);
            path = rest.substr(slash_pos);
        } else {
            host = rest;
            path = "/";
        }
        port = 554;
    }
    
    return true;
}

std::string Relay::buildSetupUrl(const std::string& base_url, const std::string& control_url) {
    if (control_url.empty()) return base_url;
    
    if (control_url.substr(0, 7) == "rtsp://") {
        return control_url;
    }
    
    if (control_url[0] == '/') {
        std::string host, path;
        int port;
        if (parseUrl(base_url, host, port, path)) {
            std::ostringstream oss;
            oss << "rtsp://" << host << ":" << port << control_url;
            return oss.str();
        }
        return control_url;
    }
    
    if (base_url.back() == '/') {
        return base_url + control_url;
    }
    return base_url + "/" + control_url;
}

bool Relay::parseSDP(const std::string& sdp) {
    video_info_ = StreamInfo();
    audio_info_ = StreamInfo();
    
    std::istringstream iss(sdp);
    std::string line;
    StreamInfo* current_info = nullptr;
    
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        
        if (line.substr(0, 2) == "m=") {
            if (line.substr(2, 5) == "video") {
                current_info = &video_info_;
            } else if (line.substr(2, 5) == "audio") {
                current_info = &audio_info_;
            } else {
                current_info = nullptr;
            }
        }
        
        if (current_info) {
            current_info->sdp_lines += line + "\r\n";
            
            if (line.substr(0, 10) == "a=control:") {
                current_info->control_url = line.substr(10);
                logVerbose("SDP control URL: " + current_info->control_url);
            }
            else if (line.substr(0, 6) == "m=video" || line.substr(0, 6) == "m=audio") {
                std::istringstream mss(line);
                std::string m, media, proto, fmt;
                mss >> m >> media >> proto >> fmt;
                if (fmt != "(null)" && !fmt.empty()) {
                    current_info->port = std::stoi(fmt);
                }
            }
        }
    }
    
    logVerbose("SDP parsed: video=" + video_info_.control_url + 
               ", audio=" + audio_info_.control_url);
    
    return !video_info_.control_url.empty() || !audio_info_.control_url.empty();
}

bool Relay::sendOptions(const std::string& url) {
    std::ostringstream req;
    req << "OPTIONS " << url << " RTSP/1.0\r\n"
        << "CSeq: " << ++cseq_ << "\r\n"
        << "User-Agent: RTSPRelay/2.0\r\n";
    
    if (!session_.empty()) {
        req << "Session: " << session_ << "\r\n";
    }
    req << "\r\n";
    
    if (!sendRequest(req.str())) return false;
    
    std::string resp;
    if (receiveResponse(resp, 5000) <= 0) {
        logError("OPTIONS response failed");
        return false;
    }
    
    logVerbose("OPTIONS response:\n" + resp);
    return resp.find("200 OK") != std::string::npos;
}

bool Relay::sendDescribe(const std::string& url, std::string& sdp) {
    sdp.clear();
    
    std::ostringstream req;
    req << "DESCRIBE " << url << " RTSP/1.0\r\n"
        << "CSeq: " << ++cseq_ << "\r\n"
        << "User-Agent: RTSPRelay/2.0\r\n"
        << "Accept: application/sdp\r\n"
        << "\r\n";
    
    if (!sendRequest(req.str())) return false;
    
    std::string resp;
    if (receiveResponse(resp, 10000) <= 0) {
        logError("DESCRIBE response failed");
        return false;
    }
    
    logVerbose("DESCRIBE response:\n" + resp);
    
    if (resp.find("200 OK") == std::string::npos) {
        logError("DESCRIBE failed: " + resp);
        return false;
    }
    
    // Extract Session if present
    std::string resp_session = extractSession(resp);
    if (!resp_session.empty() && session_.empty()) {
        session_ = resp_session;
        logVerbose("Session from DESCRIBE: " + session_);
    }
    
    // Extract SDP
    size_t sdp_marker = resp.find("v=0\r\n");
    if (sdp_marker != std::string::npos) {
        sdp = resp.substr(sdp_marker);
    } else {
        size_t sdp_start = resp.find("\r\n\r\n");
        if (sdp_start != std::string::npos) {
            sdp = resp.substr(sdp_start + 4);
        }
    }
    
    return parseSDP(sdp);
}

bool Relay::sendSetup(const std::string& url, const StreamInfo& info, TransportMode mode, bool is_video) {
    std::string setup_url = buildSetupUrl(url, info.control_url);
    
    std::ostringstream req;
    req << "SETUP " << setup_url << " RTSP/1.0\r\n"
        << "CSeq: " << ++cseq_ << "\r\n"
        << "User-Agent: RTSPRelay/2.0\r\n";
    
    if (!session_.empty()) {
        req << "Session: " << session_ << "\r\n";
    }
    
    if (mode == TransportMode::TCP) {
        req << "Transport: RTP/AVP/TCP;unicast;interleaved=" << (is_video ? "0-1" : "2-3") << "\r\n";
    } else {
        req << "Transport: RTP/AVP;unicast;client_port=" << (is_video ? "5000-5001" : "5002-5003") << "\r\n";
    }
    
    req << "\r\n";
    
    logVerbose("SETUP request:\n" + req.str());
    
    if (!sendRequest(req.str())) return false;
    
    std::string resp;
    if (receiveResponse(resp, 10000) <= 0) {
        logError("SETUP response failed");
        return false;
    }
    
    logVerbose("SETUP response:\n" + resp);
    
    if (resp.find("200 OK") == std::string::npos) {
        logError("SETUP failed: " + resp);
        return false;
    }
    
    // Extract Session if present
    std::string resp_session = extractSession(resp);
    if (!resp_session.empty()) {
        session_ = resp_session;
        logVerbose("Session from SETUP: " + session_);
    }
    
    return true;
}

bool Relay::sendPlay(const std::string& url) {
    std::ostringstream req;
    req << "PLAY " << url << " RTSP/1.0\r\n"
        << "CSeq: " << ++cseq_ << "\r\n"
        << "User-Agent: RTSPRelay/2.0\r\n";
    
    if (!session_.empty()) {
        req << "Session: " << session_ << "\r\n";
    }
    req << "\r\n";
    
    if (!sendRequest(req.str())) return false;
    
    std::string resp;
    if (receiveResponse(resp, 5000) <= 0) {
        logError("PLAY response failed");
        return false;
    }
    
    logVerbose("PLAY response:\n" + resp);
    return resp.find("200 OK") != std::string::npos;
}

bool Relay::sendTeardown(const std::string& url) {
    std::ostringstream req;
    req << "TEARDOWN " << url << " RTSP/1.0\r\n"
        << "CSeq: " << ++cseq_ << "\r\n"
        << "User-Agent: RTSPRelay/2.0\r\n";
    
    if (!session_.empty()) {
        req << "Session: " << session_ << "\r\n";
    }
    req << "\r\n";
    
    logVerbose("TEARDOWN request:\n" + req.str());
    sendRequest(req.str());
    
    std::string resp;
    receiveResponse(resp, 3000);
    
    return true;
}

bool Relay::pullStream() {
    logVerbose("=== Starting RTSP Pull ===");
    logVerbose("Source: " + config_.source_url);
    logVerbose("Mode: " + std::string(config_.pull_mode == TransportMode::TCP ? "TCP" : "UDP"));
    
    std::string host, path;
    int port;
    if (!parseUrl(config_.source_url, host, port, path)) {
        return false;
    }
    
    logVerbose("Parsed: host=" + host + ", port=" + std::to_string(port) + ", path=" + path);
    
    if (!connectToServer(host, port)) {
        return false;
    }
    
    cseq_ = 0;
    session_.clear();
    
    // DESCRIBE
    std::string sdp;
    if (!sendDescribe(config_.source_url, sdp)) {
        logError("DESCRIBE failed");
        disconnect();
        return false;
    }
    
    logVerbose("DESCRIBE successful, SDP:\n" + sdp);
    
    // SETUP video track
    if (!video_info_.control_url.empty()) {
        if (!sendSetup(config_.source_url, video_info_, config_.pull_mode, true)) {
            logError("Video SETUP failed");
            disconnect();
            return false;
        }
        logVerbose("Video SETUP successful");
    }
    
    // SETUP audio track (if present)
    if (!audio_info_.control_url.empty()) {
        if (!sendSetup(config_.source_url, audio_info_, config_.pull_mode, false)) {
            logVerbose("Audio SETUP failed (continuing without audio)");
        } else {
            logVerbose("Audio SETUP successful");
        }
    }
    
    // PLAY
    if (!sendPlay(config_.source_url)) {
        logError("PLAY failed");
        disconnect();
        return false;
    }
    
    logVerbose("PLAY successful - stream pulled!");
    running_ = true;
    
    // Start receiving RTP data
    char buffer[8192];
    while (running_) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd_, &readfds);
        struct timeval tv = {1, 0};
        
        int ret = select(0, &readfds, NULL, NULL, &tv);
        if (ret > 0) {
            int n = recv(sockfd_, buffer, sizeof(buffer), 0);
            if (n > 0) {
                logVerbose("<< RX RTP " + std::to_string(n) + " bytes");
            } else if (n == 0) {
                logVerbose("Connection closed");
                break;
            }
        }
    }
    
    sendTeardown(config_.source_url);
    disconnect();
    running_ = false;
    
    return true;
}

bool Relay::pushStream() {
    logVerbose("=== RTSP Push not implemented yet ===");
    return false;
}

void Relay::stop() {
    running_ = false;
    disconnect();
}

} // namespace rtsp
