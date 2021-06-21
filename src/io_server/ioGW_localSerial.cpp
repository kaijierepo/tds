#include "pch.h"
#include "ioGW_LocalSerial.h"
#include "commSrv.h"


DWORD WINAPI GWLocalComWorkThread(LPVOID lpParam)
{
	ioGW_LocalSerial* pGW = (ioGW_LocalSerial*)lpParam;
	char buf[500] = {0};
	while(1)
	{
		int iLen = 0;
		pGW->ReadCom(buf,iLen);

		if(iLen > 0)
		{
			pGW->OnRecvData(buf,iLen);
		}

		Sleep(20);
	};
}

ioGW_LocalSerial::ioGW_LocalSerial(void)
{
	m_devType = IO_DEV_TYPE::gw_local_serial;
	m_addr = "COM1";
	m_hCom = NULL;
	pTdsSession = NULL;
}


ioGW_LocalSerial::~ioGW_LocalSerial(void)
{
}

bool ioGW_LocalSerial::run()
{
	if (m_hCom == NULL)
	{
		return false;
	}

	DWORD dwThread = 0;
	HANDLE hThread = CreateThread(NULL, 0, GWLocalComWorkThread, (LPVOID)this, 0, &dwThread);
	if (hThread != NULL)
	{
		CloseHandle(hThread);
		hThread = NULL;
	}

	return true;
}


void ioGW_LocalSerial::SendData(char* pData, int iLen)
{
	ioPath addr;
	addr.addr = m_addr;
	commSrv.StatisOnSend((char*)pData,iLen,addr);
	WriteCom(pData,iLen);
}

bool ioGW_LocalSerial::ReadCom(LPVOID buf, int& len)
{
	if (m_hCom == NULL || m_hCom == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	if (!ReadFile(m_hCom, buf, 500, (LPDWORD)&len, NULL))
		return false;

	ioPath addr;
	addr.addr = m_addr;
	commSrv.StatisOnRecv((char*)buf,len,addr);

	return true;
}

bool ioGW_LocalSerial::WriteCom(LPVOID buf, int len)
{
	if (m_hCom == NULL || m_hCom == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	DWORD dwLen = 0;
	if (!WriteFile(m_hCom, buf, len, &dwLen, NULL))
	{
		return false;
	}

	return true;
}

bool ioGW_LocalSerial::OnRecvData(char* pData, int iLen )
{
	for(int i = 0;i<m_vecChild.size();i++)
	{
		m_vecChild.at(i)->OnRecvData(pData,iLen);
	}

	if (pTdsSession)
	{
		pTdsSession->send(pData, iLen);
	}

	return true;
}

bool ioGW_LocalSerial::closeCom()
{
	if (m_hCom)
	{
		CloseHandle(m_hCom);
		m_hCom = NULL;
	}
	return true;
}

bool ioGW_LocalSerial::OpenCom(string conf)
{
	if (conf != "")
	{
		json j = json::parse(conf);

		m_portNum = j["portNum"].get<string>();
		m_baudRate = j["baudRate"].get<int>();
		m_parity = j["parity"].get<int>();
		m_byteSize = j["byteSize"].get<int>();
		m_stopBits = j["stopBits"].get<int>();
	}


	if(m_hCom)
	{
		CloseHandle(m_hCom);
		m_hCom = NULL;
	}
	string  strComPort= _T("\\\\.\\") + m_addr;

	m_hCom = CreateFile(strComPort.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0, // 独占方式
		NULL,
		OPEN_EXISTING,// 打开而不是创建
		0,
		NULL);

	if (m_hCom == INVALID_HANDLE_VALUE)
	{
		m_strErrorInfo = sys::getLastError("CreateFile");
		m_hCom = NULL;
		return false;
	}

	DCB dcb;
	SecureZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);
	GetCommState(m_hCom, &dcb);
	dcb.BaudRate = m_baudRate;
	dcb.ByteSize = m_byteSize;
	dcb.Parity = m_parity;
	dcb.StopBits = m_stopBits; 
	SetCommState(m_hCom, &dcb);

	SetupComm(m_hCom, 1024, 1024);

	COMMTIMEOUTS CommTimeouts;
	ZeroMemory(&CommTimeouts, sizeof(CommTimeouts));
	CommTimeouts.ReadIntervalTimeout = 0;
	CommTimeouts.ReadTotalTimeoutMultiplier = 0;
	CommTimeouts.ReadTotalTimeoutConstant = 1000;
	CommTimeouts.WriteTotalTimeoutMultiplier = 0;
	CommTimeouts.WriteTotalTimeoutConstant = 0;
	SetCommTimeouts(m_hCom, &CommTimeouts);

	PurgeComm(m_hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	return true;
}

