#include "pch.h"
#include "wsProto.h"
#include "base64.h"
#include <WinSock2.h>
#include "sha1.h"


int IsValidPkt_WEBSOCKET(unsigned char* pData, size_t iLen)
{
	CWSPPkt req;
	if (WS_ERROR_FRAME != req.unpack((unsigned char*)pData, iLen))
	{
		return req.iFrmLen;
	}
	return 0;
}


CWSPPkt::CWSPPkt()
{
	payloadData = NULL;
	iFrmLen = 0;
}

CWSPPkt::~CWSPPkt()
{
	if (payloadData)
		delete payloadData;
}

WS_FrameType CWSPPkt::unpack(LPVOID pBuf, int iBufLen, bool bGetCmdInfo)
{
	BYTE* frameData = (BYTE*)pBuf;
	int len = iBufLen;
	WS_FrameType ret = WS_ERROR_FRAME;

	const int frameLength = len;
	if (frameLength < 2)
	{
		ret = WS_ERROR_FRAME;
	}

	if ((frameData[0] & 0x70) != 0x0)
	{
		ret = WS_ERROR_FRAME;
	}

	// fin   1 complete frame  0 to be continued
	fin_ = frameData[0] >> 7;


	// mask
	if ((frameData[1] & 0x80) != 0x80)
	{
		ret = WS_ERROR_FRAME;
	}

	// opt code
	uint64_t payloadLength = 0;
	uint8_t payloadFieldExtraBytes = 0;
	uint8_t opcode = static_cast<uint8_t>(frameData[0] & 0x0f);

	//std::cout << "mask:" << ((frameData[1] & 0x80) != 0x80) << std::endl;
	//std::cout << "frameLength: " << frameLength << std::endl;
	//std::cout << "payloadLength: " << payloadLength << std::endl;
	//std::cout << "payloadFieldExtraBytes: " << payloadFieldExtraBytes << std::endl;
	//std::cout << "opcode: " << opcode << std::endl;


	if (opcode == WS_TEXT_FRAME || 
		opcode == WS_BINARY_FRAME ||
		opcode == WS_CONTINUATION_FRAME)
	{
		//utf-8 text
		ret = (WS_FrameType)opcode;
		payloadLength = static_cast<uint64_t>(frameData[1] & 0x7f);
		if (payloadLength == 0x7e)//max payload is 65535;  2 bytes for payload len value storage; the leading 7 bits is used as a flag
		{
			uint16_t payloadLength16b = 0;
			payloadFieldExtraBytes = 2;
			memcpy(&payloadLength16b, &frameData[2], payloadFieldExtraBytes);
			payloadLength = ntohs(payloadLength16b);
		}
		else if (payloadLength == 0x7f)
		{
			UINT64 payloadLength64b = 0;
			payloadFieldExtraBytes = 8;
			BYTE* pDest = (BYTE*)&payloadLength;
			BYTE* pSrc = &frameData[2];
			for(int i=0;i<8;i++)
			{
				pDest[7-i]=pSrc[i];
			}
		}
	}
	else if (opcode == WS_PING_FRAME || opcode == WS_PONG_FRAME)
	{
		
	}
	else if (opcode == WS_CLOSING_FRAME)
	{
		ret = WS_CLOSING_FRAME;
	}
	else
	{
		ret = WS_ERROR_FRAME;
	}

	if (2 + 4 + payloadLength + payloadFieldExtraBytes > iBufLen)
		return WS_ERROR_FRAME;


	//unmask
	if ((ret != WS_ERROR_FRAME) && (payloadLength > 0))
	{
		// header  2  masking key  4 
		char *maskingKey = (char*)&frameData[2 + payloadFieldExtraBytes];
		payloadData = new char[payloadLength + 1];
		memset(payloadData, 0, payloadLength + 1);
		memcpy(payloadData, &frameData[2 + payloadFieldExtraBytes + 4], payloadLength);
		for (int i = 0; i < payloadLength; i++)
		{
			payloadData[i] = payloadData[i] ^ maskingKey[i % 4];
		}

		iPayloadLen = payloadLength;
		iFrmLen = iPayloadLen + 2/*2 head*/ + 4/*4 masking key*/ + payloadFieldExtraBytes;
		
	}


	frmType = GetFrameType((char*)pBuf,iBufLen);
	return ret;
}

