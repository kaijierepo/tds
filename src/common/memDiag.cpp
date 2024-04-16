#include "memDiag.h"
#ifdef _WIN32
#include <DbgHelp.h>
#include <thread>
#include <mutex>
#include <map>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "tdb.h"
#include "common.h"

#include "json.hpp"

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

using json = nlohmann::json;
#include "md5.h"

#define STD_FUN_CHECK_IDX 1

MemDiag memDiag;

string g_strSourceDir = "\\sourcecode";
HANDLE g_process = nullptr;

void *newImp(size_t size)
{
	void *ptr = malloc(size);
	if (memDiag.enableMemDiag) {
		void *callStack[10] = { 0 };
		USHORT frameCount = CaptureStackBackTrace(0, 10, callStack, NULL);

		if (memDiag.g_stdFuncList.find(callStack[STD_FUN_CHECK_IDX]) != memDiag.g_stdFuncList.end()) {
			return ptr;
		}

		//Trace then statis mode
		//int i = 0;
		//for (; i < MAX_ALLOC_LOG_COUNT; i++) {
		//	bool expected = false;
		//	//set used to write data
		//	if (!memDiag.g_memAllocInfo[i].used.compare_exchange_weak(expected, true))
		//		continue;
		//	memcpy(memDiag.g_memAllocInfo[i].stack, callStack, 10 * sizeof(void *));
		//	memDiag.g_memAllocInfo[i].size = size;
		//	memDiag.g_memAllocInfo[i].ptr = ptr;

		//	//do not read data before set dataSetted
		//	memDiag.g_memAllocInfo[i].dataSetted = true;
		//	memDiag.g_memTrace++;

		//	break;
		//}

		//Direct Statis mode
		int i = 0;
		for (; i < MAX_STATIS_COUNT; i++) {
			MEM_ALLOC_STATIS& infoBuff = memDiag.g_memStatis[i];
			bool expected = false;
			//statis to existed statis info
			if (infoBuff.used) {
				while (!infoBuff.dataSetted) { //do not do memcmp before data setted. ensure only one statisInfo for one stack
					Sleep(0);
				}

				if (memcmp(infoBuff.stack, callStack, 10 * sizeof(void*)) == 0) {
					infoBuff.allocCount++;
					infoBuff.allocSize += size;
					break;
				}
				else {
					continue;
				}
			}
			//try to occupy add a new statis
			else if(memDiag.g_memStatis[i].used.compare_exchange_strong(expected, true)) {
				memcpy(infoBuff.stack, callStack, 10*sizeof(void*));
				infoBuff.allocCount = 1;
				infoBuff.allocSize = size;
				infoBuff.dataSetted = true;
				memDiag.g_memStatisSize++;
				break;
			}
			//used by another thread ,try another info buff
			else {
				continue;
			}
		}

		memDiag.g_newCount++;
	}
	return ptr;
}

void deleteImp(void *ptr)
{
	if (memDiag.enableMemDiag) {
		void *callStack[10] = { 0 };
		USHORT frameCount = CaptureStackBackTrace(0, 10, callStack, NULL);

		//for (int i = 0; i < memDiag.g_memTrace; i++) {
		//	if (memDiag.g_memAllocInfo[i].used == false)
		//		continue;
		//	if (memDiag.g_memAllocInfo[i].used == true) {
		//		if (memDiag.g_memAllocInfo[i].ptr == ptr) {
		//			memDiag.g_memAllocInfo[i].dataSetted = false;
		//			memDiag.g_memAllocInfo[i].used = false;
		//			memDiag.g_memTrace--;
		//			break;
		//		}
		//	}
		//}


		//Direct Statis mode,do not support size statis ,because ptr is not traced
		int i = 0;
		for (; i < MAX_STATIS_COUNT; i++) {
			MEM_ALLOC_STATIS& infoBuff = memDiag.g_memStatis[i];
			bool expected = false;
			//statis to existed statis info
			if (infoBuff.used) {
				while (!infoBuff.dataSetted){
					Sleep(0);
				}
				if (memcmp(infoBuff.stack, callStack, 10) == 0) {
					infoBuff.allocCount--;
					break;
				}
				else {
					continue;
				}
			}
			else {
				break;
			}
		}

		memDiag.g_deleteCount++;
	}
	free(ptr);

}

