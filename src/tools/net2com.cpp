#include "net2com.h"
#include "logger.h"
#include "kvIni.h"
#include <WinSock2.h>
#include <format>
#include <filesystem>
#include "common.h"
#include <algorithm>


tcp2com::tcp2com() {
	m_bLogToFile = true;
	m_iDestPort = 0;
}

string tcp2com::defaultConf() {
	string s = R"(
#net2com 串口转网络透传网关配置
#基本参数
logStr=0               #0 使用16进制字符串记录日志，1 将数据包原始内存作为utf8字符串记录日志

#串口参数
com=COM1
baudRate=19200
byteSize=8
stopBits=1
parity=None            #None Even Odd

#网络端参数
mode=tcpClient         #tcpClient:网关作为tcp客户端主动连接服务器。 tcpServer:网关作为服务器等待外部连接
registerPktStr={"method":"devRegister","ioAddr":"RS485Gateway_0001"}\n\n    #首发注册包。 ioAddr填入透传网关的地址

#客户端模式参数
remoteIP=127.0.0.1     #client模式下的服务器IP
remotePort=664         #client模式下的服务器端口。TDS的modbusRTU服务端口为664

#服务端模式参数
localIP=               #server模式下绑定的本地ip，留空为0.0.0.0
localPort=663          #server模式下的服务端口
)";

	return s;
}

string tcp2com::getTcpAddr() {
	string s;

	if (m_mode == "tcpClient") {
		s = m_strDestIp + ":" + std::format("{}",m_iDestPort);
	}
	else if (m_mode == "tcpServer") {
		s = m_localIP + ":" + std::format("{}",m_localPort);
	}

	return s;
}

string gb_to_utf8(string instr) { //ansi-->utf-8
	string str;

#ifdef _WIN32
	size_t MAX_STRSIZE = instr.length() * 2 + 2;
	WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
	memset(wcharstr, 0, MAX_STRSIZE);
	MultiByteToWideChar(CP_ACP, 0, (char*)instr.data(), -1, wcharstr, (int)MAX_STRSIZE);
	char* charstr = new char[MAX_STRSIZE];
	memset(charstr, 0, MAX_STRSIZE);
	WideCharToMultiByte(CP_UTF8, 0, wcharstr, -1, charstr, (int)MAX_STRSIZE, NULL, NULL);
	str = charstr;
	delete[] wcharstr;
	delete[] charstr;
#else
	str = instr;
#endif

	return str;
}

string appPath() {
	string str;

	//不要使用std::filesystem::current_path(),这个是当前运行目录，和程序目录可能不一致

#ifdef _WIN32
		//windows获取到的是反斜杠，tds内统一使用斜杠
	TCHAR p[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, p, MAX_PATH);//获取可执行模块的路径

	string strPath = (char*)p;
	size_t nEnd = strPath.rfind('\\');//取最后的"\"号之前地址

	strPath = strPath.substr(0, nEnd);
	strPath = gb_to_utf8(strPath);

	str = str::replace(strPath,"\\", "/");
#else 
	char* p = NULL;
	const int len = 256;

	/// to keep the absolute path of executable's path
	char arr_tmp[len] = { 0 };
	int n = readlink("/proc/self/exe", arr_tmp, len);

	if (NULL != (p = strrchr(arr_tmp, '/'))) {
		*p = '\0';
	}
	else {
		return std::string("");
	}

	str = arr_tmp;
#endif

	return str;
}

bool fileExist(string pszFileName) {
	std::filesystem::path filePath = charCodec::utf8_to_utf16(pszFileName);

	if (std::filesystem::exists(filePath)) {
		return true;
	}
	else if (std::filesystem::is_directory(filePath)) {
		return true;
	}

	return  false;
}

bool writeFile(string path, char* data, size_t len) {
	FILE* fp = nullptr;
	wstring wpath = charCodec::utf8_to_utf16(path);
	_wfopen_s(&fp, wpath.c_str(), L"wb");

	if (fp) {
		fwrite(data, 1, len, fp);
		fclose(fp);

		return true;
	}
	else {
#ifdef _WIN32
		string info = std::format("writeFile,path={0},len={1}", path.c_str(), len);
		DWORD dwErrCode = GetLastError();
		printf("[error]%d", dwErrCode);
#endif
	}

	return false;
}

