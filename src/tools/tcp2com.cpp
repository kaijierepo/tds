#include "pch.h"
#include "tcp2com.h"
#include "logger.h"
#include "common.h"
#include "json.hpp"
#include "ioSrv.h"
#include "kvIni.h"
#include <WinSock2.h>

using json = nlohmann::json;

namespace ns_tcp2com {
	void run() {
		tcp2com* t2c = new tcp2com();
		t2c->run();
	}

	class Reg {
	public:
		Reg() {
			tds->tools["net2com"] = ns_tcp2com::run;
		}
	};

	Reg reg;
};



tcp2com::tcp2com()
{
	m_bLogToFile = true;
	m_iDestPort = 0;
	serial = createIODev(DEV_TYPE_local_serial);
}

void  serialRecvCallback(void* user, unsigned char* pData, size_t iLen)
{
	string netAddr;
	tcp2com* pt2c = (tcp2com*)user;
	if (pt2c->m_mode == "tcpClient") {
		pt2c->tcpClt.SendData(pData, iLen);
	}
	else if (pt2c->m_mode == "tcpServer") {
		pt2c->tcpServer.SendData((char*)pData, iLen);
	}
	else if (pt2c->m_mode == "udpServer") {
		netAddr = str::format("UDP-%s:%d(%d)", pt2c->m_strDestIp, pt2c->m_iDestPort,iLen);
		pt2c->udpServer.SendData(pData, iLen, pt2c->m_strDestIp, pt2c->m_iDestPort);
	}

	string log = str::bytesToHexStr(pData, iLen);
	LOG(pt2c->serial->getIOAddrStr() + " --> " + netAddr + "  " + log);
}

string tcp2com::defaultConf()
{
	string s = R"(#net2com 串口转网络透传网关配置
#串口参数
com=COM1
baudRate=19200
byteSize=8
stopBits=1
parity=None            #None Even Odd

#网络端参数
mode=tcpClient         #tcpClient:网关作为tcp客户端主动连接服务器。 tcpServer:网关作为服务器等待外部连接
registerPktStr={"method":"devRegister","ioAddr":"RS485Gateway_0001"}\n\n    #首发注册包。 ioAddr填入透传网关的地址

#客户端模式参数
remoteIP=127.0.0.1     #client模式下的服务器IP
remotePort=664         #client模式下的服务器端口。TDS的modbusRTU服务端口为664

#服务端模式参数
localIP=               #server模式下绑定的本地ip，留空为0.0.0.0
localPort=663          #server模式下的服务端口

)";
	return s;
}

string tcp2com::getTcpAddr()
{
	string s;
	if (m_mode == "tcpClient") {
		s = m_strDestIp + ":" + str::fromInt(m_iDestPort);
	}
	else if (m_mode == "tcpServer") {
		s = m_localIP + ":" + str::fromInt(m_localPort);
	}
	return s;
}


void tcp2com::run()
{
	string confPath = fs::appPath() + "/net2com.ini"; 
	if (!fs::fileExist(confPath))
	{
		string s = defaultConf();
		fs::writeFile(confPath, s);
	}

	KV_INI tdsIni;
	tdsIni.load(confPath);
	json jConf;
	jConf["com"] = tdsIni.getValStr("com","COM1");
	jConf["baudRate"] = tdsIni.getValInt("baudRate", 19200);
	jConf["parity"] = tdsIni.getValStr("parity","None");
	jConf["byteSize"] = tdsIni.getValInt("byteSize",8);
	jConf["stopBits"] = tdsIni.getValStr("stopBits","1");
	m_bLogToFile = tdsIni.getValInt("logToFile", 1);
	m_bLogToConsole = tdsIni.getValInt("logToConsole", 1);
	m_mode = tdsIni.getValStr("mode", "");
	m_strDestIp = tdsIni.getValStr("remoteIP","127.0.0.1");
	m_iDestPort = tdsIni.getValInt("remotePort", 664);
	m_localIP = tdsIni.getValStr("localIP", "0,0,0,0");
	m_localPort = tdsIni.getValInt("localPort", 663);
	registerPktStr = tdsIni.getValStr("registerPktStr", "");

	if (m_mode == "tcpClient")
	{
		if (m_strDestIp != "" && m_iDestPort != 0) {
			tcpClt.run(this, m_strDestIp, m_iDestPort);
			LOG("启动tcp客户端，服务器地址:%s:%d", m_strDestIp.c_str(), m_iDestPort);
		}
		else {
			LOG("tcp客户端参数错误");
		}
	}
	else if (m_mode == "tcpServer") {
		if (m_localPort != 0) {
			tcpServer.run(this,m_localPort, m_localIP);
			LOG("启动tcp服务端，服务器地址:%s:%d", m_localIP.c_str(), m_localPort);
		}
		else {
			LOG("tcp服务端参数错误");
		}
	}
	else if (m_mode == "udpServer") {
		if (m_localPort != 0) {
			udpServer.run(this, m_localPort, m_localIP);
			LOG("启动udp服务端，服务器地址:%s:%d", m_localIP.c_str(), m_localPort);
		}
		else {
			LOG("udp服务端参数错误");
		}
	}
	else {
		LOG("[error]请设置配置文件mode为tcpServer或者tcpClient");
		return;
	}



	if (serial->connect(jConf))
	{
		serial->setRecvCallback(this, serialRecvCallback);
		serial->run();
		LOG("打开串口成功: " + serial->getIOAddrStr());
		string comParam = "波特率:" + str::fromInt(jConf["baudRate"].get<int>()) + ",";
		comParam += "停止位:" + jConf["stopBits"].get<string>() + ",";
		comParam += "数据位:" + str::fromInt(jConf["byteSize"].get<int>()) + ",";
		comParam += "校验位:" + jConf["parity"].get<string>();
		LOG(comParam);
	}
	else
	{
		LOG("打开串口失败: " + jConf["com"].get<string>());
		getchar();
		exit(0);
	}
}

void tcp2com::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
	if (bIsConn)
	{
		
	}
	else
	{
		
	}
	
}

void tcp2com::OnRecvData_TCPServer(unsigned char* pData, size_t iLen, tcpSession* pCltInfo)
{
	serial->sendData(pData, iLen);
	string log = str::bytesToHexStr(pData, iLen);
	LOG(serial->getIOAddrStr() + " <-- " + getTcpAddr() + "  " + log);
}

void tcp2com::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
	if (bIsConn)
	{
		LOG("连接Tcp服务成功: " + m_strDestIp + ":" + str::fromInt(m_iDestPort));
		string s = str::parseEscapeChar(registerPktStr);
		if (s.length() > 0) {
			tcpClt.SendData((char*)s.c_str(), s.length());
			LOG("发送首发注册包,长度=%d,[%s]", s.length(), s.c_str());
		}
	}
	else
	{
		
	}
}

void tcp2com::OnRecvData_TCPClient(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo)
{
	serial->sendData(pData, iLen);
	string log = str::bytesToHexStr(pData, iLen);
	LOG(serial->getIOAddrStr() + " <-- " + getTcpAddr() + "  " + log);
}

void tcp2com::OnRecvUdpData(unsigned char* pData, size_t iLen, std::string strIP, int port)
{
	serial->sendData(pData, iLen);
	string log = str::bytesToHexStr(pData, iLen);
	string s = str::format("UDP-%s:%d(%d)", strIP.c_str(), port,iLen);
	LOG(serial->getIOAddrStr() + " <-- " +  s + "  " + log);
}
