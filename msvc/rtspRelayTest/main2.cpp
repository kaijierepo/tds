#include "rtspRelay2.h"
#include <iostream>
#include <thread>
#include <chrono>

void printUsage() {
    std::cout << "RTSP Relay Test v2\n";
    std::cout << "Usage: rtspRelayTest2.exe [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --source <url>   Source RTSP URL (default: rtsp://127.0.0.1:554/stream/1)\n";
    std::cout << "  --target <url>   Target RTSP URL (default: rtsp://127.0.0.1:554/stream/2)\n";
    std::cout << "  --mode <mode>    Transport mode: tcp or udp (default: tcp)\n";
    std::cout << "  --timeout <ms>   Timeout in milliseconds (default: 10000)\n";
    std::cout << "  --quiet          Suppress verbose output\n";
    std::cout << "  --help           Show this help\n";
}

int main(int argc, char* argv[]) {
    using namespace rtsp;
    
    RelayConfig config;
    config.source_url = "rtsp://127.0.0.1:554/stream/1";
    config.target_url = "rtsp://127.0.0.1:554/stream/2";
    config.pull_mode = TransportMode::TCP;
    config.push_mode = TransportMode::TCP;
    config.timeout_ms = 10000;
    config.verbose = true;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        else if (arg == "--source" && i + 1 < argc) {
            config.source_url = argv[++i];
        }
        else if (arg == "--target" && i + 1 < argc) {
            config.target_url = argv[++i];
        }
        else if (arg == "--mode" && i + 1 < argc) {
            std::string mode = argv[++i];
            if (mode == "udp" || mode == "UDP") {
                config.pull_mode = TransportMode::UDP;
                config.push_mode = TransportMode::UDP;
            }
        }
        else if (arg == "--timeout" && i + 1 < argc) {
            config.timeout_ms = std::stoi(argv[++i]);
        }
        else if (arg == "--quiet" || arg == "-q") {
            config.verbose = false;
        }
    }
    
    std::cout << "=== RTSP Relay Test v2 ===\n";
    std::cout << "Source: " << config.source_url << "\n";
    std::cout << "Target: " << config.target_url << "\n";
    std::cout << "Mode: " << (config.pull_mode == TransportMode::TCP ? "TCP" : "UDP") << "\n";
    std::cout << "============================\n\n";
    
    Relay relay;
    relay.setConfig(config);
    relay.setVerboseCallback([](const char* msg) {
        std::cout << msg << "\n";
    });
    
    bool success = relay.pullStream();
    
    if (success) {
        std::cout << "\n=== SUCCESS ===\n";
    } else {
        std::cout << "\n=== FAILED: " << relay.getLastError() << " ===\n";
    }
    
    return success ? 0 : 1;
}
