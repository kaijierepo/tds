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

#include "streamCommon.h"
#include "streamSession.h"

// RTP包结�?
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



struct STREAM_OPEN_PARAM {
    std::string tag;
    std::string originPullUrl;
    std::string relayPushUrl;
    std::string pushToTag;
    std::string streamUrl;
    std::string pushToIP;
    std::string srcStreamFetch; // "always" 或 "ondemand"
};

class StreamNode {
public:
    // 录像控制
    struct RecordControl {
        bool recording = false;          // 是否启用录像
        std::string path;             // 录像文件路径
        uint64_t max_file_size = 0;   // 单个录像文件最大大小(字节)，0表示不限制
        uint64_t max_duration = 0;    // 单个录像文件最长时长(秒)，0表示不限制
        int max_files = 0;            // 最多保留的录像文件数量，0表示不限制
		int preSeconds = 0;          // 录像预录时间(秒)，即在事件发生前也保存的录像时长
        bool firstWrite = true;
	    bool preRecordingDone = false;  // 预录数据已一次性写出，避免重复写入
        bool isIdrNal = false;
        bool isLastIdrNal = false;
        int nalCount = 0;

        std::vector<char> fu_a_buffer_; // FU-A分片缓存
        std::chrono::steady_clock::time_point startTime;  // 录像开始时间，用于计算 duration
	};

    // 配置结构
    struct Config {
        bool verbose = false;        // 详细日志
        std::string tag;
        std::string streamUrl;
        std::string srcStreamFetch = "always"; // 拉流模式: "always" 或 "ondemand"
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



    // 回调函数
    using FrameCallback = std::function<void(const uint8_t* data, size_t size, uint32_t timestamp)>;
    using ErrorCallback = std::function<void(const std::string& error, int code)>;

    // 构造函数/析构函数
    StreamNode();
    ~StreamNode();

    // 禁止拷贝
    StreamNode(const StreamNode&) = delete;
    StreamNode& operator=(const StreamNode&) = delete;

    // 公共接口
    bool run(const Config& config);
    void stop();


    // 状态查询
    SESSION_STATE getState();
    bool isRunning();
    Statistics getStatistics();

    // 设置回调
    void setFrameCallback(FrameCallback cb);
    void setErrorCallback(ErrorCallback cb);

    bool extractRtspAuthInfo(STREAM_SESSION& session);


public:

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
        std::vector<uint8_t> data;
        bool parse(const uint8_t* data, size_t size);
        std::vector<uint8_t> serialize() const;

        bool isIdrNalu;
        bool isLastIdrNalu;
    };

    // URL解析
    struct URLComponents {
        std::string protocol;
        std::string host;
        int port = 554;
        std::string path;

        static bool parse(const std::string& url, URLComponents& components);
    };

public:
    // 配置和状态
    Config config_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> stopping_{ false };
    std::atomic<bool> isPulling_{ false };
    std::atomic<bool> isPushing_{ false };

    RecordControl rec_ctrl_;

    // 流信息
    STREAM_SESSION session_origin_;
    STREAM_SESSION session_relay_push_;

    // 流属性（与 session 无关，描述流经本节点的媒体流参数）
    uint32_t clock_rate_ = 90000;
	std::vector<std::shared_ptr<STREAM_SESSION>> session_list_client_pull_;
	std::mutex session_list_client_pull_mutex_;

    //一个IDR 会被分成多个NALU， 一个NALU 会被分成多个RTP packet发送
    //当录制.h264或者发送webrtc的rtp是，在连续的多个IDR nalu之前，需要加入sps/pps
    //last_nalu_was_idr_ 变量用于确认上一个不是idr,下一个是idr的nalu时，发送一次sps/pps
    bool last_nalu_was_idr_ = false; //判断上一个是否是IDR的RTP包，一般一个IDR帧会分成多个
    std::vector<char> last_sps_;  // 最新SPS缓存，IDR前写入（在onRecvOriginRtpPkt中更新）
    std::vector<char> last_pps_;  // 最新PPS缓存，IDR前写入（在onRecvOriginRtpPkt中更新）

    // 最近一个关键帧的 RTP 原始数据缓存（新会话首次发送时使用，加速出图）
    std::vector<std::vector<uint8_t>> keyframe_cache_;
    // 保护 keyframe_cache_ 与 last_nalu_was_idr_：sendRTPPacketToClients 可能被多个线程并发调用
    // (拉流线程 / 每个 RTSP 推流客户端独立线程 / 文件源线程)，无锁并发读写 std::vector 会造成堆破坏→double free
    std::mutex keyframe_cache_mutex_;
    bool keyframe_caching_ = false;  // 当前是否正在缓存关键帧（遇到IDR开始，marker=1结束）

    // 线程
    std::thread recv_thread_origin_rtp_;
    std::thread ctrl_thread_rtsp_client_;

    // 同步
    mutable std::mutex state_mutex_;
    mutable std::mutex stats_mutex_;
    mutable std::mutex queue_mutex_;
    mutable std::recursive_mutex rec_mutex_;   // 保护 rec_ctrl_ 控制字段

    // 数据队列
    int getBufferedSeconds();
    void addToRtpBuffer(std::shared_ptr<RTPPacket> pPkt);
    int rtp_buffer_max_seconds_ = 60; 
    std::vector<std::shared_ptr<RTPPacket>> rtp_buffer_;
    // 录像 I/O 线程 — 生产者-消费者队列，将磁盘写入与实时收包线程解耦
    std::queue<std::shared_ptr<RTPPacket>> record_queue_;
    std::mutex record_queue_mutex_;
    std::condition_variable record_queue_cv_;
    std::thread record_io_thread_;
    std::atomic<bool> record_io_running_{false};

    size_t max_queue_size_ = 50000;

    // 统计
    Statistics stats_;

    // 回调函数
    ErrorCallback error_callback_;

    // 时间戳
    uint32_t base_timestamp_ = 0;
    uint16_t base_sequence_ = 0;

    std::chrono::system_clock::time_point open_time_;

    // 最近一次转发请求的时间（用于按需拉流空闲检测）
    std::chrono::steady_clock::time_point last_forward_request_time_;

    // 工作线程
    void threadCtrl_rtspClient();
    void threadRecv_originPull();
    void threadRecv_rtspPublish(std::shared_ptr<STREAM_SESSION> session);
    void onRecvOriginRtpPkt(std::shared_ptr<RTPPacket> pkt, STREAM_SESSION& session);
    bool checkIsIdrNalu(const RTPPacket& packet);
    void sendSingleNalRtp(const std::vector<uint8_t>& nal, uint32_t ts, STREAM_SESSION& session);

    void sendRTPPacketToClients(const RTPPacket& packet);
    void forwardRTPPacket(const RTPPacket& packet);
    void recordRTPPacket(std::shared_ptr<RTPPacket> pPkt);
    void flushRecordBuffer();
    bool hasWorkToDo();
    void threadRec_h264File();
    void writeNALtoFile(uint8_t nal_type, char* nal, size_t size, std::ofstream& ofs);
    void writeRTPPacketToFile(std::shared_ptr<RTPPacket> pPkt, std::ofstream& ofs);

    // ICE-Lite (WebRTC) — 每客户端一线程处理 STUN 请求
    void startRtcSessionHandleThread(std::shared_ptr<STREAM_SESSION> session);
    void stopAll_threadCtrl_webrtcServer();

private:
    // ICE-Lite 工作循环（由每个 WebRTC session 的 ice_thread_ 执行）
    void threadCtrl_webrtcServer(std::shared_ptr<STREAM_SESSION> session);
};