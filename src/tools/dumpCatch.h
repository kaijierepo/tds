#pragma once

#ifdef WINDOWS
class CDumpCatch
{
public:
	CDumpCatch();
	~CDumpCatch();


	static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI TempSetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
	static BOOL ReleaseDumpFile(const std::string& strPath, EXCEPTION_POINTERS* pException);
	static void createDump(_EXCEPTION_POINTERS* pException);
	static LONG WINAPI UnhandledExceptionFilterEx(struct _EXCEPTION_POINTERS* pException);
	static void MyPureCallHandler(void);
	static void MyInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved);

	BOOL AddExceptionHandle();
	BOOL RemoveExceptionHandle();
	BOOL PreventSetUnhandledExceptionFilter();
	void SetInvalidHandle();
	void UnSetInvalidHandle();

private:
	LPTOP_LEVEL_EXCEPTION_FILTER m_preFilter;
	_invalid_parameter_handler m_preIph;
	_purecall_handler m_prePch;
};

extern CDumpCatch dumpCatch;

#endif