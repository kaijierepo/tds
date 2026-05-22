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

// RTP包结构
// NAL header (1 byte) format: F(1) | NRI(2) | Type(5)
// - F: forbidden_zero_bit
// - NRI (bits 6-5): nal_ref_idc (importance / priority)
// - Type (bits 4-0): nal_unit_type
// Helper macros to extract fields from NAL header byte
#define NAL_HDR_F(_h) (((_h) >> 7) & 0x01)
#define NAL_HDR_NRI(_h) (((_h) & 0x60) >> 5)
#define NAL_HDR_TYPE(_h) ((_h) & 0x1F)

    // Common NAL unit type constants
#define NAL_TYPE_NON_IDR          1
#define NAL_TYPE_IDR              5
#define NAL_TYPE_SEI              6
#define NAL_TYPE_SPS              7
#define NAL_TYPE_PPS              8
#define NAL_TYPE_AUD              9
#define NAL_TYPE_END_OF_SEQUENCE  10
#define NAL_TYPE_END_OF_STREAM    11
#define NAL_TYPE_FILLER_DATA      12
#define NAL_TYPE_PREFIX_NALU      14
#define NAL_TYPE_SUBSET_SPS       15
#define NAL_TYPE_SLICE_EXTENSION  19
#define NAL_TYPE_SLICE_EXT_3D     20
#define NAL_TYPE_SLICE_EXT_DEPTH  21
#define NAL_TYPE_STAP_A           24
#define NAL_TYPE_FU_A             28

class RtspClient {
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

    // 传输模式枚举
    enum class TransportMode {
        NONE,   // 
        UDP,    // UDP传输
        TCP     // RTP over RTSP (TCP)
    };

    // 录像控制
    struct RecordControl {
        bool recording = false;          // 是否启用录像
        std::string path;             // 录像文件路径
        uint64_t max_file_size = 0;   // 单个录像文件最大大小(字节)，0表示不限制
        uint64_t max_duration = 0;    // 单个录像文件最长时长(秒)，0表示不限制
        int max_files = 0;            // 最多保留的录像文件数量，0表示不限制
		int preSeconds = 0;          // 录像预录时间(秒)，即在事件发生前也保存的录像时长
        bool firstWrite = true;

        std::vector<char> fu_a_buffer_; // FU-A分片缓存
        // 记录上一个写入的是否为 IDR，用于判断连续的 IDR
        bool last_was_idr_ = false;
	};

    // 配置结构
    struct Config {
        std::string source_url;      // 源RTSP地址
        std::string target_url;       // 目标RTSP地址
        std::string source_username; // 源用户名（可选）
        std::string source_password;  // 源密码（可选）
        std::string target_username;  // 目标用户名（可选）
        std::string target_password;  // 目标密码（可选）
        int retry_interval = 3000;   // 重试间隔(ms)
        int max_retries = 10;        // 最大重试次数
        int rtp_timeout = 5000;      // RTP超时(ms)
        int buffer_size = 65536;     // 缓冲区大小
        bool verbose = false;        // 详细日志
        std::string tag;
        
        // 拉流（从源获取）传输模式
        TransportMode pull_mode = TransportMode::UDP;
        // 推流（发送到目标）传输模式
        TransportMode push_mode = TransportMode::UDP;
        
        // UDP特定配置
        int udp_recv_buffer_size = 0;    // UDP接收缓冲区大小(0=系统默认)
        int udp_send_buffer_size = 0;    // UDP发送缓冲区大小(0=系统默认)
        int udp_ttl = 64;                // TTL生存时间
        int udp_tos = 0xC0;              // Type of Service (Default: AF41 低延迟)
        bool udp_multicast_loop = false; // 组播回环
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
    };

    // 认证信息结构
    struct AuthInfo {
        std::string username;
        std::string password;
        std::string realm;
        std::string nonce;
        std::string algorithm;
        bool use_digest = false;
        std::string authorization_header;  // 缓存认证头

        void clear() {
            username.clear();
            password.clear();
            realm.clear();
            nonce.clear();
            algorithm.clear();
            authorization_header.clear();
            use_digest = false;
        }

