#ifdef ENABLE_QJS
#include "ScriptEngine.h"
#include "tds.h"
#include "cutils.h"
#include "quickjs-libc.h"
#include <sstream>
#include "yyjson.h"
#include <limits>
#include <cmath>
#include "logger.h"
#include "quickjs.h"

#ifdef ENABLE_QJS_HTTP
#include "mongoose.h"
#endif

#include "windows.h"

thread_local ScriptEngine* pEngine;

namespace tJSEngine {
    int parseStopBits(string s) {
        if (s == "1") {
            return 0;
        }
        else if (s == "1.5") {
            return 1;
        }
        else if (s == "2") {
            return 2;
        }

        return 0;
    }

    int parseParity(string s) {
        if (s == "None") {
            return 0;
        }
        else if (s == "Odd") {
            return 1;
        }
        else if (s == "Even") {
            return 2;
        }
        else if (s == "Mark") {
            return 3;
        }
        else if (s == "Space") {
            return 4;
        }

        return 0;
    }

    std::string pointerToString(void* ptr) {
        uintptr_t ptrVal = reinterpret_cast<uintptr_t>(ptr);
        std::ostringstream oss;
        oss << "0x" << std::hex << ptrVal;
        return oss.str();
    }

    void* stringToPointer(const std::string& str) {
        char* endPtr;
        uintptr_t ptrVal = std::strtoull(str.c_str(), &endPtr, 0);
        return reinterpret_cast<void*>(ptrVal);
    }
}

// 递归将 JS 值转换为 yyjson 值
static yyjson_mut_val* js_value_to_yyjson(JSContext* ctx, yyjson_mut_doc* doc, JSValueConst val, std::vector<JSValueConst>& visited);

// 处理对象类型
static yyjson_mut_val* handle_object(JSContext* ctx, yyjson_mut_doc* doc, JSValueConst obj, std::vector<JSValueConst>& visited) {
    // 检查循环引用
    for (auto& v : visited) {
        if (JS_VALUE_GET_PTR(v) == JS_VALUE_GET_PTR(obj)) {
            return yyjson_mut_str(doc, "[Circular Reference]");
        }
    }

    visited.push_back(obj);

    // 创建新的 JSON 对象
    yyjson_mut_val* json_obj = yyjson_mut_obj(doc);

    // 获取属性枚举
    JSPropertyEnum* props = nullptr;
    uint32_t prop_count = 0;

    if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, obj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
        visited.pop_back();
        return json_obj; // 空对象
    }

    // 遍历所有属性
    for (uint32_t i = 0; i < prop_count; i++) {
        JSAtom atom = props[i].atom;
        const char* key = JS_AtomToCString(ctx, atom);

        if (!key) continue;

        JSValue prop_val = JS_GetProperty(ctx, obj, atom);
        yyjson_mut_val* json_val = js_value_to_yyjson(ctx, doc, prop_val, visited);

        // 添加到 JSON 对象
        yyjson_mut_obj_add_val(doc,json_obj, key, json_val);

        JS_FreeCString(ctx, key);
        JS_FreeValue(ctx, prop_val);
        JS_FreeAtom(ctx, atom);
    }

    free(props);
    visited.pop_back();
    return json_obj;
}

// 处理数组类型
static yyjson_mut_val* handle_array(JSContext* ctx, yyjson_mut_doc* doc, JSValueConst arr, std::vector<JSValueConst>& visited) {
    // 检查循环引用
    for (auto& v : visited) {
        if (JS_VALUE_GET_PTR(v) == JS_VALUE_GET_PTR(arr)) {
            return yyjson_mut_str(doc, "[Circular Reference]");
        }
    }

    visited.push_back(arr);

    // 创建新的 JSON 数组
    yyjson_mut_val* json_arr = yyjson_mut_arr(doc);

    // 获取数组长度
    JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
    int32_t len = 0;
    JS_ToInt32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    // 遍历数组元素
    for (int32_t i = 0; i < len; i++) {
        JSValue item_val = JS_GetPropertyUint32(ctx, arr, i);
        yyjson_mut_val* json_item = js_value_to_yyjson(ctx, doc, item_val, visited);
        yyjson_mut_arr_append(json_arr, json_item);
        JS_FreeValue(ctx, item_val);
    }

    visited.pop_back();
    return json_arr;
}

// 主转换函数
static yyjson_mut_val* js_value_to_yyjson(JSContext* ctx, yyjson_mut_doc* doc, JSValueConst val, std::vector<JSValueConst>& visited) {
    if (JS_IsUndefined(val) || JS_IsUninitialized(val)) {
        return yyjson_mut_null(doc);
    }
    else if (JS_IsNull(val)) {
        return yyjson_mut_null(doc);
    }
    else if (JS_IsBool(val)) {
        return yyjson_mut_bool(doc, JS_ToBool(ctx, val));
    }
    else if (JS_IsNumber(val)) {
        double num;
        JS_ToFloat64(ctx, &num, val);
        return yyjson_mut_real(doc, num);
    }
    else if (JS_IsString(val)) {
        const char* str = JS_ToCString(ctx, val);
        yyjson_mut_val* json_str = yyjson_mut_strcpy(doc, str);
        JS_FreeCString(ctx, str);
        return json_str;
    }
    else if (JS_IsArray(ctx, val)) {
        return handle_array(ctx, doc, val, visited);
    }
    else if (JS_IsObject(val)) {
        return handle_object(ctx, doc, val, visited);
    }
    else if (JS_IsFunction(ctx, val)) {
        return yyjson_mut_str(doc, "[Function]");
    }
    else {
        return yyjson_mut_str(doc, "[Unsupported Type]");
    }
}

