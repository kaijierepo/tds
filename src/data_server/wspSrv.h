#pragma once
#ifndef MYWEBSOCKET_4_0_H
#define MYWEBSOCKET_4_0_H
#include <WinSock2.h>
#include <iostream>
#include <queue>
#include <set>
#pragma comment(lib,"ws2_32.lib")
#define DATA_BUFSIZE 4096
#include "wsProto.h"
#include "tcpSrv.h"
#include "stream2pkt.h"
#include "tdsSession.h"

/*websocket 服务器类*/

const int BUFLEN = 500; //设置字符缓冲区buff大小

enum Client_type   //客户端以什么方式与服务器连接
{
	TCP,
	UDP,
	WEBSOCKET
	//未完待续
};

enum Action_type   //事件类型
{
	SUBSCRIBE,    //页面连接（升级为websocket连接）
	UNSUBSCRIBE,  //连接断开
	MESSAGE		  //收到消息
	//未完待续
};

struct PerIOcontext
{
	OVERLAPPED m_Overlapped;
	SOCKET m_sockAccept;
	WSABUF m_wsaBuf;
	char buffer[DATA_BUFSIZE];
};

struct Action
{
	Action() {}
	Action(Action_type t, SOCKET s) :type(t), client(s) {}
	Action(Action_type t, SOCKET s, char* m, int len)
	{
		type = t;
		client = s;
		msg = new char[len + 1];
		memcpy(msg, m, len);
		nLen = len;
	}

	Action_type type;     //action类型（建立websocket连接，断开连接，消息等）
	SOCKET client;        //要操作的客户端套接字
	//std::string msg;      //传输的消息 !!!不能用string类型（遇到'\0'）会截断
	char* msg;
	int nLen;
};

class wspSrv
{
public:
	wspSrv();
	~wspSrv();

	void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	void OnRecvWSFrame(char* pData, int iLen, shared_ptr<TDS_SESSION> pALC);
	void OnRecvWSData(char* pData, int iLen, stream2pkt* pPab, shared_ptr<TDS_SESSION> pALC);
	void OnOpen();
	void OnClose();

	int sendData(char* sendData, int len, tcpSession* pCltInfo= NULL,WS_FrameType ft= WS_TEXT_FRAME);

	tcpSrv* m_pTcpServer;
};


#endif // !MYWEBSOCKET_4_0_H
