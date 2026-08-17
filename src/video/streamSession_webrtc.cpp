// ============================================================================
// streamSession_webrtc.cpp - WebRTC 协议处理（与 StreamNode 解耦）
// 包含：STUN / DTLS / SRTCP 处理 + SDP Answer 构建
// ============================================================================

#include "streamSession_webrtc.h"
#include "streamNode.h"
#include "streamServer.h"
#include "streamSession.h"

#include <psa/crypto.h>
#include <mbedtls/error.h>
#include <logger.h>

#include <sstream>
#include <random>
#include <cstring>
#include <cstdio>

// ============================================================================
// WebRTC SRTCP 反馈处理：解密浏览器回传的 RTCP compound，
// 识别 PLI/FIR（请求关键帧）与 NACK（按序号重传缓存的 SRTP 包，修复花屏）
// ============================================================================

void sessionHandleSRTCP(std::shared_ptr<STREAM_SESSION> session,
                        SessionDtlsState* dtls_state,
                        uint8_t* buf, int len,
                        struct sockaddr_in& peer) {
    // 解密整个 SRTCP compound（NACK 的丢包序号位于加密段，必须解密后才能读取）
    std::vector<uint8_t> rtcp;
    if (SrptProtect::unprotectRtcp(dtls_state->recv_ctx, buf, (size_t)len, rtcp) != 0) {
        return;  // 认证失败或格式错误
    }

    // 从重传缓存取出已加密的 SRTP 包并原样重发（不触碰 srtp_ctx/local_seq）
    int resent = 0;
    auto retransmit = [&](uint16_t seq) {
        std::lock_guard<std::mutex> lk(dtls_state->rtx_mutex_);
        RtxCachedPacket& slot = dtls_state->rtx_buf_[seq % SessionDtlsState::kRtxBufSize];
        if (slot.valid && slot.seq == seq && !slot.data.empty()) {
            sendto(session->rtp_socket, (const char*)slot.data.data(),
                   (int)slot.data.size(), 0,
                   (const struct sockaddr*)&peer, sizeof(peer));
            resent++;
        }
    };

    // 遍历 compound 中的各 RTCP 包（单包长度 = (length+1)*4 字节）
    size_t off = 0;
    while (off + 4 <= rtcp.size()) {
        uint8_t  fmt  = rtcp[off] & 0x1F;
        uint8_t  pt   = rtcp[off + 1];
        uint16_t words = ((uint16_t)rtcp[off + 2] << 8) | rtcp[off + 3];
        size_t   pkt_len = ((size_t)words + 1) * 4;
        if (off + pkt_len > rtcp.size()) break;

        if (pt == 206 && (fmt == 1 || fmt == 4)) {
            // PSFB: PLI(1)/FIR(4) -> 请求重发关键帧
            // LOG("[ICE] client feedback %s, request keyframe resend",
            //     fmt == 1 ? "PLI" : "FIR");
            dtls_state->request_keyframe_resend_ = true;
        }
        else if (pt == 205 && fmt == 1) {
            // RTPFB Generic NACK: 头部 12B(公共头8B+media SSRC 4B)，FCI 从 off+12 起
            // 每个 FCI = PID(2B) + BLP(2B)：PID 丢失，BLP 第 i 位置1表示 PID+i+1 也丢失
            size_t fci = off + 12;
            while (fci + 4 <= off + pkt_len) {
                uint16_t pid = ((uint16_t)rtcp[fci] << 8) | rtcp[fci + 1];
                uint16_t blp = ((uint16_t)rtcp[fci + 2] << 8) | rtcp[fci + 3];
                retransmit(pid);
                for (int b = 0; b < 16; b++) {
                    if (blp & (1 << b)) retransmit((uint16_t)(pid + b + 1));
                }
                fci += 4;
            }
        }
        off += pkt_len;
    }

    if (resent > 0) {
        LOG("[ICE] NACK: retransmitted %d cached packet(s)", resent);
    }
}


// ============================================================================
// WebRTC STUN 处理：解析 Binding Request 并回复 Binding Success Response
// ============================================================================

