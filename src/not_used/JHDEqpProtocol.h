#ifndef TDS_NOT_USED_JHDEQPPROTOCOL_H
#define TDS_NOT_USED_JHDEQPPROTOCOL_H

#ifdef _WIN32
/*
设备通信协议目前分三个版本：
1. Equip Protocol Ver 0 (V0)：原始的can采集应用协议,两线载波的方式下也使用此协议
2. Equip Protocol Ver 1 (V1): 高速载波2014实现时的D520模块扩展，增加了油压油位，扳动信号等的上送
3. Equip Protocol Ver 2 (V2): 新系统开发时候的新硬件设备扩展
*/

//#include <windef.h>
#include "common/common.h"
#include "proto_common.h"


/******************************************************************************************************************************************************************************
*														Equip Protocol 协议基础部分
*******************************************************************************************************************************************************************************/
#define EQP_MANAGER_PORT		5011			//2014设备上位机监听设备连接的端口号
#define EQP_MANAGER_PORT_2016	5011			//2016设备上位机监听设备连接的端口号
#define SERVER_PORT				4011			//下位机监听主动连接的端口号
#define LOG_SERVER_PORT			5022
//命令框架
#define FRAME_HEADER			0x68			//帧头
#define FRAME_TAIL				0x97			//帧尾
#define CMD_FLAG_NOR			0x00			//正常
#define CMD_FLAG_ERROR			0x01			//错误的命令/错误的幀格式
#define CMD_FLAG_UNKNOW			0x0F			//未知错误
#define MAX_DATA_SIZE			4096			//数据最大长度
#define EQP_PKT_DIR_TOHOST	1
#define EQP_PKT_DIR_TOEQP   0

#define BRAODCAST_ADDR  0x3FF

/******************************************************************************************************************************************************************************
*														Equip Protocol Ver 0 设备应用命令部分
*******************************************************************************************************************************************************************************/

#define BY_CMD_SETADDNO 0x08//01000	设置CE_ID
#define BY_CMD_SETCLOCK 0x01//00001	设置时钟

#define BY_CMD_SET_COMMPARAM 0x09 //设置/查询设备参数
#define BY_CMD_SETCANANDID	0x03//设置/查询can参数
#define BY_CMD_READSTATUS 0x10//查询状态	双向
#define BY_CMD_SETCHANNELONOFF 0x14//输出开关量控制 双向
#define BY_CMD_RESTART 0x05//关闭设备 包括重启升级功能	双向
#define BY_CMD_UP_TEMPHUMI 0x1D  //上报：温湿度数据 采集器→主机 不需确认
#define BY_CMD_SET_VIDEOINFO 0x0A//设置码流参数  双向	2009-08-21
#define BY_CMD_SET_AUDIOINFO 0x0b//设置音频参数 双向	2009-08-21
#define BY_CMD_OPEN_RECORD	0x15//开关录象	  双向	2009-08-21
#define BY_CMD_OPEN_STREAM	0x16//开关实时码流  双向	2009-08-21
#define BY_CMD_STARTIMAGE 0x11//启动图像拍摄 10001
#define BY_CMD_READ_IMAGE_DATA 0x12//10010 读取图像数据	双向
#define BY_CMD_READ_FILEINFO 0x17
#define BY_CMD_OK 0x13//确认数据	双向
#define BY_CMD_CHECKVERINFO 0x06

/******************************************************************************************************************************************************************************
*																	V0协议扩展
*******************************************************************************************************************************************************************************/
//全动态电源控制器扩展
#define PowerCtrlExt_Protocol_Cmd			0x0C
#define PowerCtrlExt_Protocol_EqpID			0x03FE //CE_ID为0x3FE仅用于RS422转CAN电源控制器协议

//低速载波升级协议扩展
#define UpgradeExt_Protocol_Cmd				0x06


//D520扩展 （高速载波2014）
#define D520Ext_Protocol_Cmd				0x0E		//01110  一级命令码的D520扩展
#define BY_CMD_D520ExtensionProtocol 0x0E //01110 增强扩展协议//CMD_ID：命令编号,命令的唯一功能标识，对于液压转辙机工况参数采集器，固定为0x0E。

//高速载波2016扩展
#define GSZB2016Ext_Protocol_Cmd			0x1B		//一级命令码的gszb2016协议扩展
#define GSZB2016_GONGWU_Protocol_Cmd		0x0B		//临时给工务采集设备用的一级命令
#define GSZB2016Ext_Protocol_EqpID			0x3FC		//一级命令码的gszb2016协议扩展 时 一级框架下的设备ID常量值
#define GSZB2016Ext_FLAG_VAL				0x0000

/******************************************************************************************************************************************************************************
*														低速载波升级协议扩展
*******************************************************************************************************************************************************************************/
#define UPGRADE_READ_VER 0x01
#define UPGRADE_DOWNLOAD_FILE 0x02
#define UPGRADE_START 0x03

#pragma pack(push,1)

//低速载波模块升级协议部分
struct REQ_UPGRADE_FILE_INFO
{
	WORD wPktID;//信息包为0
	WORD wPktNum;
	BYTE byVer;
	BYTE r[4];
};

struct RESP_UPGRADE_FILE_INFO
{
	WORD wPktID;//信息包为0
	BYTE byState;
	BYTE r[4];
};

struct REQ_UPGRADE_FILE_DATA
{
	WORD wPktID;//信息包为0
	DWORD dwDataAddr;//数据存放地址
	WORD wDataLen;//数据长度，默认512字节
	BYTE* pData;//数据
	WORD wCRC;
	BYTE r[4];
};

struct REQ_STATE_CHECK
{
	WORD wPktID; //信息包为0
	BYTE byCheckState;
	WORD wCRC;
	BYTE r[4];
};
#pragma pack(pop)

/*****************************************************************************************
Equip Protocol Ver 1 设备应用命令部分
******************************************************************************************/

