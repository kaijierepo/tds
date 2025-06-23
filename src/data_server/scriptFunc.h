#if defined(ENABLE_QJS) && defined(TDS)
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

void initTdsFunc(JSContext* ctx);

#endif