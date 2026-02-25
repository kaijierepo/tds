/*****************************************************************************
Filename    : rsa_verify.c
Author      : RSA Verify Library
Description : RSA2048 验签函数实现
*****************************************************************************/

#include "rsa_verify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// 包含所需的头文件
// 注意：这里假设 base64.h, sha256.h, bignum.h, rsa.h 在同一目录下
// 或者已经通过 -I 包含到搜索路径
#include "base64.h"
#include "sha256.h"
#include "bignum.h"
#include "rsa.h"

// PEM文件解析常量
#define PEM_HEADER_PUBLIC "-----BEGIN PUBLIC KEY-----"
#define PEM_FOOTER_PUBLIC "-----END PUBLIC KEY-----"
#define PEM_HEADER_LEN    26
#define PEM_FOOTER_LEN    24

// 内部函数声明
static rsa_verify_result_t parse_pem_public_key_pkcs8(const char *pem_string,
                                               uint8_t *modulus, size_t *modulus_len,
                                               uint8_t *exponent, size_t *exponent_len);
static bool validate_public_key(const rsa2048_public_key_t *key);
static rsa_verify_result_t rsa_pkcs1_verify(const rsa2048_public_key_t *key,
                                          const uint8_t *hash,
                                          const uint8_t *signature, size_t signature_len);

/**
 * @brief 从原始数据加载RSA公钥
 */
rsa_verify_result_t rsa2048_load_public_key(rsa2048_public_key_t *key,
                                          const uint8_t *modulus, size_t modulus_len,
                                          const uint8_t *exponent, size_t exponent_len)
{
    if (!key || !modulus || !exponent) {
        return RSA_VERIFY_INVALID_KEY;
    }
    
    // 检查模数长度
    if (modulus_len > RSA2048_KEY_BYTES) {
        return RSA_VERIFY_INVALID_KEY;
    }
    
    // 检查指数长度
    if (exponent_len > RSA2048_KEY_BYTES) {
        return RSA_VERIFY_INVALID_KEY;
    }
    
    // 清空密钥结构
    memset(key, 0, sizeof(rsa2048_public_key_t));
    
    // 设置模数
    key->bits = (uint32_t)(modulus_len * 8);
    memcpy(key->modulus + (RSA2048_KEY_BYTES - modulus_len), modulus, modulus_len);
    
    // 设置指数
    memcpy(key->exponent + (RSA2048_KEY_BYTES - exponent_len), exponent, exponent_len);
    
    // 验证密钥
    if (!validate_public_key(key)) {
        return RSA_VERIFY_INVALID_KEY;
    }
    
    return RSA_VERIFY_SUCCESS;
}

/**
 * @brief 从Base64编码的PEM文件加载RSA公钥
 */
rsa_verify_result_t rsa2048_load_public_key_from_pem(rsa2048_public_key_t *key,
                                                   const char *filepath)
{
    if (!key || !filepath) {
        return RSA_VERIFY_INVALID_KEY;
    }
    
    // 读取文件
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        return RSA_VERIFY_INVALID_KEY;
    }
    
    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(fp);
        return RSA_VERIFY_INVALID_KEY;
    }
    
    // 分配内存并读取文件
    char *pem_data = (char *)malloc(file_size + 1);
    if (!pem_data) {
        fclose(fp);
        return RSA_VERIFY_MEMORY_ERROR;
    }
    
    size_t bytes_read = fread(pem_data, 1, file_size, fp);
    fclose(fp);
    
    if (bytes_read != (size_t)file_size) {
        free(pem_data);
        return RSA_VERIFY_INVALID_KEY;
    }
    
    pem_data[file_size] = '\0';
    
    // 解析PEM
    rsa_verify_result_t result = rsa2048_load_public_key_from_string(key, pem_data);
    
    free(pem_data);
    return result;
}

/**
 * @brief 从Base64编码的PEM字符串加载RSA公钥
 */