int CWSPPkt::pack(const char * inMessage, size_t messageLen,  enum WS_FrameType frameType, bool bFin, bool bOpt)
{
	int ret = WS_ERROR_FRAME;
	const uint32_t messageLength = messageLen;

	//uint8_t payloadFieldExtraBytes = (messageLength <= 0x7d) ? 0 : 2; //0x7d =125
	uint8_t payloadFieldExtraBytes = 0;
	if (0x7d < messageLength && messageLength <=0xFFFF) { //65535
		payloadFieldExtraBytes = 2;
	}
	else if (0xFFFF <  messageLength) {
		payloadFieldExtraBytes = 8;
	}
	// header: 2字节, mask位设置为0(不加密), 则后面的masking key无须填写, 省略4字节
	uint8_t frameHeaderSize = 2 + payloadFieldExtraBytes;
	uint8_t *frameHeader = new uint8_t[frameHeaderSize];
	memset(frameHeader, 0, frameHeaderSize);
	// fin位为1, 扩展位为0, 操作位为frameType
	frameHeader[0] = static_cast<uint8_t>(0x80 | frameType);
	/*if(bFin)
		frameHeader[0] |= static_cast<uint8_t>(0x80);
	else
		frameHeader[0] &= static_cast<uint8_t>(0x7F);

	if(bOpt)
		frameHeader[0] = static_cast<uint8_t>(0x80 | frameType);
	else
		frameHeader[0] = static_cast<uint8_t>(0x80 | frameType);*/

	// 填充数据长度
	if (messageLength <= 0x7d)
	{
		frameHeader[1] = static_cast<uint8_t>(messageLength);
	}
	else if(0x7d < messageLength && messageLength <= 0xFFFF)
	{
		frameHeader[1] = 0x7e;
		uint16_t len = htons(messageLength);
		memcpy(&frameHeader[2], &len, payloadFieldExtraBytes);
	}
	else {
		frameHeader[1] = 0x7f;
		uint32_t len = htonl(static_cast<uint32_t>(messageLength)); //uint32_t htonl(uint32_t hostlong); 所以这里其实有隐患
		memcpy(&frameHeader[2+4], &len, payloadFieldExtraBytes-4);//网络字节序是大端
	}

	// 填充数据
	uint32_t frameSize = frameHeaderSize + messageLength;
	char *frame = new char[frameSize + 1];
	memcpy(frame, frameHeader, frameHeaderSize);
	memcpy(frame + frameHeaderSize, inMessage, messageLength);
	frame[frameSize] = '\0';

	delete[] frameHeader;
	data = (unsigned char*)frame;
	len = frameSize;
	return ret;
}

bool CWSPPkt::isDataFrame()
{
	if (frmType == WS_BINARY_FRAME || frmType == WS_TEXT_FRAME || frmType == WS_CONTINUATION_FRAME) {
		return true;
	}
	return false;
}

bool CWSPPkt::isHandShake(string& request)
{
	std::string& str = request;
	size_t i = str.find("GET");
	if (i == std::string::npos) {
		return false;
	}
	i = str.find("Upgrade");
	if (i == std::string::npos) {
		return false;
	}
	i = str.find("websocket");
	if (i == std::string::npos) {
		return false;
	}
	return true;
}

const std::string MAGIstring = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string CWSPPkt::getKey(std::string strKey)
{
	SHA_1 sha;
	strKey += MAGIstring;
	//strcat(strKey, MAGIstring);
	unsigned int iDigSet[5];
	sha.Reset();
	sha << strKey.c_str();
	sha.Result(iDigSet);

	for (int i = 0; i < 5; i++)iDigSet[i] = htonl(iDigSet[i]);			//将字节转换成网络字节顺序

	//进行base64编码
	//strcpy(strKey , base64code.base64_encode(reinterpret_cast<const unsigned char*>(iDigSet), 20).c_str() );
	char out[100] = { 0 };
	base64_encode(reinterpret_cast<const unsigned char*>(iDigSet), 20,out);
	strKey = out;
	return strKey;
}

