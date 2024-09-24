#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_dcqk.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "base64.h"
#include "mp.h"
#include "rpcHandler.h"
#include "as.h"


using namespace httplib;

namespace ns_ioDev_dcqk {
	ioDev* createDev()
	{
		return new ioDev_dcqk();
	}
	class createReg{
	public:
		createReg() {
			mapDevCreateFunc["dcqk-sys-device"] = createDev;
			mapDevTypeLabel["dcqk-sys-device"] = "道岔缺口站机";
		};
	};
	createReg reg;
}

void ThreadSaveGapAndPic(void* lpParam);


ioDev_dcqk::ioDev_dcqk()
{
	m_devType = "dcqk-sys-device";
	m_devTypeLabel = "道岔缺口站机";
	m_level = "devcie";
}

ioDev_dcqk::~ioDev_dcqk()
{
	stop();
}


void ioDev_dcqk::DoAcq()
{

}



void ioDev_dcqk::DoCycleTask()
{
	if (timeopt::CalcTimePassSecond(m_stLastHeartbeatTime) > ioDev::m_heartBeatInterval) {
		//SendHeartbeat();
		m_stLastHeartbeatTime = timeopt::now();
	}
}

void ioDev_dcqk::onEvent_online()
{
	if (m_mapSIDToName.empty())
	{
		//	获取缺口配置
		GetGapCfg();
	}
}


size_t IsValidPkt_315(BYTE* pData, size_t iLen)
{
	if (iLen < 16) return 0;//315协议数据包最短长度

	if (pData[0] != 0x71 || pData[1] != 0x6B || pData[2] != 0x6E || pData[3] != 0x65 || pData[4] != 0x74) return 0;//帧头校验

	if (pData[5] != 0x02 && pData[5] != 0x80) return 0;

	auto a = pData[7];
	if (a != FRAME_TYPE_JSON && a != FRAME_TYPE_DATA && a != FRAME_TYPE_HEARTBEAT) return 0;

	DWORD frameLen = *(DWORD*)(pData + 8);
	if (frameLen + 16 > iLen) return 0;

	BYTE* pFrameEnd = pData + frameLen + 12;
	if (pFrameEnd[0] != 0xFF || pFrameEnd[1] != 0xFF || pFrameEnd[2] != 0xFF || pFrameEnd[3] != 0xFF)
		return 0;

	return frameLen + 16;
}

void ioDev_dcqk::OnRecvData_TCPClient(unsigned char* pData, size_t len, tcpSessionClt* connInfo)
{
	stream2pkt* pab = &m_pab;
	pab->PushStream(pData, len);

	while (pab->PopPkt(IsValidPkt_315))
	{
		if (pab->abandonData != "")
		{
			string remoteAddr = getDevAddrStr();
			LOG("[warn]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);
			m_abandonLen += pab->iAbandonLen;
		}
		onRecvPkt(pab->pkt, pab->iPktLen);
	}
	while (pab->PopPkt(IsValidPkt_315))
	{

	}
}

bool ioDev_dcqk::onRecvData(unsigned char* pData, size_t iLen) {

	return false;
}

bool ioDev_dcqk::onRecvPkt(unsigned char* pData, size_t iLen)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	if (Parse315Protocol::Parse(data, LPVOID(pData), int(iLen)))
	{
		DealJHDData(&data);
	}
	else
	{
		return false;
	}

	Parse315Protocol::Release(data, 1);//JHD上送的必须注意释放内存

	return true;
}