void* operator new(size_t size)
{
	return newImp(size);
}
void *operator new[](size_t size)
{
	return newImp(size);
}

void operator delete(void* ptr) 
{
	deleteImp(ptr);
}

void operator delete[](void *ptr)
{
	deleteImp(ptr);
}


MemDiag::MemDiag()
{
	g_memTrace = nullptr;
	g_memStatis = nullptr;
	g_memTraceSize =0;
	g_memStatisSize = 0;
	g_process = nullptr;
	g_newCount = 0;
	g_deleteCount =0;
	m_traceInterval = 0;
}


void MemDiag::getStatisInfo(void* callStack) {

}

void MemDiag::parseStdFromTraceRec(int parseTime) {
	// 初始化符号引擎 与SymCleanup必须在同一个线程当中执行
	SymInitialize(g_process, NULL, TRUE);

	// 获取符号信息
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR));
	symbol->MaxNameLen = MAX_SYM_NAME;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (int i = 0; i < MAX_TRACE_COUNT; i++) {
		bool expected = false;
		if (!g_memTrace[i].used)
			continue;

		void* func = g_memTrace[i].stack[STD_FUN_CHECK_IDX];

		bool bLibFun = true;		//库函数，非用户函数
		if(!g_strSourceDir.empty())
		{
			DWORD displacement;
			IMAGEHLP_LINE line;
			line.SizeOfStruct = sizeof(IMAGEHLP_LINE);
			if (SymGetLineFromAddr(g_process, (DWORD64)(func), &displacement, &line))
			{
				string strPath = line.FileName;
				if (strPath.find(g_strSourceDir) != string::npos)
				{
					bLibFun = false;
				}
			}
		}

		SymFromAddr(g_process, (DWORD64)(func), 0, symbol);
		std::string s = symbol->Name;
		if (bLibFun && (s.find("std::") == std::string::npos || s.find("operator new") != std::string::npos))
		{
			bLibFun = false;
		}

		if (bLibFun) {
			g_memTrace[i].used = false;
			g_memTraceSize--;
			if (g_stdFuncList.find(func) == g_stdFuncList.end()) {
				g_stdFuncList[func] = s;
			}
		}
		else
		{
			s = "";
		}
	}

	// 释放资源
	free(symbol);
	SymCleanup(g_process);
}

void MemDiag::parseStdFromStatisRec() {
	bool lastStatus = enableMemDiag;
	enableMemDiag = false;
	Sleep(50);
	// 初始化符号引擎 与SymCleanup必须在同一个线程当中执行
	SymInitialize(g_process, NULL, TRUE);

	// 获取符号信息
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR));
	symbol->MaxNameLen = MAX_SYM_NAME;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (int i = 0; i < MAX_STATIS_COUNT; i++) {
		MEM_ALLOC_STATIS& statisInfo = g_memStatis[i];
		if (!statisInfo.used)
			continue;
		if (!statisInfo.dataSetted)
			continue;

		void* func = statisInfo.stack[STD_FUN_CHECK_IDX];

		bool bLibFun = true;		//库函数，非用户函数
		if (!g_strSourceDir.empty())
		{
			DWORD displacement;
			IMAGEHLP_LINE line;
			line.SizeOfStruct = sizeof(IMAGEHLP_LINE);
			if (SymGetLineFromAddr(g_process, (DWORD64)(func), &displacement, &line))
			{
				string strPath = line.FileName;
				if (strPath.find(g_strSourceDir) != string::npos)
				{
					bLibFun = false;
				}
			}
		}

		SymFromAddr(g_process, (DWORD64)(func), 0, symbol);
		std::string s = symbol->Name;
		if (bLibFun && (s.find("std::") == std::string::npos || s.find("operator new") != std::string::npos))
		{
			bLibFun = false;
		}

		if (bLibFun) {
			if (g_stdFuncList.find(func) == g_stdFuncList.end()) {
				g_stdFuncList[func] = s;
			}
		}
		else
		{
			s = "";
		}
	}

	//statis buff maybe filled by too many std stack,clear and restart statis
	clearStatis();

	free(symbol);
	SymCleanup(g_process);

	enableMemDiag = lastStatus;
}

