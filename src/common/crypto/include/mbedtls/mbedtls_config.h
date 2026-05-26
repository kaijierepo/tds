/**
 * \file mbedtls_config.h
 *
 * \brief Minimal configuration for WebRTC DTLS-SRTP server on Windows
 *
 * 裁剪策略：仅保留 DTLS 1.2 + ECDHE-ECDSA-AES-GCM + SRTP key export 所需模块
 */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

#define MBEDTLS_CONFIG_VERSION 0x04000000

/* ===================================================================
 * SECTION: Platform abstraction layer
 * =================================================================== */
#define MBEDTLS_NET_C               /* UDP networking */
#define MBEDTLS_TIMING_C            /* Timer (DTLS retransmission) */
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PLATFORM_C 1
#define MBEDTLS_PLATFORM_UTIL_C
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PLATFORM_TIME_ALT 1   /* Use Windows timers */

/* ===================================================================
 * SECTION: General configuration
 * =================================================================== */
#define MBEDTLS_ERROR_C             /* strerror */
#define MBEDTLS_VERSION_C
#define MBEDTLS_VERSION_FEATURES

/* ===================================================================
 * SECTION: Core crypto primitives (via PSA)
 * =================================================================== */
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_ASN1_PARSE_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_ASN1_WRITE_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_BASE64_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_BIGNUM_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_MD_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PEM_PARSE_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PEM_WRITE_C 1

/* ===================================================================
 * SECTION: PSA Crypto (via tf-psa-crypto)
 * =================================================================== */
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PSA_CRYPTO_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PSA_CRYPTO_CLIENT 1
#define MBEDTLS_PSA_CRYPTO_CONFIG
#define MBEDTLS_PSA_CRYPTO_DRIVERS
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PSA_CRYPTO_BUILTIN_KEYS 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PSA_CRYPTO_STORAGE_C 1

/* ===================================================================
 * SECTION: Elliptic curve crypto (ECDHE + ECDSA)
 * =================================================================== */
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_ECP_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_ECP_DP_SECP256R1_ENABLED 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_ECP_NIST_OPTIM 1     /* NIST P-256 fast reduction */
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_ECDSA_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PK_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PK_PARSE_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_PK_WRITE_C 1

/* ===================================================================
 * SECTION: Hash functions
 * =================================================================== */
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_SHA256_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_SHA512_C 1

/* ===================================================================
 * SECTION: AEAD / Cipher
 * =================================================================== */
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_AES_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_AESNI_C 1    /* x64 AES-NI acceleration */
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_GCM_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_CIPHER_C 1

/* ===================================================================
 * SECTION: HMAC / Key derivation / RNG
 * =================================================================== */
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_HMAC_DRBG_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_CTR_DRBG_C 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_ENTROPY_C 1

/* ===================================================================
 * SECTION: SSL / DTLS
 * =================================================================== */
#define MBEDTLS_SSL_PROTO_DTLS
#define MBEDTLS_SSL_DTLS_ANTI_REPLAY
#define MBEDTLS_SSL_DTLS_CONNECTION_ID  /* DTLS CID (Connection ID) */ 
#define MBEDTLS_SSL_DTLS_HELLO_VERIFY
#define MBEDTLS_SSL_COOKIE_C
#define MBEDTLS_SSL_DTLS_SRTP          /* SRTP key export (RFC 5764) */
#define MBEDTLS_SSL_CID

#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_CLI_C              /* Required for server too (cookie gen) */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CIPHERSUITES_C

/* Enable DTLS 1.2 ciphersuites we need */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE

/* TLS 1.2 only (WebRTC uses DTLS 1.2, not 1.3) */
#define MBEDTLS_SSL_PROTO_TLS1_2
#undef  MBEDTLS_SSL_PROTO_TLS1_3

/* ===================================================================
 * SECTION: X.509 certificate
 * =================================================================== */
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_CRT_WRITE_C
#define MBEDTLS_X509_CSR_PARSE_C
#define MBEDTLS_X509_CRL_PARSE_C
#define MBEDTLS_X509_CREATE_C
#define MBEDTLS_X509_USE_C

/* ===================================================================
 * SECTION: Feature usage controls
 * =================================================================== */
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_ECP_WINDOW_SIZE           4
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_ECP_FIXED_POINT_OPTIM     1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_MPI_MAX_SIZE            512
#define MBEDTLS_SSL_IN_CONTENT_LEN     16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN    16384
#define MBEDTLS_SSL_CIPHERSUITE_COUNT   50
#define MBEDTLS_SSL_MAX_EARLY_DATA_SIZE  0

#define MBEDTLS_SSL_KEYING_MATERIAL_EXPORT /* SRTP key export (RFC 5705) */
#define MBEDTLS_DEBUG_C                    /* Debug logging */

/* ===================================================================
 * SECTION: Platform adaptation for Windows
 * =================================================================== */
#ifdef _WIN32
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_FS_IO 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_HAVE_TIME 1
#define MBEDTLS_CONFIG_CHECK_WASSET_MBEDTLS_HAVE_TIME_DATE 1
#endif

/* ===================================================================
 * SECTION: Disabled modules (save code size)
 * =================================================================== */
#undef MBEDTLS_SSL_PROTO_TLS1_3
#undef MBEDTLS_RSA_C
#undef MBEDTLS_PSA_CRYPTO_SE_C
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_SSL_TICKET_C
#undef MBEDTLS_SSL_CACHE_C
#undef MBEDTLS_SSL_SESSION_TICKETS
#undef MBEDTLS_SSL_CONTEXT_SERIALIZATION
#undef MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS
#undef MBEDTLS_SSL_TRUNCATED_HMAC
#undef MBEDTLS_SSL_ALPN
#undef MBEDTLS_SSL_SERVER_NAME_INDICATION
#undef MBEDTLS_SSL_EARLY_DATA

/* This uses PSA crypto implementations from tf-psa-crypto */
#define MBEDTLS_USE_PSA_CRYPTO

#endif /* MBEDTLS_CONFIG_H */
