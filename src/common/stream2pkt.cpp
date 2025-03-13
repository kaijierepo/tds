#include "stream2pkt.h"
#include <cstring>

size_t IsValidPkt_IQ60(unsigned char* pData, size_t iLen)
{
	if (iLen < 3)
		return 0;
	if (pData[0] == '[')
	{
		for (int i = 0; i < iLen; i++)
		{
			if (pData[i] == '\n' && pData[i - 1] == ']')
			{
				return i + 1;
			}
		}
	}
	return 0;
}


size_t IsValidPkt_ModbusTcp(unsigned char* pData, size_t iLen)
{
	if (iLen < 7)
		return 0;

	size_t len = pData[4] * 256 + pData[5];
	if (iLen == len + 6) {
		return iLen;
	}
	return 0;
}

const unsigned char auchCRCHi[] =
{
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
	0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
	0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
	0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81,
	0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
	0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
	0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
	0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
	0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
	0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
	0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
	0x40
};


const unsigned char auchCRCLo[] =
{
	0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7, 0x05, 0xC5, 0xC4,
	0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
	0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD,
	0x1D, 0x1C, 0xDC, 0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
	0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32, 0x36, 0xF6, 0xF7,
	0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
	0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE,
	0x2E, 0x2F, 0xEF, 0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
	0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1, 0x63, 0xA3, 0xA2,
	0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
	0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB,
	0x7B, 0x7A, 0xBA, 0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
	0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0, 0x50, 0x90, 0x91,
	0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
	0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88,
	0x48, 0x49, 0x89, 0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
	0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83, 0x41, 0x81, 0x80,
	0x40
};

unsigned short N_CRC16(unsigned char* updata, long long len)
{
	unsigned char uchCRCHi = 0xff;
	unsigned char uchCRCLo = 0xff;
	long long  uindex;
	while (len--)
	{
		uindex = uchCRCHi ^ *updata++;
		uchCRCHi = uchCRCLo ^ auchCRCHi[uindex];
		uchCRCLo = auchCRCLo[uindex];
	}
	return (uchCRCHi << 8 | uchCRCLo);
}


//该函数目前未制作完美,只能检查响应包的是否有效
size_t IsValidPkt_ModbusRTU(unsigned char* pData, size_t iLen)
{
	if (iLen < 4)
		return 0;

	unsigned char fCode = pData[1];

	//recv two pkt at the same time
	if (fCode == 0x01 || fCode == 0x02 || fCode == 0x03 || fCode == 0x04){
		int byteCount = pData[2];

		//not a complete pkt
		if (iLen < byteCount + 1 + 1 + 1 + 2) return 0;

		unsigned short crc1 = *(unsigned short*)(pData + 1/*addr*/ + 1/*funcCode*/ + 1/*byteCount*/ + byteCount);
		unsigned short crc2 = N_CRC16((unsigned char*)pData, 3 + byteCount); //crc include addr and funcCode

		//some device use little endian(not correct),compatialble with these devices
		if (crc1 == crc2) {
			return 3 + byteCount + 2;
		}

		//big endian(correct) modbus slave ,netAssist is correct
		unsigned char hbyte = crc1 / 256;
		unsigned char lbyte = crc1 % 256;
		crc1 = lbyte * 256 + hbyte;
		if (crc1 == crc2) {
			return 3 + byteCount + 2;
		}
	}
	//else if (fCode == 0x05) {
	//	unsigned short crc1 = *(unsigned short*)(pData + 1/*addr*/ + 1/*funcCode*/ + 1/*byteCount*/ + byteCount);
	//	common::endianSwap((char*)&crc1, 2);
	//	unsigned short crc2 = common::N_CRC16((unsigned char*)pData, 3 + byteCount); //crc校验包含地址字节和功能码字节
	//	if (crc1 == crc2) {
	//		return 2 + byteCount + 2;
	//	}
	//}

	unsigned short crc1 = *(unsigned short*)(pData + iLen - 2);
	unsigned short crc2 = N_CRC16((unsigned char*)pData, iLen - 2);
	if (crc1 == crc2)
		return iLen;
	unsigned char hbyte = crc1 / 256;
	unsigned char lbyte = crc1 % 256;
	crc1 = lbyte * 256 + hbyte;
	if (crc1 == crc2)
		return iLen;

	return 0;
}

