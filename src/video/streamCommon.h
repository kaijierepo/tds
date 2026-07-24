#pragma once

#include <string>
#include <cctype>

// ============================================================================
// streamCommon — 流媒体模块跨平台公共头文件
//
// 提供统一 socket 类型、跨平台宏，供 dtls_transport、streamNode 等模块共用，
// 避免各模块直接依赖 streamNode.h。
// ============================================================================

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
