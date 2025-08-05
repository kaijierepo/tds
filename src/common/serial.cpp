#include "serial.h"
#include "common.h"
#include "logger.h"

DWORD WINAPI GWLocalComWorkThread(LPVOID lpParam)
{
	Serial* pGW = (Serial*)lpParam;
	unsigned char buf[500*1000] = {0};

	while (1) {
		if (pGW->m_hCom == nullptr) {
			Sleep(1000);
			if (!pGW->open()) {
				Sleep(5000); //每5秒，尝试重新打开一次串口
			}
		}

		while (pGW->m_hCom)
		{
			size_t iLen = 0;
			pGW->read((char*)buf, iLen);

			if (iLen > 0)
			{
				if(pGW->m_pCallbackUser)
					pGW->m_pCallbackUser->onRecvData_serial(buf, iLen);
				else
					printf("[warn][串口    ]没有设置回调函数，无法处理接收到的数据\n");
			}
		};
	}
	return 0;
}

Serial::Serial(void)
{
	m_pCallbackUser = nullptr;
	m_hCom = NULL;
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

	m_baudRate = 9600;
	m_byteSize = 8;
	m_parity = "None";// None, Odd, Even, Mark, Space
	m_stopBits = "1"; //1 , 1,5 ,2 
}


Serial::~Serial(void)
{
	stop();
}

bool Serial::run(ICallback_serial* pCallback)
{
	//启动后，不断尝试打开串口，如果打开成功，则监听数据接收
	m_pCallbackUser = pCallback;
	DWORD dwThread = 0;
	m_hRecvThread = CreateThread(NULL, 0, GWLocalComWorkThread, (LPVOID)this, 0, &dwThread);
	return true;
}

void Serial::stop()
{
	closeCom();//此处必须先closeCom让工作线程从阻塞等待中退出，工作线程才能检测到退出标记位
}


bool Serial::read(char* buf, size_t& len)
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
			string s = "[error]hardware " + m_comPort + "is deleted,check your hardware connection!";
			printf(s.c_str());
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

	//assert(comstat.cbInQue < 500 * 1000);

	BOOL bRet = ReadFile(m_hCom, (LPVOID)(buf), comstat.cbInQue, (LPDWORD)&len, &m_ovRead);//该操作立即返回，因为缓冲区已经有数据
	if (!bRet)
		return false;
	if(len == 0)
		return false;

	buf[len] = 0;//方便字符串打印

	return true;
}

bool Serial::write(unsigned char* buf, int len)
{
	if (m_hCom == NULL || m_hCom == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	DWORD dwLen = 0;
	if (!WriteFile(m_hCom, buf, len, &dwLen, &m_ovWrite))
	{
		if (GetLastError() == ERROR_IO_PENDING) {
			return true;
		}
		else {
			m_strErrorInfo = sys::getLastError("WriteFile");
			return false;
		}
	}

	return true;
}

bool Serial::closeCom()
{
	if (m_hCom == NULL)
		return true;

	HANDLE hCom = m_hCom;
	m_hCom = NULL;
	//if (m_hRecvThread)
	//{
		//必须先关闭readfile阻塞读取，否则closeHandle会阻塞
		//CancelSynchronousIo(m_hRecvThread);
		//现在已经改为非阻塞式的readFile.
		//CloseHandle(m_hRecvThread);
	//}

	if (hCom)
	{
		BOOL bRet = CloseHandle(hCom);//这里会使得阻塞的 GetOverlappedResult 返回
		if (!bRet)
		{
			m_strErrorInfo = sys::getLastError("CloseHandle");
			return false;
		}
	}

	printf("[warn][串口    ]关闭串口,%s", m_comPort.c_str());
	return true;
}

int Serial::parseStopBits(string s)
{
	if (s == "1")
		return 0;
	else if (s == "1.5")
		return 1;
	else if (s == "2")
		return 2;
	return 0;
}

int Serial::parseParity(string s)
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

bool Serial::open()
{
	return open(m_comPort, m_baudRate, m_parity, m_byteSize, m_stopBits);
}

bool Serial::open(string confPort, int baudRate, string parity, int byteSize, string stopBits)
{
	bool ret = false;
	m_comPort = confPort;
	m_baudRate = baudRate;
	m_parity = parity;
	m_byteSize = byteSize;
	m_stopBits = stopBits;


	if (m_comPort.find("COM") == string::npos)
		return false;

	string  strComPort = "\\\\.\\" + m_comPort;
	if (m_hCom)
	{
		if (!CloseHandle(m_hCom))
		{
			m_strErrorInfo = sys::getLastError("CloseHandle");
			goto OPEN_END;
		}
		else
			m_hCom = NULL;
	}

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
		goto OPEN_END;
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
	if (!SetCommState(m_hCom, &dcb))
	{
		m_strErrorInfo = sys::getLastError("SetCommState");
		CloseHandle(m_hCom);
		m_hCom = NULL;
		goto OPEN_END;
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
	ret = true;

OPEN_END:
	if (ret) {
		LOG("[warn][串口   ]串口打开成功,串口号:%s,baudRate:%d,byteSize:%d,stopBits:%s,parity:%s", m_comPort.c_str(), m_baudRate, m_byteSize, m_stopBits.c_str(), m_parity.c_str());
	}
	else
		LOG("[warn][串口   ]串口打开失败,串口号:%s,baudRate:%d,byteSize:%d,stopBits:%s,parity:%s,错误信息:%s", m_comPort.c_str(), m_baudRate, m_byteSize, m_stopBits.c_str(), m_parity.c_str(), m_strErrorInfo.c_str());
	return ret;
}

bool Serial::isOpen()
{
	return m_hCom != NULL;
}
