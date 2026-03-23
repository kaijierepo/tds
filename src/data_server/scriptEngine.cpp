#ifdef ENABLE_QJS
#include "scriptEngine.h"
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

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>
#include <cerrno>
#define GetLastError() errno
#endif

#if (defined(_MSVC_LANG) && _MSVC_LANG < 201703L) || (!defined(_MSVC_LANG) && defined(__cplusplus) && __cplusplus < 201703L)
#include <experimental/filesystem>
namespace stdfs = std::experimental::filesystem;
#else
#include <filesystem>
namespace stdfs = std::filesystem;
#endif

#include "tdb.h" //for db.insert,db.select script functions
#include "tds.h"

static std::wstring utf8_to_utf16(const string& u8str) {
    const char* utf8_str = u8str.c_str();
    size_t length = u8str.length();
    if (!utf8_str || length == 0) {
        return std::wstring();
    }

    // 预分配足够的空间（最坏情况：每个ASCII字符对应1个wchar_t）
    std::wstring result;
    result.reserve(length);

    const uint8_t* data = reinterpret_cast<const uint8_t*>(utf8_str);
    const uint8_t* end = data + length;

    while (data < end) {
        uint8_t c = *data;

        if (c < 0x80) {
            // 单字节UTF-8 (0-0x7F)
            result.push_back(static_cast<wchar_t>(c));
            data++;
        }
        else if ((c & 0xE0) == 0xC0) {
            // 双字节UTF-8 (0x80-0x7FF)
            if (data + 1 >= end) {
                throw std::runtime_error("Invalid UTF-8 sequence: incomplete 2-byte sequence");
            }

            uint32_t code_point = ((c & 0x1F) << 6) | (data[1] & 0x3F);
            result.push_back(static_cast<wchar_t>(code_point));
            data += 2;
        }
        else if ((c & 0xF0) == 0xE0) {
            // 三字节UTF-8 (0x800-0xFFFF)
            if (data + 2 >= end) {
                throw std::runtime_error("Invalid UTF-8 sequence: incomplete 3-byte sequence");
            }

            uint32_t code_point = ((c & 0x0F) << 12) |
                ((data[1] & 0x3F) << 6) |
                (data[2] & 0x3F);
            result.push_back(static_cast<wchar_t>(code_point));
            data += 3;
        }
        else if ((c & 0xF8) == 0xF0) {
            // 四字节UTF-8 (0x10000-0x10FFFF)，需要UTF-16代理对
            if (data + 3 >= end) {
                throw std::runtime_error("Invalid UTF-8 sequence: incomplete 4-byte sequence");
            }

            uint32_t code_point = ((c & 0x07) << 18) |
                ((data[1] & 0x3F) << 12) |
                ((data[2] & 0x3F) << 6) |
                (data[3] & 0x3F);

            // 转换为UTF-16代理对
            code_point -= 0x10000;
            wchar_t high_surrogate = static_cast<wchar_t>((code_point >> 10) + 0xD800);
            wchar_t low_surrogate = static_cast<wchar_t>((code_point & 0x3FF) + 0xDC00);

            result.push_back(high_surrogate);
            result.push_back(low_surrogate);
            data += 4;
        }
        else {
            throw std::runtime_error("Invalid UTF-8 sequence: invalid leading byte");
        }
    }

    // 调整容量以释放多余空间
    result.shrink_to_fit();
    return result;
}

static std::string utf16_to_utf8(const wstring& u16str) {
    const wchar_t* utf16_str = u16str.c_str();
    size_t length = u16str.length();
    if (!utf16_str || length == 0) {
        return std::string();
    }

    // 预分配足够的空间（最坏情况：每个UTF-16代码单元对应3字节）
    std::string result;
    result.reserve(length * 3);

    const wchar_t* data = utf16_str;
    const wchar_t* end = data + length;

    while (data < end) {
        uint32_t code_unit = static_cast<uint32_t>(*data);

        if (code_unit < 0xD800 || code_unit > 0xDFFF) {
            // 不是代理对，直接处理
            if (code_unit < 0x80) {
                // 单字节UTF-8
                result.push_back(static_cast<char>(code_unit));
            }
            else if (code_unit < 0x800) {
                // 双字节UTF-8
                result.push_back(static_cast<char>(0xC0 | (code_unit >> 6)));
                result.push_back(static_cast<char>(0x80 | (code_unit & 0x3F)));
            }
            else {
                // 三字节UTF-8
                result.push_back(static_cast<char>(0xE0 | (code_unit >> 12)));
                result.push_back(static_cast<char>(0x80 | ((code_unit >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (code_unit & 0x3F)));
            }
            data++;
        }
        else {
            // 处理代理对
            if (code_unit > 0xDBFF || data + 1 >= end) {
                throw std::runtime_error("Invalid UTF-16 sequence: invalid surrogate pair");
            }

            uint32_t high_surrogate = code_unit;
            uint32_t low_surrogate = static_cast<uint32_t>(*(data + 1));

            if (low_surrogate < 0xDC00 || low_surrogate > 0xDFFF) {
                throw std::runtime_error("Invalid UTF-16 sequence: invalid low surrogate");
            }

            // 计算实际代码点
            uint32_t code_point = ((high_surrogate - 0xD800) << 10) +
                (low_surrogate - 0xDC00) + 0x10000;

            // 四字节UTF-8
            result.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));

            data += 2;
        }
    }

    // 调整容量以释放多余空间
    result.shrink_to_fit();
    return result;
}

static bool createFolderOfPath(string strFile) {
    size_t iDotPos = strFile.rfind('.');
    size_t iSlashPos = strFile.rfind('/');
    if (iDotPos != string::npos && iDotPos > iSlashPos) {//is a file
        strFile = strFile.substr(0, iSlashPos);
    }

#ifdef _WIN32
    return stdfs::create_directories(utf8_to_utf16(strFile));
#else
    stdfs::path p = strFile;
    return stdfs::create_directories(p);
#endif
}

static bool readFile(string path, string& data)
{
    FILE* fp = nullptr;
#ifdef _WIN32
    _wfopen_s(&fp, utf8_to_utf16(path).c_str(), L"rb");
#else
    fp = fopen(path.c_str(), "rb");
#endif
    if (fp)
    {
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        data.resize(len);
        fseek(fp, 0, SEEK_SET);
        fread((void*)data.data(), 1, len, fp);
        fclose(fp);
        return true;
    }
    return false;
}

