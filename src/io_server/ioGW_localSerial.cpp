#include "pch.h"
#include "ioGW_localSerial.h"
#include "logger.h"
#include "tdsSession.h"
#include "ioSrv.h"
#include "webSrv.h"

#ifdef ENABLE_SERIAL

void refreshSerialIODev()
{
	//从操作系统的设备管理器获得串口列表信息
	vector<sys::COM_INFO> aryNew;
	aryNew = sys::getCOMInfoList();


	//新列表里有，当前列表没有。  为新上线的串口创建对应的ioDev.置为在线
	for (auto& i : aryNew)
	{
		sys::COM_INFO ci = i;
		ioDev* ls = ioSrv.getIODev(ci.portNum);
		if (!ls)
		{
			ls = ioSrv.onChildDevDiscovered(ci.portNum, ci.portNum, DEV_TYPE_local_serial);
			if (ls)
				ls->m_devTypeLabel = ci.desc;
		}
	}

	//当前列表里有，新列表没有。 表示离线。 离线的空闲设备，从io设备列表中删除。已配置设备保留
	vector<ioDev*> ary = ioSrv.getChildren(DEV_TYPE_local_serial);
	for (auto& i : ary)
	{
		bool bOnline = false;
		//在新列表里面找的到才在线
		for (auto& newStatus : aryNew)
		{
			if (newStatus.portNum == i->getIOAddrStr())
			{
				bOnline = true;
			}
		}

		if (bOnline == false && i->m_dispositionMode == DEV_DISPOSITION_MODE::spare)
		{
			ioSrv.deleteDescendant(i);
		}
	}
}

namespace ns_ioGW_LocalSerial {

	void searchDev() {
		refreshSerialIODev();
	}

	ioDev* createDev()
	{
		return new ioGW_LocalSerial();
	}
	class createReg {
	public:
		createReg() {
			mapDevCreateFunc["local-serial"] = createDev;
			mapDevSearchFunc["local-serial"] = searchDev;
			mapDevTypeLabel["local-serial"] = "本地串口";
		};
	};
	createReg reg;
}


DWORD WINAPI GWLocalComWorkThread(LPVOID lpParam)
{
	ioGW_LocalSerial* pGW = (ioGW_LocalSerial*)lpParam;
	pGW->m_bWorkingThreadRunning = true;
	unsigned char buf[500*1000] = {0};

	while (pGW->m_bRunning) {
		if (pGW->m_hCom == nullptr) {
			timeopt::sleepMilli(100);
			if (!pGW->connect()) {
				timeopt::sleepMilli(5000); //每5秒，尝试重新打开一次串口
			}
		}

		while (pGW->m_hCom)
		{
			if (!pGW->m_bRunning)break;

			size_t iLen = 0;
			pGW->ReadCom((char*)buf, iLen);

			if (iLen > 0)
			{
				pGW->onRecvData(buf, iLen);
			}
		};
	}


	pGW->m_bWorkingThreadRunning = false;
	pGW->m_signalWorkThreadExit.notify();
	return 0;
}

ioGW_LocalSerial::ioGW_LocalSerial(void)
{
	m_devType = "local-serial";
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

	m_baudRate = 9600;
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
	if (m_dispositionMode == DEV_DISPOSITION_MODE::spare) {
		return false;
	}

	//启动后，不断尝试打开串口，如果打开成功，则监听数据接收
	DWORD dwThread = 0;
	if (!m_bWorkingThreadRunning) {
		m_hRecvThread = CreateThread(NULL, 0, GWLocalComWorkThread, (LPVOID)this, 0, &dwThread);
	}
	return true;
}

void ioGW_LocalSerial::stop()
{
	closeCom();//此处必须先closeCom让工作线程从阻塞等待中退出，工作线程才能检测到退出标记位
	ioDev::stop();
}


bool ioGW_LocalSerial::sendData(unsigned char* pData, size_t iLen)
{
	bool ret = WriteCom(pData,iLen);
	IOLogSend((unsigned char*)pData, iLen, ret,getIOAddrStr(),"host",  getIOAddrStr());
	lastSendTime = timeopt::now();
	m_sendBytes += iLen;
	return ret;
}


void thread_localSerialCycleTask(ioGW_LocalSerial* p) {
	for (int i = 0; i < p->m_vecChildDev.size(); i++)
	{
		ioDev* pChild = p->m_vecChildDev[i];
		pChild->DoCycleTaskSync();

		while (1) {
			if (!p->isBusBusy())  //某些特殊的串口设备，即使接收到了数据，也要等待一会再发下一包，否则会产生通信错误，该机制用于此种情况
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
		}
	}
	p->m_bCycleAcqThreadRunning = false;
}


void ioGW_LocalSerial::DoCycleTask()
{
	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval)
	{
		if (!m_bCycleAcqThreadRunning) {
			m_bCycleAcqThreadRunning = true;
			thread t(thread_localSerialCycleTask, this);
			t.detach();
			m_stLastAcqTime = timeopt::now();
		}
	}
}

bool ioGW_LocalSerial::ReadCom(char* buf, size_t& len)
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

	assert(comstat.cbInQue < 500 * 1000);

	BOOL bRet = ReadFile(m_hCom, (LPVOID)(buf), comstat.cbInQue, (LPDWORD)&len, &m_ovRead);//该操作立即返回，因为缓冲区已经有数据
	if (!bRet)
		return false;
	if(len == 0)
		return false;

	buf[len] = 0;//方便字符串打印

	return true;
}