size_t IsValidPkt_TDSP(unsigned char* pData, size_t iLen)
{
	if (iLen < 15)
		return 0;
	if (pData[0] != '{')
		return 0;

	for (int i = 0; i < iLen - 3; i++)
	{
		if (pData[i] == '\r' && pData[i + 1] == '\n' && pData[i + 2] == '\r' && pData[i + 3] == '\n')
		{
			return i + 4;
		}
	}

	for (int i = 0; i < iLen - 1; i++)
	{
		if (pData[i] == '\n' && pData[i + 1] == '\n')
		{
			return i + 2;
		}
	}
	return 0;
}

unsigned char calcLeakDetectCheckCode(unsigned char* pData, int len) {
	unsigned char crc = 0;
	for (int j = 0; j < len; j++)
	{
		crc ^= pData[j];
		for (int i = 0; i < 8; i++)
		{
			if ((unsigned char)(crc & 0x01) > 0) crc = (unsigned char)((unsigned char)(crc >> 1) ^ 0x8C);
			else crc >>= 1;
		}
	}
	return crc;
}

size_t isValidPkt_DLT645_2007(unsigned char* p, size_t iLen)
{
	int D07_FRAME_MIN_LEN = 12;
	int preambleCount = 0;

	//支持0-n的任意个前导字符
	while (preambleCount < iLen) {
		if (p[preambleCount] == 0xFE) {
			preambleCount++;
		}
		else {
			break;
		}
	}

	int iDataLen = iLen - preambleCount;

	if (iDataLen < D07_FRAME_MIN_LEN)
		return 0;

	unsigned char* pkt = p + preambleCount;

	if (pkt[0] != 0x68)
		return 0;

	if (pkt[7] != 0x68)
		return 0;

	int dlen = pkt[9];

	if (iLen < 12 + dlen)
		return 0;

	unsigned char cs = pkt[10 + dlen];
	unsigned char rcs = 0;
	for (int i = 0; i < 10 + dlen; i++) {
		rcs += pkt[i];
	}

	if (cs != rcs)
		return 0;

	if (pkt[10 + dlen + 1] != 0x16)
		return 0;

	// preamble + head + payload + tail
	return preambleCount + 10 + dlen + 2;
}

size_t IsValidPkt_315(unsigned char* pData, int iLen)
{
	if (iLen < 16) return 0;//315协议数据包最短长度

	if (pData[0] != 0x71 || pData[1] != 0x6B || pData[2] != 0x6E || pData[3] != 0x65 || pData[4] != 0x74) return 0;//帧头校验

	if (pData[5] != 0x02 && pData[5] != 0x80) return 0;

	auto a = pData[7];
	if (a != 0x3F && a != 0x8F && a != 0x0F) return 0; //2023.5.18 新增JSON数据帧 scz

	unsigned long frameLen = *(unsigned long*)(pData + 8);
	if (frameLen + 16 > iLen) return 0;

	unsigned char* pFrameEnd = pData + frameLen + 12;
	if (pFrameEnd[0] != 0xFF || pFrameEnd[1] != 0xFF || pFrameEnd[2] != 0xFF || pFrameEnd[3] != 0xFF)
		return 0;


	return (size_t)frameLen + 16;
}

size_t IsValidPkt_LeakDetect(unsigned char* pData, size_t iLen)
{
	if (iLen < 12)
		return 0;
	if (pData[0] == 0xA5 && pData[1] == 0xA5)
	{

	}
	else
		return 0;

	unsigned char cmdCode = pData[2];


	for (int i = 0; i <= iLen - 2; i++)
	{
		if (pData[i] == 0x5A && pData[i + 1] == 0x5A)
		{
			//除了采样数据包校验了，其他命令包都是0xff
			if (calcLeakDetectCheckCode(pData + 2, i - 1 - 2) == pData[i - 1] || 0xFF == pData[i - 1])
			{
				if (cmdCode == 0x23)//读数据包命令
				{
					if (i + 2 != 1013)
						return 0;
				}

				return i + 2;
			}		
		}
	}

	return 0;
}


void stream2pkt::Resize(unsigned char*& pData, size_t& iLen, size_t iNewSize)
{
	unsigned char* pNewData = new unsigned char[iNewSize];

	if (pData == NULL || iLen == 0)
	{
	}
	else
	{
		size_t iCopySize = iLen < iNewSize ? iLen : iNewSize;
		memcpy(pNewData, pData, iCopySize);
		delete pData;
	}

	pData = pNewData;
	iLen = iNewSize;
}

void stream2pkt::ResizeStreamBuff(size_t iNewSize)
{
	//in this function do not have to lock m_cs;
	Resize(stream, iStreaBuffSize, iNewSize);
}

