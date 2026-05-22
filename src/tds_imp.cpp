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
#include "tSockSrv.h"
#include "ioSrv.h"
#include "ioDev.h"
#include "tdsConf.h"
#include "mp.h"
#include "tAlmSrv.h"
#include "logServer.h"
#include "scriptManager.h"
#include "tdb.h"
#include "tdsWatchDog.h"
#include "userMng.h"
#include "webSrv.h"
#include "taskServer.h"
#include "statusServer.h"
#include "common.h"
#include "rpcHandler_common.h"
#include "mqttSrv.h"
#include "video/streamServer.h"

#include "as_interface.h"
#ifdef _WIN32
#include "memDiag.h"
#endif

#ifdef USE_SVN_REV  //把svn版本号编译到目标文件中
#include "version.h"
#endif

string InterfaceEncoding = "utf8";

string version = "v1.0";
string g_ComputerStartupTime="";
string g_strStartupTime = "";

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

#include "CDataSimu.h"

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
				HWND hWnd = FindWindowW(NULL, charCodec::utf8_to_utf16(tds->uiWndTitle).c_str());
				if (tds->uiWnd != hWnd)
				{
					tds->uiWnd = hWnd;
					string s = "chrome窗口句柄: " + str::format("[%d]0x%x,%d",idx, tds->uiWnd, tds->uiWnd);
					LOG(s);
					idx++;
				}
				timeopt::sleepMilli(50);
				windowFindTime -= 50;
			}	

			HICON hIcon = NULL;
			wstring ws = charCodec::gb_to_utf16(fs::appPath() + "\\favicon.ico");
			hIcon = (HICON)LoadImageW(NULL, ws.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE);

			timeopt::sleepMilli(3000); //此处要sleep一下,不然任务栏图标替换不掉

			SendMessage((HWND)tds->uiWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
			SendMessage((HWND)tds->uiWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
			
			WaitForSingleObject(pi.hProcess, INFINITE);//用户从任务栏右键关闭chrome浏览器，此处阻塞解除，程序从此处退出
		}

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		if (tds->m_fpProcBeforeExit!=NULL)
		{
			tds->m_fpProcBeforeExit();
		}

		tds->stop();
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

#include <iostream>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

bool isTdsRunning() {
#ifdef _DEBUG
	return false;
#endif // !_DEBUG

#ifdef _WIN32
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		LOG("sixf------------------------------------WSAStartup");

		return true;
	}
#endif

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		LOG("sixf------------------------------------sockfd");

		return true;
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(60006); // 指定要绑定的端口号
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 绑定到本地回环地址

	int bindResult = ::bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
	if (bindResult < 0) {
#ifdef _WIN32
		unsigned long iErr = GetLastError();
		closesocket(sockfd);
		WSACleanup();
#else
		int iErr = errno;
		close(sockfd);
#endif
		std::cout << "TDS is already running, trying to bind port 60006 failed. Error: " << iErr << std::endl;

		LOG("sixf------------------------------------bindResult");
		return true;
	}

	return false;
}

TDS_imp::TDS_imp()
{
	conf = nullptr;
	xiaoT = nullptr;
	gzhServer = nullptr;
	smsServer = nullptr;
	shellServer = nullptr;
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
	else if (fs::fileExist(fs::appPath() + "\\miniblink_x64.dll")||\
		fs::fileExist(fs::appPath() + "\\mb108_x64.dll"))
	{
		uimode = "miniblink";
	}
	else
	{
		uimode = "console";
	}

	return uimode;
}

/*
bool TDS_imp::setWorkingDir()
{
	string cwd = fs::appPath();
	BOOL bRet = SetCurrentDirectoryW(charCodec::utf8_to_utf16(cwd).c_str());
	string s = bRet ? "成功" : "失败";
	//LOG("[keyinfo][工作目录   ]" + cwd + "设置" + s + ",工作目录用于RPC命令中的相对路径");
	return true;
}*/

void checkDBFormat(string path,bool& bCheckEnd, DB_FMT &db_Fmt) {
	if (!fs::fileExist(path)) return;
#ifdef _WIN32
	wstring wstrFolder = charCodec::tds_to_utf16(path);
	for (auto& i : filesystem::directory_iterator(wstrFolder)) {
		if (i.is_directory()) {
		string childFolderPath = charCodec::utf16_to_utf8(i.path().wstring());
#else
	for (auto& i : filesystem::directory_iterator(path)) {
		if (i.is_directory()) {
			string childFolderPath = i.path().string();
#endif
			checkDBFormat(childFolderPath, bCheckEnd, db_Fmt);
			if (bCheckEnd) break;
		}
		else if (i.is_regular_file())
		{
			fs::FILE_INFO fi;
#ifdef _WIN32
			fi.path = charCodec::utf16_to_utf8(i.path().wstring());
#else
			fi.path = i.path().string();
#endif
			fi.path = str::replace(fi.path, "\\", "/");
			size_t pos = fi.path.rfind("/");
			fi.name = fi.path.substr(pos + 1, fi.path.length() - pos - 1);
			
			if (fi.name.rfind(".jdb") == fi.name.length() - 4)
			{
				bCheckEnd = true;
				db_Fmt.deListName = "data_list.jdb";
				db_Fmt.curveIdxListName = "data_list.jdb";
				db_Fmt.curveDeNameSuffix = ".curve.jdb";
				db_Fmt.deItemKey_value = "value";
				
			}
			else if (fi.name.rfind("db.json") == fi.name.length() - 7 || fi.name.rfind(".curve.json") == fi.name.length() - 11)
			{
				bCheckEnd = true;
				db_Fmt.deListName = "db.json";
				db_Fmt.curveIdxListName = "db.curve.json";
				db_Fmt.curveDeNameSuffix = ".curve.json";
				db_Fmt.deItemKey_value = "val";
			}

			if (bCheckEnd) break;

		}
	}
}

std::string execCommand(const char* cmd) {
	std::array<char, 128> buffer;
	std::string result;
#ifdef _WIN32
	// 创建匿名管道
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	HANDLE hRead, hWrite;
	if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
		throw std::runtime_error("CreatePipe() failed!");
	}

	// 设置启动信息
	STARTUPINFOA si;
	ZeroMemory(&si, sizeof(STARTUPINFOA));
	si.cb = sizeof(STARTUPINFOA);
	si.hStdOutput = hWrite;
	si.hStdError = hWrite;
	si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE; // 隐藏窗口

	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

	// 创建进程
	if (!CreateProcessA(NULL, const_cast<char*>(cmd), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(hRead);
		CloseHandle(hWrite);
		throw std::runtime_error("CreateProcess() failed!");
	}

	// 关闭写句柄
	CloseHandle(hWrite);

	// 读取输出
	DWORD bytesRead;
	while (ReadFile(hRead, buffer.data(), buffer.size(), &bytesRead, NULL) && bytesRead > 0) {
		result.append(buffer.data(), bytesRead);
	}

	// 关闭读句柄
	CloseHandle(hRead);

	// 等待进程结束
	WaitForSingleObject(pi.hProcess, INFINITE);

	// 关闭进程和线程句柄
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
#else
	FILE* pipe = popen(cmd, "r");
	if (!pipe) {
		throw std::runtime_error("popen() failed!");
	}
	while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
		result += buffer.data();
	}
	pclose(pipe);
#endif
	return result;
}

std::string getSystemBootTime() {
#ifdef _WIN32
	std::string output = execCommand("systeminfo");
	std::string bootTimeLabel =charCodec::utf8_to_gb("系统启动时间:");
	size_t pos = output.find(bootTimeLabel);
	if (pos != std::string::npos) {
		size_t endPos = output.find('\n', pos);
		if (endPos != std::string::npos) {
			string bootTime = output.substr(pos + bootTimeLabel.length(), endPos - pos - bootTimeLabel.length());
			bootTime.erase(std::remove(bootTime.begin(), bootTime.end(), ' '), bootTime.end());
			bootTime.erase(std::remove(bootTime.begin(), bootTime.end(), '\r'), bootTime.end());
			return bootTime;
		}
	}
	return "BootTimeNotFound";
#else
	// Linux 实现：读取 /proc/stat 获取启动时间戳，转换为本地时间
	// 步骤1：读取系统启动时间（自开机以来的秒数）
	std::string uptime_str = execCommand("cat /proc/uptime | awk '{print $1}'");
	double uptime_seconds = atof(uptime_str.c_str());
	if (uptime_seconds <= 0) {
		return "BootTimeNotFound";
	}

	// 步骤2：获取当前时间戳（秒），减去开机时长 = 启动时间戳
	time_t now = time(nullptr);
	time_t boot_timestamp = now - (time_t)uptime_seconds;

	// 步骤3：转换为本地时间字符串（格式和 Windows 对齐，如：2026-01-13 09:00:00）
	struct tm boot_tm;
	localtime_r(&boot_timestamp, &boot_tm); // 线程安全版本（替代 localtime）

	char boot_time_buf[64] = { 0 };
	// 格式化：年-月-日 时:分:秒（和 Windows systeminfo 输出格式兼容）
	strftime(boot_time_buf, sizeof(boot_time_buf), "%Y-%m-%d %H:%M:%S", &boot_tm);

	return std::string(boot_time_buf);
#endif
}

PredictFunc dv_predict = NULL;

void loadDeepVersion() {
#ifdef _WIN32
	string deepVersionDllPath = fs::appPath() + "/DeepVision.dll";
	if (fs::fileExist(deepVersionDllPath)) {
		HMODULE hDLL = LoadLibrary(deepVersionDllPath.c_str());  // 请确保 DLL 文件路径正确
		if (hDLL == NULL) {
			DWORD errorCode = GetLastError();
			std::cerr << "Failed to load DLL!" << std::endl;
			return;
		}

		dv_predict = (PredictFunc)GetProcAddress(hDLL, "predict");
		if (dv_predict == NULL) {
			std::cerr << "Failed to get predict function!" << std::endl;
		}
	}
#else
	// TODO: add Linux .so
#endif
}

void loadDataSimu() {
	// 获取所有 MP 对象
	map<string, MP*> mapAllMP;
	prj.getMpList(mapAllMP);

	// 读取保存的 JSON 文件
	string simuConfPath = charCodec::utf8_to_gb(tds->conf->confPath + "/DataSimu.json");
	yyjson_read_err err;
	yyjson_doc* doc = yyjson_read_file(simuConfPath.c_str(), YYJSON_READ_NOFLAG, nullptr, &err);

	if (!doc) {
		return;
	}

	yyjson_val* root = yyjson_doc_get_root(doc);

	// 遍历 JSON 数组
	size_t idx, max;
	yyjson_val* item;
	yyjson_arr_foreach(root, idx, max, item) {
		if (yyjson_is_obj(item)) {
			// 获取 tag
			const char* tag = yyjson_get_str(yyjson_obj_get(item, "tag"));
			if (!tag) {
				continue;
			}

			// 查找对应的 MP 对象
			auto it = mapAllMP.find(tag);
			if (it != mapAllMP.end()) {
				MP* pmp = it->second;
				if (pmp) {
					// 更新 MP 对象的模拟配置
					yyjson_val* lowLimitVal  = yyjson_obj_get(item, "lowLimit");
					yyjson_val* highLimitVal = yyjson_obj_get(item, "highLimit");
					yyjson_val* enableVal = yyjson_obj_get(item, "enable");
					yyjson_val* intervalVal = yyjson_obj_get(item, "interval");

					if (yyjson_is_real(lowLimitVal)) {
						pmp->m_simuConf.lowLimit = yyjson_get_real(lowLimitVal);
					}

					if (yyjson_is_real(highLimitVal)) {
						pmp->m_simuConf.highLimit = yyjson_get_real(highLimitVal);
					}

					if (yyjson_is_bool(enableVal)) {
						pmp->m_simuConf.enable = yyjson_get_bool(enableVal);
					}

					if (yyjson_is_bool(intervalVal)) {
						pmp->m_simuConf.interval = yyjson_get_int(intervalVal);
					}
				}
			}
		}
	}

	// 释放 JSON 文档
	yyjson_doc_free(doc);
}

void runDataSimu() {
	g_pDataSimu = new CDataSimu();
	g_pDataSimu->startDataSimu();
}

void openAllStream() {
	map<string, MP*> mapAllMP;
	prj.getMpList(mapAllMP);
	for (auto& pair : mapAllMP) {
		MP* pmp = pair.second;
		if (pmp && pmp->m_mediaUrl != "") {
			string tag = pmp->getTag();
			streamSrv.openStream(tag);
		}
	}
}

bool TDS_imp::run(string cmdline) {
	mg_log_set(MG_LL_NONE);

	fs::createFolderOfPath(fs::appPath() + "/temp");

#ifndef DEBUG
 // mg_log_set("0");
#endif

	TIME exeTime = timeopt::now(); 
	g_strStartupTime = exeTime.toStr(false);

#ifdef _WINDLL // dll模式下需要创建命令行
	tds->conf->mode = "tds";
#endif

	logger.m_bSaveToFile = true;

	tds->conf->loadConf();
	tds->conf->loadCurrentData();

	tds->conf->bCallAsyn = tds->conf->getInt("callAsyn", 1);
	ioDev::m_pingInterval = tds->conf->getInt("pingInterval", 30000);
	RPC_SESSION::defaultSubAll = tds->conf->getInt("defaultSubAll", 1) == 1 ? true : false;

	//check mode
	if (conf->uiMode == "") {
		conf->uiMode = getUIMode();
	}

#ifdef _WINDLL // dll模式下需要创建命令行
	if (conf->uiMode == "console") {
		createConsole();
	}
#endif

	logger.m_bEnable = tds->conf->getInt("enableLog", 1) == 1 ? true : false;
	logger.m_strLogDir = tds->conf->logPath;
	logger.setLogLevel(tdsConf.logLevel);

	if (tds->conf->getInt("singleInst", 1)) {
		if (isTdsRunning()) {
#ifdef _WIN32
			printf(charCodec::tds_to_gb("tds已经运行，程序将于5秒后自动退出\r\n").c_str());
#else
			printf("tds已经运行，程序将于5秒后自动退出\r\n");
#endif

			LOG("tds已经运行，退出程序");

			timeopt::sleepMilli(5 * 1000);
			exit(0);
		}
	}

	string appName = fs::appName();

#ifdef USE_SVN_REV
	m_sTitle = appName + " v" + SVN_VERSION + " b" + getbuildtimeShort();
#else
	m_sTitle = appName + " " + version + "(" + getbuildtime() + ")";
#endif

	LOG("[keyinfo]服务启动,版本:%s", m_sTitle.c_str());
	LOG("[日志      ] 记录等级:" + tdsConf.logLevel + ",日志文件路径:" + logger.m_strLogDir);

	string navIniPath = tds->conf->confPath + "/nav.ini";
	if (fs::fileExist(navIniPath)) {
		string s;
		fs::readFile(navIniPath, s);

		if (!str::isUTF8(s)) {
			string u8s = charCodec::gb_to_utf8(s);
			fs::writeFile(navIniPath, u8s);

			LOG("[warn]nav.ini非utf8编码，进行gb2312->utf8转换");
		}
	}


	//指定配置路径没有配置文件夹，则新建
	if (tds->conf->confPath == fs::appPath() + "/conf") {
		if (!fs::fileExist(tds->conf->confPath)) {
			fs::createFolderOfPath(tds->conf->confPath);

			LOG("[keyinfo]配置路径未找到配置文件夹,新建配置,路径:" + tds->conf->confPath);
		}
	}

	LOG("[UI路径	] " + tds->conf->uiPath);
	LOG("[组态路径	] " + tds->conf->confPath);
	LOG("[数据库	] " + tds->conf->dbPath);

	rpcHandler_common.m_confPath = tds->conf->confPath;
	rpcHandler_common.m_dbPath   = tds->conf->dbPath;
	rpcHandler_common.m_fmsPath  = tds->conf->fmsPath;
	rpcHandler_common.m_appPath  = fs::appPath();

	//初始化系统组件，完成静态结构建立。loadConf和init类函数。在调用run之前，要先完成.否则在结构建立之前就进行数据io，可能会出现一些不必要的错误。

	//startup tds modules
	//if db folder is not exist. open will create an empty folder
	//先初始化数据库。 mo和io的初始化都可能从数据库中加载数据 。
	//ioSrv会从数据库加载设备配置缓存数据
	if (tds->conf->enableDB) {
		::db.m_timeUnit        = (DB_TIME_UNIT)tds->conf->getInt("dbTimeUnit", 1);
		::db.m_dbFmt.dbRootTag = tds->conf->getStr("dbRootTag", "");

		bool bCheckEnd = false;

		//1,有配置就用配置来
		//2,没配置做检查
		string deListName = tds->conf->getStr("deListName", "");

		DB_FMT db_Fmt;
		if (deListName == "") {
			checkDBFormat(tds->conf->dbPath, bCheckEnd, db_Fmt);
		}

		//::db.m_dbFmt.deListName = db_Fmt.deListName == "" ? tds->conf->getStr("deListName", "db.json") : db_Fmt.deListName;
		::db.m_dbFmt.deListName = deListName == "" ? db_Fmt.deListName : deListName;
		::db.m_dbFmt.curveIdxListName  = db_Fmt.curveIdxListName  == "" ? tds->conf->getStr("curveIdxListName", "db.curve.json") : db_Fmt.curveIdxListName;
		::db.m_dbFmt.curveDeNameSuffix = db_Fmt.curveDeNameSuffix == "" ? tds->conf->getStr("curveDeNameSuffix", ".curve.json") : db_Fmt.curveDeNameSuffix;
		::db.m_dbFmt.deItemKey_value   = db_Fmt.deItemKey_value   == "" ? tds->conf->getStr("deItemKey_value", "val") : db_Fmt.deItemKey_value;

		::db.Open(tds->conf->dbPath, g_getTagsByTagSelector, prj.m_name);

		::db.m_confPath      = tds->conf->confPath;
		::db.m_currentPath   = tds->conf->currentPath;
		::db.m_bEnableFsBuff = tds->conf->getInt("dbBuffer", 0) == 1 ? true : false;
		::db.m_enableDB = tds->conf->getInt("enableDB", 1) == 1 ? true : false;

		string recPath = tds->conf->dbPath + "/record";
		fs::createFolderOfPath(recPath);
	}
		
	prj.loadObjTemplate();
	prj.loadConfFile();
	prj.getAllVarExpScript();

	ioSrv.loadConf();

	ioDev::m_offlineConfirmCount = tds->conf->getInt("offlineConfirmCount", 1);

	//报警服务相关参数与接口
	AsInitParam asInitParam;
	asInitParam.confPath                       = tds->conf->confPath;
	asInitParam.enableGlobalAlarm              = tds->conf->enableGlobalAlarm;
	asInitParam.func_obj_setJAlmStatus         = funcImp_obj_setJAlmStatus;
	asInitParam.func_obj_getTypeTagByTag       = funcImp_obj_getTypeTagByTag;
	asInitParam.func_log                       = funcImp_log;
	asInitParam.func_rpcHand_notify            = funcImp_rpcHand_notify;
	asInitParam.func_sms_notify                = funcImp_sms_notify;
	asInitParam.func_usrMng_checkTagPermission = funcImp_usrMng_checkTagPermission;

	//报警
	almSrv.m_dbFileMode = ONE_FILE_PER_MONTH;

#ifdef ENABLE_ALM_SRV_HOOK_SCRIPT
	almSrv.m_scriptBeforeUpdateAlarm = tds->conf->getStr("scriptBeforeUpdateAlarm", "");
#endif

	almSrv.init(::db.m_path + "/alarms", asInitParam);
	almSrv.tableCurrent.unAckListSizeLimit = tds->conf->getInt("unAckListSizeLimit", 10);

	//
	userMng.init();
	ScriptEngine::callMethodImp = callMethod;
	ScriptEngine::ScriptFolder = tds->conf->confPath + "/scripts";
	ScriptEngine::ConfFolder = tds->conf->confPath;
	ScriptEngine::init();
	scriptManager.setConfPath(tds->conf->confPath);
	scriptManager.m_reloadFile = tds->conf->getInt("reloadScriptFile", 0);
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

	for(auto& i:tds->plugins) {
		i.second->init();
	}

	//加载deepVersion
	loadDeepVersion();

	//开始运行，与外部建立通讯并进行数据io
	runWebServers();

	SOCK_SRV_CONF ssc;
	ssc.masterTdsAddrs  = tds->conf->getStr("masterTds", "");
	ssc.childTdsIP      = tds->conf->getStr("childTdsIP", "");
	ssc.tcpSrvPort      = tds->conf->getInt("tcpPort", 670);
	ssc.udpSrvPort      = tds->conf->getInt("udpPort", 0);
	ssc.tcpKeepAliveSec = tds->conf->tcpKeepAliveDS;
	ssc.tcpClientRegPkt = rpcSrv.getChildTdsRegPkt();


	sockSrv.m_pCallback       = onSockSrvCallback;
	sockSrv.m_pStatusCallback = onSockSrvStatusCallback;
	sockSrv.run(ssc);

	mqttSrv.run();

	ioSrv.run(); //先启动ioSrv加载io组态,再启动ds.如果先启动ds可能会把某些managed设备当作spare设备
	logSrv.run();
	scriptManager.run();
	userMng.run();

	//运行tds插件
	if (tds->xiaoT)
		tds->xiaoT->run();
	if (tds->smsServer)
		tds->smsServer->run();
	if (tds->gzhServer)
		tds->gzhServer->run();

	for (auto& i : tds->plugins)  {
		i.second->run();
	}

	ioSrv.updateTag2IOAddrBinding();
	taskSrv.run();

#ifdef _WIN32
	statusSrv.m_bLogStatus = tds->conf->getInt("logSrvStatus", 1);
	statusSrv.run();
#endif

	prj.loadRtDB();
	prj.runRtDB();

	//create browser window
	if (conf->uiMode == "chrome") {
		createChromeWnd();
	}

#ifndef _WINDLL
#ifdef _WIN32
	SetConsoleTitleW(charCodec::utf8_to_utf16(m_sTitle).c_str());
#endif
#endif

	g_ComputerStartupTime = getSystemBootTime();

	// 加载数据模拟配置
	loadDataSimu();

	//开启一个线程,进行数据仿真
	thread t(runDataSimu);
	t.detach();

	if(conf->alwaysOpenStream)
	{
		thread t_os(openAllStream);
		t_os.detach();
	}

	return true;
}

void TDS_imp::stop()
{
	sockSrv.stop();
	ioSrv.stop();
}

bool TDS_imp::setProcBeforeExit(fp_procBeforeExit callback)
{
	m_fpProcBeforeExit = callback;
	return true;
}

void TDS_imp::call(string method, json& param, json& err, json& rlt, RPC_SESSION session)
{
	if (tds->conf->logEnable.innerRPCCall) {
		printf("tds call,method=%s,params=%s", method.c_str(), param.dump().c_str());
	}
	try {
		RPC_RESP resp;
		rpcSrv.handleMethodCall(method, param, resp, session);
		if (resp.error != "")
		{
			if (tds->conf->logEnable.innerRPCCall) {
				printf("error=%s", resp.error.c_str());
			}
			err = json::parse(resp.error);
		}
		else{
			if (tds->conf->logEnable.innerRPCCall) {
				printf("result=%s", resp.result.c_str());
			}
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

void thread_handleRpcCall(string method,string sParam,int delay) {
	try
	{
		json param = json::parse(sParam);
		if (delay > 0) {
			timeopt::sleepMilli(delay);
		}

		RPC_SESSION session;
		RPC_RESP resp;
		rpcSrv.handleMethodCall(method, param, resp, session);
	}
	catch (const std::exception& e)
	{
		string errorType = e.what();
		//json库的 what 返回的字符串，本身可能是一个携带非utf8字符的字符串。这串错误描述可能包含了解析错误的那个字符,所以也非法。
		//全部转换为ascII，用转义字符表示。否则后面的jError.dump() 会奔溃
		errorType = str::encodeAscII(errorType);
		LOG("[error]thread_handleRpcCall处理异常:\r\n错误信息:" + errorType + "\r\n数据包:\r\n" + sParam);
	}
}


COMMON::ThreadPool g_asynCallDealThreadPool(8);
//ThreadPool g_asynCallDealThreadPool(8);

void TDS_imp::callAsyn(string method, json& param, int delay)
{
	string sp = param.dump();
	callAsyn(method, sp, delay);
}

void TDS_imp::callAsyn(string method, string& param, int delay)
{
	if (tds->conf->bCallAsyn) {
		if (method == "input") {
			g_asynCallDealThreadPool.enqueue([method, param, delay] {
				shared_lock<shared_mutex> lock(prj.m_csPrj);
				json jparam = json::parse(param);
				RPC_SESSION session;
				RPC_RESP resp;
				rpcSrv.rpc_input(jparam,resp,session);
				});
		}
		else {
			thread t(thread_handleRpcCall, method, param, delay);
			t.detach();
		}
	}
	else { //方便调试时观察堆栈
		thread_handleRpcCall(method, param, delay);
	}
}

void thread_handleBatchRpcCall(vector<json> calls, int delay) {
	if (delay > 0) {
		timeopt::sleepMilli(delay);
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
	rpcSrv.notify(method, jParams.dump());
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
#ifdef USE_SVN_REV
	string s = version + "." + SVN_VERSION;
#else
	string s = version;
#endif
	return s;
}

string TDS_imp::getSvnVersion() {
	string s;
#ifdef USE_SVN_REV
	s = SVN_VERSION;
#endif

	if (s != "")
		s += "_";

	s += "build " + getbuildtime();

	return s;
}

string TDS_imp::getServerID()
{
	return string();
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


i_tds* getTds() {
	static TDS_imp inst;
	return &inst;
}

#ifdef TDSDLL
i_tds* getITDS() {
	HMODULE hMod = LoadLibrary("tds.dll");
	if (hMod)
	{
		fp_getTds pGetTds = (fp_getTds)GetProcAddress(hMod, "getTds");
		if (pGetTds)
		{
			return pGetTds();
		}
	}
	return NULL;
}
#endif

void callMethod(string method, string param, string& rlt, string& err) {
	RPC_RESP resp;
	tds->call(method, param, resp);
	rlt = resp.result;
	err = resp.error;
}