int ioDev_dcqk::DealJHDData(LPVOID lpParam)
{
	StFrame* pData = (StFrame*)lpParam;

#ifdef _DEBUG
	OutputDebugString(Parse315Protocol::ToString(*pData, 1).c_str());
#endif

	switch (pData->ftype)
	{
	case FRAME_TYPE_HEARTBEAT:
	{
		StHeartBeat315* basic = (StHeartBeat315*)pData->lpdata;
		SendCallBackHeart(basic);
		break;
	}
	case FRAME_TYPE_DATA:
	{
		StDataBasic* basic = (StDataBasic*)pData->lpdata;
		switch (basic->cmdid)
		{
		case CMD_CODE_GAPCFG:
		{
			StGapCfgRes* lpsubdata = (StGapCfgRes*)pData->lpdata;

			for (int k = 0; k < lpsubdata->cfgcnt; k++) {

				StSwitchCfg& ZZJConf = ((StSwitchCfg*)lpsubdata->lpcfg)[k];

				char zzjName[256];
				memcpy(zzjName, ZZJConf.lpname, ZZJConf.nlen);
				zzjName[ZZJConf.nlen] = '\0';

				map<int, string>::iterator iter = m_mapSIDToName.find(ZZJConf.sid);
				if (iter == m_mapSIDToName.end())
				{
					m_mapSIDToName[ZZJConf.sid] = zzjName;
				}
			}

			break;
		}
		case CMD_CODE_GAPVAL:
		{
			StGapValue* lpsubdata = (StGapValue*)pData->lpdata;
			Do_CMD_CODE_GAPVAL(lpsubdata);
			break;
		}
		case CMD_CODE_ALARM_AND_IMG:
		case CMD_CODE_ALARM:	//0x97
		{
			StAlarmAndImgInfo* lpsubdata = (StAlarmAndImgInfo*)pData->lpdata;
			SendCallBack0x27(lpsubdata);

			map<int, string>::iterator iter = m_mapSIDToName.find(lpsubdata->sid);
			if (iter == m_mapSIDToName.end())
				return 0;

			string sZZJName = iter->second;

			//size_t iPos = sZZJName.find("#");
			//string sTmp = ".";
			//if (iPos == string::npos) {
			//	iPos = sZZJName.find("J");
			//	sTmp = "#.";
			//}
			size_t iPos = 0;
			string sTmp = ".";
			if (sZZJName.find("#") != string::npos) {
				iPos = sZZJName.find("#");
				iPos += 1;
			}
			else if (sZZJName.find("J") != string::npos) {
				iPos = sZZJName.find("J");
				sTmp = "#.";
			}
			else if (iPos = sZZJName.find("X") != string::npos) {
				iPos = sZZJName.find("X");
				sTmp = "#.";
			}
			else if (iPos = sZZJName.find("P") != string::npos) {
				iPos = sZZJName.find("P");
				sTmp = "#.";
			}
			else if (iPos = sZZJName.find("W") != string::npos) {
				iPos = sZZJName.find("W");
				sTmp = "#.";
			}
			
			string sDaoChaName = sZZJName.substr(0, iPos)+ sTmp;


			BYTE* cbFill = ((BYTE*)&lpsubdata->filldata);
			if (basic->cmdid == CMD_CODE_ALARM_AND_IMG)
			{
				cbFill[0]++;
			}
			json jParams;
			if (basic->cmdid == CMD_CODE_ALARM)
			{
				jParams["desc"] = GetAlarmDesc(*lpsubdata);
			}
			else
				jParams["desc"] = "";

			jParams["level"] = GetAlarmLevelType(lpsubdata->alarmtype);
			jParams["tag"] = m_strTagBind + "." + sDaoChaName+sZZJName;
			jParams["type"] = GetAlarmType(lpsubdata->alarmtype, cbFill[0]);
			jParams["id"] = "";
			almServer* pAlmSrv = &almSrv;
			RPC_RESP resp;
			

			//"time":"2023-08-08 11:12:23",
			//	"tag" : "1幢B6区3层",
			//	"type" : "感烟探测器",
			//	"id" : "69001"
			if (IsRecover(lpsubdata->alarmtype))
			{
				TIME tm = timeopt::Unix2SysTime(lpsubdata->alarmconfirm);
				string sTime = timeopt::stTimeToStr(tm);
				jParams["time"] = sTime.c_str();

				tm = timeopt::Unix2SysTime(lpsubdata->time);
				sTime = timeopt::stTimeToStr(tm);
				jParams["recoverTime"] = sTime.c_str();

				pAlmSrv->rpc_recoverAlarm(jParams, resp);
			}
			else
			{
				TIME tm = timeopt::Unix2SysTime(lpsubdata->time);
				string sTime = timeopt::stTimeToStr(tm);
				jParams["time"] = sTime.c_str();


				pAlmSrv->rpc_addAlarm(jParams, resp, FALSE);
			}

			break;
		}
		case CMD_CODE_ACTION_INFO:
		{
			StActionInfo* lpsubdata = (StActionInfo*)pData->lpdata;

			break;
		}
		case CMD_CODE_LASTGAPIMG:
		{
			StLastGapImgRes* lpsubdata = (StLastGapImgRes*)pData->lpdata;
			break;
		}
		case CMD_CODE_IMGLIST:
		{
			StImgListRes* lpsubdata = (StImgListRes*)pData->lpdata;
			break;
		}
		case CMD_CODE_IMGINFO:
		{
			StImgInfoRes* lpsubdata = (StImgInfoRes*)pData->lpdata;
			break;
		}
		case CMD_CODE_VEDIOLIST:
		{
			StVedioListRes* lpsubdata = (StVedioListRes*)pData->lpdata;
			break;
		}
		case CMD_CODE_VEDIOFILE:
		{
			//if (Parse315Protocol::b2Flen2byte)
			//{
			//	DealVedioFile((StVedioFileRes2*)pData->lpdata);
			//}
			//else
			//{
			//	DealVedioFile((StVedioFileRes4*)pData->lpdata);
			//}

			break;
		}
		case CMD_CODE_YYQX:
		{
			StOilPreCurve* lpsubdata = (StOilPreCurve*)pData->lpdata;
			Do_CMD_CODE_YYQX(lpsubdata);

			SendCallBack0x25(lpsubdata); //0X25命令需要回执信息
			break;
		}
		case CMD_CODE_YWINFO:
		{
			StOilLevelInfo* lpsubdata = (StOilLevelInfo*)pData->lpdata;
			Do_CMD_CODE_YWINFO(lpsubdata);
			break;
		}
		case CMD_CODE_REALCTRL:
		{
			StRealCtrlRes* lpsubdata = (StRealCtrlRes*)pData->lpdata;

			break;
		}
		case CMD_CODE_REALSTREAM:
		{
			//StRealStream* lpsubdata = (StRealStream*)pData->lpdata;

			//StVedioFrame vdata;
			//ZeroMemory(&vdata, sizeof(vdata));
			//memcpy_s(vdata.fheader, 5, FRAME_HEADER_315, 5);
			//vdata.ftail = FRAME_TAIL_315;

			//StVedioRealPlay vsubdata;
			//ZeroMemory(&vsubdata, sizeof(vsubdata));
			//vsubdata.cmdid = VEDIO_REAL_STREAM;
			//vsubdata.type = lpsubdata->packid;//填充角度
			//vsubdata.sid = lpsubdata->sid;
			//vsubdata.len = (WORD)lpsubdata->packlen;
			//vsubdata.lpdata = lpsubdata->lpdata;

			//vdata.datalen = 1 + 1 + 2 + 2 + vsubdata.len;
			//vdata.lpdata = &vsubdata;

			//LPVOID buf = NULL;
			//int len = 0;
			//CVedioParser::Unparse(vdata, buf, len);
			//SendPlayer(buf, len);


			//CString strLog, strData;
			//SYSTEMTIME rTime;
			//GetLocalTime(&rTime);
			//ByteArrayToSpaceString((char*)buf, len, strData);
			//strLog.Format("%02d:%02d:%02d.%03d %s\r\n", rTime.wHour, rTime.wMinute, rTime.wSecond, rTime.wMilliseconds, strData);

			//CFile f;
			//CString strCommPath = "E:\\CommLog.txt";
			//if (f.Open(strCommPath, CFile::modeCreate | CFile::modeReadWrite | CFile::modeNoTruncate))
			//{
			//	f.SeekToEnd();
			//	f.Write(strLog.GetBuffer(), strLog.GetLength());
			//	f.Close();
			//}

			break;
		}
		case CMD_CODE_POWERLIST:
		{
			StPowerListRes* lpsubdata = (StPowerListRes*)pData->lpdata;
			break;
		}
		case CMD_CODE_QUERY_POWER_FILE:
		{
			StPowerListRes* lpsubdata = (StPowerListRes*)pData->lpdata;
			break;
		}
		case (int)E_315_PROTOCOL_TYPE::UN_RECOVER_ALARM_0x65:
		{
			StAlarmListReq* lpsubdata = (StAlarmListReq*)pData->lpdata;
			break;
		}

		case  CMD_CODE_ELECCURVE:
		{
			StElecCurve* lpsubdata = (StElecCurve*)pData->lpdata;
			SendCallBack0x41(lpsubdata);
			break;
		}
		//case (uint8_t)E_315_PROTOCOL_TYPE::GONGKUANG_REAL_VAL_0x81:
		//{
		//	StWorkingConditionValRes* lpsubdata = (StWorkingConditionValRes*)pData->lpdata;
		//	m_dlgDataView.SetData(*pData);
		//	break;
		//}
		default:
			break;
		}
		break;
	}
	default:
		break;
	}

	return 0;
}

