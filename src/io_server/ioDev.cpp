#include "pch.h"
#include "ioDev.h"
#include "ioChan.h"
#include "logger.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "webSrv.h"
#include "tdb.h"
#include "yyjson.h"
#include "statusServer.h"
#include "tAlmSrv.h"


int devIdIdx = 0;
mutex g_csDevId;

string generateDevId() {
	g_csDevId.lock();
	string s = timeopt::nowStr(true) + "_" + str::format("%d", devIdIdx++);
	g_csDevId.unlock();
	return s;
}

bool isBatchLink(string addr)
{
	if (addr.find("#") != string::npos)
	{
		return true;
	}
	else
	{
		return false;
	}
}

//使用函数返回，避免全局变量构造顺序问题导致还没构造就被调用
std::map<string, fp_createDev>& getMapDevCreateFunc() {
	static std::map<string, fp_createDev> inst;
	return inst;
}

std::map<string, fp_searchDev>& getMapDevSearchFunc() {
	static std::map<string, fp_searchDev> inst;
	return inst;
}

std::map<string, string>& getMapDevTypeLabel() {
	static std::map<string, string> inst;
	return inst;
}

ioDev* createIODev(string type)
{
	if (type == "")
		return nullptr;

    ioDev* p = NULL;
	
	if (mapDevCreateFunc.find(type) != mapDevCreateFunc.end())
	{
		fp_createDev func_create = mapDevCreateFunc[type];
		p = func_create();
	}
	else if (type == "genicam")
	{
#ifdef ENABLE_GENICAM
		p = new ioDev_genicam();
#endif
	}
	else
	{
		LOG("[warn]不支持设备类型:" + type + ",您可以联系厂家获取支持该类型设备的专业版");
	}

	if (p)
	{
		p->m_confNodeId = generateDevId();
	}

	return p;
}

void ioDev::output(string chanAddr, json jVal, json& rlt, json& err, bool sync)
{
}

bool ioDev::input(json jVal, string addr, TIME* stDataTime) {
	TIME st;
	if (stDataTime == nullptr) {
		timeopt::now(&st);
	}
	else {
		st = *stDataTime;
	}

	string tagBind;
	if (m_mapDataChannel.find(addr) != m_mapDataChannel.end()) {
		ioChannel* pC = m_mapDataChannel[addr];
		pC->input(jVal, tagBind, &st);
	}
	else {
		return false;
	}

	if (tagBind != "") {
		json param;
		param["tag"] = tagBind;
		param["val"] = jVal;
		param["time"] = timeopt::st2str(st);
		tds->callAsyn("input", param);
	}

	return true;
}

bool ioDev::input(vector<string> chanAddr, vector<json> val, TIME* stDataTime)
{
	TIME st;
	if (stDataTime == nullptr) {
		timeopt::now(&st);
	}
	else {
		st = *stDataTime;
	}

	json inputTags = json::array();
	json inputVals = json::array();
	for (int i = 0; i < chanAddr.size(); i++) {
		string addr = chanAddr[i];
		json& jVal = val[i];
		if (m_mapDataChannel.find(addr) != m_mapDataChannel.end()) {
			ioChannel* pC = m_mapDataChannel[addr];
			string tagBind;
			pC->input(jVal,tagBind,&st);

			if (tagBind != "") {
				inputTags.push_back(tagBind);
				inputVals.push_back(pC->m_curVal);
			}
		}
	}

	if (inputTags.size() > 0) {
		//更新绑定位号值
		json param;
		param["tag"] = inputTags;
		param["val"] = inputVals;
		param["time"] = timeopt::st2str(st);
		tds->callAsyn("input", param);
	}
	

	return false;
}

void ioDev::AutoDataLink(OBJ* mo) {

}


bool ioDev::IsGateway()
{
	if (m_devType == "can_gateway" ||
		m_devType == "modbus_gateway")
	{
		return true;
	}

	return false;
}

bool ioDev::m_bAsynAcqMode = false;
int ioDev::m_heartBeatInterval = 3;
int ioDev::m_pingInterval = 30000;
bool ioDev::m_defaultOnline = false;
int ioDev::m_offlineConfirmCount = 1;

ioDev::ioDev(void)
{
	m_bPingThreadRunning = false;
	m_bWorkingThreadRunning = false;
	m_bAcqThreadRunning = false;
	m_bIsWaitingResp = false;
	m_bEnableIoLog = true;
	m_bEnableAcq = true;
	m_bRunning = true; //是否启动了自动工作 （采集线程是否启动）
	m_bEnableAcq = true;
	m_chanTimeSync = false;
	m_dispositionMode = DEV_DISPOSITION_MODE::managed;
	m_pParent = NULL;
	m_bEnableOfflineTimeout = false;
	m_bOnline = ioDev::m_defaultOnline;
	m_iSendDataFailCount = 0;
	m_tcpClt = nullptr;
	m_udpClt = nullptr;
	m_udpSrv = nullptr;
	memset(&m_stLastHeartbeatTime, 0, sizeof(TIME));
	memset(&m_stLastPingTime, 0, sizeof(TIME));
	memset(&m_stLastSetClockTime, 0, sizeof(TIME));
	memset(&m_stOnlineTime, 0, sizeof(TIME));
	timeopt::now(&m_stOfflineTime);
	timeopt::setAsTimeOrg(m_stLastChanDataTime);
	timeopt::setAsTimeOrg(m_stLastAcqTime);
	timeopt::setAsTimeOrg(m_stLastAlarmStatusTime);
	timeopt::setAsTimeOrg(lastSendTime);
	timeopt::setAsTimeOrg(lastRecvTime);
	timeopt::now(&m_stLastActiveTime);
	m_pMO = NULL;
	m_pRecvCallback = NULL;
	m_pCallbackUser = NULL;
	pSessionClientBridge = NULL;
	m_fAcqInterval = 30;
	pIOSession = NULL;
	m_onlineInfoQueried = false;
	m_acqMode = "all";
	m_acqAlarm = false;
	m_bViaAdaptor = false;
	m_avgTransactionTime = 0;
	m_sendBytes = 0;
	m_recvBytes = 0;
	m_abandonLen = 0;
	m_transactionSuccessCount = 0;
	m_transactionFailCount = 0;
	m_bCycleAcqThreadRunning = false;
	m_bRecvProcessing = false;
	m_bOutputting = false;
	m_offlineCount = 0;
	m_ioMode = "none";
	m_unhandledRecvPktCount = 0;
	m_reconnectMode = "";
	m_reconnectTime = 0;
	timeopt::now(&m_stLastReconnectTime);
}

ioDev::~ioDev(void)
{
	if (pIOSession)
	{
		pIOSession->unbindIoDev(this);
		ioSrv.handleDevOnlineAsyn(pIOSession->m_ioAddr, pIOSession);
	}
}

bool ioDev::run() {
	m_bRunning = true;

	if (m_bViaAdaptor) {
		//通过适配器无需进行通信模块初始化
	}
	else if (m_addrType == DEV_ADDR_MODE::tcpServer || m_addrType == DEV_ADDR_MODE::udpServer || m_addrType == DEV_ADDR_MODE::udpClient) {
		string ip;
		if (m_jDevAddr["ip"].is_string()) {
			ip = m_jDevAddr["ip"].get<string>();
		}
		else {
			LOG("[error]IODev启动失败,地址配置异常,ip必须是一个字符串,设备地址模式=%s,配置信息:%s", m_addrType.c_str(), m_jDevAddr.dump().c_str());
			return false;
		}

		int port = 0;
		if (m_addrType == DEV_ADDR_MODE::tcpServer || m_addrType == DEV_ADDR_MODE::udpServer || m_addrType == DEV_ADDR_MODE::udpClient) {
			if (m_jDevAddr["port"].is_number_integer()){
				port = m_jDevAddr["port"].get<int>();
			}
			else {
				LOG("[error]IODev启动失败,地址配置异常,port必须是一个整数,设备地址模式=%s,配置信息:%s", m_addrType.c_str(), m_jDevAddr.dump().c_str());
				return false;
			}
		}

		//udp模式下，配置localPort不为0，用于设备进行udp回包时，不回给请求的udp客户端，而是发往固定端口的场景。
		//localPort不是必须的
		int localPort = 0;
		if (m_jDevAddr["localPort"].is_number_integer()) {
			localPort = m_jDevAddr["localPort"].get<int>();
		}

		if (m_addrType == DEV_ADDR_MODE::tcpServer) {
			if (m_tcpClt == nullptr)
				m_tcpClt = new tcpClt();

			m_tcpClt->run(this, ip, port); //逐步把 ioSrv 中的 onRecvData_tcpClt重构掉，放在ioDev对象内部处理 tcpClient接收数据更合理
			LOG("[IO设备]启动设备,地址模式:%s,设备类型:%s,远端地址:%s,本地启动tcpClient,本地IP:%s", m_addrType.c_str(), m_devType.c_str(), getDevAddrStr().c_str(), ioSrv.m_ioSrvIP.c_str());
		}
		else if((m_addrType == DEV_ADDR_MODE::udpServer || m_addrType == DEV_ADDR_MODE::udpClient) && port > 0) {
			if (m_udpClt == nullptr)
				m_udpClt = new UdpClt();

			m_udpClt->run(this,0,ioSrv.m_ioSrvIPAsClient);
			LOG("[IO设备]启动设备,地址模式:%s,设备类型:%s,远端地址:%s,本地启动udpClient,本地IP:%s,本地端口:%d", m_addrType.c_str(), m_devType.c_str(), getDevAddrStr().c_str(), ioSrv.m_ioSrvIP.c_str(),m_udpClt->m_port);
		}
	}

	return true;
}

void ioDev::stop()
{
	m_bRunning = false;
	if(m_tcpClt)
		m_tcpClt->stop();
	if (m_udpClt)
		m_udpClt->stop();
	if (m_udpSrv)
		m_udpSrv->stop();

	for (auto i : m_vecChildDev)
	{
		i->stop();
	}
	while (m_bCycleAcqThreadRunning || m_bRecvProcessing || m_bOutputting) {
		timeopt::sleepMilli(1);
	}
}

