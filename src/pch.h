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
#include "tds_imp.h"
#include <shared_mutex>
using json = nlohmann::json;
using namespace std;

#define _HAS_STD_BYTE 0 //windows sdk有byte类型， c++17有std::byte，解决定义冲突问题
#define GENICAM_MAIN_COMPILER VC141

#ifdef TDSDLL
//#define MG_TLS MG_TLS_NONE// Enable built-in TLS 1.3 stack
#ifndef MG_TLS
#define MG_TLS MG_TLS_BUILTIN
#endif
#elif defined(_WIN32)
#ifndef MG_TLS
#define MG_TLS MG_TLS_BUILTIN
#endif
#else
#ifndef MG_TLS
#define MG_TLS MG_TLS_NONE
#endif
#endif

extern i_tds* tds;
