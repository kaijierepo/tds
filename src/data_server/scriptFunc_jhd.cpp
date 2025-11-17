#if defined(ENABLE_QJS) && defined(JHD)
#include "scriptFunc_jhd.h"
#include "logger.h"

extern string g_strScriptFuncConfPath = "";

static std::string utf82gbk(/*char* gbkStr, int maxGbkStrlen, */const char* srcStr)
{
	if (NULL == srcStr)
	{
		printf("Bad Parameter\n");
		return "";
	}
	int srcLen = strlen(srcStr);

#if defined(_WIN32) || defined(_WIN64)
	int len = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)srcStr, srcLen, NULL, 0);
	unsigned short* strUnicode = new unsigned short[len + 1];
	memset(strUnicode, 0, len * 2 + 2);
	MultiByteToWideChar(CP_UTF8, 0, (LPCCH)srcStr, -1, (LPWSTR)strUnicode, len);
	len = WideCharToMultiByte(CP_ACP, 0, (LPWSTR)strUnicode, -1, NULL, 0, NULL, NULL);
	char* gbkStr = new char[len + 1];
	memset(gbkStr, 0, len + 1);
	WideCharToMultiByte(CP_ACP, 0, (LPWSTR)strUnicode, -1, gbkStr, len, NULL, NULL);
	delete[] strUnicode;
	std::string strGBK = gbkStr;
	delete[] gbkStr;
	return strGBK;
#else //linux
	//首先先将utf8编码转换为unicode编码  
	if (NULL == setlocale(LC_ALL, "zh_CN.utf8")) //设置转换为unicode前的码,当前为utf8编码  
	{
		printf("Bad Parameter\n");
		return "";
	}

	int unicodeLen = mbstowcs(NULL, srcStr, 0); //计算转换后的长度  
	if (unicodeLen <= 0)
	{
		printf("Can not Transfer!!!\n");
		return "";
	}
	wchar_t* unicodeStr = (wchar_t*)calloc(sizeof(wchar_t), unicodeLen + 1);
	mbstowcs(unicodeStr, srcStr, strlen(srcStr)); //将utf8转换为unicode  

	//将unicode编码转换为gbk编码  
	if (NULL == setlocale(LC_ALL, "zh_CN.gbk")) //设置unicode转换后的码,当前为gbk  
	{
		printf("Bad Parameter\n");
		return "";
	}
	int gbkLen = wcstombs(NULL, unicodeStr, 0); //计算转换后的长度  
	if (gbkLen <= 0)
	{
		printf("Can not Transfer!!!\n");
		return "";
	}
	//else if (gbkLen >= maxGbkStrlen) //判断空间是否足够  
	//{
	//	printf("Dst Str memory not enough\n");
	//	return "";
	//}
	char* gbkStr = new char[gbkLen + 1];
	wcstombs(gbkStr, unicodeStr, gbkLen);
	gbkStr[gbkLen] = 0; //添加结束符  
	free(unicodeStr);
	std::string strGBK = gbkStr;
	delete[] gbkStr;
	return strGBK;
#endif

}

static JSValue qjs_getRefCurve(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    json jArgs = engineArrayToJson(ctx, argv, argc);

    if (jArgs.size() == 2) {
		std::string tag = jArgs[0].get<std::string>();
		std::string name = jArgs[1].get<std::string>();

		std::string strDirectory = utf82gbk(tag.c_str());
		std::replace(strDirectory.begin(), strDirectory.end(), '.', '\\');

		strDirectory = g_strScriptFuncConfPath + "\\refCurve\\" + strDirectory;

		std::string strFileName = utf82gbk(name.c_str());
		std::string strPath = strDirectory + "\\" + strFileName;

		yyjson_read_err err_r;
		auto ReadFile_doc = yyjson_read_file(strPath.c_str(), 0, 0, &err_r);

		auto src_doc = yyjson_mut_doc_new(nullptr);
		auto src_root = yyjson_mut_obj(src_doc);
		yyjson_mut_doc_set_root(src_doc, src_root);

		auto src_params = yyjson_mut_obj(src_doc);
		yyjson_mut_obj_add_val(src_doc, src_root, "result", src_params);
		yyjson_mut_obj_add_strcpy(src_doc, src_root, "tag", tag.c_str());
		yyjson_mut_obj_add_strcpy(src_doc, src_root, "name", name.c_str());

		if (ReadFile_doc){
			auto ReadFile_root = yyjson_doc_get_root(ReadFile_doc);
			auto copy_root = yyjson_val_mut_copy(src_doc, ReadFile_root);

			yyjson_mut_obj_add_val(src_doc, src_params, "curve", copy_root);
			yyjson_doc_free(ReadFile_doc);
		}

		JSValue obj = yyVal_to_qjsVal(ctx, src_root);
		yyjson_mut_doc_free(src_doc);

		return obj;
    }

    return JS_UNDEFINED;
}

void initScriptFunc_jhd(JSContext* ctx, void* pDev) {
    JSValue global = JS_GetGlobalObject(ctx);

    JS_SetPropertyStr(ctx, global, "getRefCurve", JS_NewCFunction(ctx, qjs_getRefCurve, "getRefCurve", 2));
}

#endif