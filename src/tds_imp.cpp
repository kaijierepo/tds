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
#include "video/audioPlayer.h"
#include <filesystem>
#include "tools/dumpCatch.h"
#include "tools/hmrSrv.h"
#include "users/userMng.h"



string InterfaceEncoding = "utf8";

string version = "v1.0";


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
	conf = nullptr;
	db = nullptr;
	xiaoT = nullptr;
	gzhServer = nullptr;
	smsServer = nullptr;
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
	//LOG("[keyinfo][工作目录   ]" + cwd + "设置" + s + ",工作目录用于RPC命令中的相对路径");
	return true;
}

bool TDS_imp::runAsTDB()
{
	tds->db = &::db;
	logger.setLogLevel(tdsConf.logLevel);
	LOG("[keyinfo][日志      ] 记录等级:" + tdsConf.logLevel + ",日志文件路径:" + fs::appPath() + "\\log");
	LOG("[数据库	] " + tds->conf->dbPath);
	::db.Open(tds->conf->dbPath, prj.m_strName);
	runWebServers();
	GetLocalTime(&stStartupTime);
	string sTitle = "TDB " + version + "." + SVN_VERSION + "(" + getbuildtime() + ")|启动:" + timeopt::st2str(tds->stStartupTime);
	SetConsoleTitleW(charCodec::utf8toUtf16(sTitle).c_str());
}


bool TDS_imp::run(string cmdline)
{
	dogFeeder.run();

	//load tds.json
	logger.m_bEnable = tdsConf.enableLog;

	//funcMongooseLogCb = LOG3;
	#ifndef DEBUG
	 // mg_log_set("0");
	#endif
	 

	//初始化接口
	tds->db = &::db;

	//check mode
	if(conf->uiMode == "")
		conf->uiMode = getUIMode();

#ifdef _WINDLL // dll模式下需要创建命令行
	if (conf->uiMode == "console")
	{
		createConsole();
	}
#endif


	logger.setLogLevel(tdsConf.logLevel);
	LOG("[keyinfo][日志      ] 记录等级:" + tdsConf.logLevel + ",日志文件路径:" + fs::appPath() + "\\log");


	//如果配置中没有自定义商标信息，写入良途软件商标信息
	if (tds->conf->confPath == fs::appPath() + "/conf")
	{
		if (!fs::fileExist(tds->conf->confPath)) {
			fs::createFolderOfPath(tds->conf->confPath);
			LOG("[warn]新建配置文件夹,路径:" + tds->conf->confPath);
		}
	}
	createDefaultCompanyInfo();

	LOG("[UI路径	] " + tds->conf->uiPath);
	LOG("[组态路径	] " + tds->conf->confPath);
	LOG("[数据库	] " + tds->conf->dbPath);

	//初始化系统组件，完成静态结构建立。loadConf和init类函数。在调用run之前，要先完成.否则在结构建立之前就进行数据io，可能会出现一些不必要的错误。

	//startup tds modules
	//if db folder is not exist. open will create an empty folder
	//先初始化数据库。 mo和io的初始化都可能从数据库中加载数据 。
	//ioSrv会从数据库加载设备配置缓存数据
	if (tds->conf->enableDB)
		::db.Open(tds->conf->dbPath, prj.m_strName);
	prj.loadConf();
	ioSrv.loadConf();
	almSrv.init();
	userMng.init();

	//初始化tds插件
	if (tds->xiaoT)
		tds->xiaoT->init();
	if (tds->smsServer)
		tds->smsServer->init();
	if (tds->shellServer)
		tds->shellServer->init();
	if (tds->gzhServer)
		tds->gzhServer->init();


	//开始运行，与外部建立通讯并进行数据io
	if (tds->conf->edge)
	{
		ds.runAsEdge();
	}
	else
	{
		runWebServers();
		
	}
	ioSrv.run(); //先启动ioSrv加载io组态,再启动ds.如果先启动ds可能会把某些managed设备当作spare设备
#ifdef ENABLE_FFMPEG
	//rds.run(); //remote desktop server
#endif
	logSrv.run();
	sHost.run();
	audioPlayer.run();
	if (conf->debugMode)
	{
		hmrServer.run(tds->conf->uiPath);
	}
	userMng.run();


	//运行tds插件
	if (tds->xiaoT)
		tds->xiaoT->run();
	if (tds->smsServer)
		tds->smsServer->run();
	if (tds->shellServer)
		tds->shellServer->run();
	if (tds->gzhServer)
		tds->gzhServer->run();

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

	string sTitle = "TDS " + version + "." + SVN_VERSION + "(" + getbuildtime() + ")|启动:" + timeopt::st2str(tds->stStartupTime);
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

void handleRpcCall(string method,string param) {
	json j = json::parse(param);
	RPC_SESSION session;
	RPC_RESP resp;
	rpcSrv.handleMethodCall(method, j, resp, session);
}

void TDS_imp::callAsyn(string method, string param)
{
	thread t(handleRpcCall, method, param);
	t.detach();
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

string TDS_imp::getVersion() {
	string s = version + "." + SVN_VERSION;
	return s;
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
using namespace std::filesystem;
void TDS_imp::createDefaultCompanyInfo() {
	string confPath = tds->conf->confPath;

	//生成默认配置
	try
	{
		if (!fs::fileExist(confPath + "/info.json"))
		{
			copy(charCodec::utf8toUtf16(fs::appPath() + "/ui/app/assets/info.json"), charCodec::utf8toUtf16(confPath + "/info.json"));
		}

		if (!fs::fileExist(confPath + "/banner.svg"))
		{
			copy(charCodec::utf8toUtf16(fs::appPath() + "/ui/app/assets/banner.svg"), charCodec::utf8toUtf16(confPath + "/banner.svg"));
		}

		if (!fs::fileExist(confPath + "/logo.svg"))
		{
			copy(charCodec::utf8toUtf16(fs::appPath() + "/ui/app/assets/logo.svg"), charCodec::utf8toUtf16(confPath + "/logo.svg"));
		}
	}
	catch(exception& e)
	{

	}


	//拷贝到ui目录
	try
	{
		if (fs::fileExist(confPath + "/info.json"))
		{
			std::filesystem::copy( charCodec::utf8toUtf16(confPath + "/info.json"), charCodec::utf8toUtf16(fs::appPath() + "/ui/info.json"), std::filesystem::copy_options::overwrite_existing);
		}

		if (fs::fileExist(confPath + "/banner.svg"))
		{
			std::filesystem::copy(charCodec::utf8toUtf16(confPath + "/banner.svg"), charCodec::utf8toUtf16(fs::appPath() + "/ui/banner.svg"), std::filesystem::copy_options::overwrite_existing);
		}

		if (fs::fileExist(confPath + "/logo.svg"))
		{
			std::filesystem::copy(charCodec::utf8toUtf16(confPath + "/logo.svg"), charCodec::utf8toUtf16(fs::appPath() + "/ui/logo.svg"), std::filesystem::copy_options::overwrite_existing);
		}
	}
	catch (exception& e)
	{

	}
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