#ifdef ENABLE_QJS_HTTP
struct mg_http_data {
    std::string head;
    std::string body;
    bool done = false;
    int status = 0;

    void reset() {
        head.clear();
        body.clear();
        done = false;
        status = 0;
    }
};

static void mg_connect_fn(struct mg_connection* connect, int ev, void* ev_data) {
    mg_http_data* data = (mg_http_data*)connect->fn_data;
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;

        data->head.assign(hm->head.ptr, hm->head.len);
        data->body.assign(hm->body.ptr, hm->body.len);
        data->status = mg_http_status(hm);

        data->done = true;
        connect->is_closing = 1;
    }
    else if (ev == MG_EV_ERROR) {
        data->done = true;
        connect->is_closing = 1;
    }
}
#endif

extern "C" {
	static JSValue qjs_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
		const char* log = JS_ToCString(ctx, argv[0]);
        if (!log) {
            return JS_ThrowTypeError(ctx, "Argument must be a string");
        }

		std::string s = log;
		pEngine->m_vecOutput.push_back(s);

		JS_FreeCString(ctx, log); 
		return JS_NewObject(ctx);
	}

	static JSValue qjs_http_request(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
#ifdef ENABLE_QJS_HTTP
		std::vector<JSValueConst> visited;
        yyjson_mut_doc* mdoc = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* yyv = js_value_to_yyjson(ctx, mdoc, argv[0], visited);
		if (yyv && yyjson_mut_is_obj(yyv)) {
            string ip;
            if (yyjson_mut_obj_get(yyv, "hostname")) {
                ip = yyjson_mut_get_str(yyjson_mut_obj_get(yyv, "hostname"));
            }

            int port = 0;
            if (yyjson_mut_obj_get(yyv, "port")) {
                port = yyjson_mut_get_num(yyjson_mut_obj_get(yyv, "port"));
            }

            string method;
            if (yyjson_mut_obj_get(yyv, "method")) {
                method = yyjson_mut_get_str(yyjson_mut_obj_get(yyv, "method"));
            }

            string path;
            if (yyjson_mut_obj_get(yyv, "path")) {
                path = yyjson_mut_get_str(yyjson_mut_obj_get(yyv, "path"));
            }

            string body;
            if (yyjson_mut_obj_get(yyv, "body")) {
                body = yyjson_mut_get_str(yyjson_mut_obj_get(yyv, "body"));
            }

            string url = "http://" + ip + ":" + std::to_string(port) + path;

            struct mg_mgr mgr;
            mg_mgr_init(&mgr);

            mg_http_data data;
            struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

            if (connect) {
                if (method == "POST") {
                    mg_printf(connect,
                        "POST %s HTTP/1.0\r\n"
                        "Host: %s\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: %u\r\n"
                        "\r\n"
                        "%s",
                        path.c_str(), ip.c_str(), (unsigned int)body.size(), body.c_str()
                    );
                }
                else {
                    mg_printf(connect,
                        "GET %s HTTP/1.0\r\n"
                        "Host: %s\r\n"
                        "\r\n",
                        path.c_str(), ip.c_str()
                    );
                }

                TIME tStart;
                tStart.setNow();
                while (!data.done && TIME::calcTimePassSecond(tStart) < 10.0) {
                    mg_mgr_poll(&mgr, 100);
                }
            }

            mg_mgr_free(&mgr);
            yyjson_mut_doc_free(mdoc);

            JSValue ret = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, ret, "body", JS_NewString(ctx, data.body.c_str()));
            JS_SetPropertyStr(ctx, ret, "status", JS_NewInt32(ctx, data.status));
            return ret;
		}

		yyjson_mut_doc_free(mdoc);
		return JS_NewObject(ctx);
#else
        return JS_NULL;