static bool writeFile(string path, const char* data, size_t len)
{
    createFolderOfPath(path);

    FILE* fp = nullptr;
#ifdef _WIN32
    wstring wpath = utf8_to_utf16(path);
    _wfopen_s(&fp, wpath.c_str(), L"wb");
#else
    fp = fopen(path.c_str(), "wb");
#endif
    if (fp)
    {
        fwrite(data, 1, len, fp);
        fclose(fp);
        return true;
    }
    else
    {
#ifdef _WIN32
        DWORD errCode = GetLastError();
        printf("writeFile,path=%s,[error]%d", path.c_str(), errCode);
#endif
    }
    return false;
}


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


JSValue yyVal_to_qjsVal(JSContext* ctx, yyjson_val* val)
{
    if (!val) return JS_NULL;

    char* json_str = yyjson_val_write(val, YYJSON_WRITE_PRETTY, NULL);
    if (!json_str) {
        printf("yyjson serialization failed");
        return JS_NULL;
    }

    JSValue js_val = JS_ParseJSON(ctx, json_str, strlen(json_str), "<yyjson>");

    free(json_str);

    if (JS_IsException(js_val)) {
        JSValue error = JS_GetException(ctx);
        const char* error_msg = JS_ToCString(ctx, error);
        printf("JSON parse error: %s", error_msg);
        JS_FreeCString(ctx, error_msg);
        JS_FreeValue(ctx, error);
        return JS_NULL;
    }

    return js_val;
}

JSValue yyVal_to_qjsVal(JSContext* ctx, yyjson_mut_val* val) {
    if (!val) return JS_NULL;

    char* json_str = yyjson_mut_val_write(val, YYJSON_WRITE_PRETTY, NULL);
    if (!json_str) {
        printf("yyjson serialization failed");
        return JS_NULL;
    }

    JSValue js_val = JS_ParseJSON(ctx, json_str, strlen(json_str), "<yyjson>");

    free(json_str);

    if (JS_IsException(js_val)) {
        JSValue error = JS_GetException(ctx);
        const char* error_msg = JS_ToCString(ctx, error);
        printf("JSON parse error: %s", error_msg);
        JS_FreeCString(ctx, error_msg);
        JS_FreeValue(ctx, error);
        return JS_NULL;
    }

    return js_val;
}

yyjson_val* qjsVal_to_yyVal(JSContext* ctx, JSValueConst js_val, yyjson_doc*& doc) {
    JSValue json_str_val = JS_JSONStringify(ctx, js_val, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json_str_val)) {
        return nullptr;
    }

    const char* json_str = JS_ToCString(ctx, json_str_val);
    if (!json_str) {
        JS_FreeValue(ctx, json_str_val);
        return nullptr;
    }

    yyjson_read_err err;
    doc = yyjson_read((char*)json_str, strlen(json_str),0);
    if (doc == nullptr)
        return nullptr;

    yyjson_val* yyVal = yyjson_doc_get_root(doc);
 
    JS_FreeCString(ctx, json_str);
    JS_FreeValue(ctx, json_str_val);

    return yyVal;
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

/**
 * API 响应统一结构：{ success, result?, error? }
 * 优点：
 * 1. 显式状态：用 success 布尔值直接标记成功/失败，逻辑直观无歧义；
 * 2. 互斥结果：成功时仅含 result（操作型为 "OK"，数据型为业务数据），失败时仅含 error（结构化错误：code/message/details）；
 * 3. 覆盖全场景：兼容操作型（执行动作）、数据型（返回信息）、混合类型（动作+数据）及空结果；
 * 4. 错误透明：结构化 error 支持精准调试与分类处理，避免字符串/ null 歧义。
 */

