#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

#pragma comment(lib, "ws2_32.lib")

// RTSP Relay namespace
namespace rtsp {

enum class TransportMode {
    UDP,
    TCP
};

struct StreamInfo {
    std::string control_url;
    std::string sdp_lines;
    int port = 0;
};

struct RelayConfig {
    std::string source_url;
    std::string target_url;
    TransportMode pull_mode = TransportMode::TCP;
    TransportMode push_mode = TransportMode::TCP;
    int timeout_ms = 10000;
    int buffer_size = 65536;
    bool verbose = true;
};

class Relay {
public:
    using VerboseCallback = std::function<void(const char*)>;
    
    Relay();
    ~Relay();
    
    void setVerboseCallback(VerboseCallback cb) { verbose_cb_ = std::move(cb); }
    void setConfig(const RelayConfig& config) { config_ = config; }
    RelayConfig getConfig() const { return config_; }
    
    bool pullStream();
    bool pushStream();
    void stop();
    bool isRunning() const { return running_; }
    
    const char* getLastError() const { return last_error_.c_str(); }
    
private:
    RelayConfig config_;
    bool running_ = false;
    std::string last_error_;
    VerboseCallback verbose_cb_;
    
    SOCKET sockfd_ = INVALID_SOCKET;
    int cseq_ = 0;
    std::string session_;
    StreamInfo video_info_;
    StreamInfo audio_info_;
    
    void logVerbose(const std::string& msg);
    void logError(const std::string& msg);
    
    bool connectToServer(const std::string& host, int port);
    void disconnect();
    
    bool sendRequest(const std::string& request);
    int receiveResponse(std::string& response, int timeout_ms);
    bool sendOptions(const std::string& url);
    bool sendDescribe(const std::string& url, std::string& sdp);
    bool sendSetup(const std::string& url, const StreamInfo& info, TransportMode mode, bool is_video);
    bool sendPlay(const std::string& url);
    bool sendTeardown(const std::string& url);
    
    bool parseSDP(const std::string& sdp);
    std::string extractSession(const std::string& response);
    
    bool parseUrl(const std::string& url, std::string& host, int& port, std::string& path);
    std::string buildSetupUrl(const std::string& base_url, const std::string& control_url);
};

} // namespace rtsp
