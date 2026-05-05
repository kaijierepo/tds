#include "rtspClient.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <climits>
#include <cstdarg>
#include <ctime>
#include <algorithm>
#include <random>
#include <cassert>
#include <regex>
#include <logger.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define SOCKET_ERROR_NUM WSAGetLastError()
#define CLOSE_SOCKET closesocket
#define SOCKET_TYPE SOCKET
#define INVALID_SOCKET_VALUE INVALID_SOCKET
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
#endif

// 静态初始化
#ifdef _WIN32
class WinsockInitializer {
public:
    WinsockInitializer() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~WinsockInitializer() {
        WSACleanup();
    }
};
static WinsockInitializer winsock_init;
#endif

// Base64解码（用于解析 sprop-parameter-sets）
static std::vector<uint8_t> base64Decode(const std::string& input) {
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

// ============================================================================
// MD5实现 (RFC 1321)
// ============================================================================

// 辅助宏
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

// 循环左移
#define LEFT_ROTATE(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

// 步骤函数
#define FF(a, b, c, d, x, s, ac) { \
    (a) += F((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = LEFT_ROTATE((a), (s)); \
    (a) += (b); \
}

#define GG(a, b, c, d, x, s, ac) { \
    (a) += G((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = LEFT_ROTATE((a), (s)); \
    (a) += (b); \
}

#define HH(a, b, c, d, x, s, ac) { \
    (a) += H((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = LEFT_ROTATE((a), (s)); \
    (a) += (b); \
}

#define II(a, b, c, d, x, s, ac) { \
    (a) += I((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = LEFT_ROTATE((a), (s)); \
    (a) += (b); \
}

// MD5初始化
void RtspClient::md5Init(MD5Context* context) {
    context->count[0] = 0;
    context->count[1] = 0;

    // 初始化状态 (魔法数)
    context->state[0] = 0x67452301;
    context->state[1] = 0xefcdab89;
    context->state[2] = 0x98badcfe;
    context->state[3] = 0x10325476;
}

// MD5更新
void RtspClient::md5Update(MD5Context* context, const uint8_t* data, size_t length) {
    size_t index = (size_t)((context->count[0] >> 3) & 0x3F);

    // 更新位数
    context->count[0] += (uint32_t)(length << 3);
    if (context->count[0] < (uint32_t)(length << 3)) {
        context->count[1]++;
    }
    context->count[1] += (uint32_t)(length >> 29);

    size_t partLen = 64 - index;
    size_t i = 0;

    // 处理完整块
    if (length >= partLen) {
        memcpy(&context->buffer[index], data, partLen);
        md5Transform(context->state, context->buffer);

        for (i = partLen; i + 63 < length; i += 64) {
            md5Transform(context->state, &data[i]);
        }

        index = 0;
    }
    else {
        i = 0;
    }

    // 保存剩余数据
    memcpy(&context->buffer[index], &data[i], length - i);
}

// MD5结束
void RtspClient::md5Final(MD5Context* context, uint8_t digest[16]) {
    static uint8_t padding[64] = {
        0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    // 保存长度
    uint8_t bits[8];
    for (int i = 0; i < 8; i++) {
        bits[i] = (uint8_t)((context->count[i >> 2] >> ((i & 3) << 3)) & 0xFF);
    }

    // 填充1位和0位
    size_t index = (size_t)((context->count[0] >> 3) & 0x3F);
    size_t padLen = (index < 56) ? (56 - index) : (120 - index);
    md5Update(context, padding, padLen);

    // 附加长度
    md5Update(context, bits, 8);

    // 输出
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            digest[i * 4 + j] = (uint8_t)((context->state[i] >> (j * 8)) & 0xFF);
        }
    }

    // 清理
    memset(context, 0, sizeof(*context));
}

// MD5转换函数
void RtspClient::md5Transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];

    // 解码输入
    for (int i = 0, j = 0; j < 64; i++, j += 4) {
        x[i] = ((uint32_t)block[j]) | (((uint32_t)block[j + 1]) << 8) |
            (((uint32_t)block[j + 2]) << 16) | (((uint32_t)block[j + 3]) << 24);
    }

    // 第1轮
    FF(a, b, c, d, x[0], 7, 0xd76aa478);
    FF(d, a, b, c, x[1], 12, 0xe8c7b756);
    FF(c, d, a, b, x[2], 17, 0x242070db);
    FF(b, c, d, a, x[3], 22, 0xc1bdceee);
    FF(a, b, c, d, x[4], 7, 0xf57c0faf);
    FF(d, a, b, c, x[5], 12, 0x4787c62a);
    FF(c, d, a, b, x[6], 17, 0xa8304613);
    FF(b, c, d, a, x[7], 22, 0xfd469501);
    FF(a, b, c, d, x[8], 7, 0x698098d8);
    FF(d, a, b, c, x[9], 12, 0x8b44f7af);
    FF(c, d, a, b, x[10], 17, 0xffff5bb1);
    FF(b, c, d, a, x[11], 22, 0x895cd7be);
    FF(a, b, c, d, x[12], 7, 0x6b901122);
    FF(d, a, b, c, x[13], 12, 0xfd987193);
    FF(c, d, a, b, x[14], 17, 0xa679438e);
    FF(b, c, d, a, x[15], 22, 0x49b40821);

    // 第2轮
    GG(a, b, c, d, x[1], 5, 0xf61e2562);
    GG(d, a, b, c, x[6], 9, 0xc040b340);
    GG(c, d, a, b, x[11], 14, 0x265e5a51);
    GG(b, c, d, a, x[0], 20, 0xe9b6c7aa);
    GG(a, b, c, d, x[5], 5, 0xd62f105d);
    GG(d, a, b, c, x[10], 9, 0x02441453);
    GG(c, d, a, b, x[15], 14, 0xd8a1e681);
    GG(b, c, d, a, x[4], 20, 0xe7d3fbc8);
    GG(a, b, c, d, x[9], 5, 0x21e1cde6);
    GG(d, a, b, c, x[14], 9, 0xc33707d6);
    GG(c, d, a, b, x[3], 14, 0xf4d50d87);
    GG(b, c, d, a, x[8], 20, 0x455a14ed);
    GG(a, b, c, d, x[13], 5, 0xa9e3e905);
    GG(d, a, b, c, x[2], 9, 0xfcefa3f8);
    GG(c, d, a, b, x[7], 14, 0x676f02d9);
    GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    // 第3轮
    HH(a, b, c, d, x[5], 4, 0xfffa3942);
    HH(d, a, b, c, x[8], 11, 0x8771f681);
    HH(c, d, a, b, x[11], 16, 0x6d9d6122);
    HH(b, c, d, a, x[14], 23, 0xfde5380c);
    HH(a, b, c, d, x[1], 4, 0xa4beea44);
    HH(d, a, b, c, x[4], 11, 0x4bdecfa9);
    HH(c, d, a, b, x[7], 16, 0xf6bb4b60);
    HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    HH(a, b, c, d, x[13], 4, 0x289b7ec6);
    HH(d, a, b, c, x[0], 11, 0xeaa127fa);
    HH(c, d, a, b, x[3], 16, 0xd4ef3085);
    HH(b, c, d, a, x[6], 23, 0x04881d05);
    HH(a, b, c, d, x[9], 4, 0xd9d4d039);
    HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
    HH(b, c, d, a, x[2], 23, 0xc4ac5665);

    // 第4轮
    II(a, b, c, d, x[0], 6, 0xf4292244);
    II(d, a, b, c, x[7], 10, 0x432aff97);
    II(c, d, a, b, x[14], 15, 0xab9423a7);
    II(b, c, d, a, x[5], 21, 0xfc93a039);
    II(a, b, c, d, x[12], 6, 0x655b59c3);
    II(d, a, b, c, x[3], 10, 0x8f0ccc92);
    II(c, d, a, b, x[10], 15, 0xffeff47d);
    II(b, c, d, a, x[1], 21, 0x85845dd1);
    II(a, b, c, d, x[8], 6, 0x6fa87e4f);
    II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    II(c, d, a, b, x[6], 15, 0xa3014314);
    II(b, c, d, a, x[13], 21, 0x4e0811a1);
    II(a, b, c, d, x[4], 6, 0xf7537e82);
    II(d, a, b, c, x[11], 10, 0xbd3af235);
    II(c, d, a, b, x[2], 15, 0x2ad7d2bb);
    II(b, c, d, a, x[9], 21, 0xeb86d391);

    // 更新状态
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

// MD5辅助函数
std::string RtspClient::md5Hex(const std::string& input) {
    MD5Context context;
    md5Init(&context);
    md5Update(&context, (const uint8_t*)input.c_str(), input.size());

    uint8_t digest[16];
    md5Final(&context, digest);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; i++) {
        ss << std::setw(2) << (int)digest[i];
    }
    return ss.str();
}

// Base64编码辅助函数
std::string RtspClient::base64Encode(const std::string& input) {
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

// HTTP包验证函数（保持不变）
size_t IsValidPkt_HTTP(std::string& strData, size_t iLen) {
    size_t iPos_contentLengthLineStart = strData.find("Content-Length:");
    if (iPos_contentLengthLineStart == std::string::npos) {
        auto iDataLen = strData.length();
        if (iDataLen >= 4) {
            std::string tail = strData.substr(iDataLen - 4, 4);
            if (tail == "\r\n\r\n")
                return iLen;
            else
                return 0;
        }
    }
    else {
        size_t iPos_contentLengthLineEnd = strData.find("\r\n", iPos_contentLengthLineStart);
        if (iPos_contentLengthLineEnd == std::string::npos)
            return 0;

        std::string strLen = strData.substr(iPos_contentLengthLineStart + 15, iPos_contentLengthLineEnd - (iPos_contentLengthLineStart + 15));
        size_t iContentLen = atoi(strLen.c_str());

        size_t iBodyStart = 0;
        for (size_t i = iPos_contentLengthLineEnd; i + 3 < iLen; i++) {
            if (strData[i] == '\r' &&
                strData[i + 1] == '\n' &&
                strData[i + 2] == '\r' &&
                strData[i + 3] == '\n') {
                iBodyStart = i + 4;
                break;
            }
        }

        if (iBodyStart == 0)
            return 0;

        if (iLen >= iBodyStart + iContentLen)
            return iBodyStart + iContentLen;

        return 0;
    }
    return 0;
}
// ============================================================================
// Connection 实现
// ============================================================================

RtspClient::Connection::Connection() : sockfd_(RtspClient::kInvalidSocket) {
}

RtspClient::Connection::~Connection() {
    disconnect();
}

bool RtspClient::Connection::connect(const std::string& host, int port, int timeout_ms) {
    disconnect();
    last_error_ = 0;

    // 创建socket
    sockfd_ = static_cast<RtspClient::SocketHandle>(socket(AF_INET, SOCK_STREAM, 0));
    if (sockfd_ == RtspClient::kInvalidSocket) {
        last_error_ = SOCKET_ERROR_NUM;
        return false;
    }

    // 设置超时
    setSocketTimeout(timeout_ms);

    // 解析主机名
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // 尝试直接解析为IP地址
    if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
        // 如果不是IP地址，进行DNS解析
        struct hostent* server = gethostbyname(host.c_str());
        if (!server) {
            last_error_ = SOCKET_ERROR_NUM;
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(sockfd_));
            sockfd_ = RtspClient::kInvalidSocket;
            return false;
        }
        memcpy(&serv_addr.sin_addr, server->h_addr, server->h_length);
    }

    // 连接
    if (::connect(static_cast<SOCKET_TYPE>(sockfd_), (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        last_error_ = SOCKET_ERROR_NUM;
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(sockfd_));
        sockfd_ = RtspClient::kInvalidSocket;
        return false;
    }

    // 禁用 Nagle 算法，确保请求立即发送
    int nodelay = 1;
    //setsockopt(static_cast<SOCKET_TYPE>(sockfd_), IPPROTO_TCP, TCP_NODELAY,
    //    reinterpret_cast<char*>(&nodelay), sizeof(nodelay));

    host_ = host;
    port_ = port;

    // 恢复为阻塞模式
    setSocketTimeout(0);
    last_error_ = 0;

    return true;
}

void RtspClient::Connection::disconnect() {
    if (sockfd_ != RtspClient::kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(sockfd_));
        sockfd_ = RtspClient::kInvalidSocket;
    }
    host_.clear();
    port_ = 0;
    last_error_ = 0;
}

bool RtspClient::Connection::isConnected() const {
    return sockfd_ != RtspClient::kInvalidSocket;
}

int RtspClient::Connection::send(const void* data, size_t size, int timeout_ms) {
    if (sockfd_ == RtspClient::kInvalidSocket) return -1;

    if (timeout_ms > 0) {
        setSocketTimeout(timeout_ms);
    }

    const char* bytes = static_cast<const char*>(data);
    size_t total_sent = 0;

    while (total_sent < size) {
        size_t remaining = size - total_sent;
        int chunk = static_cast<int>((remaining > static_cast<size_t>(INT_MAX)) ? static_cast<size_t>(INT_MAX) : remaining);

        int n = static_cast<int>(::send(static_cast<SOCKET_TYPE>(sockfd_),
            bytes + total_sent, chunk, 0));

        if (n <= 0) {
            last_error_ = (n < 0) ? SOCKET_ERROR_NUM : 0;
            if (timeout_ms > 0) {
                setSocketTimeout(0);
            }
            return (total_sent > 0) ? static_cast<int>(total_sent) : n;
        }

        total_sent += static_cast<size_t>(n);
    }

    if (timeout_ms > 0) {
        setSocketTimeout(0);
    }

    last_error_ = 0;
    return static_cast<int>(total_sent);
}

int RtspClient::Connection::receive(void* buffer, size_t size, int timeout_ms) {
    if (sockfd_ == RtspClient::kInvalidSocket) return -1;

    if (timeout_ms > 0) {
        setSocketTimeout(timeout_ms);
    }

    int received = static_cast<int>(::recv(static_cast<SOCKET_TYPE>(sockfd_),
        (char*)buffer, static_cast<int>(size), 0));
    if (received <= 0) {
        last_error_ = (received < 0) ? SOCKET_ERROR_NUM : 0;
    }
    else {
        last_error_ = 0;
    }

    if (timeout_ms > 0) {
        setSocketTimeout(0);
    }

    return received;
}

int RtspClient::Connection::receiveHttpResp(std::string& response, int timeout_ms) {
    if (sockfd_ == RtspClient::kInvalidSocket) return -1;

    int total = 0;
    char buf[1000] = { 0 };

    // 清除超时设置，使用 select() 来等待
    setSocketTimeout(0);

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd_, &readfds);
        
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        int select_result = ::select(static_cast<int>(sockfd_) + 1, &readfds, nullptr, nullptr, &tv);
        if (select_result <= 0) {
            // select 超时或错误
            last_error_ = (select_result < 0) ? SOCKET_ERROR_NUM : 0;
            // 即使超时，如果有部分数据也要尝试处理
            if (total > 0 && IsValidPkt_HTTP(response, static_cast<size_t>(total))) {
                last_error_ = 0;
                return total;
            }
            return (total > 0) ? total : -1;
        }

        // socket 可读
        int n = static_cast<int>(::recv(static_cast<SOCKET_TYPE>(sockfd_), buf, (int)sizeof(buf), 0));
        if (n <= 0) {
            last_error_ = (n < 0) ? SOCKET_ERROR_NUM : 0;
            if (total > 0 && IsValidPkt_HTTP(response, static_cast<size_t>(total))) {
                last_error_ = 0;
                return total;
            }
            return (total > 0) ? total : n;
        }

        total += n;
        response.append(buf, n);

        // 调试：打印每次接收的数据
        printf("[DEBUG] recv %d bytes, total=%d, first bytes: %.*s\n", n, total, n > 20 ? 20 : n, buf);

        if (IsValidPkt_HTTP(response, static_cast<size_t>(total))) {
            break;
        }
    }

    last_error_ = 0;
    return total;
}