void MemDiag::clearStatis() {
	for (int i = 0; i < MAX_STATIS_COUNT; i++) {
		MEM_ALLOC_STATIS& statisInfo = g_memStatis[i];
		statisInfo.used = false;
		statisInfo.dataSetted = false;
		memset(statisInfo.stack, 0, 10 * sizeof(void*));
		statisInfo.allocCount = 0;
		statisInfo.allocSize = 0;
		statisInfo.funcStack.clear();
		statisInfo.id = "";
	}
	g_memStatisSize = 0;
}

void MemDiag::clearTrace() {
	bool lastStatus = enableMemDiag;
	enableMemDiag = false;
	Sleep(50);
	//for (int i = 0; i < MAX_TRACE_COUNT; i++) {
	//	g_memAllocInfo[i].used = false;
	//}
	//g_memTraceSize = 0;

	clearStatis();

	enableMemDiag = lastStatus;
}

std::string formatHex(unsigned long long num) {
	std::stringstream ss;
	ss << "0x" << std::setfill('0') << std::setw(16) << std::hex << num;
	return ss.str();
}

string MemDiag::getStackId(void** stack) {
	string id;
	for (int i = 0; i < 10; i++) {
		string s = formatHex((unsigned long long)stack[i]); \
			id += "-" + s;
	}
	MD5 md5;
	id = md5(id);
	return id;
}


void MemDiag::runMemTrace() {

}


bool MemDiag::handleRpcCall_memDiag(string method,string& sParams, string& rlt, string& err){
	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);

	bool handled = true;
	if (method == "memDiag.log") {
		rpc_memDiag_logTrace(yyv_params, rlt, err);
	}
	else if (method == "memDiag.getStatis") {
		if (g_memStatisSize == 0) {
			rpc_memDiag_getStatisHist(yyv_params, rlt, err);
		}
		else
			rpc_memDiag_getStatis_fromStatis(yyv_params, rlt, err);
	}
	else if (method == "memDiag.getStatisHist") {
		rpc_memDiag_getStatisHist(yyv_params, rlt, err);
	}
	else if (method == "memDiag.getStd") { //get std func from trace rec ,and delete from trace rec
		bool lastStatus = enableMemDiag;
		enableMemDiag = false;
		parseStdFromStatisRec();
		enableMemDiag = lastStatus;

		yyjson_mut_doc* mdoc = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* rlt_mut_root = yyjson_mut_arr(mdoc); 
		for (auto& i : g_stdFuncList) {
			string s = formatHex((unsigned long long)i.first);
			yyjson_mut_val* func = yyjson_mut_obj(mdoc);
			yyjson_mut_val* key = yyjson_mut_strcpy(mdoc, s.c_str());
			yyjson_mut_val* val = yyjson_mut_strcpy(mdoc, i.second.c_str());
			yyjson_mut_obj_put(func, key, val);
			yyjson_mut_arr_append(rlt_mut_root, func);
		}
		size_t len;
		rlt = yyjson_mut_val_write(rlt_mut_root, YYJSON_WRITE_NOFLAG, &len);
		yyjson_mut_doc_free(mdoc);
	}
	else if (method == "memDiag.alloc") {
		yyjson_val* yyv_size = yyjson_obj_get(yyv_params, "size");
		int size = yyjson_get_int(yyv_size);
		yyjson_val* yyv_count = yyjson_obj_get(yyv_params, "count");
		int count = yyjson_get_int(yyv_count);
		if (size == 0)
			size = 100;
		if (count == 0)
			count = 1;

		allocMemTest(size,count);
		rlt = "\"ok\"";
	}
	else if (method == "memDiag.startTrace") {
		yyjson_val* yyv_interval = yyjson_obj_get(yyv_params, "interval");
		m_traceInterval = yyjson_get_int(yyv_params);
		if(g_memTrace == nullptr)
			g_memTrace = new MEM_ALLOC_INFO[MAX_TRACE_COUNT];
		if(g_memStatis == nullptr)
			g_memStatis = new MEM_ALLOC_STATIS[MAX_STATIS_COUNT];
		if(g_process == nullptr)
			g_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());
		enableMemDiag = true;
		rlt = "\"ok\"";
	}
	else if (method == "memDiag.stopTrace") {
		enableMemDiag = false;
		rlt = "\"ok\"";
	}
	else if (method == "memDiag.clearTrace") {
		clearTrace();
		rlt = "\"ok\"";
	}
	else {
		handled = false;
	}

	yyjson_doc_free(doc);
	return handled;
}