extern "C" {
	static JSValue qjs_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

        std::string s;
        for (int i = 0; i < argc; i++) {
            const char* log = JS_ToCString(ctx, argv[i]);
            if (!log) {
                return JS_ThrowTypeError(ctx, "Argument must be a string");
            }
            if (s != "")
                s += " ";
            s += log;
            JS_FreeCString(ctx, log);
        }

		pEngine->m_vecOutput.push_back(s);
		return JS_NewObject(ctx);
	}

    static JSValue qjs_logToServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

        std::string s;
        if (JS_IsString(argv[0])) {
            const char* log = JS_ToCString(ctx, argv[0]);
            if (!log) {
                return JS_ThrowTypeError(ctx, "Argument must be a string");
            }
            s = log;
            JS_FreeCString(ctx, log);
        }
        else {
            JSValue json_str_val = JS_JSONStringify(ctx, argv[0], JS_UNDEFINED, JS_UNDEFINED);
            if (JS_IsException(json_str_val)) {
                return JS_UNDEFINED;
            }

            const char* json_str = JS_ToCString(ctx, json_str_val);
            if (!json_str) {
                JS_FreeValue(ctx, json_str_val);
                return JS_UNDEFINED;
            }
            s = json_str;
            JS_FreeCString(ctx, json_str);
            JS_FreeValue(ctx, json_str_val);
        }

        pEngine->m_vecOutput.push_back(s);
        LOG(s);
        return JS_UNDEFINED;
    }

    static JSValue qjs_setReturn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

        // 参数转 json
        json jArgs = engineArrayToJson(ctx, argv, argc);

        if (jArgs.size() == 1) {
            json j = jArgs[0];
            pEngine->m_scriptRet = j;
        }

        return JS_UNDEFINED;
    }

    static JSValue qjs_callMethod(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        // 参数转 json
        json jArgs = engineArrayToJson(ctx, argv, argc);

        if (jArgs.size() == 2) {
            json j = jArgs[0];
            string method = j.get<string>();
            json& params = jArgs[1];

            if (ScriptEngine::callMethodImp) {
                string rlt, err;
                ScriptEngine::callMethodImp(method, params.dump(), rlt,err);

                string resp;
                if(rlt != ""){
                    resp = "{\"result\":" + rlt + "}";
                }
                else if (err != "") {
                    resp = "{\"error\":" + err + "}";       
                }
                else {
                    return JS_UNDEFINED;
                }

                JSValue js_val = JS_ParseJSON(ctx, resp.c_str(), resp.size(), "<yyjson>");
                return js_val;
            }
            else if (ScriptEngine::callMethodRRImp) {
                string req = "{\"method\":\"" + method + "\",\"params\":" + params.dump() + "}";
                string resp;
                ScriptEngine::callMethodRRImp(req,resp);

                JSValue js_val = JS_ParseJSON(ctx, resp.c_str(), resp.size(), "<yyjson>");
                return js_val;
            }
        }

        return JS_UNDEFINED;
    }

	static JSValue qjs_http_request(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
#ifdef ENABLE_QJS_HTTP
		std::vector<JSValueConst> visited;
        yyjson_doc* yydoc = nullptr;
		yyjson_val* yyv = qjsVal_to_yyVal(ctx, argv[0],yydoc);
		if (yydoc && yyv && yyjson_is_obj(yyv)) {
            string ip;
            if (yyjson_obj_get(yyv, "hostname")) {
                ip = yyjson_get_str(yyjson_obj_get(yyv, "hostname"));
            }

            int port = 0;
            if (yyjson_obj_get(yyv, "port")) {
                port = yyjson_get_num(yyjson_obj_get(yyv, "port"));
            }

            string method;
            if (yyjson_obj_get(yyv, "method")) {
                method = yyjson_get_str(yyjson_obj_get(yyv, "method"));
            }

            string path;
            if (yyjson_obj_get(yyv, "path")) {
                path = yyjson_get_str(yyjson_obj_get(yyv, "path"));
            }

            string body;
            if (yyjson_obj_get(yyv, "body")) {
                body = yyjson_get_str(yyjson_obj_get(yyv, "body"));
            }

            string headers;
            if (yyjson_obj_get(yyv, "headers")) {
                yyjson_val* jHeaders = yyjson_obj_get(yyv, "headers");

                size_t idx, max;
                yyjson_val* key;
                yyjson_val* val;

                max = yyjson_obj_size(jHeaders);
                yyjson_obj_foreach(jHeaders, idx, max, key, val) {
                    const char* k = yyjson_get_str(key);
                    const char* v = yyjson_get_str(val);

                    if (k && v) {
                        headers += string(k) + ": " + string(v) + "\r\n";
                    }
                }
            }

            string url;
            if(port != 0)
                url = "http://" + ip + ":" + std::to_string(port) + path;
            else
                url = "http://" + ip + path;

            struct mg_mgr mgr;
            mg_mgr_init(&mgr);

            mg_http_data data;
            struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

            if (connect) {
                if (method == "POST") {
                    mg_printf(connect,
                        "POST %s HTTP/1.0\r\n"
                        "Host: %s\r\n"
                        "%s"
                        "Content-Type: application/json\r\n"
                        "Content-Length: %u\r\n"
                        "\r\n"
                        "%s",
                        path.c_str(), ip.c_str(), headers.c_str(), (unsigned int)body.size(), body.c_str()
                    );
                }
                else {
                    mg_printf(connect,
                        "GET %s HTTP/1.0\r\n"
                        "Host: %s\r\n"
                        "%s"
                        "\r\n",
                        path.c_str(), ip.c_str(), headers.c_str()
                    );
                }

                time_t tStart = time(nullptr);
                while (1) {
                    if (data.done)
                        break;
                    time_t tNow = time(nullptr);
                    if (tNow - tStart > 10)
                        break;
                    mg_mgr_poll(&mgr, 100);
                }
            }

            mg_mgr_free(&mgr);
            yyjson_doc_free(yydoc);

            JSValue ret = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, ret, "body", JS_NewString(ctx, data.body.c_str()));
            JS_SetPropertyStr(ctx, ret, "status", JS_NewInt32(ctx, data.status));
            return ret;
		}

		return JS_NewObject(ctx);
#else
        return JS_NULL;
