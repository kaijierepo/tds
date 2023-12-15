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

	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) override;
};