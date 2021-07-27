#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"

class ioDev_iq60 : public ioDev
{
public:
	ioDev_iq60();
	~ioDev_iq60();
	bool onRecvPkt(json jPkt);
	bool getCurrentVal();
	bool waitResponse(int timeout);
	virtual bool scanChannel(json& chanList);


	std::shared_ptr<TDS_SESSION> ioSession;
	string currentCmd;
	vector<json> currentResp; //分包组包
	bool getResponse;
};

extern void onRecvIQ60Pkt(char* pData, int iLen,std::shared_ptr<TDS_SESSION> pALC);
extern map<string, ioDev_iq60*> g_mapIQ60;