#endif
	}

    static JSValue qjs_sleep(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc > 0) {
            int milli = 0;
            JS_ToInt32(ctx, &milli, argv[0]);
            std::this_thread::sleep_for(std::chrono::milliseconds(milli));
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

        //提取JSValue当中的时间字段


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

#ifdef ENABLE_SERIAL
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
#endif

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

    static JSValue qjs_ByteArray_readInt32LE(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 2) {
            return JS_NULL;
        }
        JSValue jsArray = argv[0];

        if (!JS_IsArray(ctx, jsArray)) {
            return JS_NULL;
        }

        if (!JS_IsNumber(argv[1])) {
            return JS_NULL;
        }

        int32_t offset = 0;
        JS_ToInt32(ctx, &offset, argv[1]);
        JSValue len_val = JS_GetPropertyStr(ctx, jsArray, "length");
        int32_t len = 0;
        JS_ToInt32(ctx, &len, len_val);
        JS_FreeValue(ctx, len_val);
        vector<unsigned char> byteArray;
        for (int32_t i = 0; i < len; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, jsArray, i);
            int32_t v = 0;
            JS_ToInt32(ctx, &v, item);
            byteArray.push_back(v);
            JS_FreeValue(ctx, item);
        }

        if (offset + 4 > byteArray.size()) {
            return JS_NULL;
        }

        int ret = 0;
        unsigned char* pRet = (unsigned char*)&ret;
        memcpy(pRet, byteArray.data() + offset, 4);
        return JS_NewInt32(ctx, ret);
    }
    static JSValue qjs_ByteArray_readUInt32LE(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 2) {
            return JS_NULL;
        }
        JSValue jsArray = argv[0];

        if (!JS_IsArray(ctx, jsArray)) {
            return JS_NULL;
        }

        if (!JS_IsNumber(argv[1])) {
            return JS_NULL;
        }

        int32_t offset = 0;
        JS_ToInt32(ctx, &offset, argv[1]);
        JSValue len_val = JS_GetPropertyStr(ctx, jsArray, "length");
        int32_t len = 0;
        JS_ToInt32(ctx, &len, len_val);
        JS_FreeValue(ctx, len_val);
        vector<unsigned char> byteArray;
        for (int32_t i = 0; i < len; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, jsArray, i);
            int32_t v = 0;
            JS_ToInt32(ctx, &v, item);
            byteArray.push_back(v);
            JS_FreeValue(ctx, item);
        }

        if (offset + 4 > byteArray.size()) {
            return JS_NULL;
        }

        uint32_t ret = 0;
        unsigned char* pRet = (unsigned char*)&ret;
        memcpy(pRet, byteArray.data() + offset, 4);
        return JS_NewUint32(ctx, ret);
    }
    static JSValue qjs_ByteArray_readFloatLE(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 2) {
            return JS_NULL;
        }
        JSValue jsArray = argv[0];

        if (!JS_IsArray(ctx, jsArray)) {
            return JS_NULL;
        }

        if (!JS_IsNumber(argv[1])) {
            return JS_NULL;
        }

        int32_t offset = 0;
        JS_ToInt32(ctx, &offset, argv[1]);
        JSValue len_val = JS_GetPropertyStr(ctx, jsArray, "length");
        int32_t len = 0;
        JS_ToInt32(ctx, &len, len_val);
        JS_FreeValue(ctx, len_val);
        vector<unsigned char> byteArray;
        for (int32_t i = 0; i < len; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, jsArray, i);
            int32_t v = 0;
            JS_ToInt32(ctx, &v, item);
            byteArray.push_back(v);
            JS_FreeValue(ctx, item);
        }

        if (offset + 4 > byteArray.size()) {
            return JS_NULL;
        }

        float ret = 0;
        unsigned char* pRet = (unsigned char*)&ret;
        memcpy(pRet, byteArray.data() + offset, 4);
        return JS_NewFloat64(ctx, ret);
    }
    static JSValue qjs_ByteArray_readInt16LE(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 2) {
            return JS_NULL;
        }
        JSValue jsArray = argv[0];

        if (!JS_IsArray(ctx, jsArray)) {
            return JS_NULL;
        }

        if (!JS_IsNumber(argv[1])) {
            return JS_NULL;
        }

        int32_t offset = 0;
        JS_ToInt32(ctx, &offset, argv[1]);
        JSValue len_val = JS_GetPropertyStr(ctx, jsArray, "length");
        int32_t len = 0;
        JS_ToInt32(ctx, &len, len_val);
        JS_FreeValue(ctx, len_val);
        vector<unsigned char> byteArray;
        for (int32_t i = 0; i < len; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, jsArray, i);
            int32_t v = 0;
            JS_ToInt32(ctx, &v, item);
            byteArray.push_back(v);
            JS_FreeValue(ctx, item);
        }

        if (offset + 4 > byteArray.size()) {
            return JS_NULL;
        }

        int16_t ret = 0;
        unsigned char* pRet = (unsigned char*)&ret;
        memcpy(pRet, byteArray.data() + offset, 2);
        return JS_NewInt64(ctx, ret);
    }
    static JSValue qjs_ByteArray_readUInt16LE(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 2) {
            return JS_NULL;
        }
        JSValue jsArray = argv[0];

        if (!JS_IsArray(ctx, jsArray)) {
            return JS_NULL;
        }

        if (!JS_IsNumber(argv[1])) {
            return JS_NULL;
        }

        int32_t offset = 0;
        JS_ToInt32(ctx, &offset, argv[1]);
        JSValue len_val = JS_GetPropertyStr(ctx, jsArray, "length");
        int32_t len = 0;
        JS_ToInt32(ctx, &len, len_val);
        JS_FreeValue(ctx, len_val);
        vector<unsigned char> byteArray;
        for (int32_t i = 0; i < len; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, jsArray, i);
            int32_t v = 0;
            JS_ToInt32(ctx, &v, item);
            byteArray.push_back(v);
            JS_FreeValue(ctx, item);
        }

        if (offset + 4 > byteArray.size()) {
            return JS_NULL;
        }

        uint16_t ret = 0;
        unsigned char* pRet = (unsigned char*)&ret;
        memcpy(pRet, byteArray.data() + offset, 2);
        return JS_NewInt64(ctx, ret);
    }
    static JSValue qjs_Byte_readBit(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        if (argc != 2) {
            return JS_NULL;
        }
        JSValue jsByteVal = argv[0];

        if (!JS_IsNumber(jsByteVal)) {
            return JS_NULL;
        }

        if (!JS_IsNumber(argv[1])) {
            return JS_NULL;
        }

        int32_t offset = 0;
        JS_ToInt32(ctx, &offset, argv[1]);
        
        if (offset > 7) {
            return JS_NULL;
        }

        int32_t byteVal32 = 0;
        JS_ToInt32(ctx, &byteVal32, jsByteVal);

        byteVal32 = (byteVal32 >> offset) & 0x01;
        return JS_NewInt64(ctx, byteVal32);
    }

    static JSValue qjs_db_select(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        JSValue jsVal = JS_NULL;
        if (argc == 1) {
            JSValue jsParams = argv[0];
            if (JS_IsObject(jsParams)) {
                string err, rlt, queryInfo;
                yyjson_doc* yydoc = nullptr;
                qjsVal_to_yyVal(ctx, jsParams, yydoc);

                if (yydoc) {
                    yyjson_val* yyParams = yyjson_doc_get_root(yydoc);
                    db.handleRpc("db.select",yyParams, rlt, err, queryInfo, "", "zh");

                    string ret;
                    if (!rlt.empty()) {
                        ret = "{\"result\":" + rlt;
                    }
                    else if (!err.empty()) {
                        ret = "{\"error\":" + rlt;
                    }
                    else {
                        ret = "{\"error\":\"unhandled\"}";
                    }

                    if (!queryInfo.empty()) {
                        ret += ",\"queryInfo\":" + queryInfo;
                    }
                    ret +="}";

                    jsVal = JS_ParseJSON(ctx, ret.c_str(), ret.length(), nullptr);
                    yyjson_doc_free(yydoc);     
                }
            }
        }

        return jsVal;
    }

    static JSValue qjs_db_insert(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);
        string ret = "{\"success\":";
        string err, rlt, dbQi;
        if (argc == 1) {
            JSValue params = argv[0];
            JSValue json_str_val = JS_JSONStringify(ctx, params, JS_UNDEFINED, JS_UNDEFINED);
            if (JS_IsException(json_str_val)) {
                return JS_NULL;
            }
            const char* json_str = JS_ToCString(ctx, json_str_val);
            if (!json_str) {
                JS_FreeValue(ctx, json_str_val);
                return JS_NULL;
            }
            string sParams = json_str;
            JS_FreeValue(ctx, json_str_val);
            if (JS_IsObject(params)) {
                if (sParams.find("table")!=string::npos) {
                    if (sParams.find("tableType") == string::npos) {
                        return JS_NULL;
                    }
                    yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
                    yyjson_val* yyv_params = yyjson_doc_get_root(doc);
                    db.rpc_db_table_insert(yyv_params, rlt, err, dbQi, "", "zh");
                    yyjson_doc_free(doc);
                }
                else {
                    db.rpc_db_insert(sParams, rlt, err, dbQi, "", "zh");
                }
            }
            else {
                err = "\"missing params\"";
            }
        }
        else if (argc >= 2) {
            JSValue jsTag = argv[0];
            JSValue jsVal = argv[1];
            const char* s = JS_ToCString(ctx, jsTag);
            string tag = s ? s : "";
            JS_FreeCString(ctx, s);
            JSValue json_str_val = JS_JSONStringify(ctx, jsVal, JS_UNDEFINED, JS_UNDEFINED);
            if (JS_IsException(json_str_val)) {
                return JS_NULL;
            }
            s = JS_ToCString(ctx, json_str_val);
            if (!s) {
                JS_FreeValue(ctx, json_str_val);
                return JS_NULL;
            }
            string val_jstr = s;
            string time;
            if (argc >= 3) {
                JSValue jsTime = argv[2];
                s = JS_ToCString(ctx, jsTag);
                time = s ? s : "";
                JS_FreeCString(ctx, s);
            }
            string sParams = "{\"tag\":\"" + tag + "\"," +
                "\"val\":" + val_jstr;
            if (time != "")
                sParams += ",\"time\":\"" + time + "\"";
            sParams += "}";
            db.rpc_db_insert(sParams, rlt, err, dbQi, "", "zh");
        }

        ret += (rlt != "" ? "true" : "false");
        if (rlt != "") {
            ret += ",\"result\":" + rlt;
        }
        else {
            ret += ",\"error\":" + err;
        }
        ret += "}";
        JSValue js_val = JS_ParseJSON(ctx, ret.data(), ret.length(), "<yyjson>");
        if (JS_IsException(js_val)) {
            JSValue exception = JS_GetException(ctx);
            const char* error_msg = JS_ToCString(ctx, exception);
            printf("File:%s,Line:%d,JSON Parse Error: %s\n",__FILE__,__LINE__,ret.c_str());
            JS_FreeCString(ctx, error_msg);
            JS_FreeValue(ctx, exception);
            JS_FreeValue(ctx, js_val);
            return JS_NULL;
        }
        
        return js_val;
    }

    static JSValue qjs_db_delete(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        JSValue jsVal = JS_NULL;
        if (argc == 1) {
            JSValue jsParams = argv[0];
            if (JS_IsObject(jsParams)) {
                string err, rlt, queryInfo;
                yyjson_doc* yydoc = nullptr;
                qjsVal_to_yyVal(ctx, jsParams, yydoc);

                if (yydoc) {
                    yyjson_val* yyParams = yyjson_doc_get_root(yydoc);
                    db.handleRpc("db.delete", yyParams, rlt, err, queryInfo, "", "zh");

                    string ret;
                    if (!rlt.empty()) {
                        ret = "{\"result\":" + rlt;
                    }
                    else if (!err.empty()) {
                        ret = "{\"error\":" + rlt;
                    }
                    else {
                        ret = "{\"error\":\"unhandled\"}";
                    }

                    if (!queryInfo.empty()) {
                        ret += ",\"queryInfo\":" + queryInfo;
                    }
                    ret += "}";

                    jsVal = JS_ParseJSON(ctx, ret.c_str(), ret.length(), nullptr);
                    yyjson_doc_free(yydoc);
                }
            }
        }

        return jsVal;
    }

    static JSValue qjs_db_update(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        JSValue jsVal = JS_NULL;
        if (argc == 1) {
            JSValue jsParams = argv[0];
            if (JS_IsObject(jsParams)) {
                string err, rlt, queryInfo;
                yyjson_doc* yydoc = nullptr;
                qjsVal_to_yyVal(ctx, jsParams, yydoc);

                if (yydoc) {
                    yyjson_val* yyParams = yyjson_doc_get_root(yydoc);
                    db.handleRpc("db.update", yyParams, rlt, err, queryInfo, "", "zh");

                    string ret;
                    if (!rlt.empty()) {
                        ret = "{\"result\":" + rlt;
                    }
                    else if (!err.empty()) {
                        ret = "{\"error\":" + rlt;
                    }
                    else {
                        ret = "{\"error\":\"unhandled\"}";
                    }

                    if (!queryInfo.empty()) {
                        ret += ",\"queryInfo\":" + queryInfo;
                    }
                    ret += "}";

                    jsVal = JS_ParseJSON(ctx, ret.c_str(), ret.length(), nullptr);
                    yyjson_doc_free(yydoc);
                }
            }
        }

        return jsVal;
    }

    static JSValue qjs_setConfFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        json jArgs = engineArrayToJson(ctx, argv, argc);

        if (jArgs.size() == 2) {
            json path = jArgs[0];
            json data = jArgs[1];
            if (path.is_string() && data.is_string()) {
                std::string rPath = path.get<std::string>();
                string sData = data.get<string>();

                string aPath = ScriptEngine::ConfFolder + "/" + rPath;
                createFolderOfPath(aPath);

                bool bRet = writeFile(aPath, sData.c_str(),sData.length());
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

                sPath = ScriptEngine::ConfFolder + "/" + sPath;
                bool bRet = readFile(sPath, sData);
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
} 

void register_cpp_functions(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);

    JS_SetPropertyStr(ctx, global, "log", JS_NewCFunction(ctx, qjs_log, "log", 1));
    JS_SetPropertyStr(ctx, global, "logToServer", JS_NewCFunction(ctx, qjs_logToServer, "logToServer", 1));
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, qjs_log, "log", 1));
    JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, qjs_log, "error", 1));
    JS_SetPropertyStr(ctx, global, "console", console);

    JSValue http = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, http, "request", JS_NewCFunction(ctx, qjs_http_request, "request", 1));
    JS_SetPropertyStr(ctx, global, "http", http);

    JS_SetPropertyStr(ctx, global, "sleep", JS_NewCFunction(ctx, qjs_sleep, "sleep", 1));
    JS_SetPropertyStr(ctx, global, "backtrace", JS_NewCFunction(ctx, qjs_backtrace, "backtrace", 1));

    JSValue strObj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, strObj, "toHexStr", JS_NewCFunction(ctx, qjs_str_toHexStr, "toHexStr", 1));
    JS_SetPropertyStr(ctx, global, "STR", strObj);

    JSValue ByteArray = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ByteArray, "readInt32LE", JS_NewCFunction(ctx, qjs_ByteArray_readInt32LE, "readInt32LE", 2));
    JS_SetPropertyStr(ctx, ByteArray, "readUInt32LE", JS_NewCFunction(ctx, qjs_ByteArray_readUInt32LE, "readUInt32LE", 2));
    JS_SetPropertyStr(ctx, ByteArray, "readInt16LE", JS_NewCFunction(ctx, qjs_ByteArray_readInt16LE, "readInt16LE", 2));
    JS_SetPropertyStr(ctx, ByteArray, "readUInt16LE", JS_NewCFunction(ctx, qjs_ByteArray_readUInt16LE, "readUInt16LE", 2));
    JS_SetPropertyStr(ctx, ByteArray, "readFloatLE", JS_NewCFunction(ctx, qjs_ByteArray_readFloatLE, "readFloatLE", 2));
    JS_SetPropertyStr(ctx, global, "ByteArray", ByteArray);

    JSValue Byte = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, Byte, "readBit", JS_NewCFunction(ctx, qjs_Byte_readBit, "readBit", 2));
    JS_SetPropertyStr(ctx, global, "Byte", Byte);

    JS_SetPropertyStr(ctx, global, "time", JS_NewCFunction(ctx, qjs_time, "time", 0));

