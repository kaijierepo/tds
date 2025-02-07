#pragma once

//功能模块
//
//#define ENABLE_GENICAM
//#define ENABLE_FFMPEG

#include <queue>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <iostream>
#include <exception>
#include "common.h"

#include "json.hpp"
#include "tds.h"
#include <shared_mutex>
using json = nlohmann::json;
using namespace std;

#define _HAS_STD_BYTE 0 //windows sdk有byte类型， c++17有std::byte，解决定义冲突问题
#define GENICAM_MAIN_COMPILER VC141

#ifdef TDSDLL
#define MG_TLS MG_TLS_NONE// Enable built-in TLS 1.3 stack
#elif defined(_WIN32)
#define MG_TLS MG_TLS_BUILTIN
#else
#define MG_TLS MG_TLS_NONE
#endif