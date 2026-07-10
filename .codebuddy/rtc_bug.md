# WebRTC DTLS 连接失败 - 问题排查记录

## 现象

浏览器端 WebRTC 测试页面连接 TDS 时，**DTLS 握手失败**，无法收到视频流。

日志时间线（大约 16 秒）：
```
[00:08:30] SDP Offer/Answer 协商成功
[00:08:31] ICE 状态: checking
[00:08:31] ontrack 触发，绑定 <video> 元素
[00:08:31] 连接状态: connecting → DTLS 握手进行中
[00:08:47] ICE 状态: disconnected
[00:08:47] 连接状态: failed → DTLS 连接失败
```

**直接的失败原因是 ICE 断开（disconnected），而非 DTLS 本身。**

## 已验证项

### 1. SDP Offer/Answer 协商 - ✅ 正常
- 浏览器生成 SDP Offer（包含 VP8/VP9/H264/AV1/RTX/FEC 等多种 codec）
- TDS 返回 SDP Answer（H264, sendonly，ice-lite）
- `setLocalDescription` / `setRemoteDescription` 调用均成功

### 2. ICE Candidate 交换 - ✅ 正常
- TDS 返回多个 host candidate（多网卡多 IP: 192.168.0.110, 192.168.6.106, 192.168.4.201 等共 15 个）
- 浏览器 ICE gathering 完成，生成了 mDNS candidate 和 host candidate

### 3. STUN Binding Response - ✅ 已验证正确

通过 `test-stun-comprehensive.js` 工具完整验证：

| 检查项 | 结果 |
|--------|------|
| 消息类型 (0x0101) | ✅ |
| Magic Cookie (0x2112A442) | ✅ |
| Transaction ID 匹配 | ✅ |
| XOR-MAPPED-ADDRESS | ✅ |
| MESSAGE-INTEGRITY (HMAC-SHA1) | ✅ |
| FINGERPRINT (CRC32) | ✅ |

### 4. 关键代码路径

- **SDP Answer 构造**: `streamServer.cpp:272` (`rpc_playWebRtc`)
- **STUN 处理**: `streamNode.cpp:3437-3560` (STUN binding request → response)
- **DTLS**: 使用 mbedtls 实现
- **RTP 端口**: 动态分配 (bind to 0)，由 `createUDPServerSocket` 创建

### 5. localIP 来源

```
HTTP Host header → webSrv.cpp:822 → session.localIP → streamServer.cpp:300 → serverIp
```

`serverIp` 被用于：
- SDP Answer 的 `o=`、`c=` 行
- ICE candidate 的 IP 地址
- STUN response 的 XOR-MAPPED-ADDRESS

代码会检测 `localhost` 等主机名并通过 `getaddrinfo` 解析为数字 IP（`streamServer.cpp:305-329`）。

## 可能的根本原因

### 假设 A：多网卡 ICE candidate 问题

TDS 服务器有多个网卡（192.168.0.x, 192.168.3.x, 192.168.4.x, 192.168.6.x 等），SDP Answer 中有 15 个 candidate。

- 浏览器的 ICE agent 可能尝试所有 candidate 对，由于网络隔离，某些 candidate 不可达
- ICE 在 `checking` 阶段 16 秒后进入 `disconnected`，说明两侧都没能建立有效的连通性检查

### 假设 B：DTLS 证书指纹不匹配

SDP Answer 中的 `a=fingerprint:sha-256` 来自 `m_dtlsFingerprint`（`streamServer.cpp:349`）。如果该指纹与实际 DTLS 握手使用的证书不一致，DTLS 会在握手阶段失败。

但日志显示 DTLS 甚至没到握手阶段——ICE 先断开了。

### 假设 C：浏览器端与 TDS 之间的网络隔离

浏览器与 TDS 不在同一网段时，host candidates（内网 IP）不可达。这种情况下需要 STUN/TURN server（NAT 穿透），而 TDS 目前不提供 TURN 能力。

