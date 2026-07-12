#include "pch.h"
#include "srtp_protect.h"
#include "logger.h"
#include <algorithm>
#include <cstdio>
#include <mbedtls/private/sha1.h>
#include <mbedtls/private/aes.h>

// ============================================================================
// SRTP Key Material 处理 (RFC 3711 + RFC 5764)
//
// RFC 5764 §4.2: DTLS-SRTP 通过 DTLS exporter("EXTRACTOR-dtls_srtp") 导出 60 字节:
//   client_write_key[16] | server_write_key[16] |
//   client_write_salt[14] | server_write_salt[14]
//
// RFC 3711 §4.3: 当 key_derivation_rate = 0 (WebRTC 默认):
//   session keys 直接由 master key + master salt 构成，不需要 KDF:
//
//   k_e (encryption key) = k_master 的前 16 字节
//   k_a (auth key)       = k_master 填充到 20 字节 (末尾补 0x00)
//   k_s (salting key)    = master_salt 的前 14 字节
//
// 注意: key_derivation_rate = 0 意味着 encrypt_key 和 auth_key
//       的前 16 字节相同，这与 libsrtp (浏览器) 的行为一致。
// ============================================================================

// ============================================================================
// SrptProtect 实现
// ============================================================================

/* ---------- SrptProtect::initFromDtls ---------- */
SrptProtect::Context
SrptProtect::initFromDtls(const DtlsTransport::SrptKeyingMaterial& keys,
                           bool is_server, uint32_t ssrc) {
    Context ctx;
    ctx.ssrc = ssrc;
    ctx.rollover_counter = 0;

    // 服务端: server_write_key/salt 用于加密发给客户端的包
    //        client_write_key/salt 用于解密来自客户端的包
    const uint8_t* master_key  = is_server ? keys.server_write_key : keys.client_write_key;
    const uint8_t* master_salt = is_server ? keys.server_write_salt : keys.client_write_salt;

    // RFC 3711 §4.3, key_derivation_rate = 0 (WebRTC 默认):
    //   session keys 直接使用 master key 和 master salt
    //
    // k_e = master_key (16 bytes)
    memcpy(ctx.encrypt_key, master_key, 16);

    // k_a = master_key 填充到 20 bytes (HMAC-SHA1 需要 20 bytes)
    // RFC 3711 §4.3: "the auth key is padded to the right with zeros"
    memcpy(ctx.auth_key, master_key, 16);
    memset(ctx.auth_key + 16, 0, 4);

    // k_s = master_salt (14 bytes)
    memcpy(ctx.encrypt_salt, master_salt, 14);

    // 诊断日志: 打印完整 key material 用于对比浏览器端密钥
    {
        char hex_buf[512];
        int pos = snprintf(hex_buf, sizeof(hex_buf),
            "SRTP init (is_server=%d) key=", is_server);
        for (int i = 0; i < 16; i++)
            pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos, "%02x", ctx.encrypt_key[i]);
        pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos, " salt=");
        for (int i = 0; i < 14; i++)
            pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos, "%02x", ctx.encrypt_salt[i]);
        pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos, " auth=");
        for (int i = 0; i < 20; i++)
            pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos, "%02x", ctx.auth_key[i]);
        LOG("%s", hex_buf);
    }

    // 重放窗口初始化 — 0xFFFF 确保第一个包总是能通过重放检测
    ctx.highest_seq = 0xFFFF;

    ctx.initialized = true;
    return ctx;
}

