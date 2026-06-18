#include "StreamNode.h"
#include "streamServer.h"
#include "dtls_transport.h"
#include "srtp_protect.h"
#include <psa/crypto.h>
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
void StreamNode::md5Init(MD5Context* context) {
    context->count[0] = 0;
    context->count[1] = 0;

    // 初始化状态 (魔法数)
    context->state[0] = 0x67452301;
    context->state[1] = 0xefcdab89;
    context->state[2] = 0x98badcfe;
    context->state[3] = 0x10325476;
}

// MD5更新
void StreamNode::md5Update(MD5Context* context, const uint8_t* data, size_t length) {
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
void StreamNode::md5Final(MD5Context* context, uint8_t digest[16]) {
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
void StreamNode::md5Transform(uint32_t state[4], const uint8_t block[64]) {
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
std::string StreamNode::md5Hex(const std::string& input) {
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
std::string StreamNode::base64Encode(const std::string& input) {
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

StreamNode::Connection::Connection() : sockfd_(StreamNode::kInvalidSocket) {
}

StreamNode::Connection::~Connection() {
    disconnect();
}

bool StreamNode::Connection::connect(const std::string& host, int port, int timeout_ms) {
    disconnect();
    last_error_ = 0;

    // 创建socket
    sockfd_ = static_cast<StreamNode::SocketHandle>(socket(AF_INET, SOCK_STREAM, 0));
    if (sockfd_ == StreamNode::kInvalidSocket) {
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
            sockfd_ = StreamNode::kInvalidSocket;
            return false;
        }
        memcpy(&serv_addr.sin_addr, server->h_addr, server->h_length);
    }

    // 连接
    if (::connect(static_cast<SOCKET_TYPE>(sockfd_), (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        last_error_ = SOCKET_ERROR_NUM;
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(sockfd_));
        sockfd_ = StreamNode::kInvalidSocket;
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

void StreamNode::Connection::disconnect() {
    if (sockfd_ != StreamNode::kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(sockfd_));
        sockfd_ = StreamNode::kInvalidSocket;
    }
    host_.clear();
    port_ = 0;
    last_error_ = 0;
}

bool StreamNode::Connection::isConnected() const {
    return sockfd_ != StreamNode::kInvalidSocket;
}

int StreamNode::Connection::send(const void* data, size_t size, int timeout_ms) {
    if (sockfd_ == StreamNode::kInvalidSocket) return -1;

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

int StreamNode::Connection::receive(void* buffer, size_t size, int timeout_ms) {
    if (sockfd_ == StreamNode::kInvalidSocket) return -1;

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

int StreamNode::Connection::receiveHttpResp(std::string& response, int timeout_ms) {
    if (sockfd_ == StreamNode::kInvalidSocket) return -1;

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
        //printf("[DEBUG] recv %d bytes, total=%d, first bytes: %.*s\n", n, total, n > 20 ? 20 : n, buf);

        if (IsValidPkt_HTTP(response, static_cast<size_t>(total))) {
            break;
        }
    }

    last_error_ = 0;
    return total;
}

bool StreamNode::Connection::setSocketTimeout(int timeout_ms) {
    if (sockfd_ == StreamNode::kInvalidSocket) return false;

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
// StreamNode 实现
// ============================================================================

StreamNode::StreamNode() {
    stats_.start_time = std::chrono::steady_clock::now();
    stats_.last_frame_time = std::chrono::steady_clock::now();
}

StreamNode::~StreamNode() {
    stop();
}

bool StreamNode::start(const Config& config) {
    if (running_) {
        return false;
    }

    config_ = config;
    running_ = true;
    stopping_ = false;
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

    control_thread_ = std::thread(&StreamNode::controlThread, this);
	control_thread_.detach();

    LOG("[StreamNode] StreamNode started,tag=%s,src=%s,target=%s",config_.tag.c_str(), config_.source_url.c_str(), config_.target_url.c_str());

    return true;
}

void StreamNode::stop() {
    if (!running_) return;

    stopping_ = true;
    running_ = false;

    cv_.notify_all();

    // 停止所有 ICE 线程
    stopAllIceThreads();

    if (rtp_handle_thread_.joinable()) {
        rtp_handle_thread_.join();
    }

    if (control_thread_.joinable()) {
        control_thread_.join();
    }

    teardown();

    setState(State::IDLE, "Stopped");
}

void StreamNode::restart() {
    stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    start(config_);
}

StreamNode::State StreamNode::getState() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

bool StreamNode::isRunning() {
    return running_;
}

StreamNode::Statistics StreamNode::getStatistics() {
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

void StreamNode::setStatusCallback(StatusCallback cb) {
    status_callback_ = cb;
}

void StreamNode::setFrameCallback(FrameCallback cb) {

}

void StreamNode::setErrorCallback(ErrorCallback cb) {
    error_callback_ = cb;
}

// ============================================================================
// 认证相关函数
// ============================================================================

std::string StreamNode::calculateBasicAuth(const AuthInfo& auth) {
    std::string credentials = auth.username + ":" + auth.password;
    return "Basic " + base64Encode(credentials);
}

std::string StreamNode::calculateDigest(const std::string& method, const std::string& uri,
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

bool StreamNode::parseWWWAuthenticate(const std::string& response, AuthInfo& auth) {
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

void StreamNode::updateAuthHeader(AuthInfo& auth, const std::string& method, const std::string& uri) {
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

bool StreamNode::rtspDescribe(Connection& conn, const std::string& url,
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
                << "User-Agent: StreamNode/1.0\r\n";

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
                setError("DESCRIBE send failed: sent=" + std::to_string(sent) +
                    " err=" + std::to_string(conn.lastError()));
                response.clear();
                return false;
            }

            response.clear();
            int rc = conn.receiveHttpResp(response, 1000);
            if (rc <= 0) {
                setError("DESCRIBE recv failed: rc=" + std::to_string(rc) +
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
                LOG("[StreamNode]tag=%s,DESCRIBE failed:%s",config_.tag.c_str(),response.substr(0, 200).c_str());
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

bool StreamNode::rtspSetup(Connection& conn, const std::string& url,
    std::string& session, STREAM_SESSION& stream, bool record_mode) {
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
        << "User-Agent: StreamNode/1.0\r\n";

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

bool StreamNode::rtspPlay(Connection& conn, const std::string& url,
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
        << "User-Agent: StreamNode/1.0\r\n"
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

bool StreamNode::rtspTeardown(Connection& conn, const std::string& url,
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
        << "User-Agent: StreamNode/1.0\r\n"
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

bool StreamNode::rtspAnnounce(Connection& conn, const std::string& url,
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
        << "User-Agent: StreamNode/1.0\r\n"
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

bool StreamNode::rtspRecord(Connection& conn, const std::string& url,
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
        << "User-Agent: StreamNode/1.0\r\n"
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

bool StreamNode::rtspGetParameter(Connection& conn, const std::string& url,
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
        << "User-Agent: StreamNode/1.0\r\n"
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

int StreamNode::getBufferedSeconds()
{
    // 计算当前 rtp_buffer_ 中的数据覆盖的大致秒数
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (rtp_buffer_.empty()) return 0;

    // 以最早包和最新包的 RTP timestamp 差值为基准计算（无符号，支持回绕）
    uint32_t newest_ts = rtp_buffer_.back()->timestamp;
    uint32_t oldest_ts = rtp_buffer_.front()->timestamp;
    uint32_t diff = newest_ts - oldest_ts; // 无符号差值，处理 32 位回绕

    // 获取时钟频率（ticks per second），RTCP/SDP 中给出，视频常见为 90000
    uint32_t clock = (pull_session_.clock_rate > 0) ? static_cast<uint32_t>(pull_session_.clock_rate) : 90000u;
    if (clock == 0) clock = 90000u;

    // 整数秒（截断子秒）。如果存在刻度差但小于1秒，返回1以提示非空缓冲
    int seconds = static_cast<int>(diff / clock);
    if (diff > 0 && seconds == 0) seconds = 1;
    return seconds;
}

void StreamNode::addToRtpBuffer(std::shared_ptr<RTPPacket> pPkt)
{
    // 校验输入
    if (!pPkt) return;

    // 使用共享指针直接保存引用，无需深拷贝对象。调用者共享同一份数据，
    // 引用计数自动管理生命周期，可安全地在多个消费者间传递。
    std::lock_guard<std::mutex> lock(queue_mutex_);
    rtp_buffer_.push_back(pPkt);

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
        uint32_t clock = (pull_session_.clock_rate > 0) ?
            static_cast<uint32_t>(pull_session_.clock_rate) : 90000u;
        if (clock == 0) clock = 90000u;

        // 当最旧包到最新包的时间差超过配置的秒数窗口时，逐个删除最旧包。
        while (!rtp_buffer_.empty()) {
            auto oldest = rtp_buffer_.front();
            uint32_t oldest_ts = oldest->timestamp;

            // 无符号相减可以按模 2^32 处理 timestamp 回绕情况。
            uint32_t diff = newest_ts - oldest_ts;

            // 将刻度差转换为整秒（会截断小数部分）。
            uint32_t seconds = diff / clock;

            if (seconds > static_cast<uint32_t>(rtp_buffer_max_seconds_)) {
                // 包已超出时间窗口 -> 从缓冲中移除。
                // shared_ptr 引用计数减1，若无人再持有则自动析构。
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
        // shared_ptr 自动管理生命周期，erase 后若无其他引用则析构。
        rtp_buffer_.erase(rtp_buffer_.begin());
    }
}

void StreamNode::rtpHandleThread() {
    StreamNode::doRtpRecv();
}

void StreamNode::controlThread() {
    while (running_ && !stopping_) {
            // 启动拉流与推流
            if (isPulling_ == false) {
                if (doStreamPull()) {
                    rtp_handle_thread_ = std::thread(&StreamNode::rtpHandleThread,this);
                    rtp_handle_thread_.detach();
                    isPulling_ = true;
                }
                else {
                    doReconnect();
                    teardown();
                }
            }
 
            if (isPulling_ == true && isPushing_ == false && config_.target_url != "") {
                if (doStreamPush()) {
                    isPushing_ = true;
				}
                else {
                    doReconnect();
                    teardown();
                }
            }

            // 心跳保活
            if (isPulling_) {
                if (source_conn_ && !source_session_.empty()) {
                    if (!rtspGetParameter(*source_conn_, config_.source_url, source_session_)) {
                        setError("Source RTSP keepalive failed", 1002);
                        isPulling_ = false;
                        teardown();
                    }
                }
            }

            if (isPushing_) {
                if (target_conn_ && !target_session_.empty()) {
                    if (!rtspGetParameter(*target_conn_, config_.target_url, target_session_)) {
                        setError("Target RTSP keepalive failed", 1002);
                        isPushing_ = false;
                        teardown();
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

bool StreamNode::doStreamPush() {
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
    push_session_ = pull_session_;
    {
        std::string track_control = "trackID=0";
        if (!pull_session_.control_url.empty()) {
            std::string src = pull_session_.control_url;
            auto pos = src.find_last_of('/');
            track_control = (pos == std::string::npos) ? src : src.substr(pos + 1);
            if (track_control.empty() || track_control == "*" ||
                track_control.rfind("rtsp://", 0) == 0 ||
                track_control.rfind("rtsps://", 0) == 0) {
                track_control = "trackID=0";
            }
        }
        push_session_.control_url = track_control;
    }

    std::string target_sdp = generateSDP(push_session_, STREAM_SESSION());

    // 发送ANNOUNCE到目标
    if (!rtspAnnounce(*target_conn_, config_.target_url, target_sdp, target_session_)) {
        setError("ANNOUNCE failed", 3007);
        return false;
    }

    // 如果使用 UDP 推流，应先创建并绑定本地 RTP/RTCP sockets，
    // 并将 client_port 写入 target_video_info_，再发送 SETUP。
    if (config_.push_mode == TransportMode::UDP) {
        if (!createUDPPushSocket()) {
            logError("Failed to create UDP push socket, falling back to TCP");
            config_.push_mode = TransportMode::TCP;
        }
        else {
            // 填写 client_port，格式 "RTP-RTCP"
            push_session_.client_port = std::to_string(push_session_.client_rtp_port) + "-" + std::to_string(push_session_.client_rtcp_port);
            logInfo("Push stream: Using UDP mode, client_port=" + push_session_.client_port);
        }
    }

    if (config_.push_mode == TransportMode::TCP) {
        logInfo("Push stream: Using TCP mode (RTP over RTSP)");
    }

    // 发送SETUP到目标
    if (!rtspSetup(*target_conn_, config_.target_url, target_session_, push_session_, true)) {
        setError("SETUP failed for target", 3008);
        return false;
    }

    // 解析目标服务器端口
    {
        logInfo("Target SETUP Transport: " + push_session_.transport);
        const std::string key = "server_port=";
        size_t pos = push_session_.transport.find(key);
        if (pos != std::string::npos) {
            pos += key.size();
            while (pos < push_session_.transport.size() &&
                (push_session_.transport[pos] == ' ' || push_session_.transport[pos] == '\t')) {
                ++pos;
            }
            int port = 0;
            while (pos < push_session_.transport.size() &&
                push_session_.transport[pos] >= '0' && push_session_.transport[pos] <= '9') {
                port = port * 10 + (push_session_.transport[pos] - '0');
                ++pos;
            }
            if (port > 0 && port <= 65535) {
                push_session_.server_rtp_port = port;
            }
        }
    }

    logInfo("Target RTP port: " + std::to_string(push_session_.server_rtp_port));
    if (push_session_.server_rtp_port == 0) {
        setError("Missing/invalid server_port in target Transport: " + push_session_.transport, 3010);
        return false;
    }

    // 保存目标RTP地址信息（用于UDP推流）
    target_rtp_host_ = target_url.host;
    logInfo("Target RTP host: " + target_rtp_host_);


    // 发送RECORD到目标
    if (!rtspRecord(*target_conn_, config_.target_url, target_session_)) {
        setError("RECORD failed", 3009);
        return false;
    }

	LOG("[keyinfo][StreamNode]tag=%s,stream forward success,pushToUrl:%s", config_.tag.c_str(), config_.target_url.c_str());

    return true;
}

bool StreamNode::doStreamPull() {
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

    LOG("[StreamNode]tag=%s,Connect to source success,%s",config_.tag.c_str(),(src_url.host + ":" + std::to_string(src_url.port)).c_str());

    // 发送DESCRIBE
    std::string sdp;
    if (!rtspDescribe(*source_conn_, config_.source_url, sdp, source_session_)) {
        setError("DESCRIBE failed", 2003);
        return false;
    }

    // 解析SDP
    if (!parseSDP(sdp, pull_session_, pull_audio_session_)) {
        setError("Failed to parse SDP", 2004);
        return false;
    }

    // 调试：打印收到的 SDP 内容
    std::string sdp_for_log = sdp;
    std::replace(sdp_for_log.begin(), sdp_for_log.end(), '\r', '~');
    std::replace(sdp_for_log.begin(), sdp_for_log.end(), '\n', '~');
    LOG("[StreamNode]tag=%s, sdp received: %s,streamInfo:%s,audioControl:%s",
        config_.tag.c_str(), 
        sdp_for_log.c_str(),
        pull_session_.control_url.c_str(),
        pull_audio_session_.control_url.c_str());

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
        pull_session_.client_port = std::to_string(pull_session_.client_rtp_port) + "-" + std::to_string(pull_session_.client_rtcp_port);
    }
    else {
        pull_session_.client_port = "0-0";  // TCP模式不需要client_port
    }

    LOG("[StreamNode]tag=%s,SETUP,mode=%s,local rtp/rtcp port=%s",
        config_.tag.c_str(),
        pullUseUDP ? "udp" : "tcp",
        pull_session_.client_port.c_str()
    );

    // 发送SETUP到源
    if (!rtspSetup(*source_conn_, config_.source_url, source_session_, pull_session_)) {
        setError("SETUP failed for source", 3003);
        return false;
    }

    // 解析传输信息
    std::istringstream transport_stream(pull_session_.transport);
    std::string token;
    while (std::getline(transport_stream, token, ';')) {
        if (token.find("server_port=") != std::string::npos) {
            size_t pos = token.find('=');
            pull_session_.server_port = token.substr(pos + 1);

            // 解析RTP端口
            size_t dash = pull_session_.server_port.find('-');
            if (dash != std::string::npos) {
                pull_session_.server_rtp_port = std::stoi(pull_session_.server_port.substr(0, dash));
            }
        }
        else if (token.find("source=") != std::string::npos) {
            size_t pos = token.find('=');
            pull_session_.remote_host = token.substr(pos + 1);
        }
    }


    LOG("[StreamNode]tag=%s,SETUP success,mode=%s,server port=%s",
        config_.tag.c_str(),
        pullUseUDP ? "udp" : "tcp",
        pull_session_.server_port.c_str()
    );

    // 发送PLAY
    if (!rtspPlay(*source_conn_, config_.source_url, source_session_)) {
        setError("PLAY failed", 3004);
        return false;
    }

    return true;
}

void StreamNode::doRtpRecv() {
    setState(State::PLAYING, "Streaming started");
    bool pullUDP = (config_.pull_mode == TransportMode::UDP);
    LOG("[keyinfo][StreamNode]tag=%s,Pull Success,rtp handle thread start,mode:%s",config_.tag.c_str(),pullUDP ? "UDP" : "TCP");

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

    while (running_ && !stopping_) {
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
            auto pPkt = std::make_shared<RTPPacket>();
            RTPPacket& packet = *pPkt;
            if (packet.parse(buffer.data(), received)) {
                // 捕获实际 SSRC（用于 SDP 声明，只记录一次）
                if (pull_session_.video_ssrc == 0 && packet.ssrc != 0) {
                    pull_session_.video_ssrc = packet.ssrc;
                    LOG("[StreamNode] Captured video SSRC=%u", packet.ssrc);
                }

                // 放入缓存
                addToRtpBuffer(pPkt);

                // 转发推流
                if (isPushing_) {
                    forwardRTPPacket(packet);
                }

                //发送给拉流客户端
                sendRTPPacketToClients(packet);


                // 录制到磁盘（加锁保护 rec_ctrl_ 和 record_batch_buffer_，与 rpc_startRecord/rpc_stopRecord 互斥）
                {
                    std::lock_guard<std::recursive_mutex> lock(rec_mutex_);
                    if (!rec_ctrl_.recording) continue;  // 双重检查：锁获取期间 recording 可能已被 stopRecord 置 false
                    if (rec_ctrl_.firstWrite && !rec_ctrl_.preRecordingDone) {
                        //从rtp_buffer_取出rec_ctrl_.preSeconds的数据并录制（仅一次）
                        std::vector<std::shared_ptr<RTPPacket>> pre_packets;
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            uint32_t _clock = (pull_session_.clock_rate > 0) ? static_cast<uint32_t>(pull_session_.clock_rate) : 90000u;
                            for (auto it = rtp_buffer_.rbegin(); it != rtp_buffer_.rend(); ++it) {
                                if (packet.timestamp - (*it)->timestamp <= static_cast<uint64_t>(rec_ctrl_.preSeconds) * _clock) {
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
                        rec_ctrl_.preRecordingDone = true;
                    }
                    // 无论是否写过预录数据，当前包都要录制
                    recordRTPPacket(pPkt);
                }

                //logVerbose((pullUDP ? "UDP" : "TCP") + std::string("->RTP: seq=") + 
                //    std::to_string(packet.sequence_number) +
                //    " ts=" + std::to_string(packet.timestamp) +
                //    " size=" + std::to_string(received));
            }
        }
        else if (received > 0 && received<=12) {
            printf("[StreamNode]rtp handle thread,wrong recv len");
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

    isPulling_ = false;
    LOG("[StreamNode]Pull thread stopped,tag= %s ",config_.tag.c_str());
}

void StreamNode::teardown() {
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
    target_rtp_host_.clear();

    pull_session_.client_rtp_port = 0;
    pull_session_.client_rtcp_port = 0;
    push_session_.client_rtp_port = 0;
    push_session_.client_rtcp_port = 0;

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

bool StreamNode::createUDPPullSocket() {
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
            pull_session_.rtp_socket = rtp_sock;
            pull_session_.rtcp_socket = rtcp_sock;
            pull_session_.client_rtp_port = rtp_port;
            pull_session_.client_rtcp_port = rtcp_port;

            logInfo("UDP pull sockets created: rtp_fd=" + std::to_string(pull_session_.rtp_socket) +
                    " rtp_port=" + std::to_string(pull_session_.client_rtp_port) +
                    " rtcp_fd=" + std::to_string(pull_session_.rtcp_socket) +
                    " rtcp_port=" + std::to_string(pull_session_.client_rtcp_port));
            return true;
        }

        // 如果绑定失败，释放并重试（可能下一个端口被占用）
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_sock));
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
    }

    logError("Failed to create consecutive UDP pull sockets for RTP/RTCP");
    pull_session_.rtp_socket = kInvalidSocket;
    pull_session_.rtcp_socket = kInvalidSocket;
    pull_session_.client_rtp_port = 0;
    pull_session_.client_rtcp_port = 0;
    return false;
}

// 修改 createUDPPushSocket()：分配并 bind 一对连续端口（RTP/RTCP）
bool StreamNode::createUDPPushSocket() {
    const int max_attempts = 10;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // 创建RTP socket并绑定到系统分配的端口（0）
        SocketHandle rtp_sock = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
        if (rtp_sock == kInvalidSocket) {
            logError("Failed to create UDP push socket (rtp)");
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
        local_addr.sin_port = htons(0); // 系统分配端口

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
            push_session_.rtp_socket = rtp_sock;
            push_session_.rtcp_socket = rtcp_sock;
            push_session_.client_rtp_port = rtp_port;
            push_session_.client_rtcp_port = rtcp_port;

            logInfo("UDP push sockets created: rtp_fd=" + std::to_string(push_session_.rtp_socket) +
                " rtp_port=" + std::to_string(push_session_.client_rtp_port = rtp_port) +
                " rtcp_fd=" + std::to_string(push_session_.rtcp_socket) +
                " rtcp_port=" + std::to_string(push_session_.client_rtcp_port));
            return true;
        }

        // 绑定失败，释放并重试
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_sock));
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
    }

    logError("Failed to create consecutive UDP push sockets for RTP/RTCP");
    push_session_.rtp_socket = kInvalidSocket;
    push_session_.rtcp_socket = kInvalidSocket;
    push_session_.client_rtp_port = 0;
    push_session_.client_rtcp_port = 0;
    return false;
}
bool StreamNode::createUDPServerSocket(STREAM_SESSION& streamInfo)
{
    const int max_attempts = 10;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // 创建RTP socket并绑定到系统分配的端口（0）
        SocketHandle rtp_sock = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
        if (rtp_sock == kInvalidSocket) {
            logError("Failed to create UDP push socket (rtp)");
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
        local_addr.sin_port = htons(0); // 系统分配端口

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
            streamInfo.rtp_socket = rtp_sock;
            streamInfo.rtcp_socket = rtcp_sock;
            streamInfo.server_rtp_port = rtp_port;
            streamInfo.server_rtcp_port = rtcp_port;

            logInfo("UDP server sockets created: rtp_fd=" + std::to_string(streamInfo.rtp_socket) +
                " rtp_port=" + std::to_string(streamInfo.server_rtp_port = rtp_port) +
                " rtcp_fd=" + std::to_string(streamInfo.rtcp_socket) +
                " rtcp_port=" + std::to_string(streamInfo.server_rtcp_port = rtcp_port
                ));
            return true;
        }

        // 绑定失败，释放并重试
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_sock));
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_sock));
    }

    logError("Failed to create consecutive UDP push sockets for RTP/RTCP");
    streamInfo.rtp_socket = kInvalidSocket;
    streamInfo.rtcp_socket = kInvalidSocket;
    streamInfo.client_rtp_port = 0;
    streamInfo.client_rtcp_port = 0;
    return false;
}
void StreamNode::closeUDPSockets() {
    if (pull_session_.rtp_socket != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(pull_session_.rtp_socket));
        pull_session_.rtp_socket = kInvalidSocket;
    }
    if (pull_session_.rtcp_socket != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(pull_session_.rtcp_socket));
        pull_session_.rtcp_socket = kInvalidSocket;
    }


    if (push_session_.rtp_socket != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(push_session_.rtp_socket));
        push_session_.rtp_socket = kInvalidSocket;
    }
    if (push_session_.rtcp_socket != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(push_session_.rtcp_socket));
        push_session_.rtcp_socket = kInvalidSocket;
    }


    pull_session_.client_rtp_port = 0;
    pull_session_.client_rtcp_port = 0;
    push_session_.client_rtp_port = 0;
    push_session_.client_rtcp_port = 0;
}

bool StreamNode::configureUDPSocket(SocketHandle sock, bool is_multicast) {
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

bool StreamNode::sendUDPDataToSession(const uint8_t* data, size_t size, STREAM_SESSION& rtspSession) {
    int remoteRtpPort = 0;
    int remoteRtcpPort = 0;
    if (rtspSession.session_type_ == CLINET_PULL || rtspSession.session_type_ == CLINET_PUSH) {
        remoteRtpPort = rtspSession.server_rtp_port;
        remoteRtcpPort = rtspSession.server_rtcp_port;
    }
    else {
        remoteRtpPort = rtspSession.client_rtp_port;
        remoteRtcpPort = rtspSession.client_rtcp_port;
    }

    if (rtspSession.rtp_socket == kInvalidSocket || remoteRtpPort == 0) {
        return false;
    }

    // 解析目标地址
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(remoteRtpPort);

    if (inet_pton(AF_INET, rtspSession.remote_host.c_str(), &target_addr.sin_addr) <= 0) {
        struct hostent* server = gethostbyname(rtspSession.remote_host.c_str());
        if (!server) {
            logError("Failed to resolve target host: " + rtspSession.remote_host);
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.errors++;
            return false;
        }
        memcpy(&target_addr.sin_addr, server->h_addr, server->h_length);
    }

    // 发送UDP数据
    int sent = sendto(static_cast<SOCKET_TYPE>(rtspSession.rtp_socket),
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


int StreamNode::receiveUDPData(uint8_t* buffer, size_t size, std::string& src_ip, int& src_port) {
    if (pull_session_.rtp_socket == kInvalidSocket) {
        return -1;
    }

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    int received = recvfrom(static_cast<SOCKET_TYPE>(pull_session_.rtp_socket),
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

bool StreamNode::parseSDP(const std::string & sdp, STREAM_SESSION & video_info, STREAM_SESSION & audio_info) {
    std::istringstream ss(sdp);
    std::string line;
    STREAM_SESSION* current_info = nullptr;

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

std::string StreamNode::generateSDP(const STREAM_SESSION & video_info, const STREAM_SESSION & audio_info) {
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

bool StreamNode::URLComponents::parse(const std::string & url, URLComponents & components) {
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

// 每个 session 独有的 DTLS/SRTP 状态（定义在 sendRTPPacketToClients 之前，
// 使其在 SRTP 发送路径中能被安全地 static_cast 访问）
struct SessionDtlsState {
    DtlsTransport dtls;
    SrptProtect::Context srtp_ctx;
    bool dtls_initialized = false;
    bool srtp_ready = false;
    // Per-session RTP 序列号管理：原始流的 seq 不能直接透传，
    // 每个 WebRTC 客户端需要独立连续的序列号
    uint16_t local_seq = 0;
    bool    seq_inited = false;
    // SPS/PPS 注入标记（替代 session->state 的 3→4 标记）
    bool    sps_pps_injected = false;
};

void StreamNode::sendRTPPacketToClients(const RTPPacket& packet) {
    std::vector<std::shared_ptr<StreamNode::STREAM_SESSION>> playClients;
    client_sessions_mutex_.lock();
    playClients = client_sessions_;
    client_sessions_mutex_.unlock();
    // 序列化RTP包
    auto data = packet.serialize();

    for (size_t i = 0; i < playClients.size(); i++) {
        auto& sp = playClients[i];
        if (!sp) continue;
        StreamNode::STREAM_SESSION& client = *sp;
        if (client.transport_mode == TransportMode::UDP) {
            // UDP推流（RTSP 明文）
            if (sendUDPDataToSession(data.data(), data.size(), client)) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_forwarded += data.size();
                stats_.frames_forwarded++;
            }
        }
        else {
            // TCP推流：发送到目标RTSP服务器（通过RTSP控制的连接）
            //if (target_conn_ && target_conn_->isConnected()) {
            //    // RTP over RTSP: 插入 $ (0x24) + channel + length
            //    uint8_t rtpOverTcp[4] = { 0x24, 0x00, 0x00, 0x00 };  // channel 0, length待定
            //    rtpOverTcp[2] = (data.size() >> 8) & 0xFF;
            //    rtpOverTcp[3] = data.size() & 0xFF;

            //    std::vector<uint8_t> tcpPacket;
            //    tcpPacket.insert(tcpPacket.end(), rtpOverTcp, rtpOverTcp + 4);
            //    tcpPacket.insert(tcpPacket.end(), data.begin(), data.end());

            //    int sent = target_conn_->send(tcpPacket.data(), tcpPacket.size());
            //    if (sent > 0) {
            //        std::lock_guard<std::mutex> lock(stats_mutex_);
            //        stats_.bytes_forwarded += data.size();
            //        stats_.frames_forwarded++;
            //    }
            //}
        }
    }

    // === WebRTC SRTP 发送路径 ===
    // 遍历所有 client_sessions_，对 DTLS+SRTP 已完成的会话通过 SRTP 加密后发送视频
    {
        // 检测当前包是否包含 IDR NAL（用于在 IDR 前插入 SPS/PPS）
        bool isIdr = false;
        if (!packet.payload.empty()) {
            uint8_t nalHeader = packet.payload[0];
            uint8_t nalType = nalHeader & 0x1F;
            if (nalType == NAL_TYPE_IDR) {
                isIdr = true;
            } else if (nalType == NAL_TYPE_FU_A && packet.payload.size() > 1) {
                // FU-A: 第二个字节是 FU header，其中低 5 位是 NAL type
                uint8_t fuHeader = packet.payload[1];
                uint8_t fuNalType = fuHeader & 0x1F;
                bool start = (fuHeader & 0x80) != 0; // S 位
                bool end = (fuHeader & 0x40) != 0;   // E 位
				if (fuNalType == NAL_TYPE_IDR && start) { // 只有 FU-A 的第一个包（S=1）才算是 IDR 的开始
                    isIdr = true;
                }
            } else if (nalType == NAL_TYPE_STAP_A && packet.payload.size() > 2) {
                // STAP-A: 跳过第一个 NALU 的长度字段(2B)检查
                uint8_t firstNalType = packet.payload[2] & 0x1F;
                if (firstNalType == NAL_TYPE_IDR) {
                    isIdr = true;
                }
            }
        }

        // 获取 client_sessions_ 快照（避免持锁遍历）
        std::vector<std::shared_ptr<STREAM_SESSION>> sessions;
        {
            std::lock_guard<std::mutex> lock(client_sessions_mutex_);
            sessions = client_sessions_;
        }
        for (auto& session : sessions) {
            if (!session || !session->is_webrtc) continue;
            // state: 3=SRTP激活（is_webrtc 下 S3_SRTP_ACTIVE 即为激活态）
            if (session->state != SESSION_STATE::S3_SRTP_ACTIVE) continue;

            // 通过 SessionDtlsState 正确访问 DTLS 和 SRTP 上下文
            auto* dtlsState = static_cast<SessionDtlsState*>(session->dtls_transport_);
            if (!dtlsState || !dtlsState->srtp_ready) continue;
            if (!dtlsState->dtls.isPeerSet()) continue;

            DtlsTransport& dtls = dtlsState->dtls;
            SrptProtect::Context& srtpCtx = dtlsState->srtp_ctx;

            // 初始化 per-session 序列号（以原始流第一个包的 seq 为基准）
            if (!dtlsState->seq_inited) {
                dtlsState->local_seq = packet.sequence_number;
                dtlsState->seq_inited = true;
            }

            // 如果当前包是 IDR 且 session 有 SPS/PPS，先发送 SPS/PPS RTP 包
            if (!session->last_was_idr_ && isIdr 
                && !session->sps.empty() && !session->pps.empty()) {
                // 辅助函数：发送单个 NAL 的 RTP 包，使用 per-session 独立序列号
                auto sendSingleNalRtp = [&](const std::vector<uint8_t>& nal) {
                    std::vector<uint8_t> nalData(12 + nal.size());
                    nalData[0] = 0x80;  // V=2, P=0, X=0, CC=0
                    nalData[1] = (0 << 7) | (packet.payload_type & 0x7F);  // marker=0
                    nalData[2] = (dtlsState->local_seq >> 8) & 0xFF;
                    nalData[3] = dtlsState->local_seq & 0xFF;
                    nalData[4] = (packet.timestamp >> 24) & 0xFF;
                    nalData[5] = (packet.timestamp >> 16) & 0xFF;
                    nalData[6] = (packet.timestamp >> 8) & 0xFF;
                    nalData[7] = packet.timestamp & 0xFF;
                    nalData[8]  = (packet.ssrc >> 24) & 0xFF;
                    nalData[9]  = (packet.ssrc >> 16) & 0xFF;
                    nalData[10] = (packet.ssrc >> 8) & 0xFF;
                    nalData[11] = packet.ssrc & 0xFF;
                    memcpy(&nalData[12], nal.data(), nal.size());

                    dtlsState->local_seq++;  // 递增序列号

                    auto srtpPkt = SrptProtect::protect(srtpCtx, nalData);
                    if (!srtpPkt.empty()) {
                        const struct sockaddr_in& peerAddr = dtls.getPeerAddr();
                        sendto(session->rtp_socket,
                            (const char*)srtpPkt.data(), (int)srtpPkt.size(), 0,
                            (const struct sockaddr*)&peerAddr, sizeof(peerAddr));
                    }
                };

                sendSingleNalRtp(session->sps);
                sendSingleNalRtp(session->pps);
                LOG("SRTP: injected SPS (%zu bytes, NAL type=0x%02x) + PPS (%zu bytes, NAL type=0x%02x) before IDR, seq_start=%u",
                    session->sps.size(),
                    session->sps.empty() ? 0 : (session->sps[0] & 0x1F),
                    session->pps.size(),
                    session->pps.empty() ? 0 : (session->pps[0] & 0x1F),
                    dtlsState->local_seq - 2);
            }
            session->last_was_idr_ = isIdr;

            // 用 per-session 独立序列号替换原始 seq 后发送
            // 注意：需要修改 rtpVec 中的序列号字节（byte[2], byte[3]）
            std::vector<uint8_t> rtpVec(data.begin(), data.end());
            rtpVec[2] = (dtlsState->local_seq >> 8) & 0xFF;
            rtpVec[3] = dtlsState->local_seq & 0xFF;
            dtlsState->local_seq++;

            std::vector<uint8_t> srtpPkt = SrptProtect::protect(srtpCtx, rtpVec);
            if (srtpPkt.empty()) continue;

            const struct sockaddr_in& peerAddr = dtls.getPeerAddr();
            int sent = sendto(session->rtp_socket,
                (const char*)srtpPkt.data(), (int)srtpPkt.size(), 0,
                (const struct sockaddr*)&peerAddr, sizeof(peerAddr));

            // 前 5 次打印发送状态
            static int srtp_send_count = 0;
            if (srtp_send_count < 5) {
                char ipbuf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &peerAddr.sin_addr, ipbuf, sizeof(ipbuf));
                LOG("SRTP send #%d: sent=%d/%zu to %s:%u",
                    srtp_send_count, sent, srtpPkt.size(),
                    ipbuf, ntohs(peerAddr.sin_port));
                srtp_send_count++;
            }

            if (sent > 0) {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_forwarded += srtpPkt.size();
                stats_.frames_forwarded++;
            }
        }
    }
}

void StreamNode::forwardRTPPacket(const RTPPacket & packet) {
    // 序列化RTP包
    auto data = packet.serialize();
    
    if (config_.push_mode == TransportMode::UDP) {
        // UDP推流
        if (sendUDPDataToSession(data.data(), data.size(),push_session_)) {
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

bool StreamNode::RTPPacket::parse(const uint8_t * data, size_t size) {
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

std::vector<uint8_t> StreamNode::RTPPacket::serialize() const {
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

std::string StreamNode::extractSessionID(const std::string & response) {
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

std::string StreamNode::extractTransport(const std::string & response) {
    size_t pos = response.find("Transport: ");
    if (pos == std::string::npos) return "";

    size_t end = response.find("\r\n", pos);
    return response.substr(pos + 11, end - pos - 11);
}

std::string StreamNode::generateCSeq() {
    static std::atomic<int> counter{ 1 };
    return std::to_string(counter++);
}

void StreamNode::setError(const std::string & error, int code) {
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

void StreamNode::setState(State new_state, const std::string & msg) {
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

bool StreamNode::shouldReconnect() const {
    if (config_.max_retries > 0 && retry_count_ >= config_.max_retries) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - last_reconnect_time_ < std::chrono::milliseconds(config_.retry_interval)) {
        return false;
    }

    return true;
}

void StreamNode::doReconnect() {
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

void StreamNode::logInfo(const std::string & msg) const {
    std::cout << "[INFO] " << msg << std::endl;
}

void StreamNode::logError(const std::string & msg) const {
    std::cerr << "[ERROR] " << msg << std::endl;
}

void StreamNode::logDebug(const std::string & msg) const {
    if (config_.verbose) {
        std::cout << "[DEBUG] " << msg << std::endl;
    }
}

void StreamNode::logVerbose(const std::string & msg) const {
    if (config_.verbose) {
        std::cout << "[VERBOSE] " << msg << std::endl;
    }
}


/**
 * @brief 从RTSP URL中提取用户名和密码
 * @param config 配置结构体（包含source_url，输出source_username/source_password）
 * @return 解析成功返回true，失败返回false
 */

bool StreamNode::extractRtspAuthInfo(StreamNode::Config& config) {
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
void StreamNode::recordRTPPacket(std::shared_ptr<RTPPacket> pPkt) {
    std::lock_guard<std::recursive_mutex> lock(rec_mutex_);  // 与 rpc_stopRecord 互斥，可被 doRtpRecv 重入
    if (!rec_ctrl_.recording || rec_ctrl_.path.empty()) return;

    std::vector<std::shared_ptr<RTPPacket>> to_write;
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
        // to_write 析构时 shared_ptr 引用计数自动递减，无需手动 delete
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

    // to_write 析构：shared_ptr 引用计数递减，交由 rtp_buffer_ 继续管理
    ofs.flush();
}

void StreamNode::flushRecordBuffer() {
    std::lock_guard<std::recursive_mutex> lock(rec_mutex_);  // 与 doRtpRecv 互斥，可被 rpc_stopRecord 重入
    if (record_batch_buffer_.empty()) return;

    std::vector<std::shared_ptr<RTPPacket>> to_write;
    to_write.swap(record_batch_buffer_);

    std::ofstream ofs(rec_ctrl_.path, std::ios::binary | std::ios::app);
    if (!ofs) {
        logError("flushRecordBuffer: Failed to open record file: " + rec_ctrl_.path);
        return;
    }

    for (auto p : to_write) {
        const std::vector<uint8_t>& payload = p->payload;
        if (payload.empty()) continue;

        uint8_t nal_unit_type = payload[0] & 0x1F;

        if (nal_unit_type == NAL_TYPE_STAP_A && payload.size() >= 2) {
            size_t off = 1;
            while (off + 2 <= payload.size()) {
                uint16_t L = (payload[off] << 8) | payload[off + 1];
                off += 2;
                if (L == 0) continue;
                if (off + L > payload.size()) break;
                const uint8_t* subNal = &payload[off];
                writeNALtoFile(subNal[0] & 0x1F, (char*)subNal, L, ofs);
                off += L;
            }
        }
        else if (nal_unit_type == NAL_TYPE_FU_A && payload.size() >= 2) {
            uint8_t fu_header = payload[1];
            bool end = (fu_header & 0x40) != 0;
            uint8_t fu_a_org_type = fu_header & 0x1F;
            uint8_t nal_header = (payload[0] & 0xE0) | fu_a_org_type;

            bool start = (fu_header & 0x80) != 0;
            if (start) {
                rec_ctrl_.fu_a_buffer_.clear();
                rec_ctrl_.fu_a_buffer_.push_back(nal_header);
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

    ofs.flush();
    LOG("[StreamNode] Flushed %zu buffered packets to record file, tag=%s",
        to_write.size(), config_.tag.c_str());
}

void StreamNode::writeNALtoFile(uint8_t nal_type, char* nal, size_t size, std::ofstream& ofs) {
    // firstWrite 期间只跳过视频数据（non-IDR slice），放行 SPS/PPS/IDR/SEI/AUD
    // 确保录制文件以摄像头原生的参数集开头，不插入 SDP 的 SPS/PPS（VUI 时间参数可能不一致）
    if (rec_ctrl_.firstWrite && nal_type == NAL_TYPE_NON_IDR) {
        return;
    }
    rec_ctrl_.firstWrite = false;

    const uint8_t start_code[4] = { 0x00, 0x00, 0x00, 0x01 };
    ofs.write((const char*)start_code, sizeof(start_code));
    ofs.write((const char*)nal, size);
}

// ============================================================================
// ICE-Lite + DTLS + SRTP (WebRTC) 实现 — 每客户端一线程
// ============================================================================

void StreamNode::startIceHandleThread(std::shared_ptr<STREAM_SESSION> session) {
    if (!session || session->rtp_socket == kInvalidSocket || !session->is_webrtc) {
        return;
    }

    // 设置 socket 接收超时为 1 秒，保证 stop 时能及时退出
#ifdef _WIN32
    int timeout_ms = 1000;
    setsockopt(static_cast<SOCKET_TYPE>(session->rtp_socket), SOL_SOCKET, SO_RCVTIMEO,
               (const char*)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval tv = {1, 0};
    setsockopt(session->rtp_socket, SOL_SOCKET, SO_RCVTIMEO,
               (const char*)&tv, sizeof(tv));
#endif

    session->ice_running_ = true;
    session->ice_thread_ = std::thread(&StreamNode::iceHandleLoop, this, session);

    logInfo("ICE thread started for socket fd=" + std::to_string(session->rtp_socket)
            + " ufrag=" + session->ice_ufrag);
}

void StreamNode::stopAllIceThreads() {
    // 获取所有 client_sessions_ 快照，停止其中的 WebRTC ICE 线程
    std::vector<std::shared_ptr<STREAM_SESSION>> sessions;
    {
        std::lock_guard<std::mutex> lock(client_sessions_mutex_);
        sessions = client_sessions_;
    }

    for (auto& s : sessions) {
        if (!s || !s->is_webrtc) continue;

        s->ice_running_ = false;
        if (s->ice_thread_.joinable()) {
            s->ice_thread_.join();
        }
        // 清理 DTLS 状态（iceHandleLoop 退出时通常已清理，这里兜底）
        if (s->dtls_transport_) {
            delete static_cast<SessionDtlsState*>(s->dtls_transport_);
            s->dtls_transport_ = nullptr;
            s->srtp_context_   = nullptr;
        }
        logInfo("ICE thread stopped for socket fd=" + std::to_string(s->rtp_socket));
    }
}

void StreamNode::iceHandleLoop(std::shared_ptr<STREAM_SESSION> session) {
    uint8_t buf[2048];

    // 初始化本会话的 DTLS 状态
    auto* dtls_state = new SessionDtlsState();
    session->dtls_transport_ = dtls_state;
    session->srtp_context_   = &dtls_state->srtp_ctx;

    // 使用 StreamServer 的共享证书初始化 DTLS
    extern StreamServer streamSrv;
    if (streamSrv.m_dtlsCertPem.empty() || streamSrv.m_dtlsKeyPem.empty()) {
        LOG("[ICE] WARNING: No DTLS cert configured, DTLS disabled");
        // 暂不初始化 DTLS，只处理 STUN
    } else {
        dtls_state->dtls_initialized = dtls_state->dtls.init(
            streamSrv.m_dtlsCertPem, streamSrv.m_dtlsKeyPem);
        if (dtls_state->dtls_initialized) {
            dtls_state->dtls.setSocket(session->rtp_socket, {}); // peer 会在首包时由 recvfrom 设置
            dtls_state->dtls.startHandshake();
        }
    }

    auto dtls_start = std::chrono::steady_clock::now();

    while (session->ice_running_) {
        struct sockaddr_in peer;
        socklen_t peerLen = sizeof(peer);
        int len = recvfrom(static_cast<SOCKET_TYPE>(session->rtp_socket),
                           (char*)buf, sizeof(buf), 0,
                           (struct sockaddr*)&peer, &peerLen);

        // 检查 DTLS 握手是否超时（8秒内 state 未到 3）
        if (session->state >= S1_ICE_CONNECTED && session->state < S3_SRTP_ACTIVE) {
            auto now = std::chrono::steady_clock::now();
            if (now - dtls_start > std::chrono::seconds(8)) {
                LOG("[ICE] DTLS handshake timeout (8s), state=%d",
                    (int)session->state);
                session->ice_running_ = false;
                break;
            }
        }

        if (len < 0) {
            continue;  // 超时，继续循环
        }
        if (len < 1) continue;

        uint8_t firstByte = buf[0];

        // ---- 协议分流 ----
        // STUN:  0x00 (Binding Request) 或 0x01 (Binding Success/Error)
        // DTLS:  0x14 (ChangeCipherSpec), 0x15 (Alert), 0x16 (Handshake), 0x17 (AppData)
        // SRTP:  首字节 0x80 (RTP version 2, 无扩展/CSRC)

        // === STUN ===
        if (firstByte == 0x00 || firstByte == 0x01) {
            if (len < 20) continue;

            uint16_t msgType = (buf[0] << 8) | buf[1];
            uint32_t magic = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16)
                           | ((uint32_t)buf[6] << 8)  | (uint32_t)buf[7];
            if (magic != 0x2112A442) continue;
            if (msgType != 0x0001) continue;  // 仅处理 Binding Request

            uint8_t tid[12];
            memcpy(tid, buf + 8, 12);

            LOG("[STUN] Received Binding Request from %s:%d (peer.sin_port=%d)",
                inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), (int)peer.sin_port);

            // ---- 构造 Binding Success Response（含 MESSAGE-INTEGRITY） ----
            // ICE 要求 Success Response 必须包含 MESSAGE-INTEGRITY，
            // 否则浏览器会丢弃响应并持续重试。
            uint8_t response[128] = {};
            int pos = 0;
            // STUN Header (20 bytes)
            response[pos++] = 0x01; response[pos++] = 0x01;  // Binding Success Response
            // Length 占位，后面回填
            int lenPos = pos; pos += 2;
            response[pos++] = 0x21; response[pos++] = 0x12;  // Magic Cookie
            response[pos++] = 0xA4; response[pos++] = 0x42;
            memcpy(response + pos, tid, 12); pos += 12;

            // XOR-MAPPED-ADDRESS (12 bytes)
            response[pos++] = 0x00; response[pos++] = 0x20;  // attr type
            response[pos++] = 0x00; response[pos++] = 0x08;  // attr len = 8
            response[pos++] = 0x00;                          // reserved
            response[pos++] = 0x01;                          // IPv4
            uint16_t xorPort = ntohs(peer.sin_port) ^ 0x2112;
            response[pos++] = (xorPort >> 8) & 0xFF;
            response[pos++] = xorPort & 0xFF;
            uint32_t xorAddr = ntohl(peer.sin_addr.s_addr) ^ 0x2112A442;
            response[pos++] = (xorAddr >> 24) & 0xFF;
            response[pos++] = (xorAddr >> 16) & 0xFF;
            response[pos++] = (xorAddr >> 8)  & 0xFF;
            response[pos++] = xorAddr & 0xFF;

            // MESSAGE-INTEGRITY (24 bytes: type 2 + len 2 + hmac 20)
            int miPos = pos;
            response[pos++] = 0x00; response[pos++] = 0x08;  // attr type = 0x0008
            response[pos++] = 0x00; response[pos++] = 0x14;  // attr len = 20
            pos += 20;  // HMAC-SHA1 占位

            // ---- 添加 FINGERPRINT 占位（CRC-32，必须放在最后）----
            int attrLen = pos - 20;
            response[lenPos]     = (attrLen >> 8) & 0xFF;
            response[lenPos + 1] = attrLen & 0xFF;

            // ★ 关键：先设置最终长度（含 FINGERPRINT），再计算 HMAC。
            const std::string& icePwd = session->ice_pwd;
            if (!icePwd.empty()) {
                psa_mac_operation_t macOp = psa_mac_operation_init();
                psa_key_attributes_t keyAttr = PSA_KEY_ATTRIBUTES_INIT;
                psa_set_key_usage_flags(&keyAttr, PSA_KEY_USAGE_SIGN_MESSAGE);
                psa_set_key_algorithm(&keyAttr, PSA_ALG_HMAC(PSA_ALG_SHA_1));
                psa_set_key_type(&keyAttr, PSA_KEY_TYPE_HMAC);

                psa_key_id_t keyId = PSA_KEY_ID_NULL;
                psa_status_t ps = psa_import_key(&keyAttr,
                    (const uint8_t*)icePwd.data(), icePwd.size(), &keyId);
                psa_reset_key_attributes(&keyAttr);

                if (ps == PSA_SUCCESS) {
                    ps = psa_mac_sign_setup(&macOp, keyId, PSA_ALG_HMAC(PSA_ALG_SHA_1));
                    if (ps == PSA_SUCCESS) {
                        psa_mac_update(&macOp, response, miPos);
                        size_t macLen = 20;
                        psa_status_t ps2 = psa_mac_sign_finish(&macOp, response + miPos + 4, 20, &macLen);
                        if (ps2 != PSA_SUCCESS) {
                            LOG("[STUN] psa_mac_sign_finish failed: %d", (int)ps2);
                        } else {
                            // 打印 HMAC 用于调试
                            char hmacHex[41] = {};
                            for (int i = 0; i < 20; i++) {
                                sprintf(hmacHex + i * 2, "%02x", response[miPos + 4 + i]);
                            }
                            LOG("[STUN] MI computed, key='%s', hmac=%s", icePwd.c_str(), hmacHex);
                        }
                    } else {
                        LOG("[STUN] psa_mac_sign_setup failed: %d", (int)ps);
                    }
                    psa_destroy_key(keyId);
                } else {
                    LOG("[STUN] psa_import_key failed: %d", (int)ps);
                }
            } else {
                LOG("[STUN] WARNING: ice_pwd is empty, MI not computed");
            }

            // ---- 计算 FINGERPRINT CRC-32 ----
            // 某些浏览器（Chrome）依赖 FINGERPRINT 区分 STUN 与其他协议
            {
                // 计算 CRC-32（覆盖整个 STUN 消息，不含 FINGERPRINT 属性本身）
                int fpPos = pos;
                response[pos++] = 0x80; response[pos++] = 0x28;  // attr type = 0x8028
                response[pos++] = 0x00; response[pos++] = 0x04;  // attr len = 4
                int fpValuePos = pos;  // CRC 值写入位置
                pos += 4;              // CRC 值占位
                // 即：header + 所有属性（不含 FINGERPRINT 的 type/length/value）
                int finalAttrLen = pos - 20;
                response[lenPos]     = (finalAttrLen >> 8) & 0xFF;
                response[lenPos + 1] = finalAttrLen & 0xFF;
                // 计算 CRC-32（覆盖整个 STUN 消息，不含 FINGERPRINT 属性本身）
                // 即：header + 所有属性（不含 FINGERPRINT 的 type/length/value）
                static const uint32_t crcTable[256] = {
                    0x00000000,0x77073096,0xee0e612c,0x990951ba,0x076dc419,0x706af48f,0xe963a535,0x9e6495a3,
                    0x0edb8832,0x79dcb8a4,0xe0d5e91e,0x97d2d988,0x09b64c2b,0x7eb17cbd,0xe7b82d07,0x90bf1d91,
                    0x1db71064,0x6ab020f2,0xf3b97148,0x84be41de,0x1adad47d,0x6ddde4eb,0xf4d4b551,0x83d385c7,
                    0x136c9856,0x646ba8c0,0xfd62f97a,0x8a65c9ec,0x14015c4f,0x63066cd9,0xfa0f3d63,0x8d080df5,
                    0x3b6e20c8,0x4c69105e,0xd56041e4,0xa2677172,0x3c03e4d1,0x4b04d447,0xd20d85fd,0xa50ab56b,
                    0x35b5a8fa,0x42b2986c,0xdbbbc9d6,0xacbcf940,0x32d86ce3,0x45df5c75,0xdcd60dcf,0xabd13d59,
                    0x26d930ac,0x51de003a,0xc8d75180,0xbfd06116,0x21b4f4b5,0x56b3c423,0xcfba9599,0xb8bda50f,
                    0x2802b89e,0x5f058808,0xc60cd9b2,0xb10be924,0x2f6f7c87,0x58684c11,0xc1611dab,0xb6662d3d,
                    0x76dc4190,0x01db7106,0x98d220bc,0xefd5102a,0x71b18589,0x06b6b51f,0x9fbfe4a5,0xe8b8d433,
                    0x7807c9a2,0x0f00f934,0x9609a88e,0xe10e9818,0x7f6a0dbb,0x086d3d2d,0x91646c97,0xe6635c01,
                    0x6b6b51f4,0x1c6c6162,0x856530d8,0xf262004e,0x6c0695ed,0x1b01a57b,0x8208f4c1,0xf50fc457,
                    0x65b0d9c6,0x12b7e950,0x8bbeb8ea,0xfcb9887c,0x62dd1ddf,0x15da2d49,0x8cd37cf3,0xfbd44c65,
                    0x4db26158,0x3ab551ce,0xa3bc0074,0xd4bb30e2,0x4adfa541,0x3dd895d7,0xa4d1c46d,0xd3d6f4fb,
                    0x4369e96a,0x346ed9fc,0xad678846,0xda60b8d0,0x44042d73,0x33031de5,0xaa0a4c5f,0xdd0d7cc9,
                    0x5005713c,0x270241aa,0xbe0b1010,0xc90c2086,0x5768b525,0x206f85b3,0xb966d409,0xce61e49f,
                    0x5edef90e,0x29d9c998,0xb0d09822,0xc7d7a8b4,0x59b33d17,0x2eb40d81,0xb7bd5c3b,0xc0ba6cad,
                    0xedb88320,0x9abfb3b6,0x03b6e20c,0x74b1d29a,0xead54739,0x9dd277af,0x04db2615,0x73dc1683,
                    0xe3630b12,0x94643b84,0x0d6d6a3e,0x7a6a5aa8,0xe40ecf0b,0x9309ff9d,0x0a00ae27,0x7d079eb1,
                    0xf00f9344,0x8708a3d2,0x1e01f268,0x6906c2fe,0xf762575d,0x806567cb,0x196c3671,0x6e6b06e7,
                    0xfed41b76,0x89d32be0,0x10da7a5a,0x67dd4acc,0xf9b9df6f,0x8ebeeff9,0x17b7be43,0x60b08ed5,
                    0xd6d6a3e8,0xa1d1937e,0x38d8c2c4,0x4fdff252,0xd1bb67f1,0xa6bc5767,0x3fb506dd,0x48b2364b,
                    0xd80d2bda,0xaf0a1b4c,0x36034af6,0x41047a60,0xdf60efc3,0xa867df55,0x316e8eef,0x4669be79,
                    0xcb61b38c,0xbc66831a,0x256fd2a0,0x5268e236,0xcc0c7795,0xbb0b4703,0x220216b9,0x5505262f,
                    0xc5ba3bbe,0xb2bd0b28,0x2bb45a92,0x5cb36a04,0xc2d7ffa7,0xb5d0cf31,0x2cd99e8b,0x5bdeae1d,
                    0x9b64c2b0,0xec63f226,0x756aa39c,0x026d930a,0x9c0906a9,0xeb0e363f,0x72076785,0x05005713,
                    0x95bf4a82,0xe2b87a14,0x7bb12bae,0x0cb61b38,0x92d28e9b,0xe5d5be0d,0x7cdcefb7,0x0bdbdf21,
                    0x86d3d2d4,0xf1d4e242,0x68ddb3f8,0x1fda836e,0x81be16cd,0xf6b9265b,0x6fb077e1,0x18b74777,
                    0x88085ae6,0xff0f6a70,0x66063bca,0x11010b5c,0x8f659eff,0xf862ae69,0x616bffd3,0x166ccf45,
                    0xa00ae278,0xd70dd2ee,0x4e048354,0x3903b3c2,0xa7672661,0xd06016f7,0x4969474d,0x3e6e77db,
                    0xaed16a4a,0xd9d65adc,0x40df0b66,0x37d83bf0,0xa9bcae53,0xdebb9ec5,0x47b2cf7f,0x30b5ffe9,
                    0xbdbdf21c,0xcabac28a,0x53b39330,0x24b4a3a6,0xbad03605,0xcdd70693,0x54de5729,0x23d967bf,
                    0xb3667a2e,0xc4614ab8,0x5d681b02,0x2a6f2b94,0xb40bbe37,0xc30c8ea1,0x5a05df1b,0x2d02ef8d
                };
                auto crc32 = [&](const uint8_t* data, size_t len) -> uint32_t {
                    uint32_t crc = 0xFFFFFFFF;
                    for (size_t i = 0; i < len; i++)
                        crc = crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
                    return ~crc;
                };
                // CRC 覆盖范围：STUN header + 所有属性（不含 FINGERPRINT 本身）
                uint32_t crc = crc32(response, fpPos);
                crc ^= 0x5354554E;  // XOR with "STUN" per RFC 5389
                response[fpValuePos++] = (crc >> 24) & 0xFF;
                response[fpValuePos++] = (crc >> 16) & 0xFF;
                response[fpValuePos++] = (crc >> 8)  & 0xFF;
                response[fpValuePos++] = crc & 0xFF;
            }

            // 打印调试信息
            {
                char dbg[256] = {};
                int n = 0;
                for (int i = 0; i < pos && n < 200; i++) {
                    n += sprintf(dbg + n, "%02x", response[i]);
                }
                LOG("[STUN] Response sent to %s:%d, len=%d, hex=%s",
                    inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), pos, dbg);
            }

            sendto(static_cast<SOCKET_TYPE>(session->rtp_socket),
                   (const char*)response, pos, 0,
                   (struct sockaddr*)&peer, sizeof(peer));

            // ICE 连通性确认：收到 Binding Request 并回复 Response
            // 只在初始状态(0)时升级为1，避免 keep-alive Binding Request 把 SRTP 激活(3)降级
            if (session->state == SESSION_STATE::S0_WAITING_ICE) {
                session->state = SESSION_STATE::S1_ICE_CONNECTED;
                dtls_start = std::chrono::steady_clock::now();  // 开始 DTLS 握手计时
            }
        }
        // === DTLS ===
        else if (firstByte >= 0x14 && firstByte <= 0x18) {
            if (!dtls_state->dtls_initialized) continue;

            // 每次收到 DTLS 数据包，重置握手超时计时器
            dtls_start = std::chrono::steady_clock::now();

            // 更新对端地址（首包时绑定）
            dtls_state->dtls.setSocket(session->rtp_socket, peer);

            // 设置客户端传输标识（IP+Port），DTLS Cookie 需要它来生成 HMAC
            dtls_state->dtls.setClientTransportId(peer);

            // 将主循环 recvfrom 已消费的 DTLS 数据喂入内部缓冲区，
            // 这样 mbedtls 的 bio_recv 才能读到数据并完成握手
            dtls_state->dtls.feedData(buf, len);

            // DTLS 握手是多步骤状态机，需要循环调用 doHandshakeStep()
            // 直到返回 WANT_READ（需要等对端数据）、WANT_WRITE（需要等发送完成）或出错
            int ret;
            while (true) {
                ret = dtls_state->dtls.doHandshakeStep();

                // 握手成功：doHandshakeStep 内部已设置 handshake_done_ 并导出密钥
                if (dtls_state->dtls.isHandshakeDone()) {
                    dtls_state->srtp_ready = true;
                    session->state = SESSION_STATE::S2_DTLS_COMPLETED; // DTLS 完成

                    // 初始化 SRTP 上下文（服务端使用 server_write_key）
                    const auto& keys = dtls_state->dtls.getKeyingMaterial();
                    if (keys.ready) {
                        dtls_state->srtp_ctx = SrptProtect::initFromDtls(
                            keys, true, /* is_server */
                            0);         // ssrc 将在发送时设置

                        session->state = SESSION_STATE::S3_SRTP_ACTIVE; // SRTP 激活
                        LOG("[ICE] DTLS handshake + SRTP keys ready for socket fd="
                            + std::to_string(session->rtp_socket));
                    }
                    break;
                } else if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                           ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    // 正常等待：需要等对端发数据或等发送缓冲区就绪，退出循环
                    break;
                } else if (ret == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
                    // DTLS Cookie 验证：等待客户端重发带 Cookie 的 ClientHello
                    // 这是正常流程，退出循环等待下一个数据包
                    break;
                } else if (ret == 0) {
                    // 中间步骤成功（如 HELLO_REQUEST→CLIENT_HELLO 状态转换），
                    // 继续循环推进状态机
                    continue;
                } else {
                    // 握手失败，重置以便重试
                    char errbuf[128];
                    mbedtls_strerror(ret, errbuf, sizeof(errbuf));
                    LOG("[ICE] DTLS error: %s (0x%04X), will retry", errbuf, -ret);
                    // 浏览器可能重新发起握手，不退出循环
                    break;
                }
            }
        }
        // === SRTP (来自客户端的加密 RTP) ===
        else if (firstByte == 0x80 && dtls_state->srtp_ready) {
            std::vector<uint8_t> srtpPkt(buf, buf + len);
            std::vector<uint8_t> rtpPkt;
            int ur = SrptProtect::unprotect(dtls_state->srtp_ctx, srtpPkt, rtpPkt);
            if (ur == 0) {
                // 解密成功，RTCP 或 RTCP 回传处理
                // 对于 WebRTC 服务端，客户端通常不发送 RTP，这里忽略
            }
        }
        else {
            // 未知协议，忽略
        }
    }

    // 清理本会话的 DTLS 状态
    if (dtls_state) {
        delete dtls_state;
        session->dtls_transport_ = nullptr;
        session->srtp_context_   = nullptr;
    }
}