bool RtspClient::Connection::setSocketTimeout(int timeout_ms) {
    if (sockfd_ == RtspClient::kInvalidSocket) return false;

#ifdef _WIN32
    DWORD tv = timeout_ms;
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
#endif

    int result = setsockopt(static_cast<SOCKET_TYPE>(sockfd_), SOL_SOCKET, SO_RCVTIMEO,
        (const char*)&tv, sizeof(tv));
    if (result == 0) {
        result = setsockopt(static_cast<SOCKET_TYPE>(sockfd_), SOL_SOCKET, SO_SNDTIMEO,
            (const char*)&tv, sizeof(tv));
    }

    if (result != 0) {
        last_error_ = SOCKET_ERROR_NUM;
    }
    return result == 0;
}

// ============================================================================
// RtspClient 实现
// ============================================================================

RtspClient::RtspClient() {
    stats_.start_time = std::chrono::steady_clock::now();
    stats_.last_frame_time = std::chrono::steady_clock::now();
}

RtspClient::~RtspClient() {
    stop();
}

bool RtspClient::start(const Config& config) {
    if (running_) {
        return false;
    }

    config_ = config;
    running_ = true;
    stopping_ = false;
    streaming_ = false;
    retry_count_ = 0;

    // 清除之前的认证信息
    source_auth_.clear();
    target_auth_.clear();

    // 初始化认证信息
    if (!config_.source_username.empty()) {
        source_auth_.username = config_.source_username;
        source_auth_.password = config_.source_password;
    }

    if (!config_.target_username.empty()) {
        target_auth_.username = config_.target_username;
        target_auth_.password = config_.target_password;
    }

    setState(State::CONNECTING, "Starting RTSP relay");

    worker_thread_ = std::thread(&RtspClient::workerThread, this);

    LOG("[RtspClient] RtspClient started,tag=%s,src=%s,target=%s",config_.tag.c_str(), config_.source_url.c_str(), config_.target_url.c_str());

    return true;
}

void RtspClient::stop() {
    if (!running_) return;

    stopping_ = true;
    streaming_ = false;
    running_ = false;

    cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    if (control_thread_.joinable()) {
        control_thread_.join();
    }

    teardown();

    setState(State::IDLE, "Stopped");
    logInfo("RTSP relay stopped");
}

void RtspClient::restart() {
    stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    start(config_);
}

RtspClient::State RtspClient::getState() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

bool RtspClient::isRunning() {
    return running_;
}

RtspClient::Statistics RtspClient::getStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    // 计算实时统计
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - stats_.last_frame_time).count();

    if (elapsed > 0) {
        stats_.fps = stats_.frames_received / elapsed;
        stats_.bitrate = (stats_.bytes_received * 8 / 1000.0) / elapsed;
    }

    return stats_;
}

void RtspClient::setStatusCallback(StatusCallback cb) {
    status_callback_ = cb;
}

void RtspClient::setFrameCallback(FrameCallback cb) {

}

void RtspClient::setErrorCallback(ErrorCallback cb) {
    error_callback_ = cb;
}

// ============================================================================
// 认证相关函数
// ============================================================================

std::string RtspClient::calculateBasicAuth(const AuthInfo& auth) {
    std::string credentials = auth.username + ":" + auth.password;
    return "Basic " + base64Encode(credentials);
}

std::string RtspClient::calculateDigest(const std::string& method, const std::string& uri,
    const AuthInfo& auth) {
    // 计算HA1 = MD5(username:realm:password)
    std::string ha1_input = auth.username + ":" + auth.realm + ":" + auth.password;
    std::string ha1 = md5Hex(ha1_input);

    // 计算HA2 = MD5(method:uri)
    std::string ha2_input = method + ":" + uri;
    std::string ha2 = md5Hex(ha2_input);

    // 计算response = MD5(HA1:nonce:HA2)
    std::string response_input = ha1 + ":" + auth.nonce + ":" + ha2;
    std::string response = md5Hex(response_input);

    return response;
}

bool RtspClient::parseWWWAuthenticate(const std::string& response, AuthInfo& auth) {
    // 查找WWW-Authenticate头
    size_t www_auth_pos = response.find("WWW-Authenticate: ");
    if (www_auth_pos == std::string::npos) {
        return false;
    }

    size_t line_end = response.find("\r\n", www_auth_pos);
    std::string auth_line = response.substr(www_auth_pos + 18, line_end - www_auth_pos - 18);

    logVerbose("WWW-Authenticate: " + auth_line);

    // 检查认证类型
    if (auth_line.find("Digest") == 0) {
        auth.use_digest = true;

        // 解析Digest参数
        size_t realm_pos = auth_line.find("realm=\"");
        if (realm_pos != std::string::npos) {
            size_t realm_end = auth_line.find("\"", realm_pos + 7);
            if (realm_end != std::string::npos) {
                auth.realm = auth_line.substr(realm_pos + 7, realm_end - realm_pos - 7);
            }
        }

        size_t nonce_pos = auth_line.find("nonce=\"");
        if (nonce_pos != std::string::npos) {
            size_t nonce_end = auth_line.find("\"", nonce_pos + 7);
            if (nonce_end != std::string::npos) {
                auth.nonce = auth_line.substr(nonce_pos + 7, nonce_end - nonce_pos - 7);
            }
        }

        size_t algorithm_pos = auth_line.find("algorithm=\"");
        if (algorithm_pos != std::string::npos) {
            size_t algorithm_end = auth_line.find("\"", algorithm_pos + 11);
            if (algorithm_end != std::string::npos) {
                auth.algorithm = auth_line.substr(algorithm_pos + 11, algorithm_end - algorithm_pos - 11);
            }
        }
        else {
            auth.algorithm = "MD5";
        }

        return true;
    }
    else if (auth_line.find("Basic") == 0) {
        auth.use_digest = false;

        size_t realm_pos = auth_line.find("realm=\"");
        if (realm_pos != std::string::npos) {
            size_t realm_end = auth_line.find("\"", realm_pos + 7);
            if (realm_end != std::string::npos) {
                auth.realm = auth_line.substr(realm_pos + 7, realm_end - realm_pos - 7);
            }
        }

        return true;
    }

    return false;
}

