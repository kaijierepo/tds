#pragma once
#include "obj.h"
#include "tdsConf.h"
#include "json.hpp"
using json = nlohmann::json;
#include "tdsSession.h"
#include "tds.h"
#include <shared_mutex>
#include "scriptManager.h"

class RTSPRelay;

struct OBJ_TEMPLATE {
	std::string type;
	json tplData;
	OBJ obj;

	OBJ_TEMPLATE() {

	}

	~OBJ_TEMPLATE() {

	}
};

class ioServer;
class TDB;
class ioDev;
class MP;
class TAG_SELECTOR;

struct EZVIZ_ACCESS_INFO {
	std::string tag;
	std::string serialNo;
	std::string appKey;
	std::string secret;
	std::string token;
	std::string flvUrl;
	std::string ezopenUrl;
	TIME lastUpdate;
};


struct LOCK_THREAD_RECORDER {
	LOCK_THREAD_RECORDER(unsigned long* recorder, unsigned long id) {
		*recorder = id;
		pid = recorder;
	}

	~LOCK_THREAD_RECORDER() {
		if (pid) {
			*pid = 0;
		}
	}

	unsigned long* pid;
};

extern map<std::string, std::string> g_mapConfFile;


class project : public OBJ  
{
public:
	bool loadConfFile();
	bool loadConf(std::string& confStr);
	bool loadConf(json& jConf,bool bCreate=true);
	bool loadConf(yyjson_val* conf, bool bCreate = true);
	bool saveConfFile();
	void clear();
	void getMpTypeList(json& mpTypeList);

	void rpc_setObj(yyjson_val* params, RPC_RESP& resp, RPC_SESSION& session);

	std::vector<MP*> getAllEzvizMp();

	std::string m_moConfFileDump; //字符串配置数据//最近一次保存的缓存，如果前端获取整颗树，直接获取此处加快速度
	map<std::string, MP*> m_mapAllMP;

	bool m_enableEzviz;
	bool m_enableZLM;

	map<std::string, std::unique_ptr<RTSPRelay>> m_mapRtspRelays;
	map<std::string, std::unique_ptr<RTSPRelay>> m_mapRtspRelays_urlID;
	mutable std::mutex m_relayMutex_urlID;
	mutable std::mutex m_relayMutex; // 保护 m_mapRtspRelays 的互斥锁
	map<std::string, EZVIZ_ACCESS_INFO> m_mapEzvizAccess;

public:
	project();
	virtual ~project();

	json getTypeTagByTag(std::string tag); //tag可以比当前对象树的配置更深

	void saveRtStatus();

	void loadRtDB();

	void runRtDB();

	//对象模版配置
	json getObjTemplate(std::string objTplType);
	bool loadObjTemplate();
	void setObjTemplate(json& params);
	void getAllVarExpScript();
	map<std::string, OBJ_TEMPLATE*> m_mapObjTempalte;

	bool openStream(std::string tag,std::string pushTo = "");

	bool closeStream(std::string tag);

private:
	json m_jMOTree;
	map<std::string, std::string> m_mapDataLink;

public:
	shared_mutex m_csPrj;
	unsigned long m_prjWriteLockThread;
};

extern void g_getTagsByTagSelector(TAG_SELECTOR& tagSelector,SELECT_RLT& rlt);
extern project prj;