bool ioDev::toJson(yyjson_mut_val* conf, yyjson_mut_doc* doc, DEV_QUERIER querier)
{
	DEV_CONF_LOCK_SHARED(this);
	yyjson_mut_val* key;
	yyjson_mut_val* val;
	//配置数据 - 保存在配置文件中
	//重构json为yyjson
	if (querier.getConf) {
		//conf["addrMode"] = m_addrType;
		key = yyjson_mut_strcpy(doc, "addrMode");
		val = yyjson_mut_strcpy(doc, m_addrType.c_str());
		yyjson_mut_obj_put(conf, key, val);
        
		//conf["addr"] = m_jDevAddr;
		key = yyjson_mut_strcpy(doc, "addr");
		string s = m_jDevAddr.dump();
		yyjson_doc* jDoc = yyjson_read(s.c_str(), s.size(), 0);
		yyjson_val* yyDevAddr = yyjson_doc_get_root(jDoc);
		yyjson_mut_val* jDevAddrMut = yyjson_val_mut_copy(doc, yyDevAddr);
        yyjson_mut_obj_put(conf, key, jDevAddrMut);
        yyjson_doc_free(jDoc);

		//conf["ioMode"] = m_ioMode;
        key = yyjson_mut_strcpy(doc, "ioMode");
        val = yyjson_mut_strcpy(doc, m_ioMode.c_str());
        yyjson_mut_obj_put(conf, key, val);

		//conf["type"] = m_devType;
        key = yyjson_mut_strcpy(doc, "type");
        val = yyjson_mut_strcpy(doc, m_devType.c_str());
        yyjson_mut_obj_put(conf, key, val);


		//conf["typeLabel"] = getDevTypeLabel(m_devType);
        key = yyjson_mut_strcpy(doc, "typeLabel");
        val = yyjson_mut_strcpy(doc, getDevTypeLabel(m_devType).c_str());
        yyjson_mut_obj_put(conf, key, val);


		if (m_devSubType != "") {
			//conf["subType"] = m_devSubType;
            key = yyjson_mut_strcpy(doc, "subType");
            val = yyjson_mut_strcpy(doc, m_devSubType.c_str());
            yyjson_mut_obj_put(conf, key, val);
			//conf["subTypeLabel"] = getDevSubTypeLabel(m_devSubType);
            key = yyjson_mut_strcpy(doc, "subTypeLabel");
            val = yyjson_mut_strcpy(doc, getDevSubTypeLabel(m_devSubType).c_str());
            yyjson_mut_obj_put(conf, key, val);
		}

		//conf["level"] = m_level;
        key = yyjson_mut_strcpy(doc, "level");
        val = yyjson_mut_strcpy(doc, m_level.c_str());
        yyjson_mut_obj_put(conf, key, val);

		//conf["manageStatus"] = m_dispositionMode;
        key = yyjson_mut_strcpy(doc, "manageStatus");
        val = yyjson_mut_strcpy(doc, m_dispositionMode.c_str());
        yyjson_mut_obj_put(conf, key, val);

		//conf["acqInterval"] = m_fAcqInterval;
        key = yyjson_mut_strcpy(doc, "acqInterval");
        val = yyjson_mut_real(doc, m_fAcqInterval);
        yyjson_mut_obj_put(conf, key, val);

		//conf["enableAcq"] = m_bEnableAcq;
        key = yyjson_mut_strcpy(doc, "enableAcq");
        val = yyjson_mut_bool(doc, m_bEnableAcq);
        yyjson_mut_obj_put(conf, key, val);

		if (m_strTagBind != "") {
			//conf["tagBind"] = m_strTagBind;
            key = yyjson_mut_strcpy(doc, "tagBind");
            val = yyjson_mut_strcpy(doc, m_strTagBind.c_str());
            yyjson_mut_obj_put(conf, key, val);
		}
			
		if (m_strChanTemplate != "") {
			//conf["chanTemplate"] = m_strChanTemplate;
            key = yyjson_mut_strcpy(doc, "chanTemplate");
			val = yyjson_mut_strcpy(doc, m_strChanTemplate.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}

		//conf["nodeID"] = m_confNodeId;
        key = yyjson_mut_strcpy(doc, "nodeID");
		val = yyjson_mut_strcpy(doc, m_confNodeId.c_str());
		yyjson_mut_obj_put(conf, key, val);


		if (m_acqMode != "") {
			//conf["acqMode"] = m_acqMode;
            key = yyjson_mut_strcpy(doc, "acqMode");
            val = yyjson_mut_strcpy(doc, m_acqMode.c_str());
            yyjson_mut_obj_put(conf, key, val);
		}

		if (m_chanGroup != "") {
			//conf["chanGroup"] = m_chanGroup;
            key = yyjson_mut_strcpy(doc, "chanGroup");
            val = yyjson_mut_strcpy(doc, m_chanGroup.c_str());
            yyjson_mut_obj_put(conf, key, val);
		}

		if (m_chanTimeSync) {
			//conf["chanTimeSync"] = true;
            key = yyjson_mut_strcpy(doc, "chanTimeSync");
            val = yyjson_mut_bool(doc, m_chanTimeSync);
            yyjson_mut_obj_put(conf, key, val);
		}
			
		//conf["acqAlarm"] = m_acqAlarm;
        key = yyjson_mut_strcpy(doc, "acqAlarm");
        val = yyjson_mut_bool(doc, m_acqAlarm);
        yyjson_mut_obj_put(conf, key, val);

		if (m_strUser != "") {
			//conf["user"] = m_strUser;
            key = yyjson_mut_strcpy(doc, "user");
            val = yyjson_mut_strcpy(doc, m_strUser.c_str());
            yyjson_mut_obj_put(conf, key, val);
		};
		if (m_strPwd != "") {
			//conf["pwd"] = m_strPwd;
            key = yyjson_mut_strcpy(doc, "pwd");
			val = yyjson_mut_strcpy(doc, m_strPwd.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}

		if (m_subTopics != "") {
			key = yyjson_mut_strcpy(doc, "subTopics");
			val = yyjson_mut_strcpy(doc, m_subTopics.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}

		if (m_translatorProto != "") {
			//conf["translatorProto"] = m_translatorProto;
            key = yyjson_mut_strcpy(doc, "translatorProto");
            val = yyjson_mut_strcpy(doc, m_translatorProto.c_str());
            yyjson_mut_obj_put(conf, key, val);
		}
		if (m_standAloneIOType != "") {
			//conf["standAloneIOType"] = m_standAloneIOType;
            key = yyjson_mut_strcpy(doc, "standAloneIOType");
            val = yyjson_mut_strcpy(doc, m_standAloneIOType.c_str());
            yyjson_mut_obj_put(conf, key, val);
		}
		if (m_cycleTaskScript != "") {
			//conf["cycleTaskScript"] = m_cycleTaskScript;
            key = yyjson_mut_strcpy(doc, "cycleTaskScript");
            val = yyjson_mut_strcpy(doc, m_cycleTaskScript.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
		if (m_outputScript != "") {
			//conf["outputScript"] = m_outputScript;
            key = yyjson_mut_strcpy(doc, "outputScript");
			val = yyjson_mut_strcpy(doc, m_outputScript.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
		if (m_onRecvScript != "") {
			//conf["onRecvScript"] = m_onRecvScript;
            key = yyjson_mut_strcpy(doc, "onRecvScript");
            val = yyjson_mut_strcpy(doc, m_onRecvScript.c_str());
            yyjson_mut_obj_put(conf, key, val);
		}
		if (m_tdspScript != "") {
			//conf["tdspScript"] = m_tdspScript;
            key = yyjson_mut_strcpy(doc, "tdspScript");
            val = yyjson_mut_strcpy(doc, m_tdspScript.c_str());
            yyjson_mut_obj_put(conf, key, val);
		}

		if (m_bEnableOfflineTimeout) {
			//conf["enableOfflineTimeout"] = m_bEnableOfflineTimeout;
            key = yyjson_mut_strcpy(doc, "enableOfflineTimeout");
            val = yyjson_mut_bool(doc, m_bEnableOfflineTimeout);
            yyjson_mut_obj_put(conf, key, val);
			//conf["offlineTimeout"] = m_offlineTimeout;
            key = yyjson_mut_strcpy(doc, "offlineTimeout");
            val = yyjson_mut_int(doc, m_offlineTimeout);
            yyjson_mut_obj_put(conf, key, val);
		}

		if (m_bEnableHttpHeartbeat) {
			//conf["enableHttpHeartbeat"] = m_bEnableHttpHeartbeat;
            key = yyjson_mut_strcpy(doc, "enableHttpHeartbeat");
            val = yyjson_mut_bool(doc, m_bEnableHttpHeartbeat);
            yyjson_mut_obj_put(conf, key, val);
			//conf["httpHeartbeatUrl"] = m_httpHeartbeatUrl;
            key = yyjson_mut_strcpy(doc, "httpHeartbeatUrl");
            val = yyjson_mut_strcpy(doc, m_httpHeartbeatUrl.c_str());
            yyjson_mut_obj_put(conf, key, val);
		}

		if (m_bEnablePingOnlineCheck) {
			//conf["enablePingOnlineCheck"] = m_bEnablePingOnlineCheck;
            key = yyjson_mut_strcpy(doc, "enablePingOnlineCheck");
            val = yyjson_mut_bool(doc, m_bEnablePingOnlineCheck);
            yyjson_mut_obj_put(conf, key, val);
		}

		if (m_devType == DEV_TYPE::DEV::tdsp_device && m_devSubType == TDSP_SUB_TYPE::childTds) {
			//conf["tdsVersion"] = m_tdsVersion;
			key = yyjson_mut_strcpy(doc, "tdsVersion");
			val = yyjson_mut_strcpy(doc, m_tdsVersion.c_str());
			yyjson_mut_obj_put(conf, key, val);

			yyjson_mut_val* yyv_serverStauts = yyjson_mut_obj(doc);
			//conf["serverStatus"]["cpu"] = m_tdsSrvStatus.cpu;
            key = yyjson_mut_strcpy(doc, "cpu");
            val = yyjson_mut_real(doc, m_tdsSrvStatus.cpu);
            yyjson_mut_obj_put(yyv_serverStauts, key, val);
			
			//conf["serverStatus"]["mem"] = m_tdsSrvStatus.mem;
            key = yyjson_mut_strcpy(doc, "mem");
            val = yyjson_mut_real(doc, m_tdsSrvStatus.mem);
            yyjson_mut_obj_put(yyv_serverStauts, key, val);
			
			//conf["serverStatus"]["handle"] = m_tdsSrvStatus.handle;
            key = yyjson_mut_strcpy(doc, "handle");
            val = yyjson_mut_int(doc, m_tdsSrvStatus.handle);
            yyjson_mut_obj_put(yyv_serverStauts, key, val);

			key = yyjson_mut_strcpy(doc, "serverStatus");
            yyjson_mut_obj_put(conf, key, yyv_serverStauts);

			//conf["statusUpdateTime"] = m_statusUpdateTime;
            key = yyjson_mut_strcpy(doc, "statusUpdateTime");
            val = yyjson_mut_strcpy(doc, m_statusUpdateTime.c_str());
            yyjson_mut_obj_put(conf, key, val);

			//conf["serverUpgradeStatus"] = m_childTdsUpgradeStatus;
            key = yyjson_mut_strcpy(doc, "serverUpgradeStatus");
            val = yyjson_mut_strcpy(doc, m_childTdsUpgradeStatus.c_str());
            yyjson_mut_obj_put(conf, key, val);
		}

		if (m_reconnectMode != "" && m_reconnectTime > 0) {
			//json obj = json::object();
			yyjson_mut_val* yyv_obj = yyjson_mut_obj(doc);
			//obj["mode"] = m_reconnectMode;
            key = yyjson_mut_strcpy(doc, "mode");
            val = yyjson_mut_strcpy(doc, m_reconnectMode.c_str());
            yyjson_mut_obj_put(yyv_obj, key, val);

			//obj["time"] = m_reconnectTime;
            key = yyjson_mut_strcpy(doc, "time");
            val = yyjson_mut_int(doc, m_reconnectTime);
            yyjson_mut_obj_put(yyv_obj, key, val);

			//conf["reconnect"] = obj;
            key = yyjson_mut_strcpy(doc, "reconnect");
			yyjson_mut_obj_put(conf, key, yyv_obj);
		}
	}

	//运行时数据 - 与实际硬件设备关联的状态信息，硬件上送的数据
	if (querier.getStatus)
	{
		if (m_charset != "") {
			//conf["charset"] = m_charset;
            key = yyjson_mut_strcpy(doc, "charset");
            val = yyjson_mut_strcpy(doc, m_charset.c_str());
            yyjson_mut_obj_put(conf, key, val);
		}
			

		//conf["online"] = m_bOnline;
        key = yyjson_mut_strcpy(doc, "online");
        val = yyjson_mut_bool(doc, m_bOnline);
        yyjson_mut_obj_put(conf, key, val);


		//conf["connected"] = m_bConnected;
        key = yyjson_mut_strcpy(doc, "connected");
        val = yyjson_mut_bool(doc, m_bConnected);
        yyjson_mut_obj_put(conf, key, val);


		if (pIOSession != nullptr)
		{
			//conf["remoteIP"] = pIOSession->getRemoteAddr();
            key = yyjson_mut_strcpy(doc, "remoteIP");
            val = yyjson_mut_strcpy(doc, pIOSession->getRemoteAddr().c_str());
			yyjson_mut_obj_put(conf, key, val);
		}

		//详细信息
		//conf["chanUpdateTime"] = timeopt::st2str(m_stLastChanDataTime);
        key = yyjson_mut_strcpy(doc, "chanUpdateTime");
        val = yyjson_mut_strcpy(doc, timeopt::st2str(m_stLastChanDataTime).c_str());
        yyjson_mut_obj_put(conf, key, val);

		m_csAlmStatus.lock();
		if (m_jAlarmStatus != nullptr) {
			//conf["alarmUpdateTime"] = timeopt::st2str(m_stLastAlarmStatusTime);
            key = yyjson_mut_strcpy(doc, "alarmUpdateTime");
            val = yyjson_mut_strcpy(doc, timeopt::st2str(m_stLastAlarmStatusTime).c_str());
            yyjson_mut_obj_put(conf, key, val);

			//conf["alarmStatus"] = m_jAlarmStatus;
			string s = m_jAlarmStatus.dump();
            yyjson_doc* tempdoc = yyjson_read(s.c_str(), s.size(), 0);
			yyjson_val* yyv_as = yyjson_doc_get_root(tempdoc);
			yyjson_mut_val* jAlmStatusMut = yyjson_val_mut_copy(doc, yyv_as);
            key = yyjson_mut_strcpy(doc, "alarmStatus");
            yyjson_mut_obj_put(conf, key, jAlmStatusMut);
            yyjson_doc_free(tempdoc);
		}
		m_csAlmStatus.unlock();


		//动态配置 - 动态生成的配置信息 不保存在配置文件中，仅为方便接口调用者使用
		//conf["ioAddr"] = getIOAddrStr();
        key = yyjson_mut_strcpy(doc, "ioAddr");
        val = yyjson_mut_strcpy(doc, getIOAddrStr().c_str());
        yyjson_mut_obj_put(conf, key, val);

		//conf["addrType"] = m_addrType;
        key = yyjson_mut_strcpy(doc, "addrType");
        val = yyjson_mut_strcpy(doc, m_addrType.c_str());
        yyjson_mut_obj_put(conf, key, val);

		//conf["enableAlarm"] = tds->conf->enableGlobalAlarm;
        key = yyjson_mut_strcpy(doc, "enableAlarm");
        val = yyjson_mut_bool(doc, tds->conf->enableGlobalAlarm);
        yyjson_mut_obj_put(conf, key, val);

		//conf["avgRespTime"] = m_avgTransactionTime;
        key = yyjson_mut_strcpy(doc, "avgRespTime");
        val = yyjson_mut_int(doc, m_avgTransactionTime);
        yyjson_mut_obj_put(conf, key, val);

		//conf["unhandleRecvPktCount"] = m_unhandledRecvPktCount;
        key = yyjson_mut_strcpy(doc, "unhandleRecvPktCount");
        val = yyjson_mut_int(doc, m_unhandledRecvPktCount);
        yyjson_mut_obj_put(conf, key, val);
	}

	//备注
	if (querier.getRemark)
	{
		//conf["remark"] = m_remark;
        key = yyjson_mut_strcpy(doc, "remark");
        val = yyjson_mut_strcpy(doc, m_remark.c_str());
        yyjson_mut_obj_put(conf, key, val);
	}

	//升级状态信息数据
	if (querier.getUpgradeInfo) {
		//json jui;
		//jui = m_upgradeInfo.toJson();
		yyjson_mut_val* jui = yyjson_mut_obj(doc);
		m_upgradeInfo.toJson(jui, doc);

		//conf["upgradeInfo"] = jui;
        key = yyjson_mut_strcpy(doc, "upgradeInfo");
        yyjson_mut_obj_put(conf, key, jui);
	}

	if (querier.getDebug) {
		string s = str::bytesToHexStr(m_pab.stream, m_pab.iStreamLen);
		//conf["pktAssembleBuff"] = str::bytesToHexStr(m_pab.stream, m_pab.iStreamLen);
        key = yyjson_mut_strcpy(doc, "pktAssembleBuff");
        val = yyjson_mut_strcpy(doc, s.c_str());
        yyjson_mut_obj_put(conf, key, val);

		//conf["lastFailReq"] = m_lastFailReq;
        key = yyjson_mut_strcpy(doc, "lastFailReq");
        val = yyjson_mut_strcpy(doc, m_lastFailReq.c_str());
        yyjson_mut_obj_put(conf, key, val);

		//conf["lastFailReqTime"] = m_lastFailReqTime;
        key = yyjson_mut_strcpy(doc, "lastFailReqTime");
        val = yyjson_mut_strcpy(doc, m_lastFailReqTime.c_str());
        yyjson_mut_obj_put(conf, key, val);
	}

	if (querier.getChild)
	{
		if (m_vecChildDev.size() > 0)
		{
			yyjson_mut_val* children = yyjson_mut_arr(doc);
			for (auto& i : m_vecChildDev)
			{
                yyjson_mut_val* j = yyjson_mut_obj(doc);
				i->toJson(j, doc, querier);
                yyjson_mut_arr_append(children, j);
			}

            key = yyjson_mut_strcpy(doc, "children");
            yyjson_mut_obj_put(conf, key, children);
		}
	}

	if (querier.getChan && m_channels.size() > 0)
	{
		//json channels = json::array();
		yyjson_mut_val* channels = yyjson_mut_arr(doc);

		for (auto& i : m_channels)
		{
			//空闲通道作为状态数据，必须指定获取状态才返回
			if (!querier.getStatus && i->m_dispositionMode == DEV_DISPOSITION_MODE::spare) {
				continue;
			}
			yyjson_mut_val* j = yyjson_mut_obj(doc);
            i->toJson(j, doc, querier);
            yyjson_mut_arr_append(channels, j);
		}
		//conf["channels"] = channels;
        key = yyjson_mut_strcpy(doc, "channels");
        yyjson_mut_obj_put(conf, key, channels);
        return true;
	}

	
	return true;
}

//bool ioDev::toJson(json& conf, DEV_QUERIER querier)
//{
//	DEV_CONF_LOCK_SHARED(this);
//
//	//配置数据 - 保存在配置文件中
//	if (querier.getConf) {
//		conf["addrMode"] = m_addrType;
//		conf["addr"] = m_jDevAddr;
//		conf["ioMode"] = m_ioMode;
//		conf["type"] = m_devType;
//		conf["typeLabel"] = getDevTypeLabel(m_devType);
//		if (m_devSubType != "") {
//			conf["subType"] = m_devSubType;
//			conf["subTypeLabel"] = getDevSubTypeLabel(m_devSubType);
//		}
//
//		conf["level"] = m_level;
//		conf["manageStatus"] = m_dispositionMode;
//		conf["acqInterval"] = m_fAcqInterval;
//		conf["enableAcq"] = m_bEnableAcq;
//		if (m_strTagBind != "")
//			conf["tagBind"] = m_strTagBind;
//		if (m_strChanTemplate != "")
//			conf["chanTemplate"] = m_strChanTemplate;
//		conf["nodeID"] = m_confNodeId;
//		if (m_acqMode != "")
//			conf["acqMode"] = m_acqMode;
//		if (m_chanGroup != "")
//			conf["chanGroup"] = m_chanGroup;
//		if (m_chanTimeSync)
//			conf["chanTimeSync"] = true;
//
//		conf["acqAlarm"] = m_acqAlarm;
//		if (m_strUser != "") {
//			conf["user"] = m_strUser;
//		};
//		if (m_strPwd != "") {
//			conf["pwd"] = m_strPwd;
//		}
//		if (m_translatorProto != "") {
//			conf["translatorProto"] = m_translatorProto;
//		}
//		if (m_standAloneIOType != "") {
//			conf["standAloneIOType"] = m_standAloneIOType;
//		}
//		if (m_cycleTaskScript != "") {
//			conf["cycleTaskScript"] = m_cycleTaskScript;
//		}
//		if (m_outputScript != "") {
//			conf["outputScript"] = m_outputScript;
//		}
//		if (m_onRecvScript != "") {
//			conf["onRecvScript"] = m_onRecvScript;
//		}
//		if (m_tdspScript != "") {
//			conf["tdspScript"] = m_tdspScript;
//		}
//
//		if (m_bEnableOfflineTimeout) {
//			conf["enableOfflineTimeout"] = m_bEnableOfflineTimeout;
//			conf["offlineTimeout"] = m_offlineTimeout;
//		}
//
//		if (m_bEnableHttpHeartbeat) {
//			conf["enableHttpHeartbeat"] = m_bEnableHttpHeartbeat;
//			conf["httpHeartbeatUrl"] = m_httpHeartbeatUrl;
//		}
//
//		if (m_bEnablePingOnlineCheck) {
//			conf["enablePingOnlineCheck"] = m_bEnablePingOnlineCheck;
//		}
//
//		if (m_devType == DEV_TYPE::DEV::tdsp_device && m_devSubType == TDSP_SUB_TYPE::childTds) {
//			conf["tdsVersion"] = m_tdsVersion;
//			conf["serverStatus"]["cpu"] = m_tdsSrvStatus.cpu;
//			conf["serverStatus"]["mem"] = m_tdsSrvStatus.mem;
//			conf["serverStatus"]["handle"] = m_tdsSrvStatus.handle;
//			conf["statusUpdateTime"] = m_statusUpdateTime;
//			conf["serverUpgradeStatus"] = m_childTdsUpgradeStatus;
//		}
//
//		if (m_reconnectMode != "" && m_reconnectTime > 0) {
//			json obj = json::object();
//
//			obj["mode"] = m_reconnectMode;
//			obj["time"] = m_reconnectTime;
//
//			conf["reconnect"] = obj;
//		}
//	}
//
//	//运行时数据 - 与实际硬件设备关联的状态信息，硬件上送的数据
//	if (querier.getStatus)
//	{
//		if (m_charset != "")
//			conf["charset"] = m_charset;
//
//		conf["online"] = m_bOnline;
//		conf["connected"] = m_bConnected;
//		if (pIOSession != nullptr)
//		{
//			conf["remoteIP"] = pIOSession->getRemoteAddr();
//		}
//
//		//详细信息
//		conf["chanUpdateTime"] = timeopt::st2str(m_stLastChanDataTime);
//
//		m_csAlmStatus.lock();
//		if (m_jAlarmStatus != nullptr) {
//			conf["alarmUpdateTime"] = timeopt::st2str(m_stLastAlarmStatusTime);
//			conf["alarmStatus"] = m_jAlarmStatus;
//		}
//		m_csAlmStatus.unlock();
//
//
//		//动态配置 - 动态生成的配置信息 不保存在配置文件中，仅为方便接口调用者使用
//		conf["ioAddr"] = getIOAddrStr();
//		conf["addrType"] = m_addrType;
//		conf["enableAlarm"] = tds->conf->enableGlobalAlarm;
//
//		conf["avgRespTime"] = m_avgTransactionTime;
//		conf["unhandleRecvPktCount"] = m_unhandledRecvPktCount;
//	}
//
//	//备注
//	if (querier.getRemark)
//	{
//		conf["remark"] = m_remark;
//	}
//
//	//升级状态信息数据
//	if (querier.getUpgradeInfo) {
//		json jui;
//		jui = m_upgradeInfo.toJson();
//		conf["upgradeInfo"] = jui;
//	}
//
//	if (querier.getDebug) {
//		conf["pktAssembleBuff"] = str::bytesToHexStr(m_pab.stream, m_pab.iStreamLen);
//		conf["lastFailReq"] = m_lastFailReq;
//		conf["lastFailReqTime"] = m_lastFailReqTime;
//	}
//
//	if (querier.getChild)
//	{
//		if (m_vecChildDev.size() > 0)
//		{
//			json children = json::array();
//			for (auto& i : m_vecChildDev)
//			{
//				json j;
//				i->toJson(j, querier);
//				children.push_back(j);
//			}
//			conf["children"] = children;
//		}
//	}
//
//	if (querier.getChan && m_channels.size() > 0)
//	{
//		json channels = json::array();
//		for (auto& i : m_channels)
//		{
//			//空闲通道作为状态数据，必须指定获取状态才返回
//			if (!querier.getStatus && i->m_dispositionMode == DEV_DISPOSITION_MODE::spare) {
//				continue;
//			}
//			json j;
//			i->toJson(j, querier);
//			channels.push_back(j);
//		}
//		conf["channels"] = channels;
//	}
//
//	
//	return true;
//}

bool ioDev::getStatus(json& status, string opt)
{
	return false;
}

bool ioDev::getChanVal(json& valList)
{
	DEV_CONF_LOCK_SHARED(this);
	for (int i = 0; i < m_channels.size(); i++)
	{
		ioDev* p = m_channels[i];
		p->getChanVal(valList);
	}

	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		p->getChanVal(valList);
	}
	
	return true;
}


bool ioDev::getChanStatus(json& statusList)
{
	DEV_CONF_LOCK_SHARED(this);
	for (int i = 0; i < m_channels.size(); i++)
	{
		ioDev* p = m_channels[i];
		p->getChanStatus(statusList);
	}

	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		p->getChanStatus(statusList);
	}
	
	return true;
}

bool ioDev::loadConf(json& conf)
{
	DEV_CONF_LOCK_UNIQUE(this);


	auto kv = conf.find("addrMode");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_addrType = item.get<string>();
		}
	}

	kv = conf.find("type");
    if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_devType = item.get<string>();
		}
	}

	kv = conf.find("addrType");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_addrType = item.get<string>();
		}
	}

	kv = conf.find("remark");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_remark = item.get<string>();
		}
	}
	
	kv = conf.find("subType");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_devSubType = item.get<string>();
		}
	}

	kv = conf.find("ioMode");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_ioMode = item.get<string>();
		}
	}

	kv = conf.find("viaAdaptor");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_boolean()) {
			bool b = item.get<bool>();
			if (b) {
				m_ioMode = "adaptor";
			}
		}
	}

	kv = conf.find("translatorProto");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_translatorProto = item.get<string>();
		}
	}

	kv = conf.find("standAloneIOType");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_standAloneIOType = item.get<string>();
		}
	}

	kv = conf.find("cycleTaskScript");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_cycleTaskScript = item.get<string>();
		}
	}

	kv = conf.find("outputScript");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_outputScript = item.get<string>();
		}
	}

	kv = conf.find("onRecvScript");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_onRecvScript = item.get<string>();
		}
	}

	kv = conf.find("tdspScript");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_tdspScript = item.get<string>();
		}
	}

	kv = conf.find("addr");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (!item.is_null()) {
			m_jDevAddr = item;
			m_devAddr = getDevAddrStr();
			//addrMode以后保存到io.json。此处兼容未保存的
			//if (m_jDevAddr.is_object() && m_addrType == "")
			//{
			//	if (m_jDevAddr["id"] != nullptr) {
			//		m_addrType = DEV_ADDR_MODE::deviceID;
			//	}
			//	else if (m_jDevAddr["port"].is_number_integer() && m_jDevAddr["port"].get<int>() != 0)
			//		m_addrType = DEV_ADDR_MODE::tcpServer;
			//	else
			//		m_addrType = DEV_ADDR_MODE::tcpClient;
			//}

			//TCP服务器模式下，如果有修改IP或者端口，则重新启动连接
			if (m_addrType == DEV_ADDR_MODE::tcpServer && m_tcpClt)
			{
				string ip;
				if (m_jDevAddr["ip"].is_string())
				{
					ip = m_jDevAddr["ip"].get<string>();

					int port;
					if (m_jDevAddr["port"].is_number_integer())
					{
						port = m_jDevAddr["port"].get<int>();

						if (m_tcpClt->m_remoteIP != ip || m_tcpClt->m_remotePort != port)
						{
							m_tcpClt->stop();
							m_tcpClt->run(this, ip, port); //逐步把 ioSrv 中的 onRecvData_tcpClt重构掉，放在ioDev对象内部处理 tcpClient接收数据更合理
							LOG("[IO设备]修改地址重新启动设备,地址模式:%s,设备类型:%s,远端地址:%s,本地启动tcpClient,本地IP:%s", m_addrType.c_str(), m_devType.c_str(), getDevAddrStr().c_str(), ioSrv.m_ioSrvIP.c_str());
						}
					}
				}
			}
			//UDP服务器模式下，如果有修改端口，则重新启动连接
			else if (m_addrType == DEV_ADDR_MODE::udpServer && m_udpClt)
			{
				int localPort = 0;
				if (m_jDevAddr["localPort"].is_number_integer())
				{
					localPort = m_jDevAddr["localPort"].get<int>();

					if (localPort != m_udpClt->m_port)
					{
						m_udpClt->stop();
						m_udpClt->run(this, localPort, ioSrv.m_ioSrvIPAsClient);
						LOG("[IO设备]修改地址重新启动设备,地址模式:%s,设备类型:%s,远端地址:%s,本地启动udpClient,本地IP:%s,本地端口:%d", m_addrType.c_str(), m_devType.c_str(), getDevAddrStr().c_str(), ioSrv.m_ioSrvIP.c_str(), m_udpClt->m_port);
					}
				}
			}
		}
	}

	kv = conf.find("user");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_strUser = item.get<string>();
		}
	}

	kv = conf.find("pwd");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_strPwd = item.get<string>();
		}
	}

	kv = conf.find("subTopics");
	if (kv != conf.end()) {
		json& item = kv.value();
		if(item.is_string())
		{
			m_subTopics = item.get<string>();
		}
	}

	kv = conf.find("acqInterval");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_number()) {
			m_fAcqInterval = item.get<float>();
		}
	}

	kv = conf.find("enableAcq");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_boolean()) {
			m_bEnableAcq = item.get<bool>();
		}
	}

	kv = conf.find("chanTimeSync");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_boolean()) {
			m_chanTimeSync = item.get<bool>();
		}
	}


	kv = conf.find("manageStatus");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_dispositionMode = item.get<string>();
		}
	}


	kv = conf.find("chanTemplate");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_strChanTemplate = item.get<string>();
		}
	}

	kv = conf.find("acqMode");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_acqMode = item.get<string>();
		}
	}

	kv = conf.find("chanGroup");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_chanGroup = item.get<string>();
		}
	}


	kv = conf.find("acqAlarm");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_boolean()) {
			m_acqAlarm = item.get<bool>();
		}
	}

	kv = conf.find("enableOfflineTimeout");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_boolean()) {
			m_bEnableOfflineTimeout = item.get<bool>();
		}
	}

	kv = conf.find("offlineTimeout");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_number_integer()) {
			m_offlineTimeout = item.get<int>();
		}
	}

	kv = conf.find("enableHttpHeartbeat");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_boolean()) {
			m_bEnableHttpHeartbeat = item.get<bool>();
		}
	}

	kv = conf.find("httpHeartbeatUrl");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_httpHeartbeatUrl = item.get<string>();
		}
	}

	kv = conf.find("enablePingOnlineCheck");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_boolean()) {
			m_bEnablePingOnlineCheck = item.get<bool>();
		}
	}

	kv = conf.find("nodeID");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_confNodeId = item.get<string>();
		}
	}
	if (m_confNodeId == "") //该操作主要用于升级没有nodeId的配置
		m_confNodeId = generateDevId();

	kv = conf.find("tagBind");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_strTagBind = item.get<string>();
			//启用设备，才更新绑定的mo中的 关联io地址信息。 备用的不更新。
			//否则备用的绑定地址和启用的相同时，可能会错误的使用备用设备的信息
			//配置加载完成后，统一更新
			//if (m_dispositionMode == DEV_DISPOSITION_MODE::managed && m_strTagBind != "")
			//{
			//	json tagBinding = json::array();
			//	json binding;
			//	binding["ioAddr"] = getIOAddrStr();
			//	binding["tag"] = m_strTagBind;
			//	tagBinding.push_back(binding);
			//	tds->callAsyn("updateTagBinding", tagBinding.dump());
			//}
		}
	}


	if (conf.contains("children"))
	{
		deleteChildren();
		json childDev = conf["children"];

		for (auto i : childDev)
		{
			ioDev* pChild = nullptr;
			pChild = createIODev(i["type"].get<string>());

			if (pChild) {
				pChild->loadConf(i);
				pChild->m_pParent = this;
				m_vecChildDev.push_back(pChild);
			}
		}
	}


	if (conf.contains("channels"))
	{
		deleteAllChannels();
		
		json childDev = conf["channels"];

		for (auto i : childDev)
		{
			ioChannel* pdc = nullptr;
			pdc = new ioChannel();
			pdc->m_jDevAddr = i["addr"];
			if (i["addr"].is_string())
				pdc->m_devAddr = i["addr"].get<string>();
			pdc->loadConf(i);
			//批量映射配置.主要用于mqtt的场景，当mqtt的路径结构和MOTree的树结构一致时
			if (pdc->m_devAddr != "" && isBatchLink(pdc->m_devAddr)) //datachannel instance of the batch data link will be created dynamicly when the channel data is received
			{
				m_mapBatchDataLink[pdc->m_devAddr] = i["tagBind"];
			}
			addChannel(pdc);

			if (m_strTagBind != "") { 
				pdc->m_strTagBind = str::trimPrefix(pdc->m_strTagBind, m_strTagBind);
				pdc->m_strTagBind = str::trimPrefix(pdc->m_strTagBind, ".");
			}
		}
	}

	kv = conf.find("reconnect");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_object()) {
			if (item["mode"].is_string()) {
				m_reconnectMode = item["mode"].get<string>();
			}

			if (item["time"].is_number_integer()) {
				m_reconnectTime = item["time"].get<int>();
			}
		}
	}
		
	
	return true;
}

