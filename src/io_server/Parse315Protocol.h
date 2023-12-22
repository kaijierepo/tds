/*
315协议解析类
*/
//#include <string>
#include "common/common.h"

using namespace std;

#pragma once


/*
协议数据结构定义
*/

#pragma once

//#include <WinDef.h>
//#include <vector>

#define JHD_CONN_PORT			9999			//JHD系统默认端口
#define JHD_VEDIO_PORT			9998			//双端口模式下的视频端口

//与JHD系统的通信协议
#define PROTOCAL_CODE			0x02			//协议码
#define PROTOCAL_DATAVERSION	0x01			//数据版本

#define FRAME_HEADER_315		"qknet"		//帧头
#define FRAME_TYPE_HEARTBEAT	0x0F		//心跳帧
#define FRAME_TYPE_DATA			0x8F		//数据帧
#define FRAME_TYPE_JSON			0x3F		//json帧
#define FRAME_TAIL_315			0xFFFFFFFF	//帧尾

//扳动方向
enum class eMOVE_DIRECT : uint8_t
{
	unknow = -1,  //无效
	fix2invert = 0x00, //定到反
	invert2fix = 0x01, //反到定
	fix2fix = 0x02,
	invert2invert = 0x03,
	fix2fault = 0x04,
	invert2fault = 0x05,
	fault2fix = 0x06,
	fault2invert = 0x07,
	fault2fault = 0x08,
};

//scz 2022.11.09 新增 待补全
enum class E_315_PROTOCOL_TYPE : uint8_t
{
	CONFIG_FILE_0x13			= 0x13, //上传配置文件

	SIGNAL_1DQJ_INFO_0x22		= 0x22,	//1DQJ及道岔区段状态信息
	GAP_CONFIG_0x23				= 0x23,	//缺口配置
	YW_GAP_STATUS_0X24			= 0x24,	//油位及缺口采集设备状态信息
	OILPRESSURE_CURVE_0X25		= 0x25,	//油压曲线(315预留，卡斯科已在使用)
	GAP_VALUE_0x26				= 0x26,	//道岔缺口模拟量最新值
	ALARM_AND_IMG_0x27			= 0x27,	//报警/预警信息及缺口图像信息
	LAST_GAP_IMG_0x29			= 0x29,	//最新道岔缺口图像信息
	IMG_INFO_0x2A				= 0x2A,	//图像信息
	VEDIO_LIST_0x2E				= 0x2E,	//视频时间列表
	VEDIO_FILE_0x2F				= 0x2F,	//视频文件
	REAL_VIDEO_CTRL_0x30		= 0x30,	//实时视频流控制命令
	REAL_VIDEO_STREAM_0x31		= 0x31,	//实时码流			
	IMG_LIST_0x32				= 0x32,	//图像列表
	SKYLIGHT_TIME_0x33			= 0x33,	//天窗时间
	MOVE_CURRENT_CURVE_0x41		= 0X41,	//动作电流（与交控的接口）
	DCS_CURVE_0x42				= 0x42,	//电参数计算阻力命令
	POWER_LIST_0x43				= 0x43,	//阻力文件列表
	POWER_CONTENT_0x44			= 0x44,	//阻力文件信息
	MANUAL_JIAYOU_0x51			= 0x51,	//手动加油
	JIAYOU_RECORD_0x52			= 0x52, //加油记录
	OIL_BOX_VOLUME_0x53         = 0x53, //加油箱储油量
	DBJ_FBJ_0x60				= 0x60,	//下发定反表状态
	UN_RECOVER_ALARM_0x65		= 0x65,	//发送未确认的报警列表 (与众合的接口)
	GONGKUANG_REAL_VAL_0x81		= 0x81,	//工况参数实时值更新0x81[主动请求]
	GONGKUANG_INIT_VALUE_0x82	= 0x82,	//工况参数初始值校准
	GONGKUANG_ZD_CURVE_0x83		= 0x83, //工况振动曲线数据

	JSON_VIDEO_LIST             = 0xE1, //视频列表  给智能运维使用

	JDSP_0xF0					= 0xF0,	//JDSP子协议
	NEW_DATA_FILE_0xF1			= 0xF1,	//站机采集到了新的数据文件
	QUERY_POWER_FILE_0xF2		= 0xF2,	//查询指定时间段内阻力文件
	DOWNLOAD_DATA_FILE_0xF3		= 0xF3,	//下载数据文件
};
//scz 2023.5.18 增加E1下面二级命令 智能运维使用
enum class E_315_PROTOCOL_E1 : uint8_t
{
	VEDIO_LIST_0x2E = 0x2E,		//视频时间列表
	POWER_LIST_0x43 = 0x43,		//阻力文件列表
	POWER_CONTENT_0x44 = 0x44,	//阻力文件信息
};

enum class E_315_JSON_TYPE : uint8_t
{
	JSON_ARRAY = 0x01, 
	JSON_OBJECT = 0x02,
};
extern DWORD g_dw0x23ExCmdID;	//道岔缺口扩展配置命令ID

//命令码
#define CMD_CODE_GAPCFG			0x23			//缺口配置（315）
#define CMD_CODE_GAPVAL			0x26			//缺口值
#define CMD_CODE_ALARM_AND_IMG	0x27			//报警/预警信息及缺口图像信息
#define CMD_CODE_ACTION_INFO	0x28			//道岔动作后模拟量及缺口图像信息（卡斯科）
#define CMD_CODE_LASTGAPIMG		0x29			//道岔缺口最新图像
#define CMD_CODE_IMGLIST		0x32			//图像列表
#define CMD_CODE_IMGINFO		0x2A			//图像信息
#define CMD_CODE_VEDIOLIST		0x2E			//视频时间列表
#define CMD_CODE_VEDIOFILE		0x2F			//视频文件
#define CMD_CODE_1DQJINFO		0x22			//1DQJ及道岔区段状态信息
#define CMD_CODE_YYQX			0x25			//油压曲线(315预留，卡斯科已在使用)
#define CMD_CODE_YWINFO			0x24			//油位及缺口采集设备状态信息
#define CMD_CODE_PARAMETER		0x21			//参数设置与获取(卡斯科）
#define CMD_CODE_REALCTRL		0x30			//实时视频流控制命令
#define CMD_CODE_REALSTREAM		0x31			//实时码流
#define CMD_CODE_ELECCURVE		0x41			//室外电参数动作曲线
#define CMD_CODE_POWSTATIC      0x40            //静态阻力上送
#define CMD_CODE_DCSCURVE		0x42			//电参数计算阻力命令
#define CMD_CODE_POWERLIST		0x43			//阻力文件列表
#define CMD_CODE_POWERCONTENT	0x44			//阻力文件信息

#define CMD_CODE_JDSP			0xF0			//JDSP子协议
#define CMD_CODE_NEW_DATA_FILE  0xF1			//站机采集到了新的数据文件
#define CMD_CODE_QUERY_POWER_FILE  0xF2			//查询指定时间段内阻力文件
#define CMD_CODE_DOWNLOAD_DATA_FILE  0xF3		//下载数据文件
#define CMD_CODE_DBJFBJ			0x60			//定反表状态
#define CMD_CODE_ALARM			0x97			//报警/预警信息


//转辙机类型
#define	SWITCH_TYPE_ZD6			1
#define	SWITCH_TYPE_EJ			2
#define	SWITCH_TYPE_GK			3
#define SWITCH_TYPE_S700K		4
#define SWITCH_TYPE_ZDJ_9		5
#define SWITCH_TYPE_ZYJ7_I		6
#define SWITCH_TYPE_ZYJ7_II		7
#define SWITCH_TYPE_ZYJ9		8

//采集信息类型
#define SWITCH_INFO_GAP			1				//缺口值
#define	SWITCH_INFO_OFFSET		2				//偏移值
#define	SWITCH_INFO_STD			3				//标准值
#define	SWITCH_INFO_OIL			4				//油位
#define	SWITCH_INFO_TEMP		5				//温度
#define	SWITCH_INFO_HUM			6				//湿度
#define	SWITCH_INFO_OILPRE		7				//油压曲线
#define	SWITCH_INFO_VOL			8				//电压
#define	SWITCH_INFO_ELEC		9				//电流