void stream2pkt::ResizePopPktBuff(size_t iNewSize)
{
	//in this function do not have to lock m_cs;
	Resize(pkt, iPktBuffSize, iNewSize);
}

void stream2pkt::PushStream(unsigned char* pData, size_t iLen)
{
	std::lock_guard<mutex> g(m_cs);
	if (iStreamLen + iLen > iStreaBuffSize)
		ResizeStreamBuff(iStreamLen + iLen);

	memcpy(stream + iStreamLen , pData, iLen);
	iStreamLen += iLen;
}

void stream2pkt::PushStream(char* pData, size_t iLen)
{
	PushStream((unsigned char*)pData, iLen);
}

string bytesToHexStr(vector<char>& bytes)
{
	string str;
	for (size_t i = 0; i < bytes.size(); i++)
	{
		string b;
		b.resize(2);
		sprintf((char*)b.data(),"%02X", (unsigned char)bytes[i]);
		str += b;
	}
	return str;
}

string bytesToHexStr(unsigned char* p, size_t len, string splitter = "")
{
	string str;
	for (int i = 0; i < len; i++)
	{
		string b;
		b.resize(2);
		sprintf((char*)b.data(), "%02X", (unsigned char)p[i]);
		str += b;
		str += splitter;
	}

	return str;
}

bool stream2pkt::PopPkt(string cpt)
{
	std::lock_guard<mutex> g(m_cs);
	for (size_t i = 0; i < iStreamLen; i++)
	{
		size_t ilen = 0;

		if (ilen == 0 &&
			(cpt == "textEnd2LF"))
		{
			if (i > 0)
				break;
			ilen = IsValidPkt_textEnd_LFLF(stream + i, iStreamLen - i);
			if (ilen > 0)
			{
				m_protocolType = "textEnd2LF";
			}
		}

		if (ilen == 0 &&
			(cpt == "textEnd1LF"))
		{
			if (i > 0)
				break;
			ilen = IsValidPkt_textEnd_LF(stream + i, iStreamLen - i);
			if (ilen > 0)
			{
				m_protocolType = "textEnd1LF";
			}
		}

		if (ilen == 0 &&
			(cpt == "->"  ))//APP_LAYER_PROTO::terminalPrompt //用于iq60
		{
			if (i > 0)
				break;
			ilen = IsValidPkt_terminalPrompt(stream + i, iStreamLen - i);
			if (ilen > 0)
			{
				m_protocolType = "->";
			}
		}


		if (ilen)
		{
			if (ilen > iPktBuffSize)
				ResizePopPktBuff(ilen);



			if (i > 0)
				abandonData = bytesToHexStr(stream, i,"");
			else
				abandonData = "";
			
			memcpy(pkt, stream + i, ilen);
			iPktLen = ilen;

			memcpy(stream, stream + i + ilen, iStreamLen - i - ilen);
			iStreamLen -= i + ilen;
			iAbandonLen = i;

			ResizeStreamBuff(iStreamLen);

			return true;
		}
	}
	return false;
}

bool stream2pkt::PopPkt(fp_validPktCheck pktCheckFn, bool faultTolerant)
{
	std::lock_guard<mutex> g(m_cs);

	for (int i = 0; i < iStreamLen; i++)
	{
		size_t iPopPktLen = 0;
		if (!faultTolerant && i > 0)
			break;
		if (m_prefix.size() > 0) {
			if (iStreamLen - i < m_prefix.size())
				return false;

			if (0!=memcmp(m_prefix.data(), stream + i, m_prefix.size())) {
				continue;
			}
		}

		iPopPktLen = pktCheckFn(stream + i + m_prefix.size(), iStreamLen - i - m_prefix.size());

		if (iPopPktLen)
		{
			iAbandonLen = i;
			if (i > 0)
				abandonData = bytesToHexStr(stream, iAbandonLen);
			else
				abandonData = "";

			if (iPopPktLen > iPktBuffSize)
				ResizePopPktBuff(iPopPktLen);
			if(m_prefix.size()>0)
				memcpy(pkt, stream + iAbandonLen + m_prefix.size(), iPopPktLen);
			else
				memcpy(pkt, stream + iAbandonLen, iPopPktLen);
			iPktLen = iPopPktLen;

			if (m_prefix.size() > 0) {
				memcpy(stream, stream + iAbandonLen + iPopPktLen + m_prefix.size(), iStreamLen - iAbandonLen - iPopPktLen - m_prefix.size());
				iStreamLen -= iAbandonLen + iPopPktLen + m_prefix.size();
			}
			else {
				memcpy(stream, stream + iAbandonLen + iPopPktLen, iStreamLen - iAbandonLen - iPopPktLen);
				iStreamLen -= iAbandonLen + iPopPktLen;
			}
			ResizeStreamBuff(iStreamLen);


			return true;
		}
	}

	return false;
}