void RtspClient::updateAuthHeader(AuthInfo& auth, const std::string& method, const std::string& uri) {
    if (!auth.hasCredentials()) {
        auth.authorization_header.clear();
        return;
    }

    if (auth.use_digest) {
        std::string response = calculateDigest(method, uri, auth);

        std::stringstream auth_header;
        auth_header << "Authorization: Digest "
            << "username=\"" << auth.username << "\", "
            << "realm=\"" << auth.realm << "\", "
            << "nonce=\"" << auth.nonce << "\", "
            << "uri=\"" << uri << "\", "
            << "response=\"" << response << "\"";

        if (!auth.algorithm.empty()) {
            auth_header << ", algorithm=\"" << auth.algorithm << "\"";
        }

        auth.authorization_header = auth_header.str();
    }
    else {
        auth.authorization_header = "Authorization: " + calculateBasicAuth(auth);
    }
}

// ============================================================================
// RTSP协议函数
// ============================================================================

bool RtspClient::rtspDescribe(Connection& conn, const std::string& url,
    std::string& sdp, std::string& session) {
    URLComponents url_components;
    if (!URLComponents::parse(url, url_components)) {
        logError("DESCRIBE failed: invalid url format: " + url);
        return false;
    }

    std::string host_header = url_components.host;
    if (host_header.find(':') != std::string::npos) {
        host_header = "[" + host_header + "]";
    }
    host_header += ":" + std::to_string(url_components.port);

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == config_.source_url || url.find(config_.source_url) == 0) {
        auth_info = &source_auth_;
    }
    else {
        auth_info = &target_auth_;
    }

    auto do_describe = [&](const std::string& request_uri, bool include_host,
        bool use_auth, std::string& response) -> bool {
            std::stringstream request;
            request << "DESCRIBE " << request_uri << " RTSP/1.0\r\n"
                << "CSeq: " << generateCSeq() << "\r\n"
                << "User-Agent: RtspClient/1.0\r\n";

            if (include_host) {
                request << "Host: " << host_header << "\r\n";
            }

            // 添加认证头
            if (use_auth && auth_info && auth_info->hasCredentials()) {
                updateAuthHeader(*auth_info, "DESCRIBE", request_uri);
                if (!auth_info->authorization_header.empty()) {
                    request << auth_info->authorization_header << "\r\n";
                }
            }

            request << "Accept: application/sdp\r\n"
                << "\r\n";

            const std::string req = request.str();
            logVerbose(">> DESCRIBE " + request_uri +
                (include_host ? "" : " (no Host)") +
                (use_auth ? " (with auth)" : ""));

            int sent = conn.send(req.c_str(), req.size());
            if (sent != static_cast<int>(req.size())) {
                logError("DESCRIBE send failed: sent=" + std::to_string(sent) +
                    " err=" + std::to_string(conn.lastError()));
                response.clear();
                return false;
            }

            response.clear();
            int rc = conn.receiveHttpResp(response, 5000);
            if (rc <= 0) {
                logError("DESCRIBE recv failed: rc=" + std::to_string(rc) +
                    " err=" + std::to_string(conn.lastError()));
                response.clear();
                return false;
            }

            if (response.find("200 OK") != std::string::npos) {
                // 成功
                size_t sdp_start = response.find("\r\n\r\n");
                if (sdp_start != std::string::npos) {
                    sdp = response.substr(sdp_start + 4);
                }
                else {
                    sdp.clear();
                }
                // 注意：虽然 RTSP RFC 规定 Session 应该在 SETUP 响应中返回，
                // 但 ZLMediaKit 在 DESCRIBE 响应中也包含 Session。
                // 为了兼容 ZLM，我们需要从 DESCRIBE 响应中提取 Session。
                // 这样在后续的 SETUP 请求中可以带上 Session。
                std::string session_in_response = extractSessionID(response);
                if (!session_in_response.empty()) {
                    session = session_in_response;
                    logVerbose("Session from DESCRIBE: " + session);
                }
                return true;
            }
            else if (response.find("401 Unauthorized") != std::string::npos) {
                // 需要认证
                if (auth_info && auth_info->hasCredentials()) {
                    // 解析WWW-Authenticate头
                    if (parseWWWAuthenticate(response, *auth_info)) {
                        logInfo("Authentication required, retrying with credentials");
                    }
                    else {
                        logError("Failed to parse WWW-Authenticate header");
                    }
                }
                else {
                    logError("Authentication required but no credentials provided");
                }
                return false;
            }
            else {
                logError("DESCRIBE failed: " + response.substr(0, 200));
                return false;
            }
        };

    // 尝试顺序：无认证 -> 带认证
    std::string response;

    // 第一次尝试：不带认证
    if (do_describe(url, true, false, response)) {
        return true;
    }

    // 第二次尝试：带认证（如果提供了凭据）
    if (auth_info && auth_info->hasCredentials()) {
        if (do_describe(url, true, true, response)) {
            return true;
        }
    }

    // 如果上面失败，尝试不带Host头
    if (do_describe(url, false, false, response)) {
        return true;
    }

    // 如果提供了凭据，尝试不带Host头但带认证
    if (auth_info && auth_info->hasCredentials()) {
        if (do_describe(url, false, true, response)) {
            return true;
        }
    }

    // 一些RTSP服务器期望路径格式的URI
    if (!url_components.path.empty() && url_components.path != url) {
        logVerbose("Retry DESCRIBE with path-only URI: " + url_components.path);

        if (do_describe(url_components.path, true, false, response)) {
            return true;
        }

        if (auth_info && auth_info->hasCredentials()) {
            if (do_describe(url_components.path, true, true, response)) {
                return true;
            }
        }
    }

    return false;
}