//报警类型
#define ALARM_TYPE_QKYJ			1				//缺口预警及预警图像
#define ALARM_TYPE_QKBJ			2				//缺口报警及报警图像
#define ALARM_TYPE_QKSBGZ		3				//缺口采集设备故障，此时没有缺口值及缺口图像，左右偏移标志填无效(00),总包数填1，本包序号0，图像总长度0， 本帧图像长度0.
#define ALARM_TYPE_TXWFSB		4				//缺口图像无法识别报警
#define ALARM_TYPE_GCKLGD		5				//过车时框量过大报警及过车视频
#define ALARM_TYPE_ZZJSBGZ		6				//转辙机采集设备故障报警
#define ALARM_TYPE_WDBJ			7				//温度报警
#define ALARM_TYPE_SDBJ			8				//湿度报警
#define ALARM_TYPE_YWYJ			9				//油位预警（预留）
#define ALARM_TYPE_YWBJ			10				//油位报警（预留）
#define ALARM_TYPE_POWERYJ      11              //阻力预警
#define ALARM_TYPE_POWERBJ      12              //阻力告警
#define ALARM_TYPE_TEMPERATURE  77				//温度预警
#define ALARM_TYPE_HUMILITY		78				//湿度预警
#define ALARM_TYPE_QKYJHF		101				//缺口预警恢复及图像
#define ALARM_TYPE_QKBJHF		102				//缺口报警恢复及图像
#define ALARM_TYPE_QKSBGZHF		103				//缺口采集设备故障恢复及图像
#define ALARM_TYPE_TXWFSBHF		104				//缺口图像无法识别报警恢复及图像
#define ALARM_TYPE_ZZJSBGZHF	105				//转辙机采集设备故障报警恢复
#define ALARM_TYPE_GCKLGDHF		106				//过车时框量过大报警恢复及过车视频
#define ALARM_TYPE_WDBJHF		107				//温度报警恢复
#define ALARM_TYPE_SDBJHF		108				//温度报警恢复
#define ALARM_TYPE_YWYJHF		109				//油位预警恢复（预留）
#define ALARM_TYPE_YWBJHF		110				//油位报警恢复（预留）
#define ALARM_TYPE_POWERYJHF    111             //阻力预警恢复
#define ALARM_TYPE_POWERBJHF    112             //阻力告警恢复
#define ALARM_TYPE_TEMPERATUREHF  177			//温度预警恢复
#define ALARM_TYPE_HUMILITYHF	178				//湿度预警恢复


//定反位操作方向(0xFF 无效)
#define DIRECT_DW_TO_FW			0				//定到反
#define DIRECT_FW_TO_DW			1				//反到定
#define DIRECT_DW_TO_DW			2				//定到定
#define DIRECT_FW_TO_FW			3				//反到反
#define DIRECT_DW_TO_GZ			4				//定位到故障位
#define DIRECT_FW_TO_GZ			5				//反位到故障位
#define DIRECT_GZ_TO_DW			6				//故障到定位
#define DIRECT_GZ_TO_FW			7				//故障到反位
#define DIRECT_GZ_TO_GZ			8				//故障到故障位

//室外电参数曲线类型
//交流机型动作曲线
#define ELEC_CURVE_TYPE_DZXJNUAB	0x01		//动作线电压Uab
#define ELEC_CURVE_TYPE_DZXJNUBC	0x02		//动作线电压Ubc
#define ELEC_CURVE_TYPE_DZXJNUAC	0x03		//动作线电压Uac
#define ELEC_CURVE_TYPE_DZDLIA		0x04		//动作电流Ia
#define ELEC_CURVE_TYPE_DZDLIB		0x05		//动作电流Ib
#define ELEC_CURVE_TYPE_DZDLIC		0x06		//动作电流Ic
#define ELEC_CURVE_TYPE_GLYS		0x07		//功率因素
#define ELEC_CURVE_TYPE_ZYGGL		0x08		//总有功功率
#define ELEC_CURVE_TYPE_ZL			0x09		//阻力
//直流机型动作曲线
#define ELEC_CURVE_TYPE_DZDYDW1		0x11		//动作电压定位1
#define ELEC_CURVE_TYPE_DZDYFW1		0x12		//动作电压反位1
#define ELEC_CURVE_TYPE_DZDYDW2		0x13		//动作电压定位2
#define ELEC_CURVE_TYPE_DZDYFW2		0x14		//动作电压反位2
#define ELEC_CURVE_TYPE_DZDLDW1		0x15		//动作电流定位1
#define ELEC_CURVE_TYPE_DZDLFW1		0x16		//动作电流反位1
#define ELEC_CURVE_TYPE_DZDLDW2		0x17		//动作电流定位2
#define ELEC_CURVE_TYPE_DZDLFW2		0x18		//动作电流反位2


//视频播放插件
#define VEDIO_PLAYER_IP			"127.0.0.1"		//视频播放插件IP
#define VEDIO_PLAYER_PORT		9997			//视频播放插件端口

//视频播放插件通信协议命令
#define VEDIO_START_FILE		0x10			//开始播放历史视频文件
#define VEDIO_STOP_FILE			0x11			//结束播放历史视频文件
#define VEDIO_SET_TITLE			0x12			//请求设置视频显示标题
#define VEDIO_REAL_CTRL			0x13			//开始或结束实时视频流
#define VEDIO_REAL_STREAM		0x14			//播放实时视频流

//命令帧 应答帧 主动上送帧 回执帧等
enum FRAME_KIND {
	CMD = 0,
	RES,
	UP_SEND,
	BACK,
	GENERAL
};

#pragma pack(push, 1)

//未恢复告警列表 命令码：0x65
//请求
typedef struct
{
	BYTE cmdid;		//命令码：0x65
	WORD sid;		//转辙机ID
	BYTE res[3];	//预留：填充3字节0xFF
}StAlarmListReq;

//0x65 文件记录 结构体
struct S_315_ALARM_INFO
{
	int16_t zzjid;		//转辙机编号：0xFFFF：表示所有转辙机，非0xFFFF：表示指定转辙机
	uint32_t time;		//报警时间
	int8_t fixinvert;	//定反位：0 定位， 1反位，-1 无效
	int8_t leftright;	//左右偏标志：0无效，1左偏（负），2右偏（正）
	int8_t alarm;		//报警类型
	int16_t shift;		//偏移值
	int16_t gap;		//缺口值
	int16_t standard;	//标准值
	int8_t acqtype;		//采集类型：0 周期、1 扳动、2过车
	uint32_t reserve;	//预留：填充4字节0xFF
};

typedef struct tagJSON3FINFO
{
	uint32_t t_len;
	E_315_PROTOCOL_E1 cmd; //命令码
	uint16_t packno;	//包序号
	uint32_t r;
	E_315_JSON_TYPE json_type;//帧内容类型
	uint32_t json_len;  //帧内容长度
	char data[0];
}E_315_JSON_INFO;

//应答
typedef struct
{
	BYTE cmdid;				//命令码：0x63
	WORD cnt;				//记录数
	LPVOID lprecord;		//文件记录
}StAlarmListRes;

//应答
typedef struct
{
	BYTE cmdid;				//命令码：0x81
	WORD zzjid;			//转辙机编号
	uint32_t time;			//采集时间
	WORD valType;			//值类型
	short value;			//值
	uint32_t reserve;	//预留：填充4字节0xFF
}StWorkingConditionValRes;

//应答
typedef struct
{
	BYTE cmdid;				//命令码：0x82
	WORD zzjid;				//转辙机编号
	uint32_t time;			//时间
	BYTE actionType;		//动作类型
	BYTE valType;			//值类型
	int valueK;				//值
	int valueB;				//值
	BYTE result;			//返回值 0成功 1失败
	uint32_t reserve;	//预留：填充4字节0xFF
}StOpWorkingConditionRes;

//应答
typedef struct
{
	BYTE cmdid;				//命令码：0x83
	WORD zzjid;			//转辙机编号
	uint32_t time;			//采集时间
	BYTE curveType;			//曲线类型(1B)
	BYTE acpfreq;			//采样间隔（1B，如:10ms）
	WORD curveLen;			//本条曲线数据长度(2B)
}StVibrationCurveRes;

//帧总体结构
struct StFrame
{
	BYTE fheader[5];	//帧头：字符型 5字节“qknet”(71 6B 6E 65 74)
	BYTE protocode;		//协议码：此版本协议码为0x02H.
	BYTE dataversion;	//数据版本：此版本号由道岔缺口厂家提供给监测厂家。若监测接收到的数据版本类型和厂家提供的不一致，则不处理接收的数据。
	BYTE ftype;			//帧类型：0x0FH表示心跳帧，0x8FH表示数据帧。
	DWORD datalen;		//数据长度
	LPVOID lpdata;		//数据
	DWORD ftail;		//帧尾：填充4个字节的0xFF。