void sessionHandleSTUN(std::shared_ptr<STREAM_SESSION> session,
                       uint8_t* buf, int len,
                       struct sockaddr_in& peer,
                       std::chrono::steady_clock::time_point& dtls_start) {
    if (len < 20) return;

    uint16_t msgType = (buf[0] << 8) | buf[1];
    uint32_t magic = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16)
                   | ((uint32_t)buf[6] << 8)  | (uint32_t)buf[7];
    if (magic != 0x2112A442) return;
    if (msgType != 0x0001) return;  // 仅处理 Binding Request

    uint8_t tid[12];
    memcpy(tid, buf + 8, 12);

    // 解析请求中的 USERNAME 属性（RFC 5245 §7.1.2.2 要求响应中回显）
    std::string reqUsername;
    {
        uint16_t msgLength = (buf[2] << 8) | buf[3];
        int attrPos = 20;
        int attrEnd = attrPos + msgLength;
        while (attrPos + 4 <= attrEnd && attrPos + 4 <= len) {
            uint16_t attrType = (buf[attrPos] << 8) | buf[attrPos + 1];
            uint16_t attrLen = (buf[attrPos + 2] << 8) | buf[attrPos + 3];
            int paddedLen = (attrLen + 3) & ~3;  // 4-byte aligned
            if (attrType == 0x0006) {  // USERNAME
                // 长度上限：ufrag+pwd 合法组合最长约 256 字节（RFC 5245），
                // 超长属性会溢出下方 256 字节的响应缓冲区（可被远程利用）
                if (attrLen > 300) return;
                int valLen = attrLen;
                if (attrPos + 4 + valLen <= len) {
                    reqUsername.assign((const char*)(buf + attrPos + 4), valLen);
                }
                break;
            }
            attrPos += 4 + paddedLen;
        }
    }

    // 检查 USE-CANDIDATE (0x0025)，用于诊断 ICE nomination 状态
    bool hasUseCandidate = false;
    {
        uint16_t msgLength = (buf[2] << 8) | buf[3];
        int attrPos = 20;
        int attrEnd = attrPos + msgLength;
        while (attrPos + 4 <= attrEnd && attrPos + 4 <= len) {
            uint16_t attrType = (buf[attrPos] << 8) | buf[attrPos + 1];
            uint16_t attrLen = (buf[attrPos + 2] << 8) | buf[attrPos + 3];
            int paddedLen = (attrLen + 3) & ~3;
            if (attrType == 0x0025) {  // USE-CANDIDATE
                hasUseCandidate = true;
                break;
            }
            attrPos += 4 + paddedLen;
        }
    }

    // LOG("[STUN] Received Binding Request from %s:%d, username=%s, useCandidate=%d",
    //     inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), reqUsername.c_str(), hasUseCandidate);

    // ---- 构造 Binding Success Response（含 MESSAGE-INTEGRITY） ----
    // ICE 要求 Success Response 必须包含 MESSAGE-INTEGRITY 和 USERNAME，
    // 否则浏览器会丢弃响应并持续重试。
    uint8_t response[256] = {};
    int pos = 0;
    // STUN Header (20 bytes)
    response[pos++] = 0x01; response[pos++] = 0x01;  // Binding Success Response
    // Length 占位，后面回填
    int lenPos = pos; pos += 2;
    response[pos++] = 0x21; response[pos++] = 0x12;  // Magic Cookie
    response[pos++] = 0xA4; response[pos++] = 0x42;
    memcpy(response + pos, tid, 12); pos += 12;

    // XOR-MAPPED-ADDRESS (12 bytes)
    response[pos++] = 0x00; response[pos++] = 0x20;  // attr type
    response[pos++] = 0x00; response[pos++] = 0x08;  // attr len = 8
    response[pos++] = 0x00;                          // reserved
    response[pos++] = 0x01;                          // IPv4
    uint16_t xorPort = ntohs(peer.sin_port) ^ 0x2112;
    response[pos++] = (xorPort >> 8) & 0xFF;
    response[pos++] = xorPort & 0xFF;
    uint32_t xorAddr = ntohl(peer.sin_addr.s_addr) ^ 0x2112A442;
    response[pos++] = (xorAddr >> 24) & 0xFF;
    response[pos++] = (xorAddr >> 16) & 0xFF;
    response[pos++] = (xorAddr >> 8)  & 0xFF;
    response[pos++] = xorAddr & 0xFF;

    // USERNAME - 回显请求中的 USERNAME（RFC 5245 §7.1.2.2 MUST）
    // 防御：为 XOR-MAPPED(12) + USERNAME + USE-CANDIDATE(4) + MI(24) + FP(8) 预留空间，
    // 剩余不足则跳过该属性，避免栈缓冲区溢出
    int usernameLen = (int)reqUsername.length();
    if (usernameLen > 0 && (size_t)pos + 4 + ((usernameLen + 3) & ~3) + 28 <= sizeof(response)) {
        int paddedLen = (usernameLen + 3) & ~3;
        response[pos++] = 0x00; response[pos++] = 0x06;  // attr type = USERNAME
        response[pos++] = (usernameLen >> 8) & 0xFF;
        response[pos++] = usernameLen & 0xFF;
        memcpy(response + pos, reqUsername.data(), usernameLen);
        pos += paddedLen;
    }

    // USE-CANDIDATE - 回显请求中的 USE-CANDIDATE（RFC 8445 §8.1.1.2）
    if (hasUseCandidate) {
        response[pos++] = 0x00; response[pos++] = 0x25;  // attr type = USE-CANDIDATE
        response[pos++] = 0x00; response[pos++] = 0x00;  // attr len = 0
    }

    // MESSAGE-INTEGRITY (24 bytes: type 2 + len 2 + hmac 20)
    int miPos = pos;
    response[pos++] = 0x00; response[pos++] = 0x08;  // attr type = 0x0008
    response[pos++] = 0x00; response[pos++] = 0x14;  // attr len = 20
    pos += 20;  // HMAC-SHA1 占位

    // ---- 设置 STUN 头长度（不含 FINGERPRINT，符合 RFC 5389）----
    // HMAC 覆盖范围：header + 所有属性（含 MESSAGE-INTEGRITY 头但不含其值，
    // 且不含 FINGERPRINT），RFC 5389 §15.4
    int attrLen = pos - 20;
    response[lenPos]     = (attrLen >> 8) & 0xFF;
    response[lenPos + 1] = attrLen & 0xFF;
    const std::string& icePwd = session->ice_pwd;
    if (!icePwd.empty()) {
        psa_mac_operation_t macOp = psa_mac_operation_init();
        psa_key_attributes_t keyAttr = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_usage_flags(&keyAttr, PSA_KEY_USAGE_SIGN_MESSAGE);
        psa_set_key_algorithm(&keyAttr, PSA_ALG_HMAC(PSA_ALG_SHA_1));
        psa_set_key_type(&keyAttr, PSA_KEY_TYPE_HMAC);

        psa_key_id_t keyId = PSA_KEY_ID_NULL;
        psa_status_t ps = psa_import_key(&keyAttr,
            (const uint8_t*)icePwd.data(), icePwd.size(), &keyId);
        psa_reset_key_attributes(&keyAttr);

        if (ps == PSA_SUCCESS) {
            ps = psa_mac_sign_setup(&macOp, keyId, PSA_ALG_HMAC(PSA_ALG_SHA_1));
            if (ps == PSA_SUCCESS) {
                psa_mac_update(&macOp, response, miPos);
                size_t macLen = 20;
                psa_status_t ps2 = psa_mac_sign_finish(&macOp, response + miPos + 4, 20, &macLen);
                psa_mac_abort(&macOp);  // 无论 finish 成功与否都释放操作句柄
                if (ps2 != PSA_SUCCESS) {
                    LOG("[STUN] psa_mac_sign_finish failed: %d", (int)ps2);
                } else {
                    // 打印 HMAC 用于调试（日志过多，暂时注释；排查 STUN 时再打开）
                    // char hmacHex[41] = {};
                    // for (int i = 0; i < 20; i++) {
                    //     sprintf(hmacHex + i * 2, "%02x", response[miPos + 4 + i]);
                    // }
                    // LOG("[STUN] MI computed, key='%s', hmac=%s", icePwd.c_str(), hmacHex);
                }
            } else {
                psa_mac_abort(&macOp);  // setup 失败也要释放操作句柄
                LOG("[STUN] psa_mac_sign_setup failed: %d", (int)ps);
            }
            psa_destroy_key(keyId);
        } else {
            LOG("[STUN] psa_import_key failed: %d", (int)ps);
        }
    } else {
        LOG("[STUN] WARNING: ice_pwd is empty, MI not computed");
    }

    // ---- 计算 FINGERPRINT CRC-32 ----
    // 某些浏览器（Chrome）依赖 FINGERPRINT 区分 STUN 与其他协议
    {
        // 计算 CRC-32（覆盖整个 STUN 消息，不含 FINGERPRINT 属性本身）
        int fpPos = pos;
        response[pos++] = 0x80; response[pos++] = 0x28;  // attr type = 0x8028
        response[pos++] = 0x00; response[pos++] = 0x04;  // attr len = 4
        int fpValuePos = pos;  // CRC 值写入位置
        pos += 4;              // CRC 值占位
        // 即：header + 所有属性（不含 FINGERPRINT 的 type/length/value）
        int finalAttrLen = pos - 20;
        response[lenPos]     = (finalAttrLen >> 8) & 0xFF;
        response[lenPos + 1] = finalAttrLen & 0xFF;

        // CRC-32 表
        static const uint32_t crcTable[256] = {
            0x00000000,0x77073096,0xee0e612c,0x990951ba,0x076dc419,0x706af48f,0xe963a535,0x9e6495a3,
            0x0edb8832,0x79dcb8a4,0xe0d5e91e,0x97d2d988,0x09b64c2b,0x7eb17cbd,0xe7b82d07,0x90bf1d91,
            0x1db71064,0x6ab020f2,0xf3b97148,0x84be41de,0x1adad47d,0x6ddde4eb,0xf4d4b551,0x83d385c7,
            0x136c9856,0x646ba8c0,0xfd62f97a,0x8a65c9ec,0x14015c4f,0x63066cd9,0xfa0f3d63,0x8d080df5,
            0x3b6e20c8,0x4c69105e,0xd56041e4,0xa2677172,0x3c03e4d1,0x4b04d447,0xd20d85fd,0xa50ab56b,
            0x35b5a8fa,0x42b2986c,0xdbbbc9d6,0xacbcf940,0x32d86ce3,0x45df5c75,0xdcd60dcf,0xabd13d59,
            0x26d930ac,0x51de003a,0xc8d75180,0xbfd06116,0x21b4f4b5,0x56b3c423,0xcfba9599,0xb8bda50f,
            0x2802b89e,0x5f058808,0xc60cd9b2,0xb10be924,0x2f6f7c87,0x58684c11,0xc1611dab,0xb6662d3d,
            0x76dc4190,0x01db7106,0x98d220bc,0xefd5102a,0x71b18589,0x06b6b51f,0x9fbfe4a5,0xe8b8d433,
            0x7807c9a2,0x0f00f934,0x9609a88e,0xe10e9818,0x7f6a0dbb,0x086d3d2d,0x91646c97,0xe6635c01,
            0x6b6b51f4,0x1c6c6162,0x856530d8,0xf262004e,0x6c0695ed,0x1b01a57b,0x8208f4c1,0xf50fc457,
            0x65b0d9c6,0x12b7e950,0x8bbeb8ea,0xfcb9887c,0x62dd1ddf,0x15da2d49,0x8cd37cf3,0xfbd44c65,
            0x4db26158,0x3ab551ce,0xa3bc0074,0xd4bb30e2,0x4adfa541,0x3dd895d7,0xa4d1c46d,0xd3d6f4fb,
            0x4369e96a,0x346ed9fc,0xad678846,0xda60b8d0,0x44042d73,0x33031de5,0xaa0a4c5f,0xdd0d7cc9,
            0x5005713c,0x270241aa,0xbe0b1010,0xc90c2086,0x5768b525,0x206f85b3,0xb966d409,0xce61e49f,
            0x5edef90e,0x29d9c998,0xb0d09822,0xc7d7a8b4,0x59b33d17,0x2eb40d81,0xb7bd5c3b,0xc0ba6cad,
            0xedb88320,0x9abfb3b6,0x03b6e20c,0x74b1d29a,0xead54739,0x9dd277af,0x04db2615,0x73dc1683,
            0xe3630b12,0x94643b84,0x0d6d6a3e,0x7a6a5aa8,0xe40ecf0b,0x9309ff9d,0x0a00ae27,0x7d079eb1,
            0xf00f9344,0x8708a3d2,0x1e01f268,0x6906c2fe,0xf762575d,0x806567cb,0x196c3671,0x6e6b06e7,
            0xfed41b76,0x89d32be0,0x10da7a5a,0x67dd4acc,0xf9b9df6f,0x8ebeeff9,0x17b7be43,0x60b08ed5,
            0xd6d6a3e8,0xa1d1937e,0x38d8c2c4,0x4fdff252,0xd1bb67f1,0xa6bc5767,0x3fb506dd,0x48b2364b,
            0xd80d2bda,0xaf0a1b4c,0x36034af6,0x41047a60,0xdf60efc3,0xa867df55,0x316e8eef,0x4669be79,
            0xcb61b38c,0xbc66831a,0x256fd2a0,0x5268e236,0xcc0c7795,0xbb0b4703,0x220216b9,0x5505262f,
            0xc5ba3bbe,0xb2bd0b28,0x2bb45a92,0x5cb36a04,0xc2d7ffa7,0xb5d0cf31,0x2cd99e8b,0x5bdeae1d,
            0x9b64c2b0,0xec63f226,0x756aa39c,0x026d930a,0x9c0906a9,0xeb0e363f,0x72076785,0x05005713,
            0x95bf4a82,0xe2b87a14,0x7bb12bae,0x0cb61b38,0x92d28e9b,0xe5d5be0d,0x7cdcefb7,0x0bdbdf21,
            0x86d3d2d4,0xf1d4e242,0x68ddb3f8,0x1fda836e,0x81be16cd,0xf6b9265b,0x6fb077e1,0x18b74777,
            0x88085ae6,0xff0f6a70,0x66063bca,0x11010b5c,0x8f659eff,0xf862ae69,0x616bffd3,0x166ccf45,
            0xa00ae278,0xd70dd2ee,0x4e048354,0x3903b3c2,0xa7672661,0xd06016f7,0x4969474d,0x3e6e77db,
            0xaed16a4a,0xd9d65adc,0x40df0b66,0x37d83bf0,0xa9bcae53,0xdebb9ec5,0x47b2cf7f,0x30b5ffe9,
            0xbdbdf21c,0xcabac28a,0x53b39330,0x24b4a3a6,0xbad03605,0xcdd70693,0x54de5729,0x23d967bf,
            0xb3667a2e,0xc4614ab8,0x5d681b02,0x2a6f2b94,0xb40bbe37,0xc30c8ea1,0x5a05df1b,0x2d02ef8d
        };
        auto crc32 = [&](const uint8_t* data, size_t len) -> uint32_t {
            uint32_t crc = 0xFFFFFFFF;
            for (size_t i = 0; i < len; i++)
                crc = crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
            return ~crc;
        };
        // CRC 覆盖范围：STUN header + 所有属性（不含 FINGERPRINT 本身）
        uint32_t crc = crc32(response, fpPos);
        crc ^= 0x5354554E;  // XOR with "STUN" per RFC 5389
        response[fpValuePos++] = (crc >> 24) & 0xFF;
        response[fpValuePos++] = (crc >> 16) & 0xFF;
        response[fpValuePos++] = (crc >> 8)  & 0xFF;
        response[fpValuePos++] = crc & 0xFF;
    }

    // 打印调试信息（日志过多，暂时注释；排查 STUN 时再打开）
    // {
    //     char dbg[256] = {};
    //     int n = 0;
    //     for (int i = 0; i < pos && n < 200; i++) {
    //         n += sprintf(dbg + n, "%02x", response[i]);
    //     }
    //     LOG("[STUN] Response sent to %s:%d, len=%d, hex=%s",
    //         inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), pos, dbg);
    // }

    int sent = sendto(static_cast<SOCKET_TYPE>(session->rtp_socket),
                    (const char*)response, pos, 0,
                    (struct sockaddr*)&peer, sizeof(peer));
    if (sent < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        LOG("[STUN] sendto FAILED: ret=%d, WSAError=%d, target=%s:%d, len=%d",
            sent, err, inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), pos);
