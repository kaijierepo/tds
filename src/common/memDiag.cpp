#include "memDiag.h"
#include <DbgHelp.h>
#include <thread>
#include <mutex>



#ifdef _DEBUG
bool g_enableMemDiag = true;
#else
bool g_enableMemDiag = false;
#endif
int g_stackId = 0;

tArray g_memAllocLog;
tArray g_memFreeLog;

//std::mutex mtx;
MEM_ALLOC_STATIS* g_memAlloc[MAX_STACK_COUNT] = { 0 };
MEM_ALLOC_STATIS* g_memFree[MAX_STACK_COUNT] = { 0 };
int g_memAllocSize = 0;
MEM_ALLOC_STATIS* getAllocStatis(void** stack) {
	for (int i = 0; i < g_memAllocSize && i< MAX_STACK_COUNT; i++) {
		if (memcmp(stack, g_memAlloc[i]->stack, 10*sizeof(void*)) == 0) {
			return g_memAlloc[i];
		}
	}
	return nullptr;
}

MEM_ALLOC_STATIS* getFreeStatis(void** stack) {
	for (int i = 0; i < g_memAllocSize && i < MAX_STACK_COUNT; i++) {
		if (memcmp(stack, g_memAlloc[i]->stack, 10 * sizeof(void*)) == 0) {
			return g_memAlloc[i];
		}
	}
	return nullptr;
}

void eraseAllocInfo(void* ptr) {
	//mtx.lock();
	for (int i = 0; i < g_memAllocSize; i++) {
		g_memAlloc[i]->buffList.erase(ptr);
	}
	//mtx.unlock();
}

void addAllocInfo(MEM_ALLOC_STATIS* p) {
	//mtx.lock();
	if (g_memAllocSize == MAX_STACK_COUNT)
		return;

	g_memAlloc[g_memAllocSize] = p;
	g_memAllocSize++;
	//mtx.unlock();
}

#define MAX_STACK_DEPTH 100
void GetStackTrace(void** stackFrames, DWORD maxDepth)
{
	USHORT frameCount = CaptureStackBackTrace(0, maxDepth, stackFrames, NULL);

	//// 初始化符号引擎
	//SymInitialize(GetCurrentProcess(), NULL, TRUE);

	//// 获取符号信息
	//SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR));
	//symbol->MaxNameLen = MAX_SYM_NAME;
	//symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	//for (USHORT i = 0; i < frameCount; i++)
	//{
	//	// 获取函数名
	//	SymFromAddr(GetCurrentProcess(), (DWORD64)(stackFrames[i]), 0, symbol);

	//	// 将函数名追加到堆栈信息中
	//	strcat(stackTrace, symbol->Name);
	//	strcat(stackTrace, "\n");
	//}

	//// 释放资源
	//free(symbol);
	//SymCleanup(GetCurrentProcess());
}

void* operator new(size_t size) {
	void* ptr = malloc(size);
	if (g_enableMemDiag) {
		//void* callStack[10] = { 0 };
		//GetStackTrace(callStack, 10);

		//MEM_ALLOC_STATIS* p = getAllocInfo(callStack);
		//if (p == nullptr) {
		//	p = (MEM_ALLOC_STATIS*)malloc(sizeof(MEM_ALLOC_STATIS));
		//	memset(p, 0, sizeof(MEM_ALLOC_STATIS));
		//	memcpy(p->stack, callStack, 10*sizeof(void*));
		//	p->stackID = g_stackId++;
		//	addAllocInfo(p);
		//}
		//p->buffList.push(ptr);

		MEM_ALLOC_INFO* p = (MEM_ALLOC_INFO*)malloc(sizeof(MEM_ALLOC_INFO));
		memset(p, 0, sizeof(MEM_ALLOC_INFO));
		p->size = size;
		p->ptr = ptr;
		g_memAllocLog.push(p);
	}
	return ptr;
}


void operator delete(void* ptr) {
	if (g_enableMemDiag) {
		g_memFreeLog.push(ptr);
		//eraseAllocInfo(ptr);
	}
	free(ptr);
}

MEM_ALLOC_STATIS::MEM_ALLOC_STATIS() {
	allocCount = 0;
	allocSize = 0;
	stackID = 0;
	memset(stack, 0, 10);
}

void memStatisThread() {
	while (1) {
		Sleep(1000);

	}
}

MemDiag::MemDiag()
{
	std::thread t(memStatisThread);
	t.detach();
}
