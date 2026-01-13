#include "rtspRelay.h"
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
void RTSPRelay::md5Init(MD5Context* context) {
    context->count[0] = 0;
    context->count[1] = 0;

    // 初始化状态 (魔法数)
    context->state[0] = 0x67452301;
    context->state[1] = 0xefcdab89;
    context->state[2] = 0x98badcfe;
    context->state[3] = 0x10325476;
}

// MD5更新
void RTSPRelay::md5Update(MD5Context* context, const uint8_t* data, size_t length) {
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
void RTSPRelay::md5Final(MD5Context* context, uint8_t digest[16]) {
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
void RTSPRelay::md5Transform(uint32_t state[4], const uint8_t block[64]) {
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
std::string RTSPRelay::md5Hex(const std::string& input) {
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
std::string RTSPRelay::base64Encode(const std::string& input) {
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

RTSPRelay::Connection::Connection() : sockfd_(RTSPRelay::kInvalidSocket) {
}

RTSPRelay::Connection::~Connection() {
    disconnect();
}

bool RTSPRelay::Connection::connect(const std::string& host, int port, int timeout_ms) {
    disconnect();
    last_error_ = 0;

    // 创建socket
    sockfd_ = static_cast<RTSPRelay::SocketHandle>(socket(AF_INET, SOCK_STREAM, 0));
    if (sockfd_ == RTSPRelay::kInvalidSocket) {
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
            sockfd_ = RTSPRelay::kInvalidSocket;
            return false;
        }
        memcpy(&serv_addr.sin_addr, server->h_addr, server->h_length);
    }

    // 连接
    if (::connect(static_cast<SOCKET_TYPE>(sockfd_), (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        last_error_ = SOCKET_ERROR_NUM;
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(sockfd_));
        sockfd_ = RTSPRelay::kInvalidSocket;
        return false;
    }

    host_ = host;
    port_ = port;

    // 恢复为阻塞模式
    setSocketTimeout(0);
    last_error_ = 0;

    return true;
}

void RTSPRelay::Connection::disconnect() {
    if (sockfd_ != RTSPRelay::kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(sockfd_));
        sockfd_ = RTSPRelay::kInvalidSocket;
    }
    host_.clear();
    port_ = 0;
    last_error_ = 0;
}

bool RTSPRelay::Connection::isConnected() const {
    return sockfd_ != RTSPRelay::kInvalidSocket;
}

int RTSPRelay::Connection::send(const void* data, size_t size, int timeout_ms) {
    if (sockfd_ == RTSPRelay::kInvalidSocket) return -1;

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

int RTSPRelay::Connection::receive(void* buffer, size_t size, int timeout_ms) {
    if (sockfd_ == RTSPRelay::kInvalidSocket) return -1;

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

int RTSPRelay::Connection::receiveHttpResp(std::string& response, int timeout_ms) {
    if (sockfd_ == RTSPRelay::kInvalidSocket) return -1;

    int total = 0;
    char buf[1000] = { 0 };

    if (timeout_ms > 0) {
        setSocketTimeout(timeout_ms);
    }

    while (true) {
        int n = static_cast<int>(::recv(static_cast<SOCKET_TYPE>(sockfd_), buf, (int)sizeof(buf), 0));
        if (n <= 0) {
            last_error_ = (n < 0) ? SOCKET_ERROR_NUM : 0;
            if (timeout_ms > 0) setSocketTimeout(0);
            return (total > 0) ? total : n;
        }

        total += n;
        response.append(buf, n);

        if (IsValidPkt_HTTP(response, static_cast<size_t>(total))) {
            break;
        }
    }

    if (timeout_ms > 0) {
        setSocketTimeout(0);
    }

    last_error_ = 0;
    return total;
}

bool RTSPRelay::Connection::setSocketTimeout(int timeout_ms) {
    if (sockfd_ == RTSPRelay::kInvalidSocket) return false;

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
// RTSPRelay 实现
// ============================================================================

RTSPRelay::RTSPRelay() {
    stats_.start_time = std::chrono::steady_clock::now();
    stats_.last_frame_time = std::chrono::steady_clock::now();
}

RTSPRelay::~RTSPRelay() {
    stop();
}

bool RTSPRelay::start(const Config& config) {
    if (running_) {
        logError("Already running");
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

    worker_thread_ = std::thread(&RTSPRelay::workerThread, this);
    stats_thread_ = std::thread(&RTSPRelay::statsThread, this);

    logInfo("RTSP relay started");
    logInfo("Source: " + config_.source_url);
    logInfo("Target: " + config_.target_url);

    return true;
}

void RTSPRelay::stop() {
    if (!running_) return;

    stopping_ = true;
    streaming_ = false;
    running_ = false;

    cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    if (rtp_thread_.joinable()) {
        rtp_thread_.join();
    }

    if (control_thread_.joinable()) {
        control_thread_.join();
    }

    if (stats_thread_.joinable()) {
        stats_thread_.join();
    }

    teardown();

    setState(State::IDLE, "Stopped");
    logInfo("RTSP relay stopped");
}

void RTSPRelay::restart() {
    stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    start(config_);
}

RTSPRelay::State RTSPRelay::getState() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

bool RTSPRelay::isRunning() {
    return running_;
}

RTSPRelay::Statistics RTSPRelay::getStatistics() {
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

void RTSPRelay::setStatusCallback(StatusCallback cb) {
    status_callback_ = cb;
}

void RTSPRelay::setFrameCallback(FrameCallback cb) {
    frame_callback_ = cb;
}

void RTSPRelay::setErrorCallback(ErrorCallback cb) {
    error_callback_ = cb;
}

// ============================================================================
// 认证相关函数
// ============================================================================

std::string RTSPRelay::calculateBasicAuth(const AuthInfo& auth) {
    std::string credentials = auth.username + ":" + auth.password;
    return "Basic " + base64Encode(credentials);
}

std::string RTSPRelay::calculateDigest(const std::string& method, const std::string& uri,
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

bool RTSPRelay::parseWWWAuthenticate(const std::string& response, AuthInfo& auth) {
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

void RTSPRelay::updateAuthHeader(AuthInfo& auth, const std::string& method, const std::string& uri) {
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

bool RTSPRelay::rtspDescribe(Connection& conn, const std::string& url,
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
                << "User-Agent: RTSPRelay/1.0\r\n";

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
                session = extractSessionID(response);
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

bool RTSPRelay::rtspSetup(Connection& conn, const std::string& url,
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

    std::string setup_url = url;
    if (!stream.control_url.empty()) {
        setup_url = stream.control_url;
        if (!(setup_url.rfind("rtsp://", 0) == 0 || setup_url.rfind("rtsps://", 0) == 0)) {
            if (!url.empty() && !setup_url.empty() && setup_url.front() == '/') {
                size_t scheme_pos = url.find("://");
                if (scheme_pos != std::string::npos) {
                    size_t authority_end = url.find('/', scheme_pos + 3);
                    std::string base = (authority_end == std::string::npos) ? url : url.substr(0, authority_end);
                    setup_url = base + setup_url;
                }
                else {
                    setup_url = url + setup_url;
                }
            }
            else {
                if (!url.empty() && url.back() == '/') {
                    setup_url = url + setup_url;
                }
                else {
                    setup_url = url + "/" + setup_url;
                }
            }
        }
    }

    std::stringstream request;
    request << "SETUP " << setup_url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: RTSPRelay/1.0\r\n";

    if (!host_header.empty()) {
        request << "Host: " << host_header << "\r\n";
    }

    if (!session.empty()) {
        request << "Session: " << session << "\r\n";
    }

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "SETUP", setup_url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Transport: RTP/AVP;unicast;"
        << (record_mode ? "mode=record;" : "")
        << "client_port=" << stream.client_port << "\r\n"
        << "\r\n";

    const std::string req = request.str();
    logVerbose(">> SETUP " + setup_url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("SETUP send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    int rc = conn.receiveHttpResp(response, 5000);
    if (rc <= 0) {
        logError("SETUP recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
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

bool RTSPRelay::rtspPlay(Connection& conn, const std::string& url,
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
        << "User-Agent: RTSPRelay/1.0\r\n"
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

bool RTSPRelay::rtspTeardown(Connection& conn, const std::string& url,
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
        << "User-Agent: RTSPRelay/1.0\r\n"
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

bool RTSPRelay::rtspAnnounce(Connection& conn, const std::string& url,
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
        << "User-Agent: RTSPRelay/1.0\r\n"
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

bool RTSPRelay::rtspRecord(Connection& conn, const std::string& url,
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
        << "User-Agent: RTSPRelay/1.0\r\n"
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

bool RTSPRelay::rtspGetParameter(Connection& conn, const std::string& url,
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
        << "User-Agent: RTSPRelay/1.0\r\n"
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

void RTSPRelay::workerThread() {
    logInfo("Worker thread started");

    while (running_ && !stopping_) {
        try {
            if (!connectToSource()) {
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

            rtp_thread_ = std::thread(&RTSPRelay::rtpThread, this);
            control_thread_ = std::thread(&RTSPRelay::controlThread, this);

            setState(State::PLAYING, "Streaming started");

            // 等待结束
            while (running_ && !stopping_) {
                {
                    std::unique_lock<std::mutex> lock(state_mutex_);
                    cv_.wait_for(lock, std::chrono::seconds(1));

                    if (state_ == State::S_ERROR) {
                        break;
                    }
                }

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

            streaming_ = false;
            cv_.notify_all();

            if (rtp_thread_.joinable()) {
                rtp_thread_.join();
            }

            if (control_thread_.joinable()) {
                control_thread_.join();
            }

            teardown();

        }
        catch (const std::exception& e) {
            setError(std::string("Worker thread exception: ") + e.what(), 1000);
        }

        // 确保在重连尝试前停止会话线程并清理
        streaming_ = false;
        cv_.notify_all();

        if (rtp_thread_.joinable()) {
            rtp_thread_.join();
        }

        if (control_thread_.joinable()) {
            control_thread_.join();
        }

        teardown();

        if (running_ && !stopping_ && shouldReconnect()) {
            doReconnect();
        }
    }

    logInfo("Worker thread stopped");
}

bool RTSPRelay::connectToSource() {
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

    logInfo("Connected to source server");

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

    setState(State::CONNECTED, "Source connected");
    return true;
}

bool RTSPRelay::setupStreams() {
    setState(State::CONNECTING, "Setting up streams");

    // 重置之前的套接字
    if (rtp_socket_ != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_socket_));
        rtp_socket_ = kInvalidSocket;
    }
    if (rtcp_socket_ != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_socket_));
        rtcp_socket_ = kInvalidSocket;
    }
    target_rtp_port_ = 0;

    // 创建RTP接收套接字
    rtp_socket_ = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
    if (rtp_socket_ == kInvalidSocket) {
        setError("Failed to create RTP socket", 3001);
        return false;
    }

    // 绑定到任意端口
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(0);

    if (::bind(static_cast<SOCKET_TYPE>(rtp_socket_), (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        setError("Failed to bind RTP socket", 3002);
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_socket_));
        rtp_socket_ = kInvalidSocket;
        return false;
    }

    // 获取绑定的端口
    socklen_t len = sizeof(local_addr);
    getsockname(static_cast<SOCKET_TYPE>(rtp_socket_), (struct sockaddr*)&local_addr, &len);
    int rtp_port = ntohs(local_addr.sin_port);
    if (rtp_port <= 0 || rtp_port >= 65535) {
        setError("Invalid RTP port selected", 3002);
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_socket_));
        rtp_socket_ = kInvalidSocket;
        return false;
    }

    // 创建RTCP套接字
    rtcp_socket_ = static_cast<SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
    if (rtcp_socket_ == kInvalidSocket) {
        setError("Failed to create RTCP socket", 3001);
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_socket_));
        rtp_socket_ = kInvalidSocket;
        return false;
    }

    struct sockaddr_in rtcp_addr;
    memset(&rtcp_addr, 0, sizeof(rtcp_addr));
    rtcp_addr.sin_family = AF_INET;
    rtcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    rtcp_addr.sin_port = htons(static_cast<uint16_t>(rtp_port + 1));

    if (::bind(static_cast<SOCKET_TYPE>(rtcp_socket_), (struct sockaddr*)&rtcp_addr, sizeof(rtcp_addr)) < 0) {
        setError("Failed to bind RTCP socket", 3002);
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_socket_));
        rtcp_socket_ = kInvalidSocket;
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_socket_));
        rtp_socket_ = kInvalidSocket;
        return false;
    }

    source_video_info_.client_port = std::to_string(rtp_port) + "-" + std::to_string(rtp_port + 1);
    logInfo("Local RTP/RTCP ports: " + source_video_info_.client_port);

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

    // Play成功后表示拉流成功，下面开始推流

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

    // 发送RECORD到目标
    if (!rtspRecord(*target_conn_, config_.target_url, target_session_)) {
        setError("RECORD failed", 3009);
        return false;
    }

    return true;
}

void RTSPRelay::rtpThread() {
    logInfo("RTP thread started");

    std::vector<uint8_t> buffer(config_.buffer_size);
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.last_frame_time = std::chrono::steady_clock::now();
    }

    while (running_ && !stopping_ && streaming_) {
        // 设置接收超时
#ifdef _WIN32
        DWORD tv = 1000;
        setsockopt(static_cast<SOCKET_TYPE>(rtp_socket_), SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(static_cast<SOCKET_TYPE>(rtp_socket_), SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

        long received = recvfrom(static_cast<SOCKET_TYPE>(rtp_socket_), (char*)buffer.data(), (int)buffer.size(), 0,
            (struct sockaddr*)&from_addr, &from_len);

        if (received > 0) {
            const auto now = std::chrono::steady_clock::now();

            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes_received += received;
                stats_.frames_received++;
                stats_.last_frame_time = now;
            }

            // 解析RTP包
            RTPPacket packet;
            if (packet.parse(buffer.data(), received)) {
                // 转发到目标
                forwardRTPPacket(packet);

                // 调用回调
                if (frame_callback_) {
                    frame_callback_(packet.payload.data(), packet.payload.size(), packet.timestamp);
                }

                logVerbose("RTP packet: seq=" + std::to_string(packet.sequence_number) +
                    " ts=" + std::to_string(packet.timestamp) +
                    " size=" + std::to_string(received));
            }
        }
        else if (received < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err != WSAETIMEDOUT && err != WSAEWOULDBLOCK && err != WSAECONNRESET) {
#else
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
#endif
                logError("recvfrom error: " + std::to_string(err));
                break;
            }
            }
        }

    logInfo("RTP thread stopped");
    }

void RTSPRelay::controlThread() {
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

void RTSPRelay::statsThread() {
    logInfo("Statistics thread started");

    while (running_ && !stopping_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto stats = getStatistics();

        if (config_.verbose) {
            std::stringstream ss;
            ss << "Stats: frames=" << stats.frames_received
                << " forwarded=" << stats.frames_forwarded
                << " fps=" << std::fixed << std::setprecision(1) << stats.fps
                << " bitrate=" << stats.bitrate << "kbps"
                << " errors=" << stats.errors;
            logDebug(ss.str());
        }
    }

    logInfo("Statistics thread stopped");
}

void RTSPRelay::teardown() {
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

    // 清除认证信息（保留用户名密码）
    source_auth_.realm.clear();
    source_auth_.nonce.clear();
    source_auth_.authorization_header.clear();

    target_auth_.realm.clear();
    target_auth_.nonce.clear();
    target_auth_.authorization_header.clear();

    if (rtp_socket_ != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtp_socket_));
        rtp_socket_ = kInvalidSocket;
    }
    if (rtcp_socket_ != kInvalidSocket) {
        CLOSE_SOCKET(static_cast<SOCKET_TYPE>(rtcp_socket_));
        rtcp_socket_ = kInvalidSocket;
    }
}

// ============================================================================
// SDP处理函数
// ============================================================================

bool RTSPRelay::parseSDP(const std::string & sdp, StreamInfo & video_info, StreamInfo & audio_info) {
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

std::string RTSPRelay::generateSDP(const StreamInfo & video_info, const StreamInfo & audio_info) {
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

bool RTSPRelay::URLComponents::parse(const std::string & url, URLComponents & components) {
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

void RTSPRelay::forwardRTPPacket(const RTPPacket & packet) {
    if (!target_conn_ || target_rtp_port_ == 0 || rtp_socket_ == kInvalidSocket) {
        return;
    }

    // 构建目标地址
    URLComponents target_url;
    if (!URLComponents::parse(config_.target_url, target_url)) {
        return;
    }

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(target_rtp_port_);

    if (inet_pton(AF_INET, target_url.host.c_str(), &target_addr.sin_addr) <= 0) {
        struct hostent* server = gethostbyname(target_url.host.c_str());
        if (!server) return;
        memcpy(&target_addr.sin_addr, server->h_addr, server->h_length);
    }

    // 序列化并发送RTP包
    auto data = packet.serialize();
    int sent = sendto(static_cast<SOCKET_TYPE>(rtp_socket_), (const char*)data.data(), (int)data.size(), 0,
        (struct sockaddr*)&target_addr, sizeof(target_addr));
    if (sent != static_cast<int>(data.size())) {
#ifdef _WIN32
        int err = WSAGetLastError();
#else
        int err = errno;
#endif
        logError("sendto error: sent=" + std::to_string(sent) + " err=" + std::to_string(err));
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.errors++;
        return;
    }

    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.bytes_forwarded += data.size();
    stats_.frames_forwarded++;
}

bool RTSPRelay::RTPPacket::parse(const uint8_t * data, size_t size) {
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

std::vector<uint8_t> RTSPRelay::RTPPacket::serialize() const {
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

std::string RTSPRelay::extractSessionID(const std::string & response) {
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

std::string RTSPRelay::extractTransport(const std::string & response) {
    size_t pos = response.find("Transport: ");
    if (pos == std::string::npos) return "";

    size_t end = response.find("\r\n", pos);
    return response.substr(pos + 11, end - pos - 11);
}

std::string RTSPRelay::generateCSeq() {
    static std::atomic<int> counter{ 1 };
    return std::to_string(counter++);
}

void RTSPRelay::setError(const std::string & error, int code) {
    logError("Error [" + std::to_string(code) + "]: " + error);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.errors++;
    }

    if (error_callback_) {
        error_callback_(error, code);
    }

    setState(State::S_ERROR, error);
}

void RTSPRelay::setState(State new_state, const std::string & msg) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = new_state;
    }

    if (status_callback_) {
        status_callback_(new_state, msg);
    }

    logInfo("State changed to " + std::to_string(static_cast<int>(new_state)) + ": " + msg);
    cv_.notify_all();
}

bool RTSPRelay::shouldReconnect() const {
    if (config_.max_retries > 0 && retry_count_ >= config_.max_retries) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - last_reconnect_time_ < std::chrono::milliseconds(config_.retry_interval)) {
        return false;
    }

    return true;
}

void RTSPRelay::doReconnect() {
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

void RTSPRelay::logInfo(const std::string & msg) const {
    std::cout << "[INFO] " << msg << std::endl;
}

void RTSPRelay::logError(const std::string & msg) const {
    std::cerr << "[ERROR] " << msg << std::endl;
}

void RTSPRelay::logDebug(const std::string & msg) const {
    if (config_.verbose) {
        std::cout << "[DEBUG] " << msg << std::endl;
    }
}

void RTSPRelay::logVerbose(const std::string & msg) const {
    if (config_.verbose) {
        std::cout << "[VERBOSE] " << msg << std::endl;
    }
}