#if defined(ENABLE_QJS) && defined(JHD)
#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "tdsSession.h"
#include "scriptEngine.h"
#include "scriptFunc.h"

using json = nlohmann::json;
using namespace std;

extern RPC_SESSION currentSession;
extern string g_strScriptFuncConfPath;

void initScriptFunc_jhd(JSContext* ctx, void* pDev);

#endif