void ioDev::addChannel(ioChannel* pdc)
{
	pdc->m_pParent = this;
	m_channels.push_back(pdc);
	m_mapDataChannel[pdc->getDevAddrStr()] = pdc;
}

string ioDev::getConnInfo()
{
	return getDevAddrStr();
}

bool ioDev::connect()
{
	return false;
}

bool ioDev::disconnect()
{
	return false;
}

string ioDev::getDesc()
{
	
	return "";
}

void ioDev::triggerCycleAcq()
{
	timeopt::setAsTimeOrg(m_stLastAcqTime);
}

void ioDev::doPingHeartbeat(string ip)
{
	string strcmd;

	//这段代码的含义是执行ping命令，其中：
	// -n 1 表示只发送一个数据包。
	// -w 300 表示设置超时时间为300毫秒。
	// > nul 表示将命令的输出定向到空设备
#ifdef _WIN32
	strcmd = "ping -n 1 -w 300 > nul "+ ip;
#else
	strcmd = "ping -c 2 -w 500 > /dev/null " + ip;
#endif
	
	//int isatus = system(strcmd.c_str());  该函数不能并发

	int isatus = 1;
#ifdef _WIN32
	FILE* fp = _popen(strcmd.c_str(), "r"); //此函数可以并发
#else
	FILE* fp = popen(strcmd.c_str(), "r"); //此函数可以并发
#endif
	if (fp == NULL)
		isatus = -1;
	else
#ifdef _WIN32
		isatus = _pclose(fp);
#else
		isatus = pclose(fp);
#endif
	if (isatus == 0)
	{
		setOnline(true);
	}
	else
	{
		setOffline(true,"ping失败");
	}
}

