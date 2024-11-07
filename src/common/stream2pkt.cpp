#include "stream2pkt.h"
#include "common.h"

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


size_t IsValidPkt_ModbusRTU(unsigned char* pData, size_t iLen)
{
	if (iLen < 4)
		return 0;

	unsigned char fCode = pData[1];

	//头尾检测处理粘包
	if (fCode == 0x01 || fCode == 0x02 || fCode == 0x03 || fCode == 0x04){
		int byteCount = pData[2];

		unsigned short crc1 = *(unsigned short*)(pData + 1/*addr*/ + 1/*funcCode*/ + 1/*byteCount*/ + byteCount);
		unsigned short crc2 = common::N_CRC16((unsigned char*)pData, 3 + byteCount); //crc校验包含地址字节和功能码字节

		//某些设备错误的使用了小端方式，兼容。crc本身并不绝对正确
		if (crc1 == crc2) {
			return 3 + byteCount + 2;
		}

		//规范方式，大端模式。 modbus slave软件，netAssist软件都是标准的
		common::endianSwap((char*)&crc1, 2);
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
	unsigned short crc2 = common::N_CRC16((unsigned char*)pData, iLen - 2);
	if (crc1 == crc2)
		return iLen;
	common::endianSwap((char*)&crc1, 2);
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

	if (iLen < 4 + D07_FRAME_MIN_LEN)
		return 0;

	if (!(p[0] == 0xFE && p[1] == 0xFE && p[2] == 0xFE && p[3] == 0xFE))
		return 0;

	unsigned char* pkt = p + 4;

	if (pkt[0] != 0x68)
		return 0;

	if (pkt[7] != 0x68)
		return 0;

	int dlen = pkt[9];

	if (iLen < 12 + dlen + 4)
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
	return 4 + 10 + dlen + 2;
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
	CAutoLock lock(&m_cs);
	Resize(stream, iStreaBuffSize, iNewSize);
}

void stream2pkt::ResizePopPktBuff(size_t iNewSize)
{
	CAutoLock lock(&m_cs);
	Resize(pkt, iPktBuffSize, iNewSize);
}

void stream2pkt::PushStream(unsigned char* pData, size_t iLen)
{
	CAutoLock lock(&m_cs);
	if (iStreamLen + iLen > iStreaBuffSize)
		ResizeStreamBuff(iStreamLen + iLen);

	memcpy(stream + iStreamLen , pData, iLen);
	iStreamLen += iLen;
}

void stream2pkt::PushStream(char* pData, size_t iLen)
{
	PushStream((unsigned char*)pData, iLen);
}

bool stream2pkt::PopPkt(string cpt)
{
	CAutoLock lock(&m_cs);
	//对位置i到末尾的数据进行有效数据包判断，允许i之前出现错误数据。有可能i到末尾之前有多个数据包
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
				abandonData = str::bytesToHexStr(stream, i);
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
	CAutoLock lock(&m_cs);

	//对位置i到末尾的数据进行有效数据包判断，允许i之前出现错误数据。有可能i到末尾之前有多个数据包
	for (int i = 0; i < iStreamLen; i++)
	{
		size_t iPopPktLen = 0;

		//是否容错，允许正确包之间出现异常数据。如果一定不会出现可以减少检测次数提高性能
		if (!faultTolerant && i > 0)
			break;

		//检查前缀
		if (m_prefix.size() > 0) {
			if (iStreamLen - i < m_prefix.size())
				return false;

			if (0!=memcmp(m_prefix.data(), stream + i, m_prefix.size())) {
				continue;
			}
		}

		//从前缀的后面1个字节开始检查数据包
		iPopPktLen = pktCheckFn(stream + i + m_prefix.size(), iStreamLen - i - m_prefix.size());

		if (iPopPktLen)
		{
			//记录丢弃数据
			iAbandonLen = i;
			if (i > 0)
				abandonData = str::bytesToHexStr(stream, iAbandonLen);
			else
				abandonData = "";

			//拷贝出pkt
			if (iPopPktLen > iPktBuffSize)
				ResizePopPktBuff(iPopPktLen);
			if(m_prefix.size()>0)
				memcpy(pkt, stream + iAbandonLen + m_prefix.size(), iPopPktLen);
			else
				memcpy(pkt, stream + iAbandonLen, iPopPktLen);
			iPktLen = iPopPktLen;

			//从stream中删除
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
	CAutoLock lock(&m_cs);

	if(iStreamLen > iPktBuffSize)
		ResizePopPktBuff(iStreamLen);

	memcpy(pkt, stream, iStreamLen);
	iPktLen = iStreamLen;
	iStreamLen = 0;
	ResizeStreamBuff(iStreamLen);

	m_protocolType = cpt;

	if (iPktLen > 0)
		return 1;
	return 0;
}


size_t IsValidPkt_HTTP(unsigned  char* pData,size_t iLen )
{
	unsigned char* ptmp = new unsigned char[iLen + 1];
	memset(ptmp, 0, iLen + 1);
	memcpy(ptmp, pData, iLen);
	string strData = (char*)ptmp;
	delete ptmp;

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