	//新增一些维护型字段
	FRAME_KIND e_frmKind;
	StFrame()
	{
		memset(this, 0, sizeof(StFrame));
		memcpy_s(fheader, 5, FRAME_HEADER_315, 5);
		protocode = PROTOCAL_CODE;
		dataversion = PROTOCAL_DATAVERSION;
		ftail = FRAME_TAIL_315;

		e_frmKind = GENERAL;
	}
};


//帧内容数据结构

//心跳数据
typedef struct
{
	DWORD hbtime;		//时间: 4B的unix时间用于时钟同步
	BYTE filldata[3];	//填充3个字节的0xFF
}StHeartBeat315;


//基础命令数据结构，所有数据命令都包含该格式，解析数据时可用此结构判断命令类型，然后再用相应数据结构解析
typedef struct
{
	BYTE cmdid;			//请求命令的命令码
}StDataBasic;


//JDSP 命令吗 0xf0
struct StJDSP {
	BYTE cmdid;
	BYTE res[4];
	string jdsp;
};


//道岔缺口配置信息（315） 命令码：0x23
typedef struct
{
	BYTE cmdid;			//命令码：0x23
	WORD cfgcnt;		//配置数
	LPVOID lpcfg;		//转辙机配置i
}StGapCfgRes;

//转辙机配置
typedef struct
{
	BYTE nlen;		//转辙机名称长度
	LPSTR lpname;	//转辙机名称
	BYTE stype;		//转辙机类型
	WORD sid;		//转辙机ID
	WORD cnt;		//采集信息个数
	LPWORD lpinfo;	//采集信息类型
}StSwitchCfg;
//道岔缺口配置信息 end


//道岔缺口值 命令码：0x26
typedef struct
{
	BYTE cmdid;				//命令码：0x26
	WORD cnt;				//总记录数
	LPVOID lpdata;			//缺口数据记录
}StGapValue;

//缺口数据记录
typedef struct
{
	DWORD time;				//时间：4字节(Unix时间)
	WORD sid;				//转辙机ID
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	BYTE lrsign;			//左右偏标志：0 无效，1 左偏， 2 右偏 (315跟卡斯科左偏右偏的含义不同)
	WORD offset;			//偏移值：2字节，低位在前，除以100为实际值，单位mm
	WORD gap;				//缺口值：2字节，低位在前，除以100为实际值，单位mm
	WORD std;				//标准值：2字节，低位在前，除以100为实际值，单位mm
	BYTE gaptype;			//缺口值类型：0 静态采集， 1 道岔操纵后采集， 2 过车时采集 （315协议中定义，在卡斯科中填充OxFF）
	BYTE filldata[3];		//0xFF(3B, 预留)
}StGapRecord;
//道岔缺口值 end

typedef struct
{
	DWORD time;				//时间：4字节(Unix时间)
	WORD sid;				//转辙机ID
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	BYTE lrsign;			//左右偏标志：0 无效，1 左偏， 2 右偏 (315跟卡斯科左偏右偏的含义不同)
	WORD offset;			//偏移值：2字节，低位在前，除以100为实际值，单位mm
	WORD gap;				//缺口值：2字节，低位在前，除以100为实际值，单位mm
	WORD std;				//标准值：2字节，低位在前，除以100为实际值，单位mm
	BYTE gaptype;			//缺口值类型：0 静态采集， 1 道岔操纵后采集， 2 过车时采集 （315协议中定义，在卡斯科中填充OxFF）
	BYTE filldata[4];		//0xFF(3B, 预留)
}StGapRecord_4res;


//报警/预警信息及缺口图像信息回执  上位机收到完整的报警信息后，给下位机一个回执，予以确认
typedef struct
{
	BYTE cmdid;				//命令码：0x27 0x97
	WORD sid;				//转辙机ID
	DWORD time;				//报警时间：4字节(Unix时间)
	DWORD alarmconfirm;		//报警确认信号, 用于监测回执
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	BYTE lrsign;			//左右偏标志：0 无效，1 左偏， 2 右偏 (315跟卡斯科左偏右偏的含义不同)
	BYTE alarmtype;			//报警类型
	WORD offset;			//偏移值：2字节，低位在前，除以100为实际值，单位mm
	WORD gap;				//缺口值：2字节，低位在前，除以100为实际值，单位mm
	WORD std;				//标准值：2字节，低位在前，除以100为实际值，单位mm
	DWORD filldata;			//0xFF(4B, 预留)
	DWORD imglen;			//图像长度
}StAlarmAndImgRec;
//报警/预警信息及缺口图像信息 end
// 
//报警/预警信息及缺口图像信息  命令码：0x27
struct StAlarmAndImgInfo : public StAlarmAndImgRec
{
	//BYTE cmdid;				//命令码：0x27
	//WORD sid;				//转辙机ID
	//DWORD time;				//报警时间：4字节(Unix时间)
	//DWORD alarmconfirm;		//报警确认信号, 用于监测回执
	//BYTE fixorinvert;		//定反位：0 定位，1  反位
	//BYTE lrsign;			//左右偏标志：0 无效，1 左偏， 2 右偏 (315跟卡斯科左偏右偏的含义不同)
	//BYTE alarmtype;			//报警类型
	//WORD offset;			//偏移值：2字节，低位在前，除以100为实际值，单位mm
	//WORD gap;				//缺口值：2字节，低位在前，除以100为实际值，单位mm
	//WORD std;				//标准值：2字节，低位在前，除以100为实际值，单位mm
	//DWORD filldata;			//0xFF(4B, 预留)
	//DWORD imglen;			//图像长度
	LPVOID lpimg;			//图像内容
};



//道岔动作后模拟量及缺口图像信息  命令码：0x28 （卡斯科）
typedef struct
{
	BYTE cmdid;				//命令码：0x28
	WORD sid;				//转辙机ID
	DWORD collecttime;		//图像采集时间：4字节(Unix时间)
	DWORD actioncttime;		//道岔动作时间：4字节(Unix时间)
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	BYTE lrsign;			//左右偏标志：0 无效，1 左偏， 2 右偏 (315跟卡斯科左偏右偏的含义不同)
	BYTE filldata1;			//0xFF(1B, 预留)
	WORD offset;			//偏移值：2字节，低位在前，除以100为实际值，单位mm
	WORD gap;				//缺口值：2字节，低位在前，除以100为实际值，单位mm
	WORD std;				//标准值：2字节，低位在前，除以100为实际值，单位mm
	DWORD filldata2;		//0xFF(4B, 预留)
	DWORD imglen;			//图像长度
	LPVOID lpimg;			//图像内容
}StActionInfo;

//道岔动作后模拟量及缺口图像信息回执
typedef struct
{
	BYTE cmdid;				//命令码：0x28
	WORD sid;				//转辙机ID
	DWORD collecttime;		//图像采集时间：4字节(Unix时间)
	DWORD actioncttime;		//道岔动作时间：4字节(Unix时间)
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	BYTE lrsign;			//左右偏标志：0 无效，1 左偏， 2 右偏 (315跟卡斯科左偏右偏的含义不同)
	BYTE filldata1;			//0xFF(1B, 预留)
	WORD offset;			//偏移值：2字节，低位在前，除以100为实际值，单位mm
	WORD gap;				//缺口值：2字节，低位在前，除以100为实际值，单位mm
	WORD std;				//标准值：2字节，低位在前，除以100为实际值，单位mm
	DWORD filldata2;		//0xFF(4B, 预留)
	DWORD imglen;			//图像长度
}StActionInfoRec;
//道岔动作后模拟量及缺口图像信息 end


//道岔缺口最新图像 命令码：0x29
//请求
typedef struct
{
	BYTE cmdid;				//命令码：0x29
	WORD sid;				//转辙机ID
	DWORD filldata;			//0xFF(4B, 预留)
}StLastGapImgReq;

//应答
typedef struct
{
	BYTE cmdid;				//命令码：0x29
	WORD sid;				//转辙机ID
	DWORD time;				//图像采集时间：4字节(Unix时间)
	DWORD filldata1;		//0xFF(4B, 预留)
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	BYTE lrsign;			//左右偏标志：0 无效，1 左偏， 2 右偏 (315跟卡斯科左偏右偏的含义不同)
	BYTE filldata2;			//0xFF(1B, 预留)
	WORD offset;			//偏移值：2字节，低位在前，除以100为实际值，单位mm
	WORD gap;				//缺口值：2字节，低位在前，除以100为实际值，单位mm
	WORD std;				//标准值：2字节，低位在前，除以100为实际值，单位mm
	DWORD filldata3;		//0xFF(4B, 预留)
	DWORD imglen;			//图像/视频长度
	LPVOID lpimg;			//图像/视频内容
}StLastGapImgRes;
//道岔缺口最新图像 end