bool ioDev::handleDevRpcCall(json& jReq, RPC_RESP& rpcResp)
{
	string method = jReq["method"].get<string>();
	json jParams = jReq["params"];
	json jId = jReq["id"];

	bool sync = jId != nullptr ? true : false;
	TIME tStart; tStart.setNow();
	LOG("[TDSP路由转发]客户端->设备,ioAddr=%s,method=%s,rpcId=%s,sync=%d,req=%s",
		getIOAddrStr().c_str(), 
		method.c_str(),
		jId.dump().c_str(),
		sync?1:0,
		jReq.dump().c_str());
	ioDev* pIoDev = this;
	if (pIoDev->pIOSession == nullptr && pIoDev->m_addrType != DEV_ADDR_MODE::udpServer)
	{
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devOffline, "设备离线");
		LOG("[warn]" + rpcResp.error);
		return true;
	}
	if (pIoDev->m_devType != DEV_TYPE::DEV::tdsp_device)
	{
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devTypeError, "IO设备类型错误");
		LOG("[warn]" + rpcResp.error);
		return true;
	}

	jReq["clientId"] = "tds";
	jReq.erase("user");
	jReq.erase("token");

	json jSessionParams; 
	if (jReq.contains("tag")) {
		jSessionParams["tag"] = jReq["tag"];
	}


	json jRlt, jErr;
	//发起同步请求，此处阻塞
	bool callRet = false;
	pIoDev->call(method, jParams, jSessionParams, jRlt, jErr,sync);
	string respInfo;
	if (jRlt != nullptr) {
		rpcResp.result = jRlt.dump();
		respInfo = "成功,result=" + rpcResp.result;
	}
	else if (jErr != nullptr)
	{
		rpcResp.error = jErr.dump();
		respInfo = "失败,error=" + rpcResp.error;
	}
	else
	{
		assert(false);
	}
	rpcResp.timeCost = TIME::calcTimePassMilliSecond(tStart);
	LOG("[TDSP路由转发]设备->客户端,ioAddr=%s,method=%s,rpcId=%s,耗时=%d,%s\r\n", 
		getIOAddrStr().c_str(),
		method.c_str(), 
		jId.dump().c_str(),
		rpcResp.timeCost,
		respInfo.c_str());
	return true;
}