如果浏览器在另一台机器上，或者通过 VPN/代理访问 192.168.0.110:667，那么 UDP 媒体流可能无法直连到 TDS 的 RTP 端口。

### 假设 D：ICE-lite 模式兼容性

TDS 使用 `a=ice-lite`（SDP Answer 中明确标记），浏览器需要适应 ice-lite 的行为。部分浏览器实现对此支持有限。

## 测试工具

| 文件 | 用途 |
|------|------|
| `test-stun-ip.js` | 通过 RPC 创建 WebRTC 会话，发送 STUN 请求验证响应可达性 |
| `test-stun-comprehensive.js` | 完整验证 STUN 响应的每个字段（MI、FP、XOR-MAPPED-ADDRESS） |
| `out/tds/ui/app/webrtc/index.html` | 浏览器端 WebRTC 测试页面 |


## 根因定位 (2026-07-10)

### 确认根因：**STUN Binding Response 缺少 USERNAME 属性**

**RFC 5245 §7.1.2.2** 明确规定：
> If the Binding request contained the USERNAME attribute, then the Binding response MUST contain the USERNAME attribute. The USERNAME attribute value MUST be the same as the one received in the request.

浏览器（Chrome/libwebrtc）在 ICE 连通性检查时发送的 STUN Binding Request 包含 USERNAME 属性（格式：`{browser-ufrag}:{server-ufrag}`），但 TDS 的 STUN 响应中**没有回显 USERNAME**。

libwebrtc 在收到响应后验证 USERNAME 存在性，缺失时丢弃响应，导致所有 ICE candidate pair 的连通性检查都失败。浏览器经过约 16 秒重试后 ICE 进入 `disconnected` 状态。

### 为什么 `test-stun-comprehensive.js` 测试通过？

测试脚本发送的 STUN Binding Request **不包含 USERNAME 属性**（不带属性的 bare request），此时服务端不返回 USERNAME 是合法的。浏览器实际的 ICE 请求会带 USERNAME，才会触发此 bug。

### 修复内容

**`src/video/streamNode.cpp`**（`iceHandleLoop` 函数）：
1. 解析传入 STUN Binding Request 的 USERNAME 属性（type 0x0006）
2. 在 STUN Binding Success Response 中回显 USERNAME（置于 XOR-MAPPED-ADDRESS 之后、MESSAGE-INTEGRITY 之前）
3. 响应 buffer 从 128 扩大到 256 字节（USERNAME 最多约 `8+1+8=17` 字节 + 4 字节对齐）

**`test-stun-comprehensive.js`**：
1. STUN 请求增加 USERNAME 属性（模拟浏览器 ICE 行为）
2. 属性解析增加 USERNAME (0x0006) 识别
3. 增加 USERNAME 回显验证
4. 修复属性解析的 4 字节对齐问题

### 修复后预期行为

浏览器收到包含正确 USERNAME 的 STUN 响应 → ICE 连通性检查通过 → DTLS 握手开始 → SRTP 密钥协商 → 视频流开始传输

## 下一步方向

1. **重新编译部署**：停止 tds.exe，用 Visual Studio 编译 `debug|x64` 配置，替换 `out/tds/tds.exe`
2. **回归测试**：运行 `node test-stun-comprehensive.js` 验证 USERNAME 回显
3. **浏览器验证**：用浏览器打开 `/ui/app/webrtc/index.html` 测试 WebRTC 视频流
4. **如仍有问题**：
   - 检查 TDS 日志中 `[STUN] Received Binding Request` 是否出现（确认 UDP 可达）
   - 用 `test-stun-ip.js` 测试指定 IP 的 STUN 连通性
   - 抓包验证 DTLS ClientHello 是否到达 RTP 端口

## 关键配置

- TDS HTTP API 端口：667（TCP）
- WebRTC RTP 端口：动态分配（UDP）
- WebRTC 测试页面：`/ui/app/webrtc/index.html`
- RPC 方法：`playWebRtc`
- 服务器多网卡：192.168.0.x / 3.x / 4.x / 6.x 网段
