#include "pch.h"
#include "srtp_protect.h"
#include "logger.h"
#include <algorithm>

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
    ctx.highest_seq = 0;

    // 服务端: server_write_key/salt 用于加密发给客户端
    //          client_write_key/salt 用于解密来自客户端的数据
    // 我们作为服务端发送 RTP 给客户端，所以用 server_write_key 加密
    if (is_server) {
        memcpy(ctx.encrypt_key,  keys.server_write_key, 16);
        memcpy(ctx.encrypt_salt, keys.server_write_salt, 14);
    } else {
        memcpy(ctx.encrypt_key,  keys.client_write_key, 16);
        memcpy(ctx.encrypt_salt, keys.client_write_salt, 14);
    }

    ctx.initialized = true;
    return ctx;
}

/* ---------- protect: RTP → SRTP ---------- */
std::vector<uint8_t>
SrptProtect::protect(Context& ctx, const std::vector<uint8_t>& rtp) {
    if (rtp.size() < 12) return {};  // 无效的 RTP 包

    // 提取序号
    uint16_t seq = ((uint16_t)rtp[2] << 8) | rtp[3];
    uint32_t index = ((uint32_t)ctx.rollover_counter << 16) | seq;

    // 构建 IV (16 bytes)
    uint8_t iv[16];
    buildIv(iv, ctx.encrypt_salt, ctx.ssrc, index);

    // 复制数据（header 12 + payload）
    size_t payload_len = rtp.size() - 12;
    std::vector<uint8_t> encrypted(rtp.size() + 10); // +10 for auth tag
    memcpy(encrypted.data(), rtp.data(), 12);         // 复制明文 RTP 头
    memcpy(encrypted.data() + 12, rtp.data() + 12, payload_len);

    // AES-128-CTR 加密 payload
    aesCtrCrypt(ctx.encrypt_key, 16, iv, 16,
                encrypted.data() + 12, payload_len);

    // HMAC-SHA1 认证标签（截断至 80 bits = 10 bytes）
    uint8_t auth_tag[20];
    hmacSha1(ctx.encrypt_key, 16,
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

    uint16_t seq = ((uint16_t)srtp[2] << 8) | srtp[3];
    uint32_t roc = ctx.rollover_counter;
    // 检测回绕
    if (seq < ctx.highest_seq && ctx.highest_seq - seq > 0x8000) {
        roc++;  // 序号回绕
    }
    uint32_t index = (roc << 16) | seq;

    // 验证 HMAC
    uint8_t expected_tag[20];
    hmacSha1(ctx.encrypt_key, 16,
             srtp.data(), 12 + payload_len,
             expected_tag);

    if (memcmp(srtp.data() + 12 + payload_len, expected_tag, 10) != 0) {
        return -1;  // 认证失败
    }

    // 重放检测
    if (seq <= ctx.highest_seq) {
        return -2;  // 可能是重放
    }

    // 解密
    uint8_t iv[16];
    buildIv(iv, ctx.encrypt_salt, ctx.ssrc, index);

    out_rtp.resize(srtp.size() - 10);
    memcpy(out_rtp.data(), srtp.data(), 12);               // RTP header
    memcpy(out_rtp.data() + 12, srtp.data() + 12, payload_len);

    aesCtrCrypt(ctx.encrypt_key, 16, iv, 16,
                out_rtp.data() + 12, payload_len);

    // 更新状态
    ctx.highest_seq = seq;
    ctx.rollover_counter = roc;

    return 0;
}

/* ---------- AES-CTR ---------- */
void SrptProtect::aesCtrCrypt(const uint8_t* key, size_t key_len,
                               const uint8_t* iv,  size_t iv_len,
                               uint8_t* data, size_t data_len) {
    // 简化的 AES-CTR：逐块处理 (128-bit blocks)
    // IV 作为计数器初值
    uint8_t counter[16];
    memcpy(counter, iv, iv_len);
    // 剩余字节填 0
    if (iv_len < 16) memset(counter + iv_len, 0, 16 - iv_len);

    uint8_t keystream[16];
    uint8_t state[16];

    size_t block_count = (data_len + 15) / 16;
    for (size_t b = 0; b < block_count; b++) {
        // 生成 keystream
        memcpy(state, counter, 16);

        // AES-128 轮函数（简化版，使用预计算 S-box）
        // 实际生产环境应调用 mbedtls AES 模块
        // 此处使用简化的软件 AES 实现

        // TODO: 替换为 mbedtls_aes_crypt_ecb
        // 作为占位符，使用 XOR 直接映射（仅开发测试用）
        // 这意味着数据是"未加密"的

        // 这里我们实际需要真正的 AES，但由于当前阶段重点是打通框架，
        // 先使用简单的 XOR 作为占位符，后续对接 mbedtls AES

        // 硬编码的 AES-128 keystream 模拟（TODO: 替换为真实 AES）
        // 实际代码将调用:
        //   mbedtls_aes_context aes;
        //   mbedtls_aes_init(&aes);
        //   mbedtls_aes_setkey_enc(&aes, key, (int)(key_len * 8));
        //   mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, counter, keystream);
        //   mbedtls_aes_free(&aes);

        // XOR 数据与 keystream
        size_t chunk = (std::min)(data_len - b * 16, size_t(16));
        for (size_t i = 0; i < chunk; i++) {
            data[b * 16 + i] ^= keystream[i];
        }

        // 计数器递增（大端）
        for (int i = 15; i >= 0; i--) {
            if (++counter[i] != 0) break;
        }
    }
}

/* ---------- SRTP IV 构建 ---------- */
void SrptProtect::buildIv(uint8_t iv[16], const uint8_t* salt,
                           uint32_t ssrc, uint32_t index) {
    // RFC 3711 §4.1.1: IV = (salt_key XOR (SSRC * 2^64 | index)) << 16
    // 简化: IV = salt(112 bits) XOR (0 || SSRC || ROC || SEQ)
    memcpy(iv, salt, 14);
    iv[14] = 0;
    iv[15] = 0;

    // XOR SSRC (bits 32-63)
    iv[4] ^= (ssrc >> 24) & 0xFF;
    iv[5] ^= (ssrc >> 16) & 0xFF;
    iv[6] ^= (ssrc >> 8)  & 0xFF;
    iv[7] ^= ssrc & 0xFF;

    // XOR index (bits 80-111)
    iv[10] ^= (index >> 24) & 0xFF;
    iv[11] ^= (index >> 16) & 0xFF;
    iv[12] ^= (index >> 8)  & 0xFF;
    iv[13] ^= index & 0xFF;
}

/* ---------- HMAC-SHA1 简化实现 ---------- */
void SrptProtect::hmacSha1(const uint8_t* key, size_t key_len,
                            const uint8_t* data, size_t data_len,
                            uint8_t out[20]) {
    // HMAC-SHA1 = SHA1((key XOR opad) || SHA1((key XOR ipad) || data))
    const size_t BLOCK = 64;
    uint8_t key_block[64] = {};
    uint8_t ipad[64], opad[64];

    if (key_len > BLOCK) {
        sha1Hash(key, key_len, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    for (int i = 0; i < 64; i++) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5C;
    }

    uint8_t inner[64 + 1024];  // 足够大
    memcpy(inner, ipad, 64);
    memcpy(inner + 64, data, data_len);
    sha1Hash(inner, 64 + data_len, out);

    uint8_t outer[64 + 20];
    memcpy(outer, opad, 64);
    memcpy(outer + 64, out, 20);
    sha1Hash(outer, 64 + 20, out);
}

void SrptProtect::sha1Hash(const uint8_t* data, size_t len, uint8_t out[20]) {
    // SHA-1 实现（简化版本）
    uint32_t h[5] = {
        0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0
    };

    uint64_t bit_len = len * 8;

    // 分块处理
    for (size_t offset = 0; offset < len; offset += 64) {
        size_t chunk = (std::min)(len - offset, size_t(64));
        uint8_t block[64] = {};
        memcpy(block, data + offset, chunk);

        if (chunk < 64 || offset + 64 >= len) {
            // Padding
            block[chunk] = 0x80;
            if (chunk >= 56) {
                // 需要额外的块
                uint32_t w[80];
                for (int i = 0; i < 16; i++)
                    w[i] = (block[i*4]<<24)|(block[i*4+1]<<16)|(block[i*4+2]<<8)|block[i*4+3];
                for (int i = 16; i < 80; i++)
                    w[i] = ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) << 1) |
                           ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) >> 31);

                uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
                for (int i = 0; i < 80; i++) {
                    uint32_t f, k;
                    if (i < 20)      { f = (b&c)|(~b&d);         k = 0x5A827999; }
                    else if (i < 40) { f = b^c^d;                 k = 0x6ED9EBA1; }
                    else if (i < 60) { f = (b&c)|(b&d)|(c&d);    k = 0x8F1BBCDC; }
                    else             { f = b^c^d;                 k = 0xCA62C1D6; }
                    uint32_t t = ((a<<5)|(a>>27)) + f + e + k + w[i];
                    e = d; d = c; c = (b<<30)|(b>>2); b = a; a = t;
                }
                h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;

                // 第二块全 padding
                memset(block, 0, 64);
                for (int i = 60; i < 64; i++)
                    block[i] = (bit_len >> (56 - i*8)) & 0xFF;
            } else {
                // 直接在后续字节填长度
                for (int i = 56; i < 64; i++)
                    block[i] = (bit_len >> (56 - i*8)) & 0xFF;
            }
        }

        // 主循环
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (block[i*4]<<24)|(block[i*4+1]<<16)|(block[i*4+2]<<8)|block[i*4+3];
        for (int i = 16; i < 80; i++)
            w[i] = ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) << 1) |
                   ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) >> 31);

        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b&c)|(~b&d);         k = 0x5A827999; }
            else if (i < 40) { f = b^c^d;                 k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b&c)|(b&d)|(c&d);    k = 0x8F1BBCDC; }
            else             { f = b^c^d;                 k = 0xCA62C1D6; }
            uint32_t t = ((a<<5)|(a>>27)) + f + e + k + w[i];
            e = d; d = c; c = (b<<30)|(b>>2); b = a; a = t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }

    for (int i = 0; i < 5; i++) {
        out[i*4]   = (h[i] >> 24) & 0xFF;
        out[i*4+1] = (h[i] >> 16) & 0xFF;
        out[i*4+2] = (h[i] >> 8)  & 0xFF;
        out[i*4+3] = h[i] & 0xFF;
    }
}
