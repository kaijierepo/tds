#pragma once
#include <vector>
#include <string>
#include <map>
using namespace std;

typedef int (*fp_validPktCheck)(unsigned char* pData, size_t iLen);

class stream2pkt{
public:
	void Init() {
		iAbandonLen = 0;
		iPktLen = 0;
		iStreamLen = 0;
		m_protocolType = "unknown";

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
	void Resize(unsigned char*& pData, size_t& iLen, size_t iNewSize);
	void ResizeStreamBuff(size_t iNewSize);
	void ResizePopPktBuff(size_t iNewSize);
	void PushStream(unsigned char* pData, size_t iLen);
	void PushStream(char* pData, size_t iLen);
	bool PopPkt(string cpt = "");
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

	std::vector<byte> m_prefix;

	//流数据
	unsigned char* stream;
	size_t iStreaBuffSize;
	size_t iStreamLen;

	//组包成功的数据
	unsigned char* pkt;
	size_t iPktBuffSize;
	size_t iPktLen;
	string m_protocolType;

	size_t iAbandonLen;
	string abandonData;

	map<string, fp_validPktCheck> m_mapProto2PktCheckFn;

	int IsValidPkt_HTTP(unsigned char* pData,size_t iLen);


};


//LF == line feed == 0x0A  CR == carriage return == 0x0D
extern int IsValidPkt_textEnd_LF(unsigned char* pData, size_t iLen); // 1个换行符结尾
extern int IsValidPkt_textEnd_LFLF(unsigned char* pData, size_t iLen); // 2个换行符结尾
extern int IsValidPkt_textEnd_CRLF(unsigned char* pData, size_t iLen); // 2个换行符结尾
extern int IsValidPkt_terminalPrompt(unsigned char* pData, size_t iLen);
extern int IsValidPkt_IQ60(unsigned char* pData, size_t iLen);
extern int IsValidPkt_ModbusRTU(unsigned char* pData, size_t iLen);
extern int IsValidPkt_ModbusTcp(unsigned char* pData, size_t iLen);
extern int IsValidPkt_TDSP(unsigned char* pData, size_t iLen);
extern int IsValidPkt_LeakDetect(unsigned char* pData, size_t iLen);