#endif
	}

    static JSValue qjs_sleep(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc > 0) {
            int milli = 0;
            JS_ToInt32(ctx, &milli, argv[0]);
            TIME::sleepMilli(milli);
        }
        return JS_NULL;
    }

    static JSValue qjs_backtrace(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        JSValue error = JS_NewError(ctx);
        JSValue stackVal = JS_GetPropertyStr(ctx, error, "stack");
        const char* stack = JS_ToCString(ctx, stackVal);

        if (stack) {
            LOG("%s\n", stack);
            JS_FreeCString(ctx, stack);
        }
        else {
            LOG("No backtrace available\n");
        }

        JS_FreeValue(ctx, stackVal);
        JS_FreeValue(ctx, error);

        return JS_UNDEFINED;
    }

    static JSValue qjs_json_stringify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc == 1) {
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue jsonObj = JS_GetPropertyStr(ctx, global, "JSON");
            JSValue stringifyFunc = JS_GetPropertyStr(ctx, jsonObj, "stringify");
            JSValue result = JS_Call(ctx, stringifyFunc, jsonObj, 1, argv);

            JS_FreeValue(ctx, stringifyFunc);
            JS_FreeValue(ctx, jsonObj);
            JS_FreeValue(ctx, global);

            return result;
        }
        return JS_NULL;
    }

    static JSValue qjs_json_parse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc == 1) {
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue jsonObj = JS_GetPropertyStr(ctx, global, "JSON");
            JSValue parseFunc = JS_GetPropertyStr(ctx, jsonObj, "parse");
            JSValue result = JS_Call(ctx, parseFunc, jsonObj, 1, argv);

            JS_FreeValue(ctx, parseFunc);
            JS_FreeValue(ctx, jsonObj);
            JS_FreeValue(ctx, global);

            return result;
        }
        return JS_NULL;
    }

    static JSValue qjs_str_toHexStr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc == 1 && JS_IsArray(ctx, argv[0])) {
            JSValue len_val = JS_GetPropertyStr(ctx, argv[0], "length");
            int32_t len = 0;
            JS_ToInt32(ctx, &len, len_val);
            JS_FreeValue(ctx, len_val);

            std::string s;
            for (int32_t i = 0; i < len; ++i) {
                JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
                int32_t b = 0;
                JS_ToInt32(ctx, &b, item);
                char buf[8];
                snprintf(buf, sizeof(buf), "%02X ", (unsigned char)b);
                s += buf;
                JS_FreeValue(ctx, item);
            }
            return JS_NewString(ctx, s.c_str());
        }
        return JS_NULL;
    }

    static JSValue qjs_toStr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        json jTime;
        jsValToJsonVal(ctx, this_val, jTime);

        TIME t;
        t.wYear = jTime["year"].get<int>();
        t.wMonth = jTime["month"].get<int>();
        t.wDay = jTime["day"].get<int>();
        t.wHour = jTime["hour"].get<int>();
        t.wMinute = jTime["minute"].get<int>();
        t.wSecond = jTime["second"].get<int>();
        t.wMilliseconds = jTime["millisecond"].get<int>();

        std::string sTime = t.toStr();
        return JS_NewString(ctx, sTime.c_str());
    }

    static JSValue qjs_fromStr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 1 || !JS_IsString(argv[0])) {
            return JS_NULL;
        }
        
        JSValue jsTime = JS_DupValue(ctx, this_val);

        const char* strTime = JS_ToCString(ctx, argv[0]);
        if (!strTime) {
            JS_FreeValue(ctx, jsTime);
            return JS_NULL;
        }

        TIME t;
        t.fromStr(strTime);
        JS_FreeCString(ctx, strTime);

        JS_SetPropertyStr(ctx, jsTime, "year", JS_NewInt32(ctx, t.wYear));
        JS_SetPropertyStr(ctx, jsTime, "month", JS_NewInt32(ctx, t.wMonth));
        JS_SetPropertyStr(ctx, jsTime, "day", JS_NewInt32(ctx, t.wDay));
        JS_SetPropertyStr(ctx, jsTime, "hour", JS_NewInt32(ctx, t.wHour));
        JS_SetPropertyStr(ctx, jsTime, "minute", JS_NewInt32(ctx, t.wMinute));
        JS_SetPropertyStr(ctx, jsTime, "second", JS_NewInt32(ctx, t.wSecond));
        JS_SetPropertyStr(ctx, jsTime, "millisecond", JS_NewInt32(ctx, t.wMilliseconds));

        JS_FreeValue(ctx, jsTime);
        return JS_NULL;
    }

    static JSValue qjs_increaseSeconds(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 1 || !JS_IsNumber(argv[0])) {
            return JS_NULL;
        }

        json jTime;
        jsValToJsonVal(ctx, this_val, jTime);

        TIME t;
        t.wYear = jTime["year"].get<int>();
        t.wMonth = jTime["month"].get<int>();
        t.wDay = jTime["day"].get<int>();
        t.wHour = jTime["hour"].get<int>();
        t.wMinute = jTime["minute"].get<int>();
        t.wSecond = jTime["second"].get<int>();
        t.wMilliseconds = jTime["millisecond"].get<int>();

        int addSec = 0;
        JS_ToInt32(ctx, &addSec, argv[0]);
        time_t unixTime = t.toUnixTime();
        unixTime += addSec;
        t.fromUnixTime(unixTime);

        JSValue jsTime = JS_DupValue(ctx, this_val);
        JS_SetPropertyStr(ctx, jsTime, "year", JS_NewInt32(ctx, t.wYear));
        JS_SetPropertyStr(ctx, jsTime, "month", JS_NewInt32(ctx, t.wMonth));
        JS_SetPropertyStr(ctx, jsTime, "day", JS_NewInt32(ctx, t.wDay));
        JS_SetPropertyStr(ctx, jsTime, "hour", JS_NewInt32(ctx, t.wHour));
        JS_SetPropertyStr(ctx, jsTime, "minute", JS_NewInt32(ctx, t.wMinute));
        JS_SetPropertyStr(ctx, jsTime, "second", JS_NewInt32(ctx, t.wSecond));
        JS_SetPropertyStr(ctx, jsTime, "millisecond", JS_NewInt32(ctx, t.wMilliseconds));

        JS_FreeValue(ctx, jsTime);

        return JS_NULL;
    }

    static JSValue qjs_toUnixTime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        json jTime;
        jsValToJsonVal(ctx, this_val, jTime);

        TIME t;
        t.wYear = jTime["year"].get<int>();
        t.wMonth = jTime["month"].get<int>();
        t.wDay = jTime["day"].get<int>();
        t.wHour = jTime["hour"].get<int>();
        t.wMinute = jTime["minute"].get<int>();
        t.wSecond = jTime["second"].get<int>();
        t.wMilliseconds = jTime["millisecond"].get<int>();

        time_t tt = t.toUnixTime();
        return JS_NewInt64(ctx, static_cast<int64_t>(tt));
    }

    static JSValue qjs_time(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        TIME t; t.setNow();

        JSValue timeObj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, timeObj, "year", JS_NewInt32(ctx, t.wYear));
        JS_SetPropertyStr(ctx, timeObj, "month", JS_NewInt32(ctx, t.wMonth));
        JS_SetPropertyStr(ctx, timeObj, "day", JS_NewInt32(ctx, t.wDay));
        JS_SetPropertyStr(ctx, timeObj, "hour", JS_NewInt32(ctx, t.wHour));
        JS_SetPropertyStr(ctx, timeObj, "minute", JS_NewInt32(ctx, t.wMinute));
        JS_SetPropertyStr(ctx, timeObj, "second", JS_NewInt32(ctx, t.wSecond));
        JS_SetPropertyStr(ctx, timeObj, "millisecond", JS_NewInt32(ctx, t.wMilliseconds));

        // 绑定方法
        JS_SetPropertyStr(ctx, timeObj, "toStr", JS_NewCFunction(ctx, qjs_toStr, "toStr", 0));
        JS_SetPropertyStr(ctx, timeObj, "fromStr", JS_NewCFunction(ctx, qjs_fromStr, "fromStr", 1));
        JS_SetPropertyStr(ctx, timeObj, "increaseSeconds", JS_NewCFunction(ctx, qjs_increaseSeconds, "increaseSeconds", 1));
        JS_SetPropertyStr(ctx, timeObj, "toUnixTime", JS_NewCFunction(ctx, qjs_toUnixTime, "toUnixTime", 0));

        return timeObj;
    }

    static JSValue qjs_openSerial(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 5) {
            return JS_NULL;
        }

        // 参数解析
        json jArgs = engineArrayToJson(ctx, argv, argc);
        if (jArgs.size() != 5) {
            return JS_NULL;
        }

        std::string errorInfo;
        std::string portName = jArgs[0].get<std::string>();
        int baudRate = jArgs[1].get<int>();
        std::string parity = jArgs[2].get<std::string>();
        int byteSize = jArgs[3].get<int>();
        std::string stopBits = jArgs[4].get<std::string>();

        HANDLE hCom = nullptr;
        bool ret = false;
        std::string strComPort = "\\\\.\\" + portName;
        COMMTIMEOUTS timeouts = { 0 };

        // 打开串口
        hCom = CreateFileA(strComPort.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hCom == INVALID_HANDLE_VALUE) {
            errorInfo = "CreateFile fail,error code:" + std::to_string(GetLastError());
            goto OPEN_END;
        }

        // 配置串口参数
        COMSTAT comstat;
        DWORD dwError;
        ClearCommError(hCom, &dwError, &comstat);

        DCB dcb;
        SecureZeroMemory(&dcb, sizeof(DCB));

        dcb.DCBlength = sizeof(DCB);
        GetCommState(hCom, &dcb);

        dcb.BaudRate = baudRate;
        dcb.ByteSize = byteSize;
        dcb.Parity = tJSEngine::parseParity(parity);
        dcb.StopBits = tJSEngine::parseStopBits(stopBits);

        if (!SetCommState(hCom, &dcb)) {
            errorInfo = "SetCommState fail,error code" + std::to_string(GetLastError());
            CloseHandle(hCom);
            hCom = nullptr;
            goto OPEN_END;
        }

        SetupComm(hCom, 1024, 1024);

        // 设置超时时间
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 100;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 2000;
        if (!SetCommTimeouts(hCom, &timeouts)) {
            printf("设置超时失败，错误代码: %d\n", GetLastError());
            CloseHandle(hCom);
            goto OPEN_END;
        }
        ret = true;

    OPEN_END:
        if (ret) {
            printf("[warn][串口   ]串口打开成功,串口号:%s,baudRate:%d,byteSize:%d,stopBits:%s,parity:%s,串口句柄:%p",
                portName.c_str(), baudRate, byteSize, stopBits.c_str(), parity.c_str(), hCom);
        }
        else {
            printf("[warn][串口   ]串口打开失败,串口号:%s,baudRate:%d,byteSize:%d,stopBits:%s,parity:%s,错误信息:%s",
                portName.c_str(), baudRate, byteSize, stopBits.c_str(), parity.c_str(), errorInfo.c_str());
        }

        if (ret) {
            std::string sHandle = tJSEngine::pointerToString(hCom);
            return JS_NewString(ctx, sHandle.c_str());
        }
        else {
            return JS_NULL;
        }
    }

    static JSValue qjs_readSerial(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 1) {
            return JS_NULL;
        }

        // 参数解析
        json jArgs = engineArrayToJson(ctx, argv, argc);
        if (jArgs.size() != 1) {
            return JS_NULL;
        }

        std::string sH = jArgs[0].get<std::string>();
        void* hCom = tJSEngine::stringToPointer(sH);
        DWORD dwError = 0;
        unsigned char buf[50000] = { 0 };
        int iLen = 0;

        BOOL bReadRet = ReadFile(hCom, (LPVOID)(buf), 50000, (LPDWORD)&iLen, NULL); // 阻塞读取
        dwError = GetLastError();
        if (dwError != 0) {
            printf("[warn]ReadFile Error %d\n", dwError);
        }

        if (iLen > 0) {
            JSValue arr = JS_NewArray(ctx);
            for (int i = 0; i < iLen; i++) {
                JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, buf[i]));
            }
            return arr;
        }
        else {
            return JS_NULL;
        }
    }

    static JSValue qjs_writeSerial(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 2) {
            return JS_NewBool(ctx, false);
        }

        // 参数解析
        json jArgs = engineArrayToJson(ctx, argv, argc);
        if (jArgs.size() != 2) {
            return JS_NewBool(ctx, false);
        }

        std::string sH = jArgs[0].get<std::string>();
        void* hCom = tJSEngine::stringToPointer(sH);
        json jData = jArgs[1];
        std::vector<unsigned char> vec;
        std::string sData;
        char* pData = nullptr;
        int len = 0;

        if (jData.is_array()) {
            for (int i = 0; i < jData.size(); i++) {
                unsigned char b = jData[i].get<unsigned char>();
                vec.push_back(b);
            }
            pData = (char*)vec.data();
            len = static_cast<int>(vec.size());
        }
        else if (jData.is_string()) {
            sData = jData.get<std::string>();
            pData = (char*)sData.c_str();
            len = static_cast<int>(sData.length());
        }
        else {
            return JS_NewBool(ctx, false);
        }

        DWORD bytesWritten = 0;
        BOOL bRet = WriteFile(
            hCom,           // 串口句柄
            pData,          // 数据缓冲区
            len,            // 数据长度
            &bytesWritten,  // 实际写入的字节数
            NULL            // 同步模式设为 NULL
        );

        if (bRet) {
            return JS_NewBool(ctx, true);
        }
        else {
            return JS_NewBool(ctx, false);
        }
    }

    static JSValue qjs_closeSerial(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 1) {
            return JS_NULL;
        }

        // 参数解析
        json jArgs = engineArrayToJson(ctx, argv, argc);
        if (jArgs.size() != 1) {
            return JS_NULL;
        }

        std::string sH = jArgs[0].get<std::string>();
        void* hCom = tJSEngine::stringToPointer(sH);
        if (hCom != nullptr) {
            CloseHandle(hCom);
        }

        return JS_NULL;
    }

    static JSValue qjs_arrayToStr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 1 || !JS_IsArray(ctx, argv[0])) {
            return JS_NULL;
        }

        // 解析 JS 数组为 json
        json jArgs = engineArrayToJson(ctx, argv, argc);
        if (jArgs.size() != 1 || !jArgs[0].is_array()) {
            return JS_NULL;
        }

        json jArr = jArgs[0];
        std::vector<char> charArray;
        charArray.resize(jArr.size() + 1);
        for (size_t i = 0; i < jArr.size(); i++) {
            unsigned char b = jArr[i].get<unsigned char>();
            char cb = *((char*)&b);
            charArray[i] = cb;
        }
        charArray[jArr.size()] = 0;
        std::string s = (char*)charArray.data();

        return JS_NewString(ctx, s.c_str());
    }

    static JSValue qjs_strToArray(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 1 || !JS_IsString(argv[0])) {
            return JS_NULL;
        }

        size_t len = 0;
        const char* s = JS_ToCStringLen(ctx, &len, argv[0]);
        if (!s) {
            return JS_NULL;
        }

        JSValue arr = JS_NewArray(ctx);
        for (size_t i = 0; i < len; ++i) {
            unsigned char ucb = static_cast<unsigned char>(s[i]);
            JS_SetPropertyUint32(ctx, arr, (uint32_t)i, JS_NewInt32(ctx, ucb));
        }

        JS_FreeCString(ctx, s);
        return arr;
    }
} 