void MemDiag::allocMemTest(int size,int count) {
	for (int i = 0; i < count; i++) {
		void* p = new char[size];
		m_allocTest.push_back(p);
	}
}

void MemDiag::rpc_memDiag_logTrace(yyjson_val* params, string& rlt, string& err){
	MapSnapshot mapSnapshot;
	rpc_memDiag_getStatis_fromStatis(params, rlt, err);

	yyjson_doc* doc = yyjson_read(rlt.c_str(), rlt.length(), 0);
	yyjson_val* yyv_statisList = yyjson_doc_get_root(doc);

	size_t idx = 0;
	size_t max = 0;
	yyjson_val* item;
	yyjson_arr_foreach(yyv_statisList, idx, max, item) {
		yyjson_mut_doc* mdoc = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* de = yyjson_mut_obj(mdoc);

		yyjson_mut_val* key = yyjson_mut_strcpy(mdoc, "db");
		yyjson_mut_val* val = yyjson_mut_strcpy(mdoc, "memTrace");
		yyjson_mut_obj_put(de, key, val);

		key = yyjson_mut_strcpy(mdoc, "tag");
		string id = yyjson_get_str(yyjson_obj_get(item, "id"));
		val = yyjson_mut_strcpy(mdoc, id.c_str());
		yyjson_mut_obj_put(de, key, val);

		key = yyjson_mut_strcpy(mdoc, "val");
		val = yyjson_val_mut_copy(mdoc, item);
		yyjson_mut_obj_put(de, key, val);

		size_t len;
		string sParams = yyjson_mut_val_write(de, YYJSON_WRITE_NOFLAG, &len);
		string dbRlt, dbErr, dbQi;
		db.rpc_db_insert(sParams,dbRlt,dbErr, dbQi);
	}

	rlt = "\"ok\"";
}

bool readFile(string path, string& data)
{
	FILE* fp = nullptr;
	wstring wPath = DB_STR::utf8_to_utf16(path);
#ifdef _WIN32
	_wfopen_s(&fp, wPath.c_str(), L"rb");
#else
	fp = fopen(path.c_str(), "rb");
#endif
	if (fp)
	{
		fseek(fp, 0, SEEK_END);
		long len = ftell(fp);
		char* pdata = new char[len + 2];
		memset(pdata, 0, len + 2);
		fseek(fp, 0, SEEK_SET);
		fread(pdata, 1, len, fp);
		data = pdata;
		fclose(fp);
		delete[] pdata;
		return true;
	}
	return false;
}

void MemDiag::rpc_memDiag_getStatisHist(yyjson_val* params, string& rlt, string& err) {
	string s;
	readFile("D:\\HuiJing\\jhd20\\out\\memStatis.json",s);
	json j = json::parse(s);
	rlt = j.dump();
}


