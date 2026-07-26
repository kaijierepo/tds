#pragma once 

#include "obj.h"
#include "tdsSession.h"
#include "tds.h"


struct TIME_SPAN {
	TIME_SPAN()
	{
		hour = 0;
		minute = 0;
		second = 0;
	}
	int hour;
	int minute;
	int second;
};

struct ALARM_LIMIT {
	bool enableHigh;
	double high;
	bool enableLow;
	double low;
	ALARM_LIMIT()
	{
		enableHigh = false;
		enableLow = false;
		high = 0;
		low = 0;
	}
};

struct VALID_RANGE {
	bool enable;
	double min;
	double max;
};

using namespace std;
class OBJ;

struct MP_STATUS {
	std::string m_pullingSrcUrl; //当前正在拉流的地址
};

namespace DATA_SAVE_MODE {
	const std::string always = "always";
	const std::string onchange = "onchange";
	const std::string cyclic = "cyclic";
	const std::string never = "never";
	const std::string cyclic_onchange = "cyclic|onchange";
}

struct DATA_SIMU_CONF {
	double lowLimit;
	double highLimit;
	bool enable;
	int interval;

	DATA_SIMU_CONF() {
		lowLimit = 0;
		highLimit = 0;
		enable = false;
		interval = 0;
	}
};


class MP : public OBJ
{
public:
	MP();
	~MP();

	json strVal2Val(std::string s);

	//bool loadConf(json& conf, bool bCreate = true) override;
	bool loadConf(yyjson_val* conf, bool bCreate = true) override;
	bool loadStatus(yyjson_val* status) override;
	bool toJson(yyjson_mut_val* conf, yyjson_mut_doc* doc, OBJ_PROP_SEL q, bool* parentSelectedByLeafType = nullptr, const std::string& user = "admin") override;
	std::string getValDesc(json& jVal, bool getUnit = true);
	//bool toJson(json& conf, json serializeOption) override;
	bool loadTreeStatus(OBJ* pSrcTree) override;
	bool loadObjStatus(OBJ* pSrcObj) override;
	void calcAlarm();
public:
	//fileData为数据元携带的文件数据
	//这个函数为最频繁调用的函数，使用引用提高性能
	void input(json& jVal,  json* fileData = NULL, TIME* dataTime = NULL);
	void updateVal(std::string& jstrVal, TIME& dataTime, std::string& fileData);
	OBJ* getParentMo();

	bool needSaveToDB();
	void saveToDB();
	void output(json jVal, json& rlt, json& err,bool sync = true);
	bool IsCurValValid();
	std::string getMpTypeLabel();
	std::string getValDesc(bool getUnit);
	std::string getMpType();

	void stopStreamPull(std::string zlmProxyKey);

	bool startStreamPull();
	bool stopStreamPush();
	bool startStreamPush(std::string desUrl);
	std::string m_strPusherProxyKey;
	std::string m_strPullProxyKey;

	DATA_SIMU_CONF m_simuConf;

	MP_STATUS m_mpStatus;

	//配置
	std::string m_valType;//数值类型， bool，模拟量，json等
	std::string m_ioType;
	std::string m_ioTypeLabel;
	//监测点类型，按应用方式来区分，例如温度、湿度、车闸、人闸等。
	//当使用者说 我想看一下温度的数据，我想看一下车闸的数据，这个XXX的数据就是监测点类型
	//json的值类型必须要指定mpType，相当于json数据的数据字典
	//其他类型可以不指定，将mp的name当做物理量类型。 也可以指定
	std::string m_mpType;
	//std::string m_physicalType;
	bool m_alarmMp;//该监控点是1个报警状态。 报警类型默认为监控点名称
	std::string m_strUnit;
	int m_decimalDigits;
	TIME_SPAN m_saveInterval;
	int getSaveInterval();
	std::string m_saveMode;
	ALARM_LIMIT m_alarmLimit;
	VALID_RANGE m_validRange;
	double m_K;
	double m_B;
	double m_deadZone;
	//bool m_bDownSample;
	//int m_downSampleInterval; //单位毫秒
	//TIME m_stLastSampleTime;
	std::string m_srcStreamFetch; //ondemand按需拉流 或 always持续拉流
	std::string m_expression;    //计算表达式

	//整型枚举值
	bool getValByEnumVal(std::string enumVal, int& val);
	map<int, std::string> mapEnumVal;
	bool m_isEnum;


	//视频
	std::string m_mediaSrcType;
	std::string m_mediaUrl;
	std::string m_relayTransport; //推流转发传输协议: "tcp" | "udp"，默认 "tcp"
	bool m_bIsStreaming;
	bool m_bServeStream;  //从视频源拉流，向外提供流媒体服务
	std::string m_serialNo; //设备序列号
	std::string m_appKey;
	std::string m_secret;
	bool m_isOpenningStream; //当前是否正在打开媒体源。如果连续发送多个打开媒体源的请求，忽略后面的请求

	//状态
	// hasValue = true 表示有一个值被写入,lastUpdateTime有效,m_curVal可以为null。(当数据超出有效范围时，curVal将等于null，并且这是一个有意义的null值，该值应当被记录入数据库,同时可以将orgVal记录到数据库)
	// hasValue = false 表示从没有收到过数据。lastUpdateTime为无效。
	// curValValid = true 表示 m_curVal!=null
	// curValValid = false 表示 m_curVal==null
	mutex m_curValLock;
	std::string m_orgVal = "null";
	std::string m_curVal = "null";
	std::string m_defaultVal = "null"; //默认值，软件刚启动时加载的值
	map<std::string,std::string> m_curValAttr;  //与val同级的Attri
	std::string m_curFileData = "null";
	std::string m_lastVal = "null";
	TIME m_lastSaveTime;
	int  m_lastSaveInterval;  //上一次采集到数据后，距离上一次保存时间达到保存时间间隔，存储改时间间隔，评估是否都定时存了
	bool hasValue();

	//推拉流
	std::vector<std::shared_ptr<TDS_SESSION>> m_vecPuller;
	mutex m_csPuller;

	TIME m_simuDataTime;
};