void register_cpp_functions(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);

    JS_SetPropertyStr(ctx, global, "log", JS_NewCFunction(ctx, qjs_log, "log", 1));

    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, qjs_log, "log", 1));
    JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, qjs_log, "error", 1));
    JS_SetPropertyStr(ctx, global, "console", console);

    JSValue http = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, http, "request", JS_NewCFunction(ctx, qjs_http_request, "request", 1));
    JS_SetPropertyStr(ctx, global, "http", http);

    JS_SetPropertyStr(ctx, global, "sleep", JS_NewCFunction(ctx, qjs_sleep, "sleep", 1));
    JS_SetPropertyStr(ctx, global, "backtrace", JS_NewCFunction(ctx, qjs_backtrace, "backtrace", 1));
    JS_SetPropertyStr(ctx, global, "json_stringify", JS_NewCFunction(ctx, qjs_json_stringify, "json_stringify", 1));
    JS_SetPropertyStr(ctx, global, "json_parse", JS_NewCFunction(ctx, qjs_json_parse, "json_parse", 1));

    JSValue strObj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, strObj, "toHexStr", JS_NewCFunction(ctx, qjs_str_toHexStr, "toHexStr", 1));
    JS_SetPropertyStr(ctx, global, "STR", strObj);

    JS_SetPropertyStr(ctx, global, "time", JS_NewCFunction(ctx, qjs_time, "time", 0));
    JS_SetPropertyStr(ctx, global, "openSerial", JS_NewCFunction(ctx, qjs_openSerial, "openSerial", 5));
    JS_SetPropertyStr(ctx, global, "readSerial", JS_NewCFunction(ctx, qjs_readSerial, "readSerial", 1));
    JS_SetPropertyStr(ctx, global, "writeSerial", JS_NewCFunction(ctx, qjs_writeSerial, "writeSerial", 2));
    JS_SetPropertyStr(ctx, global, "closeSerial", JS_NewCFunction(ctx, qjs_closeSerial, "closeSerial", 1));
    JS_SetPropertyStr(ctx, global, "arrayToStr", JS_NewCFunction(ctx, qjs_arrayToStr, "arrayToStr", 1));
    JS_SetPropertyStr(ctx, global, "strToArray", JS_NewCFunction(ctx, qjs_strToArray, "strToArray", 1));

    JS_FreeValue(ctx, global);
}

