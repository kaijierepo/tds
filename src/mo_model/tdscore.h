#pragma once
#include <queue>
#include <string>
#include<map>
#include "common.hpp"
#include "tchar.h"
using namespace std;

#define Nan 0x7fc00000

/*name patern
functions variable              camel mode    getIODevices
enum string			         short dash     gw-local-serial
*/

namespace TDS {
	const std::map<string, string> VAL_TYPE_LABEL = {
	{std::map<string,string>::value_type("json","JSON")},
	{std::map<string,string>::value_type("float","浮点型")},
	{std::map<string,string>::value_type("int","整型")},
	{std::map<string,string>::value_type("bool","布尔型")},
	{std::map<string,string>::value_type("video","视频")},
	{std::map<string,string>::value_type("string","字符串型")}
	};

	const std::map<string, string> IO_TYPE_LABEL = {
	{std::map<string,string>::value_type("i","输入")},
	{std::map<string,string>::value_type("o","输出")},
	{std::map<string,string>::value_type("io","输入/输出")}
	};

	const std::map<string, string> IO_DEV_TYPE_LABEL = {
	{std::map<string,string>::value_type("modbus-rtu","modbus rtu")},
	{std::map<string,string>::value_type("local-serial","本地串口")},
	{std::map<string,string>::value_type("iq60-gateway","IQ60物联网网关")},
	{std::map<string,string>::value_type("tdsp-device","TDSP设备")},
	{std::map<string,string>::value_type("io-channel","IO通道")},
	};
//物理量类型。
//注意:不同的物理量可以有相同的单位，只是物理应用场景不同。
//根据物理量类型可以确定默认单位,例如V，但是可依然可以修改具体单位例如 kV
namespace PHYSICAL_TYPE {
	const string unknown = "unknown";
	const string temp = "temp";		//温度,°C (press alt,input 0176 =>°)
	const string humidity = "humidity";	//湿度,RH
	const string pressure = "pressure";	//压力,mPa
	const string current = "current";	//电流,mA
	const string voltage = "voltage";	//电压,V
	const string liquid_level = "liquid_level";		//液位,mm
	const string distance = "distance";	//距离,mm
	const string displacement = "displacement";//位移,mm
	const string resist = "resist";       //阻力,N
	const string power = "power";		//功率
	const string acceleration = "acceleration";		//加速度
	const string luminous_flux = "luminous_flux";		//光通量
	const string disolved_oxygen = "disolved_oxygen";//溶解氧
	const string power_factor = "power_factor"; //功率因数
	const string speed = "speed"; //速度 默认 km/h
	const string rotation_rate = "rotation_rate"; //转速 单位 rpm
	const string gas_concentration = "gas_concentration"; //气体浓度
};

const std::map<string, string> DEFALT_UNIT = {
	{std::map<string,string>::value_type("unknown","默认")},
	{std::map<string,string>::value_type("temp","°C")},
	{std::map<string,string>::value_type("humidity","RH")},
	{std::map<string,string>::value_type("pressure","mPa")},
	{std::map<string,string>::value_type("current","mA")},
	{std::map<string,string>::value_type("voltage","V")},
	{std::map<string,string>::value_type("liquid_level","mm")},
	{std::map<string,string>::value_type("distance","m")},
	{std::map<string,string>::value_type("displacement","mm")},
	{std::map<string,string>::value_type("resist","N")},	
	{std::map<string,string>::value_type("power","kW")},
	{std::map<string,string>::value_type("acceleration","g")},
	{std::map<string,string>::value_type("luminous_flux","lux")},
	{std::map<string,string>::value_type("disolved_oxygen","mg/L")},
	{std::map<string,string>::value_type("power_factor","")},
	{std::map<string,string>::value_type("speed","km/h")},
	{std::map<string,string>::value_type("rotation_rate","rpm")},
	{std::map<string,string>::value_type("gas_concentration","mg/m³")}
};


const std::map<string, string> PHYSICAL_TYPE_LABEL = {
	{std::map<string,string>::value_type("unknown","")},
	{std::map<string,string>::value_type("temp","")},
	{std::map<string,string>::value_type("humidity","湿度")},
	{std::map<string,string>::value_type("pressure","压力")},
	{std::map<string,string>::value_type("current","电流")},
	{std::map<string,string>::value_type("voltage","电压")},
	{std::map<string,string>::value_type("liquid_level","液位")},
	{std::map<string,string>::value_type("distance","距离")},
	{std::map<string,string>::value_type("displacement","位移")},
	{std::map<string,string>::value_type("resist","阻力")},
	{std::map<string,string>::value_type("power","功率")},
	{std::map<string,string>::value_type("acceleration","加速度")},
	{std::map<string,string>::value_type("luminous_flux","光通量")},
	{std::map<string,string>::value_type("disolved_oxygen","溶解氧")},
	{std::map<string,string>::value_type("power_factor","功率因数")},
	{std::map<string,string>::value_type("speed","速度")},
	{std::map<string,string>::value_type("rotation_rate","转速")}
};

namespace MO_TYPE {
	const string mo = "mo";
	const string mp = "mp";
	const string org = "org";
	const string mpgroup = "mpgroup";
	const string project = "project";
	const string amo = "amo";
	const string custom = "custom";
}

namespace MO_TYPE_LABEL {
	const string COMMON = "自定义";
	const string MP = "监测点";
	const string MP_GROUP = "监测点组";
	const string PROJECT = "工程";
	const string ACTIVE_OBJ = "活动监测点";
}

namespace TDS_ERROR {
	const string ok = "ok";
	const string error1 = "error1";
	const string error2 = "error2";
}

const std::map<string, string> TDS_ERROR_DESC = {
	{std::map<string,string>::value_type("error1","error1的详细解释")},
	{std::map<string,string>::value_type("error2","error2的详细解释")},
};

//应用层协议类型
namespace APP_LAYER_PROTO {
	const string UNKNOWN = "alp_unknown";
	const string HTTP= "alp_http";
	const string MODBUS_RTU= "alp_modbus_rtu";
	const string PROTOCOL_WEBSOCKET= "alp_websocket";
	const string TDSRPC= "alp_tdsrpc";
	const string PROTOCOL_FRAMING_PROTOCOL= "alp_framing_protocol";
	const string IQ60 = "alp_iq60";
	const string textEnd2LF = "textEnd2LF";  //以2个换行结尾的字符串
};

//the protocol used as a transportation layer (no command specified in this layer,only for data transfer)
namespace TRANSFER_LAYER_PROTO_TYPE
{
	const string TLT_UNKNOWN = "tlp_unknonw";
	const string TLT_NONE = "tlp_none"; //no transportation layer
	const string TLT_CAN_V1 = "tlp_can_v1"; //payload is self framing protocol
	const string TLT_CAN_V2 = "tlp_can_v2"; //payload use can head subpkt info for framing
	const string TLT_WEB_SOCKET = "tlp_websocket";
	const string TLT_HTTP = "tlp_http";
};

enum IO_GATEWAY_TYPE {
	GW_UNKNOWN,
	GW_CAN_TRANSPARENT,//send can frames to gateway
	GW_USER_PROTO_OVER_CAN,//user proto data is sperated into can frame payload by gateway
	GW_TRANSPARENT  //user proto data is sended transparently through gateway
};


//设备管理状态
namespace DEV_DISPOSITION_MODE {
	const string managed = "managed"; //后续重构为 inService 表示启用
	const string spare = "spare";
};

// unique identifier for an io device in a certain system
// any device in an IOT senario can be linked by device addr and gateway addr
struct ioAddress {
	string ioAddr; //设备地址
};


typedef enum recvPktType {
	RECV_PKT_UNKNOWN = -1, // 未知
	RECV_PKT_UNCONF = 0, // 未配置数据包
	RECV_PKT_ASYN, // 通知包
	RECV_PKT_SYNC // 响应包
}RecvPktType;


typedef enum EPACKET_DIR {//表示请求, 响应, 通知
	REQ = 0,
	RESP,
	NOTIFY
}ePktDir;

struct PKT_DATA {
	char* data;
	int len;
	string proto;
	recvPktType dealType;

