#include "pch.h"
#include "wsProto.h"
#include "stream2pkt.h"


void stream2pkt::Resize(char*& pData, int& iLen, int iNewSize)
{
	char* pNewData = new char[iNewSize];

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

void stream2pkt::PushStream(char* pData, int iLen)
{
	if (iStreamLen + iLen > iStreaBuffSize)
		ResizeStreamBuff(iStreamLen + iLen);

	memcpy_s(stream + iStreamLen, iStreaBuffSize , pData, iLen);
	iStreamLen += iLen;
}

bool stream2pkt::PopPkt(string cpt)
{
	//对位置i到末尾的数据进行有效数据包判断，允许i之前出现错误数据。有可能i到末尾之前有多个数据包
	for (int i = 0; i < iStreamLen; i++)
	{
		int ilen = 0;

		if (ilen == 0 &&
			(cpt == APP_LAYER_PROTO::UNKNOWN || cpt == APP_LAYER_PROTO::MODBUS_RTU))
		{	
			if (i == 0)
			{
				ilen = IsValidPkt_ModbusRTU(stream + i, iStreamLen - i);
				if (ilen > 0)
				{
					m_protocolType = APP_LAYER_PROTO::MODBUS_RTU;
				}
			}	
		}

		if (ilen == 0 &&
			(cpt == APP_LAYER_PROTO::UNKNOWN || cpt == APP_LAYER_PROTO::TDSRPC))
		{
			if (i == 0)
			{
				ilen = IsValidPkt_JSONRPC(stream + i, iStreamLen - i);
				if (ilen > 0)
				{
					m_protocolType = APP_LAYER_PROTO::TDSRPC;
				}
			}
		}

		if (ilen == 0 &&
			(cpt == APP_LAYER_PROTO::UNKNOWN || cpt == APP_LAYER_PROTO::HTTP))
		{
			if (i == 0)
			{
				ilen = IsValidPkt_HTTP(stream + i, iStreamLen - i);
				if (ilen > 0)
				{
					m_protocolType = APP_LAYER_PROTO::TDSRPC;
				}
			}
		}

		if (ilen == 0 &&
			(cpt == APP_LAYER_PROTO::PROTOCOL_WEBSOCKET))
		{
			if (i > 0)
				break;
			ilen = IsValidPkt_WEBSOCKET(stream + i, iStreamLen - i);
			if (ilen > 0)
			{
				m_protocolType = APP_LAYER_PROTO::PROTOCOL_WEBSOCKET;
			}
		}


		if (ilen == 0 &&
			(cpt == APP_LAYER_PROTO::IQ60))
		{
			if (i > 0)
				break;
			ilen = IsValidPkt_IQ60(stream + i, iStreamLen - i);
			if (ilen > 0)
			{
				m_protocolType = APP_LAYER_PROTO::IQ60;
			}
		}

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


		if (ilen)
		{
			if (ilen > iPktBuffSize)
				ResizePopPktBuff(ilen);

			memcpy_s(pkt, iPktBuffSize, stream + i, ilen);
			iPktLen = ilen;

			memcpy_s(stream, iStreaBuffSize, stream + i + ilen, iStreamLen - i - ilen);
			iStreamLen -= i + ilen;
			iAbandonBytes += i;

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

int stream2pkt::IsValidPkt_ModbusRTU( char* pData,int iLen )
{
	if (iLen < 4)
		return 0;

	WORD crc1 = *(WORD*)(pData + iLen -2);
	common::endianSwap((char*)&crc1, 2);
	WORD crc2 = common::N_CRC16((unsigned char*)pData,iLen - 2);
	if(crc1 == crc2)
		return iLen;
	else
		return 0;
}


int stream2pkt::IsValidPkt_HTTP( char* pData,int iLen )
{
	char* ptmp = new char[iLen + 1];
	memset(ptmp, 0, iLen + 1);
	memcpy(ptmp, pData, iLen);
	string strData = ptmp;
	delete ptmp;

	int iPos_contentLengthLineStart = strData.find("Content-Length:"); //15
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
		int iPos_contentLengthLineEnd = strData.find("\r\n", iPos_contentLengthLineStart);
		if (iPos_contentLengthLineEnd == string::npos)
			return 0;

		string strLen = strData.substr(iPos_contentLengthLineStart + 15, iPos_contentLengthLineEnd - (iPos_contentLengthLineStart + 15));
		int iContentLen = atoi(strLen.c_str());

		int iBodyStart = 0;
		for (int i = iPos_contentLengthLineEnd; i + 3 < iLen; i++)
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

int stream2pkt::IsValidPkt_JSONRPC(char* pData, int iLen)
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

int stream2pkt::IsValidPkt_WEBSOCKET(char* pData, int iLen)
{
	CWSPPkt req;
	if (WS_ERROR_FRAME != req.unpack((char*)pData, iLen))
	{
		return req.iFrmLen;
	}
	return 0;
}

int stream2pkt::IsValidPkt_textEnd2LF(char* pData, int iLen)
{
	if (iLen < 5)
		return 0;
	for (int i = 1; i < iLen; i++)
	{
		if (pData[i] == '\n' || pData[i] == '>')
		{
			return i + 1;
		}
	}
	return 0;
}

int stream2pkt::IsValidPkt_IQ60(char* pData, int iLen)
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
