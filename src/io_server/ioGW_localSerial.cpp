#include "pch.h"
#include "ioGW_LocalSerial.h"
#include "commSrv.h"
#include "logger.h"
#include "tdsSession.h"




DWORD WINAPI GWLocalComWorkThread(LPVOID lpParam)
{
	ioGW_LocalSerial* pGW = (ioGW_LocalSerial*)lpParam;
	pGW->m_bWorkingThreadRunning = true;
	char buf[500] = {0};
	while(pGW->m_hCom)
	{
		if (!pGW->m_bRunning)break;

		int iLen = 0;
		pGW->ReadCom(buf,iLen);

		if(iLen > 0)
		{
			pGW->OnRecvData(buf,iLen);
		}
	};
	pGW->m_bWorkingThreadRunning = false;
	pGW->m_signalWorkThreadExit.notify();
	return 0;
}

ioGW_LocalSerial::ioGW_LocalSerial(void)
{
	m_devType = IO_DEV_TYPE::GW::local_serial;
	m_devAddr = "COM1"; 
	m_hCom = NULL;
	m_level = "gateway";
	m_ovWaitEvent.hEvent = CreateEvent(
		NULL,   // default security attributes 
		TRUE,   // manual-reset event 
		FALSE,  // not signaled 
		NULL    // no name
	);

	m_ovRead.hEvent = CreateEvent(
		NULL,   // default security attributes 
		TRUE,   // manual-reset event 
		FALSE,  // not signaled 
		NULL    // no name
	);

	m_ovWrite.hEvent = CreateEvent(
		NULL,   // default security attributes 
		TRUE,   // manual-reset event 
		FALSE,  // not signaled 
		NULL    // no name
	);


	m_portNum = "COM1";
	m_baudRate = 19200;
	m_byteSize = 8;
	m_parity = "None";// None, Odd, Even, Mark, Space
	m_stopBits = "1"; //1 , 1,5 ,2 
}


ioGW_LocalSerial::~ioGW_LocalSerial(void)
{
	stop();
}

bool ioGW_LocalSerial::run()
{
	if (m_hCom == NULL)
	{
		return false;
	}

	DWORD dwThread = 0;
	m_hRecvThread = CreateThread(NULL, 0, GWLocalComWorkThread, (LPVOID)this, 0, &dwThread);
	return true;
}

void ioGW_LocalSerial::stop()
{
	closeCom();//此处必须先closeCom让工作线程从阻塞等待中退出，工作线程才能检测到退出标记位
	ioDev::stop();
}


bool ioGW_LocalSerial::sendData(char* pData, int iLen)
{
	if (m_bEnableIoLog)
		statisOnSend((char*)pData,iLen,getIOAddrStr());
	return WriteCom(pData,iLen);
}