std::string CWSPPkt::GetHandshakeString(std::string request)
{
	std::string response;
	size_t pos = request.find("Sec-WebSocket-Key: ");
	response += "HTTP/1.1 101 Switching Protocols\r\n";
	response += "Connection: upgrade\r\n";
	response += "Access-Control-Allow-Credentials:true\r\n";
	response += "Access-Control-Allow-Headers:content-type\r\n";
	response += "Sec-WebSocket-Accept: ";

	std::string strKey = request.substr(pos + 19, 24);
	//std::cout << "oldKey" << strKey << std::endl;

	std::string newKey = getKey(strKey.c_str());
	//std::cout << "newKey" << newKey << std::endl;

	response += newKey + "\r\n";
	response += "Upgrade: websocket\r\n\r\n";

	/*std::cout << response << std::endl;
	std::string s = "puVOuWb7rel6z2AVZBKnfw==\r";
	std::cout << getKey(s) << std::endl;*/
	return response;
}

WS_FrameType CWSPPkt::GetFrameType(const char *frameData, int len)
{
	int ret = WS_ERROR_FRAME;
	const int frameLength = len;
	if (frameLength < 2)
	{
		return WS_FrameType(ret);
	}

	//RFC6455 5.2
	//If a nonzero value is received and none of
	//the negotiated extensions defines the meaning of such a nonzero
	//	value, the receiving endpoint MUST _Fail the WebSocket
	//	Connection_.
	if ((frameData[0] & 0x70) != 0x0)
	{
		return WS_FrameType(ret);
	}

	//RFC6455 5.2
	//All frames sent from
	//client to server have this bit set to 1.
	if ((frameData[1] & 0x80) != 0x80)
	{
		return WS_FrameType(ret);
	}

	uint8_t opcode = static_cast<uint8_t>(frameData[0] & 0x0f);
	if (opcode == WS_TEXT_FRAME ||
	    opcode == WS_BINARY_FRAME || 
		opcode == WS_PING_FRAME || 
		opcode == WS_PONG_FRAME ||
	    opcode == WS_CLOSING_FRAME ||
		opcode == WS_CONTINUATION_FRAME)
	{
		ret = opcode;
	}
	else
	{
		ret = WS_ERROR_FRAME;
	}

	return WS_FrameType(ret);
}

int CWSPPkt::fetch_fin(char * msg, int & pos)
{
	fin_ = (unsigned char)msg[pos] >> 7;
	return 0;
}

int CWSPPkt::fetch_opcode(char * msg, int & pos)
{
	opcode_ = msg[pos] & 0x0f;
	pos++;
	return 0;
}

int CWSPPkt::fetch_mask(char * msg, int & pos)
{
	mask_ = (unsigned char)msg[pos] >> 7;
	return 0;
}

int CWSPPkt::fetch_masking_key(char * msg, int & pos)
{
	if (mask_ != 1)
		return 0;
	for (int i = 0; i < 4; i++)
		masking_key_[i] = msg[pos + i];
	pos += 4;
	return 0;
}

int CWSPPkt::fetch_payload_length(char * msg, int & pos)
{
	payload_length_ = msg[pos] & 0x7f;
	pos++;
	if (payload_length_ == 126) {
		uint16_t length = 0;
		memcpy(&length, msg + pos, 2);
		pos += 2;
		payload_length_ = ntohs(length);
	}
	else if (payload_length_ == 127) {
		uint32_t length = 0;
		memcpy(&length, msg + pos, 4);
		pos += 4;
		payload_length_ = ntohl(length);
	}
	return 0;
}

int CWSPPkt::fetch_payload(char * msg, int & pos)
{
	memset(payload_, 0, sizeof(payload_));
	if (mask_ != 1) {
		memcpy(payload_, msg + pos, payload_length_);
	}
	else {
		for (int i = 0; i < payload_length_; i++) {
			int j = i % 4;
			payload_[i] = msg[pos + i] ^ masking_key_[j];
		}
	}
	pos += payload_length_;
	return 0;
}
