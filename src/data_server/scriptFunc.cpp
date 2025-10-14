#if defined(ENABLE_QJS) && defined(TDS)
#include "scriptFunc.h"
#include "logger.h"
#include "tds.h"
#include "rpcHandler.h"
#include "prj.h"
#include "scriptEngine.h"
#include "ioSrv.h"
#include "ioDev_custom.h"
#include <string>
#include <sstream>
#include <cstdint>
#include "mp.h"



static JSValue qjs_val(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);
    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    if (jArgs.size() > 0) {
        json tag = jArgs[0];

        if (tag.is_string()) {
            string sTagOrg = tag.get<string>();
            string sTag = TAG::resolveTag(sTagOrg, pEngine->m_tagContext);

            OBJ* p = prj.queryObj(sTag);
            if (p == nullptr) {
                p = prj.queryObj(sTagOrg); // 尝试原始位号

                if (p) {
                    sTag = sTagOrg;
                }
            }

            if (p) {
                if (jArgs.size() == 1) {
                    json params;
                    params["tag"] = sTag;
                    params["getStatus"] = true;
                    params["getConf"] = false;

                    json err, rlt;
                    RPC_SESSION sess;
                    tds->call("getMp", params, err, rlt, sess);

                    if (rlt != nullptr) {
                        json jVal = rlt["val"];
                        json jTime = rlt["time"];
                        string info = "val(\"" + sTag + "\") = " + jVal.dump();
                        pEngine->m_vecOutput.push_back(info);

                        string time = jTime.get<string>();

                        TIME t;
                        t.fromStr(time);

                        pEngine->m_vecValRefTime[sTag] = t.toStr(true);

                        JSValue jsVal;
                        jsonValToJsVal(jVal, ctx, jsVal);

                        return jsVal;
                    }
                    else {
                        int errCode = err["code"].get<int>();

                        string errMsg = err["message"].get<string>();
                        string errInfo = str::format("函数val执行错误,错误码:%d,错误信息:%s", errCode, errMsg.c_str());

                        pEngine->m_vecOutput.push_back(errInfo);
                        if (tds->conf->logEnable.scriptEngine) {
                            LOG("[脚本引擎]运行错误,错误信息:%s,\r\n环境位号:%s,脚本用户:%s\r\n脚本:%s", errInfo.c_str(), pEngine->m_tagContext.c_str(), pEngine->m_user.c_str(), pEngine->m_script.c_str());
                        }
                    }
                }
                else if (jArgs.size() >= 2) {
                    json time = jArgs[1];
                    if (time.is_string()) {
                        json jParams;
                        jParams["tag"] = sTag;

                        string sTime = time.get<string>();
                        jParams["time"] = sTime;

                        if (jArgs.size() >= 3) {
                            json jAggr = jArgs[2];
                            jParams["aggregate"] = jAggr;
                        }

                        json err, rlt;
                        RPC_SESSION sess;
                        tds->call("db.select", jParams, err, rlt, sess);

                        if (p->m_level == "mp") {
                            MP* pmp = (MP*)p;
                            pEngine->m_vecValRefTime[sTag] = pmp->m_stDataLastUpdate.toStr(true);
                        }

                        string info = str::format("val(\"%s\",\"%s\",%s) = ", sTag.c_str(), sTime.c_str(), jParams["aggregate"].dump().c_str());
                        if (rlt.is_array() && rlt.size() > 0) {
                            json& jDe = rlt[0];
                            json& jVal = jDe["val"];
                            JSValue ret;
                            jsonValToJsVal(jVal, ctx, ret);
                            info += jVal.dump();
                            pEngine->m_vecOutput.push_back(info);
                            return ret;
                        }
                        else {
                            info += "null";
                            pEngine->m_vecOutput.push_back(info);
                        }
                    }
                }
            }
            else {
                pEngine->m_vecOutput.push_back("无法找到指定的位号");
            }
        }
    }
    pEngine->m_bValNullInCalc = true;
    return JS_NULL;
}

