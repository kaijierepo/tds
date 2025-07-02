#include "pch.h"
#include "mp.h"
#include "obj.h"
#include "common.h"
#include "prj.h"
#include "tAlmSrv.h"
#include "rpcHandler.h"
#include "tdb.h"
#include "logger.h"
#include "ioSrv.h"
#include "ioChan.h"


#include "json.hpp"

inline string getValTypeLabel(string valType)
{
	if (valType == "json") return "JSON";
	else if (valType == "float") return "浮点型";
	else if (valType == "int") return "整型";
	else if (valType == "bool") return "布尔型";
	else if (valType == "video") return "视频";
	else if (valType == "string") return "字符串型";
	else return "未知值类型";
};

inline string getIOTypeLabel(string valType)
{
	if (valType == "i") return "输入";
	else if (valType == "o") return "输出";
	else if (valType == "io") return "输入/输出";
	else if (valType == "v") return "内部变量";
	else if (valType == "c") return "常量";
	else return "未知IO类型";
};

MP::MP()
{
	m_level = "mp";
	m_stDataLastUpdate.initAsInvalid();
	timeopt::setAsTimeOrg(m_lastSaveTime);
	//timeopt::setAsTimeOrg(m_stLastSampleTime);
	m_K = 1;
	m_B = 0;
	m_bIsStreaming = false;
	m_deadZone = 0;
	//m_bDownSample = 0;
	//m_downSampleInterval = 2000;
	m_srcStreamFetch = "ondemand";
	m_bServeStream = false;
	m_isOpenningStream = false;
}

MP::~MP()
{
}

//string格式主要是人容易阅读,书写的字符串格式. 一般默认为 Hjson格式
json MP::strVal2Val(string sdv)
{
	json jVal;
	if (sdv == "" || sdv == "?")
	{
		//保持无效值
		//m_curVal.empty()==true
	}
	else
	{
		if (m_valType == VAL_TYPE::boolean)
		{
			if (sdv == "1" || sdv == "true" || sdv == "开")
				jVal = true;
			else if (sdv == "0" || sdv == "false" || sdv == "关")
				jVal = false;
		}
		else if (m_valType == VAL_TYPE::integer)
		{
			jVal = atoi(sdv.c_str());
		}
		else if (m_valType == VAL_TYPE::Float)
		{
			jVal = atof(sdv.c_str());
		}
		else if (m_valType == VAL_TYPE::str)
		{
			jVal = sdv;
		}
	}

	return jVal;
}


bool MP::loadConf(json& conf,bool bCreate)
{
	OBJ::loadConf(conf);
	if(conf["valType"]!=nullptr)
		m_valType = conf["valType"].get<string>();

	if (conf["alarmMp"] != nullptr)
		m_alarmMp = conf["alarmMp"].get<bool>();
	else
		m_alarmMp = false;

	//不仅仅float类型可以使用单位. 整形也可以使用单位。例如： 3次   5个 等等 
	if (conf["unit"] != nullptr)
		m_strUnit = conf["unit"].get<string>();

	if (conf["decimalDigits"] != nullptr)
		m_decimalDigits = conf["decimalDigits"].get<int>();
	else
		m_decimalDigits = -1;

	if (conf["ioType"] != nullptr)
	{
		m_ioType = conf["ioType"].get<string>();
		//m_ioTypeLabel = getIOTypeLabel(m_ioType);
	}

	if (conf["alarmLimit"] != nullptr)
	{
		json jAL = conf["alarmLimit"];
		if (jAL["enableHigh"].is_boolean()){
			m_alarmLimit.enableHigh = jAL["enableHigh"].get<bool>();
		}
		if (jAL["high"].is_number()) {
			m_alarmLimit.high = jAL["high"].get<float>();
		}
		if (jAL["enableLow"].is_boolean()) {
			m_alarmLimit.enableLow = jAL["enableLow"].get<bool>();
		}
		if (jAL["low"].is_number()) {
			m_alarmLimit.low = jAL["low"].get<float>();
		}
	}

	if (conf["validRange"] != nullptr)
	{
		json j = conf["validRange"];
		if (j["enable"].is_boolean()) {
			m_validRange.enable = j["enable"].get<bool>();
		}
		if (j["min"].is_number()) {
			m_validRange.min = j["min"].get<double>();
		}
		if (j["max"].is_number()) {
			m_validRange.max = j["max"].get<double>();
		}
	}

	if (m_valType == VAL_TYPE::json)
	{
		if(conf["mpType"]!=nullptr)
		m_mpType = conf["mpType"].get<string>();
	}

	if (conf["saveMode"] != nullptr)
	{
		m_saveMode = conf["saveMode"].get<string>();
	}
	else
	{
		m_saveMode = DATA_SAVE_MODE::always;
	}

	if (conf["saveInterval"] != nullptr)
	{
		json jsi = conf["saveInterval"];
		m_saveInterval.hour = jsi["hour"].get<int>();
		m_saveInterval.minute = jsi["minute"].get<int>();
		m_saveInterval.second = jsi["second"].get<int>();

		//周期模式但是周期设为0，相当于全部保存。此配置无效，默认修改为5分钟
		if (m_saveMode == DATA_SAVE_MODE::cyclic || m_saveMode == DATA_SAVE_MODE::cyclic_onchange) {
			if (getSaveInterval() == 0) {
				m_saveInterval.minute = 5;
			}
		}
	}

	if (conf["k"] != nullptr)
	{
		m_K = conf["k"].get<double>();
	}
	if (conf["b"] != nullptr)
	{
		m_B = conf["b"].get<double>();
	}

	if (conf["deadZone"].is_number()) {
		m_deadZone = conf["deadZone"].get<double>();
	}

	if (conf["defaultVal"] != nullptr)
	{
		//兼容一些错误书写,支持强转
		if (conf["defaultVal"].is_string())
		{
			m_defaultVal = strVal2Val(conf["defaultVal"].get<string>());
		}
		else
		{
			m_defaultVal = strVal2Val(conf["defaultVal"].dump());
		}

		if (m_defaultVal != nullptr && JSON_STR::is_null(m_curVal)) {
			m_curVal = m_defaultVal.dump();
		}
	}


	if (m_valType == VAL_TYPE::integer) {
		if (conf["isEnum"].is_boolean()) {
			m_isEnum = conf["isEnum"].get<bool>();
		}

		if (conf["enum"].is_array()) {
			mapEnumVal.clear();
			json& jEnum = conf["enum"];
			for (int i = 0; i < jEnum.size(); i++) {
				json& jItem = jEnum[i];
				mapEnumVal[jItem[0].get<int>()] = jItem[1].get<string>();
			}
		}
	}

	if (conf["expression"].is_string()) {
		m_expression = conf["expression"];
	}


	if (conf["mediaSrcType"] != nullptr) {
		m_mediaSrcType = conf["mediaSrcType"].get<string>();
	}

	if (conf["mediaUrl"] != nullptr) {
		m_mediaUrl = conf["mediaUrl"].get<string>();
	}

	if (m_mediaSrcType == "ezviz") {
		prj.m_enableEzviz = true;
	}


	auto kv = conf.find("serialNo");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_serialNo = item.get<string>();
		}
	}

	 kv = conf.find("appKey");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_appKey = item.get<string>();
		}
	}

	 kv = conf.find("secret");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_secret = item.get<string>();
		}
	}

	kv = conf.find("serveStream");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_boolean()) {
			m_bServeStream = item.get<bool>();
		}
	}


	//状态数据
	// 	val应该通过 loadStatus加载，不知道为何这里有这段代码。暂时注释。观察一段时间后删除
	//if(conf.contains("val"))
	//	m_curVal = conf["val"];
	if (conf.contains("time"))
	{
		string s = conf["time"].get<string>();
		if(s != "" && s != "-")
			m_stDataLastUpdate = timeopt::str2st(s);
	}

	//if (conf["downSample"].is_boolean()) {
	//	m_bDownSample = conf["downSample"].get<bool>();
	//}

	//if (conf["downSampleInterval"].is_number_integer()) {
	//	m_downSampleInterval = conf["downSampleInterval"].get<int>();
	//}

	if (conf["srcStreamFetch"].is_string()) {
		m_srcStreamFetch = conf["srcStreamFetch"];
	}

	return false;
}