//2012-11-19 油压油位 2级命令
//5.1	检测到道岔扳动状态变化
#define BY_CMD2_REPORT_MOVE 0x01
//5.2	采集器主动上报静态油压
#define BY_CMD2_REPORT_YOUYA 0x02
//5.3	采集器主动上报油位数据
#define BY_CMD2_REPORT_YOUWEI 0x03
//5.4	采集器主动上报过车信号
#define BY_CMD2_REPORT_TRAIN 0x04
//5.5	采集器主动上报当前工作状态
#define BY_CMD2_REPORT_STATUS 0x05
//5.20	采集器主动上报缺口采集按键触发信号
#define BY_CMD2_REPORT_TRIGGER 0x06
//5.21	主机推送识别后的表示缺口偏移值到采集器
#define BY_CMD2_SEND_GAP 0x07
//5.22	读取道岔当前工务参数
#define BY_CMD2_GONGWU_READDATA 0x30
//5.23	读取道岔工务参数测量归零参数
#define BY_CMD2_GONGWU_READ_ZEROPARAMDATA 0x31
//5.24	设置道岔工务参数测量归零参数
#define BY_CMD2_GONGWU_SET_ZEROPARAMDATA 0x32
//5.25	采集器模拟采样通道校正命令
#define BY_CMD2_ACQ_CHANNEL_CHECK 0x28
//5.15.1读取爬行
#define BY_CMD2_GONGWU_READDATA_PAXING 0x33
//5.4.2读取密贴
#define BY_CMD2_GONGWU_READDATA_MITIE 0x34
//5.15.3读取轨距
#define BY_CMD2_GONGWU_READDATA_GUIJU 0x35

//5.21	主机推送识别后的表示缺口偏移值到采集器
#define BY_CMD2_SEND_CURRENT_GAP 0x44


/*命令响应状态：
0--请等待，已经开始采集缺口数据；
1-无对应缺口采集设备；
2-其它错误；*/
#define BY_YYYW_TRIGGER_STATUS_WAIT 0x00
#define BY_YYYW_TRIGGER_STATUS_NONE 0x01
#define BY_YYYW_TRIGGER_STATUS_OTHER 0x02

//5.6	读取待上送扳动过程油压数据记录
#define BY_CMD2_READ_YOUYA_DATA 0x11
//5.7	读取待上送扳动过程加速度数据记录
#define BY_CMD2_READ_MOVE_WAVE_DATA 0x12
//5.8	读取待上送过车过程加速度数据记录
#define BY_CMD2_READ_TRAIN_WAVE_DATA 0x13

//5.9	数据记录接收确认
#define BY_CMD2_YYYW_DATA_OK 0x16
//5.10	读取采集器工作状态
#define BY_CMD2_YYYW_READ_STATUS 0x14
//5.11	读取采集器当前采集值
#define BY_CMD_YYYW_READ_CURRENT 0x15
//5.12	设置采集器地址
#define BY_CMD_YYYW_SET_ID 0x20
//5.13	设置通讯参数
#define BY_CMD_YYYW_SET_COMMPARAM 0x21
//5.14	广播读取采集器地址
#define BY_CMD_YYYW_READ_ID 0x22
//5.15	广播设置采集器地址
#define BY_CMD_YYYW_BROAD_SET_ID 0x23
//5.16	配置采集器工作参数
#define BY_CMD_YYYW_SET_PARAM 0x24
//5.17	读取采集器工作参数
#define BY_CMD_YYYW_READ_PARAM 0x25
//5.18	配置振动采集策略
#define BY_CMD_SET_ZHENDONG_RULE 0x26
//5.19	读取振动采集策略
#define BY_CMD_READ_ZHENDONG_RULE 0x27

//77.25 按钮事件拍照
#define BY_CMD2_BUTTON_PHOTO 0x43	//需要确认回复

/******************************************************************************************************
*	Equip Protocol Ver 2 设备应用命令部分
*   目前有缺口设备，工务采集设备支持该协议
*
*******************************************************************************************************/
//v2产品类型码
#define PRODUCT_TYPE_COMMON         0x00            //不针对任何设备类型  公共命令
#define PRODUCT_TYPE_GAP			0x11			//缺口模块
#define PRODUCT_TYPE_POWER			0x21			//阻力模块
#define PRODUCT_TYPE_GONGWU			0x31			//工务参数采集模块
#define PRODUCT_TYPE_ZHENGDONG		0x61			//JHD-SV振动监测模块
#define PRODUCT_TYPE_JIAYOU         0x71            //JHD-JY21道岔外锁闭装置自动加油器
#define PRODUCT_TYPE_JIAYOU_POWER   0x81			//加油阻力一体化设备
#define PRODUCT_TYPE_GAP_POWER		0x82			//缺口阻力一体化设备
#define PRODUCT_TYPE_CIRMON         0x91			//驱动电路监测设备
#define PRODUCT_TYPE_CAN_MONITOR_GATEWAY     0xA1   //Can传感器综合监测网关
#define PRODUCT_TYPE_CMGW_WEIYI			0xA2
#define PRODUCT_TYPE_CMGW_POWER			0xA3
#define PRODUCT_TYPE_CMGW_ZHENDONG		0xA4
#define PRODUCT_TYPE_CMGW_TYZZJ			0xA8         //太原厂转辙机监测
#define PRODUCT_TYPE_SHANGSUN			0xB2
#define PRODUCT_TYPE_CMGW_POWERMGT		0xB3         //电源管理
#define PRODUCT_TYPE_SUPERSONIC		0xB4         //超声波监测

//设备公有命令
#define COMM_CMD_HEARTBEAT			0xF000			//心跳
#define COMM_CMD_GET_ID				0xF001			//读取ID
#define COMM_CMD_SET_ID				0xF002			//设置ID
#define COMM_CMD_GET_DEVINFO		0xF003			//读取模块制造信息
#define COMM_CMD_SET_DEVINFO		0xF004			//设置模块制造信息
#define COMM_CMD_UPDATE_START		0xF005			//开始升级软件
#define COMM_CMD_UPDATE_LOAD		0xF006			//发送升级包
#define COMM_CMD_UPDATE_RUN			0xF007			//执行升级
#define COMM_CMD_UPDATE_RESULT		0xF008			//上报升级结果
#define COMM_CMD_GET_NETINFO		0xF009			//读取网络参数
#define COMM_CMD_SET_NETINFO		0xF00A			//设置网络参数
#define COMM_CMD_RESTART			0xF00C			//重启设备
#define COMM_CMD_RESTORE			0xF00D			//恢复出厂设置
#define COMM_CMD_TIMING				0xF00E			//校时
#define COMM_CMD_EQP_ERROR			0xF00F			//设备异常状态上报
#define COMM_CMD_GET_CAN_BAUDRATE   0x0F10			//读取Can波特率
#define COMM_CMD_SET_CAN_BAUDRATE   0x0F11			//读取Can波特率
#define COMM_CMD_GET_AI             0x0001          //读取通道值

