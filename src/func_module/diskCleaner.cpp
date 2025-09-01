#include "diskCleaner.h"
#include <thread>
#include <filesystem>
#include "logger.h"
#include "kvIni.h"
#include <windows.h>

DiskCleaner diskCleaner;

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <regex>
#include <chrono>
#include <ctime>
#include <thread>
#include <stdexcept>
#include <cstdlib>
#include <sstream>
#include <cstdio>  // 确保包含printf所需的头文件
#include <fstream>

// 假设LOG函数已事先定义，支持printf风格的格式化输出
// 这里仅做声明，实际实现由用户提供
extern void LOG(const char* format, ...);

namespace fs = std::filesystem;

static std::wstring utf8_to_utf16(const string& u8str) {
    const char* utf8_str = u8str.c_str();
    size_t length = u8str.length();
    if (!utf8_str || length == 0) {
        return std::wstring();
    }

    // 预分配足够的空间（最坏情况：每个ASCII字符对应1个wchar_t）
    std::wstring result;
    result.reserve(length);

    const uint8_t* data = reinterpret_cast<const uint8_t*>(utf8_str);
    const uint8_t* end = data + length;

    while (data < end) {
        uint8_t c = *data;

        if (c < 0x80) {
            // 单字节UTF-8 (0-0x7F)
            result.push_back(static_cast<wchar_t>(c));
            data++;
        }
        else if ((c & 0xE0) == 0xC0) {
            // 双字节UTF-8 (0x80-0x7FF)
            if (data + 1 >= end) {
                throw std::runtime_error("Invalid UTF-8 sequence: incomplete 2-byte sequence");
            }

            uint32_t code_point = ((c & 0x1F) << 6) | (data[1] & 0x3F);
            result.push_back(static_cast<wchar_t>(code_point));
            data += 2;
        }
        else if ((c & 0xF0) == 0xE0) {
            // 三字节UTF-8 (0x800-0xFFFF)
            if (data + 2 >= end) {
                throw std::runtime_error("Invalid UTF-8 sequence: incomplete 3-byte sequence");
            }

            uint32_t code_point = ((c & 0x0F) << 12) |
                ((data[1] & 0x3F) << 6) |
                (data[2] & 0x3F);
            result.push_back(static_cast<wchar_t>(code_point));
            data += 3;
        }
        else if ((c & 0xF8) == 0xF0) {
            // 四字节UTF-8 (0x10000-0x10FFFF)，需要UTF-16代理对
            if (data + 3 >= end) {
                throw std::runtime_error("Invalid UTF-8 sequence: incomplete 4-byte sequence");
            }

            uint32_t code_point = ((c & 0x07) << 18) |
                ((data[1] & 0x3F) << 12) |
                ((data[2] & 0x3F) << 6) |
                (data[3] & 0x3F);

            // 转换为UTF-16代理对
            code_point -= 0x10000;
            wchar_t high_surrogate = static_cast<wchar_t>((code_point >> 10) + 0xD800);
            wchar_t low_surrogate = static_cast<wchar_t>((code_point & 0x3FF) + 0xDC00);

            result.push_back(high_surrogate);
            result.push_back(low_surrogate);
            data += 4;
        }
        else {
            throw std::runtime_error("Invalid UTF-8 sequence: invalid leading byte");
        }
    }

    // 调整容量以释放多余空间
    result.shrink_to_fit();
    return result;
}

// 日期结构，用于比较
struct Date {
    int year;
    int month;

    bool operator<(const Date& other) const {
        if (year != other.year) return year < other.year;
        return month < other.month;
    }
};

int _vscprintf_cross(const char* format, va_list pargs) {
    int retval;
    va_list argcopy;
    va_copy(argcopy, pargs);
    retval = vsnprintf(NULL, 0, format, argcopy);
    va_end(argcopy);
    return retval;
}

struct TIME {
    unsigned short wYear;
    unsigned short wMonth;
    unsigned short wDay;
    unsigned short wHour;
    unsigned short wMinute;
    unsigned short wSecond;
    unsigned short wMilliseconds;
    unsigned short wDayOfWeek;

    TIME() {
        memset(this, 0, sizeof(*this));
    }

    void setNow();

    time_t toUnixTime();
    void fromUnixTime(time_t t, int milli = 0);

    static 	long long calcTimePassMilliSecond(TIME& lastTime)
    {
        TIME nowTime;
        nowTime.setNow();
        time_t now = nowTime.toUnixTime();
        time_t last = lastTime.toUnixTime();
        time_t second = now - last;
        time_t milli = nowTime.wMilliseconds - lastTime.wMilliseconds;
        milli = second * 1000 + milli;
        return milli;
    }
};

