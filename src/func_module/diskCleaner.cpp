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

// 假设LOG函数已事先定义，支持printf风格的格式化输出
// 这里仅做声明，实际实现由用户提供
extern void LOG(const char* format, ...);

namespace fs = std::filesystem;

// 日期结构，用于比较
struct Date {
    int year;
    int month;

    bool operator<(const Date& other) const {
        if (year != other.year) return year < other.year;
        return month < other.month;
    }
};

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

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        LOG("Directory does not exist or is not a directory: %s", dirPath.c_str());
        throw std::invalid_argument("Invalid directory");
    }

    // 正则表达式匹配YYYYMM格式的文件夹名称
    std::regex datePattern(R"(^\d{4}(0[1-9]|1[0-2])$)");

    for (const auto& entry : fs::directory_iterator(dirPath)) {
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
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
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
    int result = std::system(command.c_str());
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

void DiskCleaner() {

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

void DiskCleaner::run()
{
	KV_INI kvi;
   
    string iniPath = getAppDir() + "/diskCleaner.ini";
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