rsa_verify_result_t rsa2048_load_public_key_from_string(rsa2048_public_key_t *key,
                                                       const char *pem_string)
{
    if (!key || !pem_string) {
        return RSA_VERIFY_INVALID_KEY;
    }
    
    uint8_t modulus[RSA2048_KEY_BYTES];
    uint8_t exponent[4];  // 公钥指数通常很小（65537是3字节）
    size_t modulus_len = 0;
    size_t exponent_len = 0;
    
    memset(modulus, 0, sizeof(modulus));
    memset(exponent, 0, sizeof(exponent));
    
    // 解析PEM格式
    rsa_verify_result_t result = parse_pem_public_key_pkcs8(pem_string, modulus, &modulus_len,
                                                     exponent, &exponent_len);
    if (result != RSA_VERIFY_SUCCESS) {
        return result;
    }
    
    // 使用解析出的数据加载公钥
    return rsa2048_load_public_key(key, modulus, modulus_len, exponent, exponent_len);
}

/**
 * @brief RSA-PKCS#1 v1.5 验签
 */
rsa_verify_result_t rsa2048_verify_signature_base64(const rsa2048_public_key_t *key,
                                                   const uint8_t *data, size_t data_len,
                                                   const char *signature_base64)
{
    if (!key || !data || !signature_base64) {
        return RSA_VERIFY_INVALID_DATA;
    }
    
    if (!validate_public_key(key)) {
        return RSA_VERIFY_INVALID_KEY;
    }
    
    // 计算数据的SHA-256哈希
    uint8_t hash[SHA256_DIGEST_SIZE];
    sha256_hash(data, data_len, hash);
    
    // 验证哈希签名
    return rsa2048_verify_hash_base64(key, hash, signature_base64);
}

/**
 * @brief RSA-PKCS#1 v1.5 验签（原始二进制签名）
 */
rsa_verify_result_t rsa2048_verify_signature_binary(const rsa2048_public_key_t *key,
                                                   const uint8_t *data, size_t data_len,
                                                   const uint8_t *signature, size_t signature_len)
{
    if (!key || !data || !signature) {
        return RSA_VERIFY_INVALID_DATA;
    }
    
    if (!validate_public_key(key)) {
        return RSA_VERIFY_INVALID_KEY;
    }
    
    // 计算数据的SHA-256哈希
    uint8_t hash[SHA256_DIGEST_SIZE];
    sha256_hash(data, data_len, hash);
    
    // 验证签名
    return rsa_pkcs1_verify(key, hash, signature, signature_len);
}

/**
 * @brief 验证消息哈希的RSA签名
 */
rsa_verify_result_t rsa2048_verify_hash_base64(const rsa2048_public_key_t *key,
                                              const uint8_t hash[SHA256_DIGEST_SIZE],
                                              const char *signature_base64)
{
    if (!key || !hash || !signature_base64) {
        return RSA_VERIFY_INVALID_DATA;
    }
    
    if (!validate_public_key(key)) {
        return RSA_VERIFY_INVALID_KEY;
    }
    
    // 解码Base64签名
    uint8_t signature[RSA2048_KEY_BYTES];
    size_t signature_len = base64_decode_to_buffer(signature_base64, signature, sizeof(signature));
    
    if (signature_len == 0) {
        return RSA_VERIFY_BASE64_ERROR;
    }
    
    // 验证签名
    return rsa_pkcs1_verify(key, hash, signature, signature_len);
}

/**
 * @brief 计算数据的SHA-256哈希
 */
void sha256_hash(const uint8_t *data, size_t data_len, uint8_t hash[SHA256_DIGEST_SIZE])
{
    if (!data || !hash) {
        return;
    }
    
    // 使用提供的SHA256库
    SHA256 sha256;
    sha256.reset();
    sha256.add(data, data_len);
    
    // 获取哈希值
    unsigned char raw_hash[SHA256::HashBytes];
    sha256.getHash(raw_hash);
    
    memcpy(hash, raw_hash, SHA256_DIGEST_SIZE);
}

/**
 * @brief Base64解码
 */
