#ifndef TDS_COMMON_SECURE_H
#define TDS_COMMON_SECURE_H

#include <string>
using namespace std;

//用户登录时，使用用户名与密码计算签名
string create_HMAC_SHA256_Base64(string data, string key);

string create_HMAC_SHA1_Base64(string data, string key);

string getMD5(string src);
#endif /* TDS_COMMON_SECURE_H */