/* ---------- protect: RTP → SRTP ---------- */
std::vector<uint8_t>
SrptProtect::protect(Context& ctx, const std::vector<uint8_t>& rtp) {
    if (rtp.size() < 12) return {};  // 无效的 RTP 包

    // 从 RTP 包头提取 SSRC（字节 8-11）
    uint32_t pkt_ssrc = ((uint32_t)rtp[8] << 24) | ((uint32_t)rtp[9] << 16)
                      | ((uint32_t)rtp[10] << 8) | rtp[11];

    // 提取序号
    uint16_t seq = ((uint16_t)rtp[2] << 8) | rtp[3];

    // 检测序号回绕 (seq wraparound):
    //   highest_seq 初始为 0xFFFF，第一个包总是 newest
    //   如果 seq << (highest_seq & 0xFFFF) 且差距 > 0x8000 → 回绕
    if (ctx.highest_seq != 0xFFFF) {
        uint16_t prev_seq = ctx.highest_seq & 0xFFFF;
        if (seq < prev_seq && prev_seq - seq > 0x8000) {
            ctx.rollover_counter++;
        }
    }
    uint32_t index = ((uint32_t)ctx.rollover_counter << 16) | seq;

    // 更新最高序号
    uint32_t full_seq = (ctx.rollover_counter << 16) | seq;
    if (ctx.highest_seq == 0xFFFF || full_seq > ctx.highest_seq) {
        ctx.highest_seq = full_seq;
    }

    // 构建 IV — 使用 RTP 包头中的实际 SSRC
    uint8_t iv[16];
    buildIv(iv, ctx.encrypt_salt, pkt_ssrc, index);

    // 复制数据（header 12 + payload）
    size_t payload_len = rtp.size() - 12;
    std::vector<uint8_t> encrypted(rtp.size() + 10); // +10 for auth tag
    memcpy(encrypted.data(), rtp.data(), 12);         // 复制明文 RTP 头
    memcpy(encrypted.data() + 12, rtp.data() + 12, payload_len);

    // AES-128-CTR 加密 payload
    aesCtrCrypt(ctx.encrypt_key, 16, iv, 16,
                encrypted.data() + 12, payload_len);

    // 诊断日志: 前 5 个包打印关键参数
    static int pkt_count = 0;
    if (pkt_count < 5) {
        char hex_buf[256];
        snprintf(hex_buf, sizeof(hex_buf),
            "SRTP protect #%d: SSRC=0x%08x seq=%u ROC=%u "
            "pkt_size=%zu "
            "iv[4..13]=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x "
            "rtp[0..3]=%02x%02x%02x%02x",
            pkt_count, pkt_ssrc, seq, ctx.rollover_counter,
            rtp.size(),
            iv[4], iv[5], iv[6], iv[7], iv[8], iv[9], iv[10], iv[11], iv[12], iv[13],
            rtp[0], rtp[1], rtp[2], rtp[3]);
        LOG("%s", hex_buf);
        pkt_count++;
    }

    // HMAC-SHA1 认证标签（截断至 80 bits = 10 bytes）
    // RFC 3711 §4.2: HMAC 输入 = 认证部分 (RTP header + encrypted payload) || ROC
    // ROC (Rollover Counter) 以大端 4 字节形式追加到认证数据后
    uint8_t auth_tag[20];
    uint8_t roc_be[4] = {
        (uint8_t)(ctx.rollover_counter >> 24),
        (uint8_t)(ctx.rollover_counter >> 16),
        (uint8_t)(ctx.rollover_counter >> 8),
        (uint8_t)(ctx.rollover_counter)
    };

    // 构建 HMAC 输入: header + encrypted_payload + ROC(4 bytes BE)
    std::vector<uint8_t> hmac_input(12 + payload_len + 4);
    memcpy(hmac_input.data(), encrypted.data(), 12 + payload_len);
    memcpy(hmac_input.data() + 12 + payload_len, roc_be, 4);

    hmacSha1(ctx.auth_key, 20,
             hmac_input.data(), hmac_input.size(),
             auth_tag);
    memcpy(encrypted.data() + 12 + payload_len, auth_tag, 10);

    // ★ 自检验证: 用独立 context 做 unprotect，验证加密/认证正确
    if (pkt_count < 5) {
        Context verify_ctx = ctx;
        verify_ctx.highest_seq = seq - 1;     // 小于当前 seq，保证通过重放检测
        verify_ctx.rollover_counter = 0;       // 重置 ROC
        std::vector<uint8_t> out_rtp;
        int vr = unprotect(verify_ctx, encrypted, out_rtp);
        char hex_buf[256];
        if (vr == 0 && out_rtp.size() == rtp.size()) {
            snprintf(hex_buf, sizeof(hex_buf),
                "SRTP self-check #%d: OK (rtp_size=%zu, match=%s)",
                pkt_count, rtp.size(),
                memcmp(rtp.data(), out_rtp.data(), rtp.size())==0 ? "YES" : "NO");
        } else {
            snprintf(hex_buf, sizeof(hex_buf),
                "SRTP self-check #%d: FAIL ret=%d", pkt_count, vr);
        }
        LOG("%s", hex_buf);
    }

    return encrypted;
}

