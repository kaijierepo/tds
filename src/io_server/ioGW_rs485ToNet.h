#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"

class ioGW_rs485ToNet : public ioDev
{
public:
	ioGW_rs485ToNet(void);
	~ioGW_rs485ToNet(void);

	string getConnInfo() override;

	bool isCommBusy() override;
	bool isConnected() override;
	bool onRecvData(unsigned char* pData, size_t iLen) override;
	bool onRecvPkt(unsigned char* pData, size_t iLen) override;
	void checkAcqReqTimeout() override;
	void DoCycleTask() override;

	stream2pkt m_stream2pkt;

	string m_strErrorInfo;
};