//图像列表 命令码：0x32
//请求
typedef struct
{
	BYTE cmdid;				//命令码：0x32
	WORD sid;				//转辙机ID
	DWORD begintime;		//开始时间：4字节(Unix时间)
	DWORD endtime;			//结束时间：4字节(Unix时间)
	BYTE imgtype;			//图像类型：0 静态图像列表， 2 操纵后图像列表， 3 过车时图像列表， 0xFF 所有类型 （315协议中定义，在卡斯科中填充OxFF）
	BYTE resqid;			//请求编号
	BYTE filldata[2];		//0xFF(2B, 预留)
}StImgListReq;

//应答
typedef struct
{
	BYTE cmdid;				//命令码：0x32
	WORD sid;				//转辙机ID
	WORD cnt;				//记录数
	BYTE resqid;			//请求编号
	BYTE filldata[3];		//0xFF(3B, 预留)
	LPVOID lprecord;		//文件记录
}StImgListRes;

//文件记录
typedef struct
{
	DWORD time;				//时间：4字节(Unix时间)
	DWORD filldata1;		//0xFF(4B, 预留)
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	BYTE lrsign;			//左右偏标志：0 无效，1 左偏， 2 右偏 (315跟卡斯科左偏右偏的含义不同)
	BYTE imgtype;			//图像类型：0 静态图像列表， 2 操纵后图像列表， 3 过车时图像列表 （315协议中定义，在卡斯科中填充OxFF）
	WORD offset;			//偏移值：2字节，低位在前，除以100为实际值，单位mm
	WORD gap;				//缺口值：2字节，低位在前，除以100为实际值，单位mm
	WORD std;				//标准值：2字节，低位在前，除以100为实际值，单位mm
	DWORD filldata2;		//0xFF(4B, 预留)
	DWORD imglen;			//图像长度
}StFileRecord;
//图像列表 end


//图像信息 命令码：0x2A
//请求
typedef struct
{
	BYTE cmdid;				//命令码：0x2A
	WORD sid;				//转辙机ID
	DWORD time;				//时间：4字节(Unix时间)
	BYTE imgtype;			//图像类型：0 静态图像列表， 2 操纵后图像列表， 3 过车时图像列表， 0xFF 所有类型 （315协议中定义，在卡斯科中填充OxFF）
	BYTE filldata[3];		//0xFF(3B, 预留)
}StImgInfoReq;

//应答
typedef struct
{
	BYTE cmdid;				//命令码：0x2A
	WORD sid;				//转辙机ID
	DWORD time;				//时间：4字节(Unix时间) (应答帧中的时间为下位机采集时间，下位机上送上位机给出的时间之前最接近的图像)
	BYTE imgtype;			//图像类型：0 静态图像列表， 2 操纵后图像列表， 3 过车时图像列表， 0xFF 所有类型 （315协议中定义，在卡斯科中填充OxFF）
	BYTE filldata1[3];		//0xFF(3B, 预留)
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	BYTE lrsign;			//左右偏标志：0 无效，1 左偏， 2 右偏 (315跟卡斯科左偏右偏的含义不同)
	BYTE filldata2;			//0xFF(1B, 预留)
	WORD offset;			//偏移值：2字节，低位在前，除以100为实际值，单位mm
	WORD gap;				//缺口值：2字节，低位在前，除以100为实际值，单位mm
	WORD std;				//标准值：2字节，低位在前，除以100为实际值，单位mm
	DWORD filldata3;		//0xFF(4B, 预留) (卡斯科：第一字节为是否是路径的标识，路径：1， 其他：文件内容) （315：填充0xFF）
	DWORD imglen;			//图像长度
	LPVOID lpimg;			//图像内容
}StImgInfoRes;
//图像信息 end


//视频时间列表 命令码：0x2E
//请求
typedef struct
{
	BYTE cmdid;				//命令码：0x2E
	BYTE vediotype;			//视频类型：0x01 道岔动作视频; 0x02	过车视频
	WORD sid;				//转辙机ID
	DWORD begintime;		//开始时间：4字节(Unix时间)
	DWORD endtime;			//结束时间：4字节(Unix时间)
	BYTE resqid;			//请求编号
	BYTE filldata[3];		//0xFF(3B, 预留)
}StVedioListReq;

//响应
typedef struct
{
	BYTE cmdid;				//命令码：0x2E
	BYTE vediotype;			//视频类型：0x01 道岔动作视频; 0x02	过车视频
	WORD sid;				//转辙机ID
	BYTE resqid;			//请求编号
	BYTE filldata[3];		//0xFF(3B, 预留)
	WORD cnt;				//记录数
	LPVOID lprecord;		//视频时间记录
}StVedioListRes;

//视频时间记录
typedef struct
{
	DWORD time;				//开始录制时间：4字节(Unix时间)
	BYTE fixorinvert1;		//录制前定反位：0 定位，1  反位
	BYTE fixorinvert2;		//录制后定反位：0 定位，1  反位
	DWORD len;				//视频长度：视频的大小，字节数
	DWORD filldata;			//0xFF(4B, 预留)
}StVedioRecord;
//视频时间列表 end


//视频文件 命令码：0x2F
//请求
typedef struct
{
	BYTE cmdid;				//命令码：0x2F
	BYTE vediotype;			//视频类型：0x01 道岔动作视频; 0x02	过车视频
	WORD sid;				//转辙机ID
	DWORD time;				//开始时间：4字节(Unix时间)
	DWORD filldata;			//0xFF(4B, 预留)
}StVedioFileReq;


//响应
struct StVedioFileRes4
{
	BYTE cmdid;				//命令码：0x2F
	BYTE vediotype;			//视频类型：0x01 道岔动作视频; 0x02	过车视频
	WORD sid;				//转辙机ID
	DWORD time;				//开始时间：4字节(Unix时间)
	BYTE fixorinvert1;		//录制前定反位：0 定位，1  反位
	BYTE fixorinvert2;		//录制后定反位：0 定位，1  反位
	WORD timelen;			//视频时长
	DWORD filldata;			//0xFF(4B, 预留)
	DWORD len;				//视频长度：视频的大小，字节数
	WORD packcnt;			//总包数
	WORD curpackid;			//本包序号
	DWORD datalen;			//本包内容长
	LPVOID lpdata;			//本包内容
};

//响应
struct StVedioFileRes2
{
	BYTE cmdid;				//命令码：0x2F
	BYTE vediotype;			//视频类型：0x01 道岔动作视频; 0x02	过车视频
	WORD sid;				//转辙机ID
	DWORD time;				//开始时间：4字节(Unix时间)
	BYTE fixorinvert1;		//录制前定反位：0 定位，1  反位
	BYTE fixorinvert2;		//录制后定反位：0 定位，1  反位
	WORD timelen;			//视频时长
	DWORD filldata;			//0xFF(4B, 预留)
	DWORD len;				//视频长度：视频的大小，字节数
	WORD packcnt;			//总包数
	WORD curpackid;			//本包序号
	WORD datalen;			//本包内容长
	LPVOID lpdata;			//本包内容

	//virtual DWORD GetDataLen() const
	//{
	//	return datalen;
	//}
	//virtual int GetItemSize() const
	//{
	//	return sizeof(datalen);
	//}
	//virtual LPVOID &GetData()
	//{
	//	return lpdata;
	//}
	//virtual int SetDataLen(BYTE *da)
	//{
	//	datalen = (*(decltype(datalen) *)da);
	//	return GetItemSize();
	//}

	//virtual void FreeData()
	//{
	//	if (lpdata)
	//	{
	//		delete[]lpdata;
	//	}
	//}
};
//视频文件 end


//1DQJ及道岔区段状态信息 命令码：0x22
typedef struct
{
	BYTE cmdid;				//命令码：0x22
	WORD sid;				//转辙机ID
	BYTE status;			//开关量状态：0x01  转辙机启动扳动(1DQJ吸起), 0x02  转辙机扳动结束(1DQJ落下), 0x03  道岔区段有车, 0x04  道岔区段车出清
	DWORD time;				//时间：4字节(Unix时间)
}St1DQJInfo;
//1DQJ及道岔区段状态信息 end

