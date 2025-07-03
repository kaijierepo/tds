#pragma once
#include <vector>
#include <string>
#include <map>
#include <mutex>
using namespace std;

namespace APP_LAYER_PROTO {
	const string UNKNOWN = "unknown";
	const string HTTP = "http";
	const string PROTOCOL_WEBSOCKET = "websocket";
	const string PROTOCOL_FRAMING_PROTOCOL = "alp_framing_protocol";
	const string tdsHMR = "tdsHMR";  //tds web hot module replacement
	const string terminalPrompt = "->";
	const string textEnd1LF = "textEnd1LF";
	const string textEnd2LF = "textEnd2LF";
};

typedef size_t(*fp_validPktCheck)(unsigned char* pData, size_t iLen);

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
	
	static void Resize(unsigned char*& pData, size_t& iLen, size_t iNewSize);
	void ResizeStreamBuff(size_t iNewSize);
	void ResizePopPktBuff(size_t iNewSize);
	void PushStream(unsigned char* pData, size_t iLen);
	void PushStream(char* pData, size_t iLen);
	bool PopPkt(string cpt = "");
	//faultTolerant=true,allow error data between data pkt
	//causes performance loss
	bool PopPkt(fp_validPktCheck pktCheckFn,bool faultTolerant = true);
	//bool PopAllAs(string cpt); 

	stream2pkt()
	{
		pkt = NULL;
		stream = NULL;
		Init();
	}
	~stream2pkt()
	{
	}

	std::vector<unsigned char> m_prefix;

	unsigned char* stream;
	size_t iStreaBuffSize;
	size_t iStreamLen;

	unsigned char* pkt;
	size_t iPktBuffSize;
	size_t iPktLen;
	string m_protocolType;

	size_t iAbandonLen;
	string abandonData;
	mutex m_cs;
	map<string, fp_validPktCheck> m_mapProto2PktCheckFn;
};


//LF == line feed == 0x0A  CR == carriage return == 0x0D
//return 0 if no valid pkt, else return length of the pkt
extern size_t IsValidPkt_HTTP(unsigned char* pData, size_t iLen);
extern size_t IsValidPkt_textEnd_LF(unsigned char* pData, size_t iLen); // 1个换行符结尾
extern size_t IsValidPkt_textEnd_LFLF(unsigned char* pData, size_t iLen); // 2个换行符结尾
extern size_t IsValidPkt_textEnd_CRLF(unsigned char* pData, size_t iLen); // 2个换行符结尾
extern size_t IsValidPkt_textEnd_CRLFCRLF(unsigned char* pData, size_t iLen);
extern size_t IsValidPkt_terminalPrompt(unsigned char* pData, size_t iLen);
extern size_t IsValidPkt_IQ60(unsigned char* pData, size_t iLen);
extern size_t IsValidPkt_ModbusRTU(unsigned char* pData, size_t iLen);
extern size_t IsValidPkt_ModbusTcp(unsigned char* pData, size_t iLen);
extern size_t IsValidPkt_TDSP(unsigned char* pData, size_t iLen);
extern size_t IsValidPkt_LeakDetect(unsigned char* pData, size_t iLen);
extern size_t isValidPkt_DLT645_2007(unsigned char* p, size_t iLen);
extern size_t IsValidPkt_315(unsigned char* pData, int iLen);