bool RtspClient::rtspSetup(Connection& conn, const std::string& url,
    std::string& session, StreamInfo& stream, bool record_mode) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == config_.source_url || url.find(config_.source_url) == 0) {
        auth_info = &source_auth_;
    }
    else {
        auth_info = &target_auth_;
    }

    // 根据 ZLM 的 SDP 格式，正确拼接 SETUP URL
    // ZLM SDP: a=control:* 表示 base URL, a=control:streamid=0 表示相对路径
    std::string setup_url = url;
    
    if (!stream.control_url.empty()) {
        if (stream.control_url.rfind("rtsp://", 0) == 0 || 
            stream.control_url.rfind("rtsps://", 0) == 0) {
            // 绝对 URL：直接使用
            setup_url = stream.control_url;
        }
        else if (stream.control_url.front() == '/') {
            // 以 / 开头的绝对路径：rtsp://host:port/path
            URLComponents src_url;
            if (URLComponents::parse(url, src_url)) {
                setup_url = src_url.protocol + "://" + src_url.host + ":" + 
                           std::to_string(src_url.port) + stream.control_url;
            }
        }
        else {
            // 相对路径（如 streamid=0）：拼接到原始 URL 后面
            // ZLM 格式：/stream/1 + streamid=0 = /stream/1/streamid=0
            if (!url.empty()) {
                if (url.back() == '*') {
                    // a=control:* 表示用 base URL
                    setup_url = url.substr(0, url.length() - 1) + stream.control_url;
                }
                else if (url.back() == '/') {
                    setup_url = url + stream.control_url;
                }
                else {
                    setup_url = url + "/" + stream.control_url;
                }
            }
            else {
                setup_url = stream.control_url;
            }
        }
    }

    std::stringstream request;
    request << "SETUP " << setup_url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: RtspClient/1.0\r\n";

    // 注意：SETUP 请求中通常不需要 Host 头，RTSP 服务器通过 URL 获取主机信息
    // 移除 Host 头，避免某些服务器拒绝请求
    (void)host_header;  // 抑制未使用变量警告

    if (!session.empty()) {
        request << "Session: " << session << "\r\n";
    }

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "SETUP", setup_url);
        request << auth_info->authorization_header << "\r\n";
    }

    // UDP传输模式：使用RTP/AVP/UDP
    if (record_mode) {
        // 推流（发送）：服务端接收
        if (config_.push_mode == TransportMode::UDP) {
            request << "Transport: RTP/AVP/UDP;unicast;mode=record;"
                << "client_port=" << stream.client_port;
            if (config_.udp_ttl != 64) {
                request << ";ttl=" << config_.udp_ttl;
            }
            request << "\r\n";
        }
        else {
            // TCP推流
            request << "Transport: RTP/AVP/TCP;unicast;mode=record;interleaved=0-1\r\n";
        }
    }
    else {
        // 拉流（接收）：客户端接收
        if (config_.pull_mode == TransportMode::UDP) {
            request << "Transport: RTP/AVP/UDP;unicast;"
                << "client_port=" << stream.client_port;
            if (config_.udp_ttl != 64) {
                request << ";ttl=" << config_.udp_ttl;
            }
            request << "\r\n";
        }
    else {
        // TCP拉流 - 尝试多种格式
        // 格式1: 标准 RFC 格式
        request << "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n";
    }
    }

    request << "\r\n";

    const std::string req = request.str();
    // 打印完整请求内容，便于调试
    std::string req_for_log = req;
    std::replace(req_for_log.begin(), req_for_log.end(), '\r', '~');
    std::replace(req_for_log.begin(), req_for_log.end(), '\n', '~');
    logVerbose(">> SETUP REQUEST:\n" + req_for_log);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("SETUP send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    // 增加超时时间，因为 ZLM 可能延迟发送 SETUP 响应
    int rc = conn.receiveHttpResp(response, 10000);
    if (rc <= 0) {
        // 调试：打印收到的原始数据
        if (!response.empty()) {
            std::string resp_for_log = response;
            std::replace(resp_for_log.begin(), resp_for_log.end(), '\r', '~');
            std::replace(resp_for_log.begin(), resp_for_log.end(), '\n', '~');
            logVerbose("<< SETUP PARTIAL DATA: " + resp_for_log);
        }
        logError("SETUP recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    // 调试：打印收到的响应
    {
        std::string resp_for_log = response;
        std::replace(resp_for_log.begin(), resp_for_log.end(), '\r', '~');
        std::replace(resp_for_log.begin(), resp_for_log.end(), '\n', '~');
        logVerbose("<< SETUP RESPONSE (rc=" + std::to_string(rc) + "): " + resp_for_log);
    }

    if (response.find("200 OK") == std::string::npos) {
        if (response.find("401 Unauthorized") != std::string::npos) {
            logError("SETUP authentication failed");
        }
        else {
            logError("SETUP failed: " + response.substr(0, 200));
        }
        return false;
    }

    stream.transport = extractTransport(response);

    std::string new_session = extractSessionID(response);
    if (!new_session.empty() && session.empty()) {
        session = new_session;
    }

    return true;
}

bool RtspClient::rtspPlay(Connection& conn, const std::string& url,
    const std::string& session) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == config_.source_url || url.find(config_.source_url) == 0) {
        auth_info = &source_auth_;
    }
    else {
        auth_info = &target_auth_;
    }

    std::stringstream request;
    request << "PLAY " << url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: RtspClient/1.0\r\n"
        << (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "PLAY", url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Session: " << session << "\r\n"
        << "Range: npt=0.000-\r\n"
        << "\r\n";

    const std::string req = request.str();
    logVerbose(">> PLAY " + url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("PLAY send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    int rc = conn.receiveHttpResp(response, 5000);
    if (rc <= 0) {
        logError("PLAY recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    if (response.find("200 OK") == std::string::npos) {
        logError("PLAY failed: " + response.substr(0, 200));
        return false;
    }

    return true;
}

bool RtspClient::rtspTeardown(Connection& conn, const std::string& url,
    const std::string& session) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == config_.source_url || url.find(config_.source_url) == 0) {
        auth_info = &source_auth_;
    }
    else {
        auth_info = &target_auth_;
    }

    std::stringstream request;
    request << "TEARDOWN " << url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: RtspClient/1.0\r\n"
        << (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "TEARDOWN", url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Session: " << session << "\r\n"
        << "\r\n";

    const std::string req = request.str();
    logVerbose(">> TEARDOWN " + url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("TEARDOWN send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    conn.receiveHttpResp(response, 5000);

    return true;
}

bool RtspClient::rtspAnnounce(Connection& conn, const std::string& url,
    const std::string& sdp, std::string& session) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == config_.target_url || url.find(config_.target_url) == 0) {
        auth_info = &target_auth_;
    }

    std::stringstream request;
    request << "ANNOUNCE " << url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: RtspClient/1.0\r\n"
        << (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "ANNOUNCE", url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Content-Type: application/sdp\r\n"
        << "Content-Length: " << sdp.size() << "\r\n"
        << "\r\n"
        << sdp;

    const std::string req = request.str();
    logVerbose(">> ANNOUNCE " + url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("ANNOUNCE send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    int rc = conn.receiveHttpResp(response, 5000);
    if (rc <= 0) {
        logError("ANNOUNCE recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    if (response.find("200 OK") == std::string::npos) {
        logError("ANNOUNCE failed: " + response.substr(0, 200));
        return false;
    }

    session = extractSessionID(response);

    return true;
}

bool RtspClient::rtspRecord(Connection& conn, const std::string& url,
    const std::string& session) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == config_.target_url || url.find(config_.target_url) == 0) {
        auth_info = &target_auth_;
    }

    std::stringstream request;
    request << "RECORD " << url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: RtspClient/1.0\r\n"
        << (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "RECORD", url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Session: " << session << "\r\n"
        << "Range: npt=0.000-\r\n"
        << "\r\n";

    const std::string req = request.str();
    logVerbose(">> RECORD " + url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("RECORD send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    int rc = conn.receiveHttpResp(response, 5000);
    if (rc <= 0) {
        logError("RECORD recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    if (response.find("200 OK") == std::string::npos) {
        logError("RECORD failed: " + response.substr(0, 200));
        return false;
    }

    return true;
}

bool RtspClient::rtspGetParameter(Connection& conn, const std::string& url,
    const std::string& session) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == config_.source_url || url.find(config_.source_url) == 0) {
        auth_info = &source_auth_;
    }
    else {
        auth_info = &target_auth_;
    }

    std::stringstream request;
    request << "GET_PARAMETER " << url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: RtspClient/1.0\r\n"
        << (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "GET_PARAMETER", url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Session: " << session << "\r\n"
        << "Content-Length: 0\r\n"
        << "\r\n";

    const std::string req = request.str();
    logVerbose(">> GET_PARAMETER " + url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("GET_PARAMETER send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    int rc = conn.receiveHttpResp(response, 5000);
    if (rc <= 0) {
        logError("GET_PARAMETER recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    if (response.find("200 OK") == std::string::npos) {
        logError("GET_PARAMETER failed: " + response.substr(0, 200));
        return false;
    }

    return true;
}

// ============================================================================
// 工作线程
// ============================================================================

int RtspClient::getBufferedSeconds()
{
    // 计算当前 rtp_buffer_ 中的数据覆盖的大致秒数
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (rtp_buffer_.empty()) return 0;

    // 以最早包和最新包的 RTP timestamp 差值为基准计算（无符号，支持回绕）
    uint32_t newest_ts = rtp_buffer_.back()->timestamp;
    uint32_t oldest_ts = rtp_buffer_.front()->timestamp;
    uint32_t diff = newest_ts - oldest_ts; // 无符号差值，处理 32 位回绕

    // 获取时钟频率（ticks per second），RTCP/SDP 中给出，视频常见为 90000
    uint32_t clock = (source_video_info_.clock_rate > 0) ? static_cast<uint32_t>(source_video_info_.clock_rate) : 90000u;
    if (clock == 0) clock = 90000u;

    // 整数秒（截断子秒）。如果存在刻度差但小于1秒，返回1以提示非空缓冲
    int seconds = static_cast<int>(diff / clock);
    if (diff > 0 && seconds == 0) seconds = 1;
    return seconds;
}

void RtspClient::addToRtpBuffer(RTPPacket* pPkt)
{
    // 校验输入
    if (!pPkt) return;

    // 将传入的 RTPPacket 拷贝到内部堆上。调用者可能在此函数返回后释放其指针，
    // 因此内部保存一份拷贝以保证缓冲区数据的有效性。
    RTPPacket* copy = new RTPPacket(*pPkt);

    // 使用队列互斥锁保护对 rtp_buffer_ 的访问，因为此函数可能由 RTP 接收
    // 线程调用，同时其他线程可能读取或修剪缓冲区。
    std::lock_guard<std::mutex> lock(queue_mutex_);
    rtp_buffer_.push_back(copy);

    // 根据 rtp_buffer_max_seconds_ 修剪缓冲区，尽量保持约定秒数的媒体数据。
    // 这里使用 RTP timestamp 来估算时长（timestamp 表示媒体时钟刻度，不是
    // wall-clock 时间）。
    //
    // 算法说明：
    // 1) 以最新包的 timestamp 为参考基准。
    // 2) 从最旧包开始，计算无符号差值 (newest_ts - oldest_ts)。采用无符号运算
    //    可以在大多数场景下正确处理 32 位 timestamp 的回绕（wrap-around）。
    // 3) 用媒体时钟频率（ticks/秒）将刻度差转换为秒：seconds = diff / clock。
    //    这里使用整数除法，结果为整秒（会截断子秒部分），如果需要子秒精度可
    //    使用浮点运算。
    if (rtp_buffer_max_seconds_ > 0 && !rtp_buffer_.empty()) {
        uint32_t newest_ts = rtp_buffer_.back()->timestamp;

        // 获取时钟频率（每秒刻度数），若 SDP 未提供则默认使用常见的视频值 90000Hz。
        uint32_t clock = (source_video_info_.clock_rate > 0) ?
            static_cast<uint32_t>(source_video_info_.clock_rate) : 90000u;
        if (clock == 0) clock = 90000u;

        // 当最旧包到最新包的时间差超过配置的秒数窗口时，逐个删除最旧包。
        while (!rtp_buffer_.empty()) {
            RTPPacket* oldest = rtp_buffer_.front();
            uint32_t oldest_ts = oldest->timestamp;

            // 无符号相减可以按模 2^32 处理 timestamp 回绕情况。
            uint32_t diff = newest_ts - oldest_ts;

            // 将刻度差转换为整秒（会截断小数部分）。
            uint32_t seconds = diff / clock;

            if (seconds > static_cast<uint32_t>(rtp_buffer_max_seconds_)) {
                // 包已超出时间窗口 -> 释放并从缓冲中移除。
                delete oldest;
                rtp_buffer_.erase(rtp_buffer_.begin());
            }
            else {
                // 当前最旧包在窗口内，停止修剪。
                break;
            }
        }
    }

    // 额外的保护：强制限制缓冲包数量到 max_queue_size_，以防 timestamp
    // 逻辑失效或时钟信息错误导致内存无限增长。
    while (rtp_buffer_.size() > max_queue_size_) {
        delete rtp_buffer_.front();
        rtp_buffer_.erase(rtp_buffer_.begin());
    }
}

void RtspClient::workerThread() {
    while (running_ && !stopping_) {
            if (!doStreamPull()) {
                teardown();
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            }

            if (config_.target_url == "")
                return true;
            if (!setupStreams()) {
                streaming_ = false;
                cv_.notify_all();
                teardown();

                if (shouldReconnect()) {
                    doReconnect();
                    continue;
                }
                else {
                    break;
                }
            }

            // 启动RTP接收线程
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.last_frame_time = std::chrono::steady_clock::now();
            }
            streaming_ = true;

            RtspClient::doRtpRecv();
            //control_thread_ = std::thread(&RtspClient::controlThread, this);
    }
}

bool RtspClient::doStreamPush() {
    // 连接到目标服务器
    URLComponents target_url;
    if (!URLComponents::parse(config_.target_url, target_url)) {
        setError("Invalid target URL format", 3005);
        return false;
    }

    target_conn_ = std::make_unique<Connection>();
    if (!target_conn_->connect(target_url.host, target_url.port)) {
        setError("Failed to connect to target server: " + target_url.host + ":" + std::to_string(target_url.port) +
            " (err=" + std::to_string(target_conn_->lastError()) +
            "). Is an RTSP server listening on that port?",
            3006);
        return false;
    }

    // 生成目标SDP
    target_video_info_ = source_video_info_;
    target_rtp_port_ = 0;
    {
        std::string track_control = "trackID=0";
        if (!source_video_info_.control_url.empty()) {
            std::string src = source_video_info_.control_url;
            auto pos = src.find_last_of('/');
            track_control = (pos == std::string::npos) ? src : src.substr(pos + 1);
            if (track_control.empty() || track_control == "*" ||
                track_control.rfind("rtsp://", 0) == 0 ||
                track_control.rfind("rtsps://", 0) == 0) {
                track_control = "trackID=0";
            }
        }
        target_video_info_.control_url = track_control;
    }

    std::string target_sdp = generateSDP(target_video_info_, StreamInfo());

    // 发送ANNOUNCE到目标
    if (!rtspAnnounce(*target_conn_, config_.target_url, target_sdp, target_session_)) {
        setError("ANNOUNCE failed", 3007);
        return false;
    }

    // 发送SETUP到目标
    if (!rtspSetup(*target_conn_, config_.target_url, target_session_, target_video_info_, true)) {
        setError("SETUP failed for target", 3008);
        return false;
    }

    // 解析目标服务器端口
    {
        logInfo("Target SETUP Transport: " + target_video_info_.transport);
        const std::string key = "server_port=";
        size_t pos = target_video_info_.transport.find(key);
        if (pos != std::string::npos) {
            pos += key.size();
            while (pos < target_video_info_.transport.size() &&
                (target_video_info_.transport[pos] == ' ' || target_video_info_.transport[pos] == '\t')) {
                ++pos;
            }
            int port = 0;
            while (pos < target_video_info_.transport.size() &&
                target_video_info_.transport[pos] >= '0' && target_video_info_.transport[pos] <= '9') {
                port = port * 10 + (target_video_info_.transport[pos] - '0');
                ++pos;
            }
            if (port > 0 && port <= 65535) {
                target_rtp_port_ = port;
            }
        }
    }

    logInfo("Target RTP port: " + std::to_string(target_rtp_port_));
    if (target_rtp_port_ == 0) {
        setError("Missing/invalid server_port in target Transport: " + target_video_info_.transport, 3010);
        return false;
    }

    // 保存目标RTP地址信息（用于UDP推流）
    target_rtp_host_ = target_url.host;
    logInfo("Target RTP host: " + target_rtp_host_);

    // 根据推流模式决定是否创建UDP socket
    if (config_.push_mode == TransportMode::UDP) {
        if (!createUDPPushSocket()) {
            logError("Failed to create UDP push socket, falling back to TCP");
            config_.push_mode = TransportMode::TCP;
        }
        else {
            logInfo("Push stream: Using UDP mode");
        }
    }

    if (config_.push_mode == TransportMode::TCP) {
        logInfo("Push stream: Using TCP mode (RTP over RTSP)");
    }


    // 发送RECORD到目标
    if (!rtspRecord(*target_conn_, config_.target_url, target_session_)) {
        setError("RECORD failed", 3009);
        return false;
    }

    return true;
}

bool RtspClient::doStreamPull() {
    setState(State::CONNECTING, "Connecting to source");

    // 解析源URL
    URLComponents src_url;
    if (!URLComponents::parse(config_.source_url, src_url)) {
        setError("Invalid source URL format", 2001);
        return false;
    }

    // 连接到源服务器
    source_conn_ = std::make_unique<Connection>();
    if (!source_conn_->connect(src_url.host, src_url.port)) {
        setError("Failed to connect to source server: " + src_url.host + ":" + std::to_string(src_url.port), 2002);
        return false;
    }

    LOG("[RtspClient]Connect to source success," + src_url.host + ":" + std::to_string(src_url.port));

    // 发送DESCRIBE
    std::string sdp;
    if (!rtspDescribe(*source_conn_, config_.source_url, sdp, source_session_)) {
        setError("DESCRIBE failed", 2003);
        return false;
    }

    // 解析SDP
    if (!parseSDP(sdp, source_video_info_, source_audio_info_)) {
        setError("Failed to parse SDP", 2004);
        return false;
    }

    // 调试：打印收到的 SDP 内容
    std::string sdp_for_log = sdp;
    std::replace(sdp_for_log.begin(), sdp_for_log.end(), '\r', '~');
    std::replace(sdp_for_log.begin(), sdp_for_log.end(), '\n', '~');
    LOG("[RtspClient] sdp received: " + sdp_for_log + 
        ",streamInfo:" + source_video_info_.control_url + 
        ",audio_control:" + source_audio_info_.control_url);

    setState(State::CONNECTED, "Source connected");
    setState(State::CONNECTING, "Setting up streams");

    // 清理之前的UDP sockets
    closeUDPSockets();

    // 根据拉流模式决定是否创建UDP socket
    bool pullUseUDP = (config_.pull_mode == TransportMode::UDP);

    if (pullUseUDP) {
        // 创建专用的UDP socket用于拉流（接收RTP）
        if (!createUDPPullSocket()) {
            logError("Failed to create UDP pull socket");
            pullUseUDP = false;
        }
    }

    if (pullUseUDP) {
        logInfo("Pull stream: Using UDP mode");
        logInfo("Local UDP port: " + std::to_string(local_rtp_port_));
        source_video_info_.client_port = std::to_string(local_rtp_port_) + "-" + std::to_string(local_rtcp_port_);
        logInfo("Source SETUP client_port: " + source_video_info_.client_port);
    }
    else {
        logInfo("Pull stream: Using TCP mode (interleaved)");
        source_video_info_.client_port = "0-0";  // TCP模式不需要client_port
    }

    // 发送SETUP到源
    if (!rtspSetup(*source_conn_, config_.source_url, source_session_, source_video_info_)) {
        setError("SETUP failed for source", 3003);
        return false;
    }

    // 解析传输信息
    std::istringstream transport_stream(source_video_info_.transport);
    std::string token;
    while (std::getline(transport_stream, token, ';')) {
        if (token.find("server_port=") != std::string::npos) {
            size_t pos = token.find('=');
            source_video_info_.server_port = token.substr(pos + 1);

            // 解析RTP端口
            size_t dash = source_video_info_.server_port.find('-');
            if (dash != std::string::npos) {
                source_video_info_.source_rtp_port = std::stoi(source_video_info_.server_port.substr(0, dash));
            }
        }
        else if (token.find("source=") != std::string::npos) {
            size_t pos = token.find('=');
            source_video_info_.source_host = token.substr(pos + 1);
        }
    }

    // 发送PLAY
    if (!rtspPlay(*source_conn_, config_.source_url, source_session_)) {
        setError("PLAY failed", 3004);
        return false;
    }

    return true;
}

void RtspClient::doRtpRecv() {
    setState(State::PLAYING, "Streaming started");
    bool pullUDP = (config_.pull_mode == TransportMode::UDP);
    
    if (pullUDP) {
        logInfo("Pull thread started (UDP mode)");
    }
    else {
        logInfo("Pull thread started (TCP/interleaved mode)");
    }

    std::vector<uint8_t> buffer(config_.buffer_size);
    std::string src_ip;
    int src_port = 0;

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.last_frame_time = std::chrono::steady_clock::now();
    }

    // TCP拉流模式下的状态
    int tcpRtpChannel = 0;
    int tcpRtcpChannel = 1;
    std::vector<uint8_t> tcpBuffer;
    bool waitingForRtpData = true;

    while (running_ && !stopping_ && streaming_) {
        int received = 0;
        
        if (pullUDP) {
            // UDP拉流 最多阻塞1秒 configureUDPSocket 中设置了1秒超时
            received = receiveUDPData(buffer.data(), buffer.size(), src_ip, src_port);
        }
        else {
            // TCP拉流：通过RTSP连接接收RTP数据
            if (!source_conn_ || !source_conn_->isConnected()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // 接收数据
            char tmpBuf[2048] = {0};
            int n = source_conn_->receive(tmpBuf, sizeof(tmpBuf), 100);
            
            if (n > 0) {
                // 添加到缓冲区
                tcpBuffer.insert(tcpBuffer.end(), (uint8_t*)tmpBuf, (uint8_t*)tmpBuf + n);
                
                // 处理RTP包 ( interleaved = $ + channel + len + data )
                while (tcpBuffer.size() >= 4) {
                    if (tcpBuffer[0] != 0x24) {
                        // 不是interleaved标记，跳过
                        tcpBuffer.erase(tcpBuffer.begin());
                        continue;
                    }
                    
                    int channel = tcpBuffer[1];
                    int len = (tcpBuffer[2] << 8) | tcpBuffer[3];
                    
                    if (tcpBuffer.size() < 4 + len) {
                        // 数据不完整，等待更多数据
                        break;
                    }
                    
                    // 检查是否是RTP数据 (channel 0)
                    if (channel == tcpRtpChannel) {
                        // 复制RTP数据
                        memcpy(buffer.data(), &tcpBuffer[4], len);
                        received = len;
                    }
                    
                    // 移除已处理的数据
                    tcpBuffer.erase(tcpBuffer.begin(), tcpBuffer.begin() + 4 + len);
                }
            }
            else if (n < 0) {
                // 接收错误
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (received > 12) {  // RTP包最小12字节头
            const auto now = std::chrono::steady_clock::now();

            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_received += received;
                stats_.frames_received++;
                stats_.last_frame_time = now;
            }

            // 解析RTP包
            RTPPacket* pPkt = new RTPPacket();
            RTPPacket& packet = *pPkt;
            if (packet.parse(buffer.data(), received)) {
                // 放入缓存
                addToRtpBuffer(pPkt);

                // 转发到目标
                if (config_.push_mode != TransportMode::NONE) {
                    forwardRTPPacket(packet);
                }

                // 录制到磁盘
                if (rec_ctrl_.recording) {
                    if (rec_ctrl_.firstWrite) {
                        //从rtp_buffer_取出rec_ctrl_.preSeconds的数据并录制
                        std::vector<RTPPacket*> pre_packets;
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            for (auto it = rtp_buffer_.rbegin(); it != rtp_buffer_.rend(); ++it) {
                                if (packet.timestamp - (*it)->timestamp <= rec_ctrl_.preSeconds * 90000) {
                                    pre_packets.push_back(*it);
                                }
                                else {
                                    break;
                                }
                            }
						}
						for (auto it = pre_packets.rbegin(); it != pre_packets.rend(); ++it) {
                            recordRTPPacket(*it);
                        }
                    }
                    else
					    recordRTPPacket(pPkt);
                }

                logVerbose((pullUDP ? "UDP" : "TCP") + std::string("->RTP: seq=") + 
                    std::to_string(packet.sequence_number) +
                    " ts=" + std::to_string(packet.timestamp) +
                    " size=" + std::to_string(received));
            }
        }
        else if (received > 0 && received<=12) {
            logVerbose("wrong recv len");
        }
        else {
            // 检查RTP超时
            auto now = std::chrono::steady_clock::now();
            std::chrono::steady_clock::time_point last_frame_time;
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                last_frame_time = stats_.last_frame_time;
            }

            if (now - last_frame_time > std::chrono::milliseconds(config_.rtp_timeout)) {
                logError("RTP timeout detected");
                setError("RTP timeout", 1001);
                break;
            }
        }
    }

    logInfo("Pull thread stopped");
}

void RtspClient::controlThread() {
    logInfo("Control thread started");

    while (running_ && !stopping_) {
        bool should_exit = false;
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            cv_.wait_for(lock, std::chrono::seconds(5), [&]() {
                return !running_ || stopping_ || !streaming_ || state_ == State::S_ERROR;
                });
            should_exit = !running_ || stopping_ || !streaming_ || state_ == State::S_ERROR;
        }
        if (should_exit) {
            break;
        }

        // 发送保活消息
        if (source_conn_ && !source_session_.empty()) {
            if (!rtspGetParameter(*source_conn_, config_.source_url, source_session_)) {
                setError("Source RTSP keepalive failed", 1002);
                break;
            }
        }

        if (target_conn_ && !target_session_.empty()) {
            if (!rtspGetParameter(*target_conn_, config_.target_url, target_session_)) {
                setError("Target RTSP keepalive failed", 1002);
                break;
            }
        }

        logVerbose("Control thread heartbeat");
    }

    logInfo("Control thread stopped");
}

void RtspClient::teardown() {
    if (source_conn_ && !source_session_.empty()) {
        rtspTeardown(*source_conn_, config_.source_url, source_session_);
    }

    if (target_conn_ && !target_session_.empty()) {
        rtspTeardown(*target_conn_, config_.target_url, target_session_);
    }

    if (source_conn_) {
        source_conn_->disconnect();
    }

    if (target_conn_) {
        target_conn_->disconnect();
    }

    source_session_.clear();
    target_session_.clear();
    target_rtp_port_ = 0;
    target_rtp_host_.clear();

    // 清除认证信息（保留用户名密码）
    source_auth_.realm.clear();
    source_auth_.nonce.clear();
    source_auth_.authorization_header.clear();

    target_auth_.realm.clear();
    target_auth_.nonce.clear();
    target_auth_.authorization_header.clear();

    // 关闭UDP sockets
    closeUDPSockets();
}

// ============================================================================
// UDP传输函数
// ============================================================================

bool RtspClient::createUDPPullSocket() {
    // 我们需要为 RTP 和 RTCP 创建一对连续的端口以便向服务器声明 "client_port=RTP-RTCP"
    // 尝试多次分配以找到一对可用的连续端口
    const int max_attempts = 10;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // 创建RTP socket并绑定到系统分配的端口（0）
        SocketHandle rtp_sock = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
        if (rtp_sock == kInvalidSocket) {
            logError("Failed to create UDP pull socket (rtp)");
            return false;
        }

        if (!configureUDPSocket(rtp_sock, false)) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            continue;
        }

        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        local_addr.sin_port = htons(0); // 让系统分配端口

        if (::bind(static_cast<SOCKET_TYPE>(rtp_sock), (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            continue;
        }

        // 获取分配的端口
        socklen_t len = sizeof(local_addr);
        if (getsockname(static_cast<SOCKET_TYPE>(rtp_sock), (struct sockaddr*)&local_addr, &len) != 0) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            continue;
        }

        int rtp_port = ntohs(local_addr.sin_port);
        int rtcp_port = rtp_port + 1;

        // 创建RTCP socket并绑定到 rtp_port + 1
        SocketHandle rtcp_sock = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
        if (rtcp_sock == kInvalidSocket) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            continue;
        }

        if (!configureUDPSocket(rtcp_sock, false)) {
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
            CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_sock));
            continue;
        }

        struct sockaddr_in rtcp_addr;
        memset(&rtcp_addr, 0, sizeof(rtcp_addr));
        rtcp_addr.sin_family = AF_INET;
        rtcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        rtcp_addr.sin_port = htons(rtcp_port);

        if (::bind(static_cast<SOCKET_TYPE>(rtcp_sock), (struct sockaddr*)&rtcp_addr, sizeof(rtcp_addr)) == 0) {
            // 成功获取到一对连续端口
            udp_pull_socket_ = rtp_sock;
            rtcp_socket_ = rtcp_sock;
            local_rtp_port_ = rtp_port;
            local_rtcp_port_ = rtcp_port;

            logInfo("UDP pull sockets created: rtp_fd=" + std::to_string(udp_pull_socket_) +
                    " rtp_port=" + std::to_string(local_rtp_port_) +
                    " rtcp_fd=" + std::to_string(rtcp_socket_) +
                    " rtcp_port=" + std::to_string(local_rtcp_port_));
            return true;
        }

        // 如果绑定失败，释放并重试（可能下一个端口被占用）
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_sock));
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
    }

    logError("Failed to create consecutive UDP pull sockets for RTP/RTCP");
    udp_pull_socket_ = kInvalidSocket;
    rtcp_socket_ = kInvalidSocket;
    local_rtp_port_ = 0;
    local_rtcp_port_ = 0;
    return false;
}

bool RtspClient::createUDPPushSocket() {
    // 创建UDP推流socket（发送RTP数据到目标）
    udp_push_socket_ = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
    if (udp_push_socket_ == kInvalidSocket) {
        logError("Failed to create UDP push socket");
        return false;
    }

    // 配置推流socket
    if (!configureUDPSocket(udp_push_socket_, false)) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(udp_push_socket_));
        udp_push_socket_ = kInvalidSocket;
        return false;
    }

    logInfo("UDP push socket created: fd=" + std::to_string(udp_push_socket_));
    return true;
}