void DEV_QUERIER::parseQueryOpt(json& opt)
{
	DEV_QUERIER& q = *this;

	if (opt.contains("getStatus")) {
		q.getStatus = opt["getStatus"].get<bool>();
	};
	if (opt.contains("getChan")) {
		q.getChan = opt["getChan"].get<bool>();
	}
	if (opt.contains("getChild")) {
		q.getChild = opt["getChild"].get<bool>();
	}
	if (opt.contains("getConf")) {
		q.getConf = opt["getConf"].get<bool>();
	}
	if (opt.contains("getDetail")) {
		q.getDetail = opt["getDetail"].get<bool>();
	}
	if (opt.contains("getRemark")) {
		q.getRemark = opt["getRemark"].get<bool>();
	}
	if (opt.contains("getUpgradeInfo")) {
		q.getUpgradeInfo = opt["getUpgradeInfo"].get<bool>();
	}
	if (opt.contains("getPAB")) {
		q.getPAB = opt["getPAB"].get<bool>();
	}
	if (opt.contains("getDebug")) {
		q.getDebug = opt["getDebug"].get<bool>();
	}
	if (opt["type"].is_string()) {
		q.type.push_back(opt["type"].get<string>());
	}
	if (opt["standAloneIO"].is_string()) {
		q.standAloneIO = opt["standAloneIO"].get<string>();
	}
	if (opt["type"].is_array()) {
		json jTypes = opt["type"];
		for (int i = 0; i < jTypes.size(); i++) {
			json j = jTypes[i];
			if (j.is_string()) {
				q.type.push_back(j.get<string>());
			}
		}
	}
	else if (opt["type"].is_string()) {
		string type = opt["type"].get<string>();
		q.type.push_back(type);
	}

	if (opt["subType"].is_string()) {
		q.subType.push_back(opt["subType"].get<string>());
	}
	else if (opt["subType"].is_array()) {
		json jSubTypes = opt["subType"];
		for (int i = 0; i < jSubTypes.size(); i++) {
			json j = jSubTypes[i];
			if (j.is_string()) {
				q.subType.push_back(j.get<string>());
			}
		}
	}

	if (opt.contains("rootTag"))
		rootTag = opt["rootTag"].get<string>();
	if (opt.contains("interface"))
		interfaceType = opt["interface"].get<string>();
	if (opt.contains("getStatis"))
		getStatis = opt["getStatis"].get<bool>();
	if (opt.contains("tag"))
		tagBind = opt["tag"].get<string>();
	if (opt.contains("keywords"))
		strKeywords = opt["keywords"].get<string>();

	//分页参数
	if (opt.contains("pageNo") && opt.contains("pageSize")) {
		paging = true;
		pageNo = opt["pageNo"].get<int>();
		pageNo -= 1;
		if (pageNo < 0)
			pageNo = 0;
		pageSize = opt["pageSize"].get<int>();
	}
}

ioDev* ioDev::getIODevByNodeID(string nodeID)
{
	DEV_CONF_LOCK_SHARED(this);
	ioDev* pD = nullptr;

	if (m_confNodeId == nodeID)
	{
		pD = this;
	}
	else {
		for (int i = 0; i < m_vecChildDev.size(); i++)
		{
			ioDev* p = m_vecChildDev[i];
			if (p->m_confNodeId == nodeID)
			{
				pD = p;
				break;
			}

			ioDev* ptmp = p->getIODevByNodeID(nodeID);
			if (ptmp) {
				pD = ptmp;
				break;
			}
		}
	}

	
	return pD;
}

ioChannel* ioDev::getChanByNodeID(string nodeID)
{
	DEV_CONF_LOCK_SHARED(this);
	ioDev* pD = nullptr;

	for (int i = 0; i < m_channels.size(); i++) {
		ioChannel* p = m_channels[i];
		if (p->m_confNodeId == nodeID)
		{
			return p;
		}
	}

	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		ioChannel* ptmp = p->getChanByNodeID(nodeID);
		if(ptmp){
			return ptmp;
		}
	}

	return nullptr;
}

bool ioDev::deleteIODevByNodeID(string nodeID)
{
	bool ret = false;
	DEV_CONF_LOCK_UNIQUE(this);
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		if (p->m_confNodeId == nodeID)
		{
			m_vecChildDev.erase(m_vecChildDev.begin() + i);

			//热组态关键机制，stop必须解除所有的设备占用
			p->stop();
			delete p;
			ret = true;
			break;
		}

		if (p->deleteIODevByNodeID(nodeID))
		{
			ret = true;
			break;
		}
			
	}
	
	return ret;
}

//ioDev* ioDev::getIODev(string ioAddr,bool bChn,bool ignorePort)
//{
//	vector<string> vecNodeName;
//	str::split(vecNodeName, ioAddr, "/");
//
//	ioDev* treeNode = NULL;
//	vector<ioDev*>* vecChildNode = &m_vecChildDev;
//	bool findDev = false;
//	//根据节点的名字，在树型结构上一层层往下找
//	for (int i = 0; i < vecNodeName.size(); i++)
//	{
//		string nodeName = vecNodeName[i];
//
//		bool findNode = false;
//		for (auto& it : *vecChildNode)
//		{
//			string nodeNameTmp = it->getDevAddrStr(ignorePort);
//			if (bChn) {
//				str::hanZi2Pinyin(nodeNameTmp, nodeNameTmp);
//				str::hanZi2Pinyin(nodeName, nodeName);
//			}
//
//			if (nodeNameTmp == nodeName)
//			{
//				findNode = true;
//				treeNode = it;
//				if (i == vecNodeName.size() - 1)//找到了最后一个节点
//				{
//					findDev = true;
//				}
//				break;
//			}
//		}
//
//		if (findNode)
//		{
//			vecChildNode = &treeNode->m_vecChildDev;
//		}
//		else
//		{
//			break;
//		}
//	}
//
//	if (findDev)
//		return treeNode;
//
//	return nullptr;
//}

//tds是否和设备建立tcp直连
bool ioDev::checkOnlineByTcpConn()
{
	//是否直连
	if (m_ioMode == "none" || m_ioMode == "translator") {
		//内网环境
		if (m_addrType == DEV_ADDR_MODE::tcpClient || m_addrType == DEV_ADDR_MODE::tcpServer) {
			if (pIOSession != nullptr) {
				return true;
			}
			else {
				return false;
			}
		}
		//id模式目前都是tcp
		else if (m_addrType == DEV_ADDR_MODE::deviceID) {
			if (pIOSession != nullptr) {
				return true;
			}
			else {
				return false;
			}
		}
		//udp,http模式
		else {
			return true;
		}
	}
	//通过适配器或者独立io
	else {
		if (m_ioMode == "standAloneIO")
		{
			//独立Io模式,判断是否有对应的ioType上线
			if (m_standAloneIOType!="" && ioSrv.m_standAloneIO.find(m_standAloneIOType) != ioSrv.m_standAloneIO.end())
				return true;
			else
				return false;
		}
		else
		{
			return true;
		}
	}
}

bool ioDev::isViaAdaptor() {
	if (m_bViaAdaptor) {
		return true;
	}
	//如果父设备是网关类设备
	else if (m_pParent != nullptr && m_pParent->m_pParent != nullptr) {
		if (m_pParent->m_bViaAdaptor) {
			return true;
		}
	}
	return false;
}

ioDev* ioDev::getIODev(string ioAddr, bool bChn, bool ignorePort, string addrType) {
	if (bChn) {
		str::hanZi2Pinyin(ioAddr, ioAddr);
	}

	for (auto& it : m_vecChildDev) {
		if (!addrType.empty()) {
			if (addrType != it->m_addrType) {
				continue;
			}
		}

		string tmp = it->getIOAddrStr(ignorePort);
		if (bChn) {
			str::hanZi2Pinyin(tmp, tmp);
		}

		if (tmp == ioAddr){
			return it;
		}

		ioDev* p = it->getIODev(ioAddr,bChn,ignorePort);
		if (p) {
			return p;
		}
	}

	if (bChn) {
		for (auto& it : m_channels) {
			string tmp = it->getIOAddrStr(ignorePort);

			str::hanZi2Pinyin(tmp, tmp);
			if (tmp == ioAddr) {
				return it;
			}
		}
	}

	return nullptr;
}

