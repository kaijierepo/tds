#include "pch.h"
#include "tcpHub.h"
#include "logger.h"
#include "common.h"
#include "kvIni.h"


string tcpHub::defaultConf() 
{
	string s = R"(#tcpHub TCP数据转发工具
#基本配置
enable_pkt_log=1       #是否启用数据包日志，如果转发码流等大数据，可以关闭

#左侧配置
left_s_port=0          #左侧服务器模式端口
left_c_ip=0            #左侧客户端模式连接的服务器ip
left_c_port=0          #左侧客户端模式连接的服务器端口
left_reg_pkt=          #左侧Tcp连接建立时的首发包

#右侧配置
right_s_port=0          #右侧服务器模式端口
right_c_ip=0            #右侧客户端模式连接的服务器ip
right_c_port=0          #右侧客户端模式连接的服务器端口
right_reg_pkt=RS485_001         #右侧Tcp连接建立时的首发包
)";

	s = str::replace(s, "\n", "\r\n");
	return s;
}

bool tcpHub::loadConf()
{
	return false;
}

void tcpHub::run()
{
	logInText = false; 

	string confPath = fs::appPath() + "/tcpHub.ini";
	if (!fs::fileExist(confPath))
	{
		string s = defaultConf();
		fs::writeFile(confPath, s);
	}
	
	KV_INI tdsIni;
	tdsIni.load(confPath);

	enable_pkt_log = tdsIni.getValInt("enable_pkt_log", 1) ? true : false;

	left_s_port = tdsIni.getValInt("left_s_port", 0);
	left_c_port = tdsIni.getValInt("left_c_port", 0);
	left_c_ip = tdsIni.getValStr("left_c_ip", "");
	left_reg_pkt = tdsIni.getValStr("left_reg_pkt", "");

	right_s_port = tdsIni.getValInt("right_s_port", 0);
	right_c_port = tdsIni.getValInt("right_c_port", 0);
	right_c_ip = tdsIni.getValStr("right_c_ip", "");
	right_reg_pkt = tdsIni.getValStr("right_reg_pkt", "");

	//left side
	if (left_s_port != 0)
	{
		bool ret = sLeft.run(this, left_s_port);
		if (ret)
		{
			LOG("Left Server at port " + str::fromInt(left_s_port) + "   start sucess");
		}
		else
		{
			LOG("Left Server at port " + str::fromInt(left_s_port) + "   start fail");
		}
	}
	if (left_c_port != 0 && left_c_ip != "") {
		bool ret = cLeft.run(this,left_c_ip, left_c_port);
		if (ret)
		{
			LOG("Left Client at addr %s:%d start success", left_c_ip.c_str(), left_c_port);
		}
		else
		{
			LOG("Left Client at addr %s:%d start fail", left_c_ip.c_str(), left_c_port);
		}
	}


	//right side
	if (right_s_port != 0)
	{
		bool ret = sRight.run(this, right_s_port);
		if (ret)
		{
			LOG("Right Server at port " + str::fromInt(right_s_port) + "   start sucess");
		}
		else
		{
			LOG("Right Server at port " + str::fromInt(right_s_port) + "   start fail");
		}
	}
	if (right_c_port != 0 && right_c_ip != "") {
		bool ret = cRight.run(this, right_c_ip, right_c_port);
		if (ret)
		{
			LOG("Right Client at addr %s:%d start success",right_c_ip.c_str(),right_c_port);
		}
		else
		{
			LOG("Right Client at addr %s:%d start fail", right_c_ip.c_str(), right_c_port);
		}
	}
}

void tcpHub::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
	if (bIsConn)
	{
		if (pCltInfo->pTcpServer == &sLeft)
		{
			LOG("Left Server " + str::fromInt(left_s_port) + ": " + pCltInfo->remoteIP + " connected");
			if (left_reg_pkt.length() > 0) {
				pCltInfo->send(left_reg_pkt.data(), left_reg_pkt.length());
				LOG("首发包:" + left_reg_pkt);
			}
		}
		else if (pCltInfo->pTcpServer == &sRight)
		{
			LOG("Right Server " + str::fromInt(right_s_port) + ": " + pCltInfo->remoteIP + " connected");
			if (right_reg_pkt.length() > 0) {
				pCltInfo->send(right_reg_pkt.data(), right_reg_pkt.length());
				LOG("首发包:" + right_reg_pkt);
			}
		}
	}
	else
	{
		if (pCltInfo->pTcpServer == &sLeft)
		{
			LOG("Left Server " + str::fromInt(left_s_port) + ": " + pCltInfo->remoteIP + " disconnected");
		}
		else if (pCltInfo->pTcpServer == &sRight)
		{
			LOG("Right Server " + str::fromInt(right_s_port) + ": " + pCltInfo->remoteIP + " disconnected");
		}
	}
	
}

void tcpHub::onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pCltInfo)
{
	if (pCltInfo->pTcpServer == &sLeft)
	{
		sRight.SendData((char*)pData, iLen);
		cRight.SendData(pData, iLen);
		if (enable_pkt_log) {
			string sData = str::bytesToHexStr(pData, iLen);
			LOG(" --> (%d) %s", iLen, sData.c_str());
		}
	}
	else if (pCltInfo->pTcpServer == &sRight)
	{
		sLeft.SendData((char*)pData, iLen);
		cLeft.SendData(pData, iLen);
		if (enable_pkt_log) {
			string sData = str::bytesToHexStr(pData, iLen);
			LOG(" <-- (%d) %s", iLen, sData.c_str());
		}
	}
}

void tcpHub::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
	if (bIsConn)
	{
		if (connInfo->pTcpClt == &cLeft) {
			LOG("Left Client to %s:%d connected",left_c_ip.c_str(),left_c_port);
			if (left_reg_pkt.length() > 0) {
				cLeft.SendData(left_reg_pkt.data(), left_reg_pkt.length());
				LOG("首发包:" + left_reg_pkt);
			}
		}
		else if (connInfo->pTcpClt == &cRight) {
			LOG("Right Client to %s:%d connected", right_c_ip.c_str(), right_c_port);
			if (right_reg_pkt.length() > 0) {
				cRight.SendData(right_reg_pkt.data(), right_reg_pkt.length());
				LOG("首发包:" + right_reg_pkt);
			}
		}
	}
	else {
		if (connInfo->pTcpClt == &cLeft) {
			LOG("Left Client to %s:%d disconnected", left_c_ip.c_str(), left_c_port);
		}
		else if (connInfo->pTcpClt == &cRight) {
			LOG("Right Client to %s:%d disconnected", right_c_ip.c_str(), right_c_port);
		}
	}
}

void tcpHub::onRecvData_tcpClt(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo)
{
	if (connInfo->pTcpClt == &cLeft)
	{
		sRight.SendData((char*)pData, iLen);
		cRight.SendData(pData, iLen);
		if (enable_pkt_log) {
			string sData = str::bytesToHexStr(pData, iLen);
			LOG(" --> (%d) %s", iLen, sData.c_str());
		}
	}
	else if (connInfo->pTcpClt == &cRight)
	{
		sLeft.SendData((char*)pData, iLen);
		cLeft.SendData(pData, iLen);
		if (enable_pkt_log) {
			string sData = str::bytesToHexStr(pData, iLen);
			LOG(" <-- (%d) %s", iLen, sData.c_str());
		}
	}
}
