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

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#include "tds.h"
#include "tdsConf.h"
#include "mp.h"

namespace FUNC_TYPE {
	const std::string ChaoShengNoSignalCurveClassify = "ChaoShengNoSignalCurveClassify";  //曲线分类
	const std::string RailDamange = "RailDamage";                                         //伤损图像检测
	const std::string JM_A_Gap = "JM_A_Gap";                                              //JM-A缺口识别
	const std::string DiodeTubeAlarm = "DiodeTubeAlarm";                                  //二极管报警

	//扩展与兼容性
	const std::string DiodeTubeAlarm_V1 = "DiodeTubeAlarm_V1";                            //二极管报警 v1版本接口
	//定义v1版本的返回json结构
};

void callMethod(string method, string param, string& rlt, string& err);

class TDS_imp : public i_tds {
public:
	TDS_imp();
	bool setEncodeing(string encoding) override;//接口字符串传递使用的字符编码
	string getUIMode();

	bool run(string cmdline = "") override;
	//tds关闭时，一定要快速关闭666端口，因为如果由于某些原因tds延迟关闭，但是依然占用666端口
	//此时用户以为程序已经退出，再次打开程序。新打开的程序由于666端口被占用而没有启动服务。
	//但是如果启动了chromeUI，ui依然可能从尚未关闭的前一个进程获取到一些web页面，让人误以为后一个tds服务启动成功了。
	void stop() override;
	bool setProcBeforeExit(fp_procBeforeExit callback) override;

	// tds 数据服务功能
	 void call(string method, json& param, json& err, json& rlt ,RPC_SESSION session) override;
	 bool call(string method, string param, RPC_RESP& resp) override;
	 void callAsyn(string method, json& param, int delay = 0) override;
	 void callAsyn(string method, string& param,int delay = 0) override;
	 void batchCallAsyn(vector<json> calls, int delay) override;
	 void setRpcHandler(fp_rpcHandler handler) override;
	 void rpcNotify(string method, string params="", string sessionId="") override;

	// io 通信服务功能
	 bool enableIoLog(string ioAddr, bool bEnable) override;
	 bool sendToIoAddr(string ioAddr, const char* p, int l) override;
	 bool connectDev(string ioAddr) override;
	 string getVersion() override;
	 string getSvnVersion() override;
	 bool isOnline(string ioAddr) override;
	 bool isConnected(string ioAddr) override;
	 bool isInUse(string ioAddr) override;
	 bool lockIoAddr(string ioAddr) override;
	 bool unlockIoAddr(string ioAddr) override;
	 bool setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback) override;

	 // 视频功能
#ifdef ENABLE_GENICAM
	 void startStream(string streamId, STREAM_INFO* si = NULL);
	 void pushStream(string streamId, char* pData, int len, STREAM_INFO* si = NULL);
	 void pullStream(string streamId, void* user, fp_onVideoStreamRecv onRecvStream, STREAM_INFO* si = NULL);
#endif
	 void log(const char* text) override;
};

extern void createConsole();
extern string g_ComputerStartupTime;
extern string g_strStartupTime;

typedef bool (*LoadModelFunc)(const char*, const bool&);                             // load_model
typedef bool (*PredictFunc)(IN const char* funcType, IN const char* modelPath, IN void* inData, IN size_t inDataLen, OUT char* outData, OUT size_t& outDataLen, OUT char* errData, OUT size_t& errDataLen);   // predict
extern PredictFunc dv_predict;