//天窗修时间 命令码：0x33
//请求
typedef struct
{
	BYTE cmdid;				//命令码：0x33
	BYTE winType;			//天窗修类型
	DWORD startTime;		//开始时间：4字节(Unix时间)
	DWORD endTime;			//结束时间：4字节(Unix时间)
	WORD winInfoLen;		//天窗修内容长度n
}StWinTimeReq;

//曲线数据
typedef struct
{
	BYTE type;				//曲线类型：左油压0x00、右油压0x01、启动电流0x02
	DWORD filldata;			//0xFF(4B, 预留)
	WORD len;				//本条曲线数据长度：指本条曲线数据本身长度，为字节数，不包含长度本身的2个字节
	LPVOID lpdata;			//曲线数据：一个数据点用2个字节，低字节在前。 倍率0.01。油压曲线单位：MPa，启动电流曲线单位：A
}StCurve;

//油压曲线回执
typedef struct
{
	BYTE cmdid;				//命令码：0x25
	WORD sid;				//转辙机ID
	DWORD time;				//时间：4字节(Unix时间)低字节在前，上位机发送动作开始时间时，此值为上位机发下来的时间；上位机没有发送道岔动作开始时间时，下位机按自己检测的道岔动作开始时间填充。
	BYTE collectfreq;		//采集频率：每秒钟采集的数据数。
	BYTE direct;			//定反位操作方向
	DWORD filldata;			//0xFF(4B, 预留)
	BYTE cnt;				//曲线条数
}StOilPreCurveRec;
//油压曲线 end

//油压曲线 命令码：0x25    (315预留，卡斯科已在使用) 
struct StOilPreCurve :public StOilPreCurveRec
{
	LPVOID lpdata;			//曲线数据
};

//油位及缺口采集设备状态信息  命令码：0x24
typedef struct
{
	BYTE cmdid;				//命令码：0x24
	WORD cnt;				//转辙机个数：本帧数据对应的转辙机数目按实际监测的转辙机数计算。
	LPVOID lpdata;			//转辙机数据记录
}StOilLevelInfo;

//转辙机数据记录
typedef struct
{
	WORD sid;				//转辙机ID
	WORD oillevel;			//油位：0xFFFF为不监测，其他为正常
	DWORD oiltime;			//油位时间：4字节(Unix时间)
	WORD camerastate;		//摄像头状态： 0：正常，1：故障,  0xFF：不监测
	DWORD camtime;			//摄像头采集时间：4字节(Unix时间)
	SHORT temperature;		//采集设备温度：2字节(低位在前),  倍率 0.01, 采用有符号数。
	DWORD temptime;			//温度采集时间：4字节(Unix时间)
	WORD humidity;			//采集设备湿度：2字节(低位在前),  倍率 0.01。
	DWORD humtime;			//湿度采集时间：4字节(Unix时间)
	DWORD filldata;			//0xFF(4B, 预留)
}StSdataRecord;
//油位及缺口采集设备状态信息 end


//参数设置与获取 命令码：0x21 （卡斯科）
typedef struct
{
	BYTE cmdid;				//命令码：0x21
	BYTE cmdtype;			//命令类型：0x01获取， 0x02设置
	BYTE paramtype;			//参数类型：0x01报警参数，0x02预警参数
	LPVOID lpdata;			//参数内容：命令类型为0x01时，上位机发送命令时内容为空。下位机回送全部转辙机信息
}StParamSet;

//参数肉容
typedef struct
{
	WORD cnt;				//转辙机个数
	LPVOID lpdata;			//转辙机参数
}StParamSetData;

//转辙机参数(没有的用0xFFFF，缺口值按 0.01)
typedef struct
{
	WORD sid;				//转辙机ID
	WORD fixgapu;			//定位缺口值上限
	WORD fixgapl;			//定位缺口值下限
	WORD fixoffsetu;		//定位偏移值上限
	WORD fixoffsetl;		//定位偏移值下限
	WORD fixstd;			//定位标准值
	WORD invertgapu;		//反位缺口值上限
	WORD invertgapl;		//反位缺口值下限
	WORD invertoffsetu;		//反位偏移值上限
	WORD invertoffsetl;		//反位偏移值下限
	WORD invertstd;			//反位标准值
	WORD tempu;				//温度上限
	WORD templ;				//温度下限
	WORD humu;				//湿度上限
	WORD huml;				//湿度下限
	WORD oillevelu;			//油位上限
	WORD oillevell;			//油位下限
}StSwitchParam;
//参数设置与获取 end


//实时视频流控制命令 命令码：0x30
//请求
typedef struct
{
	BYTE cmdid;				//命令码：0x30
	WORD sid;				//转辙机ID
	BYTE fixorinvert;		//请求命令时转辙机的定反位状态：0 定位，1  反位
	BYTE cmdtype;			//命令类型：
	//0x01  请求码流。命令内容为：帧率(1B) + 图像宽度(2B) + 图像高度(2B) + 预留(2B)。此时命令内容长度为7
	//0x02  开始。此时命令内容长度为0，没有命令内容
	//0x04  停止。此时命令内容长度为0，没有命令内容
	//0x05  设置帧率。命令长度为1。命令内容为：帧率（1B）
	//0x06  设置图像大小。命令长度为4。命令内容为：图像宽度(2B) + 图像高度(2B)
	LPVOID lpdata;			//命令内容
}StRealCtrlReq;

//请求码流
typedef struct
{
	BYTE rate;				//帧率
	WORD width;				//图像宽度
	WORD heigh;				//图像高度
	WORD filldata;			//预留
}StRealReqStream;

//设置帧率
typedef struct
{
	BYTE rate;				//帧率
}StRealSetRate;

//设置图像大小
typedef struct
{
	WORD width;				//图像宽度
	WORD heigh;				//图像高度
}StRealSetSize;

//响应
typedef struct
{
	BYTE cmdid;				//命令码：0x30
	WORD sid;				//转辙机ID
	BYTE result;			//响应结果：
	//0x00 失败。其他内容：失败原因(1B)
	//		失败原因：1 摄像头正在拍照，请5s后再直播。
	//				2 直播失败
	//0x01 成功。其他内容为空
	//0x02 已有用户建立连接。其他内容：连接用户IP(4B)
	LPVOID lpdata;//其他内容

}StRealCtrlRes;
//实时视频流控制命令 end


//实时码流 命令码：0x31
typedef struct
{
	BYTE cmdid;				//命令码：0x31
	WORD sid;				//转辙机ID
	DWORD packid;			//实时码流包序号：从 1开始
	DWORD packlen;			//实时码流包长度：< 64kB
	LPVOID lpdata;			//实时码流包数据
}StRealStream;
//实时码流 end

//室外电参数动作曲线回执
typedef struct
{
	BYTE cmdid;				//命令码：0x41
	WORD sid;				//转辙机ID
	DWORD time;				//采集时间: unix时间，低字节在前. 上位机发送动作开始时间时，此值为上位机发下来的时间；上位机没有发送道岔动作开始时间时，下位机按自己检测的道岔动作开始时间填充。
	BYTE freq;				//采集频率：每秒钟采集的数据数。  25个点
	BYTE dir;				//方向：0-定到反  1-反到定  2-定到定  3-反到反  4-定位到故障位  5-反位到故障位  6-故障到定位  7-故障到反位  8-故障到故障位  0xFF-无效
	DWORD acttime;			//动作次数
	BYTE r[4];				//预留(4B, 填充4字节0xFF)
	WORD cnt;				//曲线条数：本次采集的曲线条数。
}StElecCurveRec;
//室外电参数动作曲线 end

//室外电参数动作曲线 命令码：0x41
struct StElecCurve : public StElecCurveRec
{
	//BYTE cmdid;				//命令码：0x41
	//WORD sid;				//转辙机ID
	//DWORD time;				//采集时间: unix时间，低字节在前. 上位机发送动作开始时间时，此值为上位机发下来的时间；上位机没有发送道岔动作开始时间时，下位机按自己检测的道岔动作开始时间填充。
	//BYTE freq;				//采集频率：每秒钟采集的数据数。  25个点
	//BYTE dir;				//方向：0-定到反  1-反到定  2-定到定  3-反到反  4-定位到故障位  5-反位到故障位  6-故障到定位  7-故障到反位  8-故障到故障位  0xFF-无效
	//DWORD acttime;			//动作次数
	//BYTE r[4];				//预留(4B, 填充4字节0xFF)
	//WORD cnt;				//曲线条数：本次采集的曲线条数。
	LPVOID lpdata;			//曲线数据
};

