// main.cpp
#include "rtspRelay.h"
#include <iostream>
#include <csignal>
#include <atomic>

std::atomic<bool> running{true};

void signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
    running = false;
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    RTSPRelay relay;
    
    // 配置回调
    relay.setStatusCallback([](RTSPRelay::State state, const std::string& msg) {
        std::cout << "Status: " << static_cast<int>(state) << " - " << msg << std::endl;
    });
    
    relay.setErrorCallback([](const std::string& error, int code) {
        std::cerr << "Error (" << code << "): " << error << std::endl;
    });
    
    relay.setFrameCallback([](const uint8_t* data, size_t size, uint32_t timestamp) {
        // 可以在这里处理帧数据
    });
    
    // 配置中继
    RTSPRelay::Config config;
    config.source_url = "rtsp://127.0.0.1:554/stream/1";  
    config.target_url = "rtsp://127.0.0.1:554/stream/2";
    config.retry_interval = 3000;    // 3秒重试
    config.max_retries = 0;          // 0表示无限重试
    config.rtp_timeout = 10000;      // 10秒RTP超时
    config.verbose = true;           // 输出详细日志
    
    // UDP传输配置 - 拉流和推流可独立选择
    // TCP 模式测试（PowerShell 验证可行）
    config.pull_mode = RTSPRelay::TransportMode::TCP;   // 拉流: TCP模式
    config.push_mode = RTSPRelay::TransportMode::TCP;    // 推流: TCP模式
    // config.pull_mode = RTSPRelay::TransportMode::UDP;  // 拉流: UDP模式
    // config.push_mode = RTSPRelay::TransportMode::UDP; // 推流: UDP模式
    
    // UDP参数配置
    config.udp_ttl = 64;             // TTL生存时间
    config.udp_tos = 0xC0;           // ToS: AF41 低延迟
    config.udp_recv_buffer_size = 2 * 1024 * 1024;  // 2MB接收缓冲区
    config.udp_send_buffer_size = 2 * 1024 * 1024;  // 2MB发送缓冲区
    
    // 启动中继
    if (!relay.start(config)) {
        std::cerr << "Failed to start RTSP relay" << std::endl;
        return 1;
    }
    
    std::cout << "RTSP relay started. Press Ctrl+C to stop." << std::endl;
    
    // 主循环
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 定期打印统计信息
        auto stats = relay.getStatistics();
        std::cout << "\rStats: FPS=" << stats.fps
                  << " Bitrate=" << stats.bitrate << "kbps"
                  << " Frames=" << stats.frames_received
                  << " Errors=" << stats.errors
                  << " Reconnects=" << stats.reconnect_count
                  << std::flush;
    }
    
    std::cout << "\nStopping RTSP relay..." << std::endl;
    relay.stop();
    
    std::cout << "RTSP relay stopped successfully" << std::endl;
    return 0;
}