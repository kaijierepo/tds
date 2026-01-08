#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <map>
#include <chrono>
#include <cstdint>

using namespace std;

class RTSPRelay {
public:
    enum State {
        IDLE = 0,
        CONNECTING,
        CONNECTED,
        PLAYING,
        RECORDING,
        S_ERROR,
        RECONNECTING
    };
    
    // 配置结构
    struct Config {
        std::string source_url;      // 源RTSP地址
        std::string target_url;      // 目标RTSP地址
        int retry_interval = 3000;   // 重试间隔(ms)
        int max_retries = 10;        // 最大重试次数
        int rtp_timeout = 5000;      // RTP超时(ms)
        int buffer_size = 65536;     // 缓冲区大小
        bool verbose = false;        // 详细日志
    };
    
    // 统计结构
    struct Statistics {
        uint64_t frames_received = 0;
        uint64_t frames_forwarded = 0;
        uint64_t bytes_received = 0;
        uint64_t bytes_forwarded = 0;
        uint64_t reconnect_count = 0;
        uint64_t errors = 0;
        
        // 时间统计
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point last_frame_time;
        double fps = 0.0;
        double bitrate = 0.0;  // kbps
        double aaa;
    };
    
    // 回调函数
    using StatusCallback = std::function<void(State state, const std::string& msg)>;
    using FrameCallback = std::function<void(const uint8_t* data, size_t size, uint32_t timestamp)>;
    using ErrorCallback = std::function<void(const std::string& error, int code)>;
    
    // 构造函数/析构函数
    RTSPRelay();
    ~RTSPRelay();
    
    // 禁止拷贝
    RTSPRelay(const RTSPRelay&) = delete;
    RTSPRelay& operator=(const RTSPRelay&) = delete;
    
    // 公共接口
    bool start(const Config& config);
    void stop();
    void restart();

 
    
    // 状态查询
    State getState();
    bool isRunning();
    Statistics getStatistics();
    
    // 设置回调
    void setStatusCallback(StatusCallback cb);
    void setFrameCallback(FrameCallback cb);
    void setErrorCallback(ErrorCallback cb);
    
private:
    // RTSP消息结构
    struct RTSPMessage {
        std::string method;
        std::string uri;
        std::map<std::string, std::string> headers;
        std::string body;
        int cseq = 0;
        
        std::string toString() const;
        static RTSPMessage parse(const std::string& data);
    };
    
    // RTP包结构
    struct RTPPacket {
        uint8_t version = 2;
        bool padding = false;
        bool extension = false;
        uint8_t csrc_count = 0;
        bool marker = false;
        uint8_t payload_type = 96;  // H.264
        uint16_t sequence_number = 0;
        uint32_t timestamp = 0;
        uint32_t ssrc = 0;
        std::vector<uint8_t> payload;
        
        bool parse(const uint8_t* data, size_t size);
        std::vector<uint8_t> serialize() const;
    };
    
    // 媒体流信息
    struct StreamInfo {
        std::string control_url;
        std::string codec = "H264";
        int payload_type = 96;
        int clock_rate = 90000;
        std::string fmtp;
        
        // 传输信息
        std::string transport;
        std::string client_port;
        std::string server_port;
        std::string source_host;
        int source_rtp_port = 0;
        int source_rtcp_port = 0;
    };
    
    // 网络连接
    class Connection {
    public:
        Connection();
        ~Connection();
        
        bool connect(const std::string& host, int port, int timeout_ms = 5000);
        void disconnect();
        bool isConnected() const;
        
        size_t send(const void* data, size_t size, int timeout_ms = 5000);
        size_t receive(void* buffer, size_t size, int timeout_ms = 5000);
        size_t receiveHttpResp(std::string& response, int timeout_ms = 5000);
        
        int getSocket() const { return sockfd_; }
        
    private:
        int sockfd_ = -1;
        std::string host_;
        int port_ = 0;
        bool setSocketTimeout(int timeout_ms);
    };
    
    bool connectToSource();

    bool setupStreams();


    // 工作线程
    void workerThread();
    void rtpThread();
    void controlThread();
    void statsThread();
    
    // RTSP控制方法
    bool rtspDescribe(Connection& conn, const std::string& url, 
                     std::string& sdp, std::string& session);
    bool rtspSetup(Connection& conn, const std::string& url, 
                  const std::string& session, StreamInfo& stream);
    bool rtspPlay(Connection& conn, const std::string& url, 
                 const std::string& session);
    bool rtspTeardown(Connection& conn, const std::string& url, 
                     const std::string& session);
    bool rtspAnnounce(Connection& conn, const std::string& url, 
                     const std::string& sdp, std::string& session);
    bool rtspRecord(Connection& conn, const std::string& url, 
                   const std::string& session);

    void teardown();
    
    // SDP处理
    bool parseSDP(const std::string& sdp, StreamInfo& video_info, StreamInfo& audio_info);
    std::string generateSDP(const StreamInfo& video_info, const StreamInfo& audio_info);
    void forwardRTPPacket(const RTPPacket& packet);
    std::string extractSessionID(const std::string& response);
    std::string extractTransport(const std::string& response);
    
    // URL解析
    struct URLComponents {
        std::string protocol;
        std::string host;
        int port = 554;
        std::string path;
        
        static bool parse(const std::string& url, URLComponents& components);
    };
    
