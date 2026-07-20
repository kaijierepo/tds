#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

#include "dtls_transport.h"

/**
 * @brief SRTP 加密/解密 (AES-CTR + HMAC-SHA1)
 *
 * 符合 RFC 3711 的最小实现，用于 WebRTC RTP 会话的保护。
 * 支持 AES-128-CTR 模式加密 + HMAC-SHA1 认证（srtp_aead_aes_128_gcm 则为 AES-GCM）。
 *
 * 当前实现 SSRTP_PROFILE_AES128_CM_SHA1_80:
 *   - 加密: AES-128-CTR (16字节密钥)
 *   - 认证: HMAC-SHA1  (截断至 10 字节 tag)
 *   - MKI: 无
 */
class SrptProtect {
public:
    /// SRTP 上下文（per-session）
    struct Context {
        uint8_t  encrypt_key[16] = {};   // AES-128 key
        uint8_t  encrypt_salt[14] = {};  // SRTP master salt
        uint8_t  auth_key[20] = {};       // HMAC-SHA1 key (derived)
        // SRTCP 会话密钥（RFC 3711 §4.3 标签 0x03/0x04/0x05），用于解密浏览器反馈的 RTCP
        uint8_t  rtcp_encrypt_key[16] = {};
        uint8_t  rtcp_encrypt_salt[14] = {};
        uint8_t  rtcp_auth_key[20] = {};
        uint32_t ssrc = 0;
        uint32_t rollover_counter = 0;   // ROC (16-bit seq wrap counter)
        uint16_t highest_seq = 0;        // 已收到的最高序号（防重放）
        bool     initialized = false;
        bool     first_packet = true;     // 首个包标记（避免 sentinel 值与合法 seq 65535 冲突）

        void clear() { memset(this, 0, sizeof(*this)); }
    };

    /**
     * @brief 从 DTLS 导出的 keying material 初始化 SRTP 上下文
     * @param keys    DtlsTransport 导出的 SRTP keying material
     * @param is_server   true=使用 server_write_key, false=使用 client_write_key
     * @param ssrc        本端的 SSRC
     * @return 初始化后的 Context
     */
    static Context initFromDtls(const DtlsTransport::SrptKeyingMaterial& keys,
                                 bool is_server, uint32_t ssrc);

    /**
     * @brief SRTP 加密 (protect)
     *
     * 输入：RTP header(12B) + RTP payload
     * 输出：RTP header(12B) + encrypted payload + SRTP auth tag(10B) [+ MKI]
     *
     * @param ctx      SRTP 上下文
     * @param rtp      完整的 RTP 包（含 12 字节头 + payload）
     * @return 加密后的 SRTP 包
     */
    static std::vector<uint8_t> protect(Context& ctx,
                                        const std::vector<uint8_t>& rtp);

    /**
     * @brief SRTP 解密 (unprotect)
     * @param ctx      SRTP 上下文
     * @param srtp     完整的 SRTP 包
     * @param out_rtp  输出：解密后的 RTP 包
     * @return 0=成功, -1=认证失败, -2=重放攻击
     */
    static int unprotect(Context& ctx,
                         const std::vector<uint8_t>& srtp,
                         std::vector<uint8_t>& out_rtp);

    /**
     * @brief SRTCP 解密 (RFC 3711 §3.4)
     *
     * 结构: [RTCP compound(前8B明文,其余加密)] [E+SRTCP index(4B)] [auth tag(10B)]
     * @param ctx        SRTP 上下文（使用其 rtcp_* 会话密钥）
     * @param srtcp      完整的 SRTCP 包
     * @param out_rtcp   输出：解密后的 RTCP compound（不含 index 与 tag）
     * @return 0=成功, -1=认证失败或格式错误
     */
    static int unprotectRtcp(Context& ctx,
                             const uint8_t* srtcp, size_t len,
                             std::vector<uint8_t>& out_rtcp);

    /// AES-128-CTR keystream 生成
    /// @param skip_bytes 跳过前 N 字节密钥流（用于 RTP 头等不加密部分，KDF 调用时传 0）
    static void aesCtrCrypt(const uint8_t* key, size_t key_len,
                            const uint8_t* iv,  size_t iv_len,
                            uint8_t* data, size_t data_len,
                            size_t skip_bytes = 0);

    /// SRTP IV 构造 (RFC 3711 §4.1.1)
    static void buildIv(uint8_t iv[16], const uint8_t* salt,
                        uint32_t ssrc, uint32_t index);

private:
    /// HMAC-SHA1 (using mbedtls)
    static void hmacSha1(const uint8_t* key, size_t key_len,
                         const uint8_t* data, size_t data_len,
                         uint8_t out[20]);
};
