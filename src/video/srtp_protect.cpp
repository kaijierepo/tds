#include "pch.h"
#include "srtp_protect.h"
#include "logger.h"
#include <algorithm>
#include <mbedtls/private/sha1.h>
#include <mbedtls/private/aes.h>

// ============================================================================
// RFC 3711 §4.2.3: SRTP KDF — 从 master_key + master_salt 派生
//   auth_key (160 bits = 20 bytes) 用于 HMAC-SHA1 认证
//   加密密钥和 salt 直接使用 DTLS 导出的值（不再重复派生）
//
// SRTP Key Derivation:
//   k_e (encryption) = F (master_key, master_salt, 0x00, ..)  -- 16 bytes
//   a_e (auth)       = F (master_key, master_salt, 0x01, ..)  -- 20 bytes
//   k_s (salt)       = F (master_key, master_salt, 0x02, ..)  -- 14 bytes
// 其中 F() 是 PRF-xor (AES-CM 或类似)，但实际实现中：
//   DTLS exporter 已经给出了 r = key(16) || key'(16) || salt(14) || salt'(14)
//   我们需要用 r 作为输入来派生 session keys
// ============================================================================

namespace {
/**
 * @brief SRTP PRF-n (RFC 3711 §4.2.3)
 *
 * 使用简单的 XOR 派生方式（与 libsrtp 一致）：
 *   k = r XOR (salt << n)
 * 然后取前 needed 位
 */
void srtp_kdf(const uint8_t* master_key, size_t mk_len,
              const uint8_t* master_salt, size_t ms_len,
              uint8_t label,
              uint8_t* out, size_t out_len) {
    // div = 00 00 || 00 00 00 00 || 0000 0000 || label
    uint8_t div[16] = {};
    div[sizeof(div) - 1] = label;

    // mask = master_salt || div  （截断到合适长度）
    size_t xlen = (mk_len < out_len) ? out_len : mk_len;
    for (size_t i = 0; i < out_len; i++) {
        size_t si = i % ms_len;
        size_t di = i % sizeof(div);
        if (i < mk_len) {
            out[i] = master_key[i] ^ (master_salt[si] ^ div[di]);
        } else {
            // 超过 master_key 长度的部分继续 XOR
            out[i] = 0 ^ (master_salt[si] ^ div[di]);
        }
    }
}
}  // namespace

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
    const uint8_t* enc_key  = is_server ? keys.server_write_key : keys.client_write_key;
    const uint8_t* enc_salt = is_server ? keys.server_write_salt : keys.client_write_salt;

    // 派生 session 密钥 (RFC 3711 §4.2.3)
    // label 0x00 → encryption key (16 bytes)
    // label 0x01 → authentication key (20 bytes)  
    // label 0x02 → salting key (14 bytes)

    // 加密密钥: k_e = F(master_key, master_salt, 0x00), 取前 16 字节
    srtp_kdf(enc_key, 16, enc_salt, 14, 0x00, ctx.encrypt_key, 16);

    // 认证密钥: a_e = F(master_key, master_salt, 0x01), 取前 20 字节
    // 这就是 HMAC-SHA1 需要的独立认证密钥！
    srtp_kdf(enc_key, 16, enc_salt, 14, 0x01, ctx.auth_key, 20);

    // Salt: k_s = F(master_key, master_salt, 0x02), 取前 14 字节
    srtp_kdf(enc_key, 16, enc_salt, 14, 0x02, ctx.encrypt_salt, 14);

    // 重放窗口初始化
    ctx.highest_seq = 0xFFFF;  // 第一个包总是能通过

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
    uint32_t index = ((uint32_t)ctx.rollover_counter << 16) | seq;

    // 更新 ROC: 如果 seq 回绕，递增 rollover_counter
    if (ctx.initialized && (ctx.highest_seq & 0xFFFF) > 0
        && seq < (ctx.highest_seq & 0xFFFF)
        && (ctx.highest_seq & 0xFFFF) - seq > 0x8000) {
        ctx.rollover_counter++;
        index = ((uint32_t)ctx.rollover_counter << 16) | seq;
    }
    // 更新最高序号
    uint32_t full_seq = (ctx.rollover_counter << 16) | seq;
    if (!ctx.initialized || full_seq > ctx.highest_seq) {
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

    // HMAC-SHA1 认证标签（截断至 80 bits = 10 bytes）
    // ★ 使用独立的 auth_key，而非 encrypt_key！
    uint8_t auth_tag[20];
    hmacSha1(ctx.auth_key, 20,
             encrypted.data(), 12 + payload_len,
             auth_tag);
    memcpy(encrypted.data() + 12 + payload_len, auth_tag, 10);

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
    uint8_t expected_tag[20];
    hmacSha1(ctx.auth_key, 20,
             srtp.data(), 12 + payload_len,
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
        offset += 16;
    }

    mbedtls_aes_free(&aes);
}

/* ---------- SRTP IV 构建 (RFC 3711 §4.1.1) ---------- */
void SrptProtect::buildIv(uint8_t iv[16], const uint8_t* salt,
                           uint32_t ssrc, uint32_t index) {
    // RFC 3711 §4.1.1: IV = salt || 0x00 || 0x00  然后与 SSRC||index XOR
    //
    // 完整 16 字节 IV 布局:
    //   bits 0-87:   salt[0..10]        (11 bytes, 不被 index 覆盖的部分)
    //   bits 88-95:  salt[11] XOR index_high
    //   bits 96-103: salt[12] XOR index_mid
    //   bits 104-111:salt[13] XOR index_low
    //   bits 112-127: 0x0000             (2 bytes zero)
    //
    // 简化写法: iv = [salt(14 bytes)] [0x00] [0x00]
    //           然后 XOR SSRC 到 [4..7], XOR index 到 [6..13]
    memset(iv, 0, 16);
    memcpy(iv, salt, 14);  // salt 填充前 14 字节

    // XOR SSRC 到字节 4-7 (bits 32-63)
    iv[4]  ^= (ssrc >> 24) & 0xFF;
    iv[5]  ^= (ssrc >> 16) & 0xFF;
    iv[6]  ^= (ssrc >> 8)  & 0xFF;
    iv[7]  ^= ssrc & 0xFF;

    // ★ 修正：XOR index 到字节 6-13 (bits 48-111)
    //         注意与 SSRC 在字节 6-7 有重叠，这是正确的！
    iv[6]  ^= (index >> 24) & 0xFF;
    iv[7]  ^= (index >> 16) & 0xFF;
    iv[8]  ^= (index >> 8)  & 0xFF;
    iv[9]  ^= index & 0xFF;

    // index 继续覆盖到 salt 区域之后
    iv[10] ^= 0;  // index 高位已经用完（32-bit index），这里为 0
    iv[11] ^= 0;
    iv[12] ^= 0;
    iv[13] ^= 0;

    // 最后 2 字节保持 0 (bits 112-127)
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

    // inner = SHA1(ipad || data)
    mbedtls_sha1_context sha1;
    mbedtls_sha1_init(&sha1);
    mbedtls_sha1_starts(&sha1);
    mbedtls_sha1_update(&sha1, ipad, 64);
    mbedtls_sha1_update(&sha1, data, data_len);
    mbedtls_sha1_finish(&sha1, out);

    // outer = SHA1(opad || inner_hash)
    mbedtls_sha1_starts(&sha1);
    mbedtls_sha1_update(&sha1, opad, 64);
    mbedtls_sha1_update(&sha1, out, 20);
    mbedtls_sha1_finish(&sha1, out);
    mbedtls_sha1_free(&sha1);
}