//曲线数据
typedef struct
{
	BYTE type;				//曲线类型
	BYTE r[4];				//预留(4B, 填充4字节0xFF)
	WORD datalen;			//本条曲线数据长度：指本条曲线数据本身长度，为字节数，不包含长度本身的2个字节。
	WORD datacnt;			//曲线数据点数：本条曲线包含的数据点个数。
	LPVOID lpdata;			//曲线数据：每个数据点的内容与曲线类型相关：（重要：曲线类型中选定的一条曲线，曲线数据每个点都是2字节：而不是每一个点包含一个完整的各类数据结构！）
}StElecCurveData;




//帧内容数据结构 end



//视频插件通信协议
//帧头
typedef struct
{
	BYTE fheader[5];		//帧头：字符型 5字节“qknet”(71 6B 6E 65 74)
	DWORD datalen;			//数据长度
	LPVOID lpdata;			//数据
	DWORD ftail;			//帧尾：填充4个字节的0xFF。
}StVedioFrame;

//开始播放历史视频文件 命令码：0x10
typedef struct
{
	BYTE cmdid;				//命令码：0x10
	BYTE type;				//视频类型：0x01 道岔动作视频; 0x02 过车视频
	WORD sid;				//转辙机ID
	WORD titlelen;			//显示标题长度
	LPSTR title;			//显示标题
	WORD pathlen;			//本地视频路径长度
	LPSTR path;				//本地视频路径
}StVedioFileStart;

//结束播放历史视频文件 命令码：0x11
typedef struct
{
	BYTE cmdid;				//命令码：0x11
	BYTE type;				//视频类型：0x01 道岔动作视频; 0x02 过车视频
}StVedioFileStop;

//请求设置视频显示标题 命令码：0x12
typedef struct
{
	BYTE cmdid;				//命令码：0x12
	BYTE type;				//视频类型：0x00 实时视频，0x01 道岔动作视频; 0x02 过车视频
	WORD titlelen;			//显示标题长度
	LPSTR title;			//显示标题
}StVedioSetTitle;

//开始或结束实时视频流 命令码：0x13
typedef struct
{
	BYTE cmdid;				//命令码：0x13
	BYTE type;				//视频类型：0x00 实时视频
	BYTE sign;				//标志：0x00 结束播放实时视频流； 0x01开始播放实时视频流
	WORD sid;				//转辙机ID
	BYTE rate;				//帧率：每秒播放的帧数
	WORD width;				//图像宽度：单位为像素，播放图像的宽度（像素）
	WORD heigh;				//图像高度：单位为像素，播放图像的高度（像素）
}StVedioRealCtrl;

//播放实时视频流 命令码：0x14
typedef struct
{
	BYTE cmdid;				//命令码：0x14
	BYTE type;				//视频类型：0x00 实时视频
	WORD sid;				//转辙机ID
	WORD len;				//码流数据长度
	LPVOID lpdata;			//码流数据
}StVedioRealPlay;

//视频插件通信协议 end

typedef struct
{
	BYTE cmdid;			//命令码：0x40
	WORD zzjcnt;		//转辙机数
	LPVOID lprecord;	//记录
}StStaticPowerList;

typedef struct
{
	WORD zzjid;
	short powVal;
	DWORD acqtime;
	BYTE r[4];//预留(4B, 填充4字节0xFF)
}StStaticPowerData;

//电参数计算阻力 命令码：0x42
//曲线数据
typedef struct CurveInfoHead
{
	BYTE  type;				//曲线类型
	DWORD rbuf;				//预留
	WORD  length;			//本条曲线数据长度
	WORD  count;			//曲线数据点数
	vector<WORD> data;
}CurveInfoHead;
//请求
typedef struct CalPower
{
	BYTE cmdid;			//命令码：0x42
	WORD sid;			//转辙机ID
	DWORD time;			//采集时间
	BYTE frequency;		//最高采集频率
	BYTE dir;			//方向
	DWORD moveCount;	//动作次数
	DWORD rbuf;			//预留
	WORD curveCount;	//曲线条数
	vector<CurveInfoHead> curveInfoList;	//曲线数据 CurveInfoHead

	CalPower()
	{
		this->curveInfoList.clear();
	};

}CalPower;

//电参数 请求转阻力 命令码：0x42///////////////////////////////////////sadfhsa
typedef struct CalPowerResp
{
	BYTE  cmdid;			//命令码：0x42
	WORD  sid;			//转辙机ID
	DWORD time;			//采集时间
	BYTE  frequency;	//最高采集频率1B
	BYTE  dir;			//方向1B
	DWORD moveCount;	//动作次数4B
	DWORD rbuf;			//预留4B 0xFF
	BYTE  ret;			//计算结果 1B
	WORD  curveCount;	//曲线条数2B
	vector<CurveInfoHead> curveInfoList;	//曲线数据 CurveInfoHead

	CalPowerResp()
	{
		this->curveInfoList.clear();
	};

	CalPowerResp& operator=(CalPower& val)
	{
		this->cmdid = val.cmdid;
		this->sid = val.sid;
		this->time = val.time;
		this->frequency = val.frequency;
		this->dir = val.dir;
		this->moveCount = val.moveCount;
		this->rbuf = val.rbuf;
		return *this;
	};
}CalPowerResp;
///////////////////////////////////////////////////////////////askirhseurheasiokrl 

//阻力文件列表 命令码：0x43
//请求
typedef struct
{
	BYTE cmdid;		//命令码：0x43
	WORD sid;		//转辙机ID
	DWORD begintime;//开始时间：4字节(Unix时间)
	DWORD endtime;	//结束时间：4字节(Unix时间)
	BYTE direction;	//方向：0定到反//1反到定//2定到定//3反到反//4定位到故障位//5反位到故障位//6故障到定位//7故障到反位//8故障到故障位//0xFF 无效
}StPowerListReq;

//0x43 文件记录 结构体 
typedef struct StPowerRecord
{
	DWORD time;				//采集时间：4字节(Unix时间)
	BYTE acqfreq;		    //采集频率
	DWORD movecount;		//动作次数
	eMOVE_DIRECT dir;
	BYTE bFrictionCurve;	//是否摩擦力，0x01是摩擦力，0x00正常转换阻力, 0xFF 无效
	BYTE r[2];				//预留(2B, 填充2字节0xFF)
	BYTE calresult;         //计算结果
	uint8_t curvenum;          //曲线条数
	uint8_t rever = 0xff;
}StPowerRecord;

//应答
typedef struct
{
	BYTE cmdid;				//命令码：0x43
	WORD sid;				//转辙机ID
	BYTE direction;         //方向
	WORD cnt;				//记录数
	LPVOID lprecord;		//文件记录
}StPowerListRes;

//阻力信息 命令码：0x44
//请求
typedef struct
{
	BYTE cmdid;				//命令码：0x44
	WORD sid;				//转辙机ID
	DWORD time;				//时间：4字节(Unix时间)
	BYTE direction;         //方向
}StPowerInfoReq;

//0x44 曲线信息结构体
typedef struct
{
	BYTE bycurvetype;
	BYTE r[4];				//预留(4B, 填充4字节0xFF)
	WORD curvelen;          //曲线长度
	LPVOID lpcurvedata;		//曲线数据
}StPowerCurveInfo;
//0x44应答
typedef struct
{
	BYTE cmdid;				//命令码：0x44
	WORD sid;				//转辙机ID
	DWORD time;				//时间：4字节(Unix时间) (应答帧中的时间为下位机采集时间，下位机上送上位机给出的时间之前最接近的阻力文件)
	BYTE acqfreq;           //采集频率
	BYTE direction;         //方向
	DWORD movecount;        //动作次数
	DWORD r;				//预留(4B, 填充4字节0xFF)
	BYTE calresult;         //计算结果
	WORD cnt;				//曲线条数
	LPVOID lprecord;		//曲线记录
}StPowerInfoRes;

////315新文件生成 命令码：0xF1
typedef struct
{
	BYTE cmdid;				//命令码：0xF1
	WORD sid;				//转辙机ID
	WORD acqObjType;		//采集对象类型
	DWORD time;				//时间：4字节(Unix时间)
}NewDataFile;

////新阻力通知
//typedef struct
//{
//	BYTE cmdid;				//命令码：0xF1
//	DWORD r;				//预留
//	WORD sid;				//转辙机ID
//	DWORD r1;				//预留
//	BYTE cbDataType;		//数据类型
//	WORD acqObjType;		//采集对象类型
//	DWORD time;				//时间：4字节(Unix时间)
//	DWORD r2;				//预留
//}StNewPowerNotify;

