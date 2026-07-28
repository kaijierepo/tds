#pragma once

#include <string>
#include <cctype>
#include <iostream>
#include <vector>
#include <cstdint>

// ============================================================================
// streamCommon — 流媒体模块跨平台公共头文件
//
// 提供统一 socket 类型、跨平台宏、日志函数等，供 dtls_transport、streamNode
// 等模块共用，避免各模块直接依赖 streamNode.h。
// ============================================================================

// 详细日志全局开关（由 StreamNode::start() 从 config_.verbose 同步）
extern bool g_stream_verbose;

inline void logInfo(const std::string& msg) {
    std::cout << "[INFO] " << msg << std::endl;
}

inline void logError(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
}

inline void logDebug(const std::string& msg) {
    if (g_stream_verbose) {
        std::cout << "[DEBUG] " << msg << std::endl;
    }
}

inline void logVerbose(const std::string& msg) {
    if (g_stream_verbose) {
        std::cout << "[VERBOSE] " << msg << std::endl;
    }
}

// MD5 十六进制摘要（供认证模块使用）
#include "../common/md5.h"
inline std::string md5Hex(const std::string& input) {
    MD5 md5;
    return md5(input);
}

// Base64 编码
inline std::string base64Encode(const std::string& input) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string encoded;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    size_t in_len = input.size();
    const char* bytes_to_encode = input.c_str();

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                encoded += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; j < i + 1; j++)
            encoded += base64_chars[char_array_4[j]];

        while (i++ < 3)
            encoded += '=';
    }

    return encoded;
}

// Base64 解码（用于解析 sprop-parameter-sets）
inline std::vector<uint8_t> base64Decode(const std::string& input) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::vector<uint8_t> out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)chars[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back((uint8_t)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// URL 百分号解码（例如 %E4%B8%AD → 中文）
inline std::string url_decode(const std::string& str) {
	std::string result;
	for (size_t i = 0; i < str.length(); ++i) {
		if (str[i] == '%' && i + 2 < str.length() &&
			std::isxdigit(static_cast<unsigned char>(str[i + 1])) &&
			std::isxdigit(static_cast<unsigned char>(str[i + 2]))) {
			int value = 0;
			char hex[3] = { str[i + 1], str[i + 2], '\0' };
			sscanf(hex, "%x", &value);
			result += static_cast<char>(value);
			i += 2;
		}
		else if (str[i] == '+') {
			result += ' ';
		}
		else {
			result += str[i];
		}
	}
	return result;
}

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define SOCKET_ERROR_NUM WSAGetLastError()
#define CLOSE_SOCKET closesocket
#define SOCKET_TYPE SOCKET
#define INVALID_SOCKET_VALUE INVALID_SOCKET
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <regex>
#define SOCKET_ERROR_NUM errno
#define CLOSE_SOCKET close
#define SOCKET_TYPE int
#define INVALID_SOCKET_VALUE -1
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif
