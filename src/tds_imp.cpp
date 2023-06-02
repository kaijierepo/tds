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
#include  "reverseInterface.h"
#include "ioSrv.h"
#include "ioDev.h"
#include "tdsConf.h"
#include "mp.h"
#include "res/resource.h"
#include "as.h"
#include "logServer.h"
#include "scriptManager.h"
#include "version.h"
#include "db.h"
#include "tdsWatchDog.h"
#include "userMng.h"
#include "webSrv.h"
#include "taskServer.h"


string InterfaceEncoding = "utf8";

string version = "v1.0";


#ifdef _WIN32
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
#endif


void chromeThread()
{
#ifdef _WIN32
	string chromePath = fs::appPath() + "\\chrome\\chrome.exe";
	//--kiosk为全屏参数，并且鼠标移到屏幕上边缘不会出现退出全屏的 ×
	string chromeParam = "";
	if (tds->conf->fullscreen)
		chromeParam += " --kiosk";
	chromeParam += " --app=\"" + tds->conf->homepage + "\"";
	if (fs::fileExist(chromePath))
	{
		chromePath += chromeParam;
		wstring title = charCodec::utf8_to_utf16("123456");
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
			(LPWSTR)charCodec::utf8_to_utf16(chromePath).c_str(),        // Command line
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
				HWND hWnd = FindWindowW(NULL, charCodec::utf8_to_utf16(tdsImp.uiWndTitle).c_str());
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
			wstring ws = charCodec::gb_to_utf16(fs::appPath() + "\\favicon.ico");
			hIcon = (HICON)LoadImageW(NULL, ws.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE);

			Sleep(3000); //此处要sleep一下,不然任务栏图标替换不掉

			SendMessage((HWND)tds->uiWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
			SendMessage((HWND)tds->uiWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
			
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
#endif
}

void createChromeWnd()
{
	std::thread t(chromeThread);
	t.detach();
}


//void startMicroService(string path)
//{
//	if (fs::fileExist(path))
//	{
//		wstring title = charCodec::utf8_to_utf16("123456");
//		STARTUPINFOW si;
//		si.lpTitle = (LPWSTR)title.c_str();
//		PROCESS_INFORMATION pi;
//		ZeroMemory(&si, sizeof(si));
//		si.cb = sizeof(si);
//		ZeroMemory(&pi, sizeof(pi));
//
//		// Start the child process.
//		si.dwFlags = STARTF_USESHOWWINDOW; // 指定wShowWindow成员有效
//		si.wShowWindow = TRUE; // 此成员设为TRUE的话则显示新建进程的主窗口
//
//		if (!CreateProcessW(NULL,   // No module name (use command line)
//			(LPWSTR)charCodec::utf8_to_utf16(path).c_str(),        // Command line
//			NULL,           // Process handle not inheritable
//			NULL,           // Thread handle not inheritable
//			FALSE,          // Set handle inheritance to FALSE
//			CREATE_NEW_CONSOLE,              // No creation flags
//			NULL,           // Use parent's environment block
//			NULL,           // Use parent's starting directory
//			&si,            // Pointer to STARTUPINFO structure
//			&pi)           // Pointer to PROCESS_INFORMATION structure
//			)
//		{
//			LOG("启动失败" + path + sys::getLastError());
//		}
//		else
//		{
//
//		}
//
//		CloseHandle(pi.hProcess);
//		CloseHandle(pi.hThread);
//	}
//}


TDS_imp::TDS_imp()
{
	conf = nullptr;
	db = nullptr;
	xiaoT = nullptr;
	gzhServer = nullptr;
	smsServer = nullptr;
	tds = &tdsImp;
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
	BOOL bRet = SetCurrentDirectoryW(charCodec::utf8_to_utf16(cwd).c_str());
	string s = bRet ? "成功" : "失败";
	//LOG("[keyinfo][工作目录   ]" + cwd + "设置" + s + ",工作目录用于RPC命令中的相对路径");
	return true;
}

bool TDS_imp::run(string cmdline)
{
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
	LOG("[日志      ] 记录等级:" + tdsConf.logLevel + ",日志文件路径:" + fs::appPath() + "\\log");


	//指定配置路径没有配置文件夹，则新建
	if (tds->conf->confPath == fs::appPath() + "/conf")
	{
		if (!fs::fileExist(tds->conf->confPath)) {
			fs::createFolderOfPath(tds->conf->confPath);
			LOG("[keyinfo]配置路径未找到配置文件夹,新建配置,路径:" + tds->conf->confPath);
		}
	}

	LOG("[UI路径	] " + tds->conf->uiPath);
	LOG("[组态路径	] " + tds->conf->confPath);
	LOG("[数据库	] " + tds->conf->dbPath);

	//初始化系统组件，完成静态结构建立。loadConf和init类函数。在调用run之前，要先完成.否则在结构建立之前就进行数据io，可能会出现一些不必要的错误。

	//startup tds modules
	//if db folder is not exist. open will create an empty folder
	//先初始化数据库。 mo和io的初始化都可能从数据库中加载数据 。
	//ioSrv会从数据库加载设备配置缓存数据
	if (tds->conf->enableDB)
		::db.Open(tds->conf->dbPath, prj.m_name);
	prj.loadObjTemplate();
	prj.loadConfFile();
	prj.getAllVarExpScript();
	ioSrv.loadConf();
	almSrv.init();
	userMng.init();
	scriptManager.init();

	//初始化tds插件
	if (tds->xiaoT)
		tds->xiaoT->init();
	if (tds->smsServer)
		tds->smsServer->init();
	if (tds->shellServer)
		tds->shellServer->init();
	if (tds->gzhServer)
		tds->gzhServer->init();
	for(auto& i:tds->plugins)
	{
		i.second->init();
	}


	//开始运行，与外部建立通讯并进行数据io
	runWebServers();
	reverseInterface.run();
	ioSrv.run(); //先启动ioSrv加载io组态,再启动ds.如果先启动ds可能会把某些managed设备当作spare设备
	logSrv.run();
	scriptManager.run();
	//audioPlayer.run();
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
	for (auto& i : tds->plugins) 
	{
		i.second->run();
	}

	ioSrv.updateTag2IOAddrBinding();
	taskSrv.run();

	//create browser window
	if (conf->uiMode == "chrome")
	{
		createChromeWnd();
	}

	//timeopt::now(&stStartupTime);

	//m_sTitle = "TDS " + version + "." + SVN_VERSION + "(" + getbuildtime() + ")|启动:" + timeopt::st2str(tds->stStartupTime);
	m_sTitle = "TDS " + version + "." + SVN_VERSION + "(" + getbuildtime() + ")";
	//m_sTitle = "TDS " + version + "." + SVN_VERSION + "   ";
	SetConsoleTitleW(charCodec::utf8_to_utf16(m_sTitle).c_str());

	return true;
}

void TDS_imp::stop()
{
	reverseInterface.stop();
	ioSrv.stop();
}

bool TDS_imp::setProcBeforeExit(fp_procBeforeExit callback)
{
	m_fpProcBeforeExit = callback;
	return true;
}

void TDS_imp::call(string method, json& param, json& err, json& rlt, RPC_SESSION session)
{
	try {
		RPC_RESP resp;
		rpcSrv.handleMethodCall(method, param, resp, session);
		if (resp.error != "")
		{
			err = json::parse(resp.error);
		}
		else{
			rlt = json::parse(resp.result);
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		json jException;
		jException["exception"] = errorType;
		err = jException;
		true;
	}
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

void thread_handleRpcCall(string method,json param,int delay) {
	if (delay > 0) {
		Sleep(delay);
	}

	RPC_SESSION session;
	RPC_RESP resp;
	rpcSrv.handleMethodCall(method, param, resp, session);
}

void TDS_imp::callAsyn(string method, json& param, int delay)
{
	thread t(thread_handleRpcCall, method, param, delay);
	t.detach();
}


void TDS_imp::callAsyn(string method, string& param, int delay)
{
	json j = json::parse(param);
	thread t(thread_handleRpcCall, method, j,delay);
	t.detach();
}

void thread_handleBatchRpcCall(vector<json> calls, int delay) {
	if (delay > 0) {
		Sleep(delay);
	}

	for (int i = 0; i < calls.size(); i++) {
		json& call = calls[i];

		RPC_SESSION session;
		RPC_RESP resp;
		rpcSrv.handleMethodCall(call["method"], call["params"], resp, session);
	}
}

void TDS_imp::batchCallAsyn(vector<json> calls,int delay)
{
	thread t(thread_handleBatchRpcCall, calls, delay);
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
		return d->sendData((unsigned char*)p, l);
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
	//ioDev* d = ioSrv.getIODev(ioAddr);
	//if (d)
	//{
	//	d->setRecvCallback(user, recvCallback);
	//}
	return true;
}


#ifdef ENABLE_GENICAM
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
#endif

void TDS_imp::log(const char* text)
{
	if (InterfaceEncoding == "gb2312")
	{
		string strUtf8 = charCodec::gb_to_utf8(text);
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