        bool hasCredentials() const {
            return !username.empty() && !password.empty();
        }
    };

    // 回调函数
    using StatusCallback = std::function<void(State state, const std::string& msg)>;
    using FrameCallback = std::function<void(const uint8_t* data, size_t size, uint32_t timestamp)>;
    using ErrorCallback = std::function<void(const std::string& error, int code)>;

    // 构造函数/析构函数
    RtspClient();
    ~RtspClient();

    // 禁止拷贝
    RtspClient(const RtspClient&) = delete;
    RtspClient& operator=(const RtspClient&) = delete;

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

    bool extractRtspAuthInfo(RtspClient::Config& config);

public:
#ifdef _WIN32
    using SocketHandle = SOCKET;
    static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
    using SocketHandle = int;
    static constexpr SocketHandle kInvalidSocket = -1;
#endif

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

    enum RTSP_SESSION_TYPE {
        CLINET_PULL,
        CLINET_PUSH,
        SERVER_SEND
    };

    // 媒体流信息
    struct RTSP_SESSION {
        std::string control_url;
        std::string codec = "H264";
        int payload_type = 96;
        int clock_rate = 90000;
        std::string fmtp;
        // 如果 SDP 中包含 sprop-parameter-sets，会把解码后的 SPS/PPS 保存到这里
        std::vector<uint8_t> sps;
        std::vector<uint8_t> pps;

        RTSP_SESSION_TYPE session_type_;

        // 传输信息
        TransportMode transport_mode = TransportMode::UDP;
        std::string transport;
        std::string remote_host;
        std::string client_port;
        std::string server_port;
        int client_rtp_port = 0;
        int client_rtcp_port = 0;
		int server_rtp_port = 0;
		int server_rtcp_port = 0;
		SocketHandle rtp_socket = kInvalidSocket;
		SocketHandle rtcp_socket = kInvalidSocket;
    };

    // URL解析
    struct URLComponents {
        std::string protocol;
        std::string host;
        int port = 554;
        std::string path;

        static bool parse(const std::string& url, URLComponents& components);
    };

    // 网络连接
    class Connection {
    public:
        Connection();
        ~Connection();

        bool connect(const std::string& host, int port, int timeout_ms = 5000);
        void disconnect();
        bool isConnected() const;
        int lastError() const { return last_error_; }

        int send(const void* data, size_t size, int timeout_ms = 5000);
        int receive(void* buffer, size_t size, int timeout_ms = 5000);
        int receiveHttpResp(std::string& response, int timeout_ms = 5000);

        SocketHandle getSocket() const { return sockfd_; }

    private:
        SocketHandle sockfd_ = kInvalidSocket;
        std::string host_;
        int port_ = 0;
        int last_error_ = 0;
        bool setSocketTimeout(int timeout_ms);
    };

public:
    // 配置和状态
    Config config_;
    State state_ = State::IDLE;
    std::atomic<bool> running_{ false };
    std::atomic<bool> stopping_{ false };
    bool isPulling_{ false };
	bool isPushing_{ false };

    RecordControl rec_ctrl_;

    // 认证信息
    AuthInfo source_auth_;
    AuthInfo target_auth_;

    // 连接和会话
    std::unique_ptr<Connection> source_conn_;
    std::unique_ptr<Connection> target_conn_;
    std::string source_session_;
    std::string target_session_;

    // 流信息
    RTSP_SESSION pull_session_;
    RTSP_SESSION pull_audio_session_;
    RTSP_SESSION push_session_;

    // 播放客户段
	std::vector<RTSP_SESSION> client_sessions_;
	std::mutex client_sessions_mutex_;


    std::string target_rtp_host_;   // 目标RTP主机地址

    // 线程
    std::thread rtp_handle_thread_;
    std::thread control_thread_;

    // 同步
    mutable std::mutex state_mutex_;
    mutable std::mutex stats_mutex_;
    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;

    // 数据队列
    int getBufferedSeconds();
    void addToRtpBuffer(std::shared_ptr<RTPPacket> pPkt);
    int rtp_buffer_max_seconds_ = 60; 
    std::vector<std::shared_ptr<RTPPacket>> rtp_buffer_;
    std::vector<std::shared_ptr<RTPPacket>> record_batch_buffer_;
    size_t max_queue_size_ = 50000;