bool ioGW_LocalSerial::WriteCom(unsigned char* buf, int len)
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

bool ioGW_LocalSerial::onRecvData(unsigned char* pData, size_t iLen )
{
	setOnline();

	//m_lastBusRecvTime = timeopt::now();

	string s;
	str::fromBuff(pData, iLen,s);
	IOLogRecv((unsigned char*)pData, iLen, getIOAddrStr(),"host", getIOAddrStr());
	lastRecvTime = timeopt::now();
	m_recvBytes += iLen;

	for(int i = 0;i<m_vecChildDev.size();i++)
	{
		m_vecChildDev.at(i)->onRecvData(pData,iLen);
	}

	if (pSessionClientBridge)
	{
		if (pSessionClientBridge->type == TDS_SESSION_TYPE::terminal)
		{
			m_pab.PushStream((unsigned char*)pData, iLen);
			while (m_pab.PopPkt(APP_LAYER_PROTO::terminalPrompt))
			{
				pSessionClientBridge->send((char*)m_pab.pkt, m_pab.iPktLen);
			}
		}
		else
		{
			string s1 = str::fromBuff((char*)pData, iLen);
			LOG("[WS串口]" + s1);
			pSessionClientBridge->send(pData, iLen);
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

	setOffline();
	LOG("[warn][串口    ]关闭串口,%s", getDevAddrStr().c_str());
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


bool ioGW_LocalSerial::toJson(yyjson_mut_val* conf, yyjson_mut_doc* doc, DEV_QUERIER querier)
{
	ioDev::toJson(conf,doc, querier);

	if (querier.getConf) {
		yyjson_mut_val* jSerial = yyjson_mut_obj(nullptr);
		yyjson_mut_obj_add_int(doc,jSerial, "baudRate", m_baudRate);
        yyjson_mut_obj_add_int(doc,jSerial, "byteSize", m_byteSize);
        yyjson_mut_obj_add_strcpy(doc,jSerial, "parity", m_parity.c_str());
        yyjson_mut_obj_add_strcpy(doc,jSerial, "stopBits", m_stopBits.c_str());
		yyjson_mut_obj_add_val(doc, conf, "serial", jSerial);
	}
	return false;
}

bool ioGW_LocalSerial::loadConf(json& conf)
{
	ioDev::loadConf(conf);

	if (conf.contains("serial")) {
		json jSerial = conf["serial"];
		m_baudRate = jSerial["baudRate"].get<int>();
		m_byteSize = jSerial["byteSize"].get<int>();
		m_parity = jSerial["parity"].get<string>();
		m_stopBits = jSerial["stopBits"].get<string>();
	}

	return false;
}

string ioGW_LocalSerial::getConnInfo()
{
	string s = getDevAddrStr();
	string s1 = str::format("%d,%d,%s,%s", m_baudRate, m_byteSize, m_parity.c_str(), m_stopBits.c_str());
	return s+s1;
}

bool ioGW_LocalSerial::isConnected()
{
	return m_hCom != NULL;
}

bool ioGW_LocalSerial::connect()
{
	return OpenCom();
}

bool ioGW_LocalSerial::disconnect()
{
	return closeCom();
}

bool ioGW_LocalSerial::connect(json params)
{
	return OpenCom(params.dump());
}

bool ioGW_LocalSerial::OpenCom(string confPort, int baudRate, int parity, int byteSize, int stopBits)
{
	return false;
}


bool ioGW_LocalSerial::OpenCom()
{
	bool ret = false;
	string comPort = getDevAddrStr();

	if (comPort.find("COM") == string::npos)
		return false;

	string  strComPort = "\\\\.\\" + comPort;
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
	m_bConnected = true;
	ret = true;

OPEN_END:
	if (ret) {
		LOG("[warn][串口   ]串口打开成功,串口号:%s,baudRate:%d,byteSize:%d,stopBits:%s,parity:%s", getDevAddrStr().c_str(), m_baudRate, m_byteSize, m_stopBits.c_str(), m_parity.c_str());
		setOnline();
	}
	else
		LOG("[warn][串口   ]串口打开失败,串口号:%s,baudRate:%d,byteSize:%d,stopBits:%s,parity:%s,错误信息:%s", getDevAddrStr().c_str(), m_baudRate, m_byteSize, m_stopBits.c_str(), m_parity.c_str(), m_strErrorInfo.c_str());
	return ret;
}

bool ioGW_LocalSerial::isOpen()
{
	return m_hCom != NULL;
}


bool ioGW_LocalSerial::OpenCom(string conf)
{
	bool ret = false;
	lock_conf_unique();
	if (m_bConnected)
	{
		ret = true;
	}
	else {
		if (conf != "")
		{
			json j = json::parse(conf);
			string str;
			m_jDevAddr = j["com"].get<string>();
			m_baudRate = j["baudRate"].get<int>();
			m_parity = j["parity"].get<string>();
			m_byteSize = j["byteSize"].get<int>();
			m_stopBits = j["stopBits"].get<string>();
		}
		m_devAddr = getDevAddrStr();
		ret = OpenCom();
	}
	unlock_conf_unique();
	return ret;
}

#endif 