static JSValue qjs_notify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);

    if (jArgs.size() == 2) {
        json jParams = jArgs[1];
        std::string method = jArgs[0].get<std::string>();

        RPC_SESSION session;
        json err, rlt;
        rpcSrv.notify(method, jParams, true);
    }

    return JS_UNDEFINED;
}

static JSValue qjs_input(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);
    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    if (jArgs.size() == 2) {
        std::string sTag = jArgs[0].get<std::string>();
        sTag = TAG::resolveTag(sTag, pEngine->m_tagContext);

        json jParams;
        jParams["tag"] = sTag;
        jParams["val"] = jArgs[1];

        RPC_SESSION session;
        json err, rlt;
        tds->call("input", jParams, err, rlt, session);
    }

    return JS_UNDEFINED;
}

static JSValue qjs_output(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);
    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    if (jArgs.size() == 2) {
        std::string sTag = jArgs[0].get<std::string>();
        sTag = TAG::resolveTag(sTag, pEngine->m_tagContext);

        json jParams;
        jParams["tag"] = sTag;
        jParams["val"] = jArgs[1];

        json err, rlt;
        RPC_SESSION session;
        session.user = pEngine->m_user;
        tds->call("output", jParams, err, rlt, session);
    }

    return JS_UNDEFINED;
}

static JSValue qjs_parseTag(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);
    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    if (jArgs.size() > 0) {
        std::string tag = jArgs[0].get<std::string>();
        std::string sTag = TAG::resolveTag(tag, pEngine->m_tagContext);

        if (tag.find("*") == std::string::npos) {
            json jTag = sTag;
            JSValue obj;
            jsonValToJsVal(jTag, ctx, obj);
            return obj;
        }
        else {
            std::vector<std::string> vecTags;
            TAG_SELECTOR ts;
            ts.init(sTag);
            prj.getTagsByTagSelector(vecTags, ts);
            json jTags = json::array();
            for (size_t i = 0; i < vecTags.size(); i++) {
                jTags.push_back(vecTags[i]);
            }
            JSValue obj;
            jsonValToJsVal(jTags, ctx, obj);
            return obj;
        }
    }

    return JS_NULL;
}

static JSValue qjs_getObj(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);
    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    if (jArgs.size() > 0) {
        json tag = jArgs[0];
        if (tag.is_string()) {
            std::string sTag = tag.get<std::string>();
            sTag = TAG::resolveTag(sTag, pEngine->m_tagContext);
            OBJ* pObj = prj.queryObj(sTag, "zh");
            if (pObj) {
                OBJ_QUERIER query;
                query.getConf = true;
                query.getStatus = true;
                query.getChild = false;
                query.getMp = false;
				yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
				yyjson_mut_val* j = yyjson_mut_obj(doc);
                pObj->toJson(j, doc,query);
                JSValue obj = yyVal_to_qjsVal(ctx,j);
                yyjson_mut_doc_free(doc);
                return obj;
            }
        }
    }

    return JS_NULL;
}

static JSValue qjs_sum(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);
    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    if (jArgs.size() > 0) {
        json tag = jArgs[0];
        if (tag.is_string()) {
            std::string sTag = tag.get<std::string>();
            sTag = TAG::resolveTag(sTag, pEngine->m_tagContext);
            json params;
            params["tag"] = sTag;
            if (jArgs.size() == 2) {
                params["invalidAsZero"] = jArgs[1];
            }
            else if (jArgs.size() == 3) {
                params["time"] = jArgs[1];
                params["invalidAsZero"] = jArgs[2];
            }

            json err, rlt;
            RPC_SESSION sess;
            tds->call("sum", params, err, rlt, sess);

            if (rlt != nullptr) {
                JSValue ret;
                jsonValToJsVal(rlt, ctx, ret);
                return ret;
            }
        }
        else if (tag.is_array()) {
            json jResolvedTag = json::array();
            for (auto& t : tag) {
                if (t.is_string()) {
                    std::string s = t.get<std::string>();
                    s = TAG::resolveTag(s, pEngine->m_tagContext);
                    jResolvedTag.push_back(s);
                }
            }

            json params;
            params["tag"] = jResolvedTag;
            if (jArgs.size() > 1) {
                params["invalidAsZero"] = jArgs[1];
            }

            json err, rlt;
            RPC_SESSION sess;
            tds->call("sum", params, err, rlt, sess);

            if (rlt != nullptr) {
                JSValue ret;
                jsonValToJsVal(rlt, ctx, ret);
                return ret;
            }
        }
    }

    return JS_NULL;
}

