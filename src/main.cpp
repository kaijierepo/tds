#include "pch.h"
#include "conf.h"
#include "cmdparser.hpp"
#include "video/remoteDesktopServer.h"
#include "cmdparser.hpp"
#include "logger.h"
#include "tds_imp.h"
#include "wke.h"
#include "res/resource.h"

/*
notes:
all string data in memory is utf8 format 

design problem:
> mutithread accessing element in a dynamic list
  1.shared points

代码不安全，未来需优化的地方，全局搜索 [unsafe]

*/

#include "ioDev_mqttBroker.h"

class TDS_imp;
TDS_imp tdsImp; //tds instance;
iTDS* tds = &tdsImp;
wkeWebView m_hUI;


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
		int w = LOWORD(lParam);
		int h = HIWORD(lParam);
		wkeResize(m_hUI, w, h);
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}



void createUIWnd()
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
	int w = 800;
	int h = 600;

	//创建窗口
	HWND hwnd = CreateWindow(
		"tdsUI",           //上面注册的类名，要完全一致  
		"",                     //窗口标题文字  
		WS_OVERLAPPEDWINDOW, //窗口外观样式  
		x,             //窗口相对于父级的X坐标  
		y,             //窗口相对于父级的Y坐标  
		w,                //窗口的宽度  
		h,                //窗口的高度  
		NULL,               //没有父窗口，为NULL  
		NULL,               //没有菜单，为NULL  
		hInstance,          //当前应用程序的实例句柄  
		NULL);              //没有附加数据，为NULL 

	string s = sys::getLastError();

	ShowWindow(hwnd, SW_SHOW);


	m_hUI = wkeCreateWebWindow(WKE_WINDOW_TYPE_CONTROL, hwnd, 0, 0, w, h);
	wkeShowWindow(m_hUI, TRUE);


	wkeLoadURL(m_hUI, "http://localhost:666/terminal/");
}



int main(int argc, char** argv)
{
	//use cmd line conf first ,or use tds.json 
	cli::Parser parser(argc, argv);
	parser.set_optional<int>("p", "port", 0, "Integers in all forms, e.g., unsigned int, long long, ..., are possible. Hexadecimal and Ocatl numbers parsed as well");
	parser.set_optional<bool>("d", "debug", false, "run in debug mode. heartbeat will be closed;more log will be added;");
	parser.set_optional<string>("l", "loglevel", "debug", "value can be detail,trace,debug,warn,error");
	parser.run_and_exit_if_error();
	tds->conf->port = parser.get<int>("p");
	tds->conf->debugMode = parser.get<bool>("d");
	tds->conf->logLevel = parser.get<string>("l");

	tds->run();

	//create browser window
	if (fs::fileExist(fs::appPath() + "\\miniblink_x64.dll"))
	{
		wkeSetWkeDllPath(L"miniblink_x64.dll");
		wkeInitialize();
		createUIWnd();
	}

	/*
	while (1)
	{
		Sleep(1000);
	}*/
	// 消息循环  
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}


#define DllExport   extern "C" __declspec( dllexport )
DllExport iTDS* getTds() {
	return &tdsImp;
}