string ioDev_dcqk::GetAlarmLevelType(BYTE type)
{
	if (type == 1 || type == 9 || type == 11 || type == 16 
		|| type == 101 || type == 109 || type == 111 || type == 116)
		return "预警";
	else if (type == 2 || type == 7 || type == 8 || type == 10 || type == 12 || type == 15 || type == 17 ||type == 65
		|| type == 102 || type == 107 || type == 108 || type == 110 || type == 112 || type == 115 || type == 117 || type == 165)
		return "告警";
	
	return "";
}

string ioDev_dcqk::GetAlmLevel(BYTE type)
{
	if (type == 1) return "二级";
	else return "三级";
}

string ioDev_dcqk::GetAlarmType(BYTE type, BYTE type1)
{
	string strDesc = "";
	switch (type) {
	case 1:
		{
			if (type1 == 2)
				strDesc = "扳动采集缺口预警";
			else if (type1 == 3)
				strDesc = "过车采集缺口预警";
			else
				strDesc = "周期采集缺口预警";
		}
		break;
	case 2:
		{
			if (type1 == 2)
				strDesc = "扳动采集缺口告警";
			else if (type1 == 3)
				strDesc = "过车采集缺口告警";
			else
				strDesc = "周期采集缺口告警";
		}
		break;
	case 7:
		strDesc = "温度告警";
		break;
	case 8:
		strDesc = "湿度告警";
		break;
	case 9:
		strDesc = "油位预警";
		break;
	case 10:
		strDesc = "油位告警";
		break;
	case 11:
		strDesc = "油压预警";
		break;
	case 12:
		strDesc = "油压告警";
		break;
	case 15:
		strDesc = "过车时缺口告警";
		break;
	case 16:
		strDesc = "静态缺口预警";
		break;
	case 17:
		strDesc = "静态缺口告警";
		break;
	case 65:
		strDesc = "锁舌锁块告警";
		break;
	case 101:
		{
			if (type1 == 2)
				strDesc = "扳动采集缺口预警恢复";
			else if (type1 == 3)
				strDesc = "过车采集缺口预警恢复";
			else
				strDesc = "周期采集缺口预警恢复";
		}
		break;
	case 102:
		{
			if (type1 == 2)
				strDesc = "扳动采集缺口告警恢复";
			else if (type1 == 3)
				strDesc = "过车采集缺口告警恢复";
			else
				strDesc = "周期采集缺口告警恢复";
		}
		break;
	case 107:
		strDesc = "温度告警恢复";
		break;
	case 108:
		strDesc = "湿度告警恢复";
		break;
	case 109:
		strDesc = "油位预警恢复";
		break;
	case 110:
		strDesc = "油位告警恢复";
		break;
	case 111:
		strDesc = "油压预警恢复";
		break;
	case 112:
		strDesc = "油压告警恢复";
		break;
	case 115:
		strDesc = "过车时缺口告警恢复";
		break;
	case 116:
		strDesc = "静态缺口预警恢复";
		break;
	case 117:
		strDesc = "静态缺口告警恢复";
		break;
	case 165:
		strDesc = "锁舌锁块告警恢复";
		break;
	}
	return strDesc;
}