#else
        LOG("[STUN] sendto FAILED: ret=%d, errno=%d, target=%s:%d, len=%d",
            sent, errno, inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), pos);
#endif
    }

    // ICE 连通性确认：收到 Binding Request 并回复 Response
    // 只在初始状态(0)时升级为1，避免 keep-alive Binding Request 把 SRTP 激活(3)降级
    if (session->state_ == SESSION_STATE::SESSION_CONNECTING) {
        session->state_ = SESSION_STATE::SESSION_HANDSHAKING;
        dtls_start = std::chrono::steady_clock::now();  // 开始 DTLS 握手计时
    }

    session->last_stun_bind_req_time = std::chrono::system_clock::now();
}


// ============================================================================
// DTLS 握手处理：喂入数据并推进握手状态机
// ============================================================================

void sessionHandleDTLS(std::shared_ptr<STREAM_SESSION> session,
                       SessionDtlsState* dtls_state,
                       uint8_t* buf, int len,
                       struct sockaddr_in& peer,
                       std::chrono::steady_clock::time_point& dtls_start) {
    if (!dtls_state->dtls_initialized) return;

    // 握手完成后忽略迟到的 DTLS 包：mbedtls 不再消费内部缓冲区，
    // 继续 feedData 会让 recv_buf_ 无限增长（内存 DoS）
    if (dtls_state->dtls.isHandshakeDone()) return;

    // 每次收到 DTLS 数据包，重置握手超时计时器
    dtls_start = std::chrono::steady_clock::now();

    // 更新对端地址（首包时绑定）
    dtls_state->dtls.setSocket(session->rtp_socket, peer);

    // 设置客户端传输标识（IP+Port），DTLS Cookie 需要它来生成 HMAC
    dtls_state->dtls.setClientTransportId(peer);

    // 将主循环 recvfrom 已消费的 DTLS 数据喂入内部缓冲区，
    // 这样 mbedtls 的 bio_recv 才能读到数据并完成握手
    dtls_state->dtls.feedData(buf, len);

    // DTLS 握手是多步骤状态机，需要循环调用 doHandshakeStep()
    // 直到返回 WANT_READ（需要等对端数据）、WANT_WRITE（需要等发送完成）或出错
    int ret;
    while (true) {
        ret = dtls_state->dtls.doHandshakeStep();

        // 握手成功：doHandshakeStep 内部已设置 handshake_done_ 并导出密钥
        if (dtls_state->dtls.isHandshakeDone()) {
            dtls_state->srtp_ready = true;
            session->state_ = SESSION_STATE::SESSION_STREAMING;

            // 初始化 SRTP 上下文（服务端使用 server_write_key）
            const auto& keys = dtls_state->dtls.getKeyingMaterial();
            if (keys.ready) {
                dtls_state->srtp_ctx = SrptProtect::initFromDtls(
                    keys, true, /* is_server */
                    0);         // ssrc 将在发送时设置
                // 接收方向上下文（client_write 主密钥），用于解密浏览器反馈的 SRTCP(NACK/PLI)
                dtls_state->recv_ctx = SrptProtect::initFromDtls(
                    keys, false, /* is_server=false */
                    0);

                session->state_ = SESSION_STATE::SESSION_STREAMING; // SRTP 激活
                LOG("[ICE] DTLS handshake + SRTP keys ready for socket fd="
                    + std::to_string(session->rtp_socket));
            }
            break;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                   ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // 正常等待：需要等对端发数据或等发送缓冲区就绪，退出循环
            break;
        } else if (ret == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
            // DTLS Cookie 验证：等待客户端重发带 Cookie 的 ClientHello
            // 这是正常流程，退出循环等待下一个数据包
            // 重置超时计时器：doHandshakeStep 循环可能耗时较长，
            // 避免在回到主循环顶部时立即触发 8s 超时
            dtls_start = std::chrono::steady_clock::now();
            break;
        } else if (ret == 0) {
            // 中间步骤成功（如 HELLO_REQUEST->CLIENT_HELLO 状态转换），
            // 继续循环推进状态机
            continue;
        } else {
            // 握手失败，重置以便重试
            char errbuf[128];
            mbedtls_strerror(ret, errbuf, sizeof(errbuf));
            LOG("[ICE] DTLS error: %s (0x%04X), will retry", errbuf, -ret);
            // 浏览器可能重新发起握手，不退出循环
            break;
        }
    }
}


