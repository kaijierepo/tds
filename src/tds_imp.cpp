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

string InterfaceEncoding = "utf8";

string build_date = __DATE__;
string build_time = __TIME__;
string version_build_info = "(build " + build_date + " " + build_time + ")";
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
	string chromeParam = " --app=\"" + tds->conf->homepage + "\"";
	if (fs::fileExist(chromePath))
	{
		chromePath += chromeParam;
		wstring title = charCodec::utf8toUtf16("123456");
		STARTUPINFOW si;
		si.lpTitle = (LPWSTR)title.c_str();
		si.wShowWindow = SW_MAXIMIZE; 
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));

		// Start the child process.
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_SHOW;
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
			WaitForInputIdle(pi.hProcess, 5000);
			while (1)
			{
				tdsImp.mainWnd = FindWindowW(NULL, L"TDSUI");
				if (tdsImp.mainWnd)
					break;
				Sleep(1);
			}	
			WaitForSingleObject(pi.hProcess, INFINITE);
		}

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		if (tdsImp.m_fpProcBeforeExit!=NULL)
		{
			tdsImp.m_fpProcBeforeExit();	
		}

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
	LOG("[工作目录   ]" + cwd + "设置" + s + ",工作目录用于RPC命令中的相对路径");
	return true;
}

bool TDS_imp::run(string cmdline)
{
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
	LOG("tds " + version + version_build_info);

	logger.setLogLevel(tdsConf.logLevel);
	LOG("[日志      ] 记录等级:" + tdsConf.logLevel + ",日志文件路径:" + fs::appPath() + "\\log");

	//startup xiaot
	xiaot.init();

	//startup tds modules
	prj.loadConf();
	ds.run();  //data server
#ifdef ENABLE_FFMPEG
	//rds.run(); //remote desktop server
#endif
	ioSrv.run();

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

	return true;
}

bool TDS_imp::setProcBeforeExit(fp_procBeforeExit callback)
{
	m_fpProcBeforeExit = callback;
	return true;
}

bool TDS_imp::call(string method, string param, string& result)
{
	try {
		json jParam;
		if(param == "")
			jParam = nullptr;
		else
			jParam = json::parse(param);
		string error;
		RPC_RESULT rpcResult;
		bool bHandled = tdsSrv.handleMethodCall(method, jParam, rpcResult,error,NULL);
		if (bHandled)
		{
			if (error != "")
			{
				result = error;
				return true;
			}
			result = rpcResult.textResult;
			return true;
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		json jException;
		jException["exception"] = errorType;
		result = jException.dump();
		true;
	}
	return false;
}

void TDS_imp::setRpcHandler(fp_rpcHandler handler)
{
	tdsSrv.m_pluginHandler = handler;
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
	tdsSrv.notify(method, jParams);
}

bool TDS_imp::enableIoLog(string ioAddr, bool bEnable)
{
	ioAddress sIoAddr;
	sIoAddr.FromString(ioAddr);
	ioDev* d = ioSrv.getIODev(sIoAddr);
	if (d)
	{
		d->m_bEnableIoLog = bEnable;
		return true;
	}
	return false;
}

bool TDS_imp::sendToIoAddr(string ioAddr,const char* p, int l)
{
	ioAddress sIoAddr;
	sIoAddr.FromString(ioAddr);
	ioDev* d = ioSrv.getIODev(sIoAddr);
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
	ioAddress sIoAddr;
	sIoAddr.FromString(ioAddr);
	ioDev* d = ioSrv.getIODev(sIoAddr);
	if (d)
	{
		return d->m_bOnline;
	}
	return false;
}

bool TDS_imp::lockIoAddr(string ioAddr)
{
	ioAddress sIoAddr;
	sIoAddr.FromString(ioAddr);
	ioDev* d = ioSrv.getIODev(sIoAddr);
	if (d)
	{
		d->CommLock();
		return true;
	}
	return false;
}

bool TDS_imp::unlockIoAddr(string ioAddr)
{
	ioAddress sIoAddr;
	sIoAddr.FromString(ioAddr);
	ioDev* d = ioSrv.getIODev(sIoAddr);
	if (d)
	{
		d->CommUnlock();
		return true;
	}
	return false;
}

bool TDS_imp::setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback)
{
	ioAddress sIoAddr;
	sIoAddr.FromString(ioAddr);
	ioDev* d = ioSrv.getIODev(sIoAddr);
	if (d)
	{
		d->m_pRecvCallback = recvCallback;
		d->m_pCallbackUser = user;
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


