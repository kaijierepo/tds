#pragma once
#include "tcpSrv.h"
#include "tcpClt.h"
#include "udpSrv.h"
#include <memory>
#include "ioDev.h"


struct tcp2com_Conf {
	string mode;
	string com;
	int baudRate;
	int	byteSize;
	string stopBits;
	string parity;
	string remoteIP;
	int remotePort;
	int localPort;

};

class tcp2com : public  ICallback_tcpSrv ,public ICallback_tcpClt,public ICallback_udpSrv{
public:
	tcp2com();

	tcpSrv tcpServer;
	tcpClt tcpClt;
	udpServer udpServer;

	ioDev* serial;

	bool m_bLogCommPktAsStr;
	bool m_bLogToFile;
	bool m_bLogToConsole;
	string m_mode;
	string m_localIP;
	int m_localPort;
	int m_iDestPort;
	string m_strDestIp;

	string defaultConf();

	string getTcpAddr();

	void run();

	 void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	 void onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pCltInfo) override;

	 void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
	 void onRecvData_tcpClt(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo) override;

	 void OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession) override;

	 string registerPktStr;
	 string registerPktHexStr;
};