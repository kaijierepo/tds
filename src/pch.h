#pragma once

//功能模块

//#define ENABLE_GENICAM
//#define ENABLE_FFMPEG

#include <queue>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <iostream>
#include <exception>
#include <WinSock2.h>
#include "common.hpp"
#include "tdscore.h"
#include "json.hpp"
#include "tds.h"
#include <shared_mutex>
using json = nlohmann::json;
using namespace std;

#define _HAS_STD_BYTE 0 //windows sdk有byte类型， c++17有std::byte，解决定义冲突问题
#define GENICAM_MAIN_COMPILER VC141