void MemDiag::rpc_memDiag_getStatis_fromStatis(yyjson_val* params, string& rlt, string& err) {
	// 初始化符号引擎 与SymCleanup必须在同一个线程当中执行
	SymInitialize(g_process, NULL, TRUE);

	// 获取符号信息
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR));
	symbol->MaxNameLen = MAX_SYM_NAME;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	json jList = json::array();
	for (int i = 0; i < MAX_STATIS_COUNT; i++) {
		MEM_ALLOC_STATIS& statisInfo = g_memStatis[i];
		if (statisInfo.used == false)
			continue;
		if (statisInfo.dataSetted == false)
			continue;

		
		if (statisInfo.id == "") {
			statisInfo.id = getStackId(statisInfo.stack);
		}
		string id = g_memStatis[i].id;

		if (statisInfo.funcStack.size() == 0) {
			for (int j = 0; j < 10; j++) {
				void* func = statisInfo.stack[j];
				SymFromAddr(g_process, (DWORD64)(func), 0, symbol);
				DWORD displacement;
				IMAGEHLP_LINE line;
				line.SizeOfStruct = sizeof(IMAGEHLP_LINE);

				std::string s = symbol->Name;
				if (SymGetLineFromAddr(g_process, (DWORD64)(func), &displacement, &line))
				{
					s += str::format("(%s:%d)", line.FileName, line.LineNumber);
				}
				statisInfo.funcStack.push_back(s);
			}
		}

		json j;
		j["id"] = id;
		json funcStack = json::array();
		for (int i = 0; i < statisInfo.funcStack.size(); i++) {
			funcStack.push_back(statisInfo.funcStack[i]);
		}
		j["stack"] = funcStack;
		j["allocSize"] = statisInfo.allocSize;
		j["allocCount"] = statisInfo.allocCount.load();
		jList.push_back(j);
	}
	rlt = jList.dump();
	// 释放资源
	free(symbol);
	SymCleanup(g_process);
	/*yyjson_mut_doc* mdoc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* rlt_mut_root = yyjson_mut_arr(mdoc);
	for (auto& iter : mapStatis) {
		yyjson_mut_val* oneStatis = yyjson_mut_obj(mdoc);
		yyjson_mut_val* key = yyjson_mut_strcpy(mdoc, "id");
		yyjson_mut_val* val = yyjson_mut_strcpy(mdoc, iter.first.c_str());
		yyjson_mut_obj_put(oneStatis, key, val);

		key = yyjson_mut_strcpy(mdoc, "stack");
		val = yyjson_mut_arr(mdoc);
		for (int i = 0; i < iter.second->funcStack.size(); i++) {
			yyjson_mut_val* func = yyjson_mut_strcpy(mdoc,iter.second->funcStack[i].c_str());
			yyjson_mut_arr_append(val, func);
		}
		yyjson_mut_obj_put(oneStatis, key, val);

		key = yyjson_mut_strcpy(mdoc, "allocCount");
		val = yyjson_mut_int(mdoc, iter.second->allocCount);
		yyjson_mut_obj_put(oneStatis, key, val);

		key = yyjson_mut_strcpy(mdoc, "allocSize");
		val = yyjson_mut_int(mdoc, iter.second->allocSize);
		yyjson_mut_obj_put(oneStatis, key, val);

		yyjson_mut_arr_append(rlt_mut_root, oneStatis);
	}
	size_t len;
	string s = yyjson_mut_val_write(rlt_mut_root, YYJSON_WRITE_NOFLAG, &len);
	rlt = s;
	yyjson_mut_doc_free(mdoc);*/
}