#ifdef ENABLE_SERIAL
    JS_SetPropertyStr(ctx, global, "openSerial", JS_NewCFunction(ctx, qjs_openSerial, "openSerial", 5));
    JS_SetPropertyStr(ctx, global, "readSerial", JS_NewCFunction(ctx, qjs_readSerial, "readSerial", 1));
    JS_SetPropertyStr(ctx, global, "writeSerial", JS_NewCFunction(ctx, qjs_writeSerial, "writeSerial", 2));
    JS_SetPropertyStr(ctx, global, "closeSerial", JS_NewCFunction(ctx, qjs_closeSerial, "closeSerial", 1));
#endif

    JS_SetPropertyStr(ctx, global, "arrayToStr", JS_NewCFunction(ctx, qjs_arrayToStr, "arrayToStr", 1));
    JS_SetPropertyStr(ctx, global, "strToArray", JS_NewCFunction(ctx, qjs_strToArray, "strToArray", 1));
    JS_SetPropertyStr(ctx, global, "setReturn", JS_NewCFunction(ctx, qjs_setReturn, "setReturn", 1));
    JS_SetPropertyStr(ctx, global, "callMethod", JS_NewCFunction(ctx, qjs_callMethod, "callMethod", 2));
    JS_SetPropertyStr(ctx, global, "call", JS_NewCFunction(ctx, qjs_callMethod, "call", 2));

    JSValue db = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, db, "select", JS_NewCFunction(ctx, qjs_db_select, "select", 1));
    JS_SetPropertyStr(ctx, db, "insert", JS_NewCFunction(ctx, qjs_db_insert, "insert", 3));
    JS_SetPropertyStr(ctx, db, "delete", JS_NewCFunction(ctx, qjs_db_delete, "delete", 1));
    JS_SetPropertyStr(ctx, db, "update", JS_NewCFunction(ctx, qjs_db_update, "update", 1));
    JS_SetPropertyStr(ctx, global, "db", db);

    JS_SetPropertyStr(ctx, global, "setConfFile", JS_NewCFunction(ctx, qjs_setConfFile, "setConfFile", 2));
    JS_SetPropertyStr(ctx, global, "getConfFile", JS_NewCFunction(ctx, qjs_getConfFile, "getConfFile", 1));
    JS_SetPropertyStr(ctx, global, "getConfFileBuff", JS_NewCFunction(ctx, qjs_getConfFileBuff, "getConfFileBuff", 1));

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