static JSValue qjs_setConfFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);

    if (jArgs.size() == 2) {
        json path = jArgs[0];
        json data = jArgs[1];
        if (path.is_string() && data.is_string()) {
            std::string rPath = path.get<std::string>();
            string sData = data.get<string>();

            string aPath = tds->conf->confPath + "/" + rPath;
            fs::createFolderOfPath(aPath);

			bool bRet = fs::writeFile(aPath, sData);
            if (bRet) {
                g_mapConfFile[rPath] = sData;
            }
            return JS_NewBool(ctx, bRet);
        }
    }
    return JS_NULL;
}

static JSValue qjs_getConfFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);

    if (jArgs.size() == 1) {
        json path = jArgs[0];
        if (path.is_string()) {
            std::string sPath = path.get<std::string>();
            string sData;

            sPath = tds->conf->confPath + "/" + sPath;
            bool bRet = fs::readFile(sPath, sData);
            if (bRet) {
                return JS_NewString(ctx, sData.c_str());
            }
            return JS_NULL;
        }
    }
    return JS_NULL;
}

static JSValue qjs_getConfFileBuff(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);
    if (jArgs.size() == 1) {
        json path = jArgs[0];
        if (path.is_string()) {
            std::string rPath = path.get<std::string>();
            map<string, string>::iterator iter = g_mapConfFile.find(rPath);
            if (iter != g_mapConfFile.end()) {
                return JS_NewString(ctx, iter->second.c_str());
            }
            return JS_NULL;
        }
    }
    return JS_NULL;
}

static JSValue qjs_avg(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);
    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    if (jArgs.size() > 0) {
        json tag = jArgs[0];
        if (tag.is_string()) {
            std::string sTag = tag.get<std::string>();
            sTag = TAG::resolveTag(sTag, pEngine->m_tagContext);
            json params;
            params["tag"] = sTag;

            json err, rlt;
            RPC_SESSION sess;
            tds->call("avg", params, err, rlt, sess);

            if (rlt != nullptr) {
                JSValue ret;
                jsonValToJsVal(rlt, ctx, ret);
                return ret;
            }
        }
        else if (tag.is_array()) {
            json jResolvedTag = json::array();
            for (auto& t : tag) {
                if (t.is_string()) {
                    std::string s = t.get<std::string>();
                    s = TAG::resolveTag(s, pEngine->m_tagContext);
                    jResolvedTag.push_back(s);
                }
            }

            json params;
            params["tag"] = jResolvedTag;
            json err, rlt;
            RPC_SESSION sess;
            tds->call("avg", params, err, rlt, sess);

            if (rlt != nullptr) {
                JSValue ret;
                jsonValToJsVal(rlt, ctx, ret);
                return ret;
            }
        }
    }

    return JS_NULL;
}

static JSValue qjs_ioDev_setOnline(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue dev = this_val;
    json jDev;

    jsValToJsonVal(ctx, dev, jDev);
    if (!jDev.is_object())
        return JS_NewBool(ctx, false);

    if (jDev["confNodeId"] == nullptr)
        return JS_NewBool(ctx, false);

    std::string confNodeId = jDev["confNodeId"];

    ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
    if (!p)
        return JS_NewBool(ctx, false);

    p->setOnline();

    return JS_NULL;
}