// ============================================================================
// WebRTC SDP Answer 构建 - 生成 ICE 凭据、编码 sprop-parameter-sets、组装 SDP
// ============================================================================

// 从浏览器 SDP Offer 中提取指定 PT 对应的编码名（如 H264/H265/HEVC），未找到返回空串
std::string parseCodecNameFromOffer(const std::string& sdpOffer, int pt) {
    std::istringstream ss(sdpOffer);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.find("a=rtpmap:") != std::string::npos) {
            size_t rtpmapPos = line.find("a=rtpmap:");
            size_t ptStart = rtpmapPos + 9;  // strlen("a=rtpmap:")
            size_t ptEnd = line.find(' ', ptStart);
            if (ptEnd == std::string::npos) continue;
            std::string ptStr = line.substr(ptStart, ptEnd - ptStart);
            if (std::stoi(ptStr) != pt) continue;
            // rtpmap 行的编码名位于空格与 '/' 之间，如 "a=rtpmap:98 H265/90000"
            size_t nameStart = ptEnd + 1;
            size_t slash = line.find('/', nameStart);
            if (slash == std::string::npos) continue;
            return line.substr(nameStart, slash - nameStart);
        }
    }
    return "";
}

// 从浏览器 SDP Offer 中解析指定 codec 的 payload type（返回 0 表示未找到）
// H.265 在浏览器中可能命名为 H265 或 HEVC，两者都识别
int parseCodecPTFromOffer(const std::string& sdpOffer, const std::string& codec) {
    int codecPT = 0;
    std::istringstream ss(sdpOffer);
    std::string line;
    while (std::getline(ss, line)) {
        // 匹配 "a=rtpmap:<PT> H264/90000" 或 "a=rtpmap:<PT> H265/90000\r"
        if (line.find("a=rtpmap:") != std::string::npos) {
            size_t rtpmapPos = line.find("a=rtpmap:");
            size_t ptStart = rtpmapPos + 9;  // strlen("a=rtpmap:")
            size_t ptEnd = line.find(' ', ptStart);
            if (ptEnd == std::string::npos) continue;
            std::string ptStr = line.substr(ptStart, ptEnd - ptStart);
            // 检查 codec 是否匹配（H265 兼容 HEVC 命名）
            bool matched = false;
            if (codec == "H265") {
                matched = (line.find("H265") != std::string::npos ||
                           line.find("HEVC") != std::string::npos);
            } else {
                matched = (line.find(codec) != std::string::npos);
            }
            if (matched) {
                codecPT = std::stoi(ptStr);
                break;  // 使用第一个匹配的 PT
            }
        }
    }
    return codecPT;
}