void RtspClient::closeUDPSockets() {
    if (udp_pull_socket_ != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(udp_pull_socket_));
        udp_pull_socket_ = kInvalidSocket;
    }
    if (udp_push_socket_ != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(udp_push_socket_));
        udp_push_socket_ = kInvalidSocket;
    }
    if (rtcp_socket_ != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_socket_));
        rtcp_socket_ = kInvalidSocket;
    }
    local_rtp_port_ = 0;
    local_rtcp_port_ = 0;
}

bool RtspClient::configureUDPSocket(SocketHandle sock, bool is_multicast) {
    if (sock == kInvalidSocket) return false;

#ifdef _WIN32
    DWORD tv = 1000;  // 1秒超时
    if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_RCVTIMEO, 
                   (const char*)&tv, sizeof(tv)) != 0) {
        logError("Failed to set UDP socket timeout");
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_RCVTIMEO, 
                   (const char*)&tv, sizeof(tv)) != 0) {
        logError("Failed to set UDP socket timeout");
        return false;
    }
#endif

    // 设置TTL
    if (config_.udp_ttl > 0) {
        int ttl = config_.udp_ttl;
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), IPPROTO_IP, IP_TTL,
                       (const char*)&ttl, sizeof(ttl)) != 0) {
            logError("Failed to set UDP TTL");
        }
    }

    // 设置ToS
    if (config_.udp_tos > 0) {
        int tos = config_.udp_tos;
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), IPPROTO_IP, IP_TOS,
                       (const char*)&tos, sizeof(tos)) != 0) {
            logError("Failed to set UDP ToS");
        }
    }

    // 设置组播回环
    if (is_multicast) {
        char loop = config_.udp_multicast_loop ? 1 : 0;
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), IPPROTO_IP, IP_MULTICAST_LOOP,
                       &loop, sizeof(loop)) != 0) {
            logError("Failed to set UDP multicast loop");
        }
    }

    // 设置接收缓冲区大小
    if (config_.udp_recv_buffer_size > 0) {
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_RCVBUF,
                       (const char*)&config_.udp_recv_buffer_size, sizeof(config_.udp_recv_buffer_size)) != 0) {
            logError("Failed to set UDP recv buffer size");
        }
    }

    // 设置发送缓冲区大小
    if (config_.udp_send_buffer_size > 0) {
        if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_SNDBUF,
                       (const char*)&config_.udp_send_buffer_size, sizeof(config_.udp_send_buffer_size)) != 0) {
            logError("Failed to set UDP send buffer size");
        }
    }

    // 允许地址重用（用于快速重启）
    int reuse = 1;
    if (setsockopt(static_cast<SOCKET_TYPE>(sock), SOL_SOCKET, SO_REUSEADDR,
                   (const char*)&reuse, sizeof(reuse)) != 0) {
        logError("Failed to set UDP socket reuse");
    }

    return true;
}

