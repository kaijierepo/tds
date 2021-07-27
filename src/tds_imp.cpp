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

string tdsEncoding = "utf8";


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

	//wkeEnableHighDPISupport();//这句话要放在createWindow之前，否则会导致标题栏的图标不显示。原因不知

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

		STARTUPINFOW si;
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
			WaitForSingleObject(pi.hProcess, INFINITE);
		}

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

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
}

bool TDS_imp::setEncodeing(string encoding)
{
	tdsEncoding = encoding;
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

bool TDS_imp::run(string cmdline)
{
	//load tds.json
	tdsConf.loadConf();

	//check mode
	if(conf->uiMode == "")
		conf->uiMode = getUIMode();
	if (conf->uiMode == "console")
	{
		createConsole();
	}

	logger.setLogLevel(tdsConf.logLevel);
	LOG("日志记录等级:" + tdsConf.logLevel);

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

string TDS_imp::call(string method, string param, string& error)
{
	try {
		json jParam;
		if(param == "")
			jParam = nullptr;
		else
			jParam = json::parse(param);
		return tdsSrv.handleMethodCall(method, jParam,error,NULL);
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		return "error " + errorType;
	}
	
}

void TDS_imp::setRpcHandler(fp_rpcHandler handler)
{
	tdsSrv.m_pluginHandler = handler;
}

bool TDS_imp::sendToIoAddr(string ioAddr, char* p, int l)
{
	ioAddress sIoAddr;
	sIoAddr.FromString(ioAddr);
	ioDev* d = ioSrv.getIODev(sIoAddr);
	if (d)
	{
		return d->sendData(p, l);
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

void TDS_imp::registerVideoTag(string tag, fp_startStream startStream,void*& mp,STREAM_INFO* si)
{
	MP* pmp = prj.getMp(tag);
	if(pmp == NULL)
	{
		pmp = (MP*)prj.createChildMO(tag, MO_TYPE::mp);
		prj.m_mapAllMP[tag] = pmp;
	}
	pmp->m_valType = DATA_TYPE::video;
	pmp->m_streamPusher = startStream;
	if (si)
		pmp->m_streamInfo = *si;
	mp = pmp;
}

void TDS_imp::pushStream(void* mp, char* pData, int len, STREAM_TYPE st, STREAM_INFO* si)
{
#ifdef ENABLE_FFMPEG
	MP* pmp = (MP*)mp;
	if (pmp->m_streamPuller == NULL)
		return;
	if (pmp->m_streamPuller->pTcpSession == NULL)
	{
		delete pmp->m_videoCodec;
		pmp->m_videoCodec = NULL;
		pmp->m_streamPuller = NULL;
		return;
	}

	if (st == ST_BMP)
	{
		if (pmp->m_videoCodec == NULL)
		{
			pmp->m_videoCodec = new videoCodec();
		}

		videoCodec& vc = *pmp->m_videoCodec;
		if (!vc.bInit)
		{
			vc.inConf.pixelFmt = AV_PIX_FMT_RGB24;
			vc.outConf.codecID = AV_CODEC_ID_VP9;
		}

		vc.input_Bmp((char*)pData, len);
		int iStreamLen = 0;
		char* pStream = NULL;
		vc.output();
		//发送视频头，web端mse收到该头才能正确解码
		if (pmp->m_streamPuller->pTcpSession->iSendSucCount == 0)
		{
			pmp->m_streamPuller->send(vc.headerBuff, vc.iHeaderBuffLen);
		}
		pmp->m_streamPuller->send(vc.outputBuff, vc.iOutputLen);
		vc.iOutputLen = 0;
	}
	else if (st == ST_RGBA)
	{
		if (pmp->m_streamPuller->streamFmt == "rgba")//直接转发
		{
			pmp->m_streamPuller->send(pData,len);
		}
	}
#endif
}

void TDS_imp::log(char* text)
{
	if (tdsEncoding == "gb2312")
	{
		string strUtf8 = charCodec::ansi2Utf8(text);
		LOG(strUtf8);
	}
	else
		LOG(text);
}


