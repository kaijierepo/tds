#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"
#include "json.hpp"


class ioDev_dcqk : public ioDev
{
public:
	ioDev_dcqk();
	~ioDev_dcqk();

	void DoAcq();
	void DoCycleTask() override;
	void onEvent_online() override;

	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) override;
};