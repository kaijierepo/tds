#ifdef ENABLE_JERRY_SCRIPT
#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "jerryscript.h"
#include "tdsSession.h"
#include "scriptEngine.h"
#include "scriptFunc.h"

using json = nlohmann::json;
using namespace std;

extern RPC_SESSION currentSession;

bool initScripFunc_tds(jerry_value_t global_object, vector<GLOBAL_FUNC>& m_vecGlobalFunc);
bool initGlobalFunc(jerry_value_t global_object, vector<GLOBAL_FUNC>& m_vecGlobalFunc);
bool initIODevFunc(jerry_value_t ioDev_object, void* pDev);
#endif