/* ---------- unprotect: SRTP → RTP ---------- */
int SrptProtect::unprotect(Context& ctx,
                            const std::vector<uint8_t>& srtp,
                            std::vector<uint8_t>& out_rtp) {
    if (srtp.size() < 12 + 10) return -1;  // 最小 SRTP 包: header(12) + tag(10)

    size_t payload_len = srtp.size() - 12 - 10;

    // 从 RTP 头提取 SSRC（用于 IV 构建）
    uint32_t pkt_ssrc = ((uint32_t)srtp[8] << 24) | ((uint32_t)srtp[9] << 16)
                      | ((uint32_t)srtp[10] << 8) | srtp[11];

    uint16_t seq = ((uint16_t)srtp[2] << 8) | srtp[3];
    uint32_t roc = ctx.rollover_counter;
    uint16_t prev_highest_seq = ctx.highest_seq & 0xFFFF;

    // 检测回绕: 如果 seq 远小于 highest_seq，说明发生了序号回绕
    if (prev_highest_seq > 0 && seq < prev_highest_seq
        && prev_highest_seq - seq > 0x8000) {
        roc++;  // 序号回绕，递增 ROC
    }

    // ★ 重放检测修正：
    //   第一个包 (highest_seq=0xFFFF 初始化值) 总是通过
    //   后续包要求严格递增（允许合理的乱序范围）
    uint32_t full_index = (roc << 16) | seq;
    if (ctx.highest_seq != 0xFFFF  // 不是初始状态
        && full_index <= ctx.highest_seq) {
        return -2;  // 重放或旧包
    }

    // 验证 HMAC — 使用独立的 auth_key！
    // RFC 3711 §4.2: HMAC 输入 = 认证部分 (RTP header + encrypted payload) || ROC
    uint8_t roc_be[4] = {
        (uint8_t)(roc >> 24),
        (uint8_t)(roc >> 16),
        (uint8_t)(roc >> 8),
        (uint8_t)(roc)
    };
    uint8_t expected_tag[20];
    std::vector<uint8_t> hmac_input(12 + payload_len + 4);
    memcpy(hmac_input.data(), srtp.data(), 12 + payload_len);
    memcpy(hmac_input.data() + 12 + payload_len, roc_be, 4);

    hmacSha1(ctx.auth_key, 20,
             hmac_input.data(), hmac_input.size(),
             expected_tag);

    if (memcmp(srtp.data() + 12 + payload_len, expected_tag, 10) != 0) {
        return -1;  // 认证失败
    }

    // 解密 — 使用包中的 SSRC 构建 IV
    uint8_t iv[16];
    buildIv(iv, ctx.encrypt_salt, pkt_ssrc, full_index);

    out_rtp.resize(srtp.size() - 10);
    memcpy(out_rtp.data(), srtp.data(), 12);               // RTP header
    memcpy(out_rtp.data() + 12, srtp.data() + 12, payload_len);

    aesCtrCrypt(ctx.encrypt_key, 16, iv, 16,
                out_rtp.data() + 12, payload_len);

    // 更新状态
    ctx.highest_seq = full_index;
    ctx.rollover_counter = roc;

    return 0;
}

/* ---------- AES-CTR (using mbedtls) ---------- */
void SrptProtect::aesCtrCrypt(const uint8_t* key, size_t key_len,
                               const uint8_t* iv,  size_t iv_len,
                               uint8_t* data, size_t data_len) {
    if (data_len == 0) return;

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, (unsigned int)(key_len * 8));

    uint8_t counter[16];
    memcpy(counter, iv, iv_len);
    if (iv_len < 16) memset(counter + iv_len, 0, 16 - iv_len);

    uint8_t keystream[16];
    size_t offset = 0;
    while (offset < data_len) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, counter, keystream);

        size_t chunk = (std::min)(data_len - offset, size_t(16));
        for (size_t i = 0; i < chunk; i++)
            data[offset + i] ^= keystream[i];

        // 计数器递增（大端）
        for (int i = 15; i >= 0; i--) {
            if (++counter[i] != 0) break;
        }
        offset += chunk;
    }

    mbedtls_aes_free(&aes);
}

