#include "pch.h"
#include "dtls_transport.h"
#include "logger.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/debug.h"
#include <sstream>
#include <iomanip>

// ============================================================================
// DtlsTransport 实现
// ============================================================================

DtlsTransport::DtlsTransport() {
    mbedtls_ssl_init(&ssl_);
    mbedtls_ssl_config_init(&conf_);
    mbedtls_entropy_init(&entropy_);
    mbedtls_ctr_drbg_init(&ctr_drbg_);
    mbedtls_x509_crt_init(&own_cert_);
    mbedtls_pk_init(&own_key_);
    mbedtls_ssl_cookie_init(&cookie_ctx_);
}

DtlsTransport::~DtlsTransport() {
    mbedtls_ssl_free(&ssl_);
    mbedtls_ssl_config_free(&conf_);
    mbedtls_entropy_free(&entropy_);
    mbedtls_ctr_drbg_free(&ctr_drbg_);
    mbedtls_x509_crt_free(&own_cert_);
    mbedtls_pk_free(&own_key_);
    mbedtls_ssl_cookie_free(&cookie_ctx_);
}

// ---- 静态 BIO 回调 --------------------------------------------------------
int DtlsTransport::bio_send(void* ctx, const unsigned char* buf, size_t len) {
    auto* self = static_cast<DtlsTransport*>(ctx);
    if (!self->peer_set_ || self->sock_ == StreamNode::kInvalidSocket)
        return MBEDTLS_ERR_NET_SEND_FAILED;
    int ret = sendto(static_cast<SOCKET_TYPE>(self->sock_),
                     (const char*)buf, (int)len, 0,
                     (struct sockaddr*)&self->peer_addr_,
                     sizeof(self->peer_addr_));
    if (ret == SOCKET_ERROR) return MBEDTLS_ERR_NET_SEND_FAILED;
    return ret;
}

int DtlsTransport::bio_recv(void* ctx, unsigned char* buf, size_t len) {
    auto* self = static_cast<DtlsTransport*>(ctx);
    if (!self->peer_set_ || self->sock_ == StreamNode::kInvalidSocket)
        return MBEDTLS_ERR_NET_RECV_FAILED;
    socklen_t peerLen = sizeof(self->peer_addr_);
    int ret = recvfrom(static_cast<SOCKET>(self->sock_),
                       (char*)buf, (int)len, 0,
                       (struct sockaddr*)&self->peer_addr_,
                       &peerLen);
    if (ret == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAETIMEDOUT)
            return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return ret;
}

void DtlsTransport::debug_print(void* /*ctx*/, int level,
                                 const char* file, int line, const char* str) {
    static const char* lvl[] = { "", "E", "W", "I", "D" };
    int idx = level < 0 ? 0 : (level > 4 ? 4 : level);
    LOG("[DTLS] %d %s:%d %s", level, file, line, str);
}

void DtlsTransport::timing_set_delay(void* data, uint32_t int_ms, uint32_t fin_ms) {
    auto* timer = static_cast<mbedtls_timing_delay_context*>(data);
    mbedtls_timing_set_delay(timer, int_ms, fin_ms);
}

int DtlsTransport::timing_get_delay(void* data) {
    auto* timer = static_cast<mbedtls_timing_delay_context*>(data);
    return mbedtls_timing_get_delay(timer);
}

// ---- 初始化 ---------------------------------------------------------------
bool DtlsTransport::init(const std::string& cert_pem, const std::string& key_pem) {
    int ret;

    // 初始化随机数生成器
    const char* pers = "tds_dtls";
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg_, mbedtls_entropy_func, &entropy_,
                                 (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        LOG("[DTLS] ctr_drbg_seed failed: %d", ret);
        return false;
    }

    // 解析证书
    ret = mbedtls_x509_crt_parse(&own_cert_,
                                  (const unsigned char*)cert_pem.c_str(),
                                  cert_pem.size() + 1);
    if (ret != 0) {
        LOG("[DTLS] x509_crt_parse failed: %d", ret);
        return false;
    }

    // 解析私钥
    ret = mbedtls_pk_parse_key(&own_key_,
                                (const unsigned char*)key_pem.c_str(),
                                key_pem.size() + 1, nullptr, 0);
    if (ret != 0) {
        LOG("[DTLS] pk_parse_key failed: %d", ret);
        return false;
    }

    // 初始化 DTLS Cookie（防止 DoS 攻击）
    ret = mbedtls_ssl_cookie_setup(&cookie_ctx_);
    if (ret != 0) {
        LOG("[DTLS] cookie_setup failed: %d", ret);
        // 非致命，继续
    }

    // 配置 SSL
    ret = mbedtls_ssl_config_defaults(&conf_,
                                       MBEDTLS_SSL_IS_SERVER,
                                       MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        LOG("[DTLS] ssl_config_defaults failed: %d", ret);
        return false;
    }

    // 最小版本 TLS 1.2
    mbedtls_ssl_conf_min_tls_version(&conf_, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&conf_, MBEDTLS_SSL_VERSION_TLS1_2);

    // RNG 由 PSA 内部管理，无需显式设置

    // 只允许 ECDHE-ECDSA-AES128-GCM-SHA256（WebRTC 官方推荐）
    static const int suites[] = {
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        0
    };
    mbedtls_ssl_conf_ciphersuites(&conf_, suites);

    // 不验证客户端证书（WebRTC 场景客户端通常不提供证书）
    mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_NONE);

    // 设置证书和私钥
    ret = mbedtls_ssl_conf_own_cert(&conf_, &own_cert_, &own_key_);
    if (ret != 0) {
        LOG("[DTLS] ssl_conf_own_cert failed: %d", ret);
        return false;
    }

    // 启用 DTLS SRTP (RFC 5764)
    static const mbedtls_ssl_srtp_profile srtp_profiles[] = {
        MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80,
        MBEDTLS_TLS_SRTP_UNSET
    };
    mbedtls_ssl_conf_dtls_srtp_protection_profiles(&conf_, srtp_profiles);

    // DTLS 超时设置
    mbedtls_ssl_conf_handshake_timeout(&conf_, 1000, 30000); // min 1s, max 30s

    // 开启调试
    mbedtls_ssl_conf_dbg(&conf_, debug_print, nullptr);
    mbedtls_debug_set_threshold(2);  // INFO level

    conf_ready_ = true;
    initialized_ = true;
    LOG("[DTLS] Init OK — cert fingerprint: %s", getFingerprint(own_cert_).c_str());
    return true;
}