	string m_strCmdName; //命令名称
	string m_strCmdContent;  //命令内容概要
	string m_strPktDetail; //命令包详细解析信息

	virtual bool pack() { return false; };
	virtual string GetCmdID() { return _T(""); };
	virtual bool UnPack(LPVOID pBuf, int iBufLen, bool bGetCmdInfo = false) { return true; };
	virtual string GetPktDesc() { return _T(""); }; //包详细描述信息
	virtual string GetCmdName() { return _T(""); };
	virtual bool UnPack() { return 0; };

	void setData(char* p, int l)
	{
		if (data)delete data;
		data = new char[l];
		memcpy(data, p, l);
		len = l;
	}
	PKT_DATA(char* p, int l)
	{
		data = NULL;
		setData(p, l);
	}
	PKT_DATA()
	{
		data = NULL;
		len = 0;
	}

	~PKT_DATA()
	{
		if (data)
			delete data;
	}


	PKT_DATA& operator=(const PKT_DATA& pd)
	{
		this->len = pd.len;
		this->proto = pd.proto;

		if (pd.len > 0)
		{
			if (this->data)
				delete this->data;
			this->data = new char[pd.len];
			memcpy(this->data, pd.data, pd.len);
		}
		else
		{
			this->data = NULL;
		}
		return *this;
	}
};


struct REQ_PARAM {
	int iRetryCount;
	int iWaitTime; //mm单位
	string strLogMsgWhenSend;
	string strCmdID; //命令的标识，用来判断响应和请求是否匹配
	REQ_PARAM();
};

//Can总线协议
//连续bit转换成字节,高位在前  ":"符号为取位域
//取位域时,先取低位,再取高位
struct NET_CAN_ORG  //size = 5 bytes
{
	char DLC : 4;
	char CAN : 4;
	char ID29_25 : 5;  //ID高位在前.是这个字节中的低5位
	char Invalid : 3;  //29个bit,4个字节中有3个bit无效    
	char ID24_17;
	char ID16_9;
	char ID8_1;
};

/*
比特序(bit order)
字节序是一个对象中的多个字节之间的顺序问题，比特序就是一个字节中的8个比特位(bit)之间的顺序问题。一般情况下系统的比特序和字节序是保持一致的。
一个字节由8个bit组成，这8个bit也存在如何排序的情况，跟字节序类似的有最高有效比特位、最低有效比特位。
比特序1 0 0 1 0 0 1 0在大端系统中最高有效比特位为1、最低有效比特位为0，字节的值为0x92。在小端系统中最高、最低有效比特位则相反为0、1，字节的值为0x49。
跟字节序类似，要想保持一个字节值不变那么就要使系统能正确的识别最高、最低有效比特位。
*/
struct NET_CAN_HEAD_V2  //size = 5 bytes
{
	char DLC : 4;     //DLC
	char r0 : 2;   //bit4-5
	char RTR : 1;  //bit6
	char IDE : 1;  //bit7    =1表示扩展帧  =0表示标准帧
	char Address1 : 1;
	char B : 1;       //广播   0：普通帧  1：广播帧
	char G : 1;       //优先级  0：高级  1：低级
	char MS : 1;      //M/S   0:自主帧 1：应答帧
	char DIR : 1;     //DIR   0:下发  1:上送
	char R0 : 3;      //头部预留3位置
	char Type : 3;    //Type   100，4 自主单帧   000，0 应答单帧  011，3，非结束多帧  010，2， 结束多帧
	char Address : 5; //地址
	char InxFrame;//Index of frame  从0开始编号
	char SumFrame;//Sum of frame
	NET_CAN_HEAD_V2()
	{
		r0 = 0;
		RTR = 0;
		IDE = 1;
		DLC = 0;
		Address1 = 0;
		B = 0;
		G = 0;
		MS = 0;
		DIR = 0x0;
		R0 = 0;
		Type = 0;
		Address = 0;
		SumFrame = 0x0;
		InxFrame = 0x0;
	}

};

struct CAN_PKT_V2 //size = 13字节
{
	NET_CAN_HEAD_V2 sHead;
	char arrData[8];
	CAN_PKT_V2()
	{
		ZeroMemory(arrData, sizeof(arrData));
	}
};
}
using namespace TDS;