//315新文件生成 命令码：0xF2
//新阻力列表
typedef struct
{
	BYTE cmdid;				//命令码：0xF2
	WORD sid;				//转辙机ID
	DWORD r;				//预留
	BYTE cbDataType;		//数据类型
	WORD acqObjType;		//采集对象类型
	DWORD starttime;		//开始时间：4字节(Unix时间)
	DWORD endtime;			//结束时间：4字节(Unix时间)
}StPowerFileListResq;

//315新文件生成 命令码：0xF2
//新阻力列表
typedef struct
{
	WORD sid;				//转辙机ID
	DWORD r;				//预留
	BYTE cbDataType;		//数据类型
	WORD acqObjType;		//采集对象类型
	DWORD time;				//采集时间：4字节(Unix时间)
	DWORD r1;				//预留
}StPowerFileList;

typedef struct
{
	BYTE cmdid;				//命令码：0xF2
	StPowerFileList* list;
	int	 nListCount;
}StPowerFileListF2;

//文件请求 命令码：0xF3
typedef struct
{
	BYTE cmdid;				//命令码：0xF3
	WORD sid;				//转辙机ID
	WORD acqObjType;		//采集对象类型
	DWORD time;				//时间：4字节(Unix时间)

	/*读取模式:
	0: 服务器一次性返回所有文件分包
	1: 服务器返回指定的分包编号。分包编号由后续字段 “分包号”指定。数据包从1开始编号，1为第一包
	*/
	BYTE cbReadMode;		//读取模式
	WORD wSubID;			//分包号
	DWORD r;				//预留
}StPowerFileDataResq;

//回包
typedef struct
{
	BYTE cmdid;				//命令码：0xF3
	WORD sid;				//转辙机ID
	WORD acqObjType;		//采集对象类型
	DWORD time;				//时间：4字节(Unix时间)
	BYTE result;			//0：成功 1：文件不存在 2：文件被占用
	DWORD fileLen;			//文件总长度
	WORD pakNo;				//包序号
	WORD pakAll;			//总包数
	WORD crc;				//crc校验
	DWORD r;				//预留
	DWORD pakLen;			//本包包长
	LPVOID dataInfo;		//文件信息
}StPowerFileData;



//手动加油请求 命令码：0x51
typedef struct
{
	BYTE cmdid;				//命令码：0x51
	WORD sid;				//转辙机ID
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	DWORD r;				//预留
}StManualOilingResq;

//手动加油应答 命令码：0x51
typedef struct
{
	BYTE cmdid;				//命令码：0x51
	WORD sid;				//转辙机ID
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	BYTE result;			//响应结果：0：成功 1：没有找到设备 2：不支持手动加油功能
	DWORD r;				//预留
}StManualOilingRes;

//加油结果通知 命令码：0x52  参照JIAYOU_RECORD
typedef struct
{
	BYTE cmdid;				//命令码：0x52
	WORD sid;				//转辙机ID
	BYTE type;				//加油方式 JIAYOU_TYPE
	WORD status;			//加油状态 0：未知 1：加油成功 2: 加油失败
	BYTE failCode;			//异常码 bFailCode
	DWORD time;				//时间：4字节(Unix时间) JiaYouTime
	WORD  timeLen;			//持续时间 JiayouTimeLen
	DWORD triggerTime;		//触发时间：4字节(Unix时间) TriggerTime
	BYTE fixorinvert;		//定反位：0 定位，1  反位
	BYTE r[9];				//预留
}StOilingResultNotify;

//加油箱储油量 请求包
typedef struct
{
	BYTE cmdid;				//命令码：0x53
	DWORD r;				//预留
	WORD sid;				//转辙机ID
}StOilBoxVolumeResq;


//加油箱储油量 转辙机数据
typedef struct
{
	WORD sid;				//转辙机ID
	WORD value;				//数据
	DWORD r;				//预留
}StOilBoxVolume;

//加油箱储油量 应答包
typedef struct
{
	BYTE cmdid;				//命令码：0x53
	WORD cnt;				//转辙机数量
}StOilBoxVolumeRes;

//加油箱储油量
typedef struct
{
	BYTE cmdid;				//命令码：0x53
	WORD v;					//
	LPVOID dataInfo;		//文件信息
}StOilBoxVolumeData;

//定反表 转辙机状态
typedef struct ZZJIDANDSTATUS
{
	WORD  zzjID;
	BYTE  ZZJStatus;

}ZZJIDAndStatus;

//0x60 DBJ FBJ信息
typedef struct DBJFBJINFO
{
	BYTE cmdid;			//命令码：0x60
	DWORD time;			//采集时间
	DWORD ret;			//预留

	//数据包部分
	WORD dataCount;	//动作次数
	LPVOID curveInfoList;
	
	DBJFBJINFO()
	{
	};

}DBJFBJInfo;



#pragma pack(pop)

#ifndef DEF_TSTRING
#define DEF_TSTRING
#ifdef _UNICODE
#define tstring wstring
#else
#define tstring string
#endif
#endif


//JHD系统通信协议解析类
class Parse315Protocol
{
public:
	Parse315Protocol() {}
	virtual ~Parse315Protocol() {}
	//static tstring GetAcqDescByAcqObjType(ACQ_OBJ_TYPE acqObjType);

	static BOOL Parse(StFrame&, LPVOID, int, int dir = 1);//解析  JHD->微机监测数据包（JHD就是站机端）
	static BOOL ParseDataFrm(StFrame& data, LPVOID* buf, int len, int dir);
	static BOOL Parse_4res(StGapValue& data, LPVOID buf, int len);//对接四川众合缺口值解析
	static BOOL Unparse(StFrame&, LPVOID&, int&, int dir = 0);//反解析   微机监测->JHD数据包
	static BOOL Unparse(StFrame& data, LPVOID& buf, int& len, BYTE* subbuf, int sublen);
	static BOOL Release(StFrame&, int dir = 0);//释放内存，dir: 数据包方向  0-微机监测->JHD  1-JHD->微机监测
	static BOOL CheckPackData(StFrame&, LPVOID, int, int dir);//检查数据包是否合法并进行解析，dir: 数据包方向  0-微机监测->JHD  1-JHD->微机监测

	static BYTE GetCommandId(const StFrame&);
	static BOOL GetFrameData(LPVOID&, int&, int&);//从缓冲区取得一帧数据
	static tstring ToString(const StFrame&, int dir);//将数据包翻译成字符串，dir: 数据包方向  0-微机监测->JHD  1-JHD->微机监测
	static tstring AStringToTString(LPCSTR lpStr);
	static SYSTEMTIME Time_tToSystemTime(time_t t);
	static BOOL TimeToString(const SYSTEMTIME& time, string& str);
	static BYTE GetOptDirIndex(tstring direct);//根据操作方向类型描述文本获取对应值
	static tstring GetOptDirDesc(int direct);//根据操作方向类型值获取对应描述文本
	static tstring GetOptDirDesc(eMOVE_DIRECT direct);//根据操作方向类型值获取对应描述文本
	static tstring GetElecCurveTypeDesc(int ElecCurveType);//根据室外电参数曲线类型值获取对应描述文本
	static tstring GetCurveTypeDesc(int CurveType, int nVer);//根据曲线类型值获取对应描述文本
	static tstring GetAlarmTypeDesc(int Alarmtype, int nVer);//根据报警类型值获取对应描述文本
	static tstring GetJiaYouTypeDesc(BYTE nType);//根据加油类型值获取对应描述文本

	static string GetStrFromData(BYTE* buf, int dwLen);

private:
	static BOOL Parse(StHeartBeat315&, LPVOID, int);
	static BOOL Parse(StGapCfgRes&, LPVOID, int, FRAME_KIND& kind);
	static BOOL Parse(StGapValue&, LPVOID, int, FRAME_KIND& kind);
	static BOOL Parse(StAlarmAndImgInfo&, LPVOID, int, FRAME_KIND& kind);
	static BOOL Parse(StActionInfo&, LPVOID, int, FRAME_KIND& kind);
	static BOOL Parse(StOilBoxVolumeData&, LPVOID, int, FRAME_KIND& kind);