static JSValue qjs_ioDev_setDevVar(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);

    if (jArgs.size() == 2) {
        json varName = jArgs[0];
        if (!varName.is_string()) {
            return JS_NULL;
        }

        std::string sName = varName.get<std::string>();
        json params = jArgs[1];

        JSValue dev = this_val;
        json jDev;

        jsValToJsonVal(ctx, dev, jDev);

        if (!jDev.is_object())
            return JS_NewBool(ctx, false);

        if (jDev["confNodeId"] == nullptr)
            return JS_NewBool(ctx, false);

        std::string confNodeId = jDev["confNodeId"];

        ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
        if (!p)
            return JS_NewBool(ctx, false);

        p->m_mapDevVar[sName] = params;
    }

    return JS_NULL;
}

static JSValue qjs_ioDev_onRecvData(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jDev;
    jsValToJsonVal(ctx, this_val, jDev);

    json jArgs = engineArrayToJson(ctx, argv, argc);
    if (jArgs.size() != 1)
        return JS_NULL;

    json data = jArgs[0];
    if (!data.is_array())
        return JS_NULL;

    std::vector<unsigned char> vecData;
    for (auto& i : data) {
        if (i.is_number_integer()) {
            unsigned char b = i.get<int>();
            vecData.push_back(b);
        }
    }

    if (jDev.is_object() && jDev["confNodeId"] != nullptr) {
        std::string confNodeId = jDev["confNodeId"];
        ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
        if (p) {
            p->onRecvData(vecData.data(), vecData.size());
        }
    }

    return JS_NULL;
}

static JSValue qjs_ioDev_doTransaction(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jDev;
    jsValToJsonVal(ctx, this_val, jDev);

    json jArgs = engineArrayToJson(ctx, argv, argc);
    if (jArgs.size() != 1)
        return JS_NULL;

    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    json req = jArgs[0];
    std::vector<uint8_t> vecReq;
    if (req.is_array()) {
        for (auto& i : req) {
            if (i.is_number_integer()) {
                uint8_t b = i.get<int>();
                vecReq.push_back(b);
            }
        }
    }
    else if (req.is_string()) {
        std::string s = req.get<std::string>();
        vecReq = str::toBytes(s);
    }
    else {
        pEngine->m_sError = "错误的请求参数格式，必须是数组或者字符串";
        return JS_NULL;
    }

    std::vector<uint8_t> vecResp;
    if (jDev.is_object() && jDev["confNodeId"] != nullptr) {
        std::string confNodeId = jDev["confNodeId"];
        ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
        if (p && p->m_devType == "custom-device") {
            ioDev_custom* pc = (ioDev_custom*)p;
            pc->doTransaction(vecReq, vecResp);
        }
    }

    if (!vecResp.empty()) {
        json j = json::array();
        for (size_t i = 0; i < vecResp.size(); i++) {
            j.push_back(vecResp[i]);
        }
        JSValue jrr;
        jsonValToJsVal(j, ctx, jrr);
        return jrr;
    }

    return JS_NULL;
}


static JSValue qjs_ioDev_send(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jDev;
    jsValToJsonVal(ctx, this_val, jDev);

    json jArgs = engineArrayToJson(ctx, argv, argc);
    if (jArgs.size() != 1)
        return JS_NULL;

    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    json req = jArgs[0];
    std::vector<uint8_t> vecReq;
    if (req.is_array()) {
        for (auto& i : req) {
            if (i.is_number_integer()) {
                uint8_t b = i.get<int>();
                vecReq.push_back(b);
            }
        }
    }
    else if (req.is_string()) {
        std::string s = req.get<std::string>();
        vecReq = str::toBytes(s);
    }
    else {
        pEngine->m_sError = "错误的请求参数格式，必须是数组或者字符串";
        return JS_NULL;
    }

    bool sended = false;
    std::vector<uint8_t> vecResp;
    if (jDev.is_object() && jDev["confNodeId"] != nullptr) {
        std::string confNodeId = jDev["confNodeId"];
        ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
        if (p && p->m_devType == "custom-device") {
            ioDev_custom* pc = (ioDev_custom*)p;
            sended = pc->sendData(vecReq.data(),vecReq.size());
        }
    }

    return JS_NewBool(ctx,sended);
}