//文件处理
#define EQP_CMD_GET_FILE_LIST		0xAD71			//获取文件列表
#define EQP_CMD_GET_FILE_DATA		0xAD72			//读取文件数据
#define EQP_CMD_DELETE_FILE			0xAD73			//删除文件
#define EQp_CMD_STOP_READFILE		0xAD74			//停止文件读取

//信号处理
#define EQP_CMD_REPORT_MOVE_START	0xAD51			//上报道岔扳动启动 下位机->上位机
#define EQP_CMD_REPORT_MOVE_END		0xAD52			//上报道岔扳动结束 下位机->上位机
#define EQP_CMD_REPORT_ACQ_FINISH	0xAD55			//上报事件采集结果
#define EQP_CMD_REPORT_EVENT		0xAD56			//设备主动上报采集事件(直接阻力中使用)
#define EQP_CMD_SET_MOVE_START		0xAD61			//道岔扳动启动 上位机->下位机
#define EQP_CMD_SET_MOVE_END		0xAD62			//道岔扳动结束 上位机->下位机
#define EQP_CMD_SET_CROSS_START		0xAD63			//过车开始
#define EQP_CMD_SET_CROSS_EDN		0xAD64			//过车结束

//压力测试命令
#define CMD_STRESS_TEST             0xAD90

//Can综合监测网关
#define EQP_CMD_CAN_ONLINE_LIST		0x0101

#define EQP_CMD_CMGW_WEIYI_CALIB    0x0201

//EPV2 Gap缺口模块产品命令
#define EQP_CMD_SET_WORK_PARAM	0xAD01	//设置工作参数
#define EQP_CMD_READ_WORK_PARAM	0xAD02	//读取工作参数
#define EQP_CMD_SET_DC_INNER_PARAM	0xAD03	//设置数据采集内部参数
#define EQP_CMD_READ_DC_INNER_PARAM	0xAD04	//读取数据采集内部参数
#define EQP_CMD_SET_COMMON_RUN_PARAM	0xAD05	//设置常规运行参数
#define EQP_CMD_READ_COMMON_RUN_PARAM	0xAD06	//读取常规运行参数
#define EQP_CMD_SET_ALL_PARAM	0xAD07	//设置全部参数
#define EQP_CMD_READ_ALL_PARAM	0xAD08	//读取全部参数
#define EQP_CMD_SET_CAMERA_PARAM	0xAD09	//设置摄像机参数
#define EQP_CMD_READ_CAMERA_PARAM	0xAD0A	//读取摄像机参数
#define EQP_CMD_GET_STATUS	0xAD12	//查询状态
#define EQP_CMD_START_PHOTO2	0xAD22	//启动实时拍照2
#define EQP_CMD_START_VIDEO	0xAD31	//启动实时动态视频
#define EQP_CMD_RTVIDEO_DATA	0xAD32	//发送实时动态视频
#define EQP_CMD_CLOSE_VIDEO	0xAD33	//关闭实时动态视频
#define EQP_CMD_GET_ACQ_VAL	0xAD41	//读取采集器当前采集值
#define EQP_CMD_GET_GAP_VAL	0xAD65	//回传当前缺口数值


//EPV2 Zhengdong 振动模块产品命令
#define EQP_CMD_ZD_SET_PARAM				0xAF01			//设置工作参数
#define EQP_CMD_ZD_GET_PARAM				0xAF02			//读取工作参数
#define EQP_CMD_ZD_START_CURVE_ACQ_AF21		0xAF21			//振动信号采集启动

#define EQP_CMD_LIGHTSHINE					0xAD13			//运行灯控制


//EPV2 Gw 道岔工务参数采集器产品命令
#define CMD_GET_GONGWU_PARAM				0x30			//读取道岔工务参数
#define CMD_GET_GONGWU_CHECK_PARAM			0x31			//读取道岔工务校准参数
#define CMD_SET_GONGWU_CHECK_PARAM			0x32			//读取道岔工务校准参数
#define CMD_GET_WORK_PARAM					0x25
#define CMD_SET_WORK_PARAM					0x24
#define CMD_CODE_SETCORRECT					0x32			//设置道岔工务参数测量校正参数


//EPV2 JIAYOU 道岔外锁闭装置自动加油器产品命令
#define CMD_GET_JIAYOU_PARAM             0xDA01
#define CMD_SET_JIAYOU_PARAM             0xDA02
#define CMD_GET_JIAYOU_LEVEL             0xDA21
#define CMD_DO_JIAYOU                    0xDA22
#define CMD_GET_JIAYOU_PRESSURE          0xDA23
#define CMD_GET_JIAYOU_TEMP				 0xDA24
#define CMD_DO_JIAYOU_RESULT			 0xDA25


//EPV2 直接阻力采集产品命令
#define IMDPOWER_CMD_READ_CUR_POWER			0xAE11			//读取当前阻力
#define IMDPOWER_CMD_GET_POWER_CHECK_PARAM	0xAE21			//读取阻力校准参数
#define IMDPOWER_CMD_SET_POWER_CHECK_PARAM	0xAE22			//设置阻力校准参数
#define IMDPOWER_CMD_SET_WORK_PARAM			0xAE31			//设置阻力工作参数
#define IMDPOWER_CMD_GET_WORK_PARAM			0xAE32			//读取阻力工作参数
#define IMDPOWER_CMD_GET_ELEC_CHECK_PARAM	0xAE41			//读取电流校准参数
#define IMDPOWER_CMD_SET_ELEC_CHECK_PARAM	0xAE42			//设置电流校准参数
#define IMDPOWER_CMD_READ_CUR_ELEC			0xAE43			//读取当前电流数据


////EPV2 驱动电路监测采集产品命令
#define CIRCUIT_MON_READ_CUR_VALUE		0x6101			//读取当前实时值


//EPV2 钢轨伤损监测设备命令
#define Shangsun_CMD_WORK_PARAM			0xB200			//设置/读取工作参数
#define Shangsun_CMD_LOCKIN_WORK_PARAM	0xB201			//设置/读取锁相放大器工作参数
#define Shangsun_CMD_VIBRATION_PARAM	0xB202			//设置/读取振动工作参数
#define Shangsun_CMD_GET_REG			0xB203			//读取寄存器
#define Shangsun_CMD_SET_REG			0xB204			//设置寄存器

