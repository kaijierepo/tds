#pragma once
#include "ioDev.h"
#include "tcpClt.h"
class ioDev_iq60 : public ioDev
{
public:
	ioDev_iq60();
	~ioDev_iq60();
	bool onRecvPkt(json jPkt);
	bool getCurrentVal();
};

extern void onRecvIQ60Pkt(char* pData, int iLen);
extern map<string, ioDev_iq60*> g_mapIQ60;