string ioDev_dcqk::GetAlarmDesc(const StAlarmAndImgRec& data)
{
	string strRst;
	TCHAR buf[256] = { 0 };
	BYTE* cbFill = ((BYTE*)&data.filldata);
	//温湿度
	if (data.alarmtype == ALARM_TYPE_TEMPERATURE || data.alarmtype == ALARM_TYPE_WDBJ
		|| data.alarmtype == ALARM_TYPE_HUMILITY || data.alarmtype == ALARM_TYPE_SDBJ
		|| data.alarmtype == ALARM_TYPE_TEMPERATUREHF || data.alarmtype == ALARM_TYPE_WDBJHF
		|| data.alarmtype == ALARM_TYPE_HUMILITYHF || data.alarmtype == ALARM_TYPE_SDBJHF)
	{

		tstring sunit = (data.alarmtype % 10 == 7 ? ("℃") : ("%"));

		strRst += (data.alarmtype % 10 == 7 ? ("温度:") : ("湿度:"));

		sprintf_s(buf, ("(%.2f%s)"), ((float)data.gap) / 100.0, sunit.c_str());
		strRst += buf;

		if (data.alarmtype <= 100)
		{
			if (data.lrsign == 1)
			{
				sprintf_s(buf, ("大于%s上限(%.2f%s)"), GetAlarmLevelType(data.alarmtype).c_str(), ((float)data.std) / 100.0, sunit.c_str());
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				sprintf_s(buf, ("小于%s下限(%.2%s)"), GetAlarmLevelType(data.alarmtype).c_str(), ((float)data.offset) / 100.0, sunit.c_str());
				strRst += buf;
			}
		}

		return strRst;
	}
	//油位报警
	else if (data.alarmtype == ALARM_TYPE_YWYJ || data.alarmtype == ALARM_TYPE_YWYJHF
		|| data.alarmtype == ALARM_TYPE_YWBJ || data.alarmtype == ALARM_TYPE_YWBJHF)
	{
		sprintf_s(buf, ("油位(%dmm)"), data.gap);
		strRst += buf;
		if (data.alarmtype <= 100)
		{
			if (data.lrsign == 1)
			{
				sprintf_s(buf, ("大于%s上限(%dmm)"), GetAlarmLevelType(data.alarmtype).c_str(), data.std);
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				sprintf_s(buf, ("小于%s下限(%dmm)"), GetAlarmLevelType(data.alarmtype).c_str(), data.offset);
				strRst += buf;
			}
		}
		return strRst;
	}
	//油压报警
	else if (data.alarmtype == ALARM_TYPE_POWERYJHF || data.alarmtype == ALARM_TYPE_POWERBJHF
		|| data.alarmtype == ALARM_TYPE_POWERYJ || data.alarmtype == ALARM_TYPE_POWERBJ)
	{
		strRst += ("，");

		if (data.fixorinvert == 1)
			strRst += ("定到反");
		else
			strRst += ("反到定");

		strRst += (":");

		switch (cbFill[0])
		{
		case 1:
			strRst += ("解锁阶段");
			break;
		case 2:
			strRst += ("动作阶段");
			break;
		case 3:
			strRst += ("锁闭阶段");
			break;
		case 4:
			strRst += ("释压阶段");
			break;
		default:
			break;
		}
		sprintf_s(buf, ("油压最大值(%.2fMPa)"), ((float)data.gap) / 100.0);
		strRst += buf;
		if (data.alarmtype <= 100)
		{
			if (data.lrsign == 1)
			{
				sprintf_s(buf, ("大于上限(%.2fMPa)"), ((float)data.std) / 100.0);
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				sprintf_s(buf, ("小于下限(%.2fMPa)"), ((float)data.offset) / 100.0);
				strRst += buf;
			}
		}
		return strRst;
	}
	else if (data.alarmtype == ALARM_TYPE_QKYJ || data.alarmtype == ALARM_TYPE_QKYJHF
		|| data.alarmtype == ALARM_TYPE_QKBJ || data.alarmtype == ALARM_TYPE_QKBJHF)
	{
		strRst += ("，");

		switch (cbFill[0])
		{
		case 1:
			strRst += ("静态缺口:");
			break;
		case 2:
			strRst += ("扳动后缺口:");
			break;
		case 3:
			strRst += ("过车缺口:");
			break;
		case 4:
			strRst += ("过车前缺口:");
			break;
		case 5:
			strRst += ("过车后缺口:");
			break;
		default:
			break;
		}
		if (data.fixorinvert == 1)
			strRst += ("反位缺口");
		else
			strRst += ("定位缺口");

		sprintf_s(buf, ("(%.2fmm)"), ((float)data.gap) / 100.0);
		strRst += buf;

		if (data.alarmtype <= 100)
		{
			if (cbFill[3] == 1)
			{
				sprintf_s(buf, ("大于%s上限(%.2fmm)"), GetAlarmLevelType(data.alarmtype).c_str(), ((float)(*(WORD*)&cbFill[1])) / 100.0);
				strRst += buf;
			}
			else if (cbFill[3] == 2)
			{
				sprintf_s(buf, ("小于%s下限(%.2fmm)"), GetAlarmLevelType(data.alarmtype).c_str(), ((float)(*(WORD*)&cbFill[1])) / 100.0);
				strRst += buf;
			}
		}
		return strRst;
	}
	else
	{

		strRst += ("定反位:");
		if (data.fixorinvert == 0)
			strRst += ("定位");
		else
			strRst += ("反位");

		strRst += ("，");
		strRst += ("左右偏标志:");
		if (data.lrsign == 1)
			strRst += ("左偏");
		else if (data.lrsign == 2)
			strRst += ("右偏");
		else
			strRst += ("无效");
	}

	if (data.alarmtype == ALARM_TYPE_POWERYJHF || data.alarmtype == ALARM_TYPE_POWERBJHF
		|| data.alarmtype == ALARM_TYPE_POWERYJ || data.alarmtype == ALARM_TYPE_POWERBJ)
	{
		strRst += ("，");
		sprintf_s(buf, ("动作杆伸缩方向:%s"), data.offset == 0 ? "拉入" : "伸出");
		strRst += buf;

		strRst += ("，");
		sprintf_s(buf, ("预警/报警起始点数:%d"), data.gap);
		strRst += buf;

		strRst += ("，");
		sprintf_s(buf, ("预警/报警结束点数:%d"), data.std);
		strRst += buf;
	}
	else
	{
		strRst += ("，");
		sprintf_s(buf, ("偏移值:%.2fmm"), data.offset / 100.0);
		strRst += buf;

		strRst += ("，");
		sprintf_s(buf, ("缺口值:%.2fmm"), data.gap / 100.0);
		strRst += buf;

		strRst += ("，");
		sprintf_s(buf, ("标准值:%.2fmm"), data.std / 100.0);
		strRst += buf;

		strRst += ("，");
		sprintf_s(buf, ("图像长度:%d"), data.imglen);
		strRst += buf;
	}

	return strRst;
}


