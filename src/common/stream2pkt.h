#pragma once
#include "tdscore.h"


typedef int (*fp_validPktCheck)(unsigned char* pData, int iLen);

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
	void Resize(unsigned char*& pData, int& iLen, int iNewSize);
	void ResizeStreamBuff(int iNewSize);
	void ResizePopPktBuff(int iNewSize);
	void PushStream(unsigned char* pData, int iLen);
	bool PopPkt(string cpt = APP_LAYER_PROTO::UNKNOWN);
	//faultTolerant是否容错，允许数据包之间有异常数据出现
	//打开容错会降低性能
	bool PopPkt(fp_validPktCheck pktCheckFn,bool faultTolerant = true);
	bool PopAllAs(string cpt); 

	stream2pkt()
	{
		pkt = NULL;
		stream = NULL;
		Init();
	}

	//流数据
	unsigned char* stream;
	int iStreaBuffSize;
	int iStreamLen;

	//组包成功的数据
	unsigned char* pkt;
	int iPktBuffSize;
	int iPktLen;
	string m_protocolType;

	int iAbandonBytes;
	string abandonData;

	map<string, fp_validPktCheck> m_mapProto2PktCheckFn;

	int IsValidPkt_HTTP(unsigned char* pData,int iLen);
	int IsValidPkt_WEBSOCKET(unsigned char* pData, int iLen);
	int IsValidPkt_terminalPrompt(unsigned char* pData, int iLen);
	int IsValidPkt_textEnd2LF(unsigned char* pData, int iLen);
	int IsValidPkt_textEnd1LF(unsigned char* pData, int iLen);
};


extern int IsValidPkt_IQ60(unsigned char* pData, int iLen);
extern int IsValidPkt_ModbusRTU(unsigned char* pData, int iLen);
extern int IsValidPkt_TDSP(unsigned char* pData, int iLen);
extern int IsValidPkt_LeakDetect(unsigned char* pData, int iLen);