//EPV2 钢轨超声波监测设备命令
#define SUPERSONIC_CMD_GET_REG			0xB500			//读取寄存器
#define SUPERSONIC_CMD_SET_REG			0xB501			//设置寄存器
#define SUPERSONIC_CMD_DEVICE_PARAM	    0xB502			//设置/读取分机工作参数
#define SUPERSONIC_CMD_RW_CHANNEL_RELAY	0xB503			//设置/读取超声伤损通道继电器
#define SUPERSONIC_CMD_RW_CFG_PARAM_LIST	    0xB504	//设置/读取配置参数列表
#define SUPERSONIC_CMD_UPDATE_START		0xB505			//开始升级仪表
#define SUPERSONIC_CMD_UPDATE_LOAD		0xB506			//发送仪表主程序分包
#define SUPERSONIC_CMD_UPDATE_RUN			0xB507			//执行仪表升级
#define SUPERSONIC_CMD_UPDATE_RESULT		0xB508			//上报仪表升级结果

//EPV2 太原厂转辙机监测设备命令
#define TYZZJ_ACQEQP_CMD_WORK_PARAM		0xB301			//设置/读取工作参数
#define TYZZJ_ACQEQP_STATICDATA_UPLOAD  0xB302          //静态采样数据上送
#define TYZZJ_ACQEQP_EQPUPGRADEINFO     0xB303
#define TYZZJ_ACQEQP_EQPUPGRADEDATA     0xB304
#define TYZZJ_ACQEQP_EQPUPGRADEDATAEND  0xB305       //固件升级数据发送完成
#define TYZZJ_ACQEQP_EQPUPGRADESTATUS   0xB306       //固件更新状态上报

//EPV2 驱动电路监测设备命令
#define QDDL_ACQEQP_CMD_GET_WORK_PARAM	0xDC01	//读取工作参数
#define QDDL_ACQEQP_CMD_SET_WORK_PARAM	0xDC02	//设置工作参数
#define QDDL_ACQEQP_CMD_ON_ACQ_FILE		0xDC03	//开启文件采集
#define QDDL_ACQEQP_CMD_OFF_ACQ_FILE	0xDC04	//关闭文件采集
#define QDDL_ACQEQP_CMD_BAD_LIST	    0xDC05	//获取故障列表
#define QDDL_ACQEQP_CMD_STATUS_NOTICE	0xDC06	//状态通知
#define QDDL_ACQEQP_CMD_STATUS_CLEAR	0xDC07	//状态清除

//EPV2 电源管理监测设备命令
#define POWMGT_ACQEQP_CMD_GET_SET_WORKPARAM		0xB400	//读取、设置工作参数
#define POWMGT_ACQEQP_CMD_START_END_ELECRATED	0xB401	//开始、结束电流额定
#define POWMGT_ACQEQP_CMD_READ_LOADRATED		0xB402	//读取负载通道额定值
#define POWMGT_ACQEQP_CMD_GET_SET_RELAYSTATUS	0xB403	//读取、设置继电器状态
#define POWMGT_ACQEQP_CMD_REALTIME_VALUE		0xB404	//实时值

//JDSP子协议
#define CMD_JDSP_SUBPROTOCOL                0xFF01

#pragma pack(push,1)
//***************************一级与二级命令框架************************************
typedef struct
{
	DWORD dir : 1;	//方向，0-下行（主站→采集器）,1-上行（采集器→主站）
	DWORD ceid : 10;
	DWORD cmdid : 5;	//恒为0x1B
	DWORD flag : 4;	//FLAG恒为0x0000
	DWORD dlen : 12;	//数据长度，本协议最大为4095
}StCtrlHead; //共4字节

enum EQP_PKT_TYPE {
	EPT_ORG,
	EPT_D520Ext,
	EPT_EP2016,
};

typedef struct
{
	LPVOID lpdata;
}StData;

typedef struct
{
	BYTE cmd2;
	LPVOID lpdata;
}D520ExtData;


typedef struct
{
	BYTE cmd2;
	BYTE* lpdata;
}UpgradeExtData;

typedef struct
{
	BYTE protover;				//协议版本，目前为0x01
	BYTE productid;				//产品类型，地铁信号采集模块 0x31
	WORD cmdid2;				//二级命令
	WORD srcid;					//源ID
	WORD dstid;					//目标ID
	LPVOID lpdata;				//数据
}EqpProto2016;

union StCmdData {
	LPVOID data;
	D520ExtData d520ExtData;
	EqpProto2016 ep2016Data;
	UpgradeExtData upgradeExtData;
};

//数据帧结构(合并了1级结构和2级结构)
struct JHDEqpPktData
{
	BYTE head;					//帧头0x68
	StCtrlHead ctrlhead;		//命令控制头
	StCmdData cmdData;
	BYTE check;					//校验，从HEAD到DATA字节和
	BYTE tail;					//帧尾0x97

	EQP_PKT_TYPE GetPktType()
	{

	}

	JHDEqpPktData()
	{
		memset(this, 0, sizeof(JHDEqpPktData));
		head = 0x68;
		tail = 0x97;
	}
};

typedef struct {
	unsigned char Temp_V;
	unsigned char Temp_S;
	unsigned char Humi_V;
	unsigned char R;
}HJTEMP_HUMI, * LPHJTEMP_HUMI;


static unsigned short ccitt_table[256] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
	0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
	0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
	0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
	0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
	0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
	0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
	0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
	0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
	0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
	0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
	0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
	0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
	0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
	0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
	0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
	0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
	0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
	0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
	0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
	0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
	0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
	0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};



//伤损监测 大概420行 zgw
struct ChannelAcqInfo
{
	BYTE bySwitch;//0不采集 1采集
	WORD wAcqCurrent;//40mA
	BYTE reserve[6];

	ChannelAcqInfo()
	{
		memset(&reserve, 0, 6);
	}
};

struct DAMAGE_DETECT_WORK_PARAM
{
	DWORD dwStartFreq;//hz
	DWORD dwEndFreq;//hz
	DWORD dwFreqStep;//hz
	WORD wSetChannelWaitTime;//20s
	WORD wSetFreqWaitTime;//15s
	BYTE byFilterPointNum;//单次滤波点数5个
	WORD wAfterCrossWaitTime;//过车完等待时间10s
	WORD wCrossTimeOutLimit;//过车超时时间180s
	WORD wAcqOnePointInterval;//采集单点间隔1000ms
	WORD wZhendongStopDelayTime;//振动触发停止延长10s
	WORD wZhendongTriggerThreshold;//振动判断阈值2000mg
	WORD wOverCurrentThreshold;//电流源过流阈值1000mA
	ChannelAcqInfo tagChannelAcqInfo[8];
	BYTE reserve[32];

