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
#include "tdsConf.h"
#include "logger.h"
#include "tds_imp.h"
#include "tools/tcpHub.h"
#include "tools/tcpSwitch.h"
#include "tools/tcpReverseProxy.h"
#include "tools/tdsWatchDog.h"
#include "tools/rproxy.h"
#include "tools/tcp2wsRproxy.h"
#include "tools/httpServer.h"
#include "db.h"
#include "tools/tools.hpp"
#include "base64.h"
#include "prj.h"
#include "common.h"
#include "tools/demoTools.h"
#include "tools/dumpCatch.h"
#include "tools/aliDDNS.h"
#include "rpcHandler.h"


void updateEzvizAccessInfo() {
	try {
		for (auto& i : prj.m_mapEzvizAccess) {
			EZVIZ_ACCESS_INFO& info = i.second;

			//半天更新一次
			if (timeopt::CalcTimePassSecond(info.lastUpdate) < 12 * 60 * 60) {
				continue;
			}

			string addr = "https://open.ys7.com";
			httplib::Client cli(addr);
			cli.enable_server_certificate_verification(false);

			//更新token。 token默认过期时间7天
			httplib::Params params = {
				{"appKey",info.appKey},
				{"appSecret",info.secret}
			};

			auto resp = cli.Post("/api/lapp/token/get", params);
			if (resp != nullptr) {
				json jResp = json::parse(resp->body);
				json jData = jResp["data"];
				if (jData != nullptr) {
					json jAccessToken = jData["accessToken"];
					if (jAccessToken != nullptr) {
						info.token = jAccessToken.get<string>();
					}
				}
			}

			//获取新的flvurl。 url默认过期时间 1天
			params = {
				{"accessToken",info.token},
				{"deviceSerial",info.serialNo},
				{"protocol","4"} //flv协议
			};

			resp = cli.Post("/api/lapp/v2/live/address/get", params);
			if (resp != nullptr) {
				json jResp = json::parse(resp->body);
				json jData = jResp["data"];
				if (jData != nullptr) {
					json jUrl = jData["url"];
					if (jUrl != nullptr) {
						info.flvUrl = jUrl.get<string>();
					}
				}
			}


			params = {
				{"accessToken",info.token},
				{"deviceSerial",info.serialNo},
				{"protocol","1"} //ezopen协议
			};

			resp = cli.Post("/api/lapp/v2/live/address/get", params);
			if (resp != nullptr) {
				json jResp = json::parse(resp->body);
				json jData = jResp["data"];
				if (jData != nullptr) {
					json jUrl = jData["url"];
					if (jUrl != nullptr) {
						info.ezopenUrl = jUrl.get<string>();
						info.lastUpdate = timeopt::now();

						LOG("[萤石云] 接入信息更新tag=%s,serialNo=%s,token=%s,ezopenUrl=%s", info.tag.c_str(), info.serialNo.c_str(), info.token.c_str(), info.ezopenUrl.c_str());
					}
				}
			}
			else {
				auto err = resp.error();
				LOG("[warn][萤石云] 请求错误:%s", httplib::to_string(err).c_str());
			}
		}
	}
	catch (exception& e) {
		LOG("[warn] updateEzvizAccessInfo  exception:%s", e.what());
	}
}

/*
notes:
all string data in memory is utf8 format 

design problem:
> mutithread accessing element in a dynamic list
  1.shared points

代码不安全，未来需优化的地方，全局搜索 [unsafe]
需要改进的问题  全局搜索[问题]
*/

/*
TDS是一个数据服务
数据由数据的生产者ioDev(IO设备)提供给TDS,tdsClt(tds客户端)作为数据的使用者
ioDev虽然一般以tcpClient的方式连接到tds. 但相对于tds来说,设备被看作是服务端

*/


//exe模式下，都会有命令行窗口，通过设置 ui = chrome 或者 miniblink打开 浏览器窗口
//dll模式下，默认没有命名行窗口，通过设置 ui = console 来打开命令行窗口


#ifndef _WINDLL
//命令行参数调用 ，js解释器等模式需要默认控制台，因此默认控制台不隐藏
//terminal模式等需要隐藏，使用其他方式隐藏
//#pragma comment( linker, "/subsystem:windows /entry:mainCRTStartup" )//不显示默认控制台