size_t base64_decode_to_buffer(const char *input, uint8_t *output, size_t output_size)
{
    if (!input || !output) {
        return 0;
    }
    
    size_t input_len = strlen(input);
    unsigned int decoded_len = base64_decode(input, input_len, output);
    
    if (decoded_len > output_size) {
        return 0;
    }
    
    return decoded_len;
}

/**
 * @brief 获取错误描述
 */
const char *rsa2048_get_error_string(rsa_verify_result_t result)
{
    switch (result) {
        case RSA_VERIFY_SUCCESS:
            return "验证成功";
        case RSA_VERIFY_FAILED:
            return "验证失败";
        case RSA_VERIFY_INVALID_KEY:
            return "无效的RSA公钥";
        case RSA_VERIFY_INVALID_SIGNATURE:
            return "无效的签名格式";
        case RSA_VERIFY_INVALID_DATA:
            return "无效的输入数据";
        case RSA_VERIFY_MEMORY_ERROR:
            return "内存分配失败";
        case RSA_VERIFY_BASE64_ERROR:
            return "Base64解码失败";
        default:
            return "未知错误";
    }
}

/**
 * @brief 释放资源
 */
void rsa2048_cleanup(void)
{
    // 目前没有需要清理的资源
    // 如果有动态分配的内存，在这里释放
}

// ==================== 内部函数实现 ====================

/**
 * @brief 解析PKCS#8格式的PEM公钥（专用版）
 * @param pem_string PEM格式字符串
 * @param modulus 输出模数缓冲区
 * @param modulus_len 输出模数长度指针
 * @param exponent 输出指数缓冲区
 * @param exponent_len 输出指数长度指针
 * @return 解析结果
 */