	DAMAGE_DETECT_WORK_PARAM()
	{
		memset(&reserve, 0, 32);
	}
};

struct ShangsunWorkparamsCmd
{
	BYTE byCmdType;//bit0：0读取1设置
	BYTE reserve[6];

	ShangsunWorkparamsCmd()
	{
		memset(&reserve, 0, 6);
	}
};

struct SetShangsunWorkparams
{
	ShangsunWorkparamsCmd tagShangsunWorkparamsCmd;
	DAMAGE_DETECT_WORK_PARAM tagDAMAGE_DETECT_WORK_PARAM;
};

//锁放工作参数
//1	1nv / fA	16	100uV / pA
//2	2nv / fA	17	200uV / pA
//3	5nv / fA	18	500uV / pA
//4	10nv / fA	19	1mV / nA
//5	20nv / fA	20	2mV / nA
//6	50nv / fA	21	5mV / nA
//7	100nv / fA	22	10mV / nA
//8	200nv / fA	 23	20mV / nA
//9	500nv / fA	24	50mV / nA
//10	1uV / pA	25	100mV / nA
//11	2uV / pA	26	200mV / nA
//12	5uV / pA	27	500mV / nA
//13	10uV / pA	28	1v / uA
//14	20uV / pA
//15	50uV / pA
struct LOCK_IN_AMPLIFIER_PARAM
{
	BYTE bySignalSource;//0外部 1内部 2内部扫频 默认1
	BYTE bySignalType;//0单频电压 1差分 2电流输入，放大10^6V/A 3电流输入，放大倍数10^8V/A 默认1
	BYTE bySensitivity;//默认25
	BYTE reserve[32];

	LOCK_IN_AMPLIFIER_PARAM()
	{
		bySignalSource = 1;
		bySignalType = 1;
		bySensitivity = 25;
		memset(&reserve, 0, 32);
	}
};

struct LockInWorkparamsReqCmd
{
	BYTE byCmdType;//bit0：0读取1设置
	BYTE reserve[6];

	LockInWorkparamsReqCmd()
	{
		memset(&reserve, 0, 6);
	}
};

struct LockInWorkparamsResCmd
{
	BYTE byCmdType;//bit0：0读取1设置
	BYTE bySyncToLockIn;//请求命令0
	BYTE reserve[6];

	LockInWorkparamsResCmd()
	{
		memset(&reserve, 0, 6);
	}
};

struct SetLockInWorkparams
{
	LockInWorkparamsReqCmd tagLockInWorkparamsCmd;
	LOCK_IN_AMPLIFIER_PARAM tagLOCK_IN_AMPLIFIER_PARAM;
};

//设置/读取振动传感器工作参数（命令码：0xB202）（这个废弃了 20220823 by zgw）
struct VIBRATION_PARAM
{

	BYTE control;//状态 0：禁用  1：使能
	WORD interval; //轨温采集周期 单位秒 目前没用到
	WORD x_limit;//x方向阈值
	WORD y_limit;//y方向阈值
	WORD z_limit;//z方向阈值

	BYTE reserve[6];

	VIBRATION_PARAM()
	{
		memset(&reserve, 0, 6);
	}
};

struct VibrationWorkparamsCmd
{
	BYTE byCmdType;//bit0：0读取1设置
	BYTE reserve[6];

	VibrationWorkparamsCmd()
	{
		memset(&reserve, 0, 6);
	}
};

struct OptVibrationSensorWorkparams
{
	VibrationWorkparamsCmd tagVibrationWorkparamsCmd;
	VIBRATION_PARAM tagVIBRATION_PARAM;
};

///ADXL345 振动模块
struct VIBRATION_ADXL345_PARAM
{
	BYTE control;//状态 0：禁用  1：使能
	WORD interval; //轨温采集周期 单位秒 目前没用到
	WORD dynamicLimit;//动态门限
	WORD staticLimit;//静态门限
	BYTE staticDur;//静态持续时间
	BYTE dynamicDur;//动态持续时间
	BYTE restart;//重启

	BYTE reserve[6];

	VIBRATION_ADXL345_PARAM() {
		memset(&reserve, 0, 6);
	}
};
struct Vibration_ADXL345_WorkparamsCmd
{
	BYTE byCmdType;//bit0：0读取1设置
	BYTE reserve[6];

	Vibration_ADXL345_WorkparamsCmd()
	{
		memset(&reserve, 0, 6);
	}
};

struct OptVibrationSensor_ADXL345_Workparams
{
	Vibration_ADXL345_WorkparamsCmd tagVibrationWorkparamsCmd;
	VIBRATION_ADXL345_PARAM tagVIBRATION_PARAM;
};
///

///begin 超声波伤损监测
//读取命令包
struct GET_REGISTER_PARAMS_cmd
{
	BYTE byCode;//0x03
	WORD wStartAddr;//寄存器起始地址
	WORD wRegNums;//寄存器数量
	BYTE reserve[6];

	GET_REGISTER_PARAMS_cmd()
	{
		memset(&reserve, 0, 6);
	}
};
//读取响应包
struct GET_REGISTER_PARAMS_res
{
	BYTE byRet; // 0 : 成功。 其他：失败
	BYTE byCode;//0x03
	WORD wSize;//寄存器总子节数
	WORD wRegVal[];//寄存器数组
};
//写入命令包
struct SET_REGISTER_PARAMS_cmd
{
	BYTE byCode;//0x10
	WORD wStartAddr;//寄存器起始地址
	WORD wRegNums;//寄存器数量
	WORD wSize;//寄存器总子节数
	WORD wRegVal[];//寄存器数组
};
//写入响应包
struct SET_REGISTER_PARAMS_res
{
	BYTE byRet; // 0 : 成功。 其他：失败
	BYTE byCode;//0x10
	WORD wStartAddr;//寄存器起始地址
	WORD wRegNums;//寄存器数量
};

//工作参数
struct SUPERSONIC_WORK_PARAM
{
	BYTE byEnable;//开始或结束采集
	DWORD dwCapInterval;//分机的采集间隔ms
	BYTE dwCapMode;//0、1
	BYTE dwCapChnl;//0~7
	BYTE dwRail;//0 尖轨，  1 心轨翼轨
	BYTE reserve[13];

