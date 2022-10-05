#pragma once 
#include "tdscore.h"
#include "mo.h"
#include "json.hpp"
#include "tdsSession.h"
#include "tds.h"
#include "videoCodec.h"
#include <memory>


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
	float high;
	bool enableLow;
	float low;
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
	float min;
	float max;
};

using namespace std;
class MO;
class MP : public MO
{
public:
	MP();
	~MP();

	json strVal2Val(string s);

	bool loadConf(json& conf);
	bool toJson(json& conf, json serializeOption) override;
	bool loadStatus(MO* pSrc) override;
	void calcAlarm();
public:
	//deData表示是否有独立的数据元文件数据，使用一个json数组字符串
	//例如 deData = "[deFolder,video,pic]"
	//deFolder表示数据元数据统一放在一个文件夹中，用于一些特别复杂的数据
	//video表示有一个关联的视频文件
	//pic表示有一个关联的图片文件
	//当deFolder和video，pic同时存在时，pic和video放在deFolder中，否则和数据元索引文件放在同一个目录
	void input(json jVal, SYSTEMTIME* dataTime=NULL, json dataFile = nullptr);
	bool output(json jVal, json& rlt, json& err,bool sync = true);
	bool IsCurValValid();
	string getMpTypeLabel();
	string getMpType();
	json getRTData(string root="",bool bValOnly = false);

	//配置
	json m_defaultVal; //默认值，软件刚启动时加载的值
	string m_valType;//数值类型， bool，模拟量，json等
	string m_valTypeLabel;
	string m_ioType;
	string m_ioTypeLabel;
	//监测点类型，按应用方式来区分，例如温度、湿度、车闸、人闸等。
	//当使用者说 我想看一下温度的数据，我想看一下车闸的数据，这个XXX的数据就是监测点类型
	//json的值类型必须要指定物理量类型
	//其他类型可以不指定，将mp的name当做物理量类型。 也可以指定
	string m_mpType;
	//string m_physicalType;
	bool m_alarmMp;//该监控点是1个报警状态。 报警类型默认为监控点名称
	string m_strUnit;
	int m_decimalDigits;
	TIME_SPAN m_saveInterval;
	int getSaveInterval();
	string m_saveMode;
	ALARM_LIMIT m_alarmLimit;
	VALID_RANGE m_validRange;
	double m_K;
	double m_B;

	//视频
	string m_videoSrcType;
	string m_rtspAddr;
	bool m_bIsStreaming;
	DWORD m_srcPullingFFmpegProcID;

	//状态
	json m_orgVal;
	json m_curVal;
	json m_lastVal;
	SYSTEMTIME m_lastUpdateTime;
	SYSTEMTIME m_lastSaveTime;
};