void TIME::setNow() {
    auto now = std::chrono::system_clock::now();
    unsigned short milli = (unsigned short)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
        - std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;
    time_t tt = std::chrono::system_clock::to_time_t(now);
    fromUnixTime(tt, milli);
}

time_t  TIME::toUnixTime() {
    tm temptm = { wSecond, wMinute, wHour,wDay,wMonth - 1,wYear - 1900,wDayOfWeek, 0, 0 };
    time_t unixTime = mktime(&temptm);
    return unixTime;
}

void  TIME::fromUnixTime(time_t unixTime, int milli) {
    static std::mutex mtx;
    mtx.lock();
    tm time_tm = *localtime(&unixTime);  //线程安全linux下推荐用localtime_r，win下推荐用localtime_s，此处为方便直接加个锁
    mtx.unlock();

    wYear = time_tm.tm_year + 1900;
    wMonth = time_tm.tm_mon + 1;
    wDay = time_tm.tm_mday;
    wHour = time_tm.tm_hour;
    wMinute = time_tm.tm_min;
    wSecond = time_tm.tm_sec;
    wMilliseconds = milli;
    wDayOfWeek = time_tm.tm_wday;
}

/**
 * 从文件夹名称解析日期（YYYYMM格式）
 * @param folderName 文件夹名称
 * @return 解析出的日期结构，解析失败抛出异常
 */
Date parseFolderDate(const std::string& folderName) {
    // 验证格式：6位数字
    if (folderName.length() != 6 || !std::all_of(folderName.begin(), folderName.end(), ::isdigit)) {
        throw std::invalid_argument("Invalid folder name format");
    }

    int year = std::stoi(folderName.substr(0, 4));
    int month = std::stoi(folderName.substr(4, 2));

    if (month < 1 || month > 12) {
        throw std::invalid_argument("Invalid month value");
    }

    return { year, month };
}

/**
 * 获取当前日期（年和月）
 * @return 当前日期结构
 */
Date getCurrentDate() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time = *std::localtime(&now_time);

    return {
        local_time.tm_year + 1900,  // tm_year是从1900年开始计算的
        local_time.tm_mon + 1       // tm_mon是从0开始的（0-11）
    };
}

/**
 * 计算两个日期之间的月份差
 * @param earlier 较早的日期
 * @param later 较晚的日期
 * @return 月份差
 */
int calculateMonthDifference(const Date& earlier, const Date& later) {
    return (later.year - earlier.year) * 12 + (later.month - earlier.month);
}

/**
 * 获取当前时间字符串（用于日志）
 */
std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time = *std::localtime(&now_time);

    char timeStr[20];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &local_time);
    return std::string(timeStr);
}

/**
 * 获取目录下所有符合YYYYMM格式的文件夹
 * @param dirPath 目录路径
 * @return 符合条件的文件夹名称列表
 */
std::vector<std::pair<std::string, Date>> getDateFolders(const std::string& dirPath) {
    std::vector<std::pair<std::string, Date>> result;

    if (!fs::exists(utf8_to_utf16(dirPath)) || !fs::is_directory(utf8_to_utf16(dirPath))) {
        LOG("Directory does not exist or is not a directory: %s", dirPath.c_str());
        throw std::invalid_argument("Invalid directory");
    }

    // 正则表达式匹配YYYYMM格式的文件夹名称
    std::regex datePattern(R"(^\d{4}(0[1-9]|1[0-2])$)");

    for (const auto& entry : fs::directory_iterator(utf8_to_utf16(dirPath))) {
        if (entry.is_directory()) {
            std::string folderName = entry.path().filename().string();
            if (std::regex_match(folderName, datePattern)) {
                try {
                    Date date = parseFolderDate(folderName);
                    result.emplace_back(folderName, date);
                }
                catch (const std::exception& e) {
                    LOG("Warning: Could not parse folder %s: %s", folderName.c_str(), e.what());
                }
            }
        }
    }

    return result;
}

/**
 * 使用系统命令快速删除目录
 * @param dirPath 要删除的目录路径
 * @return 操作是否成功
 */