static JSValue qjs_ioDev_sendStr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jDev;
    jsValToJsonVal(ctx, this_val, jDev);

    json jArgs = engineArrayToJson(ctx, argv, argc);
    if (jArgs.size() != 1)
        return JS_NULL;

    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    json req = jArgs[0];
    std::vector<uint8_t> vecReq;
    string sReq = "";
    if (req.is_string()) {
		sReq = req.get<std::string>();
    }
    else {
        pEngine->m_sError = "错误的请求参数格式，必须是字符串";
        return JS_NULL;
    }

    bool sended = false;
    std::vector<uint8_t> vecResp;
    if (jDev.is_object() && jDev["confNodeId"] != nullptr) {
        std::string confNodeId = jDev["confNodeId"];
        ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
        if (p && p->m_devType == "custom-device") {
            ioDev_custom* pc = (ioDev_custom*)p;
            sended = pc->sendData((unsigned char*)sReq.c_str(), sReq.size());
        }
    }

    return JS_NewBool(ctx, sended);
}


static JSValue qjs_ioDev_getDevVar(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);

    if (jArgs.size() == 1) {
        json varName = jArgs[0];
        if (!varName.is_string()) {
            return JS_NULL;
        }
        std::string sName = varName.get<std::string>();

        json jDev;
        jsValToJsonVal(ctx, this_val, jDev);
        if (!jDev.is_object())
            return JS_NewBool(ctx, false);
        if (jDev["confNodeId"] == nullptr)
            return JS_NewBool(ctx, false);
        std::string confNodeId = jDev["confNodeId"];
        ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
        if (!p)
            return JS_NewBool(ctx, false);

        json val = p->m_mapDevVar[sName];
        JSValue jsVal;
        jsonValToJsVal(val, ctx, jsVal);
        return jsVal;
    }

    return JS_NULL;
}

static JSValue qjs_ioDev_setOffline(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jDev;
    jsValToJsonVal(ctx, this_val, jDev);

    if (!jDev.is_object())
        return JS_NewBool(ctx, false);

    if (jDev["confNodeId"] == nullptr)
        return JS_NewBool(ctx, false);

    std::string confNodeId = jDev["confNodeId"];

    ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
    if (!p)
        return JS_NewBool(ctx, false);

    p->setOffline();
    return JS_NULL;
}

static JSValue qjs_ioDev_input(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);

    json jDev;
    jsValToJsonVal(ctx, this_val, jDev);

    if (!jDev.is_object())
        return JS_NewBool(ctx, false);

    if (jDev["confNodeId"] == nullptr)
        return JS_NewBool(ctx, false);

    std::string confNodeId = jDev["confNodeId"];

    ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
    if (!p)
        return JS_NewBool(ctx, false);

    if (jArgs.size() >= 2) {
        json jVal = jArgs[0];
        json addr = jArgs[1];
        std::string chanAddr = addr.get<std::string>();
        bool bRet = p->input(jVal, chanAddr);
        return JS_NewBool(ctx, bRet);
    }
    else if (jArgs.size() == 1 && jArgs[0].is_object()) {
        json& jChans = jArgs[0];
        for (auto& [key, val] : jChans.items()) {
            p->input(val, key);
        }
        return JS_NewBool(ctx, true);
    }
    return JS_NewBool(ctx, false);
}

