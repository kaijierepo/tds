#ifndef TDS_DATA_SERVER_SCRIPTFUNC_H
#define TDS_DATA_SERVER_SCRIPTFUNC_H

#if defined(ENABLE_QJS) && defined(TDS)
#include <string>
#include <map>
#include "json.hpp"
#include "tdsSession.h"
#include "scriptEngine.h"
#include "scriptFunc.h"

using json = nlohmann::json;


extern RPC_SESSION currentSession;

void initTdsFunc(JSContext* ctx, void* pDev);
void initIODevFunc(JSContext* ctx, void* pDev, JSValue obj);

#endif
#endif /* TDS_DATA_SERVER_SCRIPTFUNC_H */
