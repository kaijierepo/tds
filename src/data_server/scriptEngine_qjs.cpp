#include "ScriptEngine_qjs.h"
#include "common.h"
#include "cutils.h"
#include "quickjs-libc.h"
#include "httplib.h"
#include "yyjson.h"

thread_local ScriptEngine_qjs* pEngine_qjs;


// 递归将 JS 值转换为 yyjson 值
static yyjson_mut_val* js_value_to_yyjson(
    JSContext* ctx,
    yyjson_mut_doc* doc,
    JSValueConst val,
    std::vector<JSValueConst>& visited);

// 处理对象类型
static yyjson_mut_val* handle_object(
    JSContext* ctx,
    yyjson_mut_doc* doc,
    JSValueConst obj,
    std::vector<JSValueConst>& visited) {

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

    if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, obj,
        JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
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
static yyjson_mut_val* handle_array(
    JSContext* ctx,
    yyjson_mut_doc* doc,
    JSValueConst arr,
    std::vector<JSValueConst>& visited) {

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
static yyjson_mut_val* js_value_to_yyjson(
    JSContext* ctx,
    yyjson_mut_doc* doc,
    JSValueConst val,
    std::vector<JSValueConst>& visited) {

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

extern "C" {
	static JSValue qjs_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
		const char* log = JS_ToCString(ctx, argv[0]);
		if (!log) return JS_ThrowTypeError(ctx, "Argument must be a string");
		std::string s = log;
		pEngine_qjs->m_vecOutput.push_back(s);
		JS_FreeCString(ctx, log); 

		return JS_NewObject(ctx);
	}


	static JSValue qjs_http_request(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
	{
		yyjson_mut_doc* mdoc = yyjson_mut_doc_new(nullptr);
		std::vector<JSValueConst> visited;// 用于检测循环引用的容器
		yyjson_mut_val* yyv = js_value_to_yyjson(ctx, mdoc, argv[0], visited);
		string s = yyjson_mut_val_write(yyv,0,nullptr);
		if(yyv)
		{
			if (yyjson_mut_is_obj(yyv)) {
				yyjson_mut_val* k = yyjson_mut_obj_get(yyv, "hostname");
				string ip;
				if (k)ip=yyjson_mut_get_str(k);
				string addr = "http://" + ip;
				k = yyjson_mut_obj_get(yyv, "port");
				int port = 0;
				if(k)port = yyjson_mut_get_num(k);
				string method;
				k = yyjson_mut_obj_get(yyv, "method");
				if (k)method = yyjson_mut_get_str(k);
				httplib::Client cli(ip, port);

				k = yyjson_mut_obj_get(yyv, "path");
				string path;
				if (k)path = yyjson_mut_get_str(k);
				string body;
				httplib::Headers headers;
				k = yyjson_mut_obj_get(yyv, "body");
				if (k) {
					body = yyjson_mut_get_str(k);
				}
				k = yyjson_mut_obj_get(yyv, "headers");
				if (k) {
	/*				json jHeaders = params["headers"];
					for (auto it = jHeaders.begin(); it != jHeaders.end(); ++it) {
						std::pair<string, string> p = { it.key(),it.value() };
						headers.insert(p);
					}*/
				}
				if (method == "GET") {
					httplib::Result rlt = cli.Get(path, headers);
					if (rlt != nullptr) {
						JSValue ret = JS_NewObject(ctx);
						JSValue body = JS_NewString(ctx, rlt->body.c_str());
						JS_SetPropertyStr(ctx, ret, "body", body);
						return ret;
					}
				}
				else if (method == "POST") {
					httplib::Result rlt = cli.Post(path, headers, body, "application/json");
					if (rlt != nullptr) {
						JSValue ret = JS_NewObject(ctx);
						JSValue body = JS_NewString(ctx, rlt->body.c_str());
						JS_SetPropertyStr(ctx, ret, "body", body);
						return ret;
					}
				}
			}
		}
		yyjson_mut_doc_free(mdoc);
		return JS_NewObject(ctx);
	}
} 

void register_cpp_functions(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);

    JS_SetPropertyStr(
        ctx,
        global,
        "log",
        JS_NewCFunction(ctx, qjs_log, "log", 1)
    );

    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(
        ctx,
        console,
        "log",
        JS_NewCFunction(ctx, qjs_log, "log", 1)
    );
    JS_SetPropertyStr(ctx, global, "console", console); // 将 console 设置为全局对象的属性


    /* 如果启用 http 对象，同样需要释放 */
     JSValue http = JS_NewObject(ctx);
     JS_SetPropertyStr(
         ctx,
         http,
         "request",
         JS_NewCFunction(ctx, qjs_http_request, "request", 1)
     );
     JS_SetPropertyStr(ctx, global, "http", http);

    JS_FreeValue(ctx, global); // 释放全局对象引用
}



ScriptEngine_qjs::ScriptEngine_qjs()
{
	m_ioDevThis = nullptr;
}

// 查找错误行号的辅助函数
int extract_line_number(const char* stack_str) {
    const char* line_pos = strstr(stack_str, ":");
    if (!line_pos) return -1;  // 未找到行号

    line_pos++;  // 跳过冒号

    // 解析行号
    int line = 0;
    while (*line_pos >= '0' && *line_pos <= '9') {
        line = line * 10 + (*line_pos - '0');
        line_pos++;
    }

    return line > 0 ? line : -1;
}

bool ScriptEngine_qjs::runScript(string& script, string user)
{
	m_script = script;
	m_user = user;
	m_vecOutput.clear();
	bool runOk = false;
	try {
		TIME tStart = timeopt::now();
		pEngine_qjs = this;
		
		// 初始化 QuickJS
		JSRuntime* rt = JS_NewRuntime();
		JSContext* ctx = JS_NewContext(rt);

		register_cpp_functions(ctx);

		JSValue result = JS_Eval(ctx, script.c_str(), script.length(), "<main>", JS_EVAL_TYPE_GLOBAL);

		if (JS_IsException(result)) {
			JSValue error = JS_GetException(ctx);
			const char* err = JS_ToCString(ctx, error);

            JSValue stack_val = JS_GetPropertyStr(ctx, error, "stack");
            const char* stack = JS_ToCString(ctx, stack_val);

            // 提取行号
            int line = extract_line_number(stack);

			string s = err;
			s = "Exception at line " + str::fromInt(line) + ":" + s;
			m_vecOutput.push_back(s);
			JS_FreeCString(ctx, err);
			JS_FreeValue(ctx, error);
		}

		// 清理资源
		JS_FreeValue(ctx, result);
		JS_FreeContext(ctx);
		JS_FreeRuntime(rt);

		int costMilli = timeopt::calcTimePassMilliSecond(tStart);
		m_vecOutput.push_back("执行耗时:" + str::fromInt(costMilli) + "ms");
	}
	catch (std::exception& e)
	{
		string s = e.what();
		m_vecOutput.push_back(s);
		return false;
	}
	return runOk;
}