static rsa_verify_result_t parse_pem_public_key_pkcs8(const char* pem_string,
    uint8_t* modulus, size_t* modulus_len,
    uint8_t* exponent, size_t* exponent_len) {
    if (!pem_string || !modulus || !exponent || !modulus_len || !exponent_len) {
        return RSA_VERIFY_INVALID_DATA;
    }

    // 1. 查找PEM头尾
    const char* header_start = strstr(pem_string, PEM_HEADER_PUBLIC);
    if (!header_start) {
        return RSA_VERIFY_INVALID_KEY;
    }

    const char* footer_start = strstr(header_start, PEM_FOOTER_PUBLIC);
    if (!footer_start) {
        return RSA_VERIFY_INVALID_KEY;
    }

    // 2. 提取Base64数据
    const char* base64_start = header_start + PEM_HEADER_LEN;
    const char* base64_end = footer_start;

    // 跳过空白字符
    while (base64_start < base64_end &&
        (*base64_start == ' ' || *base64_start == '\t' ||
            *base64_start == '\r' || *base64_start == '\n')) {
        base64_start++;
    }

    while (base64_end > base64_start &&
        (*(base64_end - 1) == ' ' || *(base64_end - 1) == '\t' ||
            *(base64_end - 1) == '\r' || *(base64_end - 1) == '\n')) {
        base64_end--;
    }

    // 3. 计算Base64长度
    size_t base64_len = base64_end - base64_start;
    if (base64_len == 0) {
        return RSA_VERIFY_INVALID_KEY;
    }

    // 复制Base64数据
    char* clean_base64 = (char*)malloc(base64_len + 1);
    if (!clean_base64) {
        return RSA_VERIFY_MEMORY_ERROR;
    }

    memcpy(clean_base64, base64_start, base64_len);
    clean_base64[base64_len] = '\0';

    // 移除空白字符
    char* dst = clean_base64;
    char* src = clean_base64;
    while (*src) {
        if (!isspace((unsigned char)*src)) {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
    size_t clean_len = dst - clean_base64;

    if (clean_len == 0) {
        free(clean_base64);
        return RSA_VERIFY_INVALID_KEY;
    }

    // 4. Base64解码
    uint8_t der_data[4096];
    unsigned int der_len = base64_decode(clean_base64, clean_len, der_data);
    free(clean_base64);

    if (der_len == 0 || der_len > sizeof(der_data)) {
        return RSA_VERIFY_BASE64_ERROR;
    }

    // 5. 解析PKCS#8 DER结构
    size_t pos = 0;

    // 5.1 外层SEQUENCE (SubjectPublicKeyInfo)
    if (der_len - pos < 2 || der_data[pos++] != 0x30) {  // SEQUENCE
        return RSA_VERIFY_INVALID_DER;
    }

    // 解析外层SEQUENCE长度
    int outer_len = 0;
    if ((der_data[pos] & 0x80) == 0) {
        outer_len = der_data[pos++];
    }
    else {
        int len_len = der_data[pos] & 0x7F;
        pos++;
        if (der_len - pos < (size_t)len_len) {
            return RSA_VERIFY_INVALID_LENGTH;
        }
        for (int i = 0; i < len_len; i++) {
            outer_len = (outer_len << 8) | der_data[pos++];
        }
    }

    if (outer_len < 0 || (size_t)outer_len > der_len - pos) {
        return RSA_VERIFY_INVALID_LENGTH;
    }

    // 5.2 算法标识符SEQUENCE
    if (der_len - pos < 2 || der_data[pos++] != 0x30) {  // SEQUENCE
        return RSA_VERIFY_INVALID_DER;
    }

    int algo_len = 0;
    if ((der_data[pos] & 0x80) == 0) {
        algo_len = der_data[pos++];
    }
    else {
        int len_len = der_data[pos] & 0x7F;
        pos++;
        if (der_len - pos < (size_t)len_len) {
            return RSA_VERIFY_INVALID_LENGTH;
        }
        for (int i = 0; i < len_len; i++) {
            algo_len = (algo_len << 8) | der_data[pos++];
        }
    }

    if (algo_len < 0 || (size_t)algo_len > der_len - pos) {
        return RSA_VERIFY_INVALID_LENGTH;
    }

    // 5.3 检查算法OID (1.2.840.113549.1.1.1 = rsaEncryption)
    if (der_len - pos < 2 || der_data[pos++] != 0x06) {  // OBJECT IDENTIFIER
        return RSA_VERIFY_UNSUPPORTED_FORMAT;
    }

    int oid_len = 0;
    if ((der_data[pos] & 0x80) == 0) {
        oid_len = der_data[pos++];
    }
    else {
        int len_len = der_data[pos] & 0x7F;
        pos++;
        if (der_len - pos < (size_t)len_len) {
            return RSA_VERIFY_INVALID_LENGTH;
        }
        for (int i = 0; i < len_len; i++) {
            oid_len = (oid_len << 8) | der_data[pos++];
        }
    }

    if (oid_len != 9) {  // RSA OID长度应为9字节
        return RSA_VERIFY_UNSUPPORTED_FORMAT;
    }

    // 检查是否为RSA OID
    static const uint8_t RSA_OID[] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01 };
    if (memcmp(der_data + pos, RSA_OID, 9) != 0) {
        return RSA_VERIFY_UNSUPPORTED_FORMAT;
    }
    pos += 9;

    // 5.4 NULL参数
    if (der_len - pos < 2 || der_data[pos] != 0x05 || der_data[pos + 1] != 0x00) {  // NULL
        return RSA_VERIFY_INVALID_DER;
    }
    pos += 2;

    // 5.5 BIT STRING
    if (der_len - pos < 2 || der_data[pos++] != 0x03) {  // BIT STRING
        return RSA_VERIFY_INVALID_DER;
    }

    int bit_string_len = 0;
    if ((der_data[pos] & 0x80) == 0) {
        bit_string_len = der_data[pos++];
    }
    else {
        int len_len = der_data[pos] & 0x7F;
        pos++;
        if (der_len - pos < (size_t)len_len) {
            return RSA_VERIFY_INVALID_LENGTH;
        }
        for (int i = 0; i < len_len; i++) {
            bit_string_len = (bit_string_len << 8) | der_data[pos++];
        }
    }

    if (bit_string_len < 1 || (size_t)bit_string_len > der_len - pos) {
        return RSA_VERIFY_INVALID_LENGTH;
    }

    // BIT STRING开头有一个未使用位数（通常为0）
    if (der_data[pos] != 0x00) {
        return RSA_VERIFY_INVALID_DER;
    }
    pos++;

    // 5.6 BIT STRING内的SEQUENCE (PKCS#1格式RSA公钥)
    if (der_len - pos < 2 || der_data[pos++] != 0x30) {  // SEQUENCE
        return RSA_VERIFY_INVALID_DER;
    }

    int rsa_seq_len = 0;
    if ((der_data[pos] & 0x80) == 0) {
        rsa_seq_len = der_data[pos++];
    }
    else {
        int len_len = der_data[pos] & 0x7F;
        pos++;
        if (der_len - pos < (size_t)len_len) {
            return RSA_VERIFY_INVALID_LENGTH;
        }
        for (int i = 0; i < len_len; i++) {
            rsa_seq_len = (rsa_seq_len << 8) | der_data[pos++];
        }
    }

    if (rsa_seq_len < 0 || (size_t)rsa_seq_len > der_len - pos) {
        return RSA_VERIFY_INVALID_LENGTH;
    }

    // 6. 解析RSA公钥参数
    // 6.1 模数n
    if (der_len - pos < 2 || der_data[pos++] != 0x02) {  // INTEGER
        return RSA_PARSE_NO_MODULUS;
    }

    int n_len = 0;
    if ((der_data[pos] & 0x80) == 0) {
        n_len = der_data[pos++];
    }
    else {
        int len_len = der_data[pos] & 0x7F;
        pos++;
        if (der_len - pos < (size_t)len_len) {
            return RSA_VERIFY_INVALID_LENGTH;
        }
        for (int i = 0; i < len_len; i++) {
            n_len = (n_len << 8) | der_data[pos++];
        }
    }

    if (n_len <= 0 || n_len > RSA2048_KEY_BYTES || (size_t)n_len > der_len - pos) {
        return RSA_VERIFY_INVALID_LENGTH;
    }

    // 处理前导零
    size_t n_start = 0;
    while (n_start < (size_t)n_len && der_data[pos + n_start] == 0x00) {
        n_start++;
    }

    size_t actual_n_len = n_len - n_start;
    if (actual_n_len == 0) {
        actual_n_len = 1;  // 模数至少1字节
        modulus[0] = 0x00;
    }
    else if (actual_n_len > RSA2048_KEY_BYTES) {
        return RSA_VERIFY_INVALID_LENGTH;
    }
    else {
        memcpy(modulus, der_data + pos + n_start, actual_n_len);
    }

    *modulus_len = actual_n_len;
    pos += n_len;

    // 6.2 公钥指数e
    if (der_len - pos < 2 || der_data[pos++] != 0x02) {  // INTEGER
        return RSA_PARSE_NO_EXPONENT;
    }

    int e_len = 0;
    if ((der_data[pos] & 0x80) == 0) {
        e_len = der_data[pos++];
    }
    else {
        int len_len = der_data[pos] & 0x7F;
        pos++;
        if (der_len - pos < (size_t)len_len) {
            return RSA_VERIFY_INVALID_LENGTH;
        }
        for (int i = 0; i < len_len; i++) {
            e_len = (e_len << 8) | der_data[pos++];
        }
    }

    if (e_len <= 0 || e_len > 4 || (size_t)e_len > der_len - pos) {
        return RSA_VERIFY_INVALID_LENGTH;
    }

    // 处理前导零
    size_t e_start = 0;
    while (e_start < (size_t)e_len && der_data[pos + e_start] == 0x00) {
        e_start++;
    }

    size_t actual_e_len = e_len - e_start;
    if (actual_e_len == 0) {
        actual_e_len = 1;
        exponent[0] = 0x00;
    }
    else if (actual_e_len > 4) {
        return RSA_VERIFY_INVALID_LENGTH;
    }
    else {
        memcpy(exponent, der_data + pos + e_start, actual_e_len);
    }

    *exponent_len = actual_e_len;

    return RSA_VERIFY_SUCCESS;
}
/**
 * @brief 验证公钥有效性
 */
static bool validate_public_key(const rsa2048_public_key_t *key)
{
    if (!key) {
        return false;
    }
    
    // 检查密钥位数
    if (key->bits != RSA2048_KEY_BITS) {
        return false;
    }
    
    // 检查模数是否全零
    int i;
    uint8_t zero_byte = 0;
    for (i = 0; i < RSA2048_KEY_BYTES; i++) {
        zero_byte |= key->modulus[i];
    }
    if (zero_byte == 0) {
        return false;
    }
    
    // 检查指数是否全零
    zero_byte = 0;
    for (i = 0; i < RSA2048_KEY_BYTES; i++) {
        zero_byte |= key->exponent[i];
    }
    if (zero_byte == 0) {
        return false;
    }
    
    return true;
}

/**
 * @brief RSA-PKCS#1 v1.5 签名验证核心函数
 */
static rsa_verify_result_t rsa_pkcs1_verify(const rsa2048_public_key_t *key,
                                          const uint8_t *hash,
                                          const uint8_t *signature, size_t signature_len)
{
    if (!key || !hash || !signature) {
        return RSA_VERIFY_INVALID_DATA;
    }
    
    if (signature_len != RSA2048_KEY_BYTES) {
        return RSA_VERIFY_INVALID_SIGNATURE;
    }
    
    // 将密钥转换为rsa_pk_t结构
    rsa_pk_t rsa_key;
    memset(&rsa_key, 0, sizeof(rsa_pk_t));
    
    rsa_key.bits = key->bits;
    memcpy(rsa_key.modulus, key->modulus, RSA2048_KEY_BYTES);
    memcpy(rsa_key.exponent, key->exponent, RSA2048_KEY_BYTES);
    
    // 使用提供的RSA库进行公钥解密
    uint8_t decrypted[RSA2048_KEY_BYTES];
    uint32_t decrypted_len = 0;
    
    int result = rsa_public_decrypt(decrypted, &decrypted_len, 
                                   (uint8_t *)signature, signature_len, 
                                   &rsa_key);
    
    if (result != 0) {
        return RSA_VERIFY_FAILED;
    }
    
    // 检查PKCS#1 v1.5填充格式
    if (decrypted_len < 2 + SHA256_DIGEST_SIZE + 8) {  // 最小填充长度
        return RSA_VERIFY_FAILED;
    }
    
    // 检查填充头
    if (decrypted[0] != 0x00 || decrypted[1] != 0x01) {
        return RSA_VERIFY_FAILED;
    }
    
    // 查找分隔符0x00
    size_t i = 2;
    while (i < decrypted_len && decrypted[i] == 0xFF) {
        i++;
    }
    
    if (i >= decrypted_len || decrypted[i] != 0x00) {
        return RSA_VERIFY_FAILED;
    }
    
    i++;  // 跳过0x00
    
    // 检查剩余长度是否足够
    if (decrypted_len - i < SHA256_DIGEST_SIZE) {
        return RSA_VERIFY_FAILED;
    }
    
    // 比较哈希值
    if (memcmp(&decrypted[i], hash, SHA256_DIGEST_SIZE) != 0) {
        return RSA_VERIFY_FAILED;
    }
    
    return RSA_VERIFY_SUCCESS;
}

std::string getVerifyRltInfo(rsa_verify_result_t rlt) {
    if (rlt == RSA_VERIFY_SUCCESS) {
        return "success";
    }
    else if (rlt == RSA_VERIFY_FAILED) {
        return "verify failed";
    }
    else if (rlt == RSA_VERIFY_INVALID_KEY) {
        return "invalid key";
    }
    else if (rlt == RSA_VERIFY_INVALID_SIGNATURE) {
        return "invalid signature";
    }
    else if (rlt == RSA_VERIFY_INVALID_DATA) {
        return "invalid data";
    }
    else if (rlt == RSA_VERIFY_MEMORY_ERROR) {
        return "memory error";
    }
    else if (rlt == RSA_VERIFY_BASE64_ERROR) {
        return "base64 error";
    }
    else {
        return "unknown error"; 
    }
}