bool stream2pkt::PopAllAs(string cpt)
{
	std::lock_guard<mutex> g(m_cs);
	if(iStreamLen > iPktBuffSize)
		ResizePopPktBuff(iStreamLen);
	memcpy(pkt, stream, iStreamLen);
	iPktLen = iStreamLen;
	iStreamLen = 0;
	ResizeStreamBuff(iStreamLen);
	m_protocolType = cpt;
	int ret = 0;
	if (iPktLen > 0)
		ret = 1;
	return ret;
}


size_t IsValidPkt_HTTP(unsigned  char* pData,size_t iLen )
{
	unsigned char* ptmp = new unsigned char[iLen + 1];
	memset(ptmp, 0, iLen + 1);
	memcpy(ptmp, pData, iLen);
	string strData = (char*)ptmp;
	delete [] ptmp;

	size_t iPos_contentLengthLineStart = strData.find("Content-Length:"); //15
	//没有http body的情况
	if (iPos_contentLengthLineStart == string::npos)
	{
		auto iDataLen = strData.length();
		if (iDataLen >= 4)
		{
			string tail = strData.substr(iDataLen - 4, 4);
			if (tail == "\r\n\r\n")
				return iLen;
			else
				return 0;
		}
	}
	else
	{
		size_t iPos_contentLengthLineEnd = strData.find("\r\n", iPos_contentLengthLineStart);
		if (iPos_contentLengthLineEnd == string::npos)
			return 0;

		string strLen = strData.substr(iPos_contentLengthLineStart + 15, iPos_contentLengthLineEnd - (iPos_contentLengthLineStart + 15));
		size_t iContentLen = atoi(strLen.c_str());

		size_t iBodyStart = 0;
		for (size_t i = iPos_contentLengthLineEnd; i + 3 < iLen; i++)
		{
			if (pData[i] == '\r' &&
				pData[i + 1] == '\n' &&
				pData[i + 2] == '\r' &&
				pData[i + 3] == '\n'
				)
			{
				iBodyStart = i + 4;
				break; //找到header后面的空行 ，后面就是body。必须break。因为body数据里面可能也有两个换行
			}
		}

		if (iBodyStart == 0)
			return 0;

		if (iLen >= iBodyStart + iContentLen)
			return iBodyStart + iContentLen;

		return 0;
	}
	return 0;
}


size_t IsValidPkt_terminalPrompt(unsigned char* pData, size_t iLen)
{
	if (iLen < 5)
		return 0;
	for (int i = 2; i < iLen; i++)
	{
		//冒号中的 ->提示符不算
		if (pData[i-1] == '-' && pData[i] == '>' && pData[i-2]!='\"')
		{
			return i + 1;
		}
	}
	return 0;
}

size_t IsValidPkt_textEnd_CRLF(unsigned char* pData, size_t iLen)
{
	if (iLen < 3)
		return 0;
	for (int i = 1; i < iLen; i++)
	{
		if (pData[i - 1] == '\r' && pData[i] == '\n')
		{
			return i + 1;
		}
	}
	return 0;
}

size_t IsValidPkt_textEnd_CRLFCRLF(unsigned char* pData, size_t iLen)
{
	if (iLen < 4)
		return 0;
	for (int i = 0; i < iLen; i++)
	{
		if (pData[i] == '\r' && pData[i+1] == '\n' &&
			pData[i+2] == '\r' && pData[i+3] == '\n')
		{
			return i + 3;
		}
	}
	return 0;
}

size_t IsValidPkt_textEnd_LFLF(unsigned char* pData, size_t iLen)
{
	if (iLen < 3)
		return 0;
	for (int i = 1; i < iLen; i++)
	{
		if (pData[i-1] == '\n' && pData[i] == '\n')
		{
			return i+1;
		}
	}
	return 0;
}

size_t IsValidPkt_textEnd_LF(unsigned char* pData, size_t iLen)
{
	for (int i = 0; i < iLen; i++)
	{
		if (pData[i] == '\n')
		{
			return i + 1;
		}
	}
	return 0;
}