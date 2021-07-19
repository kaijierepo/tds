#pragma once 
#include "tdscore.h"
#include "mo.h"
#include "json.hpp"
#include "tdsSession.h"
#include "tds.h"
#include "videoCodec.h"
#include <memory>

using namespace std;
class MO;
class MP : public MO
{
public:
	MP();
	~MP();

	bool loadConf(json& conf);
public:
	void inputVal(json jVal, SYSTEMTIME* dataTime=NULL, bool bPic = false);//bPic: whether or not the data element have a related picture
	bool outputVal(json jVal);
	bool IsCurValValid();
	string getMpTypeLabel();
	string getMpType();
	json getRTData();
	json getRT();
	json m_orgVal;
	json m_curVal;
	string m_valType;
	string m_customValType;
	string m_physicalType;
	string m_strUnit;
	SYSTEMTIME m_lastUpdateTime;

	std::shared_ptr<TDS_SESSION> m_streamPuller; //拉流方
	fp_startStream m_streamPusher; //推流方
#ifdef ENABLE_FFMPEG
	videoCodec* m_videoCodec; //
#endif

	float m_K; 
	float m_B; 
};