vector<ioDev*> ioDev::getChildren(string devType)
{
	vector<ioDev*> ary;
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		if (p->m_devType == devType)
		{
			ary.push_back(p);
		}

		vector<ioDev*> aryChild = p->getChildren(devType);
		ary.insert(ary.end(), aryChild.begin(), aryChild.end());
	}
	return ary;
}


json ioDev::getAddr()
{
	json j;
	j = m_devAddr;
	return j;
}

string ioDev::getIOAddrStr(bool ignorePort)
{
	string devAddr = getDevAddrStr(ignorePort);
	ioDev* pParent = m_pParent;
	while (pParent && pParent->m_devType != "tds")
	{
		devAddr = pParent->getDevAddrStr(ignorePort) + "/" + devAddr;
		pParent = pParent->m_pParent;
	}
		
	return devAddr;
}

string ioDev::getIP() {
	if (m_jDevAddr.is_object())
	{
		if (m_jDevAddr.contains("ip")) {
			json jip = m_jDevAddr["ip"];
			if (jip.is_string()) {
				string ip = jip.get<string>();
				return ip;
			}
		}
	}
	return "";
}

int ioDev::getPort() {
	if (m_jDevAddr.is_object())
	{
		if (m_jDevAddr["port"].is_number_integer()) {
			json j = m_jDevAddr["port"];
			if (j.is_number_integer()) {
				int p = j.get<int>();
				return p;
			}
		}
	}
	return 0;
}

int ioDev::getLocalPort() {
	if (m_jDevAddr.is_object())
	{
		if (m_jDevAddr["localPort"].is_number_integer()) {
			json j = m_jDevAddr["localPort"];
			if (j.is_number_integer()) {
				int p = j.get<int>();
				return p;
			}
		}
	}
	return 0;
}

bool ioDev::isMulticast() {
	if (m_jDevAddr.is_object())
	{
		if (m_jDevAddr["multicast"].is_boolean()) {
			json j = m_jDevAddr["multicast"];
			return j.get<bool>();
		}
	}
	return false;
}

string ioDev::getMulticastIP() {
	if (m_jDevAddr.is_object())
	{
		if (m_jDevAddr["multicastIP"].is_string()) {
			json j = m_jDevAddr["multicastIP"];
			return j.get<string>();
		}
	}
	return "";
}

string ioDev::getDevAddrStr(bool ignorePort)
{
	string devAddr;
	if (m_jDevAddr.is_object())
	{
		//通道模式下m_addrMode无效
		auto kvRegType = m_jDevAddr.find("regType");
		auto kvRegOffset = m_jDevAddr.find("regOffset");
		if (kvRegType != m_jDevAddr.end() && kvRegOffset != m_jDevAddr.end() &&
			kvRegType.value().is_string() && kvRegOffset.value().is_number())
		{
			string regType = m_jDevAddr["regType"].get<string>();
			devAddr = regType + "/" + str::fromInt(m_jDevAddr["regOffset"].get<int>());
			//增加了regBitIndex
			auto kvRegBitIndex = m_jDevAddr.find("regBitIndex");
			if (kvRegBitIndex != m_jDevAddr.end() && kvRegBitIndex.value().is_number())
				devAddr += ("/" + str::fromInt(m_jDevAddr["regBitIndex"].get<int>()));
		}
		else if (m_addrType == DEV_ADDR_MODE::deviceID)
		{
			if (m_jDevAddr["id"].is_string()) {
				devAddr = m_jDevAddr["id"].get<string>();
				if (ignorePort) {
					devAddr = ioDev::removePortFromIoAddr(devAddr);
				}
			}
		}
		else if (m_addrType == DEV_ADDR_MODE::tcpClient)
		{
			string ip;
			int port = 0;
			if (m_jDevAddr["ip"].is_string())
				ip = m_jDevAddr["ip"].get<string>();
			if (m_jDevAddr["port"].is_number_integer())
			{
				port = m_jDevAddr["port"].get<int>();
			}

			devAddr = ip;
			if (!ignorePort)
				devAddr += ":" + str::fromInt(port);
		}
		else if (m_addrType == DEV_ADDR_MODE::tcpServer)
		{
			string ip;
			int port = 0;
			if (m_jDevAddr["ip"].is_string())
				ip = m_jDevAddr["ip"].get<string>();
			if (m_jDevAddr["port"].is_number_integer())
			{
				port = m_jDevAddr["port"].get<int>();
			}

			devAddr = ip;
			if (!ignorePort)
				devAddr += ":" + str::fromInt(port);
		}
		else if (m_addrType == DEV_ADDR_MODE::udpServer)
		{
			string ip;
			int port = 0;
			if (m_jDevAddr["ip"].is_string())
				ip = m_jDevAddr["ip"].get<string>();
			if (m_jDevAddr["port"].is_number_integer())
			{
				port = m_jDevAddr["port"].get<int>();
			}

			devAddr = "UDP-" + ip;
			if (!ignorePort)
				devAddr += ":" + str::fromInt(port);
		}
		else if (m_addrType == DEV_ADDR_MODE::udpClient)
		{
			string ip;
			int port = 0;
			if (m_jDevAddr["ip"].is_string())
				ip = m_jDevAddr["ip"].get<string>();
			if (m_jDevAddr["localPort"].is_number_integer())
			{
				port = m_jDevAddr["localPort"].get<int>();
			}

			devAddr = "UDP-" + ip;
			if (!ignorePort)
				devAddr += ":" + str::fromInt(port);
		}
		else if (m_addrType == DEV_ADDR_MODE::httpClient)
		{
			string ip;
			int port = 0;
			if (m_jDevAddr["ip"].is_string())
				ip = m_jDevAddr["ip"].get<string>();
			if (m_jDevAddr["port"].is_number_integer())
			{
				port = m_jDevAddr["port"].get<int>();
			}

			devAddr = "httpClient-" + ip;
			if (!ignorePort)
				devAddr += ":" + str::fromInt(port);
		}
		else if (m_addrType == DEV_ADDR_MODE::httpServer)
		{
			string ip;
			int port = 0;
			if (m_jDevAddr["ip"].is_string())
				ip = m_jDevAddr["ip"].get<string>();
			if (m_jDevAddr["port"].is_number_integer())
			{
				port = m_jDevAddr["port"].get<int>();
			}

			devAddr = "httpServer-" + ip;
			if (!ignorePort)
				devAddr += ":" + str::fromInt(port);
		}
	}
	else if(m_jDevAddr.is_string()){
		devAddr = m_jDevAddr.get<string>();
	}
	else
	{
		devAddr = "";
	}
	return devAddr;
}

ioDev* ioDev::getIODevByIP(string ip) {
	ioDev* pD = nullptr;
	DEV_CONF_LOCK_SHARED(this);
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		if (p->getIP() == ip) {
			pD = p;
			break;
		}
	}
	
	return pD;
}

ioDev* ioDev::getIODevByIPPort(string ipport) {
	ioDev* pD = nullptr;
	DEV_CONF_LOCK_SHARED(this);
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		string ip = p->getIP();
		int port = p->getPort();
		string tmp = str::format("%s:%d", ip.c_str(), port);
		if (tmp == ipport) {
			pD = p;
			break;
		}
	}
	
	return pD;
}

ioDev* ioDev::getIODevById(string id)
{
	ioDev* pD = nullptr;
	DEV_CONF_LOCK_SHARED(this);
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		json jDevId = p->m_jDevAddr["id"];
		if (jDevId.is_string()) {
			string devId = jDevId.get<string>();
			if (devId == id) {
				pD = p;
				break;
			}
		}
		pD = p->getIODevById(id);
		if (pD) {
			break;
		}
	}
	
	return pD;
}

ioDev* ioDev::getIODevByTag(string tag)
{
	ioDev* pD = nullptr;
	DEV_CONF_LOCK_SHARED(this);
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		//备用的设备允许和相同的位号绑定，但是实际没有效果
		//因为在实际工程当中，可能会删除一台在线的设备变成备用。绑定关系没改
		//然后将另外一台设备和相同的位号绑定。此处不让那个备用的绑定影响启用的设备。
		if (p->m_dispositionMode == DEV_DISPOSITION_MODE::spare)
			continue;
		if (p->m_strTagBind == tag)
		{
			pD = p;
			break;
		}

		ioDev* pTmp = p->getIODevByTag(tag);
		if (pTmp)
		{
			pD = pTmp;
			break;
		}
	}
	
	return pD;
}

bool ioDev::CommLock(int dwTimeoutMS)
{
	if (dwTimeoutMS)
	{
		chrono::milliseconds timeout(dwTimeoutMS);
		return m_csCommLock.try_lock_for(timeout);
	}
	else
	{
		m_csCommLock.lock();
		return true;
	}
}

void ioDev::CommUnlock()
{
	m_csCommLock.unlock();
}

bool ioDev::SendPkt(DEV_PKT& pkt)
{
	return sendData(pkt.data, pkt.len);
}

bool ioDev::sendData(unsigned char* pData, size_t iLen) {
	if (m_pParent != nullptr && m_pParent != &ioSrv) {
		m_pParent->sendData(pData, iLen);
	}
	else if (m_udpClt != nullptr && m_ioMode == "none") {
		string ip = "";
		int port = 0;

		if (m_jDevAddr["ip"].is_string()) {
			ip = m_jDevAddr["ip"].get<string>();
		}

		if (m_jDevAddr["port"].is_number_integer()) {
			port = m_jDevAddr["port"].get<int>();
		}

		int iSent = m_udpClt->SendData(pData, iLen, ip, port);
		if (m_bEnableIoLog) {
			string remoteAddr = "UDP-" + ip + ":" + str::fromInt(port);
			string localAddr  = "UDP-" + m_udpClt->m_bindIP + ":" + str::fromInt(m_udpClt->m_port);
			IOLogSend((unsigned char*)pData, iLen, iSent > 0,remoteAddr,localAddr,getIOAddrStr());
		}
	}
	else if (m_tcpClt != nullptr && m_ioMode == "none") {
		int iSent = m_tcpClt->SendData((unsigned char*)pData, iLen);
		if (m_bEnableIoLog) {
			string remoteAddr = m_tcpClt->m_remoteIP + ":" + str::fromInt(m_tcpClt->m_remotePort);
			string localAddr  = m_tcpClt->m_strLocalIP + ":" + str::fromInt(m_tcpClt->m_iLocalPort);
			IOLogSend((unsigned char*)pData, iLen, iSent > 0, remoteAddr, localAddr, getIOAddrStr());
		}
	}
	else {
		//直接发送给设备
		//此处不要先锁 pIOSession再 send，否则和 tcpLinkLock会锁套锁
		shared_ptr<TDS_SESSION> pIOSessTmp = getIOSession();
		if (pIOSessTmp) {
			pIOSessTmp->send(pData, iLen);
		}
		//通过协议适配器发送给设备
		else {
			if (m_ioMode == "adaptor") {
				if (ioSrv.m_udpSrv_tdsp != nullptr) {
					size_t iSent = ioSrv.m_udpSrv_tdsp->SendData(pData, iLen, ioSrv.m_strAdpIp, ioSrv.m_iAdpPort);
					if (m_bEnableIoLog) {
						string remoteAddr = "UDP-" + ioSrv.m_strAdpIp + ":" + str::fromInt(ioSrv.m_iAdpPort);
						string localAddr  = "UDP-" + ioSrv.m_udpSrv_tdsp->m_bindIP + str::fromInt(ioSrv.m_udpSrv_tdsp->m_port);
						IOLogSend((unsigned char*)pData, iLen, iSent > 0,remoteAddr ,localAddr, getIOAddrStr());
					}
				}
			}
			else if (m_ioMode == "standAloneIO" && m_standAloneIOType != "") {
				if (ioSrv.m_udpSrv_tdsp != nullptr) {
					if (ioSrv.m_standAloneIO.find(m_standAloneIOType) != ioSrv.m_standAloneIO.end()) {
						STANDALONE_IO saio = ioSrv.m_standAloneIO[m_standAloneIOType];

						size_t iSent = ioSrv.m_udpSrv_tdsp->SendData(pData, iLen, saio.ip, saio.port);
						if (m_bEnableIoLog) {
							string remoteAddr = "UDP-" + saio.ip +":" + str::fromInt(saio.port);
							string localAddr  = "UDP-" + ioSrv.m_udpSrv_tdsp->m_bindIP + str::fromInt(ioSrv.m_udpSrv_tdsp->m_port);
							IOLogSend((unsigned char*)pData, iLen, iSent > 0, remoteAddr, localAddr, getIOAddrStr());
						}
					}
				}
			}
			else {
				LOG("[error]内部错误，设备sendData未建立正确的目标地址，%s", getIOAddrStr().c_str());
				return false;
			}
		}
	}

	return true;
}