BOOL ioDev_dcqk::IsRecover(BYTE type)
{
	if (type <= 100)
		return FALSE;
	else
		return TRUE;
}

void ioDev_dcqk::SendCallBackHeart(StHeartBeat315* pData)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_HEARTBEAT;
	data.ftail = FRAME_TAIL_315;

	StHeartBeat315 subdata;
	subdata = *pData;

	TIME tm = timeopt::now();
	subdata.hbtime = timeopt::SysTime2Unix(tm);

	data.datalen = sizeof(StHeartBeat315);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

int ioDev_dcqk::SendHeartbeat()
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_HEARTBEAT;
	data.datalen = sizeof(StHeartBeat315);
	data.ftail = FRAME_TAIL_315;

	StHeartBeat315 hb;
	ZeroMemory(&hb, sizeof(StHeartBeat315));

	hb.hbtime = _time32(NULL);
	memset(hb.filldata, 0xFF, 3);

	data.lpdata = &hb;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	bool bOk = sendData((unsigned char*)buf, len);

	if (bOk)
	{
		setOnline();
	}
	else
	{
		setOffline();
	}
	return 0;
}

void ioDev_dcqk::BanDongOpr(int iSID, BYTE bType)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	St1DQJInfo subdata;
	ZeroMemory(&subdata, sizeof(St1DQJInfo));

	subdata.cmdid = CMD_CODE_1DQJINFO;
	subdata.sid = iSID;
	subdata.time = _time32(NULL);

	subdata.status = bType;

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;
	Parse315Protocol::Unparse(data, buf, len);
	
	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::GetHisImg(int nSID, time_t sTm, time_t eTm)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StImgListReq subdata;
	ZeroMemory(&subdata, sizeof(StImgListReq));

	subdata.cmdid = CMD_CODE_IMGLIST;
	subdata.sid = nSID;
	subdata.begintime = (DWORD)sTm;
	subdata.endtime = (DWORD)eTm;
	*((DWORD*)&subdata.imgtype) = 0xFFFFFFFF;
	subdata.resqid = (rand() % 255);	//随机编号

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;
	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::GetHisVedio(int nSID, time_t sTm, time_t eTm, BYTE btVedioType)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StVedioListReq subdata;
	ZeroMemory(&subdata, sizeof(StVedioListReq));

	subdata.cmdid = CMD_CODE_VEDIOLIST;
	subdata.vediotype = btVedioType;
	subdata.sid = nSID;
	subdata.begintime = (DWORD)sTm;
	subdata.endtime = (DWORD)eTm;
	subdata.resqid = (rand() % 255); 	//随机编号

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;
	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::RealVedioOpr(int nSID, BYTE btFixorinvert, BYTE btCmdType)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));
	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StRealCtrlReq subdata;
	ZeroMemory(&subdata, sizeof(StRealCtrlReq));
	subdata.cmdid = CMD_CODE_REALCTRL;
	subdata.sid = nSID;
	subdata.fixorinvert = btFixorinvert;// m_radioFW.GetState();
	if (subdata.fixorinvert == 2)
		subdata.fixorinvert = 0xFF;

	subdata.cmdtype = btCmdType;
	StRealReqStream sub2data;
	ZeroMemory(&sub2data, sizeof(sub2data));
	subdata.lpdata = &sub2data;

	data.datalen = 1 + 2 + 1 + 1 + 7;
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;
	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::GetLastGap()
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StDataBasic subdata;
	ZeroMemory(&subdata, sizeof(StDataBasic));

	subdata.cmdid = CMD_CODE_GAPVAL;

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::GetLastImg(int nSID)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StLastGapImgReq subdata;
	ZeroMemory(&subdata, sizeof(StLastGapImgReq));

	subdata.cmdid = CMD_CODE_LASTGAPIMG;
	subdata.sid = nSID;
	subdata.filldata = 0xFFFFFFFF;

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::GetGapCfg()
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StDataBasic subdata;
	ZeroMemory(&subdata, sizeof(StDataBasic));

	subdata.cmdid = CMD_CODE_GAPCFG;

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::GetGapCfgEx()
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StDataBasic subdata;
	ZeroMemory(&subdata, sizeof(StDataBasic));

	subdata.cmdid = 0x00;

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);

}