bool fastDeleteDirectory(const std::string& dirPath) {
    // 检查目录是否存在
    if (!fs::exists(utf8_to_utf16(dirPath)) || !fs::is_directory(utf8_to_utf16(dirPath))) {
        LOG("Directory does not exist: %s", dirPath.c_str());
        return false;
    }

    std::string command;
#ifdef _WIN32
    // Windows系统使用rd命令
    // 路径包含空格时需要用双引号包裹
    command = "rd /s /q \"" + dirPath + "\"";
#else
    // Linux/macOS系统使用rm命令
    command = "rm -rf \"" + dirPath + "\"";
#endif

    //LOG("Executing command: %s", command.c_str());
    // 执行系统命令
#ifdef _WIN32
    TIME tStart;
    tStart.setNow();

    std::wstring wcommand = utf8_to_utf16(command);
    int result = _wsystem(wcommand.c_str());
    int costMilli = TIME::calcTimePassMilliSecond(tStart);

    LOG("文件夹路径：%s, 删除时长：%dms", dirPath.c_str(), costMilli);
#else
    int result = std::system(command.c_str());
#endif

    return result == 0;
}

/**
 * 执行一次清理操作
 * @param dirPath 目标目录
 * @param saveMonthCount 保留的月份数量
 * @return 被删除的文件夹数量
 */
int performCleanup(const std::string& dirPath, int saveMonthCount) {
    if (saveMonthCount < 0) {
        LOG("Invalid saveMonthCount: %d (must be non-negative)", saveMonthCount);
        throw std::invalid_argument("saveMonthCount must be non-negative");
    }

    auto dateFolders = getDateFolders(dirPath);
    Date currentDate = getCurrentDate();
    int deletedCount = 0;
    std::string currentTime = getCurrentTimeString();

    //LOG("[%s] Starting cleanup. Found %zu date folders.", currentTime.c_str(), dateFolders.size());

    for (const auto& [folderName, date] : dateFolders) {
        int monthsDiff = calculateMonthDifference(date, currentDate);
        if (monthsDiff >= saveMonthCount) {
            fs::path folderPath = fs::path(dirPath) / folderName;
            std::string pathStr = folderPath.string();

            if (fastDeleteDirectory(pathStr)) {
                LOG("删除成功,%s", folderName.c_str());
                deletedCount++;
            }
            else {
                LOG("删除失败,%s", folderName.c_str());
            }
        }
    }

    return deletedCount;
}

void cleanThread() {
    while (true) {
        try {
            int deleted = performCleanup(diskCleaner.dbDir, diskCleaner.saveMonthCount);
        }
        catch (const std::exception& e) {
            LOG("Cleanup error: %s", e.what());
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

static std::string getAppDir() {
#ifdef _WIN32
    char buffer[MAX_PATH] = { 0 };
    // 获取当前进程的可执行文件完整路径
    if (GetModuleFileNameA(NULL, buffer, MAX_PATH) == 0) {
        return "";
    }

    std::string path(buffer);
    // Windows路径使用反斜杠或正斜杠作为分隔符
    size_t last_slash = path.find_last_of("\\/");
    if (last_slash != std::string::npos) {
        return path.substr(0, last_slash);
    }
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len == -1) {
        return "";
    }
    buffer[len] = '\0';

    std::string path(buffer);
    // Linux/macOS使用正斜杠作为分隔符
    size_t last_slash = path.find_last_of('/');
    if (last_slash != std::string::npos) {
        return path.substr(0, last_slash);
    }
#endif
    return ""; // 如果没有找到路径分隔符
}

static string defaultConf() {
    string s = R"(
dbPath=          
saveMonthCount=36
)";
    return s;
}

static bool writeFile(string path, char* data, size_t len)
{
    FILE* fp = nullptr;
#ifdef _WIN32
    wstring wpath = utf8_to_utf16(path);
    _wfopen_s(&fp, wpath.c_str(), L"wb");
#else
    fp = fopen(path.c_str(), "wb");
#endif
    if (fp)
    {
        fwrite(data, 1, len, fp);
        fclose(fp);
        return true;
    }
    else
    {
        printf("writeFile error");
    }
    return false;
}

void DiskCleaner::run() {
	KV_INI kvi;
   
    string iniPath = getAppDir() + "/diskCleaner.ini";

	if (!fs::exists(utf8_to_utf16(iniPath))) {
        string s = defaultConf();
		writeFile(iniPath,(char*) s.c_str(),s.length());
		return;
	}

	kvi.load(iniPath);

	dbDir = kvi.getValStr("dbPath", "");
	saveMonthCount = kvi.getValInt("saveMonthCount", 36);

	if (dbDir == "") {
		LOG("请先在diskCleaner设置dbPath为数据库路径");
	}
	else {
		thread t(cleanThread);
		t.detach();
		LOG("diskCleaner启动,数据库路径=%s,保留数据=%d个月", dbDir.c_str(), saveMonthCount);
	}
}