bool RtspClient::sendUDPData(const uint8_t* data, size_t size) {
    if (udp_push_socket_ == kInvalidSocket || target_rtp_port_ == 0) {
        return false;
    }

    // 解析目标地址
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(target_rtp_port_);

    if (inet_pton(AF_INET, target_rtp_host_.c_str(), &target_addr.sin_addr) <= 0) {
        struct hostent* server = gethostbyname(target_rtp_host_.c_str());
        if (!server) {
            logError("Failed to resolve target host: " + target_rtp_host_);
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.errors++;
            return false;
        }
        memcpy(&target_addr.sin_addr, server->h_addr, server->h_length);
    }

    // 发送UDP数据
    int sent = sendto(static_cast<SOCKET_TYPE>(udp_push_socket_),
                      (const char*)data, (int)size, 0,
                      (struct sockaddr*)&target_addr, sizeof(target_addr));

    if (sent != static_cast<int>(size)) {
#ifdef _WIN32
        int err = WSAGetLastError();
#else
        int err = errno;
#endif
        logVerbose("UDP sendto error: sent=" + std::to_string(sent) + 
                   " expected=" + std::to_string(size) + 
                   " err=" + std::to_string(err));
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.errors++;
        return false;
    }

    return true;
}

int RtspClient::receiveUDPData(uint8_t* buffer, size_t size, std::string& src_ip, int& src_port) {
    if (udp_pull_socket_ == kInvalidSocket) {
        return -1;
    }

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    int received = recvfrom(static_cast<SOCKET_TYPE>(udp_pull_socket_),
                             (char*)buffer, (int)size, 0,
                             (struct sockaddr*)&from_addr, &from_len);

    if (received > 0) {
        char ip_str[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
        src_ip = ip_str;
        src_port = ntohs(from_addr.sin_port);
        return received;
    }
    else if (received < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAETIMEDOUT && err != WSAEWOULDBLOCK && err != WSAECONNRESET) {
            logError("UDP recvfrom error: " + std::to_string(err));
        }
#else
        int err = errno;
        if (err != EAGAIN && err != EWOULDBLOCK) {
            logError("UDP recvfrom error: " + std::to_string(err));
        }
#endif
    }

    return received;
}

// ============================================================================
// SDP处理函数
// ============================================================================

bool RtspClient::parseSDP(const std::string & sdp, StreamInfo & video_info, StreamInfo & audio_info) {
    std::istringstream ss(sdp);
    std::string line;
    StreamInfo* current_info = nullptr;

    while (std::getline(ss, line)) {
        if (line.length() < 2 || line[1] != '=') continue;

        char type = line[0];
        std::string value = line.substr(2, line.length() - 3);

        switch (type) {
        case 'm': {
            std::istringstream mstream(value);
            std::string media_type, port_str, proto, fmt;
            mstream >> media_type >> port_str >> proto >> fmt;

            if (media_type == "video") {
                current_info = &video_info;
                video_info.payload_type = std::stoi(fmt);
            }
            else if (media_type == "audio") {
                current_info = &audio_info;
                audio_info.payload_type = std::stoi(fmt);
            }
            else {
                current_info = nullptr;
            }
            break;
        }

        case 'a':
            if (!current_info) break;

            if (value.find("rtpmap:") == 0) {
                size_t colon = value.find(':');
                size_t space = value.find(' ', colon);
                size_t slash = value.find('/', space);

                if (slash != std::string::npos) {
                    std::string codec_str = value.substr(space + 1, slash - space - 1);
                    current_info->codec = codec_str;

                    std::string rate_str = value.substr(slash + 1);
                    size_t second_slash = rate_str.find('/');
                    if (second_slash != std::string::npos) {
                        rate_str = rate_str.substr(0, second_slash);
                    }
                    current_info->clock_rate = std::stoi(rate_str);
                }
            }
            else if (value.find("fmtp:") == 0) {
                size_t fmtp_start = value.find(' ');
                if (fmtp_start != std::string::npos) {
                    current_info->fmtp = value.substr(fmtp_start + 1);

                    // 解析 sprop-parameter-sets（如果存在），格式类似：sprop-parameter-sets=Z0IAH5WoFAFuQA==,aM48gA==
                    size_t sprop_pos = current_info->fmtp.find("sprop-parameter-sets=");
                    if (sprop_pos != std::string::npos) {
                        size_t start = sprop_pos + strlen("sprop-parameter-sets=");
                        size_t end = current_info->fmtp.find(';', start);
                        std::string sprop = (end == std::string::npos) ? current_info->fmtp.substr(start) : current_info->fmtp.substr(start, end - start);

                        // 去掉可能的空格
                        while (!sprop.empty() && sprop.front() == ' ') sprop.erase(sprop.begin());

                        // sprop 通常为 base64_sps,base64_pps
                        size_t comma = sprop.find(',');
                        if (comma != std::string::npos) {
                            std::string sps_b64 = sprop.substr(0, comma);
                            std::string pps_b64 = sprop.substr(comma + 1);
                            auto sps_dec = base64Decode(sps_b64);
                            auto pps_dec = base64Decode(pps_b64);
                            if (!sps_dec.empty()) current_info->sps = std::move(sps_dec);
                            if (!pps_dec.empty()) current_info->pps = std::move(pps_dec);
                        }
                    }
                }
            }
            else if (value.find("control:") == 0) {
                current_info->control_url = value.substr(8);
            }
            break;
        }
    }

    return true;
}

