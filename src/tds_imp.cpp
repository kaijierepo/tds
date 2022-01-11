/*
  TDS for iot version 1.0.0
  https://gitee.com/liangtuSoft/tds.git

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2020-present Tao Lu 卢涛

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "pch.h"
#include "tds_imp.h"
#include "rpcHandler.h"
#include "logger.h"
#include "prj.h"
#include "ds.h"
#include "ioSrv.h"
#include "xiaot/xiaot.h"
#include "io_server/ioDev.h"
#include "conf.h"
#include "mp.h"
#include "videoCodec.h"
#include "wke.h"
#include "res/resource.h"
#include "ioDev_genicam.h"
#include "streamServer.h"
#include "alarm_server/as.h"
#include "logServer/logServer.h"
#include "xiaot/scriptHost.h"
#include "version.h"
#include "data_server/db.h"
#include "tools/tdsWatchDog.h"

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#include <Shlwapi.h>
#pragma comment(lib,"shlwapi.lib")

CDumpCatch g_dumpCatch;//全局虽然未使用但不能删除

string InterfaceEncoding = "utf8";

string version = "v1.0";

TDS_imp tdsImp; //tds instance;
iTDS* tds = &tdsImp;

#include <stdio.h>
#include <io.h>
#include <FCNTL.H>
void createConsole()
{
	BOOL bRet = AllocConsole(); //打开控制台窗口以显示调试信息
	SetConsoleTitleA("TDS Console"); //设置标题
	freopen("CONOUT$", "w+t", stdout);
	freopen("CONIN$", "r+t", stdin);
}


wkeWebView m_hUI;
int w;
int h;


void showDevToolCallback(wkeWebView webView, void* param)
{

}

// 消息处理函数的实现
LRESULT CALLBACK WindowProc_tdsUI(
	_In_  HWND hwnd,
	_In_  UINT uMsg,
	_In_  WPARAM wParam,
	_In_  LPARAM lParam
)
{
	if (uMsg == WM_SIZE)
	{
		 w = LOWORD(lParam);
		 h = HIWORD(lParam);
		if(m_hUI)
		wkeResize(m_hUI, w, h);
	}
	else if (uMsg == WM_SHOWWINDOW)
	{
		
	}
	else if (uMsg == WM_CLOSE)
	{
		exit(0);
	}
	else if(uMsg == WM_KEYDOWN)
	{
		switch (wParam)
		{
			case VK_F12:
			string path = fs::appPath() + "\\front_end\\inspector.html";
			if (!fs::fileExist(path))
			{
				::MessageBox(NULL, charCodec::utf8toAnsi("没有找到./front_end/inspector.html,请将调试工具包放在程序运行目录下").c_str(), NULL, NULL);
			}
			wkeShowDevtools(m_hUI, charCodec::utf8toUtf16(path).c_str(), showDevToolCallback, NULL);
			break;
		}
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}



void createMiniblinkWnd()
{
	//注册窗口类
	HINSTANCE hInstance;
	hInstance = GetModuleHandle(NULL);
	WNDCLASS tdsUIWnd;
	tdsUIWnd.cbClsExtra = 0;
	tdsUIWnd.cbWndExtra = 0;
	tdsUIWnd.hCursor = LoadCursor(hInstance, IDC_ARROW);
	tdsUIWnd.hIcon = ::LoadIcon(hInstance, (LPCTSTR)(IDI_LOGO));
	tdsUIWnd.lpszMenuName = NULL;
	tdsUIWnd.style = CS_HREDRAW | CS_VREDRAW;
	tdsUIWnd.hbrBackground = (HBRUSH)COLOR_WINDOW;
	tdsUIWnd.lpfnWndProc = WindowProc_tdsUI;
	tdsUIWnd.lpszClassName = _T("tdsUI");
	tdsUIWnd.hInstance = hInstance;
	RegisterClass(&tdsUIWnd);


	int x = 200;
	int y = 200;
	 w = 960;
	 h = 720;

	//创建窗口
	string title = tds->conf->title;

	RECT rc;
	SetRect(&rc, 0, 0, w, h);
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

	wkeEnableHighDPISupport();//这句话要放在createWindow之前，否则会导致标题栏的图标不显示。原因不知

	HWND hwnd = CreateWindow(
		"tdsUI",           //上面注册的类名，要完全一致  
		title.c_str(),                     //窗口标题文字  
		WS_OVERLAPPEDWINDOW, //窗口外观样式  
		x,             //窗口相对于父级的X坐标  
		y,             //窗口相对于父级的Y坐标  
		rc.right - rc.left,                //窗口的宽度  
		rc.bottom - rc.top,                //窗口的高度  
		NULL,               //没有父窗口，为NULL  
		NULL,               //没有菜单，为NULL  
		hInstance,          //当前应用程序的实例句柄  
		NULL);              //没有附加数据，为NULL 


	m_hUI = wkeCreateWebWindow(WKE_WINDOW_TYPE_CONTROL, hwnd, 0, 0, w, h);
	wkeSetZoomFactor(m_hUI, 1.5);
	wkeLoadURL(m_hUI, tds->conf->homepage.c_str());
	wkeShowWindow(m_hUI, TRUE);
	ShowWindow(hwnd, SW_SHOW);
}


void chromeThread()
{
	string chromePath = fs::appPath() + "\\chrome\\chrome.exe";
	//--kiosk为全屏参数，并且鼠标移到屏幕上边缘不会出现退出全屏的 ×
	string chromeParam = "";
	if (tds->conf->fullscreen)
		chromeParam += " --kiosk";
	chromeParam += " --app=\"" + tds->conf->homepage + "\"";
	if (fs::fileExist(chromePath))
	{
		chromePath += chromeParam;
		wstring title = charCodec::utf8toUtf16("123456");
		STARTUPINFOW si;
		si.lpTitle = (LPWSTR)title.c_str();
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));

		// Start the child process.
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		if (!CreateProcessW(NULL,   // No module name (use command line)
			(LPWSTR)charCodec::utf8toUtf16(chromePath).c_str(),        // Command line
			NULL,           // Process handle not inheritable
			NULL,           // Thread handle not inheritable
			FALSE,          // Set handle inheritance to FALSE
			0,              // No creation flags
			NULL,           // Use parent's environment block
			NULL,           // Use parent's starting directory
			&si,            // Pointer to STARTUPINFO structure
			&pi)           // Pointer to PROCESS_INFORMATION structure
			)
		{
			LOG("启动Chrome失败" + sys::getLastError());
		}
		else
		{
			// 等待新进程初始化完毕  
			string s = "chrome进程Id: " + str::format("0x%x,%d", pi.dwProcessId, pi.dwProcessId);
			LOG(s);
			WaitForInputIdle(pi.hProcess, 5000);
			int windowFindTime = 3000;  //3秒内持续查找ATExpert为标题的窗口,由于Chrome的某些机制,该标题对应的窗口句柄会发生变化
			int idx = 0;
			while (windowFindTime > 0)
			{
				HWND hWnd = FindWindowW(NULL, charCodec::utf8toUtf16(tdsImp.uiWndTitle).c_str());
				if (tdsImp.uiWnd != hWnd)
				{
					tdsImp.uiWnd = hWnd;
					string s = "chrome窗口句柄: " + str::format("[%d]0x%x,%d",idx, tdsImp.uiWnd, tdsImp.uiWnd);
					LOG(s);
					idx++;
				}
				Sleep(50);
				windowFindTime -= 50;
			}	

			HICON hIcon = NULL;
			wstring ws = charCodec::ansiToUtf16(fs::appPath() + "\\favicon.ico");
			hIcon = (HICON)LoadImageW(NULL, ws.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE);

			Sleep(3000); //此处要sleep一下,不然任务栏图标替换不掉

			SendMessage(tds->uiWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
			SendMessage(tds->uiWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
			
			WaitForSingleObject(pi.hProcess, INFINITE);//用户从任务栏右键关闭chrome浏览器，此处阻塞解除，程序从此处退出
		}

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		if (tdsImp.m_fpProcBeforeExit!=NULL)
		{
			tdsImp.m_fpProcBeforeExit();	
		}

		tdsImp.stop();
		exit(0);
	}
}

void createChromeWnd()
{
	std::thread t(chromeThread);
	t.detach();
}



TDS_imp::TDS_imp()
{
	conf = &tdsConf;
	m_fpProcBeforeExit = NULL;
}

bool TDS_imp::setEncodeing(string encoding)
{
	InterfaceEncoding = encoding;
	return true;
}

string TDS_imp::getUIMode()
{
	string uimode;
	if (fs::fileExist(fs::appPath() + "\\chrome\\chrome.exe"))
	{
		uimode = "chrome";
	}
	else if (fs::fileExist(fs::appPath() + "\\miniblink_x64.dll"))
	{
		uimode = "miniblink";
	}
	else
	{
		uimode = "console";
	}

	return uimode;
}

bool TDS_imp::setWorkingDir()
{
	string cwd = fs::appPath();
	BOOL bRet = SetCurrentDirectoryW(charCodec::utf8toUtf16(cwd).c_str());
	string s = bRet ? "成功" : "失败";
	LOG("[keyinfo][工作目录   ]" + cwd + "设置" + s + ",工作目录用于RPC命令中的相对路径");
	return true;
}




bool TDS_imp::run(string cmdline)
{
	dogFeeder.run();

	//load tds.json
	tdsConf.loadConf();
	logger.m_bEnable = tdsConf.enableLog;

	//初始化接口
	tds->db = &::db;

	setWorkingDir();

	//check mode
	if(conf->uiMode == "")
		conf->uiMode = getUIMode();

#ifdef _WINDLL // dll模式下需要创建命令行
	if (conf->uiMode == "console")
	{
		createConsole();
	}
#endif

	//display version
	LOG("[keyinfo]tds " + version + getbuildtime());

	logger.setLogLevel(tdsConf.logLevel);
	LOG("[keyinfo][日志      ] 记录等级:" + tdsConf.logLevel + ",日志文件路径:" + fs::appPath() + "\\log");

	//startup xiaot
	xiaot.init();

	//startup tds modules
	//if db folder is not exist. open will create an empty folder
	//先初始化数据库。 mo和io的初始化都可能从数据库中加载数据 。
	//ioSrv会从数据库加载设备配置缓存数据
	if (tds->conf->enableDB)
		::db.Open(tds->conf->dbPath, prj.m_strName);

	prj.loadConf();
	ioSrv.run(); //先启动ioSrv加载io组态,再启动ds.如果先启动ds可能会把某些managed设备当作spare设备
	ds.run();  //data server
#ifdef ENABLE_FFMPEG
	//rds.run(); //remote desktop server
#endif

	almSrv.run();
	logSrv.run();
	sHost.run();

	//create browser window
	if (conf->uiMode == "miniblink")
	{
	    ::ShowWindow(GetConsoleWindow(), SW_HIDE);
		wkeSetWkeDllPath(L"miniblink_x64.dll");
		wkeInitialize();
		createMiniblinkWnd();
	}
	else if (conf->uiMode == "chrome")
	{
		createChromeWnd();
	}

	GetLocalTime(&stStartupTime);

	string sTitle = "TDS " + version + "." + SVN_VERSION + "(" + getbuilddate() + ")|DS端口:" + str::fromInt(tds->conf->port) + "|启动时间:" + timeopt::st2str(tds->stStartupTime);
	SetConsoleTitleW(charCodec::utf8toUtf16(sTitle).c_str());

	return true;
}

void TDS_imp::stop()
{
	ds.stop();
	ioSrv.stop();
}

bool TDS_imp::setProcBeforeExit(fp_procBeforeExit callback)
{
	m_fpProcBeforeExit = callback;
	return true;
}

bool TDS_imp::call(string method, string param , RPC_RESP& resp)
{
	try {
		json jParam;
		if(param == "")
			jParam = nullptr;
		else
			jParam = json::parse(param);
		RPC_SESSION session;
		bool bHandled = rpcSrv.handleMethodCall(method, jParam, resp, session);
		if (bHandled)
		{
			return true;
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		json jException;
		jException["exception"] = errorType;
		resp.error = jException.dump();
		true;
	}
	return false;
}

void TDS_imp::setRpcHandler(fp_rpcHandler handler)
{
	rpcSrv.m_pluginHandler = handler;
}

void TDS_imp::rpcNotify(string method, string params, string sessionId)
{
	json jParams;
	if (params != "")
	{
		try {
			jParams = json::parse(params);
		}
		catch (std::exception& e)
		{
			string s = e.what();
		}
	}
	rpcSrv.notify(method, jParams);
}

bool TDS_imp::enableIoLog(string ioAddr, bool bEnable)
{
	ioDev* d = ioSrv.getIODev(ioAddr);
	if (d)
	{
		d->m_bEnableIoLog = bEnable;
		return true;
	}
	return false;
}

bool TDS_imp::sendToIoAddr(string ioAddr,const char* p, int l)
{
	ioDev* d = ioSrv.getIODev(ioAddr);
	if (d)
	{
		return d->sendData((char*)p, l);
	}
	return false;
}

bool TDS_imp::connectDev(string ioAddr)
{
	ioDev* p = ioSrv.getIODev(ioAddr);
	if (p)
	{
		return p->connect();
	}
	return false;
}

bool TDS_imp::isOnline(string ioAddr)
{
	ioDev* d = ioSrv.getIODev(ioAddr);
	if (d)
	{
		return d->m_bOnline;
	}
	return false;
}

bool TDS_imp::isConnected(string ioAddr)
{
	ioDev* d = ioSrv.getIODev(ioAddr);
	if (d)
	{
		return d->m_bConnected;
	}
	return false;
}

bool TDS_imp::isInUse(string ioAddr)
{
	ioDev* d = ioSrv.getIODev(ioAddr);
	if (d)
	{
		return d->m_bInUse;
	}
	return false;
}

bool TDS_imp::lockIoAddr(string ioAddr)
{
	ioDev* d = ioSrv.getIODev(ioAddr);
	if (d)
	{
		d->CommLock();
		return true;
	}
	return false;
}

bool TDS_imp::unlockIoAddr(string ioAddr)
{
	ioDev* d = ioSrv.getIODev(ioAddr);
	if (d)
	{
		d->CommUnlock();
		return true;
	}
	return false;
}

bool TDS_imp::setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback)
{
	ioDev* d = ioSrv.getIODev(ioAddr);
	if (d)
	{
		d->setRecvCallback(user, recvCallback);
	}
	return true;
}

void TDS_imp::startStream(string streamId, STREAM_INFO* si)
{
	streamSrv.startStream(streamId,si);
}

void TDS_imp::pushStream(string streamId, char* pData, int len, STREAM_INFO* si)
{
	if (InterfaceEncoding == "gb2312")
	{
		streamId = charCodec::ansi2Utf8(streamId);
	}

	STREAM_DATA sd;
	sd.pData = pData;
	sd.len = len;
	sd.info = *si;
	streamSrv.pushStream(streamId,sd);
	sd.pData = NULL;
}

void TDS_imp::pullStream(string streamId, void* user, fp_onVideoStreamRecv onRecvStream,STREAM_INFO* si)
{
	streamSrvNode* pssn = streamSrv.getSrvNode(streamId);
	pssn->addPuller(user,onRecvStream);
}

void TDS_imp::log(const char* text)
{
	if (InterfaceEncoding == "gb2312")
	{
		string strUtf8 = charCodec::ansi2Utf8(text);
		LOG(strUtf8);
	}
	else
		LOG(text);
}

void TDS_imp::registerMsgSinker(fp_msgSinker sinker)
{
	m_msgSinkers.push_back(sinker);
}

void TDS_imp::publishMsg(MODULE_BUS_MSG& msg)
{
	LOG("MODULE EVENT: " + msg.moduleName + "," + msg.eventName + "," + msg.content);

	for (int i = 0; i < m_msgSinkers.size(); i++)
	{
		fp_msgSinker s = m_msgSinkers.at(i);
		s(msg);
	}
}

//////////////////////////////////////////////////////////////////////////////////////
void CDumpCatch::MyPureCallHandler(void)
{
	throw invalid_argument("");
}

void CDumpCatch::MyInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
{
	//The parameters all have the value NULL unless a debug version of the CRT library is used.
	throw invalid_argument("");
}

void CDumpCatch::SetInvalidHandle()
{
#if _MSC_VER >= 1400  // MSVC 2005/8
	m_preIph = _set_invalid_parameter_handler(MyInvalidParameterHandler);
#endif  // _MSC_VER >= 1400
	m_prePch = _set_purecall_handler(MyPureCallHandler);   //At application, this call can stop show the error message box.
}

void CDumpCatch::UnSetInvalidHandle()
{
#if _MSC_VER >= 1400  // MSVC 2005/8
	_set_invalid_parameter_handler(m_preIph);
#endif  // _MSC_VER >= 1400
	_set_purecall_handler(m_prePch); //At application this can stop show the error message box.
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI CDumpCatch::TempSetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
	return NULL;
}

BOOL CDumpCatch::AddExceptionHandle()
{
	m_preFilter = ::SetUnhandledExceptionFilter(UnhandledExceptionFilterEx);
	PreventSetUnhandledExceptionFilter();
	return TRUE;
}

BOOL CDumpCatch::RemoveExceptionHandle()
{
	if (m_preFilter != NULL)
	{
		::SetUnhandledExceptionFilter(m_preFilter);
		m_preFilter = NULL;
	}
	return TRUE;
}

CDumpCatch::CDumpCatch()
{
	SetInvalidHandle();
	AddExceptionHandle();
}

CDumpCatch::~CDumpCatch()
{
	UnSetInvalidHandle();
	RemoveExceptionHandle();
}

BOOL CDumpCatch::ReleaseDumpFile(const std::string& strPath, EXCEPTION_POINTERS* pException)
{
	HANDLE hDumpFile = ::CreateFile(strPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDumpFile == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}
	MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
	dumpInfo.ExceptionPointers = pException;
	dumpInfo.ThreadId = ::GetCurrentThreadId();
	dumpInfo.ClientPointers = TRUE;
	BOOL bRet = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), hDumpFile, MiniDumpWithFullMemory, &dumpInfo, NULL, NULL);
	::CloseHandle(hDumpFile);
	return bRet;
}

LONG WINAPI CDumpCatch::UnhandledExceptionFilterEx(struct _EXCEPTION_POINTERS* pException)
{
	char szPath[MAX_PATH] = { 0 };
	::GetModuleFileName(NULL, szPath, MAX_PATH);
	std::string strexe = szPath;

	::PathRemoveFileSpec(szPath);
	std::string strPath = szPath;

	string dmpfile = strPath + "\\*.dmp";
	string strcmd = "del /s /q " + dmpfile;
	system(strcmd.c_str());

	SYSTEMTIME stNow;
	GetLocalTime(&stNow);
	string strFile = str::format("%4d.%02d.%02d %02d-%02d-%02d.dmp", stNow.wYear, stNow.wMonth, stNow.wDay, stNow.wHour, stNow.wMinute, stNow.wSecond);
	strFile = strPath + "\\" + strFile;
	BOOL bRelease = ReleaseDumpFile(strFile.c_str(), pException);

	ShellExecute(NULL, "open", strexe.c_str(), NULL, NULL, SW_SHOW);

	return EXCEPTION_EXECUTE_HANDLER;
}

BOOL CDumpCatch::PreventSetUnhandledExceptionFilter()
{
	HMODULE hKernel32 = LoadLibrary("kernel32.dll");
	if (hKernel32 == NULL)
	{
		return FALSE;
	}
	void* pOrgEntry = ::GetProcAddress(hKernel32, "SetUnhandledExceptionFilter");
	if (pOrgEntry == NULL)
	{
		return FALSE;
	}

	unsigned char newJump[5];
	DWORD dwOrgEntryAddr = (DWORD)pOrgEntry;
	dwOrgEntryAddr += 5;//jump instruction has 5 byte space

	void* pNewFunc = &TempSetUnhandledExceptionFilter;
	DWORD dwNewEntryAddr = (DWORD)pNewFunc;
	DWORD dwRelativeAddr = dwNewEntryAddr - dwOrgEntryAddr;

	newJump[0] = 0xE9;//jump
	memcpy(&newJump[1], &dwRelativeAddr, sizeof(DWORD));
	SIZE_T bytesWritten;
	DWORD dwOldFlag, dwTempFlag;
	::VirtualProtect(pOrgEntry, 5, PAGE_EXECUTE_READWRITE, &dwOldFlag);
	BOOL bRet = ::WriteProcessMemory(::GetCurrentProcess(), pOrgEntry, newJump, 5, &bytesWritten);
	::VirtualProtect(pOrgEntry, 5, dwOldFlag, &dwTempFlag);
	return bRet;
}
