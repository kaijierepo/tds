# RTSP Relay v2 使用说明

## 概述
`rtspRelay2` 是一个简洁的 RTSP 流转发库，支持 TCP 和 UDP 两种传输模式。

## 文件位置
- 头文件: `src/video/rtspRelay2.h`
- 实现文件: `src/video/rtspRelay2.cpp`
- 测试程序: `msvc/rtspRelayTest/main2.cpp`

## 编译
```bash
cd msvc/rtspRelayTest
msbuild rtspRelayTest.vcxproj /p:Configuration=Release /p:Platform=x64 /t:Build
```

## 使用示例
```cpp
#include "rtspRelay2.h"
using namespace rtsp;

Relay relay;
RelayConfig config;
config.source_url = "rtsp://127.0.0.1:554/stream/1";
config.pull_mode = TransportMode::TCP;  // 或 UDP
relay.setConfig(config);
relay.setVerboseCallback([](const char* msg) {
    printf("%s\n", msg);
});
relay.pullStream();
```

## 命令行测试
```bash
# TCP 模式 (默认)
rtspRelayTest.exe --source rtsp://127.0.0.1:554/stream/1

# UDP 模式
rtspRelayTest.exe --source rtsp://127.0.0.1:554/stream/1 --mode udp

# 指定目标地址
rtspRelayTest.exe --source rtsp://127.0.0.1:554/stream/1 --target rtsp://127.0.0.1:554/stream/2
```

## 主要功能
1. **DESCRIBE**: 获取 SDP 信息和 Session
2. **SETUP**: 设置视频/音频轨道，支持 TCP (interleaved) 和 UDP
3. **PLAY**: 开始拉流
4. **RTP 接收**: 持续接收 RTP 数据包

## ZLM 兼容
- 自动解析 ZLM 返回的 SDP 中的 control URL (streamid=0, streamid=1)
- 自动从 DESCRIBE 响应中提取 Session
- 支持 ZLM 的相对路径格式