mutex g_mutexScriptFileBuff;
map<string, string> g_mapScriptFileBuff;

void thread_reloadFile(string filePath) {
    if (filePath.empty()) {
        return;
    }

    //收到通知时，文件可能还在写入，因此等待一点时间再读取
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    string file_data;
    if (readFile(filePath, file_data)) {
        g_mutexScriptFileBuff.lock();
        g_mapScriptFileBuff[filePath] = file_data;
        g_mutexScriptFileBuff.unlock();
	}
}

//脚本目录下的所有文件，发现文件直接加载到内存
void thread_watchScriptFile(string scriptPath) {
#ifdef _WIN32
    if (scriptPath.empty()) {
        return;
    }
    wstring wPath = utf8_to_utf16(scriptPath);


    HANDLE h_dir = CreateFileW(
        wPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, // 启用异步 IO
        nullptr
    );
    if (h_dir == INVALID_HANDLE_VALUE) {
        LOG("脚本目录打开目录失败，错误码: %lu", GetLastError());
        return;
    }

    const DWORD BUFFER_SIZE = 1024 * 1024; // 1MB 缓冲区（根据需求调整）
    BYTE* buffer = new BYTE[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    DWORD bytes_returned;
    OVERLAPPED overlapped = { 0 };

    while (true) {
        // 非阻塞调用：立即返回已有的事件（若有）
        BOOL is_ok = ReadDirectoryChangesW(
            h_dir,
            buffer,
            BUFFER_SIZE,
            TRUE, // 监控子目录
            FILE_NOTIFY_CHANGE_LAST_WRITE, // 监控最后写入时间变化
            &bytes_returned,
            nullptr,  // 改为&overlapped则为异步
            nullptr
        );

        if (!is_ok) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // 异步操作未完成，等待事件（设置超时避免永久阻塞）
                DWORD wait_result = WaitForSingleObject(overlapped.hEvent, 100); // 等待 100ms
                if (wait_result == WAIT_TIMEOUT) {
                    continue; // 超时后重试
                }
                else if (wait_result != WAIT_OBJECT_0) {
                    LOG("脚本目录等待事件失败，错误码: %lu", GetLastError());
                    break;
                }
            }
            else {
                LOG("脚本目录ReadDirectoryChangesW 失败，错误码: %lu", error);
                break;
            }
        }
        else {
            // 处理所有已完成的事件（循环读取直到无新事件）
            FILE_NOTIFY_INFORMATION* notify_info = (FILE_NOTIFY_INFORMATION*)buffer;

            while (true) {
                // 提取文件名（UTF-16）
                std::wstring file_name_w(notify_info->FileName, notify_info->FileNameLength / sizeof(WCHAR));
                std::string file_name_utf8 = utf16_to_utf8(file_name_w.c_str());

                // 处理修改事件（FILE_ACTION_MODIFIED）
                if (notify_info->Action == FILE_ACTION_MODIFIED) {
                    string file_name = utf16_to_utf8(file_name_w);//得到发生变化的文件名
                    file_name = str::replace(file_name, "\\", "/");
                    string file_path = ScriptEngine::ScriptFolder + "/" + file_name;
                    LOG("[keyinfo]脚本目录检测到脚本文件改变:" + file_path);
					thread th(thread_reloadFile, file_path);
                    th.detach();
                }
                // 移动到下一个事件
                if (notify_info->NextEntryOffset == 0) 
                    break; 
                else
                    notify_info = (FILE_NOTIFY_INFORMATION*)((BYTE*)notify_info + notify_info->NextEntryOffset);
            }

            // 重置缓冲区（可选，根据文件系统要求）
            memset(buffer, 0, BUFFER_SIZE);
        }
    }

    delete[] buffer;
    CloseHandle(h_dir);