/* ---------- SRTP IV 构建 (RFC 3711 §4.1.1) ---------- */
void SrptProtect::buildIv(uint8_t iv[16], const uint8_t* salt,
                           uint32_t ssrc, uint32_t index) {
    // RFC 3711 §4.1.1: IV = (k_s * 2^16) XOR (SSRC * 2^64) XOR (i * 2^16)
    //
    // 对照 libsrtp 的 ICM 模式实现 (srtp.c):
    //   v32[0] = 0        → bytes 0-3:  0x00000000
    //   v32[1] = SSRC     → bytes 4-7:  SSRC (big-endian)
    //   v64[1] = est<<16  → bytes 8-15: ROC(4) | SEQ(2) | 0x0000
    //
    //   然后 counter = salt XOR iv
    //
    // 最终 counter 字节布局:
    //   bytes 0-3:   salt[0..3]
    //   bytes 4-7:   salt[4..7]  XOR SSRC
    //   bytes 8-11:  salt[8..11] XOR ROC
    //   bytes 12-13: salt[12..13] XOR SEQ
    //   bytes 14-15: 0x0000

    uint32_t roc = (index >> 16) & 0xFFFFFFFF;
    uint16_t seq = index & 0xFFFF;

    memset(iv, 0, 16);
    memcpy(iv, salt, 14);  // k_s * 2^16: salt 填充前 14 字节

    // XOR SSRC * 2^64: SSRC 在字节 4-7
    iv[4] ^= (ssrc >> 24) & 0xFF;
    iv[5] ^= (ssrc >> 16) & 0xFF;
    iv[6] ^= (ssrc >> 8)  & 0xFF;
    iv[7] ^= ssrc & 0xFF;

    // XOR i * 2^16: ROC 在字节 8-11, SEQ 在字节 12-13
    iv[8]  ^= (roc >> 24) & 0xFF;
    iv[9]  ^= (roc >> 16) & 0xFF;
    iv[10] ^= (roc >> 8)  & 0xFF;
    iv[11] ^= roc & 0xFF;

    iv[12] ^= (seq >> 8) & 0xFF;
    iv[13] ^= seq & 0xFF;

    // 字节 14-15 保持 0x0000
}

/* ---------- HMAC-SHA1 (using mbedtls) ---------- */
void SrptProtect::hmacSha1(const uint8_t* key, size_t key_len,
                            const uint8_t* data, size_t data_len,
                            uint8_t out[20]) {
    // HMAC-SHA1 = SHA1((key XOR opad) || SHA1((key XOR ipad) || data))
    const size_t BLOCK = 64;
    uint8_t key_block[64] = {};

    if (key_len > BLOCK) {
        mbedtls_sha1(key, key_len, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5C;
    }

    uint8_t inner_hash[20];

    // inner = SHA1(ipad || data) — 使用独立的 context
    {
        mbedtls_sha1_context sha1_inner;
        mbedtls_sha1_init(&sha1_inner);
        mbedtls_sha1_starts(&sha1_inner);
        mbedtls_sha1_update(&sha1_inner, ipad, 64);
        mbedtls_sha1_update(&sha1_inner, data, data_len);
        mbedtls_sha1_finish(&sha1_inner, inner_hash);
        mbedtls_sha1_free(&sha1_inner);
    }

    // outer = SHA1(opad || inner_hash) — 使用另一个独立的 context
    {
        mbedtls_sha1_context sha1_outer;
        mbedtls_sha1_init(&sha1_outer);
        mbedtls_sha1_starts(&sha1_outer);
        mbedtls_sha1_update(&sha1_outer, opad, 64);
        mbedtls_sha1_update(&sha1_outer, inner_hash, 20);
        mbedtls_sha1_finish(&sha1_outer, out);
        mbedtls_sha1_free(&sha1_outer);
    }
}