void ioDev_dcqk::Getpowerfilelist(int nSID, time_t sTm, time_t eTm, BYTE btDir)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StPowerListReq subdata;
	ZeroMemory(&subdata, sizeof(StPowerListReq));

	subdata.cmdid = CMD_CODE_POWERLIST;
	subdata.sid = nSID;
	subdata.begintime = (DWORD)sTm;
	subdata.endtime = (DWORD)eTm;
	subdata.direction = btDir;// m_radioFW.GetState();
	if (subdata.direction == 2)
		subdata.direction = 0xFF;

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;
	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::SearchPowerCurveList(int nSID, time_t sTm, time_t eTm)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StPowerFileListResq subdata;
	ZeroMemory(&subdata, sizeof(StDataBasic));

	subdata.cmdid = CMD_CODE_QUERY_POWER_FILE;
	subdata.sid = nSID;
	subdata.cbDataType = 1;
	subdata.acqObjType = 4;
	subdata.starttime = (DWORD)sTm;
	subdata.endtime = (DWORD)eTm;

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::DownloadPowerCurveFile(int nSID, time_t sTm)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StPowerFileDataResq subdata;
	ZeroMemory(&subdata, sizeof(StDataBasic));

	subdata.cmdid = CMD_CODE_DOWNLOAD_DATA_FILE;
	subdata.sid = nSID;
	subdata.acqObjType = 4;	//	扳动阻力
	subdata.time = (DWORD)sTm;
	subdata.cbReadMode = 0;
	subdata.wSubID = 0;

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::ManualOiling(int nSID, BYTE btFixorinvert)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StManualOilingResq subdata;
	ZeroMemory(&subdata, sizeof(StDataBasic));

	subdata.cmdid = 0x51;
	subdata.sid = nSID;
	subdata.fixorinvert = btFixorinvert;// m_radioFW.GetState();
	if (subdata.fixorinvert == 2)
		subdata.fixorinvert = 0xFF;

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::GetUnRecoverAlarm(int nSID)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StAlarmListReq subdata;
	ZeroMemory(&subdata, sizeof(StAlarmListReq));

	subdata.cmdid = (BYTE)E_315_PROTOCOL_TYPE::UN_RECOVER_ALARM_0x65;
	subdata.sid = nSID;
	memset(subdata.res, 0xFF, 3);

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::GongKuangOpr()
{
	
}

void ioDev_dcqk::GetOilBoxVolume(int nSID)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StOilBoxVolumeResq subdata;
	ZeroMemory(&subdata, sizeof(StDataBasic));

	subdata.cmdid = (BYTE)E_315_PROTOCOL_TYPE::OIL_BOX_VOLUME_0x53;
	subdata.sid = nSID;
	subdata.r = 0xFFFFFFFF;

	data.datalen = sizeof(subdata);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}


void ioDev_dcqk::SendCallBack0x41(StElecCurve* lpsubdata)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = 0x80;
	data.dataversion = 0xFF;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StElecCurveRec subdata;
	subdata = *lpsubdata;

	data.datalen = sizeof(StElecCurveRec);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::SendCallBack0x25(StOilPreCurve* lpsubdata)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = 0x80;
	data.dataversion = 0xFF;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StOilPreCurveRec subdata;
	subdata = *lpsubdata;

	data.datalen = sizeof(StOilPreCurveRec);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::SendCallBack0x27(StAlarmAndImgInfo* lpsubdata)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = 0x80;
	data.dataversion = 0xFF;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StAlarmAndImgRec subdata;
	subdata = *lpsubdata;

	data.datalen = sizeof(StAlarmAndImgRec);
	data.lpdata = &subdata;

	LPVOID buf = NULL;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf, len);
}