void buildWebRTCSdpAnswer(STREAM_SESSION& si, const std::string& serverIp,
                          const std::string& dtlsFingerprint) {
    // 生成 ICE 凭据（每个会话随机，长度符合 RFC 5245 要求）
    std::string iceUfrag;
    std::string icePwd;
    {
        static const char alphanum[] =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<> dist(0, sizeof(alphanum) - 2);
        for (int i = 0; i < 8; i++) iceUfrag += alphanum[dist(rng)];
        for (int i = 0; i < 22; i++) icePwd += alphanum[dist(rng)];
    }

    // 标记为 WebRTC 会话并写入 ICE 凭据
    si.is_webrtc = true;
    si.ice_ufrag = iceUfrag;
    si.ice_pwd = icePwd;

    std::ostringstream sdp;
    sdp << "v=0\r\n";
    sdp << "o=- 0 0 IN IP4 " << serverIp << "\r\n";
    sdp << "s=TDS\r\n";
    sdp << "t=0 0\r\n";
    sdp << "a=group:BUNDLE 0\r\n";                     // 匹配浏览器 Offer 的 BUNDLE 标签
    sdp << "a=msid-semantic: WMS\r\n";                 // WebRTC 必须：媒体流标识语义
    sdp << "m=video " << si.server_rtp_port
        << " UDP/TLS/RTP/SAVPF " << si.payload_type << "\r\n";
    sdp << "c=IN IP4 " << serverIp << "\r\n";
    sdp << "a=mid:0\r\n";                             // 媒体流标识（匹配浏览器 Offer）
    sdp << "a=msid:TDS-stream TDS-video\r\n";          // 映射到浏览器 MediaStream
    sdp << "a=rtpmap:" << si.payload_type
        << " " << si.codec << "/" << si.clock_rate << "\r\n";

    // 构造 fmtp 行：如果已有 fmtp 则在其后追加参数集，
    // 否则从参数集构造完整 fmtp
    std::string fmtpLine;
    if (!si.fmtp.empty()) {
        fmtpLine = si.fmtp;
    }

    bool isH265 = (si.codec == "H265" || si.codec == "HEVC");
    if (isH265) {
        // H.265 fmtp（RFC 7798）：sprop-vps/sprop-sps/sprop-pps 以分号分隔
        // 源 fmtp 可能已含这些参数，逐项检测避免重复
        std::string h265ParamSets;
        if (!si.vps.empty() && fmtpLine.find("sprop-vps=") == std::string::npos) {
            std::string vps_raw(reinterpret_cast<const char*>(si.vps.data()), si.vps.size());
            h265ParamSets += "sprop-vps=" + base64Encode(vps_raw);
        }
        if (!si.sps.empty() && fmtpLine.find("sprop-sps=") == std::string::npos) {
            if (!h265ParamSets.empty()) h265ParamSets += ";";
            std::string sps_raw(reinterpret_cast<const char*>(si.sps.data()), si.sps.size());
            h265ParamSets += "sprop-sps=" + base64Encode(sps_raw);
        }
        if (!si.pps.empty() && fmtpLine.find("sprop-pps=") == std::string::npos) {
            if (!h265ParamSets.empty()) h265ParamSets += ";";
            std::string pps_raw(reinterpret_cast<const char*>(si.pps.data()), si.pps.size());
            h265ParamSets += "sprop-pps=" + base64Encode(pps_raw);
        }
        if (!h265ParamSets.empty()) {
            if (!fmtpLine.empty() && fmtpLine.back() != ';') fmtpLine += ";";
            fmtpLine += h265ParamSets;
        }
    } else {
        // H.264：将 SPS/PPS 编码为 Base64（用于 SDP sprop-parameter-sets）
        // 格式: <sps_base64>,<pps_base64>
        std::string spropParamSets;
        if (!si.sps.empty() && !si.pps.empty()) {
            std::string sps_raw(reinterpret_cast<const char*>(si.sps.data()), si.sps.size());
            std::string pps_raw(reinterpret_cast<const char*>(si.pps.data()), si.pps.size());
            std::string sps_b64 = base64Encode(sps_raw);
            std::string pps_b64 = base64Encode(pps_raw);
            spropParamSets = sps_b64 + "," + pps_b64;
        }
        if (!spropParamSets.empty()) {
            // 如果原有 fmtp 已有 sprop-parameter-sets，则不重复添加
            if (fmtpLine.find("sprop-parameter-sets") == std::string::npos) {
                if (!fmtpLine.empty()) fmtpLine += ";";
                fmtpLine += "sprop-parameter-sets=" + spropParamSets;
            }
        }
        // 确保 packetization-mode 存在（RFC 6184 必需，默认 mode=1 支持 FU-A/STAP-A）
        if (!fmtpLine.empty() && fmtpLine.find("packetization-mode") == std::string::npos) {
            fmtpLine = "packetization-mode=1;" + fmtpLine;
        }
        // 确保 profile-level-id 存在
        if (!fmtpLine.empty() && fmtpLine.find("profile-level-id") == std::string::npos) {
            if (si.sps.size() >= 4 && (si.sps[0] & 0x1F) == NAL_TYPE_SPS) {
                // sps 包含完整 NAL 单元，profile-level-id 取自 SPS RBSP 第 1-3 字节
                char buf[16];
                snprintf(buf, sizeof(buf), "profile-level-id=%02X%02X%02X",
                    si.sps[1], si.sps[2], si.sps[3]);
                fmtpLine = std::string(buf) + ";" + fmtpLine;
            } else {
                fmtpLine = "profile-level-id=42C01F;" + fmtpLine;
            }
        }
        // 确保 level-asymmetry-allowed 存在
        if (!fmtpLine.empty() && fmtpLine.find("level-asymmetry-allowed") == std::string::npos) {
            fmtpLine += ";level-asymmetry-allowed=1";
        }
    }
    if (!fmtpLine.empty()) {
        sdp << "a=fmtp:" << si.payload_type << " " << fmtpLine << "\r\n";
    }

    // 声明支持 RTCP 反馈：否则浏览器不会发送 PLI/FIR，解码卡顿时无法主动请求关键帧
    sdp << "a=rtcp-fb:" << si.payload_type << " nack\r\n";
    sdp << "a=rtcp-fb:" << si.payload_type << " nack pli\r\n";
    sdp << "a=rtcp-fb:" << si.payload_type << " ccm fir\r\n";

    sdp << "a=rtcp-mux\r\n";                           // RTCP 复用 RTP 端口
    sdp << "a=rtcp-rsize\r\n";                         // 精简 RTCP
    sdp << "a=sendonly\r\n";                            // 服务端仅发送视频
    sdp << "a=setup:passive\r\n";                       // DTLS server
    sdp << "a=ice-options:trickle\r\n";                  // 支持增量 ICE
    sdp << "a=ice-lite\r\n";                            // ICE-Lite 模式
    sdp << "a=ice-ufrag:" << iceUfrag << "\r\n";
    sdp << "a=ice-pwd:" << icePwd << "\r\n";
    sdp << "a=fingerprint:sha-256 " << dtlsFingerprint << "\r\n";
    // SSRC 声明：使用实际流中的 SSRC（如果尚未捕获则用 1 作为占位符）
    // 必须包含 msid 属性，浏览器才能将 SSRC 绑定到 <video> 元素并触发 ontrack
    uint32_t declaredSsrc = si.video_ssrc ? si.video_ssrc : 1;
    sdp << "a=ssrc:" << declaredSsrc << " msid:TDS-stream TDS-video\r\n";
    sdp << "a=ssrc:" << declaredSsrc << " cname:TDS\r\n";
    sdp << "a=candidate:1 1 UDP 2130706431 "
        << serverIp << " " << si.server_rtp_port << " typ host\r\n";
    sdp << "a=end-of-candidates\r\n";                    // 无更多候选，触发 ICE 完成

    si.sdp = sdp.str();
}