	SUPERSONIC_WORK_PARAM(){
		memset(&reserve, 0, 13);
	}
};
struct GetWorkparams_cmd
{
	BYTE byCmdType;//bit0：固定为0 读取
	BYTE reserve[6];

	GetWorkparams_cmd()
	{
		memset(&reserve, 0, 6);
	}
};
struct GetWorkparams_res
{
	BYTE byRet;// 0 : 成功。 其他：失败
	BYTE byCmdType;//bit0：固定为0 读取
	SUPERSONIC_WORK_PARAM supersonic_work_param;
	BYTE reserve[6];

	GetWorkparams_res()
	{
		memset(&reserve, 0, 6);
	}
};
struct SetWorkparams_cmd
{
	BYTE byCmdType;//bit0：固定为1 设置
	SUPERSONIC_WORK_PARAM supersonic_work_param;
	BYTE reserve[6];

	SetWorkparams_cmd()
	{
		memset(&reserve, 0, 6);
	}
};
struct SetWorkparams_res
{
	BYTE byRet;// 0 : 成功。 其他：失败
	BYTE byCmdType;//bit0：固定为1 设置
	BYTE reserve[6];

	SetWorkparams_res()
	{
		memset(&reserve, 0, 6);
	}
};

struct GetChannelRelay_cmd
{
	BYTE byCmdType;//bit0：固定为0 读取
	BYTE reserve[6];

	GetChannelRelay_cmd()
	{
		memset(&reserve, 0, 6);
	}
};
struct GetChannelRelay_res
{
	BYTE byRet;// 0 : 成功。 其他：失败
	BYTE byCmdType;//bit0：固定为0 读取
	BYTE channel[2];//"c1" "c2" "c3"...... "c8" :当前仪表所接通的继电器通道  "cn":channel none 一个通道都没接通
	BYTE reserve[6];

	GetChannelRelay_res()
	{
		memset(&reserve, 0, 6);
	}
};
struct SetChannelRelay_cmd
{
	BYTE byCmdType;//bit0：固定为1 设置
	BYTE channel[2];//当前仪表所要接通或关闭的继电器通道 "c1" "c2" "c3"...... "c8"  "ca":channel all 全部通道
	BYTE action[4];//"open" ：打开某通道 channel字段不能为"ca"; "clos"： 关闭某通道或关闭所有通道
	BYTE reserve[6];

	SetChannelRelay_cmd()
	{
		memset(&reserve, 0, 6);
	}
};
struct SetChannelRelay_res
{
	BYTE byRet;// 0 : 成功。 其他：失败
	BYTE byCmdType;//bit0：固定为1 设置
	BYTE reserve[6];

	SetChannelRelay_res()
	{
		memset(&reserve, 0, 6);
	}
};

struct SupersonicChannelCfg
{
	BYTE enableAcq;//0不采集 1采集
	short scanDA;//扫描DA
	short centralFrq;//中央频率
	short singleNums;//扫描单边个数
	short step;//每次偏移量大小
	short mode;//模式
	BYTE reserve[6];

	SupersonicChannelCfg()
	{
		memset(&reserve, 0, 6);
	}
};

struct DEVICE_CFG_PARAM_LIST
{
	SupersonicChannelCfg channelCfg[8];
	BYTE reserve[8];

	DEVICE_CFG_PARAM_LIST()
	{
		memset(&reserve, 0, 8);
	}
};
struct GetCfgParamList_cmd
{
	BYTE byCmdType;//bit0：固定为0 读取
	BYTE reserve[6];

	GetCfgParamList_cmd()
	{
		memset(&reserve, 0, 6);
	}
};
struct GetCfgParamList_res
{
	BYTE byRet;// 0 : 成功。 其他：失败
	BYTE byCmdType;//bit0：固定为0 读取
	DEVICE_CFG_PARAM_LIST cfgParamList;
	BYTE reserve[6];

	GetCfgParamList_res()
	{
		memset(&reserve, 0, 6);
	}
};
struct SetCfgParamList_cmd
{
	BYTE byCmdType;//bit0：固定为1 设置
	DEVICE_CFG_PARAM_LIST cfgParamList;
	BYTE reserve[6];

	SetCfgParamList_cmd()
	{
		memset(&reserve, 0, 6);
	}
};
struct SetCfgParamList_res
{
	BYTE byRet;// 0 : 成功。 其他：失败
	BYTE byCmdType;//bit0：固定为1 设置
	DEVICE_CFG_PARAM_LIST cfgParamList;
	BYTE reserve[6];

	SetCfgParamList_res()
	{
		memset(&reserve, 0, 6);
	}
};

//开始升级超声仪表        0xB505
typedef struct
{
	DWORD fsize;		//主程序大小
	WORD crc16;		//文件CRC16校验码
	BYTE IntVec[32];  //中断向量数据 固定32字节
	WORD psize;			//协议分包大小
	
	BYTE reserve[16];	//保留  
}StChaoShengYBStartUpgradeReq; 

typedef struct
{
	BYTE ret;			//0 : 成功。  其他：失败的错误码
	BYTE reserve[14];	//保留 
}StChaoShengYBStartUpgradeRes;


//发送超声仪表主程序分包          0xB506
typedef struct
{
	WORD id;			//包编号，从0开始。
	WORD count;			//总包数
	WORD size;			//包长度
	WORD crc16;			//包内容CRC16校验码
	LPBYTE data;		//分包内容
}StChaoShengYBUpgradePkg;

typedef struct
{
	WORD pkg_index;			//包编号，从0开始。
	BYTE res[2];			//保留
}StChaoShengYBUpgradePkgRes;

//执行超声仪表升级            0xB507
typedef struct
{
	WORD processWay;		//升级后处理。	0，自动重启。1，运行灯闪烁，10分钟后再自动重启。默认0
	BYTE reserve[12];	//保留
}StChaoShengYBExeUpgradeReq;

typedef struct
{
	BYTE ret;		//0：校验成功,开始升级  1：校验失败，需要重新发送文件	2：升级包不完整
	BYTE reserve[4];	//保留
}StChaoShengYBExeUpgradeRes;


//超声仪表升级状态报告        0xB508
typedef struct
{
	WORD status;		//0，升级成功。其它，升级失败
	BYTE reserve[16];	//保留
}StChaoShengYBExeUpgradeResult;

///end 超声波伤损监测


//--HJ设备通信协议
enum JHD_EQP_PROTO_VER {
	EPV_UNKNOWN,
	EPV0,
	EPV1,
	EPV2,
};


