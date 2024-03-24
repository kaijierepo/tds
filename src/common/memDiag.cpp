#include "memDiag.h"
#include <DbgHelp.h>
#include <thread>
#include <mutex>
#include <map>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "tdb.h"

#include "json.hpp"
using json = nlohmann::json;


MemDiag memDiag;

HANDLE g_process = nullptr;

void* operator new(size_t size) {
	void* ptr = malloc(size);
	if (memDiag.enableMemDiag) {
		void* callStack[10] = { 0 };
		USHORT frameCount = CaptureStackBackTrace(0, 10, callStack, NULL);

		if (memDiag.g_stdFuncList.find(callStack[1]) != memDiag.g_stdFuncList.end()) {
			return ptr;
		}

		for (int i = 0; i < MAX_ALLOC_LOG_COUNT; i++) {
			bool expected = false;
			//set used to write data
			if (!memDiag.g_memAllocInfo[i].used.compare_exchange_weak(expected, true))
				continue;
			memcpy(memDiag.g_memAllocInfo[i].stack, callStack, 10 * sizeof(void*));
			memDiag.g_memAllocInfo[i].size = size;
			memDiag.g_memAllocInfo[i].ptr = ptr;

			//do not read data before set dataSetted
			memDiag.g_memAllocInfo[i].dataSetted = true;
			memDiag.g_memAllocLogSize++;
			
			break;
		}
		memDiag.g_newCount++;
	}
	return ptr;
}


void operator delete(void* ptr) {
	if (memDiag.enableMemDiag) {
		void* callStack[10] = { 0 };
		USHORT frameCount = CaptureStackBackTrace(0, 10, callStack, NULL);

		for (int i = 0; i < memDiag.g_memAllocLogSize; i++) {
			if (memDiag.g_memAllocInfo[i].used == false)
				continue;
			if (memDiag.g_memAllocInfo[i].used == true) {
				if (memDiag.g_memAllocInfo[i].ptr == ptr) {
					memDiag.g_memAllocInfo[i].dataSetted = false;
					memDiag.g_memAllocInfo[i].used = false;
					memDiag.g_memAllocLogSize--;
					break;
				}
			}
		}
		memDiag.g_deleteCount++;
	}
	free(ptr);
}

MemDiag::MemDiag()
{
	g_memAllocLogSize = 0;
	g_memAllocSize =0;
	g_process = nullptr;
	g_newCount = 0;
	g_deleteCount =0;
}

void MemDiag::initMemDiag() {
	g_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());
}


void MemDiag::parseStd(int parseTime) {
	clearTrace();

	g_enableStdCallFitler = false;
	enableMemDiag = true;
	Sleep(parseTime *1000);
	enableMemDiag = false;
	g_enableStdCallFitler = true;


	// 初始化符号引擎 与SymCleanup必须在同一个线程当中执行
	SymInitialize(g_process, NULL, TRUE);

	// 获取符号信息
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR));
	symbol->MaxNameLen = MAX_SYM_NAME;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (int i = 0; i < MAX_ALLOC_LOG_COUNT; i++) {
		bool expected = false;
		if (!g_memAllocInfo[i].used)
			continue;

		void* func = g_memAllocInfo[i].stack[1];
		SymFromAddr(g_process, (DWORD64)(func), 0, symbol);
		std::string s = symbol->Name;
		if (s.find("std") != std::string::npos) {
			if (g_stdFuncList.find(func) == g_stdFuncList.end()) {
				g_stdFuncList[func] = s;
			}
		}
	}

	// 释放资源
	free(symbol);
	SymCleanup(g_process);

	clearTrace();
}

void MemDiag::clearTrace() {
	enableMemDiag = false;
	Sleep(50);
	for (int i = 0; i < MAX_ALLOC_LOG_COUNT; i++) {
		g_memAllocInfo[i].used = false;
	}
	g_memAllocLogSize = 0;
	enableMemDiag = true;
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

	return id;
}


void thread_runMemTrace() {
	//ignore mem alloc when startup for 10 seconds
	Sleep(10000);

	//trace 10 seconds alloc ,check new calls in std lib
	memDiag.parseStd(10);

	//start trace
	memDiag.enableMemDiag = true;
}

void MemDiag::runMemTrace() {
	thread t(thread_runMemTrace);
	t.detach();
}


bool MemDiag::handleRpcCall_memDiag(string method,string& sParams, string& rlt, string& err){
	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);

	bool handled = true;
	if (method == "memDiag.log") {
		rpc_memDiag_logTrace(yyv_params, rlt, err);
	}
	else if (method == "memDiag.getStatis") {
		rpc_memDiag_getStatis(yyv_params, rlt, err);
	}
	else if (method == "memDiag.getStd") {
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
		allocMemTest();
		rlt = "\"ok\"";
	}
	else if (method == "memDiag.enable") {
		enableMemDiag = true;
		rlt = "\"ok\"";
	}
	else if (method == "memDiag.checkStd") {
		parseStd();
		rlt = "\"ok\"";
	}
	else if (method == "memDiag.clear") {
		clearTrace();
		rlt = "\"ok\"";
	}
	else if (method == "memDiag.disable") {
		enableMemDiag = false;
		rlt = "\"ok\"";
	}
	else {
		handled = false;
	}

	yyjson_doc_free(doc);
	return handled;
}

void MemDiag::allocMemTest() {
	void* p = new char[1 * 1024 * 1024];
	m_allocTest.push_back(p);
}

void MemDiag::rpc_memDiag_logTrace(yyjson_val* params, string& rlt, string& err){
	rpc_memDiag_getStatis(params, rlt, err);
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

void MemDiag::rpc_memDiag_getStatis(yyjson_val* params, string& rlt, string& err) {
	enableMemDiag = false;
	Sleep(50);
	// 初始化符号引擎 与SymCleanup必须在同一个线程当中执行
	SymInitialize(g_process, NULL, TRUE);

	// 获取符号信息
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR));
	symbol->MaxNameLen = MAX_SYM_NAME;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	map<string, MEM_ALLOC_STATIS*> mapStatis;
	for (int i = 0; i < MAX_ALLOC_LOG_COUNT; i++) {
		if (g_memAllocInfo[i].used == false)
			continue;
		if (g_memAllocInfo[i].dataSetted == false)
			continue;

		string id = getStackId(g_memAllocInfo[i].stack);
		map<string, MEM_ALLOC_STATIS*>::iterator iter = mapStatis.find(id);
		if (iter != mapStatis.end()) {
			iter->second->allocSize += g_memAllocInfo[i].size;
			iter->second->allocCount++;
		}
		else {
			MEM_ALLOC_STATIS* p = new MEM_ALLOC_STATIS();
			p->allocSize = g_memAllocInfo[i].size;
			p->allocCount = 1;
			for (int j = 0; j < 10; j++) {
				void* func = g_memAllocInfo[i].stack[j];
				SymFromAddr(g_process, (DWORD64)(func), 0, symbol);
				std::string s = symbol->Name;
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
		j["allocCount"] = iter.second->allocCount;
		jList.push_back(j);
	}
	rlt = jList.dump();
	enableMemDiag = true;
}