std::string RtspClient::generateSDP(const StreamInfo & video_info, const StreamInfo & audio_info) {
    std::stringstream sdp;

    sdp << "v=0\r\n"
        << "o=- 0 0 IN IP4 0.0.0.0\r\n"
        << "s=RTSP Relay Stream\r\n"
        << "c=IN IP4 0.0.0.0\r\n"
        << "t=0 0\r\n"
        << "a=control:*\r\n";

    if (video_info.payload_type > 0) {
        sdp << "m=video 0 RTP/AVP " << video_info.payload_type << "\r\n"
            << "a=rtpmap:" << video_info.payload_type << " "
            << video_info.codec << "/" << video_info.clock_rate << "\r\n";

        if (!video_info.fmtp.empty()) {
            sdp << "a=fmtp:" << video_info.payload_type << " " << video_info.fmtp << "\r\n";
        }

        sdp << "a=control:" << video_info.control_url << "\r\n";
    }

    return sdp.str();
}

// ============================================================================
// 工具函数
// ============================================================================

bool RtspClient::URLComponents::parse(const std::string & url, URLComponents & components) {
    URLComponents out;

    size_t protocol_end = url.find("://");
    if (protocol_end == std::string::npos) return false;

    out.protocol = url.substr(0, protocol_end);
    std::string rest = url.substr(protocol_end + 3);

    size_t slash_pos = rest.find('/');
    std::string authority = (slash_pos == std::string::npos) ? rest : rest.substr(0, slash_pos);
    out.path = (slash_pos == std::string::npos) ? "/" : rest.substr(slash_pos);

    size_t at_pos = authority.rfind('@');
    std::string host_port = (at_pos == std::string::npos) ? authority : authority.substr(at_pos + 1);
    if (host_port.empty()) return false;

    auto default_port = [&out]() -> int {
        if (out.protocol == "rtsp") return 554;
        if (out.protocol == "rtsps") return 322;
        if (out.protocol == "http") return 80;
        if (out.protocol == "https") return 443;
        return 0;
        };

    out.port = default_port();

    // IPv6: [::1]:554
    if (host_port.front() == '[') {
        size_t end = host_port.find(']');
        if (end == std::string::npos) return false;
        out.host = host_port.substr(1, end - 1);

        if (end + 1 < host_port.size()) {
            if (host_port[end + 1] != ':') return false;
            std::string port_str = host_port.substr(end + 2);
            if (port_str.empty()) return false;
            try {
                out.port = std::stoi(port_str);
            }
            catch (...) {
                return false;
            }
        }
    }
    else {
        size_t colon_pos = host_port.rfind(':');
        if (colon_pos != std::string::npos) {
            if (host_port.find(':') != colon_pos) return false;
            out.host = host_port.substr(0, colon_pos);
            std::string port_str = host_port.substr(colon_pos + 1);
            if (port_str.empty()) return false;
            try {
                out.port = std::stoi(port_str);
            }
            catch (...) {
                return false;
            }
        }
        else {
            out.host = host_port;
        }
    }

    if (out.host.empty()) return false;
    if (out.port <= 0 || out.port > 65535) return false;

    components = out;
    return true;
}

void RtspClient::forwardRTPPacket(const RTPPacket & packet) {
    // 序列化RTP包
    auto data = packet.serialize();
    
    if (config_.push_mode == TransportMode::UDP) {
        // UDP推流
        if (sendUDPData(data.data(), data.size())) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.bytes_forwarded += data.size();
            stats_.frames_forwarded++;
        }
    }
    else {
        // TCP推流：发送到目标RTSP服务器（通过RTSP控制的连接）
        if (target_conn_ && target_conn_->isConnected()) {
            // RTP over RTSP: 插入 $ (0x24) + channel + length
            uint8_t rtpOverTcp[4] = { 0x24, 0x00, 0x00, 0x00 };  // channel 0, length待定
            rtpOverTcp[2] = (data.size() >> 8) & 0xFF;
            rtpOverTcp[3] = data.size() & 0xFF;
            
            std::vector<uint8_t> tcpPacket;
            tcpPacket.insert(tcpPacket.end(), rtpOverTcp, rtpOverTcp + 4);
            tcpPacket.insert(tcpPacket.end(), data.begin(), data.end());
            
            int sent = target_conn_->send(tcpPacket.data(), tcpPacket.size());
            if (sent > 0) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_forwarded += data.size();
                stats_.frames_forwarded++;
            }
        }
    }
}

bool RtspClient::RTPPacket::parse(const uint8_t * data, size_t size) {
    // RTP 数据包格式（简要）：
    // 0               1               2               3
    // 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |V=2|P|X|  CC   |M|     PT      |       sequence number         |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |                           timestamp                           |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |           synchronization source (SSRC) identifier            |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |            contributing source (CSRC) identifiers             |
    // |                             ....                              |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // 后面可能有 extension header，之后是 RTP 负载（payload）
    // 本函数按 RFC 3550 解析基础 RTP 头并将 payload 提取出来。

    if (size < 12) return false;

    version = (data[0] >> 6) & 0x03;
    padding = (data[0] >> 5) & 0x01;
    extension = (data[0] >> 4) & 0x01;
    csrc_count = data[0] & 0x0F;
    marker = (data[1] >> 7) & 0x01;
    payload_type = data[1] & 0x7F;

    sequence_number = (data[2] << 8) | data[3];
    timestamp = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    ssrc = (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];

    size_t header_size = 12 + (csrc_count * 4);
    if (extension) {
        if (size < header_size + 4) return false;
        uint16_t extension_length = (data[header_size + 2] << 8) | data[header_size + 3];
        header_size += 4 + extension_length * 4;
    }

    if (size <= header_size) return false;

    payload.assign(data + header_size, data + size);
    return true;
}

std::vector<uint8_t> RtspClient::RTPPacket::serialize() const {
    std::vector<uint8_t> data(12 + csrc_count * 4 + payload.size());

    data[0] = (version << 6) | (padding << 5) | (extension << 4) | csrc_count;
    data[1] = (marker << 7) | (payload_type & 0x7F);

    data[2] = (sequence_number >> 8) & 0xFF;
    data[3] = sequence_number & 0xFF;

    data[4] = (timestamp >> 24) & 0xFF;
    data[5] = (timestamp >> 16) & 0xFF;
    data[6] = (timestamp >> 8) & 0xFF;
    data[7] = timestamp & 0xFF;

    data[8] = (ssrc >> 24) & 0xFF;
    data[9] = (ssrc >> 16) & 0xFF;
    data[10] = (ssrc >> 8) & 0xFF;
    data[11] = ssrc & 0xFF;

    // 复制负载
    if (!payload.empty()) {
        memcpy(data.data() + 12, payload.data(), payload.size());
    }

    return data;
}

std::string RtspClient::extractSessionID(const std::string & response) {
    size_t pos = response.find("Session: ");
    if (pos == std::string::npos) return "";

    size_t end = response.find("\r\n", pos);
    std::string session_line = response.substr(pos, end - pos);

    std::string session = session_line.substr(9);

    size_t timeout_pos = session.find(';');
    if (timeout_pos != std::string::npos) {
        session = session.substr(0, timeout_pos);
    }

    return session;
}

std::string RtspClient::extractTransport(const std::string & response) {
    size_t pos = response.find("Transport: ");
    if (pos == std::string::npos) return "";

    size_t end = response.find("\r\n", pos);
    return response.substr(pos + 11, end - pos - 11);
}

std::string RtspClient::generateCSeq() {
    static std::atomic<int> counter{ 1 };
    return std::to_string(counter++);
}

void RtspClient::setError(const std::string & error, int code) {
    //logError("Error [" + std::to_string(code) + "]: " + error);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.errors++;
    }

    if (error_callback_) {
        error_callback_(error, code);
    }

    setState(State::S_ERROR, error);
}

void RtspClient::setState(State new_state, const std::string & msg) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = new_state;
    }

    if (status_callback_) {
        status_callback_(new_state, msg);
    }

    //logInfo("State changed to " + std::to_string(static_cast<int>(new_state)) + ": " + msg);
    cv_.notify_all();
}

bool RtspClient::shouldReconnect() const {
    if (config_.max_retries > 0 && retry_count_ >= config_.max_retries) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - last_reconnect_time_ < std::chrono::milliseconds(config_.retry_interval)) {
        return false;
    }

    return true;
}

void RtspClient::doReconnect() {
    retry_count_++;
    last_reconnect_time_ = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.reconnect_count++;
    }

    if (config_.max_retries <= 0) {
        setState(State::RECONNECTING, "Reconnecting (attempt " + std::to_string(retry_count_) + "/unlimited)");
    }
    else {
        setState(State::RECONNECTING,
            "Reconnecting (attempt " + std::to_string(retry_count_) +
            "/" + std::to_string(config_.max_retries) + ")");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(config_.retry_interval));
}

void RtspClient::logInfo(const std::string & msg) const {
    std::cout << "[INFO] " << msg << std::endl;
}

void RtspClient::logError(const std::string & msg) const {
    std::cerr << "[ERROR] " << msg << std::endl;
}

void RtspClient::logDebug(const std::string & msg) const {
    if (config_.verbose) {
        std::cout << "[DEBUG] " << msg << std::endl;
    }
}

void RtspClient::logVerbose(const std::string & msg) const {
    if (config_.verbose) {
        std::cout << "[VERBOSE] " << msg << std::endl;
    }
}


/**
 * @brief 从RTSP URL中提取用户名和密码
 * @param config 配置结构体（包含source_url，输出source_username/source_password）
 * @return 解析成功返回true，失败返回false
 */

bool RtspClient::extractRtspAuthInfo(RtspClient::Config& config) {
    // 正则表达式匹配RTSP URL格式：rtsp://[user:pass@]host[:port]/path
    // 分组说明：
    // 1: 用户名  2: 密码  3: 剩余部分（IP/端口/路径）
    const std::regex rtspRegex(R"(^rtsp://([^:]+):([^@]+)@.*$)");
    std::smatch matchResult;

    // 匹配URL并提取用户名和密码
    if (std::regex_match(config.source_url, matchResult, rtspRegex)) {
        if (matchResult.size() >= 3) {
            config.source_username = matchResult[1].str();
            config.source_password = matchResult[2].str();
            return true;
        }
    }

    // 若未匹配到（URL无账号密码），清空用户名密码
    config.source_username = "";
    config.source_password = "";
    return false;
}