	static BOOL Parse(StLastGapImgRes&, LPVOID, int, FRAME_KIND& kind);
	static BOOL ParseReq(StImgListReq&, LPVOID, int);
	static BOOL Parse(StImgListRes&, LPVOID, int);
	static BOOL ParseReq(StImgInfoReq&, LPVOID, int);
	static BOOL Parse(StImgInfoRes &, LPVOID, int); 
	static BOOL Parse(StImgInfoReq &, LPVOID, int); 
	static BOOL Parse(StWorkingConditionValRes&, LPVOID, int);
	static BOOL Parse(StOpWorkingConditionRes&, LPVOID, int);
	static BOOL Parse(StVibrationCurveRes&, LPVOID, int);

	static BOOL ParseReq(StVedioListReq&, LPVOID, int);
	static BOOL Parse(StVedioListRes&, LPVOID, int);
	static BOOL ParseReq(StVedioFileReq&, LPVOID, int);
	template<typename T>
	static BOOL Parse(T*, LPVOID, int);
	static BOOL ParseReq(St1DQJInfo&, LPVOID, int);//0x22这个没有响应

	static BOOL Parse(StOilPreCurve&, LPVOID, int);//0x25这个是JHD主动发出去的，以后再特别处理
	static BOOL Parse(StOilLevelInfo&, LPVOID, int);//0x24这个是JHD主动发出去的，以后再特别处理

	static BOOL ParseReq(StRealCtrlReq&, LPVOID, int);
	static BOOL Parse(StRealCtrlRes&, LPVOID, int);

	static BOOL Parse(StRealStream&, LPVOID, int);
	static BOOL Parse(StElecCurve&, LPVOID, int);
	static BOOL Parse(StElecCurveRec&, LPVOID, int);
	static BOOL Parse(StStaticPowerList&, LPVOID, int);
	static BOOL Parse(StPowerListRes &, LPVOID, int);
	static BOOL Parse(StPowerListReq &, LPVOID, int);
	static BOOL Parse(StPowerInfoRes&, LPVOID, int);
	static BOOL Parse(NewDataFile& data, LPVOID buf, int len);
// 	static BOOL Parse(StNewPowerNotify& data, LPVOID buf, int len);
	static BOOL Parse(StPowerFileData& data, LPVOID buf, int len);
	static BOOL Parse(DBJFBJInfo& data, LPVOID buf, int len);
	static BOOL Parse(StAlarmListRes&, LPVOID, int);

	static BOOL Unparse(StHeartBeat315&, LPVOID&, int&);
	static BOOL Unparse(StDataBasic&, LPVOID&, int&);
	static BOOL Unparse(StAlarmAndImgRec&, LPVOID&, int&);
	static BOOL Unparse(StActionInfoRec&, LPVOID&, int&);
	static BOOL Unparse(StManualOilingResq&, LPVOID&, int&);
	static BOOL Unparse(StLastGapImgReq&, LPVOID&, int&);
	static BOOL Unparse(StImgListReq&, LPVOID&, int&);
	static BOOL Unparse(StImgInfoReq&, LPVOID&, int&);
	static BOOL Unparse(StVedioListReq&, LPVOID&, int&);
	static BOOL Unparse(StVedioFileReq&, LPVOID&, int&);
	static BOOL Unparse(StPowerFileListResq&, LPVOID&, int&);
	static BOOL Unparse(StPowerFileDataResq&, LPVOID&, int&);
	static BOOL Unparse(St1DQJInfo&, LPVOID&, int&);
	static BOOL Unparse(StOilPreCurveRec&, LPVOID&, int&);
	static BOOL Unparse(StRealCtrlReq&, LPVOID&, int&);
	static BOOL Unparse(StPowerListReq&, LPVOID&, int&);
	static BOOL Unparse(StPowerInfoReq&, LPVOID&, int&);
	static BOOL Unparse(StPowerFileData& data, LPVOID& buf, int& len);
	static BOOL Unparse(CalPower& data, LPVOID& buf, int& len);
	static BOOL Unparse(DBJFBJInfo& data, LPVOID& buf, int& len);
	static BOOL Unparse(StAlarmListReq&, LPVOID&, int&);
	static BOOL Unparse(StOilBoxVolumeResq&, LPVOID&, int&);

	static BOOL Release(StGapCfgRes&);
	static BOOL Release(StGapValue&);
	static BOOL Release(StAlarmAndImgInfo&);
	static BOOL Release(StActionInfo&);
	static BOOL Release(StOilBoxVolumeData&);
	
	static BOOL Release(StLastGapImgRes&);
	static BOOL ReleaseReq(StImgListReq&);
	static BOOL Release(StImgListRes&);

	static BOOL Release(StImgInfoRes&);
	static BOOL Release(StVedioListRes&);
	template<typename T>
	static BOOL Release(T *);//未释放数据内存
	static BOOL Release(StPowerFileListResq& data);
	static BOOL Release(StPowerFileDataResq& data);
	static BOOL Release(StOilPreCurve&);
	static BOOL Release(StOilLevelInfo&);
	static BOOL Release(StRealCtrlReq&);
	static BOOL Release(StRealCtrlRes&);
	static BOOL Release(StRealStream&);
	static BOOL Release(StElecCurve&);
// 	static BOOL Release(StNewPowerNotify&);
	static BOOL Release(StStaticPowerList&);
	static BOOL Release(StPowerListRes&);
	static BOOL Release(StPowerInfoRes&);
	static BOOL Release(DBJFBJInfo&);
	static BOOL Release(StAlarmListRes&);

	static tstring ToString(const StHeartBeat315&);
	static tstring ToString(const StGapCfgRes&);
	static tstring ToString(const StGapValue&);
	static tstring ToString(const StAlarmAndImgInfo&);
	static tstring ToString0x27(const StAlarmAndImgRec&);
	static tstring ToString0x97(const StAlarmAndImgRec&);
	static tstring ToString(const StActionInfo&);
	static tstring ToString(const StActionInfoRec&);
	static tstring ToString(const StLastGapImgReq&);
	static tstring ToString(const StLastGapImgRes&);
	static tstring ToString(const StImgListReq&);
	static tstring ToString(const StImgListRes&);
	static tstring ToString(const StImgInfoReq&);
	static tstring ToString(const StImgInfoRes&);
	static tstring ToString(const StVedioListReq&);
	static tstring ToString(const StVedioListRes&);
	static tstring ToString(const StVedioFileReq&);
	template<typename T>
	static tstring ToString(const T *);
	static tstring ToString(const St1DQJInfo&);
	static tstring ToString(const StOilPreCurve&);
	static tstring ToString(const StOilPreCurveRec&);
	static tstring ToString(const StOilLevelInfo&);
	static tstring ToString(const StParamSet&);
	static tstring ToString(const StRealCtrlReq&);
	static tstring ToString(const StRealCtrlRes&);
	static tstring ToString(const StRealStream&);
	static tstring ToString(const StElecCurve&);
	static tstring ToString(const StElecCurveRec&);

// 	static tstring ToString(const StNewPowerNotify&);
	static tstring ToString(const StPowerFileListF2*, int nItemCount);
	static tstring ToString(const StPowerFileData&);
	static tstring ToString(const StStaticPowerList&);
	static tstring ToString(const StPowerListReq&);
	static tstring ToString(const StPowerListRes&);
	static tstring ToString(const StPowerInfoReq&);
	static tstring ToString(const StPowerInfoRes&);
	static tstring ToString(const DBJFBJInfo&);
	static tstring ToString(const StManualOilingRes&);
	static tstring ToString(const StOilingResultNotify&);
	static tstring ToString(const StAlarmListReq&);
	static tstring ToString(const StAlarmListRes&);
public:
	static BOOL b26res4byte;
	static BOOL b2Flen2byte;
};


//视频插件通信协议解析类
class CVedioParser
{
public:
	CVedioParser() {}
	virtual ~CVedioParser() {}

	static BOOL Parse(StVedioFrame&, LPVOID, int);				//解析
	static BOOL Unparse(StVedioFrame&, LPVOID&, int&);			//反解析
	static BOOL Release(StVedioFrame&);							//释放内存

private:
	static BOOL Unparse(StVedioFileStart&, LPVOID&, int&);
	static BOOL Unparse(StVedioFileStop&, LPVOID&, int&);
	static BOOL Unparse(StVedioSetTitle&, LPVOID&, int&);
	static BOOL Unparse(StVedioRealCtrl&, LPVOID&, int&);
	static BOOL Unparse(StVedioRealPlay&, LPVOID&, int&);

	static BOOL Release(StVedioFileStart&);
	static BOOL Release(StVedioSetTitle&);
	static BOOL Release(StVedioRealPlay&);
};
