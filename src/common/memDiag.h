#ifdef _WIN32
#pragma once
#define STACK_INFO_SIZE 4096
#include "Windows.h"
#include <atomic>
#include <string>
#include <map>
#include "yyjson.h"
#include <vector>
#include <list>

using namespace std;

struct MEM_SNAPSHOT_ITEM {
	size_t allocCount;
	size_t allocSize;
};
typedef std::map<std::string, MEM_SNAPSHOT_ITEM> MapSnapshot;
typedef std::list<MapSnapshot>	ListSnapshotCollection;

struct MEM_ALLOC_INFO {
	std::atomic<bool> used;
	std::atomic<bool> dataSetted;
	std::atomic_bool parsed;		
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
	std::atomic<bool> used;
	std::atomic<bool> dataSetted;
	void* stack[10];
	std::atomic<size_t> allocCount;
	size_t allocSize;
	vector<string> funcStack;
	MEM_ALLOC_STATIS* next;
	string id;
	bool valid;

	MEM_ALLOC_STATIS() {
		memset(stack, 0, 10);
		next = nullptr;
		allocCount = 0;
		allocSize = 0;
		valid = true;
	};
};

#define MAX_STATIS_COUNT 200
#define MAX_TRACE_COUNT 5000
#define MAX_SYS_FUNC_COUNT 200

#include <atomic>
#include <string>

//lock free list,deal with new/delete concurrent
class MemStatisList {
private:
	std::atomic<MEM_ALLOC_STATIS*> head;

public:
	MemStatisList() : head(nullptr) {}

	void push(MEM_ALLOC_STATIS* p) {
		MEM_ALLOC_STATIS* currentHead = head.load();
		do {
			p->next = currentHead;
		} while (!head.compare_exchange_weak(currentHead, p));
	}

	MEM_ALLOC_STATIS* get(void* callStack) {
		MEM_ALLOC_STATIS* current = head.load();
		while (current != nullptr) {
			if (memcmp(current->stack,callStack,10)){
				return current;
			}
			current = current->next;
		}
		return nullptr;
	}
};


class MemDiag {
public:
	MemDiag();

	bool handleRpcCall_memDiag(string method, string& sParams, string& rlt, string& err);

	void rpc_memDiag_logTrace(yyjson_val* params, string& rlt, string& err);
	void rpc_memDiag_getStatis_fromTrace(yyjson_val* params, string& rlt, string& err, MapSnapshot *mapSnapshot = NULL);
	void rpc_memDiag_getStatisHist(yyjson_val* params, string& rlt, string& err);
	void rpc_memDiag_getStatis_fromStatis(yyjson_val* params, string& rlt, string& err);
	std::atomic<bool> enableMemDiag;
	unsigned int m_traceInterval;

	void allocMemTest(int size, int count);
	vector<void*> m_allocTest;
	void getStatisInfo(void* callStack);
	void parseStdFromTraceRec(int parseTime = 5);
	void parseStdFromStatisRec();
	void clearStatis();
	void clearTrace();
	void runMemTrace();
	string getStackId(void** stack);

	std::atomic<bool> g_enableStdCallFitler;
	std::map<void*, std::string> g_stdFuncList;
	ListSnapshotCollection m_lstSnapshotColl;			

	//trace each alloc with ptr
	MEM_ALLOC_INFO* g_memTrace;
	std::atomic<int> g_memTraceSize;
	//statis alloc by stack
	MEM_ALLOC_STATIS* g_memStatis;
	std::atomic<int> g_memStatisSize;


	MemStatisList g_memStatisList;

	std::atomic<int> g_newCount;
	std::atomic<int> g_deleteCount;
	std::atomic_bool g_bAutoRegStdFun = false;	
};

extern MemDiag memDiag;

extern void *operator new(size_t size);
extern void *operator new[](size_t size);
extern void operator delete(void *ptr);
extern void operator delete[](void *ptr);
#endif
