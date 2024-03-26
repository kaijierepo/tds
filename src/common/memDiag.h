#pragma once
#define STACK_INFO_SIZE 4096
#include "Windows.h"
#include <atomic>
#include <string>
#include <map>
#include "yyjson.h"
#include <vector>

using namespace std;

struct MEM_ALLOC_INFO {
	std::atomic<bool> used;
	std::atomic<bool> dataSetted;
    void* stack[10];
    void* ptr;
    size_t size;
	MEM_ALLOC_INFO() {
		used = false;
		dataSetted = false;
		memset(stack, 0, 10);
		ptr = 0;
		size = 0;
	};
};


struct MEM_ALLOC_STATIS {
	void* stack[10];
	size_t allocCount;
	size_t allocSize;
	vector<string> funcStack;
	MEM_ALLOC_STATIS() {
		memset(stack, 0, 10);
	};
};

#define MAX_STACK_COUNT 1000
#define MAX_ALLOC_LOG_COUNT 50000
#define MAX_SYS_FUNC_COUNT 200

class MemDiag {
public:
	MemDiag();

	void run();

	bool handleRpcCall_memDiag(string method, string& sParams, string& rlt, string& err);

	void rpc_memDiag_logTrace(yyjson_val* params, string& rlt, string& err);
	void rpc_memDiag_getStatis(yyjson_val* params, string& rlt, string& err);

	std::atomic<bool> enableMemDiag;

	void allocMemTest();
	vector<void*> m_allocTest;
	void initMemDiag();
	void parseStd(int parseTime = 5);
	void clearTrace();
	void runMemTrace();
	string getStackId(void** stack);

	std::atomic<bool> g_enableStdCallFitler;
	std::map<void*, std::string> g_stdFuncList;
	MEM_ALLOC_INFO g_memAllocInfo[MAX_ALLOC_LOG_COUNT];
	std::atomic<int> g_memAllocLogSize;
	MEM_ALLOC_STATIS g_memAlloc[MAX_STACK_COUNT];
	std::atomic<int> g_memAllocSize;
	std::atomic<int> g_newCount;
	std::atomic<int> g_deleteCount;
};

extern MemDiag memDiag;

extern void* operator new(size_t size);
extern void operator delete(void* ptr);

