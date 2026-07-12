#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <deque>

#include "mbedtls/ssl.h"
#include "mbedtls/ssl_cookie.h"
#include "mbedtls/private/entropy.h"
#include "mbedtls/private/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/timing.h"
#include "streamCommon.h"


/**
 * @brief DTLS 传输层：封装 mbedtls DTLS 握手与 SRTP keying material 导出
 *
 * 集成到 StreamNode::IceThreadCtx 中，在同一 UDP socket 上完成：
 *   1. DTLS 握手（服务端角色）
 *   2. 导出 SRTP keying material (RFC 5764)
 *   3. 提供 SRTP 加密上下文
 */
class DtlsTransport {
public:
    /// SRTP 密钥材料 （RFC 5764 profile）
    struct SrptKeyingMaterial {
        uint8_t client_write_key[16];    // AES-128 key (client→server)
        uint8_t server_write_key[16];    // AES-128 key (server→client)
        uint8_t client_write_salt[14];   // SRTP salt (client→server)
        uint8_t server_write_salt[14];   // SRTP salt (server→client)

        /// 标识握手完成后密钥是否已导出
        bool ready = false;

        /// 重置所有字段
        void clear() {
            memset(this, 0, sizeof(*this));
            ready = false;
        }
    };

    /// DTLS 回调
    using StateCallback = std::function<void(bool connected)>;

    DtlsTransport();
    ~DtlsTransport();

    // 禁止拷贝
    DtlsTransport(const DtlsTransport&) = delete;
    DtlsTransport& operator=(const DtlsTransport&) = delete;

    /**
     * @brief 初始化 DTLS 上下文（在 StreamServer 启动时调用一次）
     * @param cert_pem   PEM 格式的 X.509 证书
     * @param key_pem    PEM 格式的 ECDSA 私钥
     * @return 成功返回 true
     */
    bool init(const std::string& cert_pem, const std::string& key_pem);

    /**
     * @brief 为每个 ICE 会话启动 DTLS 状态机
     *        需在 ice_thread 中调用，传入目标 socket 和对端地址
     */
    void startHandshake();

    /**
     * @brief 喂入网络收到的 DTLS 握手数据（存入内部缓冲区，供 bio_recv 消费）
     * @param data  缓冲区
     * @param len   长度
     * @return true 表示该包已被 DTLS 层消费
     */
    bool handleDtlsData(const uint8_t* data, size_t len);

    /**
     * @brief 将主循环已收到的 DTLS 数据包存入内部缓冲区
     *        在主循环的 recvfrom 已消费 UDP 包后，调用此方法将数据传递给 DTLS 层
     * @param data  缓冲区
     * @param len   长度
     */
    void feedData(const uint8_t* data, size_t len);

    /**
     * @brief 设置客户端传输层标识（IP+Port），用于 DTLS Cookie 生成
     *        必须在收到第一个 ClientHello 后、doHandshakeStep 之前调用
     * @param addr  对端 sockaddr_in 地址
     */
    void setClientTransportId(const struct sockaddr_in& addr);

    /**
     * @brief 运行 DTLS 握手的主循环步骤（非阻塞）
     *        在 rtcSessionHandleThread 中每收到 DTLS 包时调用
     * @return 0=成功, MBEDTLS_ERR_SSL_WANT_READ/WRITE=需等待,
     *         其他=错误
     */
    int doHandshakeStep();

    /// 是否已完成握手
    bool isHandshakeDone() const { return handshake_done_; }

    /// 获取导出的 SRTP keying material
    const SrptKeyingMaterial& getKeyingMaterial() const { return keying_material_; }

    /// 获取协商的 DTLS 加密套件名称（如 "TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256"）
    const char* getDtlsCipherName() const;

    /// 获取协商的 SRTP 保护 profile 名称（如 "AES_CM_128_HMAC_SHA1_80"）
    const char* getSrtpProfileName() const;

    /// 获取证书 SHA-256 指纹（格式: "AB:CD:..."，用于 SDP）
    static std::string getFingerprint(const mbedtls_x509_crt& cert);

    /// 生成自签 ECDSA P-256 证书（输出参数 cert_str、key_str）
    static void generateSelfSignedCert(std::string cn, std::string& cert_str, std::string& key_str);

    /// 设置 socket 和对端地址（在 ice 线程中绑定）
    void setSocket(SocketHandle sock,
                   const struct sockaddr_in& peer_addr);

    /// 获取对端地址
    const struct sockaddr_in& getPeerAddr() const { return peer_addr_; }
    bool isPeerSet() const { return peer_set_; }

private:
    mbedtls_ssl_context        ssl_;
    mbedtls_ssl_config         conf_;
    mbedtls_entropy_context    entropy_;
    mbedtls_ctr_drbg_context   ctr_drbg_;
    mbedtls_x509_crt           own_cert_;
    mbedtls_pk_context         own_key_;
    mbedtls_ssl_cookie_ctx     cookie_ctx_;
    mbedtls_timing_delay_context timer_;

    SocketHandle sock_ = kInvalidSocket;
    struct sockaddr_in       peer_addr_;
    bool peer_set_ = false;

    std::atomic<bool> initialized_{ false };
    std::atomic<bool> handshake_done_{ false };
    SrptKeyingMaterial keying_material_;

    /// SSL 配置是否已做完
    bool conf_ready_ = false;

    /// 内部接收缓冲区：主循环 recvfrom 消费了 DTLS 数据后，通过 feedData 存入，
    /// bio_recv 优先从此缓冲区读取，避免 socket 已被排空导致读不到数据
    std::mutex recv_buf_mutex_;
    std::deque<uint8_t> recv_buf_;

    void exportSrptKeys();
    static int bio_send(void* ctx, const unsigned char* buf, size_t len);
    static int bio_recv(void* ctx, unsigned char* buf, size_t len);
    static void debug_print(void* ctx, int level,
                            const char* file, int line, const char* str);

    /// DTLS 定时器回调
    static void timing_set_delay(void* data, uint32_t int_ms, uint32_t fin_ms);
    static int  timing_get_delay(void* data);
};
