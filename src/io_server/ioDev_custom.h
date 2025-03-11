#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"
#include "json.hpp"

class ioDev_custom : public ioDev
{
public:
	ioDev_custom();
	~ioDev_custom();

	void DoAcq();
	void doHttpHeartbeat();
	void DoCycleTask() override;
	void onEvent_online() override;
	bool onRecvData(unsigned char* pData, size_t iLen) override;
	void output(string chanAddr, json jVal, json& rlt, json& err, bool sync = true) override;
	void output(ioChannel* pC, json jVal, json& rlt, json& err, bool sync = true) override;
	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) override;
};