bool MP::loadConf(yyjson_val* conf, bool bCreate)
{
	OBJ::loadConf(conf);

	yyjson_val* v = yyjson_obj_get(conf, "valType");
	if (v) {
		m_valType = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "alarmMp");
	if (v) {
		m_alarmMp = yyjson_get_bool(v);
	}
	else {
		m_alarmMp = false;
	}

	////不仅仅float类型可以使用单位. 整形也可以使用单位。例如： 3次   5个 等等 
	v = yyjson_obj_get(conf, "unit");
	if (v) {
		m_strUnit = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "decimalDigits");
	if (v) {
		m_decimalDigits = yyjson_get_int(v);
	}
	else {
		m_decimalDigits = -1;
	}

	v = yyjson_obj_get(conf, "ioType");
	if (v) {
		m_ioType = yyjson_get_str(v);
	}

	yyjson_val* yyalmlimit = yyjson_obj_get(conf, "alarmLimit");
	if (yyalmlimit) {
		v = yyjson_obj_get(yyalmlimit, "enableHigh");
		if (v) {
			m_alarmLimit.enableHigh = yyjson_get_bool(v);
		}
		v = yyjson_obj_get(yyalmlimit, "high");
		if (v) {
			m_alarmLimit.high = yyjson_get_num(v);
		}
		v = yyjson_obj_get(yyalmlimit, "enableLow");
		if (v) {
			m_alarmLimit.enableLow = yyjson_get_bool(v);
		}
		v = yyjson_obj_get(yyalmlimit, "low");
		if (v) {
			m_alarmLimit.low = yyjson_get_num(v);
		}
	}

	yyjson_val* yyValidRange = yyjson_obj_get(conf, "validRange");
	if (yyValidRange) {
		v = yyjson_obj_get(yyValidRange, "enable");
		if (v) {
			m_validRange.enable = yyjson_get_bool(v);
		}
		v = yyjson_obj_get(yyValidRange, "min");
		if (v) {
			m_validRange.min = yyjson_get_num(v);
		}
		v = yyjson_obj_get(yyValidRange, "max");
		if (v) {
			m_validRange.max = yyjson_get_num(v);
		}
	}

	if (m_valType == VAL_TYPE::json)
	{
		v = yyjson_obj_get(conf, "mpType");
		if (v) {
			m_mpType = yyjson_get_str(v);
		}
	}

	v = yyjson_obj_get(conf, "saveMode");
	if (v) {
		m_saveMode = yyjson_get_str(v);
	}
	else {
		m_saveMode = DATA_SAVE_MODE::always;
	}

	yyjson_val* yySaveInterval = yyjson_obj_get(conf, "saveInterval");
	if (yySaveInterval) {
		m_saveInterval.hour = yyjson_get_int(yyjson_obj_get(yySaveInterval, "hour"));
		m_saveInterval.minute = yyjson_get_int(yyjson_obj_get(yySaveInterval, "minute"));
		m_saveInterval.second = yyjson_get_int(yyjson_obj_get(yySaveInterval, "second"));

		//周期模式但是周期设为0，相当于全部保存。此配置无效，默认修改为5分钟
		if (m_saveMode == DATA_SAVE_MODE::cyclic || m_saveMode == DATA_SAVE_MODE::cyclic_onchange) {
			if (getSaveInterval() == 0) {
				m_saveInterval.minute = 5;
			}
		}
	}

	v = yyjson_obj_get(conf, "k");
	if (v) {
		m_K = yyjson_get_num(v);
	}
	v = yyjson_obj_get(conf, "b");
	if (v) {
		m_B = yyjson_get_num(v);
	}

	v = yyjson_obj_get(conf, "deadZone");
	if (v) {
		m_deadZone = yyjson_get_num(v);
	}

	yyjson_val* yyDefaultVal = yyjson_obj_get(conf, "defaultVal");
	if (yyDefaultVal) {
		//兼容一些错误书写,支持强转
		if (yyjson_is_str(yyDefaultVal)) {
			m_defaultVal = strVal2Val(yyjson_get_str(yyDefaultVal));
		}
		else {
			size_t len = 0;
			auto s = yyjson_val_write(yyDefaultVal, YYJSON_WRITE_NOFLAG, &len);
			string result = s;
			if (s) {
				free(s);
			}
			m_defaultVal = strVal2Val(result);
		}

		if (m_defaultVal != nullptr && JSON_STR::is_null(m_curVal)) {
			m_curVal = m_defaultVal.dump();
		}
	}


	if (m_valType == VAL_TYPE::integer) {
		v = yyjson_obj_get(conf, "isEnum");
		if (v) {
			m_isEnum = yyjson_get_bool(v);
		}

		v = yyjson_obj_get(conf, "enum");
		if (yyjson_is_arr(v)) {
			mapEnumVal.clear();
			size_t size = yyjson_arr_size(v);
			for (size_t i = 0; i < size; i++) {
				yyjson_val* jItem = yyjson_arr_get(v, i);
				int key = yyjson_get_int(yyjson_arr_get(jItem, 0));
				string value = yyjson_get_str(yyjson_arr_get(jItem, 1));
				mapEnumVal[key] = value;
			}
		}
	}

	v = yyjson_obj_get(conf, "expression");
	if (v) {
		m_expression = yyjson_get_str(v);
	}

	yyjson_val* yyMediaSrvType = yyjson_obj_get(conf, "mediaSrcType");
	if (yyMediaSrvType) {
		m_mediaSrcType = yyjson_get_str(yyMediaSrvType);
		if (m_mediaSrcType == "ezviz") {
			prj.m_enableEzviz = true;
		}

		v = yyjson_obj_get(conf, "mediaUrl");
		if (v) {
			m_mediaUrl = yyjson_get_str(v);
		}

		v = yyjson_obj_get(conf, "serialNo");
		if (v) {
			m_serialNo = yyjson_get_str(v);
		}

		v = yyjson_obj_get(conf, "appKey");
		if (v) {
			m_appKey = yyjson_get_str(v);
		}

		v = yyjson_obj_get(conf, "secret");
		if (v) {
			m_secret = yyjson_get_str(v);
		}

		v = yyjson_obj_get(conf, "serveStream");
		if (v) {
			m_bServeStream = yyjson_get_bool(v);
		}

		v = yyjson_obj_get(conf, "srcStreamFetch");
		if (v) {
			m_srcStreamFetch = yyjson_get_str(v);
		}
	}

	return false;
}