//心跳    0xF000
typedef struct {
	TIME clock;		//时钟
	BYTE bEnableAcq;	//默认都为1
	BYTE reserve[3];	//保留
}StHeartBeat;


//开始升级软件        0xF005
typedef struct
{
	DWORD softver;		//服务端软件版本
	DWORD fsize;		//升级文件大小
	WORD psize;			//协议分包大小
	DWORD crc32;		//文件CRC32校验码
	BYTE reserve[16];	//保留
}StStartUpgradeReq;

typedef struct
{
	DWORD softver;		//软件版本信息
	DWORD hardver;		//硬件版本信息
	WORD psize;			//协议分包大小
	BYTE reserve[22];	//保留
}StStartUpgradeRes;


//发送升级包          0xF006
typedef struct
{
	WORD id;			//包编号，从0开始。
	WORD count;			//总包数
	WORD size;			//包长度
	WORD Nsize;			//包长度校验，Size取反
	WORD crc16;			//包内容CRC16校验码
	LPBYTE data;		//分包内容
}StUpgradePkg;

typedef struct
{
	WORD pkg_index;			//包编号，从0开始。
	BYTE res[2];			//保留
}StUpgradePkgRes;

//执行升级            0xF007
typedef struct
{
	WORD process;		//升级后处理。	0，自动重启。1，运行灯闪烁，10分钟后再自动重启。默认0
	BYTE reserve[28];	//保留
}StUpgradeReq;

typedef struct
{
	BYTE success;		//0：校验成功,开始升级  1：校验失败，需要重新发送文件	2：升级包不完整
	DWORD softver;		//即将升级的软件版本
	WORD pcount;		//缺失包总数，最大为10。当Success为2时有效
	WORD p1;			//缺失包1，当P_Count大于等于1时有效
	WORD p2;			//缺失包2，当P_Count大于等于2时有效
	WORD p3;			//缺失包3，当P_Count大于等于3时有效
	WORD p4;			//缺失包4，当P_Count大于等于4时有效
	WORD p5;			//缺失包5，当P_Count大于等于5时有效
	WORD p6;			//缺失包6，当P_Count大于等于6时有效
	WORD p7;			//缺失包7，当P_Count大于等于7时有效
	WORD p8;			//缺失包8，当P_Count大于等于8时有效
	WORD p9;			//缺失包9，当P_Count大于等于9时有效
	WORD p10;			//缺失包10，当P_Count大于等于10时有效
	BYTE reserve[4];	//保留
}StUpgradeRes;


//升级状态报告        0xF008
typedef struct
{
	WORD status;		//0，升级成功。其它，升级失败
	BYTE reserve[16];	//保留
}StUpgradeResult;

#pragma pack(pop)


struct CMD_IDENT {
	int cmd1;
	int cmd2;
};


//用法： 该类的内部数据分结构化数据和序列化数据两部分
//Pack接口用于   结构化数据->序列化数据转换
//UnPack接口用于 序列化数据->结构化数据转换
class JHDEqpPkt : public DEV_PKT, public JHDEqpPktData
{
public:
	JHDEqpPkt();
	virtual ~JHDEqpPkt();

	JHDEqpPkt& operator=(const JHDEqpPkt& right);

	string PktDataToString();
	BOOL InitFrame(JHDEqpPkt&);
	bool unpack(unsigned char* pBuf, int iBufLen, bool bGetCmdInfo = false) override;
	BOOL unpack(JHDEqpPkt&, LPVOID buf, int len);//解析数据包
	//BOOL unpack(CByteArray& aryData);
	//BOOL unpack() override;
	bool pack(BYTE* buf, int iLen);//封装数据包
	bool pack();
	//BOOL Pack(CByteArray& aryData);
	BOOL Release(JHDEqpPkt&, int dir = 0);//释放内存，dir: 数据包方向  0-发出数据包  1-收到数据包

	string GetPktDesc() override;
	static BOOL IsValidPkt(BYTE* pData, int iLen, int& iPktLen);
	static size_t IsValidPkt_JEP(unsigned char* pData, size_t iLen);
	//BOOL ExtractOnePacket(CByteArray& aryData);


	BOOL ExtractOnePacket(BYTE* pData, int& iLen, BOOL bDelFromSrc = TRUE);
	BOOL ExtractOnePacket(BYTE* pData, DWORD& dwLen);
	void GetCmdID(int& cmd1, int& cmd2);

	string GetCmdID() override;
	static BOOL GetFrameData(LPBYTE buf, int bufLen, int startCheckPos, int& framePos, int& frameLen, BOOL& continueCheck);
	static DWORD GetCmdCode(const JHDEqpPkt&);//获取命令吗

	string GetCmdName();
	static string GetCmdDesc_EPV1(int cmd_v1, JHDEqpPkt* pPkt = NULL);
	static string GetCmdDesc_EPV2(int cmd_v2, JHDEqpPkt* pPkt = NULL);
	static BOOL IsSameCmd(JHDEqpPkt& pkt1, JHDEqpPkt& pkt2);
	static BOOL IsSameCmd(CMD_IDENT id, JHDEqpPkt& pkt);
	static BOOL IsSameCmd(BYTE* pReq, int iReqLen, BYTE* pResp, int iRespLen);

	BYTE* m_CmdBuf;
	int m_iCmdBufLen;

	string strPktData;//方便调试使用，查看数据包
	string strCmdData;
	string m_strUnpackErrorInfo;//记录解包失败的情况

	JHD_EQP_PROTO_VER GetVer();

	//2016版采集协议扩展
	//2016版常用处理 Data Format Transfer 系列函数
	static BOOL DFT_PicSize(int iPixelType, int& w, int& h);
	//static string DFT_EqpSplitStr(string strSplit);
	//static string SplitHexStrTostring(string str);
	static WORD ShortToJHDShort(short s); //最高位D15为符号位，0为正，1为负
	static short JHDShortToShort(WORD s);
	static DWORD IPToJHDDWORD(string strIp);
	static string JHDDWORDToIP(DWORD ip);
	static BYTE GetCheckSum(PBYTE pBuf, DWORD dwDataLen);


	//公共部分命令
	//BOOL Pack_V2_GETBUSADDRESS(int iDesID);
	//BOOL Pack_V2_GETDEVINFO(int iDesID);
	void Pack_V2_HEARTBEAT(int iDesID,int srcId, bool bEnableAcq = true);

	
	void Pack_V2_RestartDev_AD0C(int iDesID);
	void Pack_V2_StressTest_AD90(int iDesID);
	void Pack_V2_GetCommonRealAD_Req(int iDesID);