    // 统计
    Statistics stats_;
    int retry_count_ = 0;

    // 回调函数
    StatusCallback status_callback_;
    ErrorCallback error_callback_;

    // 时间戳
    uint32_t base_timestamp_ = 0;
    uint16_t base_sequence_ = 0;

    // 重连控制
    std::chrono::steady_clock::time_point last_reconnect_time_;

    // 工作线程
    void controlThread();
    void rtpHandleThread();
    bool doStreamPull();
    bool doStreamPush();
    void doRtpRecv();


    // RTSP控制方法
    bool rtspDescribe(Connection& conn, const std::string& url,
        std::string& sdp, std::string& session);
    bool rtspSetup(Connection& conn, const std::string& url,
        std::string& session, RTSP_SESSION& stream, bool record_mode = false);
    bool rtspPlay(Connection& conn, const std::string& url,
        const std::string& session);
    bool rtspTeardown(Connection& conn, const std::string& url,
        const std::string& session);
    bool rtspAnnounce(Connection& conn, const std::string& url,
        const std::string& sdp, std::string& session);
    bool rtspRecord(Connection& conn, const std::string& url,
        const std::string& session);
    bool rtspGetParameter(Connection& conn, const std::string& url,
        const std::string& session);
    void teardown();



    // SDP处理
    bool parseSDP(const std::string& sdp, RTSP_SESSION& video_info, RTSP_SESSION& audio_info);
    std::string generateSDP(const RTSP_SESSION& video_info, const RTSP_SESSION& audio_info);
    void sendRTPPacketToClients(const RTPPacket& packet);
    void forwardRTPPacket(const RTPPacket& packet);
    void recordRTPPacket(std::shared_ptr<RTPPacket> pPkt);
    void writeNALtoFile(uint8_t nal_type, char* nal, size_t size, std::ofstream& ofs);
    std::string extractSessionID(const std::string& response);
    std::string extractTransport(const std::string& response);

    // 认证相关
    std::string calculateDigest(const std::string& method, const std::string& uri,
        const AuthInfo& auth);
    std::string calculateBasicAuth(const AuthInfo& auth);
    bool parseWWWAuthenticate(const std::string& response, AuthInfo& auth);
    void updateAuthHeader(AuthInfo& auth, const std::string& method, const std::string& uri);

    // 工具函数
    std::string generateCSeq();
    void setError(const std::string& error, int code = 0);
    void setState(State new_state, const std::string& msg = "");
    bool shouldReconnect() const;
    void doReconnect();

    // UDP传输相关
    bool createUDPPullSocket();   // 创建UDP拉流socket
    bool createUDPPushSocket();    // 创建UDP推流socket
	bool createUDPServerSocket(RTSP_SESSION& streamInfo);  // 创建UDP服务器socket,客户端拉流时
    void closeUDPSockets();
    bool configureUDPSocket(SocketHandle sock, bool is_multicast);
    bool sendUDPDataToSession(const uint8_t* data, size_t size,RTSP_SESSION& rtspSession);
    bool sendUDPData(const uint8_t* data, size_t size);
    int receiveUDPData(uint8_t* buffer, size_t size, std::string& src_ip, int& src_port);

    // 日志
    void logInfo(const std::string& msg) const;
    void logError(const std::string& msg) const;
    void logDebug(const std::string& msg) const;
    void logVerbose(const std::string& msg) const;

private:
    // MD5相关函数（内部实现）
    struct MD5Context {
        uint32_t state[4];    // 状态 (ABCD)
        uint32_t count[2];    // 位数，模2^64 (低位在前)
        uint8_t buffer[64];   // 输入缓冲区
    };

    static void md5Init(MD5Context* context);
    static void md5Update(MD5Context* context, const uint8_t* data, size_t length);
    static void md5Final(MD5Context* context, uint8_t digest[16]);
    static void md5Transform(uint32_t state[4], const uint8_t block[64]);
    static std::string md5Hex(const std::string& input);
    static std::string base64Encode(const std::string& input);
};