bool ioGW_LocalSerial::ReadCom(char* buf, int& len)
{
	if (m_hCom == NULL || m_hCom == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	DWORD dwEvtMask = 0;


	
	//等待用SetCommMask()函数设置的串口事件发生，共有9种事件可被监视：
	//EV_BREAK，EV_CTS，EV_DSR，EV_ERR，EV_RING，EV_RLSD，EV_RXCHAR，
	//EV_RXFLAG，EV_TXEMPTY；当其中一个事件发生或错误发生时，函数将
	//OVERLAPPED结构中的事件置为有信号状态，并将事件掩码填充到dwMask参数中
	//在openCom函数里面设置了EV_RXCHAR事件

	//如果异步操作不能立即完成的话,函数返回FALSE,并且调用GetLastError()函
	//数分析错误原因后返回ERROR_IO_PENDING,指示异步操作正在后台进行.这种情
	//况下,在函数返回之前系统设置OVERLAPPED结构中的事件为无信号状态
	if (WaitCommEvent(m_hCom, &dwEvtMask, &m_ovWaitEvent))
	{}
	else
	{
		DWORD dwRet = GetLastError();
		if (ERROR_IO_PENDING == dwRet)
		{
			DWORD dwBytesRead = 0;
			//https://docs.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult
			//bWait=TRUE等待层叠读取操作完成
			//CloseHandle关闭m_hCom可以使得阻塞的函数返回
			BOOL bResult = GetOverlappedResult(m_hCom,&m_ovWaitEvent,&dwBytesRead,TRUE); // 阻塞  Block
			if (bResult) {
			
			}
			else {
				return false;
			}
		}
		else if (ERROR_ACCESS_DENIED == dwRet)
		{
			//usb 串口 虚拟串口等，在串口被打开的情况下删除了设备，拔出了usb线等，进入到这里
			LOG("[error]hardware " + m_devAddr + "is deleted,check your hardware connection!");
			closeCom();
			return false;
		}
		else {
			return false;
		}
	}


	COMSTAT comstat;
	DWORD dwError;
	ClearCommError(m_hCom, &dwError, &comstat);

	if (comstat.cbInQue == 0)
		return false;

	BOOL bRet = ReadFile(m_hCom, (LPVOID)(buf), comstat.cbInQue, (LPDWORD)&len, &m_ovRead);//该操作立即返回，因为缓冲区已经有数据
	if (!bRet)
		return false;
	if(len == 0)
		return false;

	
	if(m_bEnableIoLog)
		statisOnRecv((char*)buf,len,getIOAddrStr());

	return true;
}

bool ioGW_LocalSerial::WriteCom(char* buf, int len)
{
	if (m_hCom == NULL || m_hCom == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	DWORD dwLen = 0;
	if (!WriteFile(m_hCom, buf, len, &dwLen, &m_ovWrite))
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
		if (pTdsSession->type == TDS_SESSION_TYPE::terminal)
		{
			m_pab.PushStream(pData, iLen);
			while (m_pab.PopPkt(APP_LAYER_PROTO::textEnd2LF))
			{
				pTdsSession->send(m_pab.pkt, m_pab.iPktLen);
			}
		}
		else
		{
			pTdsSession->send(pData, iLen);
		}
	}

	if (m_pRecvCallback)
	{
		m_pRecvCallback(m_pCallbackUser, pData, iLen);
	}

	return true;
}

bool ioGW_LocalSerial::closeCom()
{
	if (m_hCom == NULL)
		return true;

	HANDLE hCom = m_hCom;
	m_hCom = NULL;
	m_bConnected = false;
	if (m_hRecvThread)
	{
		//必须先关闭readfile阻塞读取，否则closeHandle会阻塞
		//CancelSynchronousIo(m_hRecvThread);
		//现在已经改为非阻塞式的readFile.
		CloseHandle(m_hRecvThread);
	}

	if (hCom)
	{
		BOOL bRet = CloseHandle(hCom);//这里会使得阻塞的 GetOverlappedResult 返回
		if (!bRet)
		{
			m_strErrorInfo = sys::getLastError("CloseHandle");
			return false;
		}
	}
	return true;
}

int ioGW_LocalSerial::parseStopBits(string s)
{
	if (s == "1")
		return 0;
	else if (s == "1.5")
		return 1;
	else if (s == "2")
		return 2;
	return 0;
}

int ioGW_LocalSerial::parseParity(string s)
{
	/* 0-4=None,Odd,Even,Mark,Space    */
	if (s == "None")
		return 0;
	else if (s == "Odd")
		return 1;
	else if (s == "Even")
		return 2;
	else if (s == "Mark")
		return 3;
	else if (s == "Space")
		return 4;
	return 0;
}

bool ioGW_LocalSerial::OpenCom(string confPort, int baudRate, int parity, int byteSize, int stopBits)
{
	return false;
}


bool ioGW_LocalSerial::OpenCom()
{
	m_devAddr = m_portNum;
	if (m_hCom)
	{
		BOOL bRet = CloseHandle(m_hCom);
		if (!bRet)
		{
			m_strErrorInfo = sys::getLastError("CloseHandle");
			return false;
		}
		else
			m_hCom = NULL;
	}
	string  strComPort = _T("\\\\.\\") + m_devAddr;

	m_hCom = CreateFile(strComPort.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0, // 独占方式
		NULL,
		OPEN_EXISTING,// 打开而不是创建
		FILE_FLAG_OVERLAPPED,
		NULL);

	if (m_hCom == INVALID_HANDLE_VALUE)
	{
		m_strErrorInfo = sys::getLastError("CreateFile");
		m_hCom = NULL;
		return false;
	}

	COMSTAT comstat;
	DWORD dwError;
	ClearCommError(m_hCom, &dwError, &comstat);


	//dcb.StopBits = 0, 1, 2对应的是1bit, 1.5bits, 2bits.
	//dcb.ByteSize = 6, 7, 8时   dcb.StopBits不能为1
	//dcb.ByteSize = 5时   dcb.StopBits不能为2
	DCB dcb;
	SecureZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);
	GetCommState(m_hCom, &dcb);
	dcb.BaudRate = m_baudRate;
	dcb.ByteSize = m_byteSize;
	dcb.Parity = parseParity(m_parity);
	dcb.StopBits = parseStopBits(m_stopBits);
	BOOL bRet = SetCommState(m_hCom, &dcb);
	if (!bRet)
	{
		m_strErrorInfo = sys::getLastError("SetCommState");
		CloseHandle(m_hCom);
		m_hCom = NULL;
		return false;
	}

	SetupComm(m_hCom, 1024, 1024);

	COMMTIMEOUTS CommTimeouts;
	ZeroMemory(&CommTimeouts, sizeof(CommTimeouts));
	CommTimeouts.ReadIntervalTimeout = 200;
	CommTimeouts.ReadTotalTimeoutMultiplier = 0;
	CommTimeouts.ReadTotalTimeoutConstant = 2000;
	CommTimeouts.WriteTotalTimeoutMultiplier = 0;
	CommTimeouts.WriteTotalTimeoutConstant = 0;
	SetCommTimeouts(m_hCom, &CommTimeouts);

	PurgeComm(m_hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	SetCommMask(m_hCom, EV_RXCHAR);
	m_bConnected = true;
	return true;
}

bool ioGW_LocalSerial::isOpen()
{
	return m_hCom != NULL;
}


bool ioGW_LocalSerial::OpenCom(string conf)
{
	if (conf != "")
	{
		json j = json::parse(conf);
		string str;
		str = j["portNum"].get<string>();
		m_portNum = str;
		str = j["baudRate"].get<string>();
		m_baudRate = atoi(str.c_str());
		str = j["parity"].get<string>();
		m_parity = str;
		str = j["byteSize"].get<string>();
		m_byteSize = atoi(str.c_str());
		str = j["stopBits"].get<string>();
		m_stopBits = str;
	}
	m_devAddr = m_portNum;

	return OpenCom();
}