// 查找错误行号的辅助函数
int extract_line_number(const char* stack_str) {
    const char* line_pos = strstr(stack_str, ":");

    // 未找到行号
    if (!line_pos) {
        return -1;  
    }

    line_pos++;  // 跳过冒号

    // 解析行号
    int line = 0;
    while (*line_pos >= '0' && *line_pos <= '9') {
        line = line * 10 + (*line_pos - '0');
        line_pos++;
    }

    return line > 0 ? line : -1;
}

bool is_integer(double x) {
    if (std::isnan(x) || std::isinf(x)) {
        return false;
    }

    const double threshold = 9007199254740992.0; // 2^53
    double abs_x = std::fabs(x);

    // 超出精度范围后无法表示小数
    if (abs_x >= threshold) {
        return true;
    }

    return x == std::trunc(x);
}

#ifndef UTF8_TO_UTF16_H
#define UTF8_TO_UTF16_H

#include <string>
#include <vector>
#include <stdexcept>
#include <cstddef>

// 跨平台 wchar_t 兼容性处理
#if defined(_WIN32) || defined(_WIN64)
using wide_char = wchar_t;
#else
#include <cwchar>
#if WCHAR_MAX > 0xFFFFu
// Linux/macOS 上 wchar_t 是 32 位，转换为 16 位存储
using wide_char = char16_t;
#else
using wide_char = wchar_t;
#endif
#endif