bool ioDev::sendStr(const char* str)
{
	string s = str;
	return sendStr(s);
}

bool ioDev::sendStr(char* str)
{
	string s = str;
	return sendStr(s);
}

bool ioDev::sendStr(string& str)
{
	return sendData((unsigned char*)str.c_str(), str.length());
}

bool ioDev::SendHeartbeatPkt()
{
	return false;
}

bool ioDev::onRecvPkt(json jPkt)
{
	return false;
}

bool ioDev::onRecvPkt(yyjson_val* jPkt, yyjson_doc* doc)
{
	return false;
}

void notifyDevOnline(string sNotify) {
	//发给客户端,tds不接受devOnline事件
	rpcSrv.notify("devOnline", sNotify);

	//发给数据服务
	if (sNotify.find("tag")!=string::npos) {
		tds->callAsyn("objOnline", sNotify);
	}
}

void notifyDevOffline(json jNotify) {
	string ioAddr = jNotify["ioAddr"];
	rpcSrv.notify("devOffline", jNotify.dump());

	//发给数据服务
	if (jNotify.contains("tag")) {
		tds->callAsyn("objOffline", jNotify);
	}
}

void ioDev::setOnline(bool setByPing /*= false*/) {
	// 在线状态当有ping检测时，忽略其他
	// if ping is enabled ,ioDev can be setonline by other check method.
	//if (m_bEnablePingOnlineCheck && !setByPing)
	//	return;

	//[问题]观察到有pIOSession已经为空，也就是说链接已经断开。却还有缓存数据没有处理，导致处理后设置为上线的问题
	//该问题需优化

	if (m_pParent != nullptr && m_pParent != &ioSrv) {
		m_pParent->setOnline();
	}

	if (m_bOnline == false) {
		if (pIOSession) {
			LOG("[warn]设备上线setOnline设置在线状态,ioAddr=%s,remoteAddr=%s", getIOAddrStr().c_str(),pIOSession->getRemoteAddr().c_str());
		}
		else {
			LOG("[warn]设备上线setOnline设置在线状态,ioAddr=%s", getIOAddrStr().c_str());
		}

		if (m_strTagBind != "") {
			TDB* dbOS = db.getChildDB("onlineStatus");
			if (dbOS) {
				dbOS->Insert(m_strTagBind, true);
			}
		}

		if (tds->conf->enableOfflineAlarm) {
			ALARM_INFO ai;
			ai.tag = getIOAddrStr();
			ai.level = ALARM_LEVEL::normal;
			ai.type = "设备掉线";
			ai.desc = "设备地址:" + getIOAddrStr() + ",绑定位号:" + m_strTagBind;

			almSrv.Update(ai);
		}

		m_offlineCount = 0;

		m_bOnline = true;
		timeopt::now(&m_stOnlineTime);
		m_onlineInfoQueried = false;

		string sNotify = "{" 
			"\"ioAddr\":\"" + getIOAddrStr() + "\"," +
			"\"nodeID\":\"" + m_confNodeId + "\"";

		if (m_strTagBind != "") {
			sNotify += ",\"tag\":\"" + m_strTagBind + "\"";
		}
			sNotify += "}";

		thread t(notifyDevOnline, sNotify);
		t.detach();
	}
}

void ioDev::setOffline(bool setByPing/* = false*/,string reasonDesc) {
	// 掉线状态当有ping检测时，忽略其他
	if (m_bEnablePingOnlineCheck && !setByPing)
		return;

	if (m_bOnline) {
		m_offlineCount++;
		LOG("[warn]设备掉线,次数累计,%d/%d,%s,offlineReason:%s", m_offlineCount, m_offlineConfirmCount, getIOAddrStr().c_str(),reasonDesc.c_str());

		if (m_strTagBind != "") {
			TDB* dbOS = db.getChildDB("onlineStatus");
			if (dbOS) {
				DB_TIME dbt;
				dbt.setNow();
				dbOS->Insert(m_strTagBind, false, &dbt);
			}
		}

		if (m_offlineCount >= m_offlineConfirmCount) {
			m_bOnline = false;

			if (tds->conf->enableOfflineAlarm) {
				ALARM_INFO ai;
				ai.tag = getIOAddrStr();
				ai.level = ALARM_LEVEL::warn;
				ai.type = "设备掉线";
				ai.desc = "设备地址:" + getIOAddrStr() + ",绑定位号:" + m_strTagBind;

				almSrv.Update(ai);
			}

			json jNotify;
			jNotify["ioAddr"] = getIOAddrStr();
			jNotify["nodeID"] = m_confNodeId;

			if (m_strTagBind != "")
				jNotify["tag"] = m_strTagBind;

			thread t(notifyDevOffline, jNotify);
			t.detach();
		}
	}
}

void ioDev::doRespTimeStatis(int time)
{
	m_csRespTime.lock();
	m_vecRespTime.push_back(time);
	if (m_vecRespTime.size() > 5) {
		m_vecRespTime.erase(m_vecRespTime.begin());
	}

	if (m_vecRespTime.size() > 0) {
		size_t total = 0;
		for (int i = 0; i < m_vecRespTime.size(); i++) {
			total += m_vecRespTime[i];
		}
		m_avgTransactionTime =(int) (total / m_vecRespTime.size());
	}
	m_csRespTime.unlock();
}

bool ioDev::isConnected()
{
	return false;
}

int ioDev::GetAcqInterval()
{
	return 0;
}

void ioDev::OnRequestTimeout(int cmd1, int cmd2)
{

}


void ioDev::DoCycleTask() {
	DEV_PKT req, resp;
	if (timeopt::CalcTimePassSecond(m_stLastHeartbeatTime) > ioDev::m_heartBeatInterval&& ioDev::m_heartBeatInterval > 0) {
		SendHeartbeatPkt();
		timeopt::now(&m_stLastHeartbeatTime);
	}
}

void ioDev::checkAcqReqTimeout()
{
	if (timeopt::CalcTimePassSecond(lastSendTime) > 5)
	{
	}	
}

bool ioDev::onRecvData(unsigned char* pData, size_t iLen)
{
	if (!m_bRunning)
		return false;

	DEV_PKT pkt;
	if (!pkt.unpack(pData, iLen))
		return false;

	bool bRetu = false;


	return false;
}

bool ioDev::onRecvData(TIME dataTime, unsigned char* pData, size_t iLen)
{
	return true;
}

ioChannel* ioDev::getChanByDevAddr(string addr)
{
	for (auto i : m_mapDataChannel)
	{
		if (i.second->getDevAddrStr() == addr) return i.second;
	}

	for (auto i : m_mapBatchDataLink)
	{
		string s = i.first;
		s = str::trim(s, "#");
		if (addr.find(s) == 0)
		{
			string wildCardVal = addr.substr(s.length(), addr.length() - s.length());
			ioChannel* p = new ioChannel();
			p->m_devAddr = addr;
			string bindTag = i.second;
			bindTag = str::replace(bindTag, "*", wildCardVal);
			bindTag = str::replace(bindTag, "/", ".");
			p->m_strTagBind = bindTag;
			m_mapDataChannel[addr] = p;
			return p;
		}
	}
	return NULL;
}

ioChannel* ioDev::getChanByIOAddr(string addr)
{
	for (int i = 0; i < m_channels.size(); i++)
	{
		ioChannel* pC = m_channels[i];
		if (pC->getIOAddrStr() == addr)
			return pC;
	}

	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* pChild = m_vecChildDev[i];
		ioChannel* pC = pChild->getChanByIOAddr(addr);
		if (pC)
			return pC;
	}

	return NULL;
}

ioChannel* ioDev::GetDataChannelByMPTag(string strMPTag)
{
	for (auto it : m_mapDataChannel)
	{
		if(it.second->m_strTagBind == strMPTag) return it.second;
	}
	return NULL;
}

void ioDev::saveConfBuff()
{
	string path = db.getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/conf.json";
	fs::createFolderOfPath(path);
	string data = m_jConf.dump(4);
	fs::writeFile(path, data);
}

bool ioDev::loadConfBuff()
{
	string path = db.getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/conf.json";
	string s;
	if (!fs::readFile(path, s))
		return false;
	try
	{
		m_jConf = json::parse(s);
	}
	catch (std::exception& e)
	{
		string s = e.what();
		LOG("[warn]loadConfBuff fail," + s + ",ioAddr=" + getIOAddrStr());
		return false;
	}
	
	return true;
}

void ioDev::saveInfoBuff()
{
	string path = db.getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/info.json";
	fs::createFolderOfPath(path);
	string data = m_jInfo.dump(4);
	fs::writeFile(path, data);
}

bool ioDev::loadInfoBuff()
{
	string path = db.getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/info.json";
	string s;
	if (!fs::readFile(path, s))
		return false;
	try
	{
		m_jInfo = json::parse(s);
	}
	catch (std::exception& e)
	{
		string s = e.what();
		LOG("[warn]loadInfoBuff fail," + s + ",ioAddr=" + getIOAddrStr());
		return false;
	}

	return true;
}

void ioDev::saveStatusBuff()
{
	string path = db.getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/status.json";
	fs::createFolderOfPath(path);
	json status;
	m_csAlmStatus.lock();
	status["alarms"] = m_jAlarmStatus;
	m_csAlmStatus.unlock();
	status["channels"] = m_jAcq;
	string data = status.dump(4);
	fs::writeFile(path, data);
}

bool ioDev::loadStatusBuff()
{
	string path = db.getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/status.json";
	string s;
	if (!fs::readFile(path, s))
		return false;
	try
	{
		json status = json::parse(s);
		m_csAlmStatus.lock();
		m_jAlarmStatus = status["alarms"];
		m_csAlmStatus.unlock();
		m_jAcq = status["channels"];
	}
	catch (std::exception& e)
	{
		string s = e.what();
		LOG("[warn]loadStatusBuff fail," + s +",ioAddr=" + getIOAddrStr());
		return false;
	}

	return true;
}

void ioDev::OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession)
{
	IOLogRecv((unsigned char*)recvData, recvDataLen,udpSession);
	onRecvData(recvData, recvDataLen);
}