void ioDev_dcqk::ParseDaoChaNameByZZJName(const string& sZZJName, string& sDc)
{
	std::regex reg("\\d{1,2}\\D");
	std::smatch matches;
	if (std::regex_search(sZZJName, matches, reg))
	{
		string str = matches[0].str();
		sDc = str.substr(0, str.length() - 1);
	}
}

void ioDev_dcqk::Do_CMD_CODE_YYQX(LPVOID pData)
{
	StOilPreCurve* lpsubdata = (StOilPreCurve*)pData;
	OBJ* pMo = prj.getObjByID(to_string(lpsubdata->sid));
	if (pMo && pMo->m_type == "转辙机") {
		for (int k = 0; k < lpsubdata->cnt;k++) {
			StCurve* pCurveAry = (StCurve*)lpsubdata->lpdata;
			StCurve* pCurve = &(pCurveAry[k]);
			//tb3386: 当曲线类型为 0x00、0x01、0x02 时,一个数据点用 2 个字节,低字节在前, 倍率 0. 01。
			//	当曲线类型为 0x03、0x04 时,一个数据点用 2 个字节,低字节在前, 倍率为 1, 数值为无符号数, 范围 0 ~65535。 目前阻力用的0x0a
			string metricName;//度量
			float ratio = 1.0;
			int pointNum = pCurve->len / 2;

			json jPt = json::array();
			if (pCurve->type == 0x0) { 
				metricName = "左油压";
				char buf[9] = {0};
				int i = 0; while (i < pointNum) {
					short* pointBuf = (short*)(pCurve->lpdata);
					float fData = pointBuf[i] * 0.01;
					sprintf_s(buf, 9, "%.1f", fData);
					jPt.push_back(buf);
					i++;
				}
			}
			else if (pCurve->type == 0x1) {
				metricName = "右油压";
				char buf[9] = { 0 };
				int i = 0; while (i < pointNum) {
					short* pointBuf = (short*)(pCurve->lpdata);
					float fData = pointBuf[i] * 0.01;
					sprintf_s(buf, 9, "%.1f", fData);
					jPt.push_back(buf);
					i++;
				}
			}
			else if (pCurve->type == 0x0a) {
				/*
				WORD power = *(WORD*)(pCurve->lpdata);  //0x7FFF:32767,  0x8000: -32768, 0x8001: -32767, 0xFFFF:-1  
				bool bNega = power & 0x8000;
				short da = (power & 0x7FFF) * (bNega ? -1 : 1) ;
				按协议,若负数 jhd发送其补码，直接强转即可
				*/
				metricName = "阻力";
				int i = 0; while (i < pointNum) {
					short* pointBuf = (short*)(pCurve->lpdata);
					short sData = pointBuf[i];
					jPt.push_back(sData);
					i++;
				}
			}
			string tag = pMo->getTag() + "."+ metricName;
			unsigned char interval = 1000 / lpsubdata->collectfreq;
			unsigned char dir = lpsubdata->direct;//0　定到反， 1 反到定
			unsigned char dzg_dir = lpsubdata->filldata & 0x0000ff00;//动作杆伸缩方向
			unsigned char curveAlm = lpsubdata->filldata & 0x0000ff00;//曲线报警状态
				TIME ti; ti.fromUnixTimeStamp(lpsubdata->time);
				string startTi = timeopt::stTimeToStr(ti);

				json jParamsArry = json::array();
				json jOne, jFile;
				if (pCurve->type == 0x0) {
					jFile["start_time"] = startTi;
					//jFile["end_time"] = "";
					jFile["interval"] = interval;
					jFile["point_count"] = pointNum;
					jFile["move_direct"] = dir;
					jFile["data"] = jPt;
					jFile["acq_type"] = 1;
					jFile["unit"] = "mpa";
				}
				else if (pCurve->type == 0x1) {
					jFile["start_time"] = startTi;
					//jFile["end_time"] = "";
					jFile["interval"] = interval;
					jFile["point_count"] = pointNum;
					jFile["move_direct"] = dir;
					jFile["data"] = jPt;
					jFile["acq_type"] = 1;
					jFile["unit"] = "mpa";
				}
				else if (pCurve->type == 0x0a) { //阻力
					jFile["powtype"] = 2;//协议没明确 过车阻力和摩擦阻力 这里默认取扳动阻力 2。
					jFile["bFrictionCurve"] = FALSE;
					jFile["fmt"] = "curve";
					jFile["interval"] = interval;
					jFile["move_direct"] = dir;
					jFile["data"] = jPt;
					jFile["start_time"] = startTi;
					//jFile["end_time"] = "";
				}
			json j0;  j0["type"] = "curve";  j0["data"] = jFile;
			jOne["file"] = j0;  jOne["tag"] = tag; jOne["time"] = startTi;
			string ss1 = jOne.dump();
			jParamsArry.push_back(jOne);

			LOG("[ioDev]dcqk新曲线,%s,%s", tag.c_str(), ss1.c_str());

			RPC_RESP resp;
			RPC_SESSION session;
			rpcSrv.rpc_input(jParamsArry, resp, session);
			
		}
	}
}