void initTdsFunc(JSContext* ctx, void* pDev) {
    JSValue global = JS_GetGlobalObject(ctx);

    JS_SetPropertyStr(ctx, global, "val", JS_NewCFunction(ctx, qjs_val, "val", 3));
    JS_SetPropertyStr(ctx, global, "notify", JS_NewCFunction(ctx, qjs_notify, "notify", 2));
    JS_SetPropertyStr(ctx, global, "input", JS_NewCFunction(ctx, qjs_input, "input", 2));
    JS_SetPropertyStr(ctx, global, "output", JS_NewCFunction(ctx, qjs_output, "output", 2));
    JS_SetPropertyStr(ctx, global, "parseTag", JS_NewCFunction(ctx, qjs_parseTag, "parseTag", 1));
    JS_SetPropertyStr(ctx, global, "getObj", JS_NewCFunction(ctx, qjs_getObj, "getObj", 1));
    JS_SetPropertyStr(ctx, global, "sum", JS_NewCFunction(ctx, qjs_sum, "sum", 3));
    JS_SetPropertyStr(ctx, global, "avg", JS_NewCFunction(ctx, qjs_avg, "avg", 3));
    JS_SetPropertyStr(ctx, global, "setConfFile", JS_NewCFunction(ctx, qjs_setConfFile, "setConfFile", 2));
    JS_SetPropertyStr(ctx, global, "getConfFile", JS_NewCFunction(ctx, qjs_getConfFile, "getConfFile", 1));
    JS_SetPropertyStr(ctx, global, "getConfFileBuff", JS_NewCFunction(ctx, qjs_getConfFileBuff, "getConfFileBuff", 1));

    JSValue db = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, db, "select", JS_NewCFunction(ctx, qjs_db_select, "select", 1));
    JS_SetPropertyStr(ctx, db, "insert", JS_NewCFunction(ctx, qjs_db_insert, "insert", 3));
	JS_SetPropertyStr(ctx, global, "db", db);

    if (pDev) {
        JSValue dev = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, global, "Dev", dev);
        initIODevFunc(ctx, pDev, dev);
    }
    
    JS_FreeValue(ctx, global);
}

void initIODevFunc(JSContext* ctx, void* pDev, JSValue obj) {
    JS_SetPropertyStr(ctx, obj, "setOnline", JS_NewCFunction(ctx, qjs_ioDev_setOnline, "setOnline", 0));
    JS_SetPropertyStr(ctx, obj, "setDevVar", JS_NewCFunction(ctx, qjs_ioDev_setDevVar, "setDevVar", 2));
    JS_SetPropertyStr(ctx, obj, "onRecvData", JS_NewCFunction(ctx, qjs_ioDev_onRecvData, "onRecvData", 1));
    JS_SetPropertyStr(ctx, obj, "send", JS_NewCFunction(ctx, qjs_ioDev_send, "send", 1));
    JS_SetPropertyStr(ctx, obj, "sendStr", JS_NewCFunction(ctx, qjs_ioDev_sendStr, "sendStr", 1));
    JS_SetPropertyStr(ctx, obj, "doTransaction", JS_NewCFunction(ctx, qjs_ioDev_doTransaction, "doTransaction", 1));
    JS_SetPropertyStr(ctx, obj, "getDevVar", JS_NewCFunction(ctx, qjs_ioDev_getDevVar, "getDevVar", 1));
    JS_SetPropertyStr(ctx, obj, "setOffline", JS_NewCFunction(ctx, qjs_ioDev_setOffline, "setOffline", 0));
    JS_SetPropertyStr(ctx, obj, "input", JS_NewCFunction(ctx, qjs_ioDev_input, "input", 2));
    
    ioDev* pDevTemp = (ioDev*)pDev;

    json jVal;
    JSValue value;

    jsonValToJsVal(pDevTemp->m_jDevAddr, ctx, value);
    JS_SetPropertyStr(ctx, obj, "addr", value);

    jVal = pDevTemp->m_bOnline;
    jsonValToJsVal(jVal, ctx, value);
    JS_SetPropertyStr(ctx, obj, "online", value);

    value = JS_NewString(ctx, pDevTemp->m_confNodeId.c_str());
    JS_SetPropertyStr(ctx, obj, "confNodeId", value);

    if (pDevTemp->m_vecChildDev.size() > 0) {
        JSValue vecChild = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, obj, "children", vecChild);

        for (size_t i = 0; i < pDevTemp->m_vecChildDev.size(); i++) {
            ioDev* pChildDev = pDevTemp->m_vecChildDev[i];
            JSValue qjsChildDev = JS_NewObject(ctx);
            JS_SetPropertyUint32(ctx, vecChild, (uint32_t)i, qjsChildDev);

            initIODevFunc(ctx, pChildDev, qjsChildDev);
        }
    }
}

#endif