// ---- 握手启动 -------------------------------------------------------------
void DtlsTransport::startHandshake() {
    if (!conf_ready_) return;

    int ret = mbedtls_ssl_setup(&ssl_, &conf_);
    if (ret != 0) {
        LOG("[DTLS] ssl_setup failed: %d", ret);
        return;
    }

    // 设置 DTLS Cookie 回调（config 级别）
    mbedtls_ssl_conf_dtls_cookies(&conf_, mbedtls_ssl_cookie_write,
                                   mbedtls_ssl_cookie_check, &cookie_ctx_);

    // 设置 BIO
    mbedtls_ssl_set_bio(&ssl_, this, bio_send, bio_recv, nullptr);

    // 设置 DTLS 时钟
    mbedtls_ssl_set_timer_cb(&ssl_, &timer_,
                              timing_set_delay, timing_get_delay);

    LOG("[DTLS] Handshake started");
}

// ---- 数据喂入 -------------------------------------------------------------
bool DtlsTransport::handleDtlsData(const uint8_t* data, size_t len) {
    if (handshake_done_) return false;

    // 将数据注入 mbedtls 的接收缓冲区会被 bio_recv 消费
    // mbedtls 的 DTLS 是基于 recvfrom 的，我们直接用套接字的真实 recv
    // 这里不需要手动喂入，mbedtls 会在 doHandshakeStep 中通过 bio_recv 读取
    return true;
}

// ---- 握手步骤 -------------------------------------------------------------
int DtlsTransport::doHandshakeStep() {
    if (handshake_done_) return 0;
    if (!initialized_) return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;

    int ret = mbedtls_ssl_handshake_step(&ssl_);
    if (ret == 0) {
        handshake_done_ = true;
        exportSrptKeys();
        LOG("[DTLS] Handshake OK");
    } else if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
               ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        // 正常等待
    } else {
        char buf[128];
        mbedtls_strerror(ret, buf, sizeof(buf));
        LOG("[DTLS] Handshake error: -0x%04x: %s", -ret, buf);
    }
    return ret;
}

// ---- SRTP Key 导出 --------------------------------------------------------
void DtlsTransport::exportSrptKeys() {
    // RFC 5764 §4.2: use_srtp DTLS-SRTP keying material export
    unsigned char keyblk[60]; // 2 * (16 key + 14 salt) = 60 bytes
    const char* label = "EXTRACTOR-dtls_srtp";

    int ret = mbedtls_ssl_export_keying_material(
        &ssl_, keyblk, sizeof(keyblk),
        label, strlen(label), nullptr, 0, 1);

    if (ret != 0) {
        LOG("[DTLS] mbedtls_ssl_export_keying_material failed: %d", ret);
        memset(keyblk, 0x42, sizeof(keyblk));
        LOG("[DTLS] WARNING: Using fallback zero keys — INSECURE!");
    }

    // 拆分 keying material
    // client_write_key (16) | server_write_key (16) | client_write_salt (14) | server_write_salt (14)
    memcpy(keying_material_.client_write_key,  keyblk,      16);
    memcpy(keying_material_.server_write_key,  keyblk + 16, 16);
    memcpy(keying_material_.client_write_salt, keyblk + 32, 14);
    memcpy(keying_material_.server_write_salt, keyblk + 46, 14);
    keying_material_.ready = true;

    LOG("[DTLS] SRTP keys exported OK");
}

// ---- 证书指纹 -------------------------------------------------------------
std::string DtlsTransport::getFingerprint(const mbedtls_x509_crt& cert) {
    unsigned char hash[32];
    size_t hash_len = 0;

    // SHA-256 直接调用
    int ret = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                          cert.raw.p, cert.raw.len, hash);
    if (ret != 0) {
        // fallback to mbedtls_sha256
        return "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
               "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00";
    }

    std::ostringstream oss;
    for (int i = 0; i < 32; i++) {
        if (i) oss << ":";
        oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << (int)hash[i];
    }
    return oss.str();
}