bool MP::loadStatus(yyjson_val* status)
{
	//常量无需加载
	if (m_ioType == "c") {
		return true;
	} 
	
	string sTime;
	TIME t;

	//如果 status 内部不携带时间，使用当前时间，使用在使用树形结构进行input的时候
	yyjson_val* yytime = yyjson_obj_get(status, "time");
	if (yytime == nullptr) {
		t = timeopt::now();
	}
	else {
		sTime = yyjson_get_str(yytime);
		if (!timeopt::isValidTimeStr(sTime)) {
			return true;
		}
		t = timeopt::str2st(sTime);
		if (t == m_stDataLastUpdate) {
			return true;
		}
	}

	m_lastVal = json::parse(m_curVal);
	yyjson_val* yyVal = yyjson_obj_get(status,"val");
	if (yyVal) {
		m_curVal = yyjson_val_write(yyVal,0,nullptr);
	}
	else
		m_curVal = JSON_STR::getNull();
	m_stDataLastUpdate = t;

	return true;
}

//bool MP::toJson(json& conf, json serializeOption)
//{	
//	OBJ_QUERIER q = parseQuerier(serializeOption);
//	return toJson(conf, q);
//}

bool MP::toJson(json& conf, OBJ_QUERIER q, bool* parentSelectedByLeafType , const string& user)
{
	if (q.dataSaveMp && m_saveMode == DATA_SAVE_MODE::never) {
		return false;
	}

	if (!OBJ::toJson(conf, q, parentSelectedByLeafType, user))
		return false;

	if (q.getConf)
	{
		//确定是否请求了该类型的监测点
		MP* p = (MP*)this;
		conf["valType"] = p->m_valType;
		if (p->m_ioType != "")
			conf["ioType"] = p->m_ioType;
		if (p->m_valType == "json")
			conf["mpType"] = p->m_mpType;
		if (p->m_alarmMp)
			conf["alarmMp"] = true;
		conf["saveMode"] = p->m_saveMode;
		if (p->m_saveMode == DATA_SAVE_MODE::cyclic || p->m_saveMode == DATA_SAVE_MODE::cyclic_onchange)
		{
			json saveInterval;
			saveInterval["hour"] = p->m_saveInterval.hour;
			saveInterval["minute"] = p->m_saveInterval.minute;
			saveInterval["second"] = p->m_saveInterval.second;
			conf["saveInterval"] = saveInterval;
		}
		if (p->m_strUnit != "")
			conf["unit"] = p->m_strUnit;
		//KB  不等于默认值则保存
		if (fabs(p->m_K - 1) > 0.000001 || fabs(p->m_B - 0) > 0.000001)
		{
			conf["k"] = p->m_K;
			conf["b"] = p->m_B;
		}

		if (p->m_saveMode == DATA_SAVE_MODE::cyclic_onchange || p->m_saveMode == DATA_SAVE_MODE::onchange) {
			conf["deadZone"] = m_deadZone;
		}

		//默认值 
		if (p->m_defaultVal != nullptr)
			conf["defaultVal"] = p->m_defaultVal;
		//报警限
		json alarmLimit;
		if (m_alarmLimit.enableHigh)
		{
			alarmLimit["enableHigh"] = m_alarmLimit.enableHigh;
			alarmLimit["high"] = m_alarmLimit.high;
		}
		if (m_alarmLimit.enableLow)
		{
			alarmLimit["enableLow"] = m_alarmLimit.enableLow;
			alarmLimit["low"] = m_alarmLimit.low;
		}
		if (alarmLimit != nullptr)
			conf["alarmLimit"] = alarmLimit;
		//有效值范围
		if (m_validRange.enable)
		{
			json validRange;
			validRange["enable"] = m_validRange.enable;
			validRange["min"] = m_validRange.min;
			validRange["max"] = m_validRange.max;
			conf["validRange"] = validRange;
		}
		//有效小数位
		if (m_decimalDigits >= 0)
			conf["decimalDigits"] = m_decimalDigits;
		if (p->m_valType == "video") {
			conf["mediaSrcType"] = m_mediaSrcType;
			conf["mediaUrl"] = m_mediaUrl;

			if (m_mediaSrcType == "ezviz") {
				conf["serialNo"] = m_serialNo;
				conf["appKey"] = m_appKey;
				conf["secret"] = m_secret;
			}
		}
		//枚举值
		if (m_valType == VAL_TYPE::integer) {
			if (m_isEnum) {
				conf["isEnum"] = true;
				if (mapEnumVal.size() > 0) {
					json jEnum = json::array();
					for (auto& i : mapEnumVal) {
						json item = json::array();
						item.push_back(i.first);
						item.push_back(i.second);
						jEnum.push_back(item);
					}
					conf["enum"] = jEnum;
				}
			}
			else {
				conf["isEnum"] = false;
			}
		}
		//降采样
		//if (m_bDownSample) {
		//	conf["downSample"] = m_bDownSample;
		//	conf["downSampleInterval"] = m_downSampleInterval;
		//}
		if (m_valType == VAL_TYPE::video) {
			conf["srcStreamFetch"] = m_srcStreamFetch;
		}

		if (m_expression != "") {
			conf["expression"] = m_expression;
		}

		if (m_bServeStream) {
			conf["serveStream"] = m_bServeStream;
		}
	}
	

	if (q.getStatus || q.getVal)
	{
		conf["val"] = json::parse(m_curVal);
		if (timeopt::isValidTime(m_stDataLastUpdate))
			conf["time"] = timeopt::st2str(m_stDataLastUpdate);
		else
			conf["time"] = "-";
	}

	if (q.getStatusDetail) {
		if (timeopt::isValidTime(m_lastSaveTime))
			conf["lastSaveTime"] = timeopt::st2str(m_lastSaveTime);
		else
			conf["lastSaveTime"] = "-";

		conf["lastSaveInterval"] = m_lastSaveInterval;
	}

	if (q.getStatus) {
		if (m_valType == VAL_TYPE::video) {
			conf["online"] = m_bOnline;
		}

		if (hasOnlineStatus()) {
			conf["online"] = m_bOnline;
		}
	}

	if (q.getStatusDesc || q.getValDesc) {
		conf["valDesc"] = getValDesc(q.getUnit);
	}
	

	return true;
}