void utf8ToUtf16(const std::string& utf8, std::wstring& utf16) {
    // 清空输出字符串
    utf16.clear();

    // 直接使用原始指针和长度操作
    const char* ptr = utf8.data();
    const size_t len = utf8.size();

    // 预分配空间（大约估算）
    if (len > 0) {
        utf16.reserve(len);
    }

    for (size_t i = 0; i < len; ) {
        unsigned char c = static_cast<unsigned char>(ptr[i]);
        size_t charLen = 0;
        uint32_t codePoint = 0;

        // 确定字符长度和提取首字节信息
        if (c < 0x80) {           // 1 字节字符
            charLen = 1;
            codePoint = c;
        }
        else if ((c & 0xE0) == 0xC0) { // 2 字节字符
            charLen = 2;
            codePoint = c & 0x1F;
        }
        else if ((c & 0xF0) == 0xE0) { // 3 字节字符
            charLen = 3;
            codePoint = c & 0x0F;
        }
        else if ((c & 0xF8) == 0xF0) { // 4 字节字符
            charLen = 4;
            codePoint = c & 0x07;
        }
        else {
            throw std::runtime_error("Invalid UTF-8 start byte");
        }

        // 检查是否越界
        if (i + charLen > len) {
            throw std::runtime_error("Incomplete UTF-8 sequence");
        }

        // 处理后续字节
        for (size_t j = 1; j < charLen; ++j) {
            unsigned char next = static_cast<unsigned char>(ptr[i + j]);
            if ((next & 0xC0) != 0x80) {
                throw std::runtime_error("Invalid UTF-8 continuation byte");
            }
            codePoint = (codePoint << 6) | (next & 0x3F);
        }

        // 转换为 UTF-16
        if (codePoint <= 0xFFFF) {
            // 直接存储基本多语言平面字符
            utf16.push_back(static_cast<wide_char>(codePoint));
        }
        else if (codePoint <= 0x10FFFF) {
            // 处理代理对
            codePoint -= 0x10000;
            uint16_t high = static_cast<uint16_t>(0xD800 | (codePoint >> 10));
            uint16_t low = static_cast<uint16_t>(0xDC00 | (codePoint & 0x3FF));

            // 分两次添加到 wstring
            utf16.push_back(static_cast<wide_char>(high));
            utf16.push_back(static_cast<wide_char>(low));
        }
        else {
            throw std::runtime_error("Code point out of valid range");
        }

        i += charLen;
    }
}
#endif // UTF8_TO_UTF16_H

bool se_readFile(string path, string& data) {
    FILE* fp = nullptr;
    wstring wPath;
    utf8ToUtf16(path,wPath);

#ifdef _WIN32
    _wfopen_s(&fp, wPath.c_str(), L"rb");
#else
    fp = fopen(path.c_str(), "rb");
#endif

    if (fp) {
        fseek(fp, 0, SEEK_END);

        long len = ftell(fp);
        if (len > 0) {
            data.resize(len);
            char* pdata = (char*)data.data();

            fseek(fp, 0, SEEK_SET);
            fread(pdata, 1, len, fp);
        }

        fclose(fp);
        return true;
    }
    return false;
}

mutex g_mutexScriptFileBuff;
map<string, string> g_mapScriptFileBuff;

