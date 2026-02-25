/*****************************************************************************
Filename    : rsa_verify.h
Author      : RSA Verify Library
Description : RSA2048 验签函数接口
*****************************************************************************/
#ifndef RSA_VERIFY_H
#define RSA_VERIFY_H


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string>

// 常量定义
#define RSA2048_KEY_BITS             2048
#define RSA2048_KEY_BYTES            ((RSA2048_KEY_BITS + 7) / 8)  // 256字节
#define RSA2048_PRIME_BYTES          (RSA2048_KEY_BYTES / 2)       // 128字节
#define SHA256_DIGEST_SIZE           32

// RSA公钥结构
typedef struct {
    uint8_t modulus[RSA2048_KEY_BYTES];     // 模数 n
    uint8_t exponent[RSA2048_KEY_BYTES];    // 公钥指数 e
    uint32_t bits;                          // 密钥位数
} rsa2048_public_key_t;

// RSA验签结果
typedef enum {
    RSA_VERIFY_SUCCESS = 0,
    RSA_VERIFY_FAILED,
    RSA_VERIFY_INVALID_KEY,
    RSA_VERIFY_INVALID_SIGNATURE,
    RSA_VERIFY_INVALID_DATA,
    RSA_VERIFY_MEMORY_ERROR,
    RSA_VERIFY_BASE64_ERROR,
    RSA_VERIFY_INVALID_DER,
    RSA_VERIFY_INVALID_LENGTH,
    RSA_VERIFY_UNSUPPORTED_FORMAT,
    RSA_PARSE_NO_MODULUS,
    RSA_PARSE_NO_EXPONENT
} rsa_verify_result_t;

 std::string getVerifyRltInfo(rsa_verify_result_t rlt);
 

/**
 * @brief 从原始数据加载RSA公钥
 * @param key 公钥结构指针
 * @param modulus 模数数据
 * @param modulus_len 模数长度
 * @param exponent 公钥指数数据
 * @param exponent_len 指数长度
 * @return RSA_VERIFY_SUCCESS 成功，其他为错误码
 */
rsa_verify_result_t rsa2048_load_public_key(rsa2048_public_key_t *key,
                                          const uint8_t *modulus, size_t modulus_len,
                                          const uint8_t *exponent, size_t exponent_len);

/**
 * @brief 从Base64编码的PEM文件加载RSA公钥
 * @param key 公钥结构指针
 * @param filepath PEM文件路径
 * @return RSA_VERIFY_SUCCESS 成功，其他为错误码
 */
rsa_verify_result_t rsa2048_load_public_key_from_pem(rsa2048_public_key_t *key,
                                                   const char *filepath);

/**
 * @brief 从Base64编码的PEM字符串加载RSA公钥
 * @param key 公钥结构指针
 * @param pem_string PEM格式字符串
 * @return RSA_VERIFY_SUCCESS 成功，其他为错误码
 */
rsa_verify_result_t rsa2048_load_public_key_from_string(rsa2048_public_key_t *key,
                                                       const char *pem_string);

/**
 * @brief RSA-PKCS#1 v1.5 验签
 * @param key RSA公钥
 * @param data 原始数据
 * @param data_len 数据长度
 * @param signature Base64编码的签名
 * @return RSA_VERIFY_SUCCESS 验证成功，RSA_VERIFY_FAILED 验证失败
 */
rsa_verify_result_t rsa2048_verify_signature_base64(const rsa2048_public_key_t *key,
                                                   const uint8_t *data, size_t data_len,
                                                   const char *signature_base64);

/**
 * @brief RSA-PKCS#1 v1.5 验签（原始二进制签名）
 * @param key RSA公钥
 * @param data 原始数据
 * @param data_len 数据长度
 * @param signature 二进制签名数据
 * @param signature_len 签名长度
 * @return RSA_VERIFY_SUCCESS 验证成功，RSA_VERIFY_FAILED 验证失败
 */
rsa_verify_result_t rsa2048_verify_signature_binary(const rsa2048_public_key_t *key,
                                                   const uint8_t *data, size_t data_len,
                                                   const uint8_t *signature, size_t signature_len);

/**
 * @brief 验证消息哈希的RSA签名
 * @param key RSA公钥
 * @param hash SHA-256哈希值
 * @param signature Base64编码的签名
 * @return RSA_VERIFY_SUCCESS 验证成功，RSA_VERIFY_FAILED 验证失败
 */
rsa_verify_result_t rsa2048_verify_hash_base64(const rsa2048_public_key_t *key,
                                              const uint8_t hash[SHA256_DIGEST_SIZE],
                                              const char *signature_base64);

/**
 * @brief 计算数据的SHA-256哈希
 * @param data 输入数据
 * @param data_len 数据长度
 * @param hash 输出哈希缓冲区（必须至少32字节）
 */
void sha256_hash(const uint8_t *data, size_t data_len, uint8_t hash[SHA256_DIGEST_SIZE]);

/**
 * @brief Base64解码
 * @param input Base64编码字符串
 * @param output 输出缓冲区
 * @param output_size 缓冲区大小
 * @return 解码后的数据长度，0表示失败
 */
size_t base64_decode_to_buffer(const char *input, uint8_t *output, size_t output_size);

/**
 * @brief 获取错误描述
 * @param result 错误码
 * @return 错误描述字符串
 */
const char *rsa2048_get_error_string(rsa_verify_result_t result);

/**
 * @brief 释放资源（如果使用了动态分配）
 */
void rsa2048_cleanup(void);


#endif // RSA_VERIFY_H