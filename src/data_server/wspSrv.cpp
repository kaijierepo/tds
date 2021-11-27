#include "pch.h"
#include "wspSrv.h"
#include "tdscore.h"
#include "logger.h"
#include "ds.h"
#include "tdsSession.h"

wspSrv::wspSrv()
{
	m_pTcpServer = NULL;
}

wspSrv ::~wspSrv()
{

}

void wspSrv::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
}

void wspSrv::OnRecvWSFrame(char* pData, int iLen, shared_ptr<TDS_SESSION> pALC)
{
	CWSPPkt req;
	WS_FrameType type = req.GetFrameType((char*)pData, iLen);
	
	switch (type)
	{
	case WS_ERROR_FRAME:
		break;
	case WS_TEXT_FRAME:
	case WS_CONTINUATION_FRAME:
		{
			req.unpack((char*)pData, iLen);
			string strJson = req.payloadData;
			pALC->m_alBuf.PushStream((char*)req.payloadData, req.iPayloadLen);
			if (req.fin_)
			{
				if (pALC->m_alBuf.PopAllAs(APP_LAYER_PROTO::TDSRPC))
				{
					pALC->iALProto = pALC->m_alBuf.m_protocolType;
					ds.OnRecvAppLayerData((char*)pALC->m_alBuf.pkt, pALC->m_alBuf.iPktLen, pALC,true);
				}
			}	
		}
		break;
	case WS_BINARY_FRAME://do no framing work when binary,used for video and transparent transfer
		{
			req.unpack((char*)pData, iLen);
			ds.OnRecvAppLayerData((char*)req.payloadData, req.iPayloadLen, pALC);
		}
		break;
	case WS_PING_FRAME:
		break;
	case WS_PONG_FRAME:
		break;
	case WS_CLOSING_FRAME:
		//closesocket(pCltInfo->sock);此处是iocp的回调线程，不要close，否则会导致该sock关联的客户端对象无法释放
		break;
	case WS_CONNECT_FRAME:
		break;
	default:
		break;
	}
}

void wspSrv::OnRecvWSData(char* pData, int iLen, stream2pkt* pPab, shared_ptr<TDS_SESSION> pALC)
{
	pPab->PushStream(pData, iLen);
	while (pPab->PopPkt(APP_LAYER_PROTO::PROTOCOL_WEBSOCKET))
	{
		OnRecvWSFrame(pPab->pkt, pPab->iPktLen, pALC);
	}

	if (pPab->iStreamLen > 1*1024*1024)
	{
		string str = str::format("%s",pALC->ip);
		LOG("[error]websocket parse error,can not get a pkt when length exceeded 10Mb,Addr=" + str);
		pPab->Init();
	}
}


void wspSrv::OnOpen()
{

}

void wspSrv::OnClose()
{

}

//将数据加上websocket格式头再发送
//如果数据发送给浏览器之后websocket断开,浏览器端进入onerror,很可能有中文并且不是utf8编码
int wspSrv::sendData(char* sendData, int len, tcpSession* pCltInfo, WS_FrameType ft)
{
	CWSPPkt resp;
	resp.pack(sendData,len, ft);
	if (m_pTcpServer)
	{
		if(m_pTcpServer->SendData((char*)resp.data, resp.len,pCltInfo))
		{
			return len;
		}
		else
		{
			return 0;
		}
		
	}	
	return 0;
}
