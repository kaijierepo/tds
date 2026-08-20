#pragma once

//=============================================================================
// winCompat.h  跨平台 Windows 兼容层
//
// 目的：让 dcqk(道岔缺口站机) / tb3386(315协议) 相关代码在
//       非 Windows 平台（麒麟 Linux x86_64/arm64/armv7）也能编译。
//
// 使用方式：Windows 下该头文件为空操作（windows.h / tchar.h / inaddr.h
//           已提供所需类型与函数）；非 Windows 下提供与 MSVC 行为兼容的
//           最小实现，仅补齐本次移植涉及的符号。
//=============================================================================

#ifdef _WIN32

// ---- Windows：直接使用系统头文件 ----
// cstdio/cstring 提供 sprintf_s/memcpy_s；tchar.h 提供 TCHAR/_T/_stprintf_s；
// windows.h 提供 BYTE/WORD/DWORD/BOOL/CRITICAL_SECTION 等，并保证
// WINAPI_FAMILY_PARTITION 等宏在 inaddr.h 之前已生效；inaddr.h 提供 IN_ADDR。
#include <cstdio>
#include <cstring>
#include <windows.h>
#include <tchar.h>
#include <inaddr.h>

#else // ---- 非 Windows（Linux 等）----

#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <mutex>
#include <chrono>

//-----------------------------------------------------------------------
// 基础类型（与 Windows SDK 对齐）
// 注意：Windows LLP64 下 DWORD/ULONG 为 32 位，而 Linux 下 unsigned long
//       是 64 位，因此 DWORD 必须用 uint32_t，不能用 unsigned long。
//-----------------------------------------------------------------------
#ifndef BYTE
typedef unsigned char BYTE;
#endif
#ifndef WORD
typedef unsigned short WORD;
#endif
#ifndef DWORD
typedef uint32_t DWORD;
#endif
#ifndef CHAR
typedef char CHAR;
#endif
#ifndef SHORT
typedef short SHORT;
#endif
#ifndef UCHAR
typedef unsigned char UCHAR;
#endif
#ifndef USHORT
typedef unsigned short USHORT;
#endif
#ifndef BOOL
typedef int BOOL;
#endif
#ifndef INT
typedef int INT;
#endif
#ifndef UINT
typedef unsigned int UINT;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// 指针类型
#ifndef LPVOID
#define LPVOID void*
#endif
#ifndef LPBYTE
#define LPBYTE BYTE*
#endif
#ifndef LPDWORD
#define LPDWORD DWORD*
#endif
#ifndef LPWORD
#define LPWORD WORD*
#endif
#ifndef LPSTR
#define LPSTR char*
#endif
#ifndef LPCSTR
#define LPCSTR const char*
#endif
#ifndef LPCTSTR
#define LPCTSTR const char*
#endif
#ifndef LPTSTR
#define LPTSTR char*
#endif

//-----------------------------------------------------------------------
// TCHAR 相关（本工程 _UNICODE 未定义，均为窄字符）
//-----------------------------------------------------------------------
#ifndef TCHAR
#define TCHAR char
#endif
#ifndef _T
#define _T(x) x
#endif

//-----------------------------------------------------------------------
// CRITICAL_SECTION：递归互斥锁，语义与 Windows 一致
//（同一线程可重复进入，Enter/Leave 计数配对）
//-----------------------------------------------------------------------
#ifndef LPCRITICAL_SECTION
#define LPCRITICAL_SECTION CRITICAL_SECTION*
#endif

#ifndef CRITICAL_SECTION
class CRITICAL_SECTION
{
public:
	std::recursive_mutex m;

	CRITICAL_SECTION() = default;
	CRITICAL_SECTION(const CRITICAL_SECTION&) = delete;
	CRITICAL_SECTION& operator=(const CRITICAL_SECTION&) = delete;
};

static inline void InitializeCriticalSection(LPCRITICAL_SECTION lp) { (void)lp; }
static inline void EnterCriticalSection(LPCRITICAL_SECTION lp) { if (lp) lp->m.lock(); }
static inline void LeaveCriticalSection(LPCRITICAL_SECTION lp) { if (lp) lp->m.unlock(); }
static inline void DeleteCriticalSection(LPCRITICAL_SECTION lp) { (void)lp; }
#endif

//-----------------------------------------------------------------------
// 内存操作
//-----------------------------------------------------------------------
#ifndef ZeroMemory
#define ZeroMemory(Dest, Length) memset((Dest), 0, (Length))
#endif
#ifndef CopyMemory
#define CopyMemory(Dest, Src, Length) memcpy((Dest), (Src), (Length))
#endif

static inline int memcpy_s(void* dest, size_t destsz, const void* src, size_t count)
{
	if (dest == nullptr || src == nullptr)
		return -1;
	if (count > destsz)
		return -2;
	memcpy(dest, src, count);
	return 0;
}

//-----------------------------------------------------------------------
// sprintf_s：提供与 MSVC 相同的两个重载
//  - 数组模板重载：sprintf_s(buf, "...", ...)         buf 为 char[N]
//  - 指针重载：    sprintf_s(buf, size, "...", ...)
//-----------------------------------------------------------------------
static inline int sprintf_s(char* buffer, size_t sizeOfBuffer, const char* format, ...)
{
	if (buffer == nullptr || sizeOfBuffer == 0)
		return -1;
	va_list args;
	va_start(args, format);
	int n = vsnprintf(buffer, sizeOfBuffer, format, args);
	va_end(args);
	return n;
}

template<size_t N>
static inline int sprintf_s(char(&buffer)[N], const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int n = vsnprintf(buffer, N, format, args);
	va_end(args);
	return n;
}

#ifndef _stprintf_s
#define _stprintf_s sprintf_s
#endif

//-----------------------------------------------------------------------
// 调试输出
//-----------------------------------------------------------------------
static inline void OutputDebugString(LPCSTR s)
{
	if (s)
		fprintf(stderr, "%s\n", s);
}

//-----------------------------------------------------------------------
// GetTickCount：进程启动以来的毫秒数（用于耗时判断）
//-----------------------------------------------------------------------
static inline DWORD GetTickCount()
{
	using namespace std::chrono;
	return (DWORD)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

//-----------------------------------------------------------------------
// IN_ADDR：Windows inaddr.h 的结构（S_un.S_addr / S_un.S_un_b.s_b1..4）
// 与 Linux 的 struct in_addr（仅 s_addr）互不干扰。
//-----------------------------------------------------------------------
struct IN_ADDR
{
	union {
		struct { BYTE s_b1; BYTE s_b2; BYTE s_b3; BYTE s_b4; } S_un_b;
		struct { WORD s_w1; WORD s_w2; } S_un_w;
		DWORD S_addr;
	} S_un;
};

#endif // _WIN32