typedef struct _GapValPicParam {
	string tag;
	string fullTime;
	string justTime;
	float fVal;
	float std;
	float offset;
	BYTE lrsign; //0 无效，1 左偏， 2 右偏
	string zzj;
	string zzj315;
	BYTE location;
	BYTE acqreason;
} GapValPicParam;
void ioDev_dcqk::Do_CMD_CODE_GAPVAL(LPVOID pData)
{
	StGapValue* pInfo = (StGapValue*)pData;
	for (int i = 0; i < pInfo->cnt; i++) {
		auto list = (StGapRecord*)(pInfo->lpdata);
		StGapRecord* pRecord = &(list[i]);

		//TDS配置的名字不一定和JHD一致 下载文件时的url以JHD的转辙机名字为准
		//m_mapSIDToName;
		OBJ* zzjMo = prj.getObjByID(to_string(pRecord->sid));
		if (!zzjMo) {
			//log
			continue;
		}
		string zzj =zzjMo->getName("");
		string zzj315 = m_mapSIDToName[pRecord->sid];
		BYTE location = pRecord->fixorinvert;
		BYTE acqreason = pRecord->gaptype;
		string theTag = zzjMo->getTag()+  ".缺口";

		TIME ti; ti.fromUnixTimeStamp(pRecord->time);
		string  strTi = timeopt::st2str(ti);
		string  strJustTime = ti.toTimeStr();
		string val;

		float fVal = pRecord->gap * 1.0 / 100;

		GapValPicParam *param = new GapValPicParam();
		param->tag = theTag;
		param->fullTime = strTi;
		param->justTime = strJustTime;
		param->fVal = fVal;
		param->std = pRecord->std * 1.0 / 100;
		param->offset = pRecord->offset * 1.0 / 100;
		param->lrsign = pRecord->lrsign;
		param->zzj = zzj;
		param->zzj315 = zzj315;
		param->location = location;
		param->acqreason = acqreason;
		thread t(ThreadSaveGapAndPic, param);
		t.detach();

	}
}

void ThreadSaveGapAndPic(void* lpParam)
{
	GapValPicParam* param = (GapValPicParam*)lpParam;

	httplib::Client client("http://127.0.0.1");
	string nianyue = param->fullTime.substr(0,4)+ param->fullTime.substr(5, 2);
	string day = param->fullTime.substr(8, 2);
	string ti = param->justTime.substr(0, 2) + param->justTime.substr(3, 2) + param->justTime.substr(6, 2);
	string url = "/db/" + nianyue + "/" + day + "/" + param->zzj315 + "/" + ti + ".grh";
	httplib::Result res = client.Get(url);
	if (res) {
		int len = res->body.size();
		char* out = new char[len * 2 + 1];   memset(out, 0, len * 2 + 1);
		base64_encode((const unsigned char*)res->body.data(), len, out);

		json jParam, jVal;
		jParam["tag"] = param->tag;
		jParam["time"] = param->fullTime;
		jVal["val"] = str::format("%.2f", param->fVal);
		jVal["std"] = str::format("%.2f", param->std);
		jVal["offset"] = str::format("%.2f", param->offset);
		jVal["lrsign"] = param->lrsign==0 ? "left" :  (param->lrsign == 1 ? "right" :  "invalid");
		jVal["location"] = param->location;
		jVal["acqreason"] = param->acqreason;
		jParam["val"] = jVal;
		json jFile;
		jFile["name"] = ti + ".grh";
		jFile["type"] = "grh";
		jFile["data"] = string(out);
		jParam["file"] = jFile;
		delete[] out;
		//string s = jParam.dump();

		RPC_RESP resp;
		RPC_SESSION session;
		rpcSrv.rpc_input(jParam, resp, session);
	}
	delete param;
}

void ioDev_dcqk::Do_CMD_CODE_YWINFO(LPVOID pData)
{
	StOilLevelInfo* pInfo = (StOilLevelInfo*)pData;
	for (int i = 0; i < pInfo->cnt; i++) {
		auto list = (StSdataRecord*)(pInfo->lpdata);
		StSdataRecord* pRecord = &(list[i]);

		OBJ* zzjMo = prj.getObjByID(to_string(pRecord->sid));
		if (!zzjMo) {
			//log
			continue;
		}
		if (pRecord->oiltime==0xffff || pRecord->oillevel==0xffff) {
			continue;
		}
		json jParam, jVal;
		jParam["tag"] = zzjMo->getTag() + ".油位";
		jParam["time"] = pRecord->oiltime;
		jVal["val"] = pRecord->oillevel;
		//jVal["location"] = 0;
		jVal["acq_type"] = 2;// 1 扳， 2 周， 5 过车
		jParam["val"] = jVal;
		string s = jParam.dump();

		RPC_RESP resp;
		RPC_SESSION session;
		rpcSrv.rpc_input(jParam, resp, session);
	}
}