    // 工具函数
    std::string generateCSeq();
    std::string getCurrentTimeRFC1123();
    std::string base64Encode(const std::string& input);
    std::string md5(const std::string& input);
    
    // 错误处理
    void setError(const std::string& error, int code = 0);
    void setState(State new_state, const std::string& msg = "");
    bool shouldReconnect() const;
    void doReconnect();
    
    // 日志
    void logInfo(const std::string& msg) const;
    void logError(const std::string& msg) const;
    void logDebug(const std::string& msg) const;
    void logVerbose(const std::string& msg) const;
    
private:
    // 配置和状态
    Config config_;
    State state_ = State::IDLE;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    
    // 连接和会话
    std::unique_ptr<Connection> source_conn_;
    std::unique_ptr<Connection> target_conn_;
    std::string source_session_;
    std::string target_session_;
    
    // 流信息
    StreamInfo source_audio_info_;
    StreamInfo source_video_info_;
    StreamInfo target_video_info_;
    
    // 网络套接字
    int rtp_socket_ = -1;
    int rtcp_socket_ = -1;
    int target_rtp_port_ = 0;
    
    // 线程
    std::thread worker_thread_;
    std::thread rtp_thread_;
    std::thread control_thread_;
    std::thread stats_thread_;
    
    // 同步
    mutable std::mutex state_mutex_;
    mutable std::mutex stats_mutex_;
    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;
    
    // 数据队列
    std::queue<RTPPacket> packet_queue_;
    size_t max_queue_size_ = 1000;
    
    // 统计
    Statistics stats_;
    int retry_count_ = 0;
    
    // 回调函数
    StatusCallback status_callback_;
    FrameCallback frame_callback_;
    ErrorCallback error_callback_;
    
    // 时间戳
    std::chrono::steady_clock::time_point last_rtp_time_;
    uint32_t base_timestamp_ = 0;
    uint16_t base_sequence_ = 0;
    
    // 重连控制
    std::chrono::steady_clock::time_point last_reconnect_time_;
};


/* 拉流
OPTIONS rtsp://127.0.0.1:8554/1 RTSP/1.0
CSeq: 2
User-Agent: LibVLC/3.0.21 (LIVE555 Streaming Media v2016.11.28)

RTSP/1.0 200 OK
Server: VLC/3.0.21
Content-Length: 0
Cseq: 2
Public: DESCRIBE,SETUP,TEARDOWN,PLAY,PAUSE,GET_PARAMETER


DESCRIBE rtsp://127.0.0.1:8554/1 RTSP/1.0
CSeq: 3
User-Agent: LibVLC/3.0.21 (LIVE555 Streaming Media v2016.11.28)
Accept: application/sdp

RTSP/1.0 200 OK
Server: VLC/3.0.21
Date: Wed, 07 Jan 2026 12:51:02 GMT
Content-Type: application/sdp
Content-Base: rtsp://127.0.0.1:8554/1
Content-Length: 448
Cache-Control: no-cache
Cseq: 3

v=0
o=- 17080136018319900443 17080136018319900443 IN IP4 DESKTOP-IKTCMTL
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
a=control:rtsp://127.0.0.1:8554/1/trackID=385


SETUP rtsp://127.0.0.1:8554/1/trackID=385 RTSP/1.0
CSeq: 4
User-Agent: LibVLC/3.0.21 (LIVE555 Streaming Media v2016.11.28)
Transport: RTP/AVP;unicast;client_port=50912-50913    

//RTP/AVP（RTP Audio Video Profile）默认使用UDP，可以显式指定 RTP/AVP/TCP 使用TCP传输
//unicast 指定传输方式为单播
//client_port=50912-50913 指定RTP端口范围，50912用于RTP数据包，50913用于RTCP控制包

RTSP/1.0 200 OK
Server: VLC/3.0.21
Date: Wed, 07 Jan 2026 12:51:02 GMT
Transport: RTP/AVP/UDP;unicast;client_port=50912-50913;server_port=50914-50915;ssrc=2A6B2790;mode=play
Session: fffd826e50a43195;timeout=60
Content-Length: 0
Cache-Control: no-cache
Cseq: 4


PLAY rtsp://127.0.0.1:8554/1 RTSP/1.0
CSeq: 5
User-Agent: LibVLC/3.0.21 (LIVE555 Streaming Media v2016.11.28)
Session: fffd826e50a43195
Range: npt=0.000-

RTSP/1.0 200 OK
Server: VLC/3.0.21
Date: Wed, 07 Jan 2026 12:51:02 GMT
RTP-Info: url=rtsp://127.0.0.1:8554/1/trackID=385;seq=1605;rtptime=2053081234
Range: npt=79706.946000-
Session: fffd826e50a43195;timeout=60
Content-Length: 0
Cache-Control: no-cache
Cseq: 5

TEARDOWN rtsp://127.0.0.1:8554/1 RTSP/1.0
CSeq: 6
User-Agent: LibVLC/3.0.21 (LIVE555 Streaming Media v2016.11.28)
Session: fffd826e50a43195

RTSP/1.0 200 OK
Server: VLC/3.0.21
Date: Wed, 07 Jan 2026 12:51:08 GMT
Session: fffd826e50a43195;timeout=60
Content-Length: 0
Cache-Control: no-cache
Cseq: 6

*/