void MemDiag::rpc_memDiag_getStatis_fromTrace(yyjson_val* params, string& rlt, string& err, MapSnapshot *mapSnapshot) {
	bool lastStatus = enableMemDiag;
	enableMemDiag = false;
	Sleep(50);
	// 初始化符号引擎 与SymCleanup必须在同一个线程当中执行
	SymInitialize(g_process, NULL, TRUE);

	// 获取符号信息
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR));
	symbol->MaxNameLen = MAX_SYM_NAME;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	map<string, std::shared_ptr<MEM_ALLOC_STATIS>> mapStatis;
	for (int i = 0; i < MAX_TRACE_COUNT; i++) {
		if (g_memTrace[i].used == false)
			continue;
		if (g_memTrace[i].dataSetted == false)
			continue;

		string id = getStackId(g_memTrace[i].stack);
		auto iter = mapStatis.find(id);
		if (iter != mapStatis.end()) {
			iter->second->allocSize += g_memTrace[i].size;
			iter->second->allocCount++;
		}
		else {
			auto p = std::make_shared<MEM_ALLOC_STATIS>();
			p->allocSize = g_memTrace[i].size;
			p->allocCount = 1;
			for (int j = 0; j < 10; j++) {
				void* func = g_memTrace[i].stack[j];
				SymFromAddr(g_process, (DWORD64)(func), 0, symbol);
				DWORD displacement;
				IMAGEHLP_LINE line;
				line.SizeOfStruct = sizeof(IMAGEHLP_LINE);

				std::string s = symbol->Name;
				if (SymGetLineFromAddr(g_process, (DWORD64)(func), &displacement, &line))
				{
					s += str::format("(%s:%d)", line.FileName, line.LineNumber);
				}
				

				p->funcStack.push_back(s);
			}
			mapStatis[id] = p;
		}
	}
	// 释放资源
	free(symbol);
	SymCleanup(g_process);
	/*yyjson_mut_doc* mdoc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* rlt_mut_root = yyjson_mut_arr(mdoc);
	for (auto& iter : mapStatis) {
		yyjson_mut_val* oneStatis = yyjson_mut_obj(mdoc);
		yyjson_mut_val* key = yyjson_mut_strcpy(mdoc, "id");
		yyjson_mut_val* val = yyjson_mut_strcpy(mdoc, iter.first.c_str());
		yyjson_mut_obj_put(oneStatis, key, val);

		key = yyjson_mut_strcpy(mdoc, "stack");
		val = yyjson_mut_arr(mdoc);
		for (int i = 0; i < iter.second->funcStack.size(); i++) {
			yyjson_mut_val* func = yyjson_mut_strcpy(mdoc,iter.second->funcStack[i].c_str());
			yyjson_mut_arr_append(val, func);
		}
		yyjson_mut_obj_put(oneStatis, key, val);

		key = yyjson_mut_strcpy(mdoc, "allocCount");
		val = yyjson_mut_int(mdoc, iter.second->allocCount);
		yyjson_mut_obj_put(oneStatis, key, val);

		key = yyjson_mut_strcpy(mdoc, "allocSize");
		val = yyjson_mut_int(mdoc, iter.second->allocSize);
		yyjson_mut_obj_put(oneStatis, key, val);

		yyjson_mut_arr_append(rlt_mut_root, oneStatis);
	}
	size_t len;
	string s = yyjson_mut_val_write(rlt_mut_root, YYJSON_WRITE_NOFLAG, &len);
	rlt = s;
	yyjson_mut_doc_free(mdoc);*/
	json jList;
	for (auto& iter : mapStatis) {
		json j;
		j["id"] = iter.first;
		json funcStack = json::array();
		for (int i = 0; i < iter.second->funcStack.size(); i++) {
			funcStack.push_back(iter.second->funcStack[i]);
		}
		j["stack"] = funcStack;
		j["allocSize"] = iter.second->allocSize;
		j["allocCount"] = iter.second->allocCount.load();
		json dataSize = json::array();
		json dataCount = json::array();

		int lastSize = 0;
		int lastCount = 0;
		for (auto &it : m_lstSnapshotColl)
		{
			auto f = it.find(iter.first);
			if (f != it.end())
			{
				dataSize.push_back(int(f->second.allocSize - lastSize));
				dataCount.push_back(int(f->second.allocCount - lastCount));

				lastSize = f->second.allocSize;
				lastCount = f->second.allocCount;
			}
			else
			{
				dataSize.push_back(0);
				dataCount.push_back(0);
				lastSize = 0;
				lastCount = 0;
			}
		}
		//dataSize.push_back(int(iter.second->allocSize - lastSize));
		//dataCount.push_back(int(iter.second->allocCount - lastCount));

		j["dataSize"] = dataSize;
		j["dataCount"] = dataCount;

		if(mapSnapshot)
			(*mapSnapshot)[iter.first] = MEM_SNAPSHOT_ITEM{ iter.second->allocCount, iter.second->allocSize };
		jList.push_back(j);
	}
	rlt = jList.dump();
	enableMemDiag = lastStatus;
}
#endif