bool MP::toJson(yyjson_mut_val* conf, yyjson_mut_doc* doc, OBJ_QUERIER q, bool* parentSelectedByLeafType, const string& user)
{
	if (q.dataSaveMp && m_saveMode == DATA_SAVE_MODE::never) {
		return false;
	}

	if (!OBJ::toJson(conf, doc, q, parentSelectedByLeafType, user)) {
		return false;
	}

	if (q.getConf){
		//确定是否请求了该类型的监测点
		MP* p = (MP*)this;

		//conf["valType"] = p->m_valType;
		yyjson_mut_val* key = yyjson_mut_strcpy(doc, "valType");
		yyjson_mut_val* val = yyjson_mut_strcpy(doc, p->m_valType.c_str());
		yyjson_mut_obj_put(conf, key, val);

		if (p->m_ioType != "") {
			//conf["ioType"] = p->m_ioType;
			key = yyjson_mut_strcpy(doc, "ioType");
			val = yyjson_mut_strcpy(doc, p->m_ioType.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
		if (p->m_valType == "json") {
			//conf["mpType"] = p->m_mpType;
			key = yyjson_mut_strcpy(doc, "mpType");
			val = yyjson_mut_strcpy(doc, p->m_mpType.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
		if (p->m_alarmMp) {
			//conf["alarmMp"] = true;
			key = yyjson_mut_strcpy(doc, "alarmMp");
			val = yyjson_mut_true(doc);
			yyjson_mut_obj_put(conf, key, val);
		}

		//conf["saveMode"] = p->m_saveMode;
		key = yyjson_mut_strcpy(doc, "saveMode");
		val = yyjson_mut_strcpy(doc, p->m_saveMode.c_str());
		yyjson_mut_obj_put(conf, key, val);

		if (p->m_saveMode == DATA_SAVE_MODE::cyclic || p->m_saveMode == DATA_SAVE_MODE::cyclic_onchange){
			//json saveInterval;
			//saveInterval["hour"] = p->m_saveInterval.hour;
			//saveInterval["minute"] = p->m_saveInterval.minute;
			//saveInterval["second"] = p->m_saveInterval.second;
			//conf["saveInterval"] = saveInterval;
			yyjson_mut_val* rootSaveInterval = yyjson_mut_obj(doc);

			key = yyjson_mut_strcpy(doc, "hour");
			val = yyjson_mut_int(doc, p->m_saveInterval.hour);
			yyjson_mut_obj_put(rootSaveInterval, key, val);

			key = yyjson_mut_strcpy(doc, "minute");
			val = yyjson_mut_int(doc, p->m_saveInterval.minute);
			yyjson_mut_obj_put(rootSaveInterval, key, val);

			key = yyjson_mut_strcpy(doc, "second");
			val = yyjson_mut_int(doc, p->m_saveInterval.second);
			yyjson_mut_obj_put(rootSaveInterval, key, val);

			key = yyjson_mut_strcpy(doc, "saveInterval");
			yyjson_mut_obj_put(conf, key, rootSaveInterval);
		}

		if (p->m_strUnit != "") {
			//conf["unit"] = p->m_strUnit;
			key = yyjson_mut_strcpy(doc, "unit");
			val = yyjson_mut_strcpy(doc, p->m_strUnit.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}

		//KB  不等于默认值则保存
		if (fabs(p->m_K - 1) > 0.000001 || fabs(p->m_B - 0) > 0.000001) {
			//conf["k"] = p->m_K;
			//conf["b"] = p->m_B;
			key = yyjson_mut_strcpy(doc, "k");
			val = yyjson_mut_real(doc, p->m_K);
			yyjson_mut_obj_put(conf, key, val);

			key = yyjson_mut_strcpy(doc, "b");
			val = yyjson_mut_real(doc, p->m_B);
			yyjson_mut_obj_put(conf, key, val);
		}

		if (p->m_saveMode == DATA_SAVE_MODE::cyclic_onchange || p->m_saveMode == DATA_SAVE_MODE::onchange) {
			//conf["deadZone"] = m_deadZone;
			key = yyjson_mut_strcpy(doc, "deadZone");
			val = yyjson_mut_real(doc, m_deadZone);
			yyjson_mut_obj_put(conf, key, val);
		}

		////默认值 
		//if (p->m_defaultVal != nullptr) {
		//	conf["defaultVal"] = p->m_defaultVal;
		//}

		//报警限
		//json alarmLimit;
		yyjson_mut_val* rootAlarmLimit = yyjson_mut_obj(doc);
		if (m_alarmLimit.enableHigh) {
			key = yyjson_mut_strcpy(doc, "enableHigh");
			val = yyjson_mut_bool(doc, m_alarmLimit.enableHigh);
			yyjson_mut_obj_put(rootAlarmLimit, key, val);

			key = yyjson_mut_strcpy(doc, "high");
			val = yyjson_mut_real(doc, m_alarmLimit.high);
			yyjson_mut_obj_put(rootAlarmLimit, key, val);
		}
		if (m_alarmLimit.enableLow) {
			key = yyjson_mut_strcpy(doc, "enableLow");
			val = yyjson_mut_bool(doc, m_alarmLimit.enableLow);
			yyjson_mut_obj_put(rootAlarmLimit, key, val);

			key = yyjson_mut_strcpy(doc, "low");
			val = yyjson_mut_real(doc, m_alarmLimit.low);
			yyjson_mut_obj_put(rootAlarmLimit, key, val);
		}
		if (m_alarmLimit.enableHigh || m_alarmLimit.enableLow) {
			key = yyjson_mut_strcpy(doc, "alarmLimit");
			yyjson_mut_obj_put(conf, key, rootAlarmLimit);
		}


		//有效值范围
		if (m_validRange.enable) {
			yyjson_mut_val* rootValidRange = yyjson_mut_obj(doc);

			key = yyjson_mut_strcpy(doc, "enable");
			val = yyjson_mut_bool(doc, m_validRange.enable);
			yyjson_mut_obj_put(rootValidRange, key, val);

			key = yyjson_mut_strcpy(doc, "min");
			val = yyjson_mut_real(doc, m_validRange.min);
			yyjson_mut_obj_put(rootValidRange, key, val);

			key = yyjson_mut_strcpy(doc, "max");
			val = yyjson_mut_real(doc, m_validRange.max);
			yyjson_mut_obj_put(rootValidRange, key, val);

			key = yyjson_mut_strcpy(doc, "validRange");
			yyjson_mut_obj_put(conf, key, rootValidRange);
		}

		//有效小数位
		if (m_decimalDigits >= 0) {
			key = yyjson_mut_strcpy(doc, "decimalDigits");
			val = yyjson_mut_int(doc, m_decimalDigits);
			yyjson_mut_obj_put(conf, key, val);
		}

		if (p->m_valType == VAL_TYPE::video) {
			key = yyjson_mut_strcpy(doc, "mediaSrcType");
			val = yyjson_mut_strcpy(doc, m_mediaSrcType.c_str());
			yyjson_mut_obj_put(conf, key, val);

			key = yyjson_mut_strcpy(doc, "mediaUrl");
			val = yyjson_mut_strcpy(doc, m_mediaUrl.c_str());
			yyjson_mut_obj_put(conf, key, val);

			if (m_mediaSrcType == "ezviz") {
				key = yyjson_mut_strcpy(doc, "serialNo");
				val = yyjson_mut_strcpy(doc, m_serialNo.c_str());
				yyjson_mut_obj_put(conf, key, val);

				key = yyjson_mut_strcpy(doc, "appKey");
				val = yyjson_mut_strcpy(doc, m_appKey.c_str());
				yyjson_mut_obj_put(conf, key, val);

				key = yyjson_mut_strcpy(doc, "secret");
				val = yyjson_mut_strcpy(doc, m_secret.c_str());
				yyjson_mut_obj_put(conf, key, val);
			}

			key = yyjson_mut_strcpy(doc, "srcStreamFetch");
			val = yyjson_mut_strcpy(doc, m_srcStreamFetch.c_str());
			yyjson_mut_obj_put(conf, key, val);

			if (m_bServeStream) {
				key = yyjson_mut_strcpy(doc, "serveStream");
				val = yyjson_mut_bool(doc, m_bServeStream);
				yyjson_mut_obj_put(conf, key, val);
			}
		}

		//枚举值
		if (m_valType == VAL_TYPE::integer) {
			if (m_isEnum) {
				//conf["isEnum"] = true;
				key = yyjson_mut_strcpy(doc, "isEnum");
				val = yyjson_mut_true(doc);
				yyjson_mut_obj_put(conf, key, val);

				if (mapEnumVal.size() > 0) {
					//json jEnum = json::array();
					yyjson_mut_val* rootEnum = yyjson_mut_arr(doc);

					for (auto& i : mapEnumVal) {
						//json item = json::array();
						yyjson_mut_val* rootItem = yyjson_mut_arr(doc);

						//item.push_back(i.first);
						//item.push_back(i.second);
						//jEnum.push_back(item);
						val = yyjson_mut_int(doc, i.first);
						yyjson_mut_arr_append(rootItem, val);
						val = yyjson_mut_strcpy(doc, i.second.c_str());
						yyjson_mut_arr_append(rootItem, val);
						yyjson_mut_arr_append(rootEnum, rootItem);
					}
					//conf["enum"] = jEnum;
					key = yyjson_mut_strcpy(doc, "enum");
					yyjson_mut_obj_put(conf, key, rootEnum);
				}
			}
			else {
				//conf["isEnum"] = false;
				key = yyjson_mut_strcpy(doc, "isEnum");
				val = yyjson_mut_false(doc);
				yyjson_mut_obj_put(conf, key, val);
			}
		}

		//降采样
		//if (m_bDownSample) {
		//	conf["downSample"] = m_bDownSample;
		//	conf["downSampleInterval"] = m_downSampleInterval;
		//}


		if (m_expression != "") {
			//conf["expression"] = m_expression;
			key = yyjson_mut_strcpy(doc, "expression");
			val = yyjson_mut_strcpy(doc, m_expression.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
	}

	if (q.getStatus || q.getVal) {
		yyjson_mut_val* key = yyjson_mut_strcpy(doc, "val");
		string sVal = m_curVal;
		yyjson_doc* read_doc = yyjson_read_opts((char*)sVal.c_str(), sVal.size(), 0, NULL, NULL);
		yyjson_mut_val* val;
		if (read_doc) {
			yyjson_val* read_root = yyjson_doc_get_root(read_doc);
			yyjson_mut_val* val = yyjson_val_mut_copy(doc, read_root);
			yyjson_doc_free(read_doc);
			yyjson_mut_obj_put(conf, key, val);
		}


		if (timeopt::isValidTime(m_stDataLastUpdate)) {
			key = yyjson_mut_strcpy(doc, "time");
			val = yyjson_mut_strcpy(doc, timeopt::st2str(m_stDataLastUpdate).c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
		else {
			key = yyjson_mut_strcpy(doc, "time");
			val = yyjson_mut_strcpy(doc, "-");
			yyjson_mut_obj_put(conf, key, val);
		}
	}

	if (q.getStatusDetail) {
		if (timeopt::isValidTime(m_lastSaveTime)) {
			//conf["lastSaveTime"] = timeopt::st2str(m_lastSaveTime);
			yyjson_mut_val* key = yyjson_mut_strcpy(doc, "lastSaveTime");
			yyjson_mut_val* val = yyjson_mut_strcpy(doc, timeopt::st2str(m_lastSaveTime).c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
		else {
			//conf["lastSaveTime"] = "-";
			yyjson_mut_val* key = yyjson_mut_strcpy(doc, "lastSaveTime");
			yyjson_mut_val* val = yyjson_mut_strcpy(doc, "-");
			yyjson_mut_obj_put(conf, key, val);
		}

		//conf["lastSaveInterval"] = m_lastSaveInterval;
		yyjson_mut_val* key = yyjson_mut_strcpy(doc, "lastSaveInterval");
		yyjson_mut_val* val = yyjson_mut_int(doc, m_lastSaveInterval);
		yyjson_mut_obj_put(conf, key, val);
	}

	if (q.getStatus) {
		if (m_valType == VAL_TYPE::video) {
			//conf["online"] = m_bOnline;
			yyjson_mut_val* key = yyjson_mut_strcpy(doc, "online");
			yyjson_mut_val* val = yyjson_mut_bool(doc, m_bOnline);
			yyjson_mut_obj_put(conf, key, val);
		}

		if (hasOnlineStatus()) {
			//conf["online"] = m_bOnline;
			yyjson_mut_val* key = yyjson_mut_strcpy(doc, "online");
			yyjson_mut_val* val = yyjson_mut_bool(doc, m_bOnline);
			yyjson_mut_obj_put(conf, key, val);
		}
	}

	if (q.getStatusDesc || q.getValDesc) {
		//conf["valDesc"] = getValDesc(q.getUnit);
		yyjson_mut_val* key = yyjson_mut_strcpy(doc, "valDesc");
		yyjson_mut_val* val = yyjson_mut_strcpy(doc, getValDesc(q.getUnit).c_str());
		yyjson_mut_obj_put(conf, key, val);
	}

	return true;
}

string MP::getValDesc(json& jVal,bool getUnit) {
	string valDesc;
	if (jVal.is_number_float()) {
		if (m_decimalDigits >= 0) {
			string formatter = "%." + str::fromInt(m_decimalDigits) + "f";
			valDesc = str::format(formatter.c_str(), jVal.get<float>());
		}
		else {
			valDesc = jVal.dump();
		}
	}
	else if (jVal.is_number_integer()) {
		if (m_isEnum) {
			int iVal = jVal.get<int>();
			if (mapEnumVal.find(iVal) != mapEnumVal.end()) {
				valDesc = mapEnumVal[iVal];
			}
			else {
				valDesc = jVal.dump();
			}
		}
		else
			valDesc = jVal.dump();
	}
	else if (jVal.is_boolean()) {
		//根据监控点名称自动生成值描述
		if (jVal != nullptr) {
			if (m_name.find("开关") != string::npos) {
				valDesc = jVal.get<bool>() ? "开" : "关";
			}
			else if (m_name.find("静音") != string::npos) {
				valDesc = jVal.get<bool>() ? "是" : "否";
			}
			else {
				valDesc = jVal.get<bool>() ? "开" : "关";
			}
		}
		else {
			valDesc = "-";
		}
	}
	else if (jVal.is_string()) {
		valDesc = jVal.get<string>();
	}
	else {
		valDesc = "-";
	}
	if (getUnit) {
		valDesc += m_strUnit;
	}
	return valDesc;
}


string MP::getValDesc(bool getUnit) {
	json temp = json::parse(m_curVal);
	return getValDesc(temp, getUnit);
}

bool MP::loadTreeStatus(OBJ* pSrc)
{
	string tag = getTag();
	MP* pSrcMp = pSrc->GetMPByTag(tag,"zh");
	if (pSrcMp) {
		//常量类型无需加载状态
		if (pSrcMp->m_ioType != "c") {
			loadObjStatus(pSrcMp);
		}
	}
	else
		return false;
	return true;
}

bool MP::loadObjStatus(OBJ* pSrcObj)
{
	OBJ::loadObjStatus(pSrcObj);

	MP* pSrcMp = (MP*)pSrcObj;
	m_lastVal = pSrcMp->m_lastVal;
	if (pSrcMp->hasValue()) {
		m_curVal = pSrcMp->m_curVal;
	}
	m_mpStatus = pSrcMp->m_mpStatus;
	return true;
}

void MP::calcAlarm()
{
	if (JSON_STR::is_num(m_curVal))
	{
		double dbCurVal = JSON_STR::get_num(m_curVal);
		//计算报警
		if (m_alarmLimit.enableHigh)
		{
			if (dbCurVal > m_alarmLimit.high)
			{
				ALARM_INFO ai;
				ai.tag = getTag();
				ai.type = ALARM_TYPE::overHighLimit;
				ai.level = ALARM_LEVEL::alarm;
				ai.desc = str::format("报警值%f,上限值%f", dbCurVal, m_alarmLimit.high);
				almSrv.Update(ai);
			}
			else
			{
				ALARM_INFO ai;
				ai.tag = getTag();
				ai.type = ALARM_TYPE::overHighLimit;
				ai.level = ALARM_LEVEL::normal;
				almSrv.Update(ai);
			}
		}
		if (m_alarmLimit.enableLow)
		{
			if (dbCurVal < m_alarmLimit.low)
			{
				ALARM_INFO ai;
				ai.tag = getTag();
				ai.type = ALARM_TYPE::overLowLimit;
				ai.level = ALARM_LEVEL::alarm;
				ai.desc = str::format("报警值%f,下限值%f", dbCurVal, m_alarmLimit.low);
				almSrv.Update(ai);
			}
			else
			{
				ALARM_INFO ai;
				ai.tag = getTag();
				ai.type = ALARM_TYPE::overLowLimit;
				ai.level = ALARM_LEVEL::normal;
				almSrv.Update(ai);
			}
		}
	}
}



void MP::input(json& jVal, json* dataFile, TIME* dataTime)
{
	if (!m_bEnableIO)
		return;

	m_bOnline = true;

	TIME t;
	if (dataTime == NULL)
	{
		timeopt::now(&t);
		dataTime = &t;
	}

	if (memcmp(&dataTime, &m_stDataLastUpdate, sizeof(TIME)) == 0) {
		string sLastVal = m_curVal;
		string newVal = jVal.dump();
		//特殊情况，数据更新时间没有改变，但是数据却改变了，可能发生在二次计算监控点利用脚本算出了一个错误数值
		//后续脚本更新，算出正确数值，但是由于时间都使用了计算脚本引用的监控点的时间，该监控点更新时间不改变的话
		//数值无法更新进入计算监测点
		if (sLastVal != newVal) { 
			timeopt::now(&t);
			dataTime = &t;
		}
		else
			return;
	}


	//数字类型进行kb处理和上下限处理
	if (jVal.is_number())
	{
		if (m_valType == "int") {
			int iVal = jVal.get<int>();
			m_orgVal = iVal;
			int iCurVal = (int) (iVal * m_K + m_B);
			jVal = iCurVal;
			if (m_validRange.enable)
			{
				if (iCurVal < m_validRange.min || iCurVal > m_validRange.max)
				{
					jVal = nullptr;
				}
			}
		}
		else {
			double dbVal = jVal.get<double>();
			m_orgVal = dbVal;
			//dbVal*m_k可能会把一些超过double精度的非精确字段移到前面,而产生误差.默认保留10位小数精度
			double dbCurVal = dbVal * m_K + m_B; // linear calibration using K and B 
			string sVal;
			if (m_decimalDigits >= 0)
			{
				string formatter = "%." + str::fromInt(m_decimalDigits) + "f";
				sVal = str::format(formatter.c_str(), dbCurVal);
			}
			else
				sVal = str::format("%.10f", dbCurVal);
			dbCurVal = atof(sVal.c_str());
			jVal = dbCurVal;

			//关键机制。当采集到的数据是错误的（不在有效范围内），将当前值置为fasle
			//否则如果将错误值进行二次计算或者统计分析，会得到很多错误的结果
			//程序应当允许在某些值为null时，依然能够输出一些二次计算或者统计分析的结果
			if (m_validRange.enable)
			{
				if (dbCurVal < m_validRange.min || dbCurVal > m_validRange.max)
				{
					jVal = nullptr;
				}
			}
		}
	}

	updateVal(jVal, dataFile,dataTime);
}

//监控点组不算
OBJ* MP::getParentMo()
{
	OBJ* p = m_pParentMO;
	while (p) {
		if (p->m_level == MO_TYPE::mo || p->isCustomMo()) {
			return p;
		}

		p = p->m_pParentMO;
	}
	return nullptr;
}

void MP::updateVal(json& jVal, json* fileData, TIME* pDataTime)
{
	TIME t;
	if (pDataTime == NULL)
	{
		timeopt::now(&t);
		pDataTime = &t;
	}

	if (memcmp(pDataTime, &m_stDataLastUpdate, sizeof(TIME)) == 0)
		return;

	m_dbFileLock.lock();

	m_stDataLastUpdate = *pDataTime;
	OBJ* pParentMo = getParentMo();
	if (pParentMo)
		pParentMo->m_stDataLastUpdate = *pDataTime;

	//save to rt memory
	m_lastVal = json::parse(m_curVal);
	m_curVal = jVal.dump();

	if(fileData != nullptr)
		m_curFileData = *fileData;

	m_dbFileLock.unlock();

	calcAlarm();
	if (this->m_valType == "json")
	{
		jVal["type"] = this->m_valType;
		jVal["mpType"] = this->m_mpType;
	}


	//特殊的属性监测点
	if (m_name == "经度")
	{
		m_pParentMO->m_longitudeDyn = JSON_STR::is_num(m_curVal) ? JSON_STR::get_num(m_curVal) : m_pParentMO->m_longitudeDyn;
	}
	else if (m_name == "纬度")
	{
		m_pParentMO->m_latitudeDyn = JSON_STR::is_num(m_curVal) ? JSON_STR::get_num(m_curVal) : m_pParentMO->m_latitudeDyn;
	}


	m_pParentMO->m_bOnline = true;


	if (m_alarmMp && JSON_STR::is_bool(m_curVal)) //是一个报警监控点，更新报警
	{
		ALARM_INFO ai;
		ai.type = m_name;
		if (JSON_STR::get_bool(m_curVal))
			ai.level = ALARM_LEVEL::alarm;
		else
			ai.level = ALARM_LEVEL::normal;
		ai.tag = getTag();
		ai.typeLabel = m_name;
		almSrv.Update(ai);
	}
}

bool MP::needSaveToDB()
{
	bool bNeedSave = false;
	if (!JSON_STR::is_null(m_curVal))
	{
		int timePassLastSave = timeopt::CalcTimeDiffSecond(m_stDataLastUpdate, m_lastSaveTime);
		//save to db
		//这里不要使用 elseif 如果是 cyclic|onchange的存储模式， 是否周期到和是否值变化都要判断
		if (m_saveMode.find("cyclic") != string::npos)
		{
			int timespan = getSaveInterval();
			if (timePassLastSave > timespan)
			{
				bNeedSave = true;
			}
		}
		
		if (m_saveMode.find("onchange") != string::npos)
		{
			if (JSON_STR::is_num(m_curVal) && m_lastVal.is_number()) {
				double last = m_lastVal.get<double>();
				double cur = JSON_STR::get_num(m_curVal);
				double diff = fabs(last - cur);
				if (diff > m_deadZone && diff > 0.00001) {
					bNeedSave = true;
				}
			}
			else if (JSON_STR::is_bool(m_curVal) && m_lastVal.is_boolean()) {
				bool last = m_lastVal.get<bool>();
				bool cur = JSON_STR::get_bool(m_curVal);
				if (last != cur) {
					bNeedSave = true;
				}
			}
		}
		
		if (m_saveMode == "always")
		{
			bNeedSave = true;
		}

		if (bNeedSave) {
			m_lastSaveInterval = timePassLastSave;
		}
	}

	if (m_curFileData != nullptr)
	{
		bNeedSave = true;
	}

	return bNeedSave;
}


void MP::saveToDB() {
	m_dbFileLock.lock();
	timeopt::now(&m_lastSaveTime);
	json jDE;
	jDE["time"] = timeopt::st2strWithMilli(m_stDataLastUpdate);

	string sFileData;
	if (!JSON_STR::is_null(m_curVal))
		jDE["val"] = json::parse(m_curVal);
	if (m_curFileData != nullptr)
	{
		sFileData = m_curFileData.dump();
		jDE["file"] = m_curFileData;
	}
	if (m_curValAttr != nullptr) {
		for (auto& item : m_curValAttr.items()) {
			jDE[item.key()] = item.value();
		}
	}
	string sDe = jDE.dump();

	DB_TIME dbt;
	dbt.fromStr(m_stDataLastUpdate.toStr(true));
	db.Insert(getTag().c_str(), sDe, &dbt);
	m_dbFileLock.unlock();
}

//该接口保证rlt或者err一定会有一个值返回
void MP::output(json jVal, json& rlt, json& err,bool sync)
{
	//方案1：当前值变为nullptr,直到采集到新的数据值,才能确认当前值
	//m_curVal = nullptr;
	//方案2. 值不变。采集到新的数据值或者收到输出反馈，才变成新的值
	//do nothing
	string tag = getTag();


	ioDev* pDev = ioSrv.getOwnerChildTdsDev(tag);

	if (pDev) {
		//转换为在子服务中的位号
		string childTdsTag = pDev->m_strTagBind;
		tag = TAG::trimRoot(tag, childTdsTag);


		json params;
		params["tag"] = tag;
		params["val"] = jVal;
		json childRlt, childErr;
		LOG("[warn][数据输出  ]请求子服务，tag=%s,子服务名称:%s", tag.c_str(), childTdsTag.c_str());
		pDev->call("output", params, nullptr, childRlt, childErr, sync);
		if (childRlt != nullptr) {
			rlt = params;
			m_curVal = jVal.dump();
			timeopt::now(&m_stDataLastUpdate);
			LOG("[warn][数据输出  ]请求子服务 成功，tag=%s,子服务名称:%s,返回:%s", tag.c_str(), childTdsTag.c_str(),rlt.dump().c_str());
		}
		if(childErr != nullptr) {
			err = childErr;
			LOG("[warn][数据输出  ]请求子服务 失败，tag=%s,子服务名称:%s,返回:%s", tag.c_str(), childTdsTag.c_str(), err.dump().c_str());
		}
	}
	else {
		ioChannel* pC = ioSrv.getChanByTag(tag);
		if (pC)
		{
			LOG("[warn][数据输出  ]发送请求;位号:%s,值:%s,通道:%s,等待响应:%d", getTag().c_str(), jVal.dump().c_str(), pC->getIOAddrStr().c_str(),sync?1:0);
			pC->output(jVal, rlt, err, sync);
			T_ASSERT(rlt != nullptr || err != nullptr);
			if (sync) {
				if (rlt != nullptr) {
					LOG("[warn][数据输出  ]输出成功,位号:%s,值:%s,通道:%s", getTag().c_str(), jVal.dump().c_str(), pC->getIOAddrStr().c_str());
				}
				if(err!=nullptr)
				{
					LOG("[warn][数据输出  ]输出失败,位号:%s,值:%s,通道:%s,失败信息:%s", getTag().c_str(), jVal.dump().c_str(), pC->getIOAddrStr().c_str(),err.dump().c_str());
				}
			}
		}
		else
		{
			err = makeRPCError(RPC_ERROR_CODE::MO_outputFail, "未找到绑定的通道");
		}
	}
}


bool MP::IsCurValValid()
{
	return !JSON_STR::is_null(m_curVal);
}

string MP::getMpTypeLabel()
{
	string typeLabel;
	if (m_valType == VAL_TYPE::boolean)
	{
		typeLabel = m_name;
	}
	else if (m_valType == VAL_TYPE::Float)
	{
		typeLabel = m_name;
	}
	else if (m_valType == VAL_TYPE::json)
	{
		typeLabel = m_mpType;
	}
	else
	{
		typeLabel = getValTypeLabel(m_valType);
	}

	return typeLabel;
}



string MP::getMpType()
{
	string mpType;
	// as a convention , a real type MP's name is named by data type.
	if (m_valType == VAL_TYPE::boolean)
	{
		mpType = m_name;
	}
	else if (m_valType == VAL_TYPE::Float)
	{
		mpType = m_name;
	}
	else if (m_valType == VAL_TYPE::video)
	{
		mpType = "视频";
	}
	else if (m_valType == VAL_TYPE::json)
	{
		mpType = m_mpType;
	}
	else
	{
		mpType = "未知类型";
	}

	return mpType;
}

int MP::getSaveInterval()
{
	int si = m_saveInterval.hour * 60 * 3600 + m_saveInterval.minute * 60 + m_saveInterval.second;
	return si;
}

bool MP::getValByEnumVal(string enumVal,int& val)
{
	for (auto& i : mapEnumVal) {
		if (i.second == enumVal) {
			val = i.first;
			return true;
		}
	}
	return false;
}

bool MP::hasValue()
{
	return m_stDataLastUpdate.isValid();
}

void MP::stopStreamPull(string tag) {
	string key = "__defaultVhost__/stream/" + tag;
	string sPort = tds->conf->getStr("httpMediaPort", "669");
	string streamServerUrl = "127.0.0.1";
	string zlmSecret = tds->conf->getStr("zlmSecret", "Tds-666666");
	string uri = "/index/api/delStreamProxy";

	string ip = streamServerUrl;
	string port = sPort;
	string path = uri 
		+ "?key=" + mg_get_url_encode(key) 
		+ "&secret=" + mg_get_url_encode(zlmSecret);
	string url = "http://" + ip + ":" + port + path;

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		mg_printf(connect,
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			path.c_str(), ip.c_str()
		);

		TIME tStart = timeopt::now();
		while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
			mg_mgr_poll(&mgr, 100);
		}
	}

	mg_mgr_free(&mgr);

	LOG("[ZLMediaServer]Rest Api,Get %s,proxyKey=%s", url.c_str(), key.c_str());
	if (data.status != 0) {
		LOG("[ZLMediaServer] Status:%d,Response Body:%s", data.status, data.body.c_str());
	}
	else {
		LOG("[error]zlm stream server 未响应,%s", uri.c_str());
	}
}

bool MP::startStreamPull() {
	string tag = getTag();
	tag = charCodec::urlEncode(tag);
	
	string sIP = tds->conf->getStr("streamServerIP", "127.0.0.1");
	string sPort = tds->conf->getStr("httpMediaPort", "669");
	string streamServerUrl = sIP + ":" + sPort;
	string app = "stream";
	string zlmSecret = tds->conf->getStr("zlmSecret", "Tds-666666");
	string uri = "/index/api/addStreamProxy";

	string ip = sIP;
	string port = sPort;
	string path = uri 
		+ "?vhost=" + mg_get_url_encode("__defaultVhost__")
		+ "&app=" + mg_get_url_encode(app)
		+ "&stream=" + mg_get_url_encode(tag)
		+ "&url=" + mg_get_url_encode(m_mediaUrl)
		+ "&secret=" + mg_get_url_encode(zlmSecret)
		+ "&enable_hls=0"
		+ "&enable_ts=0"
		+ "&enable_mp4=0";
	string url = "http://" + ip + ":" + port + path;

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		mg_printf(connect,
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			path.c_str(), ip.c_str()
		);

		TIME tStart = timeopt::now();
		while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
			mg_mgr_poll(&mgr, 100);
		}
	}

	mg_mgr_free(&mgr);

	LOG("[ZLMediaServer]Rest Api,Get %s,app=%s,stream=%s,媒体源=%s", (streamServerUrl + uri).c_str(), app.c_str(), tag.c_str(), m_mediaUrl.c_str());
	if (data.status != 0) {
		LOG("[ZLMediaServer] Status:%d,Response Body:%s", data.status, data.body.c_str());

		json jResp = json::parse(data.body);
		json jData = jResp["data"];
		if (jData != nullptr && jData["key"] != nullptr) {
			m_mpStatus.m_pullingSrcUrl = m_mediaUrl;
			return true;
		}
	}
	else {
		LOG("[error]zlm stream server 未响应,%s", uri.c_str());
	}

	return false;
}

bool MP::stopStreamPush() {
	string tag = getTag();
	tag = charCodec::urlEncode(tag);
	
	string sIP = tds->conf->getStr("streamServerIP", "127.0.0.1");
	string sPort = tds->conf->getStr("httpMediaPort", "669");
	string zlmSecret = tds->conf->getStr("zlmSecret", "Tds-666666");
	string streamServerUrl = sIP + ":" + sPort;
	string uri = "/index/api/delStreamPusherProxy";

	string ip = sIP;
	string port = sPort;
	string path = uri
		+ "?key=" + mg_get_url_encode(m_strPusherProxyKey)
		+ "&secret=" + mg_get_url_encode(zlmSecret);
	string url = "http://" + ip + ":" + port + path;

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		mg_printf(connect,
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			path.c_str(), ip.c_str()
		);

		TIME tStart = timeopt::now();
		while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
			mg_mgr_poll(&mgr, 100);
		}
	}

	mg_mgr_free(&mgr);

	LOG("[ZLMediaServer]Rest Api,Get %s,key=%s", (streamServerUrl + uri).c_str(), m_strPusherProxyKey.c_str());
	if (data.status != 0) {
		LOG("[ZLMediaServer] Status:%d,Response Body:%s", data.status, data.body.c_str());
		return true;
	}
	else {
		LOG("[error]zlm stream server 未响应,%s", uri.c_str());
	}

	return false;
}

bool MP::startStreamPush(string desUrl) {
	string tag = getTag();
	tag = charCodec::urlEncode(tag);
	
	string sIP = tds->conf->getStr("streamServerIP", "127.0.0.1");
	string sPort = tds->conf->getStr("httpMediaPort", "669");
	string zlmSecret = tds->conf->getStr("zlmSecret", "Tds-666666");
	string streamServerUrl = sIP + ":" + sPort;
	string app = "stream";
	string uri = "/index/api/addStreamPusherProxy";

	string ip = sIP;
	string port = sPort;
	string path = uri
		+ "?vhost=" + mg_get_url_encode("__defaultVhost__")
		+ "&app=" + mg_get_url_encode(app)
		+ "&stream=" + mg_get_url_encode(tag)
		+ "&dst_url=" + mg_get_url_encode(desUrl)
		+ "&schema=" + mg_get_url_encode("rtsp")
		+ "&secret=" + mg_get_url_encode(zlmSecret);
	string url = "http://" + ip + ":" + port + path;

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		mg_printf(connect,
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			path.c_str(), ip.c_str()
		);

		TIME tStart = timeopt::now();
		while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
			mg_mgr_poll(&mgr, 100);
		}
	}

	mg_mgr_free(&mgr);

	LOG("[ZLMediaServer]Rest Api,Get %s,app=%s,stream=%s,推流目标=%s", (streamServerUrl + uri).c_str(), app.c_str(), tag.c_str(), desUrl.c_str());
	if (data.status != 0) {
		LOG("[ZLMediaServer] Status:%d,Response Body:%s", data.status, data.body.c_str());
		json jResp = json::parse(data.body);
		json jData = jResp["data"];
		if (jData != nullptr && jData["key"] != nullptr) {
			m_strPusherProxyKey = jData["key"];
			return true;
		}
	}
	else {
		LOG("[error]zlm stream server 未响应,%s", uri.c_str());
	}

	return false;
}