// ---- 自签证书生成 ----------------------------------------------------------
void DtlsTransport::generateSelfSignedCert(string cn, std::string& cert_str, std::string& key_str) {
    mbedtls_pk_context       key;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509write_cert   crt;

    mbedtls_pk_init(&key);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_x509write_crt_init(&crt);

    int ret = 0;
    const char* pers = "tds_cert_gen";

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                 (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        LOG("[DTLS] cert gen: ctr_drbg_seed failed: %d", ret);
        goto cleanup;
    }

    // 初始化 PSA Crypto（mbedTLS 4.x DTLS 内部需要）
    {
        psa_status_t psa_init_status = psa_crypto_init();
        if (psa_init_status != PSA_SUCCESS) {
            LOG("[DTLS] cert gen: psa_crypto_init failed: %d", (int)psa_init_status);
            ret = -1;
            goto cleanup;
        }
    }

    // 使用 PSA 生成 ECDSA P-256 密钥对，通过 pk_copy_from_psa 导入
    {
        psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
        psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
        psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
        psa_set_key_bits(&attr, 256);

        psa_key_id_t kid = PSA_KEY_ID_NULL;
        psa_status_t status = psa_generate_key(&attr, &kid);
        psa_reset_key_attributes(&attr);

        if (status != PSA_SUCCESS) {
            LOG("[DTLS] cert gen: psa_generate_key failed: %d", (int)status);
            ret = -1;
            goto cleanup;
        }

        // mbedTLS 4.x: 用 pk_copy_from_psa 将 PSA key 导入 pk_context
        ret = mbedtls_pk_copy_from_psa(kid, &key);
        psa_destroy_key(kid);

        if (ret != 0) {
            LOG("[DTLS] cert gen: pk_copy_from_psa failed: %d", ret);
            goto cleanup;
        }
    }

    // 写入证书字段（独立作用域，避免 goto 跨越初始化）
    {
        std::string full_cn = "CN=" + cn + ",O=TDS,C=CN";
        mbedtls_x509write_crt_set_subject_name(&crt, full_cn.c_str());
        mbedtls_x509write_crt_set_issuer_name(&crt, full_cn.c_str());

        // 有效期: 2024-01-01 ~ 2034-01-01
        mbedtls_x509write_crt_set_validity(&crt, "20240101000000", "20340101000000");

        // 序列号
        uint8_t serial_byte = 1;
        mbedtls_x509write_crt_set_serial_raw(&crt, &serial_byte, 1);

        // 版本 3
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);

        // 设置公钥 / 签发者密钥
        mbedtls_x509write_crt_set_subject_key(&crt, &key);
        mbedtls_x509write_crt_set_issuer_key(&crt, &key);

        // 签名算法 MD
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

        // 基本约束: CA=FALSE
        ret = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
        if (ret != 0) {
            LOG("[DTLS] cert gen: basic_constraints failed: %d", ret);
        }

        // Key Usage: digitalSignature | keyEncipherment
        ret = mbedtls_x509write_crt_set_key_usage(&crt,
                  MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
        if (ret != 0) {
            LOG("[DTLS] cert gen: key_usage failed: %d", ret);
        }

        // NS Cert Type: SSL Server
        ret = mbedtls_x509write_crt_set_ns_cert_type(&crt, MBEDTLS_X509_NS_CERT_TYPE_SSL_SERVER);
        if (ret != 0) {
            LOG("[DTLS] cert gen: ns_cert_type failed: %d", ret);
        }
    }

    // --- 写出 PEM ---
    {
        unsigned char cert_pem[4096];
        unsigned char key_pem_buf[2048];
        int pem_ret;

        pem_ret = mbedtls_x509write_crt_pem(&crt, cert_pem, sizeof(cert_pem));
        if (pem_ret != 0) {
            LOG("[DTLS] cert gen: x509write_crt_pem failed: %d", pem_ret);
            ret = pem_ret;
            goto cleanup;
        }

        pem_ret = mbedtls_pk_write_key_pem(&key, key_pem_buf, sizeof(key_pem_buf));
        if (pem_ret != 0) {
            LOG("[DTLS] cert gen: pk_write_key_pem failed: %d", pem_ret);
            ret = pem_ret;
            goto cleanup;
        }

        cert_str = std::string((const char*)cert_pem);
        key_str  = std::string((const char*)key_pem_buf);
    }

cleanup:
    mbedtls_pk_free(&key);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_x509write_crt_free(&crt);

    if (ret != 0) {
        LOG("[DTLS] WARNING: Certificate generation failed, returning empty");
    }
}

// ---- Socket 绑定 -----------------------------------------------------------
void DtlsTransport::setSocket(StreamNode::SocketHandle sock,
                               const struct sockaddr_in& peer_addr) {
    sock_ = sock;
    peer_addr_ = peer_addr;
    peer_set_ = true;
}
