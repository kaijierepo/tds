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


#pragma once
#include "tds.h"
#include "conf.h"
#include "mp.h"

class TDS_imp : public iTDS {
public:
	TDS_imp();


	string getVer() {
		return "1.0.0";
	}

	bool setEncodeing(string encoding);//接口字符串传递使用的字符编码
	string getUIMode();
	bool setWorkingDir();
	bool run(string cmdline = "");
	//tds关闭时，一定要快速关闭666端口，因为如果由于某些原因tds延迟关闭，但是依然占用666端口
	//此时用户以为程序已经退出，再次打开程序。新打开的程序由于666端口被占用而没有启动服务。
	//但是如果启动了chromeUI，ui依然可能从尚未关闭的前一个进程获取到一些web页面，让人误以为后一个tds服务启动成功了。
	void stop();
	bool setProcBeforeExit(fp_procBeforeExit callback);

	// tds 数据服务功能
	 bool call(string method, string param, RPC_RESP& resp);
	 void setRpcHandler(fp_rpcHandler handler);
	 void rpcNotify(string method, string params="", string sessionId="");

	// io 通信服务功能
	 bool enableIoLog(string ioAddr, bool bEnable);
	 bool sendToIoAddr(string ioAddr, const char* p, int l);
	 bool connectDev(string ioAddr);
	 bool isOnline(string ioAddr);
	 bool isConnected(string ioAddr);
	 bool isInUse(string ioAddr);
	 bool lockIoAddr(string ioAddr) ;
	 bool unlockIoAddr(string ioAddr) ;
	 bool setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback);

	 // 视频功能
	 void startStream(string streamId, STREAM_INFO* si = NULL);
	 void pushStream(string streamId, char* pData, int len, STREAM_INFO* si = NULL);
	 void pullStream(string streamId, void* user, fp_onVideoStreamRecv onRecvStream, STREAM_INFO* si = NULL);
	 void log(const char* text);

	 tdsConfig tdsConf;
	 

	 void registerMsgSinker(fp_msgSinker sinker);
	 void publishMsg(MODULE_BUS_MSG& msg);
	 vector<fp_msgSinker> m_msgSinkers;
};

class CDumpCatch
{
public:
	CDumpCatch();
	~CDumpCatch();

private:
	static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI TempSetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
	static BOOL ReleaseDumpFile(const std::string& strPath, EXCEPTION_POINTERS* pException);
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

extern void createConsole();
extern TDS_imp tdsImp; //tds instance;
extern iTDS* tds;