void tcp2com::run() {
	string confPath = appPath() + "/net2com.ini"; 
	if (!fileExist(confPath)) {
		string s = defaultConf();
		writeFile(confPath, (char*)s.c_str(),s.length());
	}

	KV_INI tdsIni;
	tdsIni.load(confPath);

	string com      = tdsIni.getValStr("com","COM1");
	int baudRate    = tdsIni.getValInt("baudRate", 19200);
	string parity   = tdsIni.getValStr("parity","None");
	int byteSize    = tdsIni.getValInt("byteSize",8);
	string stopBits = tdsIni.getValStr("stopBits","1");

	m_bLogCommPktAsStr = tdsIni.getValInt("logStr", 0);
	m_bLogToFile       = tdsIni.getValInt("logToFile", 1);
	m_bLogToConsole    = tdsIni.getValInt("logToConsole", 1);
	m_mode             = tdsIni.getValStr("mode", "");
	m_strDestIp        = tdsIni.getValStr("remoteIP","127.0.0.1");
	m_iDestPort        = tdsIni.getValInt("remotePort", 664);
	m_localIP          = tdsIni.getValStr("localIP", "0,0,0,0");
	m_localPort        = tdsIni.getValInt("localPort", 663);
	registerPktStr     = tdsIni.getValStr("registerPktStr", "");

	registerPktStr = str::replace(registerPktStr, "\\n", "\n");

	if (m_mode == "tcpClient") {
		if (m_strDestIp != "" && m_iDestPort != 0) {
			tcpClt.run(this, m_strDestIp, m_iDestPort);

			LOG("启动tcp客户端，服务器地址:%s:%d", m_strDestIp.c_str(), m_iDestPort);
		}
		else {
			LOG("tcp客户端参数错误");
		}
	}
	else if (m_mode == "tcpServer") {
		if (m_localPort != 0) {
			tcpServer.run(this,m_localPort, m_localIP);

			LOG("启动tcp服务端，服务器地址:%s:%d", m_localIP.c_str(), m_localPort);
		}
		else {
			LOG("tcp服务端参数错误");
		}
	}
	else if (m_mode == "udpServer") {
		if (m_localPort != 0) {
			udpServer.run(this, m_localPort, m_localIP);

			LOG("启动udp服务端，服务器地址:%s:%d", m_localIP.c_str(), m_localPort);
		}
		else {
			LOG("udp服务端参数错误");
		}
	}
	else {
		LOG("[error]请设置配置文件mode为tcpServer或者tcpClient");
		return;
	}

	if (m_serial.open(com, baudRate,parity, byteSize, stopBits)) {
		m_serial.run(this);

		LOG("打开串口成功: " + com);

		string comParam = "波特率:" + std::format("{}",baudRate) + ",";
		comParam += "停止位:" + stopBits + ",";
		comParam += "数据位:" + std::format("{}",byteSize) + ",";
		comParam += "校验位:" + parity;

		LOG(comParam);
	}
	else {
		LOG("打开串口失败: " + com);
		getchar();
		exit(0);
	}
}

string fromBuff(const char* p, size_t len) {
	string s;
	s.resize(len);
	memcpy(&s[0], p, len);

	return s;
}

string bytesToHexStr(char* p, size_t len, string splitter) {
	string str;
	for (int i = 0; i < len; i++) {
		string b = format("%02X", (unsigned char)p[i]);
		str += b;
		str += splitter;
	}

	return str;
}

void tcp2com::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn) {
	
}

void tcp2com::onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pCltInfo) {
	m_serial.write(pData, iLen);

	string log;
	if (m_bLogCommPktAsStr) {
		log = fromBuff((const char*)pData, iLen);
	}
	else {
		log = bytesToHexStr((char*)pData, iLen, " ");
	}

	LOG(m_serial.m_comPort + " <-- " + getTcpAddr() + "  " + log);
}

void tcp2com::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) {
	if (bIsConn) {
		LOG("连接Tcp服务成功: " + m_strDestIp + ":" + std::format("{}",m_iDestPort));

		string s = registerPktStr;
		if (s.length() > 0) {
			tcpClt.SendData((char*)s.c_str(), s.length());

			LOG("发送首发注册包,长度=%d,[%s]", s.length(), s.c_str());
		}
	}
	else {
		LOG("Tcp连接断开: " + m_strDestIp + ":" + std::format("{}",m_iDestPort));
	}
}

void tcp2com::onRecvData_tcpClt(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo) {
	m_serial.write(pData, iLen);

	string log;
	if (m_bLogCommPktAsStr) {
		log = fromBuff((char*)pData, iLen);
	}
	else {
		log = bytesToHexStr((char*)pData, iLen," ");
	}

	LOG(m_serial.m_comPort + " <-- " + getTcpAddr() + "  " + log);
}

void tcp2com::statusChange_serial(bool bIsOpen) {
}

void tcp2com::onRecvData_serial(unsigned char* pData, size_t iLen) {
	string netAddr;
	if (m_mode == "tcpClient") {
		tcpClt.SendData(pData, iLen);
	}
	else if (m_mode == "tcpServer") {
		tcpServer.SendData((char*)pData, iLen);
	}
	else if (m_mode == "udpServer") {
		netAddr = std::format("UDP-{0}:{1}({2})", m_strDestIp.c_str(), m_iDestPort, iLen);
		udpServer.SendData(pData, iLen, m_strDestIp, m_iDestPort);
	}

	string log;
	if (m_bLogCommPktAsStr) {
		log = fromBuff((char*)pData, iLen);
	}
	else {
		log = bytesToHexStr((char*)pData, iLen," ");
	}

	LOG(m_serial.m_comPort + " --> " + netAddr + "  " + log);
}

void tcp2com::OnRecvUdpData(unsigned char* pData, size_t iLen, UDP_SESSION udpSession) {
	m_serial.write(pData, iLen);

	string log = bytesToHexStr((char*)pData, iLen," ");
	string s = std::format("UDP-{0}:{1}({2})", udpSession.remoteIP.c_str(),udpSession.remotePort,iLen);

	LOG(m_serial.m_comPort + " <-- " +  s + "  " + log);
}
