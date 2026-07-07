# TDS 流媒体服务 — 技术选型分析

> 本文档从 `streamServer.md` 中拆分出来，专门记录 TDS WebRTC 流媒体服务的技术选型对比与决策过程。

---

## 1. 技术路线选择：WebRTC vs QUIC

### 1.1 浏览器 UDP 传输的唯二选择

浏览器出于安全考虑，不允许 JavaScript 直接操作 UDP Socket。要在浏览器端通过 UDP 接收视频流，只有两条路：

```
┌──────────────────────────────────────────────────┐
│              浏览器 UDP 数据传输能力                │
│                                                  │
│   WebRTC (RTCPeerConnection)    ← 本方案选择       │
│   ├── 所有主流浏览器支持 (Chrome/Firefox/Safari/Edge)│
│   ├── 无需浏览器扩展/实验性 flag                    │
│   ├── 专为实时媒体传输设计 (SRTP)                   │
│   └── 服务端只需实现媒体通道，可大幅裁剪              │
│                                                  │
│   QUIC / WebTransport                            │
│   ├── Chrome/Edge 支持，Firefox/Safari 不支持      │
│   ├── 通用数据传输协议，非媒体专用                  │
│   ├── 需要实现完整 QUIC 协议栈                      │
│   └── HTTP/3 之上，协议层更多                      │
└──────────────────────────────────────────────────┘
```

### 1.2 结论

WebRTC 是唯一合理选择——浏览器覆盖广（全部主流）、服务端可裁剪（只需媒体通道）、实现量可控。

---

## 2. DTLS/SRTP 库选型对比

### 2.1 三种候选方案

设计阶段对比了 OpenSSL 方案和 tinydtls 源码引入方案。实际实现时选择了 **mbedtls** 作为中间路线——兼顾了 OpenSSL 的功能完整性和源码引入的跨平台便利性。

|                  | OpenSSL 方案                      | tinydtls 源码引入方案            | **mbedtls 方案（实际采用）**                                         |
| ---------------- | ------------------------------- | -------------------------- | ------------------------------------------------------------ |
| DTLS             | libssl (`DTLS_method()`)        | tinydtls（~12 个 .c/.h）      | **`mbedtls_ssl_*()` API**（静态链接库）                             |
| ECC              | OpenSSL 内置 ECDHE                | micro-ecc（2 个 .c/.h）       | **mbedtls ECDSA P-256**（PSA Crypto API）                      |
| `use_srtp` 扩展    | `SSL_CTX_set_tlsext_use_srtp()` | **❌ 不支持，需 ~60 行补丁**        | **`mbedtls_ssl_conf_dtls_srtp_protection_profiles()`**（原生支持） |
| SRTP 密钥导出        | `SSL_export_keying_material()`  | 需手写（~50 行）                 | **`mbedtls_ssl_export_keying_material()`**（原生支持）             |
| 证书生成             | `X509_new()` / `X509_sign()`    | 手写 X.509 DER 编码（~100 行）    | **`mbedtls_x509write_crt_*()` API**（~140 行）                  |
| SHA1 / HMAC-SHA1 | OpenSSL 内置                      | 手写（~200 行）                 | **mbedtls SHA1/HMAC**（mbedtls 库函数）                         |
| AES-CTR          | OpenSSL EVP                     | tiny-AES-c（2 个 .c/.h）      | **mbedtls AES**（`mbedtls_aes_crypt_ecb`）                        |
| 集成方式             | 链接动态/静态库                        | 源码编译进同一可执行文件               | **链接 mbedtls 静态库**（项目已有依赖）                                   |
| 新增业务代码           | ~600 行胶水代码                      | ~950 行（含 DTLS + SRTP + 证书） | **~850 行**（DtlsTransport ~420 + SrptProtect ~300 + 证书 ~140）  |

### 2.2 为什么选择 mbedtls（而非 tinydtls）

