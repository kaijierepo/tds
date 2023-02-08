#pragma once

#include "json.hpp"
#include "ioDev.h"
using json = nlohmann::json;

namespace BYTE_ORDER {
	const string bigEndian = "be";
	const string littleEndian = "le";
	const string bigEndianByteSwap = "bebs";
	const string littleEndianByteSwap = "lebs";
}

namespace STORAGE_FMT {
	const string Int16 = "int16";
	const string UInt16 = "uint16";
	const string Int32 = "int32";
	const string UInt32 = "uint32";
	const string Int64 = "int64";
	const string Uint64 = "uint64";
	const string Float = "float";
	const string Double = "double";
	const string BCD16 = "bcd16";
	const string BCD32 = "bcd32";
}

inline int storageSize(string fmt) {
	if (fmt.find("16") != string::npos)return 2;
	else if (fmt.find("32") != string::npos)return 4;
	else if (fmt.find("64") != string::npos)return 8;
	else if (fmt.find("float") != string::npos)return 4;
	else if (fmt.find("double") != string::npos)return 8;
	else {
		return 0;
	}
}

class ioChannel : public ioDev
{
public:
	ioChannel();
	~ioChannel();

	string storageFmt2valType(string sFmt);

	bool loadConf(json& conf) override;
	bool toJson(json& conf, json opt = nullptr) override;
	bool getChanStatus(json& statusList) override;
	bool getChanVal(json& valList) override;
	bool match(string channelNo);

	virtual void input(json jVal, TIME* dataTime=NULL, bool bPic=false);
	virtual void input(json jVal,string& tagBind, TIME* dataTime = NULL, bool bPic = false);
	virtual void output(json jVal, json& rlt,json& err,bool sync = false); //sync指定为同步输出,该函数将阻塞

	json m_curVal;
	json m_curOrgVal; //未经过倍率转换的
	unsigned short m_regOffset;
	string m_regType; //modbus寄存器类型
	string m_fmt;
	string m_byteOrder;

	//线性校正
	double m_k;
	double m_b;

	string m_ioType;
	string m_ioTypeLabel;
	string m_valType;
	string m_valTypeLabel;

	TIME m_stLastUpdateTime;


	bool m_bDownSample;
	int m_iDownSampleInterval;
	TIME m_lastDownSampleTime;
	//数据流订阅会话
	mutex m_csStreamPuller;
	vector<shared_ptr<TDS_SESSION>> m_vecStreamPuller;
};