#include "ioProto/proto_bacnet.hpp"

int main(int argc, char** argv)
{
	//取出命令行命令
	vector<string> args;
	for (int i = 0; i < argc; i++) {
		string s = argv[i];
		args.push_back(s);
	}

	setThreadName("main thread");
	tds->conf = &tdsImp.tdsConf;
	
	//确认程序运行模式
	string appName = fs::appName();
	string mode = "tds";
	if (appName == "tds") {
		if(args.size() > 1)
		   mode = args[1];   //第一个参数为运行模式
		args.erase(args.begin());
	}
	else {
		mode = appName;
	}
	
	//设置当前路径为程序运行目录 tdsConf.loadConf();中的相对路径解析会用到当前路径
	tdsImp.setWorkingDir();

	//根据模式差异化加载配置
	tdsImp.tdsConf.mode = mode;
	tdsImp.tdsConf.loadConf();

	//专业版创建授权文件
	if (tds->createLicence)
	{
		tds->createLicence();
	}



	if (mode == "watchDog" || mode == "wd" || mode == "dog" || mode == "tdsd")
	{
		/*if (args.size() > 1) {
			if (args[1] == "unreg") {
				printf(_GB("是否从开机启动项删除? (y/n)"));
				char c = getchar();
				if (c == 'y' || c == 'Y') {
					watchDog.unregSelfStart();
				}
				return 0;
			}
			else if (args[1] == "reg") {
				printf(_GB("是否添加到开机启动? (y/n)"));
				char c = getchar();
				if (c == 'y' || c == 'Y') {
					watchDog.regSelfStart();
					return 0;
				}
			}
		}
		else {
			if (!watchDog.isSelfStartReg()) {
				printf(_GB("是否添加到开机启动? (y/n)"));
				char c = getchar();
				if (c == 'y' || c == 'Y') {
					watchDog.regSelfStart();
					return 0;
				}
				printf(_GB("tdsd未添加到开机自启动\r\n使用 tdsd reg/unreg 命令行添加或删除自启动\r\n"));
			}
			else
				printf(_GB("tdsd已添加到开机自启动\r\n使用 tdsd reg/unreg 命令行添加或删除自启动\r\n"));
		}*/
		watchDog.run();
	}
	else if (mode == "testconf") {
		gen_testMoConf();
		gen_testIoConf();
		return 0;
	}
	else if (mode == "testdb") {
		gen_testdb();
		return 0;
	}
	else if (mode == "dump") {
		if (args.size() >= 3) {
			string name = args[1];
			string path = args[2];
			CDumpCatch::createDump(name, path);
		}
		return 0;
	}
	else if (mode == "tcpHub")
	{
		tcpHub* tr = new tcpHub();
		tr->run();
	}
#ifdef ENABLE_TOOLS
	else if (mode == "js")
	{
		//doShell();
	}
	else if (mode == "hs" || mode == "httpServer" || mode == "httpserver" || mode == "httpSrv" || mode == "httpsrv") //httpServer
	{
		HttpServer* hs = new HttpServer();
		hs->m_ProcName = mode;
		hs->run();
	}
	else if (mode == "switch")
	{
		tcpSwitch* tr = new tcpSwitch();
		tr->run();
	}
	else if (mode == "rphttp")
	{
		rpProxy  = new RProxy();
		rpProxy->run();
	}
	else if (mode == "rptcp")
	{
		tcpReverseProxy* tr = new tcpReverseProxy();
		tr->run();
	}
	else if (mode == "tcp2ws")
	{
		Tcp2wsRproxy* tr = new Tcp2wsRproxy();
		tr->run();
	}
	else if (mode == "tcp2com")
	{
		fp_toolRun ptr = tds->tools["tcp2com"];
		if (ptr) ptr();
	}
	else if (mode == "replace") {
		//.rc文件为gb2312编码
		//string path = parser.get<string>("path");
		//string oldstr = parser.get<string>("os");
		//string newstr = parser.get<string>("ns");
		//Tools::replaceStrInFile(path, oldstr, newstr);
		exit(0);
	}
	else if (mode == "gb2u8")
	{
		vector<string> fileList;
		fs::getFileList(fileList, fs::appPath());
		LOG("文件总数:" + str::fromInt(fileList.size()));
		try {
			for (int i = 0; i < fileList.size(); i++)
			{
				string p = fs::appPath() + "/" + fileList[i];
				if (p.find(".h") == string::npos && p.find(".cpp") == string::npos)
				{
					continue;
				}

				string gbData;
				fs::readFile(p, gbData);
				if (charCodec::hasGB2312(gbData))
				{
					string u8Data = charCodec::gb_to_utf8(gbData);
					fs::writeFile(p, u8Data);
					LOG("已转换:" + p);
				}
				else
				{
					LOG("未找到GB2312字符:" + p);
				}
			}
		}
		catch (exception& e)
		{
			string es = e.what();
			LOG("转换异常:" + es);
		}
		return 0;
	}
	else if (mode == "base64") {
		if (args.size() != 3) {
			printf("参数错误");
			return 0;
		}

		if (args[1] == "enc") {
			char* pFile = nullptr;
			int len;
			if (fs::readFile(args[2], pFile, len) ){
				char* out = new char[len * 2];
				memset(out, 0, len * 2);
				base64_encode((const unsigned char*)pFile, len,out);
				string s = out;
				fs::writeFile(args[2] + "base64_enc.txt", s);
			}
		}
		else if (args[1] == "dec") {
			char* pFile = nullptr;
			int len;
			if (fs::readFile(args[2], pFile, len)) {
				unsigned char* out = new unsigned char[len * 2];
				memset(out, 0, len * 2);
				int outLen = base64_decode(pFile, len, out);
				fs::writeFile(args[2] + "base64_dec.bin",(char*)out,outLen);
			}
		}
		else {

		}
		return 0;
	}
	else if (mode == "base85") {
		//if (args.size() != 3) {
		//	printf("参数错误");
		//	return 0;
		//}

		//if (args[1] == "enc") {
		//	char* pFile = nullptr;
		//	int len;
		//	if (fs::readFile(args[2], pFile, len)) {
		//		char* dest = new char[10 * 1024 * 1024];
		//		memset(dest, 0, 10 * 1024 * 1024);
		//		 bintob85(dest,(void const*)pFile,len);
		//		 string encData = dest;
		//		 fs::writeFile(args[2] + "base85_enc.txt", encData);
		//	}
		//}
		//else {

		//}
		//return 0;
	}
#endif
	else
	{
		//run tds
		logger.m_bSaveToFile = true;
		tds->run();
	}


	aliDDNS.init();

	while (1)
	{
		sleep(1000);

		if (prj.m_enableEzviz) {
			prj.m_csPrj.lock_shared();
			vector<MP*> mps;
			prj.getMpList(mps);

			for (int i = 0; i < mps.size(); i++) {
				MP* pmp = mps[i];
				if (pmp->m_serialNo == "" ||
					pmp->m_appKey == "" ||
					pmp->m_secret == "")
					continue;

				if (pmp->m_valType == "video" && pmp->m_mediaSrcType == "ezviz") {
					if (prj.m_mapEzvizAccess.find(pmp->m_serialNo) == prj.m_mapEzvizAccess.end()) {
						EZVIZ_ACCESS_INFO  info;
						info.tag = pmp->getTag();
						info.serialNo = pmp->m_serialNo;
						info.appKey = pmp->m_appKey;
						info.secret = pmp->m_secret;
						info.token = "";
						timeopt::setAsTimeOrg(info.lastUpdate);
						prj.m_mapEzvizAccess[info.serialNo] = info;
					}
				}
			}
			prj.m_csPrj.unlock_shared();

			updateEzvizAccessInfo();
		}

		aliDDNS.doCycleTask();

		rpcSrv.cleanRpcSession();
	}
	return 0;
}












#else
#endif // !_WINDLL

#define DllExport   extern "C" __declspec( dllexport )
DllExport i_tds* getTds() {
	return &tdsImp;
}


//勿删除！！
//如果tds主程序中不使用_strdup ， strspn 这两个函数
// /MT 编译的openssl的crypto.lib会出现   这两个函数unresolved错误
//可能openssl编译的时候没有指定需要链接的lib. 主程序使用则产生了lib链接。原因不明。后续研究
void forLink() {
	char* a = new char[100];
	memset(a, 0, 100);
	string b = "abc";
	a = _strdup(b.c_str());
	size_t pos = strspn(a, "b");
	_strerror_s(a,100, "abcd");
}






