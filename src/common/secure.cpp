#include "secure.h"

#ifdef ENABLE_OPEN_SSL
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include "base64.h"
//HMAC = Hash-based Message Authentication Code
//hmac_sha256的长度与SHA-256哈希值的长度相同，即32字节（256位）
//hmac計算需要提供一個key
string create_HMAC_SHA256_Base64(string data, string key) {
	unsigned char result[SHA256_DIGEST_LENGTH];
	HMAC_CTX* hctx = HMAC_CTX_new();
	HMAC_CTX_reset(hctx);
	unsigned int len = SHA256_DIGEST_LENGTH;

	// Using sha1 hash engine here.
	// You may use other hash engines. e.g EVP_md5(), EVP_sha224, EVP_sha512, etc
	HMAC_Init_ex(hctx, key.data(), key.length(), EVP_sha256(), NULL);
	HMAC_Update(hctx, (unsigned char*)data.data(), data.length());
	HMAC_Final(hctx, result, &len);
	HMAC_CTX_free(hctx);

	char out[80] = { 0 };
	base64_encode(result, SHA256_DIGEST_LENGTH, out);
	string hmac = out;
	return hmac;
}

string create_HMAC_SHA1_Base64(string data, string key) {
    // Be careful of the length of string with the choosen hash engine. SHA1 needed 20 characters.
    // Change the length accordingly with your choosen hash engine.     
    unsigned char* result;
    unsigned int len = 20;

    result = (unsigned char*)malloc(sizeof(char) * len);

    HMAC_CTX* hctx = HMAC_CTX_new();
    HMAC_CTX_reset(hctx);

    // Using sha1 hash engine here.
    // You may use other hash engines. e.g EVP_md5(), EVP_sha224, EVP_sha512, etc
    HMAC_Init_ex(hctx, key.data(), key.length(), EVP_sha1(), NULL);
    HMAC_Update(hctx, (unsigned char*)data.data(), data.length());
    HMAC_Final(hctx, result, &len);
    HMAC_CTX_free(hctx);

    char out[40] = { 0 };
    base64_encode(result, 20, out);
    string hmac = out;

    free(result);
    return hmac;
}

string getMD5(string src) {
	int ret = -1;
	int i = 0;
	unsigned char md[MD5_DIGEST_LENGTH];
	char buf[MD5_DIGEST_LENGTH * 2 + 1];

	MD5_CTX c;

	const void* data = src.data();

	//1. 初始化
	ret = MD5_Init(&c);
	if (1 != ret)
	{
		return "";
	}

	//2. 添加数据
	ret = MD5_Update(&c, (const void*)data, strlen((char*)data));
	if (1 != ret)
	{
		return "";
	}

	//3. 计算结果
	ret = MD5_Final(md, &c);
	if (1 != ret)
	{
		return "";
	}

	//4. 输出结果
	memset(buf, 0, MD5_DIGEST_LENGTH * 2 + 1);
	for (i = 0; i < MD5_DIGEST_LENGTH; i++)
	{
		sprintf((char*)&buf[i * 2], "%02x", md[i]);
	}


	memset(md, 0, MD5_DIGEST_LENGTH);
	MD5((unsigned char*)data, strlen((char*)data), md);
	memset(buf, 0, MD5_DIGEST_LENGTH * 2 + 1);
	for (i = 0; i < MD5_DIGEST_LENGTH; i++)
	{
		sprintf((char*)&buf[i * 2], "%02x", md[i]);
	}

	string des = buf;
	return des;
}

#else
#include "sha1.h"
#include "sha256.h"
#include "hmac.h"
#include "md5.h"
#include "base64.h"
#include <vector>
#include "common.h"

string create_HMAC_SHA256_Base64(string data, string key) {
    string s = hmac<SHA256>(data, key);
	vector<unsigned char> HMAC = str::hexStrToBytes(s);
    char out[80] = { 0 };
    base64_encode((unsigned char*)HMAC.data(), HMAC.size(), out);
    string hmac = out;
    return hmac;
}


string create_HMAC_SHA1_Base64(string data, string key) {
    string s = hmac<SHA1>(data, key);
    char out[80] = { 0 };
    base64_encode((unsigned char*)s.data(), s.length(), out);
    string hmac = out;
    return hmac;
}

string getMD5(string src) {
	MD5 md5;
	return md5(src);
}
#endif