	//低速载波升级命令
	BOOL Pack_V0Upgrade_SendBinData(int iDesID, REQ_UPGRADE_FILE_DATA cmd);
	BOOL Pack_V0Upgrade(int iDesID, int cmd2, BYTE* pCmd, int iCmdLen);
	BOOL Pack_V0Upgrade_CheckState(int iDesID, REQ_STATE_CHECK cmd);
	BOOL Pack_V0Upgrade_Start(int iDesID);

	BOOL Pack_V2ShangSun_Params(int iDesID, int iSrcId, int iType, DAMAGE_DETECT_WORK_PARAM tagDAMAGE_DETECT_WORK_PARAM);
	BOOL Pack_V2ShangSun_LockIn_Params(int iDesID, int iSrcId, int iType, LOCK_IN_AMPLIFIER_PARAM tagLOCK_IN_AMPLIFIER_PARAM);
	BOOL Pack_V2ShangSun_AE_ACQ(int iDesID, int iSrcId, BYTE* pCmd, int iCmdLen);
	BOOL Pack_V2ShangsunReg_Get(int iDesID, int iSrcId, int iType, GET_REGISTER_PARAMS_cmd getRegParams);
	BOOL Pack_V2ShangsunReg_Set(int iDesID, int iSrcId, int iType, BYTE* pData, int nLen);
	BOOL Pack_V2Vibration_Params(int iDesID, int iSrcId, int iType, VIBRATION_PARAM tagVIBRATION_PARAM);
	BOOL Pack_V2Vibration_ADXL345_Params(int iDesID, int iSrcId, int iType, VIBRATION_ADXL345_PARAM tagVIBRATION_PARAM);

	BOOL Pack_V2Supersonic_Get(int iDesID, int iSrcId, int iType, GET_REGISTER_PARAMS_cmd getRegParams);//超声波伤损
	//BOOL JHDEqpPkt::Pack_V2Supersonic_Set(int iDesID, int iType, SET_REGISTER_PARAMS_cmd setRegParams);//超声波伤损
	BOOL Pack_V2Supersonic_Set(int iDesID, int iSrcId, int iType, BYTE* pData, int nLen);//超声波伤损
	BOOL Pack_V2Supersonic_GetWorkParam(int iDesID, int iSrcId, int iType, GetWorkparams_cmd getWorkParams);//超声波伤损
	BOOL Pack_V2Supersonic_SetWorkParam(int iDesID, int iSrcId, int iType, BYTE* pData, int nLen);//超声波伤损
	BOOL Pack_V2Supersonic_GetChannelRelay(int iDesID, int iSrcId, int iType, GetChannelRelay_cmd param);
	BOOL Pack_V2Supersonic_SetChannelRelay(int iDesID, int iSrcId, int iType, BYTE* pData, int nLen);
	BOOL Pack_V2Supersonic_GetCfgParamList(int iDesID, int iSrcId, int iType, GetCfgParamList_cmd param);
	BOOL Pack_V2Supersonic_SetCfgParamList(int iDesID, int iSrcId, int iType, BYTE* pData, int nLen);
	//超声仪表升级 就是把通用固件升级改了改 专用于超声仪表 zgw
	BOOL Pack_V2YiBiaoUpgrade_PrepareUpgrade(int iDesID, int iSrcId, StChaoShengYBStartUpgradeReq upgrade_PARAM, int product_type);
	BOOL Pack_V2YiBiaoUpgrade_SendUpgradeData(int iDesID, int iSrcId, StChaoShengYBUpgradePkg upgrade_PARAM, int product_type);
	BOOL Pack_V2YiBiaoUpgrade_UpgradeReq(int iDesID, int iSrcId, StChaoShengYBExeUpgradeReq upgrade_PARAM, int product_type);

	//通用固件升级相关  超声波伤损 锁放伤损 其他设备没测
	BOOL Pack_V2GeneralUpgrade_PrepareUpgrade(int iDesID, StStartUpgradeReq supersonic_PARAM, int product_type);
	BOOL Pack_V2GeneralUpgrade_SendUpgradeData(int iDesID, StUpgradePkg upgrade_PARAM, int product_type);
	BOOL Pack_V2GeneralUpgrade_UpgradeReq(int iDesID, StUpgradeReq upgrade_PARAM, int product_type);




	//--生产工具使用,下送命令已不区分产品类型码
	//--产品码可以任意填充,只是为了兼顾代码整洁  填充公共命令产品类型码和设备产品类型码  liaohh

	//根据高速载波2016设备的2级命令封装数据包
	BOOL PackByGSZB2016Cmd2(UINT cmd2, LPVOID lpdata, int len, UINT productid = 0, UINT dstid = 0xFFFF);

	string GetCmdDesc_EPV0_Upgrade(int cmd_upgrade, JHDEqpPkt* pPkt);

	//设置二级命令中的默认产品类型码（为了减少改动，避免每次封闭数据包都要指定产品类型）
	static UINT SetDefaultProductID(UINT productid);
	//获取二级命令中的默认产品类型码
	static UINT GetDefaultProductID();
	//--

	void LogCommData(BOOL bSendOrRecv, void* pData, int iLen, int iID, string strIP, BOOL bLogCommData = FALSE);

	void ClearData();

private:
	inline void PackGSZB2016ExtOneLevelCmdDefault(int iDesID) {
		head = 0x68;
		tail = 0x97;
		ctrlhead.dir = EQP_PKT_DIR_TOEQP;
		ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
		ctrlhead.ceid = iDesID;
		ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	}

	inline void PackGSZB2016ExtTweLevelCmdDefault() {
		cmdData.ep2016Data.protover = 0x01;
		cmdData.ep2016Data.srcid = 0xFFFF;
	}

	inline void PackGSZB2016ExtSetProductId(BYTE id) {
		cmdData.ep2016Data.productid = id;
	}

	inline void PackGSZB2016ExtSetDstId(WORD dstid) {
		cmdData.ep2016Data.dstid = dstid;
	}

	inline void PackGSZB2016ExtSetOperaCmd(WORD cmd) {
		cmdData.ep2016Data.cmdid2 = cmd;
	}

private:
	//二级命令中的默认产品类型码，默认为缺口类型
	static UINT g_productid;
};
#endif
#endif /* TDS_NOT_USED_JHDEQPPROTOCOL_H */
