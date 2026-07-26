# StreamNode 重构方案

## 原则

- **只做有实际价值的改动**，不为了"整齐"而移动代码
- **外科手术式修改**：不碰不需要改的代码
- **零行为变化**：重构不改功能

---

## 不做的事（及原因）

### 1. RTSP 信令方法不改（不换文件）

`rtspDescribe/Setup/Play/Teardown/Announce/Record/GetParameter`、`parseSDP`、`generateSDP`、认证函数等，**保持在 `streamNode_rtsp.cpp` 不变**。

原因：这些方法全部依赖 `StreamNode` 内部成员（`config_`、`source_conn_`、`source_auth_`、`stats_mutex_`、`logError/setError/generateCSeq` 等），换文件纯粹是"改个 .cpp 名"，无实质收益。

### 2. `streamNode.h` 类型不提取

`STREAM_SESSION`、`Connection`、`AuthInfo`、`URLComponents`、`RTSPMessage`、`RTPPacket` 等**保持在 `streamNode.h`**。

原因：这些类型被 `src/video/` 几乎所有文件引用，新增 `streamSession.h` 会导致：
- 全量重编译
- 循环依赖风险（`STREAM_SESSION` 含 `std::thread`，`StreamNode` 含 `STREAM_SESSION`）
- 没有任何解耦收益

### 3. `controlThread` / `openOriginPullSession` / `openRelayPushSession` 不动

这些本身就是控制流编排，不存在"协议 vs 控制流"的切割点。

### 4. 文件名不改

`streamNode_webrtc.cpp` 不改名为 `streamSession_webrtc.cpp`。WebRTC handler 变为 `STREAM_SESSION` 方法后在原文件编辑即可。

---

## 要做的事（3 项）

### 一、WebRTC 协议 handler 变为 `STREAM_SESSION` 方法

**文件**：`src/video/streamNode_webrtc.cpp` + `src/video/streamNode.h`

**改动**：

| 原方法（`StreamNode` 成员） | 改为 |
|---|---|
| `void StreamNode::webrtcSession_handle_STUN(session, buf, len, peer, dtls_start)` | `void STREAM_SESSION::handleSTUN(buf, len, peer, dtls_start)` |
| `void StreamNode::webrtcSession_handle_DTLS(session, state, buf, len, peer, dtls_start)` | `void STREAM_SESSION::handleDTLS(state, buf, len, peer, dtls_start)` |
| `void StreamNode::webrtcSession_handle_SRTCP(session, state, buf, len, peer)` | `void STREAM_SESSION::handleSRTCP(state, buf, len, peer)` |
| `std::string StreamNode::STREAM_SESSION::getSessionStateDesc()` | 已在此文件，不移动 |

这些方法只访问 `session` 自身的 `dtls_transport_`、`srtp_context_`、`ice_ufrag`、`ice_pwd`、`video_ssrc`，以及 `LOG()` 宏，不依赖 `StreamNode`。适合作为 session 自身的方法。

**`rtcSessionHandleThread` 调用点变更**（在 `streamNode_webrtc.cpp` 中，保持为 `StreamNode` 方法）：

```cpp
// 之前
webrtcSession_handle_STUN(session, buf, len, peer, dtls_start);
webrtcSession_handle_DTLS(session, dtls_state, buf, len, peer, dtls_start);
webrtcSession_handle_SRTCP(session, dtls_state, buf, len, peer);

// 之后
session->handleSTUN(buf, len, peer, dtls_start);
session->handleDTLS(dtls_state, buf, len, peer, dtls_start);
session->handleSRTCP(dtls_state, buf, len, peer);
```

**`streamNode.h` 变更**：
- 删除 three 个 `webrtcSession_handle_*` 声明
- 在 `STREAM_SESSION` 结构体中添加三个方法的声明

---

### 二、UDP socket 函数参数化

**文件**：`src/video/streamNode_socket.cpp` + `src/video/streamNode.h`

**改动**：

| 原方法 | 改为 | 说明 |
|---|---|---|
| `createUDPPullSocket()` | `createUDPPair(STREAM_SESSION&)` | 消除硬编码 `session_origin_pull_` |
| `createUDPPushSocket()` | **合并到上面** | 与 Pull 逻辑完全相同 |
| `createUDPServerSocket(STREAM_SESSION&)` | **不变** | 已接受 session 参数 |
| `closeUDPSockets()` | **删除**，由 `STREAM_SESSION::close()` 替代 | 见第三项 |

`createUDPPullSocket` 和 `createUDPPushSocket` 代码完全一样，唯一区别是目标 session。合并为一个自由函数：

```cpp
// streamNode_socket.cpp
static bool createUDPPair(STREAM_SESSION& session, const Config& config,
                          const std::function<void(const std::string&)>& logInfo,
                          const std::function<void(const std::string&)>& logError) {
    // 原 createUDPPullSocket 的逻辑，但写入 session 而非 session_origin_pull_
    // config 参数替代 config_.udp_*
}
```

**调用点变更**：

```cpp
// 之前
createUDPPullSocket();    // → 写入 session_origin_pull_
createUDPPushSocket();    // → 写入 session_relay_push_

// 之后
createUDPPair(session_origin_pull_, config_, ...);
createUDPPair(session_relay_push_, config_, ...);
```

`configureUDPSocket` 同样参数化，接收 `Config&` 替代直接访问 `config_`。

---

### 三、`STREAM_SESSION::close()` 自清理

**文件**：`src/video/streamNode.h`（在 `STREAM_SESSION` 结构体中添加方法）

```cpp
struct STREAM_SESSION {
    // ... 现有字段 ...

    void close() {
        if (rtp_socket != kInvalidSocket) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_socket));
            rtp_socket = kInvalidSocket;
        }
        if (rtcp_socket != kInvalidSocket) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_socket));
            rtcp_socket = kInvalidSocket;
        }
        client_rtp_port = 0;
        client_rtcp_port = 0;
    }
};
```

**效果**：
- `closeUDPSockets()` 删除
- `StreamNode::teardown()` 中改为 `session_origin_pull_.close(); session_relay_push_.close();`

---

## 改动范围总结

| 文件 | 改动 | 行数估计 |
|---|---|---|
| `streamNode.h` | -3 个 webrtc handler 声明，+3 个 `STREAM_SESSION` 方法声明，+1 个 `STREAM_SESSION::close()` | ~15 行 |
| `streamNode_webrtc.cpp` | webrtc handler 改为 `STREAM_SESSION::` 方法，调用点更新 | ~20 行 |
| `streamNode_socket.cpp` | Pull/Push 合并为一个 `createUDPPair(session&)`，删除 `closeUDPSockets` | ~100 行净减 |
| `streamNode_rtsp.cpp` | 调用点适配（`closeUDPSockets()` → `session.close()`） | ~5 行 |

**总计约 140 行改动，零文件新增/删除，零编译配置变更。**

---

## 验证方式

```bash
# Windows
MSBuild.exe msvc\tds.sln /p:Configuration=Debug /p:Platform=x64 /t:Build

# 编译通过即验证通过（零行为变化）
```
