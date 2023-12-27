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
	
}

bool ioDev_dcqk::onRecvPkt(json jPkt)
{
	return false;
}

bool ioDev_dcqk::onRecvData(unsigned char* pData, size_t iLen) {
	return onRecvPkt(pData, iLen);
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

			break;
		}
		case CMD_CODE_GAPVAL:
		{
			StGapValue* lpsubdata = (StGapValue*)pData->lpdata;

			break;
		}
		case CMD_CODE_ALARM_AND_IMG:
		{
			StAlarmAndImgInfo* lpsubdata = (StAlarmAndImgInfo*)pData->lpdata;
			//SendCallBack0x27(lpsubdata);

			TIME tm = timeopt::Unix2SysTime(lpsubdata->time);
			string sTime = timeopt::stTimeToStr(tm);

			json jParams;
			jParams["time"] = sTime.c_str();
			jParams["desc"] = "";
			jParams["level"] = "";
			jParams["tag"] = m_strTagBind;
			jParams["type"] = "";
			jParams["id"] = "";


			almServer* pAlmSrv = &almSrv;
			RPC_RESP resp;
			pAlmSrv->rpc_addAlarm(jParams, resp, FALSE);
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

			SendCallBack0x25(lpsubdata); //0X25命令需要回执信息
			break;
		}
		case CMD_CODE_YWINFO:
		{
			StOilLevelInfo* lpsubdata = (StOilLevelInfo*)pData->lpdata;

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

string ioDev_dcqk::GetAlmType(BYTE type)
{
	if (type == 1) return "预警";
	else if (type == 2 || type == 7 || type == 8) return "报警";
	else if (type == 101 || type == 102 || type == 107 || type == 108) return "恢复";
	return "";
}

int ioDev_dcqk::GetAlmLevel(BYTE type)
{
	if (type == 1) return 2;
	else return 3;
}

string ioDev_dcqk::GetAlarmDesc(BYTE type)
{
	string strDesc = "";
	switch (type) {
	case 1:
		strDesc = "缺口预警及预警图像";
		break;
	case 2:
		strDesc = "缺口报警及报警图像";
		break;
	case 7:
		strDesc = "温度报警";
		break;
	case 8:
		strDesc = "湿度报警";
		break;
	case 101:
		strDesc = "缺口预警恢复及图像";
		break;
	case 102:
		strDesc = "缺口报警恢复及图像";
		break;
	case 107:
		strDesc = "温度报警恢复";
		break;
	case 108:
		strDesc = "湿度报警恢复";
		break;
	}
	return strDesc;
}

BOOL ioDev_dcqk::IsRecover(BYTE type)
{
	if (type == 1 || type == 2 || type == 7 || type == 8) return FALSE;
	else return FALSE;
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