#else
    // 1. 创建inotify实例
    int inotify_fd = inotify_init1(IN_NONBLOCK); // 非阻塞模式
    if (inotify_fd == -1) {
        LOG("脚本目录inotify_init1 失败，错误码: %d", GetLastError());
        return;
    }

    // 2. 添加监控目录（监控文件修改事件）
    int watch_fd = inotify_add_watch(
        inotify_fd,
        scriptPath.c_str(),
        IN_MODIFY // 监控文件修改（对应Windows的FILE_NOTIFY_CHANGE_LAST_WRITE）
    );
    if (watch_fd == -1) {
        LOG("脚本目录inotify_add_watch 失败，错误码: %d", GetLastError());
        close(inotify_fd);
        return;
    }

    // 3. 事件缓冲区（inotify事件结构大小固定，1024足够）
    const int BUFFER_SIZE = 1024 * (sizeof(inotify_event) + 256);
    char* buffer = new char[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    while (true) {
        // 4. 非阻塞读取事件（无事件时返回-1，errno=EAGAIN）
        ssize_t len = read(inotify_fd, buffer, BUFFER_SIZE);
        if (len == -1) {
            if (errno == EAGAIN) {
                // 无事件时休眠100ms，避免空轮询
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            LOG("脚本目录read inotify 失败，错误码: %d", GetLastError());
            break;
        }

        // 5. 解析所有事件
        char* ptr = buffer;
        while (ptr < buffer + len) {
            inotify_event* event = (inotify_event*)ptr;

            // 仅处理文件修改事件（排除目录、排除空文件名）
            if (event->mask & IN_MODIFY && event->len > 0) {
                std::string file_name = event->name;
                file_name = str::replace(file_name, "\\", "/");
                std::string file_path = ScriptEngine::ScriptFolder + "/" + file_name;
                LOG("[keyinfo]脚本目录检测到脚本文件改变:" + file_path);

                // 异步热加载脚本（和Windows逻辑一致）
                std::thread th(thread_reloadFile, file_path);
                th.detach();
            }

            // 移动到下一个事件
            ptr += sizeof(inotify_event) + event->len;
        }
    }

    // 6. 释放资源
    inotify_rm_watch(inotify_fd, watch_fd);
    close(inotify_fd);
    delete[] buffer;
#endif
}

bool loadScriptFile(string path, string& script) {
    lock_guard<mutex> g(g_mutexScriptFileBuff);
    map<string, string>::iterator iter = g_mapScriptFileBuff.find(path);
    if (iter != g_mapScriptFileBuff.end()) {
        script = iter->second;
        return true;
    }
    else {
        if (!readFile(path, script)) {
            g_mapScriptFileBuff.erase(path);
            return false;
        }
        else{
            g_mapScriptFileBuff[path] = script;
            return true;
        }
    }
}

JSModuleDef* qjs_module_loader(JSContext* ctx, const char* module_name, void* opaque) {
    ScriptEngine* pEngine = (ScriptEngine*)JS_GetContextOpaque(ctx);

    string s = str::trim(module_name,".");
	string path = pEngine->m_folderPath + "/" + s;

    string script;
    bool ret = loadScriptFile(path, script);

    if (!ret) {
        JS_ThrowReferenceError(ctx, "Cannot open module: %s", module_name);
        return NULL;
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

fp_callMethod ScriptEngine::callMethodImp = nullptr;
fp_callMethodRR ScriptEngine::callMethodRRImp = nullptr;
string ScriptEngine::ScriptFolder = "";
string ScriptEngine::ConfFolder = "";

ScriptEngine::ScriptEngine() {
	m_ioDevThis = nullptr;

    m_bValNullInCalc = false;
    m_reloadFile = false;
    m_envVarScriptLine = 0;
}

bool ScriptEngine::runScript(SCRIPT_INFO& si, SCRIPT_RUN_INFO& sri) {
    if (si.script == "") {
        string scriptPath;
        if (si.filePath != "") {
            scriptPath = si.filePath;
            if (si.folderPath == "") {
				size_t pos = si.filePath.find_last_of("/");
                if (pos != string::npos) {
					si.folderPath = si.filePath.substr(0, pos);
                }
            }	
        }
        else {
            scriptPath = ScriptEngine::ScriptFolder + "/" + si.name + "/main.js";
            if (si.folderPath == "")
                si.folderPath = ScriptEngine::ScriptFolder + "/" + si.name;
        }
        loadScriptFile(scriptPath, m_script);
    }
    else {
        m_script = si.script;
    }

	m_user = si.user;

	m_vecOutput.clear();
	bool runOk = false;

    sri.lastExe = TIME::nowStr(true);
    sri.lastError = "";
	try {
		TIME tStart;
        tStart.setNow();

        m_folderPath = si.folderPath;
		
		// 初始化 QuickJS
		JSRuntime* rt = JS_NewRuntime();
		JSContext* ctx = JS_NewContext(rt);

        JS_SetContextOpaque(ctx, this);
        JS_SetModuleLoaderFunc(rt, NULL, qjs_module_loader, NULL);

		register_cpp_functions(ctx);
        for(auto& f : m_engineInitFuncList){
            f(ctx, m_ioDevThis);
        }

        if (si.isExp) {
            JSValue result = JS_Eval(ctx, m_script.c_str(), m_script.length(), "main.js", JS_EVAL_TYPE_GLOBAL);
            if (!JS_IsException(result)) {
                jsValToJsonVal(ctx, result, m_sEvalRet);
                string s = "Eval Return:" + m_sEvalRet.dump();
                m_vecOutput.push_back(s);
                JS_FreeValue(ctx, result);

                runOk = true;
            }
        }
        else {
            if (!m_globalObj.is_null()) {
                JSValue global = JS_GetGlobalObject(ctx);
                for (auto it = m_globalObj.items().begin(); it != m_globalObj.items().end(); ++it) {
                    const std::string& key = it.key();
                    json& value = it.value();

                    JSValue prop_value;
                    jsonValToJsVal(value, ctx, prop_value);

                    JS_SetPropertyStr(ctx, global, key.c_str(), prop_value);
                }

                JS_FreeValue(ctx, global);
            }

            JSValue mockRet = JS_Eval(ctx, si.envVarScript.c_str(), si.envVarScript.length(), "mock.js", JS_EVAL_TYPE_GLOBAL);

            JSValue evalPromise;
            evalPromise = JS_Eval(ctx, m_script.c_str(), m_script.length(), "main.js", JS_EVAL_TYPE_MODULE);

            while (JS_PromiseState(ctx, evalPromise) == JS_PROMISE_PENDING) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            int state = JS_PromiseState(ctx, evalPromise);
            if (state == JS_PROMISE_REJECTED) {
                JSValue err = JS_PromiseResult(ctx, evalPromise);
                JSValue err_msg = JS_GetPropertyStr(ctx, err, "message");
                JSValue err_stack = JS_GetPropertyStr(ctx, err, "stack");
                const char* msg = JS_ToCString(ctx, err_msg);
                const char* stack = JS_ToCString(ctx, err_stack);

                JS_FreeCString(ctx, msg);
                JS_FreeValue(ctx, err_msg);
                JS_FreeCString(ctx, stack);
                JS_FreeValue(ctx, err_stack);
                JS_FreeValue(ctx, err);

                string s = msg;
                s += "\n";
                s += stack;
                s = str::replace(s, "\n", "\r\n");
                sri.lastError = s;
                m_vecOutput.push_back(s);
            }
            else if (state == JS_PROMISE_FULFILLED) {
                if (!m_globalObj.is_null()) {
                    JSValue global = JS_GetGlobalObject(ctx);
                    for (auto it = m_globalObj.items().begin(); it != m_globalObj.items().end(); ++it) {
                        const std::string& key = it.key();
                        json& value = it.value();

                        JSValue prop_value = JS_GetPropertyStr(ctx, global, key.c_str());
                        jsValToJsonVal(ctx, prop_value, value);

                        JS_FreeValue(ctx, prop_value);
                    }

                    JS_FreeValue(ctx, global);
                }

                runOk = true;
            }

            JS_FreeValue(ctx, evalPromise);            
        }

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

void ScriptEngine::init()
{
    thread t(thread_watchScriptFile, ScriptEngine::ScriptFolder);
    t.detach();
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
    yyjson_mut_obj_add_str(doc, yyVal, "lastError", lastError.c_str());
	yyjson_mut_obj_add_bool(doc, yyVal, "runSuccess", runSuccess);
	yyjson_mut_obj_add_bool(doc, yyVal, "valNullInCalc", valNullInCalc);


    yyjson_mut_val* yyTagRefDataTime = yyjson_mut_obj(doc);
    for (auto& iter : tagRefDataTime) {
        string key = iter.first;
        string value = iter.second;
        yyjson_mut_obj_add_str(doc, yyTagRefDataTime, key.c_str(), value.c_str());
    }

    yyjson_mut_obj_add_val(doc, yyVal, "tagRefDataTime", yyTagRefDataTime);
}

string SCRIPT_INFO::getContextTag() {
    string envTag = rootTag;
    envTag = TAG::addRoot(envTag, callerObjTag);
    envTag = TAG::addRoot(envTag, org);

    return envTag;
}

string SCRIPT_INFO::getExpContextTag() {
    string envTag = rootTag;
    envTag = TAG::addRoot(envTag, callerObjTag);
    envTag = TAG::addRoot(envTag, org);

    return envTag;
}

void SCRIPT_INFO::toJson(yyjson_mut_doc* mutDoc, yyjson_mut_val* mutRoot, bool getStatus) {
    yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "mode", mode.c_str());
    yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "name", name.c_str());
    yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "desc", desc.c_str());
    yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "lastModifyTime", lastModifyTime.c_str());
    yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "lastModifyUser", lastModifyUser.c_str());

    int min = interval / (60 * 1000);
    int time = interval % (60 * 1000);
    int sec = time / 1000;
    int milli = time % 1000;

    yyjson_mut_val* jIter = yyjson_mut_obj(mutDoc);
    yyjson_mut_obj_add_int(mutDoc, jIter, "min", min);
    yyjson_mut_obj_add_int(mutDoc, jIter, "sec", sec);
    yyjson_mut_obj_add_int(mutDoc, jIter, "milli", milli);
    yyjson_mut_obj_add_val(mutDoc, mutRoot, "interval", jIter);

    yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "rootTag", rootTag.c_str());
    yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "devAddr", devAddr.c_str());
    yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "devId", devId.c_str());
    yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "calcMpTag", calcMpTag.c_str());
    yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "callerObjTag", callerObjTag.c_str());

    if (getStatus) {
        yyjson_mut_val* runInfo = yyjson_mut_obj(mutDoc);
        lastRunInfo.toJson(mutDoc, runInfo);
        yyjson_mut_obj_add_val(mutDoc, mutRoot, "runInfo", runInfo);
    }

    yyjson_mut_obj_add_bool(mutDoc, mutRoot, "enable", enable);
}

