#include "pch.h"
#include "proto/wsProto.h"
#include "stream2pkt.h"

int IsValidPkt_IQ60(unsigned char* pData, int iLen)
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


int IsValidPkt_ModbusTcp(unsigned char* pData, int iLen)
{
	if (iLen < 7)
		return 0;

	int len = pData[4] * 256 + pData[5];
	if (iLen == len + 6) {
		return iLen;
	}
	return 0;
}


int IsValidPkt_ModbusRTU(unsigned char* pData, int iLen)
{
	if (iLen < 4)
		return 0;

	WORD crc1 = *(WORD*)(pData + iLen - 2);
	common::endianSwap((char*)&crc1, 2);
	WORD crc2 = common::N_CRC16((unsigned char*)pData, iLen - 2);
	if (crc1 == crc2)
		return iLen;
	else
		return 0;
}

int IsValidPkt_TDSP(unsigned char* pData, int iLen)
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
	byte crc = 0;
	for (int j = 0; j < len; j++)
	{
		crc ^= pData[j];
		for (int i = 0; i < 8; i++)
		{
			if ((byte)(crc & 0x01) > 0) crc = (byte)((byte)(crc >> 1) ^ 0x8C);
			else crc >>= 1;
		}
	}
	return crc;
}

int IsValidPkt_LeakDetect(unsigned char* pData, int iLen)
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

void stream2pkt::Resize(unsigned char*& pData, int& iLen, int iNewSize)
{
	unsigned char* pNewData = new unsigned char[iNewSize];

	if (pData == NULL || iLen == 0)
	{
	}
	else
	{
		int iCopySize = iLen < iNewSize ? iLen : iNewSize;
		memcpy_s(pNewData, iNewSize, pData, iCopySize);
		delete pData;
	}

	pData = pNewData;
	iLen = iNewSize;
}

void stream2pkt::ResizeStreamBuff(int iNewSize)
{
	Resize(stream, iStreaBuffSize, iNewSize);
}

void stream2pkt::ResizePopPktBuff(int iNewSize)
{
	Resize(pkt, iPktBuffSize, iNewSize);
}

void stream2pkt::PushStream(unsigned char* pData, int iLen)
{
	if (iStreamLen + iLen > iStreaBuffSize)
		ResizeStreamBuff(iStreamLen + iLen);

	memcpy_s(stream + iStreamLen, iStreaBuffSize , pData, iLen);
	iStreamLen += iLen;
}

void stream2pkt::PushStream(char* pData, int iLen)
{
	PushStream((unsigned char*)pData, iLen);
}

bool stream2pkt::PopPkt(string cpt)
{
	//对位置i到末尾的数据进行有效数据包判断，允许i之前出现错误数据。有可能i到末尾之前有多个数据包
	for (int i = 0; i < iStreamLen; i++)
	{
		int ilen = 0;

		if (ilen == 0 &&
			(cpt == APP_LAYER_PROTO::textEnd2LF))
		{
			if (i > 0)
				break;
			ilen = IsValidPkt_textEnd2LF(stream + i, iStreamLen - i);
			if (ilen > 0)
			{
				m_protocolType = APP_LAYER_PROTO::textEnd2LF;
			}
		}

		if (ilen == 0 &&
			(cpt == APP_LAYER_PROTO::textEnd1LF))
		{
			if (i > 0)
				break;
			ilen = IsValidPkt_textEnd1LF(stream + i, iStreamLen - i);
			if (ilen > 0)
			{
				m_protocolType = APP_LAYER_PROTO::textEnd1LF;
			}
		}

		if (ilen == 0 &&
			(cpt == APP_LAYER_PROTO::terminalPrompt))
		{
			if (i > 0)
				break;
			ilen = IsValidPkt_terminalPrompt(stream + i, iStreamLen - i);
			if (ilen > 0)
			{
				m_protocolType = APP_LAYER_PROTO::terminalPrompt;
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
			
			memcpy_s(pkt, iPktBuffSize, stream + i, ilen);
			iPktLen = ilen;

			memcpy_s(stream, iStreaBuffSize, stream + i + ilen, iStreamLen - i - ilen);
			iStreamLen -= i + ilen;
			iAbandonBytes = i;

			ResizeStreamBuff(iStreamLen);

			return true;
		}
	}

	return false;
}

bool stream2pkt::PopPkt(fp_validPktCheck pktCheckFn, bool faultTolerant)
{
	//对位置i到末尾的数据进行有效数据包判断，允许i之前出现错误数据。有可能i到末尾之前有多个数据包
	for (int i = 0; i < iStreamLen; i++)
	{
		int ilen = 0;

		if (!faultTolerant && i > 0)
			break;

		ilen = pktCheckFn(stream + i, iStreamLen - i);

		if (ilen)
		{
			if (ilen > iPktBuffSize)
				ResizePopPktBuff(ilen);

			if (i > 0)
				abandonData = str::bytesToHexStr(stream, i);
			else
				abandonData = "";

			memcpy_s(pkt, iPktBuffSize, stream + i, ilen);
			iPktLen = ilen;

			memcpy_s(stream, iStreaBuffSize, stream + i + ilen, iStreamLen - i - ilen);
			iStreamLen -= i + ilen;
			iAbandonBytes = i;

			ResizeStreamBuff(iStreamLen);

			return true;
		}
	}

	return false;
}

bool stream2pkt::PopAllAs(string cpt)
{
	if(iStreamLen > iPktBuffSize)
		ResizePopPktBuff(iStreamLen);

	memcpy_s(pkt, iStreamLen, stream, iStreamLen);
	iPktLen = iStreamLen;
	iStreamLen = 0;
	ResizeStreamBuff(iStreamLen);

	m_protocolType = cpt;

	if (iPktLen > 0)
		return 1;
	return 0;
}


int stream2pkt::IsValidPkt_HTTP(unsigned  char* pData,int iLen )
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
		string tail = strData.substr(strData.length() - 4, 4);
		if (tail == "\r\n\r\n")
			return iLen;
		else
			return 0;
	}
	else
	{
		size_t iPos_contentLengthLineEnd = strData.find("\r\n", iPos_contentLengthLineStart);
		if (iPos_contentLengthLineEnd == string::npos)
			return 0;

		string strLen = strData.substr(iPos_contentLengthLineStart + 15, iPos_contentLengthLineEnd - (iPos_contentLengthLineStart + 15));
		int iContentLen = atoi(strLen.c_str());

		int iBodyStart = 0;
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
}


int stream2pkt::IsValidPkt_terminalPrompt(unsigned char* pData, int iLen)
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


int stream2pkt::IsValidPkt_textEnd2LF(unsigned char* pData, int iLen)
{
	if (iLen < 5)
		return 0;
	for (int i = 1; i < iLen; i++)
	{
		if (pData[i] == '\n' || pData[i] == '\n')
		{
			return i + 1;
		}
	}
	return 0;
}

int stream2pkt::IsValidPkt_textEnd1LF(unsigned char* pData, int iLen)
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