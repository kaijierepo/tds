#pragma once
#include "tdscore.h"

class stream2pkt{
public:
	void Init() {
		iAbandonBytes = 0;
		iPktLen = 0;
		iStreamLen = 0;
		m_protocolType = APP_LAYER_PROTO::UNKNOWN;

		iPktBuffSize = 0;
		iStreaBuffSize = 0;
		if(stream)
		{
			delete stream;
			stream = NULL;
		}
		if (pkt)
		{
			delete pkt;
			pkt = NULL;
		}
	}
	void Resize(char*& pData, int& iLen, int iNewSize);
	void ResizeStreamBuff(int iNewSize);
	void ResizePopPktBuff(int iNewSize);
	void PushStream(char* pData, int iLen);
	bool PopPkt(string cpt = APP_LAYER_PROTO::UNKNOWN);
	bool PopAllAs(string cpt); 

	stream2pkt()
	{
		pkt = NULL;
		stream = NULL;
		Init();
	}

	//流数据
	char* stream;
	int iStreaBuffSize;
	int iStreamLen;

	//组包成功的数据
	char* pkt;
	int iPktBuffSize;
	int iPktLen;
	string m_protocolType;

	int iAbandonBytes;

	int IsValidPkt_HTTP(char* pData,int iLen);
	int IsValidPkt_ModbusRTU(char* pData,int iLen);
	int IsValidPkt_JSONRPC(char* pData, int iLen);
	int IsValidPkt_WEBSOCKET(char* pData, int iLen);
	int IsValidPkt_IQ60(char* pData, int iLen);
};