void SCRIPT_INFO::fromJson(yyjson_val* root) {
    yyjson_val* val;

    val = yyjson_obj_get(root, "mode");
    if (val && yyjson_is_str(val)) {
        mode = yyjson_get_str(val);
    }

    val = yyjson_obj_get(root, "name");
    if (val && yyjson_is_str(val)) {
        name = yyjson_get_str(val);
    }

    val = yyjson_obj_get(root, "lastModifyUser");
    if (val && yyjson_is_str(val)) {
        lastModifyUser = yyjson_get_str(val);
    }

    val = yyjson_obj_get(root, "desc");
    if (val && yyjson_is_str(val)) {
        desc = yyjson_get_str(val);
    }

    val = yyjson_obj_get(root, "rootTag");
    if (val && yyjson_is_str(val)) {
        rootTag = yyjson_get_str(val);
    }

    val = yyjson_obj_get(root, "devAddr");
    if (val && yyjson_is_str(val)) {
        devAddr = yyjson_get_str(val);
    }

    val = yyjson_obj_get(root, "devId");
    if (val && yyjson_is_str(val)) {
        devId = yyjson_get_str(val);
    }

    val = yyjson_obj_get(root, "interval");
    if (val && yyjson_is_obj(val)) {
        int min = 0, sec = 0, milli = 0;
        yyjson_val* vmin = yyjson_obj_get(val, "min");
        yyjson_val* vsec = yyjson_obj_get(val, "sec");
        yyjson_val* vmilli = yyjson_obj_get(val, "milli");

        if (vmin && yyjson_is_int(vmin)) {
            min = (int)yyjson_get_int(vmin);
        }

        if (vsec && yyjson_is_int(vsec)) {
            sec = (int)yyjson_get_int(vsec);
        }

        if (vmilli && yyjson_is_int(vmilli)) {
            milli = (int)yyjson_get_int(vmilli);
        }

        interval = min * 60 * 1000 + sec * 1000 + milli;
    }

    val = yyjson_obj_get(root, "enable");
    if (val && yyjson_is_bool(val)) {
        enable = yyjson_get_bool(val);
    }
}