JSModuleDef* qjs_module_loader(JSContext* ctx,
    const char* module_name,
    void* opaque) {

    string s = str::trim(module_name,".");
	string path = pEngine->m_folderPath + "/" + s;

    string script;
    g_mutexScriptFileBuff.lock();
    map<string, string>::iterator iter = g_mapScriptFileBuff.find(path);
    g_mutexScriptFileBuff.unlock();
    if (iter != g_mapScriptFileBuff.end()) {
        script = iter->second;
    }
    else{
        if (!se_readFile(path, script)) {
            JS_ThrowReferenceError(ctx, "Cannot open module: %s", module_name);
            return NULL;
        }

        if (script == "") {
            JS_ThrowReferenceError(ctx, "Cannot open module: %s", module_name);
            return NULL;
        }

        g_mutexScriptFileBuff.lock();
		g_mapScriptFileBuff[path] = script;
        g_mutexScriptFileBuff.unlock();
    }

    // 编译模块
    JSValue val = JS_Eval(ctx, script.c_str(), script.size(), module_name,
        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    if (JS_IsException(val)) {
        return NULL; // 异常已设置
    }

    // 获取模块对象
    JSModuleDef* m = reinterpret_cast<JSModuleDef*>(JS_VALUE_GET_PTR(val));
    return m;
}

ScriptEngine::ScriptEngine() {
	m_ioDevThis = nullptr;
    m_initTdsFunc = nullptr;
    m_bValNullInCalc = false;
    m_reloadFile = false;
}

bool ScriptEngine::runScript(string& script, string user, SCRIPT_RUN_INFO& sri,string folder) {
	m_script = script;
	m_user = user;

	m_vecOutput.clear();
	bool runOk = false;

    sri.lastExe = TIME::nowStr(true);

	try {
		TIME tStart;
        tStart.setNow();
		pEngine = this;
        pEngine->m_folderPath = folder;
		
		// 初始化 QuickJS
		JSRuntime* rt = JS_NewRuntime();
		JSContext* ctx = JS_NewContext(rt);
        JS_SetModuleLoaderFunc(rt, NULL, qjs_module_loader, NULL);
		register_cpp_functions(ctx);
        if (m_initTdsFunc) {
            m_initTdsFunc(ctx, m_ioDevThis);
        }

        if (!m_globalObj.is_null()) {
            JSValue global = JS_GetGlobalObject(ctx);
            for (auto it = m_globalObj.items().begin(); it != m_globalObj.items().end(); ++it) {
                const std::string& key = it.key();
                json& value            = it.value();

                JSValue prop_value;
                jsonValToJsVal(value, ctx, prop_value);

                JS_SetPropertyStr(ctx, global, key.c_str(), prop_value);
            }

            JS_FreeValue(ctx, global);
        }

        JSValue result;
        if (folder != "") {
            result = JS_Eval(ctx, script.c_str(), script.length(), "<main>", JS_EVAL_TYPE_MODULE);
        }
        else {
            result = JS_Eval(ctx, script.c_str(), script.length(), "<main>", JS_EVAL_TYPE_GLOBAL);
        }
        

		if (JS_IsException(result)) {
			JSValue error = JS_GetException(ctx);
			const char* err = JS_ToCString(ctx, error);

            JSValue stack_val = JS_GetPropertyStr(ctx, error, "stack");
            const char* stack = JS_ToCString(ctx, stack_val);

            // 提取行号
            int line = extract_line_number(stack);

			string s = err;
			s = "Exception at line " + std::to_string(line) + ":" + s;
            sri.lastError = s;
			m_vecOutput.push_back(s);

			JS_FreeCString(ctx, err);
			JS_FreeValue(ctx, error);
		}
        else {
            //返回全局对象在脚本内部的修改
            if (!m_globalObj.is_null()) {
                JSValue global = JS_GetGlobalObject(ctx);
                for (auto it = m_globalObj.items().begin(); it != m_globalObj.items().end(); ++it) {
                    const std::string& key = it.key();
                    json& value = it.value();

                    JSValue prop_value = JS_GetPropertyStr(ctx, global, key.c_str());;
                    jsValToJsonVal(ctx, prop_value, value);
                }

                JS_FreeValue(ctx, global);
            }


            jsValToJsonVal(ctx, result, pEngine->m_sEvalRet);
            runOk = true;
        }

		// 清理资源
		JS_FreeValue(ctx, result);
		JS_FreeContext(ctx);
		JS_FreeRuntime(rt);

		int costMilli = TIME::calcTimePassMilliSecond(tStart);
		m_vecOutput.push_back("执行耗时:" + to_string(costMilli) + "ms");
	}
	catch (std::exception& e) {
		string s = e.what();
		m_vecOutput.push_back(s);
		return false;
	}


    sri.tagRefDataTime = m_vecValRefTime;
    sri.retVal = m_sEvalRet.dump();
    sri.valNullInCalc = m_bValNullInCalc;
    sri.runSuccess = runOk;
	return runOk;
}

void jsValToJsonVal(JSContext* ctx, JSValueConst jsVal, json& jsonVal) {
    if (JS_IsBool(jsVal)) {
        jsonVal = JS_VALUE_GET_BOOL(jsVal) != 0;
    }
    else if (JS_IsBigInt(ctx, jsVal)) {
        int64_t digit = 0;
        JS_ToInt64(ctx, &digit, jsVal);

        int intValue = (int)digit;
        jsonVal = intValue;
    }
    else if (JS_IsNumber(jsVal)) {
        double num = 0;
        JS_ToFloat64(ctx, &num, jsVal);

        // 判断是否为整数
        if (std::trunc(num) == num && 
            num <= static_cast<double>((std::numeric_limits<int64_t>::max)()) &&
            num >= static_cast<double>((std::numeric_limits<int64_t>::min)())) {
            jsonVal = static_cast<int64_t>(num);
        }
        else {
            jsonVal = num;
        }
    }
    else if (JS_IsString(jsVal)) {
        const char* str = JS_ToCString(ctx, jsVal);
        jsonVal = str ? str : "";

        JS_FreeCString(ctx, str);
    }
    else if (JS_IsArray(ctx, jsVal)) {
        jsonVal = json::array();

        JSValue len_val = JS_GetPropertyStr(ctx, jsVal, "length");
        int32_t len = 0;

        JS_ToInt32(ctx, &len, len_val);
        JS_FreeValue(ctx, len_val);

        for (int32_t i = 0; i < len; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, jsVal, i);

            json jItem;
            jsValToJsonVal(ctx, item, jItem);

            jsonVal.push_back(jItem);
            JS_FreeValue(ctx, item);
        }
    }
    else if (JS_IsObject(jsVal)) {
        jsonVal = json::object();

        JSPropertyEnum* props = nullptr;
        uint32_t prop_count = 0;

        if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, jsVal, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < prop_count; ++i) {
                JSAtom atom = props[i].atom;

                const char* key = JS_AtomToCString(ctx, atom);
                if (!key) {
                    continue;
                }

                JSValue propValue = JS_GetProperty(ctx, jsVal, atom);

                json jsonValue;
                jsValToJsonVal(ctx, propValue, jsonValue);

                jsonVal[key] = jsonValue;

                JS_FreeCString(ctx, key);
                JS_FreeValue(ctx, propValue);
                JS_FreeAtom(ctx, atom);
            }

            free(props);
        }
    }
    else {
        jsonVal = nullptr;
    }
}