bool ioDev::addChild(ioDev* p)
{
	DEV_CONF_LOCK_UNIQUE(this);
	p->m_pParent = this;
	if (p->m_level == "channel")
	{
		m_channels.push_back((ioChannel*)p);
		m_mapDataChannel[p->m_devAddr] = (ioChannel*)p;
	}
	else
	{
		m_vecChildDev.push_back(p);
	}
	
	return true;
}

void ioDev::deleteChildren()
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		delete m_vecChildDev[i];
	}
	m_vecChildDev.clear();
}

void ioDev::deleteAllChannels()
{
	m_mapDataChannel.clear();
	for (int i = 0; i < m_channels.size(); i++)
	{
		delete m_channels[i];
	}
	m_channels.clear();
}

void ioDev::deleteChild(ioDev* p)
{
	for (int i=0;i<m_vecChildDev.size();i++)
	{
		ioDev* pTemp = m_vecChildDev.at(i);
		if (pTemp == p)
		{
			m_vecChildDev.erase(m_vecChildDev.begin() + i);
			break;
		}
	}
}

void ioDev::deleteDescendant(ioDev* p)
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* pTemp = m_vecChildDev.at(i);
		pTemp->deleteDescendant(p);
		if (pTemp == p)
		{
			m_vecChildDev.erase(m_vecChildDev.begin() + i);
			break;
		}
	}
}


ioDev* ioDev::getChild(string devAddr)
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev.at(i);
		if (p->getDevAddrStr() == devAddr)
			return p;
	}
	return nullptr;
}

bool ioDev::IsAsynPacket(DEV_PKT* pd)
{
	////除了当前正在同步请求的命令，其他都做异步处理
	//if (m_pCommAddrInfo->strInSyncCmdID == pd->GetCmdID()) //这条命令正在进行同步通讯，不能异步处理
	//{
	//	return false;
	//}

	//if (m_pCommAddrInfo->strInSyncCmdID == "*")
	//{
	//	return false;
	//}

	return true;
}

bool ioDev::startUploadFirmware(string firmwareFileName)
{
	return false;
}

bool ioDev::NotNeedGateway()
{
	if (str::isIp(m_devAddr))
		return true;

	return false;
}

void ioDev::setRecvCallback(void* pUser, fp_ioAddrRecvCallback callback)
{
	//LOG("ioAddr=" + getIOAddrStr() + ",设置接收回调" + str::fromInt((DWORD)pUser));
	m_pCallbackUser = pUser; 
	m_pRecvCallback = callback; 
	if (m_pCallbackUser == nullptr)
		m_bInUse = false;
	else
		m_bInUse = true;
}

string ioDev::GetCommIP()
{
	if (m_devAddr.find('.') !=  string::npos || m_devAddr.find("COM") != string::npos) //如果自己配置了IP，那么该ip为该设备的ip或者是该设备网关的ip
	{
		return m_devAddr;
	}
	else //没有配置ip，使用父网关采集设备的ip
	{
		if (m_pParent && m_pParent->IsGateway())
		{
			return m_pParent->m_devAddr;
		}
	}

	return "";
}

void ioDev::SendToChild(TIME dataTime, unsigned char* pData, size_t iLen, string strID)
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		if (m_vecChildDev.at(i)->m_devAddr == strID)
		{
			m_vecChildDev.at(i)->onRecvData(dataTime, pData, iLen);
		}
	}
}

ioChannel* ioDev::getChanByTag(string tag)
{
	for (auto& child : m_channels)
	{
		ioChannel* pC = (ioChannel*)child;
		if (pC->m_strTagBind == tag || tag == pC->getTagBind())
		{
			return pC;
		}
	}


	for (auto& child : m_vecChildDev)
	{
		ioChannel* pC = child->getChanByTag(tag);
		if (pC)
			return pC;
	}

	return nullptr;
}

void ioDev::recursiveGetBindedChanCount(ioDev* p, size_t& count) {
	for (int i = 0; i < p->m_channels.size(); i++) {
		ioChannel* pC = p->m_channels[i];
		if (pC->m_strTagBind != "") {
			count++;
		}
	}


	for (int i = 0; i < p->m_vecChildDev.size(); i++)//网络直连设备
	{
		ioDev* c = p->m_vecChildDev[i];
		recursiveGetBindedChanCount(c, count);
	}
}


void ioDev::recursiveGetChanCount(ioDev* p, size_t& count) {
	count += p->m_channels.size();

	for (int i = 0; i < p->m_vecChildDev.size(); i++)//网络直连设备
	{
		ioDev* c = p->m_vecChildDev[i];
		recursiveGetChanCount(c, count);
	}
}

void ioDev::closeAllCycleAcq()
{
	m_bEnableAcq = false;

	for (int i = 0; i < m_vecChildDev.size(); i++) {
		ioDev* p = m_vecChildDev[i];
		p->closeAllCycleAcq();
	}
}

void ioDev::openAllCycleAcq()
{
	m_bEnableAcq = true;

	for (int i = 0; i < m_vecChildDev.size(); i++) {
		ioDev* p = m_vecChildDev[i];
		p->openAllCycleAcq();
	}
}

void ioDev::unbindIOSession()
{
	pIOSession = nullptr;
}

void ioDev::bindIOSession(shared_ptr<TDS_SESSION> ioSession)
{
	if (ioSession == nullptr)
		return;

	std::unique_lock<mutex> lock(m_csIOSession);
	
	//已绑定
	if (pIOSession == ioSession)
		return;

	// tdspPort端口连接不进行替换绑定.这里看不懂了，暂时注释2025.4.12 卢涛
	//if (pIOSession != nullptr && isConnected())
	//{
	//	if (tds->conf->tdspPort == ioSession->localPort)
	//	{
	//		return; 
	//	}
	//}

	ioSession->bindIoDev(this);

	if (pIOSession != nullptr && isConnected())
	{
		string ioAddr = getIOAddrStr();
		string devInfo = "ioAddr=" + getIOAddrStr() + ",tag=" + m_strTagBind;
		LOG("[warn][ioDev]老连接未断开，设备在新连接上线。设备:" + devInfo + ",老连接:" + pIOSession->getRemoteAddr() + ",新连接:" + ioSession->getRemoteAddr());
		//解除原有session对该io设备的绑定
		//解除绑定后，老链接的tcp连接还在，但是老连接断开不会造成设备掉线。 容错机制
		pIOSession->unbindIoDev(this);
	}

	//新的有效连接
	if (ioSession != pIOSession)
	{
		triggerCycleAcq();
	}

	pIOSession = ioSession;

	//在ioSession中添加历史绑定记录
	string ioAddr = this->getIOAddrStr();
	ioSession->m_vecHistIoBindTag[this->m_strTagBind] = this->m_strTagBind;
	ioSession->m_vecHistIoDev[ioAddr] = ioAddr;
}


CCanTransparentGateway::CCanTransparentGateway()
{
	m_devType = "can_gateway";
}


void ioDev::statisOnRecv(unsigned char* recvData, size_t len, string addr)
{
	if (commpktSessions.size() == 0)
		return;

	json j;
	TIME st;
	timeopt::now(&st);
	j["time"] = timeopt::st2strWithMilli(st);
	j["ioAddr"] = addr;
	j["type"] = "接收";
	j["len"] = len;
	j["data"] = str::bytesToHexStr(recvData, len);
	string s = j.dump();

	sendToCommLog(s);
}


void ioDev::statisOnSend(unsigned char* sendData, size_t len, string addr)
{
	if (commpktSessions.size() == 0)
		return;

	json j;
	TIME st;
	timeopt::now(&st);
	j["time"] = timeopt::st2strWithMilli(st);
	j["ioAddr"] = addr;
	j["type"] = "发送";
	j["len"] = len;
	j["data"] = str::bytesToHexStr(sendData, len);
	string s = j.dump();

	sendToCommLog(s);
}





string ioDev::removePortFromIoAddr(string ioAddr) {
	vector<string> addrNodes, addrNodes1;
	str::split(addrNodes, ioAddr, "/");
	for (int i = 0; i < addrNodes.size(); i++) {
		string a = addrNodes[i];
		a = ioDev::removePortFromDevAddr(a);
		addrNodes1.push_back(a);
	}

	string ioAddrNew;
	for (int i = 0; i < addrNodes1.size(); i++) {
		string a = addrNodes1[i];
		if (i == 0)
			ioAddrNew += a;
		else
		{
			ioAddrNew += "/";
			ioAddrNew += a;
		}
	}

	return ioAddrNew;
}

string ioDev::removePortFromDevAddr(string devAddr) {
	//除去端口号
	size_t pos = devAddr.find(":");
	if (pos > 0) {
		devAddr = devAddr.substr(0, pos);
	}

	return devAddr;
}

void ioDev::onRecvData_tcpClt(unsigned char* pData, size_t len, tcpSessionClt* connInfo)
{
	IOLogRecv(pData, len, connInfo->getRemoteAddr(), connInfo->getLocalAddr(), getIOAddrStr());
	onRecvData(pData, len);
}

void ioDev::statusChange_tcpClt(tcpSessionClt* pTcpSessClt, bool bIsConn) {
	if (bIsConn) {
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSessClt));
		p->type = TDS_SESSION_TYPE::iodev;

		ioSrv.m_mutexIoSessions.lock();
		ioSrv.m_IoSessions[pTcpSessClt] = p;
		ioSrv.m_mutexIoSessions.unlock();

		//io服务主动连上TcpServer模式的设备
		string ioAddr = str::format("%s:%d", pTcpSessClt->remoteIP.c_str(), pTcpSessClt->remotePort);
		ioDev* pIoDev = ioSrv.getIODev(ioAddr);

		if (pIoDev) {
			p->ioDevType = pIoDev->m_devType;

			pIoDev->bindIOSession(p);
			pIoDev->setOnline();
			timeopt::now(&pIoDev->m_stLastActiveTime);

			string s = str::format("[ioDev]设备上线,设备类型:%s,ioAddr:%s", pIoDev->m_devType.c_str(), pIoDev->getIOAddrStr().c_str());
			logger.logInternal(s);

			pIoDev->onEvent_online();
		}
	}
	else {
		ioSrv.m_mutexIoSessions.lock();
		std::shared_ptr<TDS_SESSION> p = ioSrv.m_IoSessions[pTcpSessClt];
		ioSrv.m_IoSessions.erase(pTcpSessClt);
		ioSrv.m_mutexIoSessions.unlock();

		p->onTcpDisconnect();
	}
}

//网关，设备，通道  三级设备可以进行三级绑定，并将3级绑定的位号组合成1个最终绑定的位号
string ioDev::getTagBind() {
	string tagBind = m_strTagBind;
	ioDev* pParent = this->m_pParent;
	while (pParent) {
		if (tagBind.find(pParent->m_strTagBind) == string::npos) {
			tagBind = pParent->m_strTagBind + "." + tagBind;
		}
		pParent = pParent->m_pParent;
	}
	return tagBind;
}

void ioDev::doStatis(DEV_STATIS& statis)
{
	statis.iTotal++;

	if (m_bOnline) {
        statis.iOnline++;
	}
	else {
        statis.iOffline++;
	}

	if (m_dispositionMode == DEV_DISPOSITION_MODE::spare) {
        statis.iSpare++;
	}
	else {
        statis.iManaged++;
	}

	statis.iChan += m_channels.size();

    for (int i = 0; i < m_vecChildDev.size(); i++) {
		ioDev* p = m_vecChildDev[i];
		p->doStatis(statis);
	}
}