/*
 * H.264 NAL Unit Header (1 byte, non-FU-A mode)
 * ==============================================
 *
 *  7 6 5 4 3 2 1 0  (bit index)
 * +-+-+-+-+-+-+-+-+
 * |F|NRI |  Type  |
 * +-+-+-+-+-+-+-+-+
 *
 * F (bit 7)     : forbidden_zero_bit, should be 0
 * NRI (bits 6-5): nal_ref_idc, importance/priority level
 *                 00 = not used for reconstruction (discardable)
 *                 01 = used for ref (low priority)
 *                 10 = used for ref (medium priority)
 *                 11 = used for ref (high priority / key data like SPS/PPS/IDR)
 * Type (bits 4-0): nal_unit_type
 *                  1  = non-IDR slice
 *                  5  = IDR slice
 *                  6  = SEI
 *                  7  = SPS (Sequence Parameter Set)
 *                  8  = PPS (Picture Parameter Set)
 *                  9  = Access Unit delimiter
 *                  10 = End of sequence
 *                  11 = End of stream
 *                  12 = Filler data
 *                  14 = Prefix NALU (SVAC)
 *                  15 = Subset SPS (SVAC)
 *                  19 = Slice extension
 *                  20 = Slice extension for 3D
 *                  21 = Slice extension depth
 *                  22 = Reserved
 *                  23 = Reserved
 *                  24+ = Unspecified
 *                  28 = FU-A (Fragmentation Unit Type A) — NOT this format
 */

 /*
  * NAL unit 类型取值说明（逐项中文说明，便于代码阅读）
  * -----------------------------------------------------
  * 0   : 未指定
  * 1   : 非IDR切片（P/B 片），普通帧的切片数据，非关键帧
  * 2-4 : 切片分区（Partition A/B/C），非常见，一般忽略
  * 5   : IDR 切片（关键帧），解码器可从此帧起开始正确解码
  * 6   : SEI（补充增强信息），包含时间、字幕等元数据，不属于图像数据
  * 7   : SPS（序列参数集），包含编码参数（profile/level/分辨率），解码前必须有
  * 8   : PPS（图像参数集），与 SPS 配合使用以初始化解码器
  * 9   : AUD（访问单元分隔符），可选，用于标记帧边界
  * 10  : 序列结束
  * 11  : 码流结束
  * 12  : 填充数据
  * 13  : 保留
  * 14  : 前缀 NAL（Prefix），某些流/编码器使用
  * 15  : 子集 SPS
  * 16-18: 保留
  * 19  : 切片扩展
  * 20  : 3D 切片扩展
  * 21  : 深度切片扩展
  * 22-23: 保留
  * 24  : STAP-A（单时刻聚合包）——一个 RTP 包内包含多个子 NAL，每个子 NAL 前有 2 字节长度
  * 25  : STAP-B（带 DON 的聚合包）
  * 26  : MTAP16（多时刻聚合，16 位偏移）
  * 27  : MTAP24（多时刻聚合，24 位偏移）
  * 28  : FU-A（分片单元 A）——大 NAL 被分片在多个 RTP 包中传输，需要重组；payload[0]=FU indicator，payload[1]=FU header
  * 29  : FU-B（分片单元 B）
  * 30-31: 未指定/保留
  *
  * 处理建议：
  * - 录制裸 h264 文件时，非聚合且非分片的 RTP 包直接写入起始码(0x00000001)+payload。
  * - 对于 FU-A，需要在 S=1 的起始片段写入起始码 + 重建的 NAL 头（从 FU indicator 和 FU header 得到），随后写入片段数据；后续片段只写数据。
  * - 对于 STAP-A，需要按 2 字节长度解析每个子 NAL，逐个写入起始码 + 子NAL数据。
  * - 其他类型（聚合/扩展）按需补充解析，或记录为原始 payload 供离线分析。
  */

std::string getNALTypeDesc(unsigned char nal_type) {
    switch (nal_type) {
    case 0: return "Unspecified (0)";
    case 1: return "Non-IDR slice (Coded slice of a non-IDR picture)";
    case 2: return "Partition A (coded slice data partition A)";
    case 3: return "Partition B (coded slice data partition B)";
    case 4: return "Partition C (coded slice data partition C)";
    case 5: return "IDR slice (Instantaneous Decoding Refresh)";
    case 6: return "SEI (Supplemental enhancement information)";
    case 7: return "SPS (Sequence Parameter Set)";
    case 8: return "PPS (Picture Parameter Set)";
    case 9: return "AUD (Access Unit Delimiter)";
    case 10: return "End of sequence";
    case 11: return "End of stream";
    case 12: return "Filler data";
    case 13: return "Reserved (13)";
    case 14: return "Prefix NALU (SVAC) / Prefix";
    case 15: return "Subset SPS (SVAC)";
    case 16: return "Reserved (16)";
    case 17: return "Reserved (17)";
    case 18: return "Reserved (18)";
    case 19: return "Slice extension";
    case 20: return "Slice extension for 3D";
    case 21: return "Slice extension depth";
    case 22: return "Reserved (22)";
    case 23: return "Reserved (23)";
    case 24: return "STAP-A (Single-time aggregation packet)";
    case 25: return "STAP-B (Single-time aggregation packet, with DON)";
    case 26: return "MTAP16 (Multi-time aggregation packet, 16-bit offsets)";
    case 27: return "MTAP24 (Multi-time aggregation packet, 24-bit offsets)";
    case 28: return "FU-A (Fragmentation Unit A)";
    case 29: return "FU-B (Fragmentation Unit B)";
    case 30: return "Unspecified (30)";
    case 31: return "Unspecified (31)";
    default: {
        // Other values (>=32) are invalid for 5-bit type but handle gracefully
        return std::string("Unknown/Unspecified NAL type: ") + std::to_string((int)nal_type);
    }
    }
}

// h264文件分析工具 https://nalu.qer.im/
void RtspClient::recordRTPPacket(RTPPacket* pPkt) {
    if (!rec_ctrl_.recording || rec_ctrl_.path.empty()) return;

    std::vector<RTPPacket*> to_write;
    record_batch_buffer_.push_back(pPkt);
    if (record_batch_buffer_.size() < 20) {
        return;
    }

    // 交换出待写入队列，清空原队列
    to_write.swap(record_batch_buffer_);

    // 打开文件（追加二进制）
    std::ofstream ofs(rec_ctrl_.path, std::ios::binary | std::ios::app);
    if (!ofs) {
        logError("Failed to open record file: " + rec_ctrl_.path);
        for (auto p : to_write) delete p;
        return;
    }

    for (auto p : to_write) {
        const std::vector<uint8_t>& payload = p->payload;
        if (payload.empty()) {
            continue;
        }

        uint8_t nal_unit_type = payload[0] & 0x1F;

        // 当 NALU 长度超过 以太网MTU（典型 1500 字节）时会触发 FU-A 分片，但实际工程中常用阈值是 1400 字节左右（留安全余量）。
        // SPS/PPS/SEI 通常很小（几十~几百字节），都是 Single NALU 方式；I 帧（IDR）NALU 通常很大（几十KB~几百KB），几乎必然走 FU-A
        if (nal_unit_type == NAL_TYPE_STAP_A && payload.size() >= 2) {
            // STAP-A: 单时刻聚合包，payload 格式：
            // byte0: STAP-A header (F|NRI|Type=24)
            // 接下来重复： 2 字节大端长度 L, L 字节子NAL 数据
            size_t off = 1;
            while (off + 2 <= payload.size()) {
                uint16_t L = (payload[off] << 8) | payload[off + 1];
                off += 2;
                if (L == 0) continue;
                if (off + L > payload.size()) break; // 不完整，退出
                const uint8_t* subNal = &payload[off];
                uint8_t sub_nal_type = subNal[0] & 0x1F;
				writeNALtoFile(sub_nal_type,(char*)subNal, L, ofs);
                off += L;
            }
        }
        else if (nal_unit_type == NAL_TYPE_FU_A && payload.size() >= 2) {
            // FU-A 片段化（RFC 6184）处理
            // H.264 NAL 单元在 RTP 中可能被分片为 FU-A（Fragmentation Unit A）格式。
            // FU-A payload 格式：
            //  byte0: FU indicator (F|NRI|Type=28) NRI 2bit 指示该 NALU 的重要性等级
            //  byte1: FU header    (S|E|R|Type) S=Start E=End R=Reserved
            //  byte2...: 分片的实际数据（不包含原始 NAL 头）
            //  S (start) 位为1 表示这是该 NAL 单元的第一个片段，
            //  需要重建原始 NAL 头并写入起始码 (0x00000001) + NAL头 + 片段数据。
            //  非起始片段只是 NAL 的继续数据，直接写入即可（不再写 NAL 头）。
            uint8_t fu_header = payload[1];
            bool start = (fu_header & 0x80) != 0; // S 位
			bool end = (fu_header & 0x40) != 0;   // E 位
            // 重建原始 NAL 头：FU indicator 的 F/NRI 保留，高 3 位；
            // FU header 的低 5 位是原始 NAL 单元类型
			uint8_t fu_a_org_type = fu_header & 0x1F;
            uint8_t nal_header = (payload[0] & 0xE0) | fu_a_org_type;

            if (start) {
                rec_ctrl_.fu_a_buffer_.clear();
				rec_ctrl_.fu_a_buffer_.push_back(nal_header); // 重建的 NAL 头
            }
            rec_ctrl_.fu_a_buffer_.insert(rec_ctrl_.fu_a_buffer_.end(), payload.begin() + 2, payload.end());

            if (end) {
                writeNALtoFile(fu_a_org_type, rec_ctrl_.fu_a_buffer_.data(), rec_ctrl_.fu_a_buffer_.size(), ofs);
            }
        }
        else {
            writeNALtoFile(nal_unit_type, (char*)payload.data(), payload.size(), ofs);
        }
    }

    for (auto p : to_write) {
        delete p;
    }

    ofs.flush();
}

void RtspClient::writeNALtoFile(uint8_t nal_type,char* nal, size_t size, std::ofstream& ofs) {
    //此处不要使用 ofstream 的 tellp获得长度，不准确
    if (rec_ctrl_.firstWrite && nal_type != NAL_TYPE_IDR) {
        return;
    }
    rec_ctrl_.firstWrite = false;

    const uint8_t start_code[4] = { 0x00, 0x00, 0x00, 0x01 };

    // NAL 是 IDR（type==5）时，在写入该 NAL 前插入 SPS/PPS
    if (nal_type == NAL_TYPE_IDR) {
        if (!rec_ctrl_.last_was_idr_ && !source_video_info_.sps.empty() && !source_video_info_.pps.empty()) {
            ofs.write((const char*)start_code, sizeof(start_code));
            ofs.write((const char*)source_video_info_.sps.data(), static_cast<std::streamsize>(source_video_info_.sps.size()));
            ofs.write((const char*)start_code, sizeof(start_code));
            ofs.write((const char*)source_video_info_.pps.data(), static_cast<std::streamsize>(source_video_info_.pps.size()));
        }
        rec_ctrl_.last_was_idr_ = true;
    }
    else {
        rec_ctrl_.last_was_idr_ = false;
    }

    ofs.write((const char*)start_code, sizeof(start_code));
    ofs.write((const char*)nal, size);
}