void jsonValToJsVal(json& jsonVal, JSContext* ctx, JSValue& jsVal) {
    if (jsonVal.is_string()) {
        jsVal = JS_NewString(ctx, jsonVal.get<string>().c_str());
    }
    else if (jsonVal.is_null()) {
        jsVal = JS_NULL;
    }
    else if (jsonVal.is_number_integer()) {
        int64_t val = jsonVal.get<int64_t>();
        jsVal = JS_NewInt64(ctx, val);
    }
    else if (jsonVal.is_number_float()) {
        jsVal = JS_NewFloat64(ctx, jsonVal.get<double>());
    }
    else if (jsonVal.is_boolean()) {
        jsVal = JS_NewBool(ctx, jsonVal.get<bool>());
    }
    else if (jsonVal.is_object()) {
        jsVal = JS_NewObject(ctx);

        for (auto it = jsonVal.items().begin(); it != jsonVal.items().end(); ++it) {
            const std::string& key = it.key();
            json& value = it.value();

            JSValue jsValue;
            jsonValToJsVal(value, ctx, jsValue);

            JS_SetPropertyStr(ctx, jsVal, key.c_str(), jsValue);
        }
    }
    else if (jsonVal.is_array()) {
        jsVal = JS_NewArray(ctx);

        for (size_t i = 0; i < jsonVal.size(); i++) {
            JSValue jsValue;
            jsonValToJsVal(jsonVal[i], ctx, jsValue);

            JS_SetPropertyUint32(ctx, jsVal, (uint32_t)i, jsValue);
        }
    }
    else {
        jsVal = JS_NULL;
	}
}

json engineArrayToJson(JSContext* ctx, const JSValueConst array[], const int count) {
    json jsonArgs = json::array();

    for (int i = 0; i < count; ++i) {
        json jsonVal;
        jsValToJsonVal(ctx, array[i], jsonVal);
        jsonArgs.push_back(jsonVal);
    }

    return jsonArgs;
}

json engineObjectToJson(JSContext* ctx, JSValueConst object) {
    json jsonObj = json::object();

    if (!JS_IsObject(object)) {
        return jsonObj;
    }

    JSPropertyEnum* props = nullptr;
    uint32_t count = 0;

    if (JS_GetOwnPropertyNames(ctx, &props, &count, object, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
        return jsonObj;
    }

    for (uint32_t i = 0; i < count; i++) {
        JSAtom atom = props[i].atom;

        const char* key = JS_AtomToCString(ctx, atom);
        if (!key) {
            continue;
        }

        JSValue jsValue = JS_GetProperty(ctx, object, atom);

        json jsonVal;
        jsValToJsonVal(ctx, jsValue, jsonVal);

        jsonObj[key] = jsonVal;

        JS_FreeCString(ctx, key);
        JS_FreeValue(ctx, jsValue);
        JS_FreeAtom(ctx, atom);
    }

    free(props);
    return jsonObj;
}

bool jsItemToJsonItem(JSContext* ctx, JSValueConst propName, JSValueConst propValue, void* data) {
    using json = nlohmann::json;
    json& jsonObj = *(json*)data;

    //解析key
    std::string key;
    const char* ckey = JS_ToCString(ctx, propName);
    if (!ckey) {
		return false;
    }

    key = ckey;
    JS_FreeCString(ctx, ckey);

    // 解析val
    json jsonValue;
    jsValToJsonVal(ctx, propValue, jsonValue);
    jsonObj[key] = jsonValue;

    return true;
}

bool jsItemToJsonItem(JSContext* ctx, JSAtom atom, JSValueConst prop_value, void* data) {
    using json = nlohmann::json;
    json& jsonObj = *(json*)data;

    // 解析key
    const char* key = JS_AtomToCString(ctx, atom);
    if (!key) {
        return false;
    }

    // 解析val
    json jsonValue;
    jsValToJsonVal(ctx, prop_value, jsonValue);
    jsonObj[key] = jsonValue;

    JS_FreeCString(ctx, key);
    return true;
}

#endif

void SCRIPT_RUN_INFO::toJson(yyjson_mut_doc* doc, yyjson_mut_val* yyVal)
{
    yyjson_mut_obj_add_strcpy(doc, yyVal, "runTime", lastExe.c_str());
	yyjson_mut_obj_add_str(doc, yyVal, "retVal", retVal.c_str());
	yyjson_mut_obj_add_bool(doc, yyVal, "runSuccess", runSuccess);
	yyjson_mut_obj_add_bool(doc, yyVal, "valNullInCalc", valNullInCalc);


    yyjson_mut_val* yyTagRefDataTime = yyjson_mut_obj(doc);
    for (auto& iter : tagRefDataTime) {
        string key = iter.first;
        string value = iter.second;
        yyjson_mut_obj_add_strcpy(doc, yyTagRefDataTime, key.c_str(), value.c_str());
    }

    yyjson_mut_obj_add_val(doc, yyVal, "tagRefDataTime", yyTagRefDataTime);
}
