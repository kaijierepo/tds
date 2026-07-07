# TDS WebRTC 流媒体服务设计

> **应用场景：** IoT 边缘网关，浏览器通过 `<video>` 标签实时播放 RTSP 摄像头视频流
> **技术栈：** mbedtls (DTLS-SRTP) + ICE-Lite + SRTP (AES-CTR/HMAC-SHA1)
> **传输要求：** 端到端延迟 < 500ms，UDP 传输

---

## 目录

1. [总体架构设计](#1-总体架构设计)
2. [模块详细设计](#2-模块详细设计)
   - [2.1 RTSP → RTP 接收层（已有）](#21-rtsp--rtp-接收层已有)
   - [2.2 StreamServer 信令服务](#22-streamserver-信令服务)
   - [2.3 ICE-Lite 实现](#23-ice-lite-实现)
   - [2.4 DTLS 握手（DtlsTransport）](#24-dtls-握手dtlstransport)
   - [2.5 SRTP 加密/解密（SrptProtect）](#25-srtp-加密解密srptprotect)
   - [2.6 自签名证书管理](#26-自签名证书管理)
   - [2.7 RTSP 服务端](#27-rtsp-服务端)
3. [数据流全链路](#3-数据流全链路)
4. [文件清单与依赖](#4-文件清单与依赖)

> **技术选型分析**已移至 [techSelectionAnalysis.md](./techSelectionAnalysis.md)

---

## 1. 总体架构设计

### 1.1 功能模块

| 模块                        | 输入                   | 输出                   | 功能                                   |
| ------------------------- | -------------------- | -------------------- | ------------------------------------ |
| RTSP Client               | RTSP URL             | RTP 裸包               | 已有：向媒体源拉流，写入环形缓冲                     |
| StreamServer              | HTTP RPC: playWebRtc | SDP Answer           | 信令：生成 ICE 凭据、SDP、分配端口                |
| StreamNode::iceHandleLoop | UDP 收包               | STUN/DTLS/SRTP 处理    | ICE-Lite + DTLS 握手 + SRTP 解密（同一线程） |
| DtlsTransport             | DTLS 握手包             | SRTP keying material | mbedtls 胶水（~460 行）                    |
| SrptProtect               | RTP 裸包               | SRTP 加密包             | AES-CTR + HMAC-SHA1（~377 行）          |

### 1.2 总体交互流程

从浏览器打开页面到 `<video>` 标签开始播放，整个链路经历 **四个阶段**，总计 **4~5 次网络往返**：

```
┌───────────────────────────────────────────────────────────────┐
│                     阶段一：信令交换 (1 RTT)                      │
│                    HTTP RPC (JSON)                              │
├───────────────────────────────────────────────────────────────┤
│                                                                │
│  浏览器                                          C++ 服务端      │
│    │                                                │          │
│    │ ① POST /rpc  playWebRtc                        │          │
│    │   ─────────────────────────────────────────→  │          │
│    │   {"tag":"camera1", "sdpOffer":"..."}         │          │
│    │                                                │          │
│    │                                     服务端处理:  │          │
│    │                                     ├─ find StreamNode by tag│
│    │                                     ├─ createUDPServerSocket│
│    │                                     │  (绑定随机 UDP 端口)  │
│    │                                     ├─ 生成 ICE ufrag/pwd │
│    │                                     ├─ 构造 SDP Answer    │
│    │                                     │  a=setup:passive    │
│    │                                     │  a=ice-lite         │
│    │                                     │  a=fingerprint:... │
│    │                                     │  a=candidate:...   │
│    │                                     ├─ push → client_sessions_│
│    │                                     └─ startIceHandleThread │
│    │                                                │          │
│    │  ② RPC Response                     │          │
│    │  ←─────────────────────────────────────────  │          │
│    │  {"sdpAnswer":"v=0...","serverRtpPort":5000} │          │
│    │                                                │          │
│    │  pc.setRemoteDescription(answer)                │          │
│    │                                                │          │
└────┼────────────────────────────────────────────────┼──────────┘
     │                                                │
     ▼                                                ▼
┌───────────────────────────────────────────────────────────────┐
│                   阶段二：ICE 连通性检查 (1 RTT)                   │
│              STUN Binding over UDP — 仅 2 条消息                 │
├───────────────────────────────────────────────────────────────┤
│                                                                │
│  浏览器                     UDP :5000              C++ 服务端    │
│    │                                                │          │
│    │ ③ STUN Binding Request                        │          │
│    │   ─────────────────────────────────────────→  │          │
│    │   Type=0x0001, Magic Cookie=0x2112A442        │          │
│    │                                                │ 校验首字节│
│    │                                                │ 构造Response│
│    │  ④ STUN Binding Success Response              │          │
│    │  ←─────────────────────────────────────────   │          │
│    │   Type=0x0101, XOR-MAPPED-ADDRESS              │          │
│    │   + MESSAGE-INTEGRITY + FINGERPRINT            │          │
│    │                                                │          │
│    │  ICE 连通 ✓ → 触发 DTLS                         │ state=S1  │
│    │                                                │          │
└────┼────────────────────────────────────────────────┼──────────┘
     │                                                │
     ▼                                                ▼
┌───────────────────────────────────────────────────────────────┐
│                 阶段三：DTLS 握手 (2~3 RTT)                      │
│    DTLS 1.2 ECDHE-ECDSA-AES128-GCM-SHA256 + SRTP 密钥导出       │
├───────────────────────────────────────────────────────────────┤
│                                                                │
│  浏览器                     UDP :5000              C++ 服务端    │
│    │                                                │          │
│    │  由于 SDP 中 a=setup:passive，                    │          │
│    │  浏览器主动发起 DTLS 握手                          │          │
│    │                                                │          │
│    │ ⑤ DTLS ClientHello                            │          │
│    │   ─────────────────────────────────────────→  │          │
│    │   cipher: TLS_ECDHE_ECDSA_...                  │  mbedtls │
│    │   ext: use_srtp (SRTP 保护配置列表)              │  cookie  │
│    │                                                │  校验    │
│    │ ⑥ DTLS ServerHello + Certificate +             │          │
│    │   ServerKeyExchange + ServerHelloDone         │          │
│    │  ←─────────────────────────────────────────   │          │
│    │   服务端自签名 ECDSA P-256 证书                   │  浏览器   │
│    │                                                │  校验指纹 │
│    │ ⑦ DTLS Certificate +                           │          │
│    │   ClientKeyExchange + CertVerify +             │          │
│    │   ChangeCipherSpec + Finished                  │          │
│    │   ─────────────────────────────────────────→  │          │
│    │                                                │  协商密钥 │
│    │ ⑧ DTLS ChangeCipherSpec + Finished             │          │
│    │  ←─────────────────────────────────────────   │          │
│    │                                                │          │
│    │ ⑨ 握手完成: exportSrptKeys()                    │          │
│    │   mbedtls_ssl_export_keying_material(          │          │
│    │     "EXTRACTOR-dtls_srtp", 60)                  │          │
│    │   → client_write_key(16) + server_write_key(16)│          │
│    │     + client_write_salt(14) + server_write_salt(14)│      │
│    │                                                │ state=S2  │
│    │   → SrptProtect::initFromDtls()                │ state=S3  │
│    │                                                │          │
└────┼────────────────────────────────────────────────┼──────────┘
     │                                                │
     ▼                                                ▼
┌───────────────────────────────────────────────────────────────┐
│               阶段四：SRTP 媒体推流 (持续，无握手)                  │
│              持续单向传输，无额外握手开销                           │
├───────────────────────────────────────────────────────────────┤
│                                                                │
│  RTSP Camera → RTP → [RTP Queue] → SRTP Encrypt → UDP → 浏览器 │
│                                                                │
│  服务端 (循环推流):                                              │
│    ┌──────────────┐        ┌──────────────┐        ┌─────────┐ │
│    │ RTP 环形缓冲  │ ────→ │ srtp_protect │ ────→ │ sendto  │ │
│    │ (已有模块)     │ 取包   │ ① AES-CTR    │ 加密   │ UDP socket│ │
│    └──────────────┘        │ ② HMAC-SHA1  │        └────┬────┘ │
│                            └──────────────┘             │      │
│                                                         │      │
│  浏览器 (自动解密 WebRTC 栈):                              │      │
│    ┌─────────┐     ┌──────────────┐     ┌──────────────┐│      │
│    │ <video> │←─── │ SRTP 解密    │←─── │ recv UDP     ││      │
│    │ srcObject│ 渲染 │ (浏览器内置)   │ 入栈 │              │←──────┘
│    └─────────┘     └──────────────┘     └──────────────┘       │
│                                                                │
└───────────────────────────────────────────────────────────────┘
```

### 1.3 各阶段开销汇总

| 阶段           | 传输协议     | 交互次数        | 消息数  | 典型耗时(局域网)    | 服务端实现量     |
| ------------ | -------- | ----------- | ---- | ------------ | ---------- |
| **信令交换**     | HTTP/TCP | 1 RTT       | 2 条  | ~1-5ms       | ~200 行     |
| **ICE 连通检查** | STUN/UDP | 1 RTT       | 2 条  | ~1ms         | ~180 行     |
| **DTLS 握手**  | DTLS/UDP | 2~3 RTT     | 6~8条 | ~10-30ms     | ~460 行     |
| **SRTP 推流**  | SRTP/UDP | 0 RTT       | 持续   | 每帧 ~2ms      | ~377 行     |
| **合计**       | —        | **4~5 RTT** | —    | **~15-40ms** | **~1200 行** |

> **注：**
>
> - 全部阶段的 UDP 数据复用同一个端口，通过首字节分流 STUN(0x00) / DTLS(0x14-0x18) / SRTP(0x80)。
> - 每个客户端会话由 StreamNode 内一个独立线程（`iceHandleLoop`）处理所有协议。

---

## 2. 模块详细设计

### 2.1 RTSP → RTP 接收层（已有）

已有 C++ 实现，完成以下功能：

```
RTSP Camera ──TCP──→ [RTSP Client] (StreamNode)
                        │
                        ├── DESCRIBE → 获取 SDP (编码格式、track 信息)
                        ├── SETUP    → 协商 RTP/RTCP 端口
                        └── PLAY     → 开始收流

                    [RTP 收包线程]
                        │
                        ├── 解析 RTP 头 (SEQ/SSRC/Timestamp/PT)
                        ├── 提取 NAL 单元 (H.264/H.265)
                        ├── 丢包检测 (SEQ 跳跃)
                        └── 写入环形缓冲队列
```

**本方案不改动此模块**，仅将 RTP 包送入 WebRTC 引擎（SRTP 加密后通过 UDP socket 发送）。

---

### 2.2 StreamServer 信令服务

#### 2.2.1 信令流

```
浏览器                              TDS
  │                                 │
  │── playWebRtc RPC ───────────→  │  浏览器发起播放请求
  │  {"tag": "camera1",             │   (通过 HTTP RPC)
  │   "sdpOffer": "..."}            │
  │                                 │
  │  ←── SDP Answer ─────────────   │  服务端回应 Answer
  │      {"sdpAnswer": "v=0\r\n     │
  │        m=video 5000 UDP/...     │
  │        a=setup:passive          │  浏览器主动发起 DTLS
  │        a=ice-lite               │  ICE-Lite 简化模式
  │        a=fingerprint:sha-256    │  mbedtls 生成的证书指纹
  │        a=candidate:..."}        │
  │                                 │
  │  ←── ICE 连通 (STUN) ──────── │  浏览器自动检查
  │  ←── DTLS 握手 (mbedtls) ──── │  mbedtls DTLS server
  │  ←── SRTP 媒体流 ──────────── │  加密视频数据
```

#### 2.2.2 RPC 接口

**playWebRtc** —— 开始 WebRTC 播放

请求：
```json
{
    "method": "playWebRtc",
    "params": {
        "tag": "camera1",
        "sdpOffer": "xxxxx"
    }
}
```

响应：
```json
{
   "method": "playWebRtc",
   "result": {
        "sdpAnswer": "xxxx",
        "serverRtpPort": 5000,
        "serverRtspPort": 0,
        "clientRtpPort": 0
   }
}
```

#### 2.2.3 SDP Answer 关键字段

```sdp
v=0
o=- 0 0 IN IP4 192.168.1.100
s=TDS
t=0 0
m=video 5000 UDP/TLS/RTP/SAVPF 96
c=IN IP4 192.168.1.100
a=mid:0
a=rtpmap:96 H264/90000
a=fmtp:96 profile-level-id=42C01F;packetization-mode=1;sprop-parameter-sets=...,...;level-asymmetry-allowed=1
a=rtcp-mux
a=rtcp-rsize
a=sendonly
a=setup:passive                             # 浏览器主动发起 DTLS
a=ice-lite                                   # ICE-Lite 模式
a=ice-ufrag:xxxxxxxx                         # 随机 8 字节
a=ice-pwd:xxxxxxxxxxxxxxxxxxxxxx             # 随机 22 字节
a=fingerprint:sha-256 AA:BB:CC:...           # mbedtls 生成的自签证书 SHA-256 指纹
a=ssrc:12345678 cname:TDS
a=candidate:1 1 UDP 2130706431 192.168.1.100 5000 typ host
```

#### 2.2.4 服务端实现（`StreamServer::rpc_playWebRtc`）

```cpp
// streamServer.cpp:185-346
bool StreamServer::rpc_playWebRtc(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session) {
    // 1. 解析 tag → 查找已有 StreamNode
    StreamNode* rc = getStreamNode(tag);

    // 2. 复制流信息，设置 SERVER_PULL 模式
    STREAM_SESSION si = rc->pull_session_;
    si.session_type_ = StreamNode::SERVER_PULL;

    // 3. 创建 UDP 服务 socket（监听随机端口）
    rc->createUDPServerSocket(si);

    // 4. 生成随机 ICE 凭据（每个会话独立）
    std::string iceUfrag;  // 8 字节 base64 随机
    std::string icePwd;    // 22 字节 base64 随机

    // 5. 标记 WebRTC 会话
    si.is_webrtc = true;
    si.ice_ufrag = iceUfrag;
    si.ice_pwd = icePwd;

    // 6. 获取证书指纹（StreamServer 启动时生成一次，全局共享）
    std::string fingerprint = m_dtlsFingerprint;

    // 7. 构造 SDP Answer（含 fmtp/sprop-parameter-sets/rtcp-mux 等）
    std::ostringstream sdp;
    // ... SDP 字段拼接 ...

    // 8. 使用 shared_ptr 加入客户端列表（避免 vector 扩容导致指针失效）
    auto sessionPtr = std::make_shared<StreamNode::STREAM_SESSION>(si);
    rc->client_sessions_.push_back(sessionPtr);

    // 9. 启动 ICE-Lite 线程（内部初始化 DTLS + SRTP）
    rc->startIceHandleThread(sessionPtr);

    // 10. 等待 1 秒后返回 SDP（给 ICE 线程初始化时间）
    // 11. 返回 SDP
    rpcResp.result = json{{"sdpAnswer", si.sdp}, {"serverRtpPort", ...}};
}
```

---

### 2.3 ICE-Lite 实现

#### 2.3.1 应用场景分析

```
场景 A：纯局域网部署                        场景 B：流媒体服务部署在公网
┌──────────────────────────┐               ┌──────────────────────────────┐
│  192.168.1.0/24          │               │         公网 Internet          │
│  ┌──────────┐            │               │  ┌──────────┐  公网 IP        │
│  │ RTSP 摄像头│            │               │  │ 浏览器     │  (在 NAT 后)  │
│  └────┬─────┘            │               │  └─────┬────┘               │
│  ┌────▼──────────────┐   │               │  ┌─────▼──────────────────┐ │
│  │ C++ 转发服务       │   │               │  │ C++ 转发服务            │ │
│  │ (ICE-Lite)        │   │               │  │ (ICE-Lite)             │ │
│  └───────────────────┘   │               │  └────────────────────────┘ │
└──────────────────────────┘               └──────────────────────────────┘

关键结论：两种场景下，浏览器都能直接向服务端的 IP:Port 发送 UDP 包
         且服务端能直接回复，不需要任何 NAT 打洞（hole punching）。
         因此 ICE-Lite 只需要回复 STUN Binding Response 即可。
```

#### 2.3.2 线程模型

ICE、DTLS、SRTP 三种协议在**同一个线程**（`iceHandleLoop`）中处理，每个客户端会话对应一个独立线程：

```
StreamNode::startIceHandleThread(shared_ptr<STREAM_SESSION> session)
  │
  ├─ 设置 socket 1 秒接收超时（保证 stop 时及时退出）
  │
  └─ 启动线程: iceHandleLoop(session)

iceHandleLoop:
  │
  ├─ 初始化 DTLS:
  │   ├─ new SessionDtlsState()
  │   │   ├── DtlsTransport dtls
  │   │   ├── SrptProtect::Context srtp_ctx
  │   │   ├── local_seq (per-session 序列号)
  │   │   └── sps_pps_injected (SPS/PPS 注入标记)
  │   ├─ dtls.init(streamSrv.m_dtlsCertPem, streamSrv.m_dtlsKeyPem)
  │   ├─ dtls.setSocket(session->rtp_socket, {})
  │   └─ dtls.startHandshake()
  │
  └─ while (session->ice_running_):
      │
      ├─ recvfrom(sock, buf, 2048, 1s_timeout)
      │
      ├─ 检查 DTLS 握手超时（ICE 连通后 8 秒内未完成则断开）
      │
      └─ 首字节分流:
          │
          ├─ [0x00, 0x01] → STUN
          │   └─ 校验 Magic Cookie → 构造 Binding Success Response
          │      (含 XOR-MAPPED-ADDRESS + MESSAGE-INTEGRITY + FINGERPRINT)
          │      session->state = S1_ICE_CONNECTED
          │
          ├─ [0x14..0x18] → DTLS
          │   └─ dtls.feedData() → dtls.doHandshakeStep()
          │       握手完成 → exportSrptKeys() → SrptProtect::initFromDtls()
          │       session->state = S3_SRTP_ACTIVE
          │
          ├─ [0x80] → SRTP (来自客户端)
          │   └─ SrptProtect::unprotect()
          │      (WebRTC 服务端场景，客户端通常不发 RTP，此处忽略)
          │
          └─ 其他 → 忽略
```

#### 2.3.3 STUN 处理

ICE-Lite 仅响应一种消息：STUN Binding Request (0x0001)。响应包含：

| 属性                    | 说明                                    |
| --------------------- | ------------------------------------- |
| **XOR-MAPPED-ADDRESS** | 返回客户端的 XOR 映射地址                       |
| **MESSAGE-INTEGRITY**  | 使用 ICE password 计算的 HMAC-SHA1，浏览器要求此属性 |
| **FINGERPRINT**        | CRC-32 校验值，浏览器用于区分 STUN 与其他协议        |

```cpp
// streamNode.cpp iceHandleLoop 中 STUN 处理段（~180 行）
if (firstByte == 0x00 || firstByte == 0x01) {
    uint16_t msgType = (buf[0] << 8) | buf[1];
    uint32_t magic = read_u32_be(buf + 4);
    if (magic != 0x2112A442) continue;
    if (msgType != 0x0001) continue;  // 仅 Binding Request

    // 构造 STUN Binding Success Response:
    //   头 20B: type=0x0101, magic, tid
    //   XOR-MAPPED-ADDRESS 12B
    //   MESSAGE-INTEGRITY 24B (HMAC-SHA1)
    //   FINGERPRINT 8B (CRC-32)
    // HMAC 使用 PSA Crypto API 计算
    // CRC 覆盖消息体不含 FINGERPRINT 属性本身，最后 XOR 0x5354554E
}
```

#### 2.3.4 SESSION_STATE 状态机

```cpp
enum SESSION_STATE {
    S0_WAITING_ICE = 0,     // 初始：等待 STUN Binding Request
    S1_ICE_CONNECTED,        // ICE 连通：STUN Response 已发送
    S2_DTLS_COMPLETED,       // DTLS 握手完成
    S3_SRTP_ACTIVE           // SRTP 密钥已导出，开始加密推流
};
```

> 状态只升不降：收到 keep-alive STUN Binding Request 时不会从 S3 降级回 S1。

---

### 2.4 DTLS 握手（DtlsTransport）

#### 2.4.1 DtlsTransport 类

**文件：** `src/video/dtls_transport.h` / `dtls_transport.cpp`（~460 行）

mbedtls 核心 API 使用：

| 步骤       | mbedtls API                                                      | 说明                                |
| -------- | ---------------------------------------------------------------- | --------------------------------- |
| 初始化      | `mbedtls_ssl_init()` / `mbedtls_ssl_config_init()`               | SSL 上下文和配置                        |
| 随机数      | `mbedtls_ctr_drbg_seed()` + `mbedtls_entropy_func()`             | CTR-DRBG 随机数生成器                   |
| 证书       | `mbedtls_x509_crt_parse()` / `mbedtls_pk_parse_key()`            | 加载 PEM 证书和 ECDSA 私钥               |
| 配置       | `mbedtls_ssl_config_defaults(SERVER, DATAGRAM)`                  | DTLS 服务端模式                        |
| TLS 版本   | `mbedtls_ssl_conf_min_tls_version(TLS1_2)`                       | 锁定 TLS 1.2                        |
| 密码套件     | `MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256`                | WebRTC 推荐套件                       |
| use_srtp | `mbedtls_ssl_conf_dtls_srtp_protection_profiles()`               | **关键：SRTP 保护配置协商**                |
| Cookie   | `mbedtls_ssl_cookie_setup()` + `mbedtls_ssl_conf_dtls_cookies()` | DTLS HelloVerifyRequest 防 DoS     |
| 握手       | `mbedtls_ssl_handshake_step()`                                   | **非阻塞步进握手**                       |
| 密钥导出     | `mbedtls_ssl_export_keying_material()`                           | **导出 60 字节 SRTP keying material** |
| 指纹       | `mbedtls_md(MBEDTLS_MD_SHA256, cert.raw.p, cert.raw.len)`        | SHA-256 证书指纹                      |
| Socket   | 自定义 BIO 回调 `bio_send()` / `bio_recv()`                           | bio_send 直接 `sendto`，bio_recv 从内部缓冲读 |

#### 2.4.2 DTLS 配置

```cpp
// dtls_transport.cpp:92-180 init()
// 密码套件：仅 ECDHE-ECDSA-AES128-GCM-SHA256（WebRTC 官方推荐）
static const int suites[] = {
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    0
};

// use_srtp 扩展：协商 SRTP 保护配置
static const mbedtls_ssl_srtp_profile srtp_profiles[] = {
    MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80,
    MBEDTLS_TLS_SRTP_UNSET
};

// 不验证客户端证书（WebRTC 场景客户端通常不提供）
mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_NONE);

// DTLS 超时：min 1s, max 30s
mbedtls_ssl_conf_handshake_timeout(&conf_, 1000, 30000);
```

#### 2.4.3 数据喂入机制

DTLS 数据不直接在 BIO 回调中读 socket，而是通过内部缓冲区解耦：

```
iceHandleLoop: recvfrom(UDP) → 首字节分流为 DTLS → dtls.feedData(buf, len)
                                                         │
                                                  [内部 recv_buf_]
                                                         │
mbedtls: mbedtls_ssl_handshake_step() → bio_recv() → 从 recv_buf_ 读数据
         mbedtls_ssl_handshake_step() → bio_send() → sendto() 直接发包
```

#### 2.4.4 握手流程

```cpp
// 在 iceHandleLoop 中
// 1. 初始化（线程启动时）
auto* dtls_state = new SessionDtlsState();
dtls_state->dtls.init(certPem, keyPem);
dtls_state->dtls.setSocket(session->rtp_socket, {});
dtls_state->dtls.startHandshake();

// 2. 每次收到 DTLS 包时（首字节 0x14-0x18）
dtls_state->dtls.setSocket(session->rtp_socket, peer);
dtls_state->dtls.setClientTransportId(peer); // Cookie HMAC 需要
dtls_state->dtls.feedData(buf, len);

// 3. 循环推进状态机直到需要等待或出错
while (true) {
    int ret = dtls_state->dtls.doHandshakeStep();
    if (dtls_state->dtls.isHandshakeDone()) {
        // 握手成功 → 导出 SRTP 密钥
        const auto& keys = dtls_state->dtls.getKeyingMaterial();
        dtls_state->srtp_ctx = SrptProtect::initFromDtls(keys, true, 0);
        session->state = S3_SRTP_ACTIVE;
        break;
    } else if (ret == WANT_READ || ret == WANT_WRITE) {
        break;  // 等待对端数据
    } else if (ret == HELLO_VERIFY_REQUIRED) {
        break;  // Cookie 验证流程，等待客户端重发
    } else if (ret == 0) {
        continue;  // 中间步骤，继续推进
    } else {
        break;  // 错误
    }
}
```

#### 2.4.5 SRTP 密钥导出

```cpp
// RFC 5764 §4.2: use_srtp DTLS-SRTP keying material export
void DtlsTransport::exportSrptKeys() {
    unsigned char keyblk[60];  // 2 × (16 key + 14 salt) = 60 bytes
    const char* label = "EXTRACTOR-dtls_srtp";

    mbedtls_ssl_export_keying_material(
        &ssl_, keyblk, sizeof(keyblk), label, strlen(label), nullptr, 0, 1);

    // 拆分:
    //   client_write_key  [0..15]
    //   server_write_key  [16..31]
    //   client_write_salt [32..45]
    //   server_write_salt [46..59]
    memcpy(keying_material_.client_write_key,  keyblk,      16);
    memcpy(keying_material_.server_write_key,  keyblk + 16, 16);
    memcpy(keying_material_.client_write_salt, keyblk + 32, 14);
    memcpy(keying_material_.server_write_salt, keyblk + 46, 14);
    keying_material_.ready = true;
}
```

#### 2.4.6 SessionDtlsState 结构

```cpp
// streamNode.cpp:2670-2681
struct SessionDtlsState {
    DtlsTransport dtls;              // mbedtls 封装
    SrptProtect::Context srtp_ctx;   // SRTP 加密上下文
    bool dtls_initialized = false;
    bool srtp_ready = false;
    // Per-session RTP 序列号：原始流 seq 不能直接透传，
    // 每个 WebRTC 客户端需要独立连续的序列号
    uint16_t local_seq = 0;
    bool    seq_inited = false;
    // SPS/PPS 注入标记
    bool    sps_pps_injected = false;
};
```

> `STREAM_SESSION` 中通过 `void* dtls_transport_` 和 `void* srtp_context_` 指针引用，避免头文件循环依赖。

---

### 2.5 SRTP 加密/解密（SrptProtect）

#### 2.5.1 SrptProtect 类

**文件：** `src/video/srtp_protect.h` / `srtp_protect.cpp`（~377 行）

| 组件              | 实现                           | 说明                                          |
| --------------- | ---------------------------- | ------------------------------------------- |
| **AES-128-CTR** | `SrptProtect::aesCtrCrypt()` | 使用 mbedtls `mbedtls_aes_crypt_ecb` 实现计数器模式 |
| **HMAC-SHA1**   | `SrptProtect::hmacSha1()`    | RFC 2104 标准实现：ipad/opad 填充 + mbedtls SHA1   |
| **SHA-1**       | mbedtls `mbedtls_sha1`       | 使用 mbedtls 库函数                              |
| **IV 构造**       | `SrptProtect::buildIv()`     | RFC 3711 §4.1.1: salt XOR (SSRC \| index)  |

#### 2.5.2 Key Derivation（RFC 3711 §4.3）

当 `key_derivation_rate = 0`（WebRTC 默认）时，session keys 直接从 master key + master salt 构成：

```
k_e (encrypt key)  = master_key 的前 16 字节
k_a (auth key)     = master_key 填充到 20 字节（后 4 字节补 0x00）
k_s (salting key)  = master_salt 的前 14 字节
```

> encrypt_key 和 auth_key 的前 16 字节相同，与 libsrtp（浏览器）行为一致。

#### 2.5.3 SRTP 加密流程

```
srtp_protect(ctx, rtp_packet):
  │
  ├─ 1. 提取 RTP 序号: seq = (rtp[2]<<8) | rtp[3]
  │    index = (roc << 16) | seq
  │
  ├─ 2. 构造 IV: buildIv(iv, salt, ssrc, index)
  │    IV = salt(14B) XOR (0..0 | SSRC | ROC | SEQ | 0x0000)
  │
  ├─ 3. AES-CTR 加密 payload:
  │    aesCtrCrypt(key, iv, rtp + 12, payload_len)
  │    → 计数器模式: keystream[i] = AES(counter++) ^ plaintext[i]
  │
  ├─ 4. HMAC-SHA1 计算认证标签:
  │    hmacSha1(auth_key, header + encrypted_payload + ROC(4B BE), &tag)
  │    → 截取前 10 字节 (80-bit)
  │
  └─ 5. 组装 SRTP 包:
       RTP header(12B) | encrypted payload | auth_tag(10B)
```

#### 2.5.4 SRTP Context 初始化

```cpp
// 从 DTLS 导出的 keying material 创建 SRTP 上下文
SrptProtect::Context ctx = SrptProtect::initFromDtls(
    dtls.getKeyingMaterial(),
    true,  // is_server → 使用 server_write_key/salt
    ssrc
);

// ctx 结构:
struct Context {
    uint8_t  encrypt_key[16];   // AES-128 key (server_write_key)
    uint8_t  encrypt_salt[14];  // SRTP salt (server_write_salt)
    uint8_t  auth_key[20];      // HMAC-SHA1 key (前16字节同encrypt_key，后4字节补0)
    uint32_t ssrc;
    uint32_t rollover_counter;  // ROC
    uint16_t highest_seq;       // 最高序号（初始0xFFFF，确保首包通过重放检测）
};
```

#### 2.5.5 SRTP 发送路径（含 SPS/PPS 注入）

```cpp
// streamNode.cpp:2727-2855 sendRTPPacketToClients WebRTC 部分
for (auto& session : client_sessions_) {
    if (!session->is_webrtc) continue;
    if (session->state != S3_SRTP_ACTIVE) continue;

    auto* dtlsState = static_cast<SessionDtlsState*>(session->dtls_transport_);

    // Per-session 序列号管理
    if (!dtlsState->seq_inited) {
        dtlsState->local_seq = packet.sequence_number;
        dtlsState->seq_inited = true;
    }

    // IDR 前注入 SPS/PPS（每个 WebRTC 客户端独立）
    if (isIdr && !session->last_was_idr_ && session 有 sps/pps) {
        sendSingleNalRtp(sps);  // 构造 RTP 包 → SRTP 加密 → sendto
        sendSingleNalRtp(pps);
    }
    session->last_was_idr_ = isIdr;

    // 替换序列号为 per-session 独立序列号
    rtpVec[2] = (local_seq >> 8) & 0xFF;
    rtpVec[3] = local_seq & 0xFF;
    local_seq++;

    // SRTP 加密 → sendto
    auto srtpPkt = SrptProtect::protect(srtpCtx, rtpVec);
    sendto(session->rtp_socket, srtpPkt, ..., peerAddr);
}
```

> 关键设计：每个 WebRTC 客户端有独立的序列号空间。原始 RTSP 流的 seq 可能不连续（重连等场景），但每个 WebRTC 客户端的 SRTP seq 必须严格连续递增，否则浏览器会因重放检测丢包。

---

### 2.6 自签名证书管理

#### 2.6.1 为什么需要证书

WebRTC 的 DTLS-SRTP 采用**证书指纹模式**（RFC 8122）：

```
1. StreamServer 启动时生成 ECDSA P-256 密钥 + 自签名证书（一次，全局共享）
2. SHA-256(证书 DER) → 指纹写入 SDP Answer 的 a=fingerprint:sha-256:...
3. 浏览器收到 Answer 后，在 DTLS 握手时验证服务端证书指纹
4. 指纹匹配 → 握手继续；不匹配 → 握手失败

注意：浏览器不验证 CA 链、过期时间、CN 等，只验证指纹！
      自签名证书完全够用。
```

#### 2.6.2 证书加载策略

```cpp
// streamServer.cpp:17-66 initDtlsCertificate()
void StreamServer::initDtlsCertificate() {
    // 1. 优先从配置目录 webRtcKey/ 加载持久化的证书和私钥
    std::string certPath = confPath + "/webRtcKey/cert.pem";
    std::string keyPath  = confPath + "/webRtcKey/key.pem";
    if (文件存在) → 加载;

    // 2. 兜底：运行时生成自签证书（仅内存，不写盘）
    if (为空) {
        DtlsTransport::generateSelfSignedCert("TDS WebRTC Server", certPem, keyPem);
    }

    // 3. 计算 SHA-256 指纹用于 SDP
    mbedtls_x509_crt_parse(&cert, certPem);
    m_dtlsFingerprint = DtlsTransport::getFingerprint(cert);
}
```

#### 2.6.3 mbedtls 自签证书生成（`DtlsTransport::generateSelfSignedCert`）

```cpp
// dtls_transport.cpp:325-460
void DtlsTransport::generateSelfSignedCert(string cn, string& cert_str, string& key_str) {
    // 1. 初始化 PSA Crypto
    psa_crypto_init();

    // 2. 使用 PSA Crypto API 生成 ECDSA P-256 密钥对
    psa_generate_key(PSA_KEY_TYPE_ECC_KEY_PAIR(SECP_R1), 256, &kid);

    // 3. 将 PSA key 导入 mbedtls_pk_context（mbedTLS 4.x 兼容）
    mbedtls_pk_copy_from_psa(kid, &key);

    // 4. 写入证书字段
    mbedtls_x509write_crt_set_subject_name("CN=...,O=TDS,C=CN");
    mbedtls_x509write_crt_set_validity("20240101000000", "20340101000000");
    mbedtls_x509write_crt_set_md_alg(MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_basic_constraints(CA=FALSE);
    mbedtls_x509write_crt_set_key_usage(digitalSignature | keyEncipherment);
    mbedtls_x509write_crt_set_ns_cert_type(SSL_SERVER);

    // 5. 自签名 + 导出 PEM
    mbedtls_x509write_crt_pem(&crt, cert_pem);
    mbedtls_pk_write_key_pem(&key, key_pem);
}
```

#### 2.6.4 证书管理

```
StreamServer 启动时:
  initDtlsCertificate()
    → 优先加载 webRtcKey/cert.pem + key.pem（持久化）
    → 兜底: generateSelfSignedCert("TDS WebRTC Server")（内存生成）
    → 存储到 m_dtlsCertPem / m_dtlsKeyPem / m_dtlsFingerprint

每次 playWebRtc:
  → 复用 m_dtlsCertPem / m_dtlsKeyPem（不重新生成）
  → m_dtlsFingerprint 写入 SDP Answer

ICE 线程启动时:
  → dtls.init(streamSrv.m_dtlsCertPem, streamSrv.m_dtlsKeyPem)
  → 每个会话独立 DtlsTransport 实例，共享同一份证书
```

---

### 2.7 RTSP 服务端

StreamServer 同时作为 RTSP 服务端，支持以下两种模式：

#### 拉流模式（DESCRIBE → SETUP → PLAY）

```
RTSP Client ──TCP──→ [StreamServer RTSP 服务端]
                        │
                        ├── OPTIONS  → 返回支持的方法
                        ├── DESCRIBE → 返回 SDP
                        ├── SETUP    → 协商 RTP/RTCP 端口，创建 UDP socket
                        └── PLAY     → 开始推流（通过 sendRTPPacketToClients）
```

#### 推流模式（ANNOUNCE → SETUP → RECORD）

```
RTSP Client ──TCP──→ [StreamServer RTSP 服务端]
                        │
                        ├── ANNOUNCE → 客户端推送 SDP，服务端创建 StreamNode
                        ├── SETUP    → 服务端创建 UDP socket 接收 RTP
                        └── RECORD   → 启动 rtpRecvThread 接收并分发 RTP
```

推流模式的数据流：
```
RTSP Client → UDP RTP → rtpRecvThread → addToRtpBuffer + sendRTPPacketToClients
```

---

## 3. 数据流全链路

### 3.1 时间线

```
T0    浏览器访问 TDS 管理页面
      → HTTP GET → 返回内嵌 HTML 页面

T1    用户点击播放，JS 执行:
      pc = new RTCPeerConnection()
      pc.addTransceiver('video', { direction: 'recvonly' })
      offer = await pc.createOffer()
      await pc.setLocalDescription(offer)
      rpc.playWebRtc({ tag: "camera1", sdpOffer: "..." })

T2    服务端收到 RPC
      → 查找已有 StreamNode（通过 tag）
      → createUDPServerSocket(si) — 分配随机 UDP 端口
      → 生成 ICE ufrag/pwd
      → 构造 SDP Answer (含证书指纹、fmtp、候选地址)
      → shared_ptr push si → client_sessions_
      → startIceHandleThread(sessionPtr) — 启动 per-session 线程
      → 等待 1 秒 → 返回 SDP Answer

T3    浏览器收到 SDP Answer
      → pc.setRemoteDescription(answer)
      → ICE: 自动向服务端 UDP 端口发 STUN Binding Request

T4    服务端 iceHandleLoop 处理 STUN Binding Request
      → 回复 STUN Binding Success Response (含 MESSAGE-INTEGRITY + FINGERPRINT)
      → 浏览器确认 ICE 连通 ✓
      → session->state = S1_ICE_CONNECTED

T5    DTLS 握手 (a=setup:passive，浏览器主动发起)
      → 浏览器发送 ClientHello → mbedtls 处理 (含 Cookie 验证)
      → 服务端 ServerHello + Certificate + ServerKeyExchange + ServerHelloDone
      → 浏览器验证证书指纹 → OK
      → ClientKeyExchange + CertificateVerify + CCS + Finished
      → 服务端 CCS + Finished
      → mbedtls_ssl_export_keying_material("EXTRACTOR-dtls_srtp", 60)
      → SrptProtect::initFromDtls()
      → session->state = S3_SRTP_ACTIVE

T6    开始视频播放
      → 服务端从 RTP 缓冲队列取包
      → sendRTPPacketToClients:
        ├─ 检测 IDR → 注入 SPS/PPS (per-session)
        ├─ 替换 per-session 独立序列号
        ├─ SrptProtect::protect() 加密
        └─ sendto() 发送到浏览器 UDP socket
      → 浏览器 WebRTC 栈解密 → <video> 渲染

端到端延迟: T0→T6 约 500-1000ms (首帧)
稳态延迟: RTP→SRTP→浏览器 约 100-200ms
```

### 3.2 稳态数据流

```
[RTSP Camera]                                [Browser <video>]
     │                                               ▲
     │ RTP 裸包                                      │
     ▼                                               │
┌────────────┐                                       │
│ RTP Queue  │ (StreamNode 已有)                      │
│ (环形缓冲)  │                                       │
└─────┬──────┘                                       │
      │                                               │
      ▼                                               │
┌──────────────┐   SRTP 加密包 (UDP)    ┌──────────┐  │
│ SrptProtect  │───────────────────────→│ 浏览器     │──┘
│ ① buildIv() │                        │ WebRTC   │
│ ② aesCtrCrypt│ ←── RTCP SR/RR ──────│ 栈解密    │
│ ③ hmacSha1()│     (可选)             │ 渲染      │
│ ④ sendto()  │                        └──────────┘
└──────────────┘
  通过 session->rtp_socket 发送
  加密约 0.5-2ms/包
  每个 WebRTC 客户端独立序列号 + SPS/PPS 注入
```

---

## 4. 文件清单与依赖

### 4.1 文件清单

| 文件                             | 行数    | 职责                                            |
| ------------------------------ | ----- | --------------------------------------------- |
| `src/video/dtls_transport.h`   | 158   | DtlsTransport 类声明、SrptKeyingMaterial 结构       |
| `src/video/dtls_transport.cpp` | 469   | DTLS 握手、SRTP 密钥导出、自签证书生成                      |
| `src/video/srtp_protect.h`     | 83    | SrptProtect 类声明、Context 结构                    |
| `src/video/srtp_protect.cpp`   | 377   | AES-CTR (mbedtls)、HMAC-SHA1 (mbedtls)、SRTP protect/unprotect |
| `src/video/streamServer.h`     | 86    | StreamServer 声明、DTLS 证书字段、RTSP 服务端             |
| `src/video/streamServer.cpp`   | ~1702 | RPC playWebRtc、SDP 生成、证书初始化、RTSP 服务端          |
| `src/video/streamNode.h`       | ~556  | STREAM_SESSION、IceThreadCtx、DTLS/SRTP 指针      |
| `src/video/streamNode.cpp`     | ~3744 | ICE-Lite 线程、DTLS 集成、SRTP 发送（iceHandleLoop + sendRTPPacketToClients） |

### 4.2 依赖总览

| 依赖               | 类型    | 用途                                                                 |
| ---------------- | ----- | ------------------------------------------------------------------ |
| **mbedtls**      | 静态链接库 | DTLS 1.2 握手、ECDSA P-256 密钥/证书、use_srtp 扩展、SRTP 密钥导出、SHA-256 指纹、随机数、AES、SHA1 |
| **PSA Crypto**   | mbedtls 子模块 | ECDSA 密钥生成、STUN MESSAGE-INTEGRITY 的 HMAC-SHA1 计算            |

> 当前版本已无手写加密算法：AES-CTR、HMAC-SHA1、SHA-1 全部使用 mbedtls 库函数实现。

### 4.3 WebRTC 相关新增代码量

| 文件                          | 新增行数        | 职责                                           |
| --------------------------- | ----------- | -------------------------------------------- |
| `dtls_transport.h`          | 158         | DtlsTransport 类声明                            |
| `dtls_transport.cpp`        | 469         | mbedtls 胶水：初始化、握手、SRTP 密钥导出、自签证书             |
| `srtp_protect.h`            | 83          | SrptProtect 类声明                              |
| `srtp_protect.cpp`          | 377         | AES-CTR、HMAC-SHA1、protect/unprotect          |
| `streamNode.h` (+WebRTC)    | ~50         | STREAM_SESSION ICE/DTLS/SRTP 字段、SESSION_STATE |
| `streamNode.cpp` (+WebRTC)  | ~550        | iceHandleLoop、SessionDtlsState、SRTP 发送路径    |
| `streamServer.h` (+证书/RTSP)| ~50         | DTLS 证书字段、RTSP 服务端声明                       |
| `streamServer.cpp` (+WebRTC)| ~200        | RPC playWebRtc、SDP 生成、证书初始化                  |
| **合计**                      | **~1937 行** | 全部 WebRTC 相关新增代码                             |

> 注：不含已有 RTSP/RTP 模块代码（streamNode 约 3190 行非 WebRTC 代码）。

---

> **文档版本：** v3.0
> **最后更新：** 2026-07-07
> **变更说明：** 基于最新代码全面重写——修正 SDP setup 方向（passive）、SESSION_STATE 状态机、MESSAGE-INTEGRITY/FINGERPRINT、SessionDtlsState per-session 管理、SPS/PPS 注入、证书磁盘持久化、RTSP 服务端、加密算法全部使用 mbedtls 库函数。技术选型对比内容已拆分至 `techSelectionAnalysis.md`。