```
关键决策点：

① tinydtls 的致命缺陷：不支持 use_srtp 扩展
   → DTLS 握手可以完成，但无法协商 SRTP 保护配置
   → 浏览器收到 ServerHello 后，发现缺少 use_srtp → DTLS-SRTP 失败
   → 这是不可绕过的功能缺失，不是实现量的问题

② mbedtls 的折中优势
   ├── 原生支持 use_srtp 扩展（RFC 5764）          ← tinydtls 没有
   ├── 原生支持 DTLS-SRTP keying material 导出      ← tinydtls 没有
   ├── 原生支持 ECDSA P-256 证书生成                 ← 无需 micro-ecc
   ├── 相比 OpenSSL：API 更简洁、跨平台编译更简单
   ├── 项目已有 mbedtls 依赖（头文件/库路径已配置）
   └── 纯 C 实现，ARM/MIPS/x86 通用，无系统依赖
```

### 2.3 mbedtls 负责的部分 vs 业务代码手写的部分

```
┌─────────────────────────────────────────────────────────┐
│                  mbedtls 负责的部分                         │
│                                                          │
│  DTLS 1.2 状态机    ← mbedtls_ssl_handshake_step()       │
│  ECDSA P-256 密钥    ← PSA Crypto psa_generate_key()      │
│  use_srtp 扩展      ← mbedtls_ssl_conf_dtls_srtp_*()      │
│  SRTP 密钥导出       ← mbedtls_ssl_export_keying_material()│
│  自签证书生成        ← mbedtls_x509write_crt_*()          │
│  SHA-256 指纹       ← mbedtls_md(MBEDTLS_MD_SHA256)      │
│  随机数生成          ← mbedtls_ctr_drbg_*()               │
│  DTLS Cookie        ← mbedtls_ssl_cookie_*()             │
│                                                          │
├─────────────────────────────────────────────────────────┤
│               业务代码手写的部分                             │
│                                                          │
│  ICE-Lite (STUN)    ← StreamNode::iceHandleLoop()        │
│  DTLS 胶水代码       ← DtlsTransport 类 (~420 行)         │
│  SRTP 加密/解密      ← SrptProtect 类 (~300 行)            │
│  AES-CTR 计数器模式  ← SrptProtect::aesCtrCrypt() (~50行) │
│  HMAC-SHA1          ← SrptProtect::hmacSha1() (~30行)    │
│  SDP 生成           ← StreamServer::rpc_playWebRtc()      │
│  端口分配与管理       ← StreamNode::createUDPServerSocket()│
└─────────────────────────────────────────────────────────┘
```

---

## 3. 应用场景约束

### 典型部署拓扑

```
                         IoT 边缘网关
┌──────────┐   RTSP   ┌─────────────────────┐   WebRTC/UDP   ┌──────────┐
│ IPC 摄像头 │────────→│   C++ 转发服务 (本方案)  │───────────────→│  浏览器   │
│ (H.264)   │  RTP    │                     │   SRTP 加密    │ <video>  │
└──────────┘         │ • RTSP 拉流(已有)     │               │  标签    │
     ×N              │ • HTTP/RPC 信令      │               └──────────┘
                     │ • ICE-Lite          │
                     │ • DTLS 握手 (mbedtls)│
                     │ • SRTP 加密转发       │
                     └─────────────────────┘

部署环境：ARM32/ARM64/MIPS/x86 嵌入式 Linux 边缘网关
传输要求：端到端延迟 < 500ms，UDP 传输避免 TCP 重传阻塞
```

### 核心需求

| 需求    | 说明                                         |
| ----- | ------------------------------------------ |
| 极低延迟  | 监控场景要求视频延迟 < 500ms，必须使用 UDP 传输             |
| 多平台部署 | 海思、瑞芯微、树莓派、君正等，mbedtls 纯 C 实现，跨平台友好        |
| 单文件部署 | 最终产物为一个独立可执行文件，mbedtls 可静态链接               |
| 浏览器通用 | 支持 Chrome/Firefox/Edge/Safari 主流浏览器        |
| 源码级可控 | mbedtls 作为唯一外部依赖，DTLS/SRTP 胶水代码约 850 行业务代码 |

---

> **文档版本：** v1.0
> **来源：** 从 `streamServer.md` v2.0 拆分
> **变更说明：** 将技术选型对比内容独立为单独文档，方便后续技术演进参考。
