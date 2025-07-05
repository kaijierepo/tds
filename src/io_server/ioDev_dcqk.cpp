#ifdef _WIN32
#include "pch.h"
#include "json.hpp"
#include "ioDev_dcqk.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "base64.h"
#include "mp.h"
#include "tAlmSrv.h"
#include "tdb.h"
#include <sstream>

std::map<int, std::string> ioDev_dcqk::g_map0x97AlarmLevel = {
	{ALARM_TYPE_QKYJ, "预警"},		//缺口预警及预警图像
	{ ALARM_TYPE_QKBJ			, "告警" },		//缺口告警及告警图像
	{ ALARM_TYPE_QKSBGZ		, "预警" },		//缺口采集设备故障，此时没有缺口值及缺口图像，左右偏移标志填无效(00),总包数填1，本包序号0，图像总长度0， 本帧图像长度0.
	{ ALARM_TYPE_TXWFSB		, "告警" },		//缺口图像无法识别告警
	{ ALARM_TYPE_GCKLGD		, "告警" },		//过车时框量过大告警及过车视频
	{ ALARM_TYPE_ZZJSBGZ		, "告警" },		//转辙机采集设备故障告警
	{ ALARM_TYPE_WDBJ			, "告警" },		//温度告警
	{ ALARM_TYPE_SDBJ			, "告警" },		//湿度告警
	{ ALARM_TYPE_YWYJ			, "预警" },		//油位预警（预留）
	{ ALARM_TYPE_YWBJ			, "告警" },		//油位告警（预留）
	{ ALARM_TYPE_POWERYJ      , "预警" },        //阻力预警
	{ ALARM_TYPE_POWERBJ      , "告警" },        //阻力告警
	{ ALARM_TYPE_ZHUANPOWERBJ, "告警" },
	{ ALARM_TYPE_LOCKBJ      , "告警" },
	{ ALARM_TYPE_CRSGAP      , "预警" },
	{ ALARM_TYPE_STATICGAPYJ , "预警" },
	{ ALARM_TYPE_STATICGAPBJ , "告警" },
	{ ALARM_TYPE_TEMPERATURE  , "预警" },		//温度预警
	{ ALARM_TYPE_HUMILITY		, "预警" },		//湿度预警
	{ ALARM_TYPE_QKYJHF		, "预警" },		//缺口预警恢复及图像
	{ ALARM_TYPE_QKBJHF		, "告警" },		//缺口告警恢复及图像
	{ ALARM_TYPE_QKSBGZHF		, "预警" },		//缺口采集设备故障恢复及图像
	{ ALARM_TYPE_TXWFSBHF		, "告警" },		//缺口图像无法识别告警恢复及图像
	{ ALARM_TYPE_ZZJSBGZHF	, "告警" },		//转辙机采集设备故障告警恢复
	{ ALARM_TYPE_GCKLGDHF		, "告警" },		//过车时框量过大告警恢复及过车视频
	{ ALARM_TYPE_WDBJHF		, "告警" },		//温度告警恢复
	{ ALARM_TYPE_SDBJHF		, "告警" },		//温度告警恢复
	{ ALARM_TYPE_YWYJHF		, "预警" },		//油位预警恢复（预留）
	{ ALARM_TYPE_YWBJHF		, "告警" },		//油位告警恢复（预留）
	{ ALARM_TYPE_POWERYJHF    , "预警" },        //阻力预警恢复
	{ ALARM_TYPE_POWERBJHF    , "告警" },        //阻力告警恢复
	{ ALARM_TYPE_ZHUANPOWERBJHF, "告警" },
	{ ALARM_TYPE_LOCKBJHF      , "告警" },
	{ ALARM_TYPE_CRSGAPHF      , "预警" },
	{ ALARM_TYPE_STATICGAPYJHF , "预警" },
	{ ALARM_TYPE_STATICGAPBJHF , "告警" },
	{ ALARM_TYPE_TEMPERATUREHF, "预警" },		//温度预警恢复
	{ ALARM_TYPE_HUMILITYHF	, "预警" } };		//湿度预警恢复


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

void eqpInfo::AddVedioCache(StVedioRecord* pTimes, WORD wCount, BYTE btType)
{
	EnterCriticalSection(&m_csVedioList);
	for (int i = 0; i < wCount; i++)
	{
		pTimes[i].filldata = 0;
		((BYTE*)&pTimes[i].filldata)[0] = btType;
		lstVedioCache.push_back(pTimes[i]);
	}
	LeaveCriticalSection(&m_csVedioList);
}

void eqpInfo::DeleteVedioCache(uint32_t sTm, BYTE btType)
{
	EnterCriticalSection(&m_csVedioList);
	for (auto it = lstVedioCache.begin(); it != lstVedioCache.end(); it++)
	{
if (it->time == sTm && ((BYTE*)&it->filldata)[0] == btType)
{
	it = lstVedioCache.erase(it);
	break;
}
	}
	LeaveCriticalSection(&m_csVedioList);
}


void ThreadDownloadVedio(void* lpParam)
{
	ioDev_dcqk* pDev = (ioDev_dcqk*)lpParam;

	while (true)
	{
		if (!pDev->m_bDownloadVedioing) break;
		DWORD dwTick = GetTickCount();
		EnterCriticalSection(&pDev->m_csEqp);
		for (auto it = pDev->m_mapEqp.begin(); it != pDev->m_mapEqp.end(); it++)
		{
			StVedioRecord* pST = NULL;
			WORD sid = 0;
			EnterCriticalSection(&it->second->m_csVedioList);
			//for (auto itVedio = it->second.lstVedioCache.begin(); itVedio != it->second.lstVedioCache.end(); itVedio++)
			auto itVedio = it->second->lstVedioCache.begin();
			if (itVedio != it->second->lstVedioCache.end())
			{
				if (((BYTE*)&itVedio->filldata)[1] == 0 || dwTick - it->second->timeLastReqDownload > 30000) //下载文件最多30秒，否则重新下载
				{
					((BYTE*)&itVedio->filldata)[1] = 1;
					pST = &(*itVedio);
					sid = it->first;
					it->second->timeLastReqDownload = dwTick;
				}
			}
			LeaveCriticalSection(&it->second->m_csVedioList);

			if (pST)
			{
				LeaveCriticalSection(&it->second->m_csVedio);
				it->second->m_mapVideos.clear();
				LeaveCriticalSection(&it->second->m_csVedio);

				pDev->QueryVedio(sid, pST);
			}
		}
		LeaveCriticalSection(&pDev->m_csEqp);

		if (!pDev->m_bDownloadVedioing) break;
		Sleep(100);
	}
}



//基于http的视频同步-begin
void ioDev_dcqk::InitVedioBuf()
{
	//总体初始化为 当日零秒，再更新为db中既有记录的最新时间
	TIME stNow;
	timeopt::now(&stNow); //stNow.wHour = 0; stNow.wMinute = 0; stNow.wSecond = 0; 地铁3分钟几乎一次过车 若首次部署时在晚上 从0秒开始 则一天的过车太多了
	int unix0 = timeopt::SysTime2Unix(stNow);
	//所关联站下面的转辙机
	m_strTagBind;
	std::vector<OBJ*> tagVec;
	prj.queryObj(&tagVec, "*", "", "转辙机", "mo");
	for (auto& it : tagVec) {
		map<string, int> m0; m0["move"] = unix0; m0["cross"] = unix0;
		m_mapZzjId_newestVideoTime[it->m_name] = m0;
	}

	//更新为db中既有记录的最新时间的下一秒
	//算了 启动之前的视频直接丢弃吧
}

//查询视频http Url列表
void ioDev_dcqk::QueryVedioUrlList()
{
	TIME stNow, stNowDay;
	timeopt::now(stNow);
	stNowDay = stNow;  stNowDay.wHour = 0; stNowDay.wMinute = 0; stNowDay.wSecond = 0;
	int unixNowDayFirstSec = timeopt::SysTime2Unix(stNowDay);

	for (auto& it : m_mapZzjId_newestVideoTime) {
		auto& zzj = it.first;

		auto& mapInfo = it.second;
		int& nMoveTime = mapInfo["move"];
		//长时间如跨天没视频更新，或 程序没启动，则缓存和DB的时间太老,计算的时间段也是老的,无法完整覆盖, nMoveTime改为当前
		{
			if (nMoveTime < unixNowDayFirstSec)
				nMoveTime = timeopt::SysTime2Unix(stNow);
		}
		TIME stMoveEnd = timeopt::Unix2SysTime(nMoveTime); stMoveEnd.wHour = 23; stMoveEnd.wMinute = 59; stMoveEnd.wSecond = 59;
		int nMoveEnd = timeopt::SysTime2Unix(stMoveEnd);

		int& nCrossTime = mapInfo["cross"];
		{
			if (nCrossTime < unixNowDayFirstSec)
				nCrossTime = timeopt::SysTime2Unix(stNow);
		}
		TIME stCrossEnd = timeopt::Unix2SysTime(nCrossTime); stCrossEnd.wHour = 23; stCrossEnd.wMinute = 59; stCrossEnd.wSecond = 59;
		int nCrossEnd = timeopt::SysTime2Unix(stCrossEnd);

		if (zzj == "1J1") {
			int d1 = 0;
		}
		else if (zzj == "20") {
			int d1 = 0;
		}
		else {
			int d1 = 0;
		}
		//if (zzjId != "19")
			//continue;

		bool bRet = SendGetVideoList_ext(zzj, "car_move_video", nMoveTime, nMoveEnd);
		if (!bRet) {
			//log
		}
		bRet = SendGetVideoList_ext(zzj, "car_cross_video", nCrossTime, nCrossEnd);
		if (!bRet) {
			//log
		}
	}

	//下载
	m_bFirstSendAfterBoot = false;
}

//strType : car_cross_video  car_move_video
bool ioDev_dcqk::SendGetVideoList_ext(string zzj, string strType, int nStartUnix, int nEndUnix)
{
	if (pIOSession == nullptr) {
		return false;
	}

	bool bRet = false;
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = PROTOCAL_DATAVERSION;
	data.ftype = FRAME_TYPE_JSON;
	data.ftail = FRAME_TAIL_315;


	St315Json subdata;
	ZeroMemory(&subdata, sizeof(StImgInfoReq));

	json joData;
	joData["id"] = zzj; //转辙机名 或ID  下位机根据是否包含字母自动判断
	joData["type"] = strType;//car_cross_video  car_move_video
	joData["object"] = "gap";//gap, lock
	joData["starttime"] = nStartUnix;
	joData["endtime"] = nEndUnix;

	subdata.cmdid = CMD_CODE_VEDIOLIST;
	subdata.pkt_num = 65535;
	memset(&(subdata.filldata), 0xFF, 4);
	subdata.frmType = 0x2;

	string str0 = joData.dump();

	subdata.frmLength = str0.length();
	subdata.lpContent = new char[subdata.frmLength];
	memcpy(subdata.lpContent, str0.c_str(), subdata.frmLength);

	data.datalen = sizeof(subdata) - sizeof(void*) + subdata.frmLength;
	data.lpdata = &subdata;

	vector<BYTE> buf;
	int len = 0;
	bRet = Parse315Protocol::Unparse(data, buf, len);
	delete[] subdata.lpContent;
	if (!bRet) {
		return false;
	}

	if (len > 0) {
		int nRet = sendData((unsigned char*)buf.data(), len);

		if (nRet <= 0) {
			bRet = false;
		}
		else if (nRet < len) {
			//最好重发剩余的 直接算失败吧
		}
		else if (nRet > len) {
			//log
		}
		else {
			bRet = true;
		}
	}
	return bRet;
}

BOOL ioDev_dcqk::ProcessJsonFrmData_0x3F(LPVOID pData)
{
	StFrame* stFrm = (StFrame*)pData;

	LPVOID pSubData = stFrm->lpdata;
	char byCmd = *((char*)pSubData);

	switch (byCmd) {
	case CMD_CODE_VEDIOLIST: {
		St315Json* pInfo = (St315Json*)pSubData;

		//zzj id、视频类型 默认从filldata里取，没有的话再从url里提取 道岔、zzj、视频类型   后续filldata扩展为8字节 发送方写入 响应包原样返回。 前两字节 zzjId。第3字节 视频类型。后面4字节 时间戳。
		//前两字节 zzjId， B[0] 高8位 B[1] 低8位。第3字节 视频类型；
		//WORD wZzjId = pInfo->filldata[0];
		//wZzjId = wZzjId<<8 + pInfo->filldata[1];
		//BYTE bType = pInfo->filldata[2];

		string strJo(pInfo->lpContent, pInfo->frmLength);
		json joList = json::parse(strJo);

		int cnt = joList.size();
		string strZzj;
		string strType;
		string strTime;
		bool bSaveOk = false;
		for (int i = 0; i < cnt; i++) {
			string ti = joList[i]["time"];
			string url0 = joList[i]["url"];
			string label = joList[i]["label"];

			//IP/ db / 202305、18、dc（ % 23）、zzj、扳动录像、1010101.avi_mv 

			int pos = url0.find("/db/");
			if (pos == string::npos) {
				//log 
				continue;
			}

			int pos1 = url0.substr(0, pos).rfind(":");
			if (pos1 == string::npos) {
				//log 
				continue;
			}
			string port = url0.substr(pos1 + 1);

			char ss[512];  strcpy(ss, url0.c_str());
			string url = str::url_decode(ss);

			string yearMonth = url.substr(pos + 4, 6);
			string day = url.substr(pos + 4 + 7, 2);

			int daochaPos = pos + 14;
			int po1 = url.find("/", daochaPos);
			string dc = url.substr(daochaPos, po1 - daochaPos);

			int zzjPos = po1 + 1;
			int po2 = url.find("/", zzjPos);
			strZzj = url.substr(zzjPos, po2 - zzjPos);

			int typePos = po2 + 1;
			int po3 = url.find("/", typePos);
			string videoType = url.substr(typePos, po3 - typePos);

			string ipPort = getIP() + string(":82");
			string path = url0.substr(pos);

			{
				string ss9 = str::format("总数:%d, %s\n", cnt, url.c_str());
				OutputDebugString(ss9.c_str());
			}
			// 存个map key为 yearMonth / day，若新的一天，则删掉旧的一天的map  每次插入DB存入map，重启了会去重  额 不行 必须得从DB种取最新时间
			string time0 = ti.substr(0, 2) + ":" + ti.substr(2, 2) + ":" + ti.substr(4, 2);
			string theTime = yearMonth.substr(0, 4) + "-" + yearMonth.substr(4, 2) + "-" + day + " " + time0;
			strType = videoType == "扳动录像" ? "move" : "cross";

			//防止重复   临界情况: 处理完但在更新m_mapZzjId_newestVideoTime之前又send了一次 
			//前边查询视频时间时保证了按照从小到大的顺序发起查询 保证新收到的时间一定更大
			{
				int& unix0 = m_mapZzjId_newestVideoTime[strZzj][strType];
				if (timeopt::SysTime2Unix(timeopt::str2st(theTime)) < unix0) {
					continue;
				}
			}

			int beforePos = 0;
			int afterPos = 0;
			if (strType == "cross") {
				if (label == "定位" || label == "左位") { beforePos = 0; afterPos = 0; }
				else { beforePos = 1; afterPos = 1; }
			}
			else {
				if (label == "定位->反位" || label == "左位->右位") { beforePos = 0; afterPos = 1; }
				else { beforePos = 1; afterPos = 0; }
			}

			// Sixf todo 未来改为tb3386通信
			//httplib::Client cli(ipPort); 
			//auto res = cli.Get(path);//"/db/202410/30/1%23/1J1/扳动录像/170102.avi_mv" 
			//if (res && res->status == 200) {

			//	//写入数据库
			//	int flen = res->body.size();
			//	char* out = new char[flen * 2 + 1];
			//	tdb_base64_encode((const unsigned char*)res->body.c_str(), flen, out);
			//	{
			//		json j, jV, jFile;
			//		string tag = m_strTagBind + "." + dc + "." + strZzj + "." + videoType;
			//		j["tag"] = tag;
			//		j["time"] = theTime;
			//		jV["aaaa"] = "aaa";
			//		j["val"] = jV;
			//		j["beforePos"] = beforePos; // 0 定位， 1  反位
			//		j["afterPos"] = afterPos;
			//		j["vedioTimeLen"] = 0;// data->timelen;
			//		j["vedioLen"] = res->body.size();
			//		jFile["type"] = (strType == "cross" ? "avi_crs" : "avi_mv");
			//		jFile["name"] = timeopt::TimeToHMSForFile(timeopt::str2st(theTime)) + (strType == "cross" ? ".avi_crs" : ".avi_mv");
			//		jFile["data"] = out;
			//		j["file"] = jFile;

			//		j["db"] = "media";
			//		tds->callAsyn("db.insert", j);
			//	}
			//	delete[] out;
			//}
			//else {
			//	string err = res ? std::to_string(res->status) : "No response";
			//	//log
			//}

			bSaveOk = true;
			strTime = theTime;
		}
		if (bSaveOk)
			m_mapZzjId_newestVideoTime[strZzj][strType] = timeopt::SysTime2Unix(timeopt::str2st(strTime)) + 1;

		break;
	}
	default:
		break;
	}

	return true;
}

//基于http的视频同步-end


ioDev_dcqk::ioDev_dcqk()
{
	m_devType = "dcqk-sys-device";
	m_devTypeLabel = "道岔缺口站机";
	m_level = "devcie";
	InitializeCriticalSection(&m_csEqp);

	m_stLastQueryVedioUrlTime = timeopt::now();
}

ioDev_dcqk::~ioDev_dcqk()
{
	stop();
	EnterCriticalSection(&m_csEqp);
	for (auto it : m_mapEqp)
	{
		delete it.second;
	}
	LeaveCriticalSection(&m_csEqp);
	DeleteCriticalSection(&m_csEqp);
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

	if (tds->conf->getInt("TB3386_EnableVideoSync", 1) == 1) {
		/*if (!m_mapEqp.empty() && timeopt::CalcTimePassSecond(m_stLastQueryVedioTime) >= 2)
		{
			m_stLastQueryVedioTime = timeopt::now();
			QueryVedioList();
		}*/

		//改为老版健康管理的机制：定期获取url列表，再用http下载
		if(!m_bFirstDoCycle){
			m_bFirstDoCycle = true;
			InitVedioBuf();
		}
		if (timeopt::CalcTimePassSecond(m_stLastQueryVedioUrlTime) > 10) {
			m_stLastQueryVedioUrlTime = timeopt::now();
			QueryVedioUrlList();
		}
	}
}

void ioDev_dcqk::onEvent_online()
{
	if (m_mapEqp.empty())
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

void ioDev_dcqk::onRecvData_tcpClt(unsigned char* pData, size_t len, tcpSessionClt* connInfo)
{
	stream2pkt* pab = &m_pab;
	pab->PushStream(pData, len);

	while (pab->PopPkt(IsValidPkt_315, false))
	{
		if (pab->abandonData != "")
		{
			string remoteAddr = getDevAddrStr();
			LOG("[warn]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);
			m_abandonLen += pab->iAbandonLen;
		}
		onRecvPkt(pab->pkt, pab->iPktLen);
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
			EnterCriticalSection(&m_csEqp);

			for (int k = 0; k < lpsubdata->cfgcnt; k++) {

				StSwitchCfg& ZZJConf = ((StSwitchCfg*)lpsubdata->lpcfg)[k];

				char zzjName[256];
				memcpy(zzjName, ZZJConf.lpname, ZZJConf.nlen);
				zzjName[ZZJConf.nlen] = '\0';

				auto iter = m_mapEqp.find(ZZJConf.sid);
				if (iter == m_mapEqp.end())
				{
					auto eqp = new eqpInfo();
					eqp->eqpName = zzjName;

					size_t iPos = 0;
					string sTmp = "";
					if (eqp->eqpName.find("#") != string::npos) {
						iPos = eqp->eqpName.find("#");
						iPos += 1;
					}
					else if (eqp->eqpName.find("J") != string::npos) {
						iPos = eqp->eqpName.find("J");
						sTmp = "#";
					}
					else if (iPos = eqp->eqpName.find("X") != string::npos) {
						iPos = eqp->eqpName.find("X");
						sTmp = "#";
					}
					else if (iPos = eqp->eqpName.find("P") != string::npos) {
						iPos = eqp->eqpName.find("P");
						sTmp = "#";
					}
					else if (iPos = eqp->eqpName.find("W") != string::npos) {
						iPos = eqp->eqpName.find("W");
						sTmp = "#";
					}

					eqp->daochaName = eqp->eqpName.substr(0, iPos) + sTmp;

					string key = m_strTagBind + "." + eqp->daochaName + "." + eqp->eqpName + ".triggerVideoLastTime";
					eqp->timeLastTriggerVedio = tds->conf->getCurrentInt(key, 0);

					key = m_strTagBind + "." + eqp->daochaName + "." + eqp->eqpName + ".passCarVideoLastTime";
					eqp->timeLastPassCarVedio = tds->conf->getCurrentInt(key, 0);

					m_mapEqp[ZZJConf.sid] = eqp;
				}
			}
			LeaveCriticalSection(&m_csEqp);
			BYTE req_0x26[17]{ 0x71, 0x6B, 0x6E, 0x65, 0x74, 0x02, 0x01, 0x8F, 0x01, 0x00, 0x00, 0x00, 0x26, 0xFF, 0xFF, 0xFF, 0xFF };
			sendData(req_0x26, 17);
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

			EnterCriticalSection(&m_csEqp);
			auto iter = m_mapEqp.find(lpsubdata->sid);
			if (iter == m_mapEqp.end())
			{
				LeaveCriticalSection(&m_csEqp);
				return 0;
			}

			string sZZJName = iter->second->eqpName;
			string sDaoChaName = iter->second->daochaName;
			LeaveCriticalSection(&m_csEqp);

			//size_t iPos = sZZJName.find("#");
			//string sTmp = ".";
			//if (iPos == string::npos) {
			//	iPos = sZZJName.find("J");
			//	sTmp = "#.";
			//}



			BYTE* cbFill = ((BYTE*)&lpsubdata->filldata);
			int nVer = (cbFill[1] == 0xFF ? 2015 : (cbFill[1] + 2000));
			if (basic->cmdid == CMD_CODE_ALARM_AND_IMG)
			{
				cbFill[0]++;
			}
			json jParams;
			if (basic->cmdid == CMD_CODE_ALARM)
			{
				jParams["desc"] = Get0x97AlarmDesc(*lpsubdata);
				jParams["level"] = Get0x97AlarmLevelType(lpsubdata->alarmtype);
				jParams["type"] = Get0x97AlarmType(lpsubdata->alarmtype, cbFill[0]);
			}
			else
			{
				jParams["desc"] = "";
				jParams["level"] = Get0x27AlarmLevelType(lpsubdata->alarmtype);
				jParams["type"] = Get0x27AlarmType(lpsubdata->alarmtype, cbFill[0], nVer);
			}

			jParams["tag"] = m_strTagBind + "." + sDaoChaName + "." + sZZJName;
			jParams["id"] = "";
			almServer* pAlmSrv = &almSrv;
			RPC_RESP resp;
			

			//"time":"2023-08-08 11:12:23",
			//	"tag" : "1幢B6区3层",
			//	"type" : "感烟探测器",
			//	"id" : "69001"
			if (tds->conf->getInt("enableTB3386Alarm", 1)) {
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


					pAlmSrv->rpc_addAlarm(jParams, resp, TRUE);
				}
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
			Do_CMD_CODE_IMGINFO(lpsubdata);
			break;
		}
		case CMD_CODE_VEDIOLIST:
		{
			StVedioListRes* lpsubdata = (StVedioListRes*)pData->lpdata;
			StVedioRecord* lpsubdata2 = (StVedioRecord*)lpsubdata->lprecord;
			EnterCriticalSection(&m_csEqp);
			m_mapEqp[lpsubdata->sid]->AddVedioCache(lpsubdata2, lpsubdata->cnt, lpsubdata->vediotype);
			LeaveCriticalSection(&m_csEqp);

			if (!m_bDownloadVedioThread)
			{
				m_bDownloadVedioing = true;
				m_bDownloadVedioThread = true;
				m_threadDownloadVedio = thread(ThreadDownloadVedio, this);
				m_threadDownloadVedio.detach();
			}

			break;
		}
		case CMD_CODE_VEDIOFILE:
		{
			if (Parse315Protocol::b2Flen2byte)
			{
				DealVedioFile((StVedioFileRes2*)pData->lpdata);
			}
			else
			{
				DealVedioFile((StVedioFileRes4*)pData->lpdata);
			}

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

			//vector<BYTE> buf;
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
	case FRAME_TYPE_JSON: 
		if (pData->lpdata == NULL) {
			string str = Parse315Protocol::ToString(*pData, 1);
			LOG("[ioDev]dcqk异常的315数据包,%s", str.c_str());
			return false;
		}
		ProcessJsonFrmData_0x3F((LPVOID)pData);
		break;
	default:
		break;
	}

	return 0;
}


template<typename T>
void ioDev_dcqk::DealVedioFile(T* data)
{
	EnterCriticalSection(&m_csEqp);
	auto pEqp = m_mapEqp[data->sid];
	LeaveCriticalSection(&m_csEqp);
	
	EnterCriticalSection(&pEqp->m_csVedio);
	//pEqp->DeleteVedioCache(data->time, data->vediotype);

	if (pEqp->m_mapVideos.find(data->time) == pEqp->m_mapVideos.end())
	{
		VedioFile &vfile = pEqp->m_mapVideos[data->time];
		vfile.sid = data->sid;
		vfile.time = data->time;
		vfile.timelen = data->timelen;
		vfile.len = data->len;
		vfile.packcnt = data->packcnt;
		vfile.maxpacklen = data->datalen;
		//vfile.recvcnt = 0;
		vfile.bFinished = FALSE;
		vfile.vctPacks.resize(data->len);


	}
	VedioFile& vfile = pEqp->m_mapVideos[data->time];

	vfile.maxpacklen = max(data->datalen, vfile.maxpacklen);
	CopyMemory(vfile.vctPacks.data() + data->curpackid * vfile.maxpacklen, data->lpdata, data->datalen);

	vfile.setPack.insert(data->curpackid);

	if (vfile.setPack.size() == vfile.packcnt)
	{
		vfile.bFinished = TRUE;


		//写入数据库
		int flen = vfile.vctPacks.size();
		char* out = new char[flen * 2 + 1];
		tdb_base64_encode(vfile.vctPacks.data(), flen, out);
		{
			json j, jV, jFile;
			string tag = m_strTagBind + "." + pEqp->daochaName + "." + pEqp->eqpName + (data->vediotype == 0x02 ? ".过车录像" : ".扳动录像");
			j["tag"] = tag;
			auto t = timeopt::Unix2SysTime(data->time);
			j["time"] = timeopt::stTimeToStr(t);
			jV["aaaa"] = "aaa";
			j["val"] = jV;
			j["beforePos"] = data->fixorinvert1;
			j["afterPos"] = data->fixorinvert2;
			j["vedioTimeLen"] = data->timelen;
			j["vedioLen"] = data->len;
			jFile["type"] = (data->vediotype == 0x02 ? "avi_crs" : "avi_mv");
			jFile["name"] = timeopt::TimeToHMSForFile(t) + (data->vediotype == 0x02 ? ".avi_crs" : ".avi_mv");
			jFile["data"] = out;
			j["file"] = jFile;

			//tds->callAsyn("input", j);

			j["db"] = "media";
			tds->callAsyn("db.insert", j);
		}

		delete[]out;
		pEqp->m_mapVideos.erase(data->time);

		string key;
		if (data->vediotype == 0x02)
		{
			pEqp->timeLastPassCarVedio = data->time;
			key = m_strTagBind + "." + pEqp->daochaName + "." + pEqp->eqpName + ".passCarVideoLastTime";
		}
		else
		{
			pEqp->timeLastTriggerVedio = data->time;
			key = m_strTagBind + "." + pEqp->daochaName + "." + pEqp->eqpName + ".triggerVideoLastTime";
		}
		tds->conf->setCurrentInt(key, data->time);

		pEqp->DeleteVedioCache(data->time, data->vediotype);
	}
	LeaveCriticalSection(&pEqp->m_csVedio);
}

string ioDev_dcqk::Get0x27AlarmLevelType(BYTE type)
{	
	if (type == 1 || type == 9 || type == 11 || type == 16
		|| type == 101 || type == 109 || type == 111 || type == 116)
		return "预警";
	else if (type == 2 || type == 7 || type == 8 || type == 10 || type == 12 || type == 13 || type == 14 || type == 15 || type == 17 || type == 65
		|| type == 102 || type == 107 || type == 108 || type == 110 || type == 112 || type == 113 || type == 114 || type == 115 || type == 117 || type == 165)
		return "告警";

	return "";
}

string ioDev_dcqk::Get0x97AlarmLevelType(BYTE type)
{	
	return g_map0x97AlarmLevel[type];
}

string ioDev_dcqk::GetAlmLevel(BYTE type)
{
	if (type == 1) return "二级";
	else return "三级";
}

string ioDev_dcqk::Get0x97AlarmType(BYTE type, BYTE type1)
{
	string strRst;
	switch (type)
	{
	case ALARM_TYPE_QKYJ:
		strRst += ("缺口预警及预警图像");
		break;
	case ALARM_TYPE_QKBJ:
		strRst += ("缺口告警及告警图像");
		break;
	case ALARM_TYPE_QKSBGZ:
		strRst += ("缺口采集设备故障");
		break;
	case ALARM_TYPE_TXWFSB:
		strRst += ("缺口图像无法识别告警");
		break;
	case ALARM_TYPE_GCKLGD:
		strRst += ("过车时框量过大告警及过车视频");
		break;
	case ALARM_TYPE_ZZJSBGZ:
		strRst += ("转辙机采集设备故障告警");
		break;
	case ALARM_TYPE_WDBJ:
		strRst += ("温度告警");
		break;
	case ALARM_TYPE_SDBJ:
		strRst += ("湿度告警");
		break;
	case ALARM_TYPE_YWYJ:
		strRst += ("油位预警");
		break;
	case ALARM_TYPE_YWBJ:
		strRst += ("油位告警");
		break;
	case ALARM_TYPE_POWERYJ:
		strRst += ("油压预警");
		break;
	case ALARM_TYPE_POWERBJ:
		strRst += ("油压告警");
		break;
	case 13:
		return ("道岔转换阻力超限报警");
		break;
	case 14:
		return ("外锁闭装置锁闭力超限报警");
		break;
	case 15:
		return ("过车时缺口值");
		break;
	case 16:
		return ("静态缺口预警");
		break;
	case 17:
		return ("静态缺口报警");
		break;
	case ALARM_TYPE_TEMPERATURE:
		strRst += ("温度预警");
		break;
	case ALARM_TYPE_HUMILITY:
		strRst += ("湿度预警");
		break;
	case ALARM_TYPE_QKYJHF:
		strRst += ("缺口预警及预警图像恢复");
		break;
	case ALARM_TYPE_QKBJHF:
		strRst += ("缺口告警及告警图像恢复");
		break;
	case ALARM_TYPE_QKSBGZHF:
		strRst += ("缺口采集设备故障恢复");
		break;
	case ALARM_TYPE_TXWFSBHF:
		strRst += ("缺口图像无法识别告警恢复");
		break;
	case ALARM_TYPE_GCKLGDHF:
		strRst += ("过车时框量过大告警及过车视频恢复");
		break;
	case ALARM_TYPE_ZZJSBGZHF:
		strRst += ("转辙机采集设备故障告警恢复");
		break;
	case ALARM_TYPE_WDBJHF:
		strRst += ("温度告警恢复");
		break;
	case ALARM_TYPE_SDBJHF:
		strRst += ("湿度告警恢复");
		break;
	case ALARM_TYPE_YWYJHF:
		strRst += ("油位预警恢复");
		break;
	case ALARM_TYPE_YWBJHF:
		strRst += ("油位告警恢复");
		break;
	case ALARM_TYPE_POWERYJHF:
		strRst += ("油压预警恢复");
		break;
	case ALARM_TYPE_POWERBJHF:
		strRst += ("油压告警恢复");
		break;
	case 113:
		return ("道岔转换阻力超限报警恢复");
		break;
	case 114:
		return ("外锁闭装置锁闭力超限报警恢复");
		break;
	case 115:
		return ("过车时缺口值恢复");
		break;
	case 116:
		return ("静态缺口预警恢复");
		break;
	case 117:
		return ("静态缺口报警恢复");
		break;
	case ALARM_TYPE_TEMPERATUREHF:
		strRst += ("温度预警恢复");
		break;
	case ALARM_TYPE_HUMILITYHF:
		strRst += ("湿度预警恢复");
		break;
	default:
		strRst += ("未知");
		break;
	}
	return strRst;
}

string ioDev_dcqk::Get0x27AlarmType(BYTE type, BYTE type1, int nVer)
{
	string strAcqType;
	if (type1 == 2)
		strAcqType = "扳动";
	else if (type1 == 3)
		strAcqType = "过车";
	else
		strAcqType = "周期";

	if (nVer < 2023)
	{
		switch (type)
		{
		case ALARM_TYPE_QKYJ:
			return strAcqType + ("缺口预警及预警图像");
			break;
		case ALARM_TYPE_QKBJ:
			return strAcqType + ("缺口报警及报警图像");
			break;
		case ALARM_TYPE_QKSBGZ:
			return ("缺口采集设备故障");
			break;
		case ALARM_TYPE_TXWFSB:
			return ("缺口图像无法识别报警");
			break;
		case ALARM_TYPE_GCKLGD:
			return ("过车时框量过大报警及过车视频");
			break;
		case ALARM_TYPE_ZZJSBGZ:
			return ("转辙机采集设备故障报警");
			break;
		case ALARM_TYPE_WDBJ:
			return ("温度报警");
			break;
		case ALARM_TYPE_SDBJ:
			return ("湿度报警");
			break;
		case ALARM_TYPE_YWYJ:
			return ("油位预警");
			break;
		case ALARM_TYPE_YWBJ:
			return ("油位报警");
			break;
		case ALARM_TYPE_POWERYJ:
			return ("阻力预警");
			break;
		case ALARM_TYPE_POWERBJ:
			return ("阻力报警");
			break;
		case ALARM_TYPE_QKYJHF:
			return strAcqType + ("缺口预警及预警图像恢复");
			break;
		case ALARM_TYPE_QKBJHF:
			return strAcqType + ("缺口报警及报警图像恢复");
			break;
		case ALARM_TYPE_QKSBGZHF:
			return ("缺口采集设备故障恢复");
			break;
		case ALARM_TYPE_TXWFSBHF:
			return ("缺口图像无法识别报警恢复");
			break;
		case ALARM_TYPE_ZZJSBGZHF:
			return ("转辙机采集设备故障报警恢复");
			break;
		case ALARM_TYPE_GCKLGDHF:
			return ("过车时框量过大报警及过车视频恢复");
			break;
		case ALARM_TYPE_WDBJHF:
			return ("温度报警恢复");
			break;
		case ALARM_TYPE_SDBJHF:
			return ("湿度报警恢复");
			break;
		case ALARM_TYPE_YWYJHF:
			return ("油位预警恢复");
			break;
		case ALARM_TYPE_YWBJHF:
			return ("油位报警恢复");
			break;
		case ALARM_TYPE_POWERYJHF:
			return ("阻力预警恢复");
			break;
		case ALARM_TYPE_POWERBJHF:
			return ("阻力报警恢复");
			break;
		default:
			return ("未知");
			break;
		}
	}
	else
	{
		switch (type)
		{
		case 1:
			return strAcqType + ("转换后缺口预警及预警图像");
			break;
		case 2:
			return strAcqType + ("转换后缺口报警及报警图像");
			break;
		case 3:
			return ("缺口采集设备故障");
			break;
		case 4:
			return ("缺口图像无法识别报警");
			break;
		case 5:
			return ("过车时旷量过大报警及过车视频");
			break;
		case 6:
			return ("转辙机采集设备故障报警");
			break;
		case 7:
			return ("温度报警");
			break;
		case 8:
			return ("湿度报警");
			break;
		case 9:
			return ("油位预警");
			break;
		case 10:
			return ("油位报警");
			break;
		case 11:
			return ("油压预警");
			break;
		case 12:
			return ("油压报警");
			break;
		case 13:
			return ("道岔转换阻力超限报警");
			break;
		case 14:
			return ("外锁闭装置锁闭力超限报警");
			break;
		case 15:
			return ("过车时缺口值");
			break;
		case 16:
			return ("静态缺口预警");
			break;
		case 17:
			return ("静态缺口报警");
			break;
		case 101:
			return strAcqType + ("转换后缺口预警及预警图像恢复");
			break;
		case 102:
			return strAcqType + ("转换后缺口报警及报警图像恢复");
			break;
		case 103:
			return ("缺口采集设备故障恢复");
			break;
		case 104:
			return ("缺口图像无法识别报警恢复");
			break;
		case 105:
			return ("转辙机采集设备故障报警恢复");
			break;
		case 106:
			return ("过车时旷量过大报警及过车视频恢复");
			break;
		case 107:
			return ("温度报警恢复");
			break;
		case 108:
			return ("湿度报警恢复");
			break;
		case 109:
			return ("油位预警恢复");
			break;
		case 110:
			return ("油位报警恢复");
			break;
		case 111:
			return ("油压预警恢复");
			break;
		case 112:
			return ("油压报警恢复");
			break;
		case 113:
			return ("道岔转换阻力超限报警恢复");
			break;
		case 114:
			return ("外锁闭装置锁闭力超限报警恢复");
			break;
		case 115:
			return ("过车时缺口值恢复");
			break;
		case 116:
			return ("静态缺口预警恢复");
			break;
		case 117:
			return ("静态缺口报警恢复");
			break;
		default:
			return ("未知");
			break;
		}
	}
	return ("");

}

string ioDev_dcqk::Get0x97AlarmDesc(const StAlarmAndImgRec& data)
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
				sprintf_s(buf, ("大于%s上限(%.2f%s)"), Get0x97AlarmLevelType(data.alarmtype).c_str(), ((float)data.std) / 100.0, sunit.c_str());
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				sprintf_s(buf, ("小于%s下限(%.2f%s)"), Get0x97AlarmLevelType(data.alarmtype).c_str(), ((float)data.offset) / 100.0, sunit.c_str());
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
				sprintf_s(buf, ("大于%s上限(%dmm)"), Get0x97AlarmLevelType(data.alarmtype).c_str(), data.std);
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				sprintf_s(buf, ("小于%s下限(%dmm)"), Get0x97AlarmLevelType(data.alarmtype).c_str(), data.offset);
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
				sprintf_s(buf, ("大于%s上限(%.2fmm)"), Get0x97AlarmLevelType(data.alarmtype).c_str(), ((float)(*(WORD*)&cbFill[1])) / 100.0);
				strRst += buf;
			}
			else if (cbFill[3] == 2)
			{
				sprintf_s(buf, ("小于%s下限(%.2fmm)"), Get0x97AlarmLevelType(data.alarmtype).c_str(), ((float)(*(WORD*)&cbFill[1])) / 100.0);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	bool bOk = sendData((unsigned char*)buf.data(), len);

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

	vector<BYTE> buf;
	int len = 0;
	Parse315Protocol::Unparse(data, buf, len);
	
	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;
	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;
	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;
	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);

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

	vector<BYTE> buf;
	int len = 0;
	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
}


void ioDev_dcqk::SendCallBack0x41(StElecCurve* lpsubdata)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = 0xFF;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StElecCurveRec subdata;
	subdata = *lpsubdata;

	data.datalen = sizeof(StElecCurveRec);
	data.lpdata = &subdata;

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
}

void ioDev_dcqk::SendCallBack0x25(StOilPreCurve* lpsubdata)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = 0xFF;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StOilPreCurveRec subdata;
	subdata = *lpsubdata;

	data.datalen = sizeof(StOilPreCurveRec);
	data.lpdata = &subdata;

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
}

void ioDev_dcqk::SendCallBack0x27(StAlarmAndImgInfo* lpsubdata)
{
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = 0xFF;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;

	StAlarmAndImgRec subdata;
	subdata = *lpsubdata;

	data.datalen = sizeof(StAlarmAndImgRec);
	data.lpdata = &subdata;

	vector<BYTE> buf;
	int len = 0;

	Parse315Protocol::Unparse(data, buf, len);

	sendData((unsigned char*)buf.data(), len);
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
	auto* pStation = prj.queryObj(m_strTagBind, "zh");
	if (!pStation) return;

	OBJ* pMo = pStation->getObjByID(to_string(lpsubdata->sid));
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
				TIME ti; ti.fromUnixTime(lpsubdata->time);
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

BYTE* reqCompose_0x2A(WORD zzjid, DWORD time, BYTE imgType)
{
	static BYTE req[27]{ 0x71, 0x6B, 0x6E, 0x65, 0x74, 0x02, 0x01, 0x8F, 0x0B, 0x00, 0x00, 0x00 };
	BYTE cmdFrame[8] = { 0x2A };
	memcpy(&cmdFrame[1], &zzjid, 2);
	memcpy(&cmdFrame[3], &time, 4);
	BYTE t = imgType;
	cmdFrame[7] = imgType;
	memcpy(req + 12, cmdFrame, 11);
	BYTE endFrame[7] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	memcpy(req + 20, endFrame, 7);
	return req;
}


void ioDev_dcqk::Do_CMD_CODE_GAPVAL(LPVOID pData)
{
	auto* pStation = prj.queryObj(m_strTagBind, "zh");
	if (!pStation) return;

	StGapValue* pInfo = (StGapValue*)pData;
	for (int i = 0; i < pInfo->cnt; i++) {
		auto list = (StGapRecord*)(pInfo->lpdata);
		StGapRecord* pRecord = &(list[i]);
		//if (pRecord->sid > m_mapEqp.size()) {
		//	return;
		//}
		//TDS配置的名字不一定和JHD一致 下载文件时的url以JHD的转辙机名字为准
		//m_mapSIDToName;
		//发送0x26命令
		BYTE* req_0x2A_0x00 = reqCompose_0x2A(pRecord->sid, pRecord->time, 0x00);
		sendData(req_0x2A_0x00, 27);
		BYTE* req_0x2A_0x02 = reqCompose_0x2A(pRecord->sid, pRecord->time, 0x02);
		sendData(req_0x2A_0x02, 27);
		BYTE* req_0x2A_0x03 = reqCompose_0x2A(pRecord->sid, pRecord->time, 0x03);
		sendData(req_0x2A_0x03, 27);
		OBJ* zzjMo = pStation->getObjByID(to_string(pRecord->sid));
		if (!zzjMo) {
			//log
			continue;
		}
		string zzj =zzjMo->getName("");
		EnterCriticalSection(&m_csEqp);

		string zzj315;
		if (m_mapEqp.find(pRecord->sid) != m_mapEqp.end()) {
			zzj315 = m_mapEqp[pRecord->sid]->eqpName;
		}
		
		LeaveCriticalSection(&m_csEqp);
		BYTE location = pRecord->fixorinvert;
		BYTE acqreason = pRecord->gaptype;

		string theTag;
		if (pRecord->fixorinvert == 1) {
			theTag += zzjMo->getTag() + ".反位缺口";
		}
		else {
			theTag += zzjMo->getTag() + ".定位缺口";
		}

		TIME ti;
		ti.fromUnixTime(pRecord->time);
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
	}
}

void uploadJson(json& mainJson, string time, StImgInfoRes* imgInfo, short temper, WORD humi) {
	WORD gapValue    = imgInfo->gap;
	BYTE imgType     = imgInfo->imgtype;
	WORD shift       = imgInfo->offset;
	BYTE shiftSymbol = imgInfo->lrsign;
	BYTE location    = imgInfo->fixorinvert;

	mainJson["time"] = time;
	mainJson["value"] = str::format("%.2f", gapValue * 1.0 / 100);

	json alarmStatus;
	alarmStatus["level"] = "alarm";
	alarmStatus["lockgap"] = false;

	mainJson["alarmStatus"] = alarmStatus;
	mainJson["bShyWindow"] = false;
	mainJson["location"] = imgInfo->fixorinvert;
	mainJson["timeout"] = 0;
	mainJson["gapImageType"] = "jpg";
	mainJson["baseline"] = 0;
	mainJson["gapline"] = 0;


	json dataAttr = json::array();

	json acqreason;
	acqreason["name"] = "acqreason";
	acqreason["label"] = "";
	acqreason["value"] = imgType;
	acqreason["unit"] = "";
	dataAttr.push_back(acqreason);

	json shiftvalue;
	shiftvalue["name"] = "shiftvalue";
	shiftvalue["label"] = "";
	shiftvalue["value"] = str::format("%.2f", shift * 1.0 / 100);
	shiftvalue["unit"] = "";
	dataAttr.push_back(shiftvalue);

	json leftorright;
	leftorright["name"] = "leftorright";
	leftorright["label"] = "";
	shiftSymbol = (shiftSymbol == 1) ? 1 : 0;
	leftorright["value"] = shiftSymbol;
	leftorright["unit"] = "";
	dataAttr.push_back(leftorright);
	json temperature;
	temperature["name"] = "temperature";
	temperature["label"] = "";
	temperature["value"] = temper * 1.0 / 100;
	temperature["unit"] = "";
	dataAttr.push_back(temperature);
	json humidity;
	humidity["name"] = "humidity";
	humidity["label"] = "";
	humidity["value"] = humi * 1.0 / 100;
	humidity["unit"] = "";
	dataAttr.push_back(humidity);
	json diffvalue;
	diffvalue["name"] = "diffvalue";
	diffvalue["label"] = "";
	diffvalue["value"] = "0.00";
	diffvalue["unit"] = "";
	dataAttr.push_back(diffvalue);
	mainJson["data_attr"] = dataAttr;
}

bool saveJpg(string tag, DB_TIME stTime, char* pData, size_t len)
{
	string folder = db.getPath_dataFolder(tag, stTime);
	size_t buffLen = len;
	char* buff = new char[buffLen];
	memcpy(buff, pData, len);
	string path = folder + "/" + stTime.toStampHMS() + ".jpg";
	bool ret = DB_FS::writeFile(path, buff, buffLen);
	delete[] buff;
	return ret;
}

static map<string, short>tagToTemperature;
static map<string, WORD>tagToHumidity;

std::string timeToString(DWORD unixTime) {
	std::time_t rawTime = static_cast<std::time_t>(unixTime);
	std::tm* timeInfo = std::localtime(&rawTime);

	std::ostringstream oss;
	oss << std::put_time(timeInfo, "%Y-%m-%d %H:%M:%S");
	return oss.str();
}

time_t strToUnixTime(const std::string& strTime) {
	std::tm tm = {};
	std::istringstream ss(strTime);
	ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
	return mktime(&tm);
}

void ioDev_dcqk::Do_CMD_CODE_IMGINFO(LPVOID pData) {
	StImgInfoRes* imgInfo = (StImgInfoRes*)pData;

	if (imgInfo->imgtype == 3) {
		LOG("Do_CMD_CODE_IMGINFO 过车 进入逻辑");
	}

	TIME ti;
	ti.fromUnixTime(imgInfo->time);
	string strTi = timeopt::st2str(ti);

	DB_TIME stTime;
	stTime.fromStr(strTi);

	if (imgInfo->imgtype == 3) {
		LOG("Do_CMD_CODE_IMGINFO 过车 time: %s", strTi.c_str());
	}

	if (imgInfo->imgtype == 3) {
		LOG("Do_CMD_CODE_IMGINFO 过车 m_strTagBind: %s", m_strTagBind.c_str());
	}

	if (imgInfo->imgtype == 3) {
		LOG("Do_CMD_CODE_IMGINFO 过车 imgInfo->sid: %s", to_string(imgInfo->sid).c_str());
	}

	auto* pStation = prj.queryObj(m_strTagBind, "zh");
	if (!pStation) {
		if (imgInfo->imgtype == 3) {
			LOG("Do_CMD_CODE_IMGINFO 过车 Error pStation不存在");
		}

		return;
	}

	OBJ* zzjMo = pStation->getObjByID(to_string(imgInfo->sid));
	if (!zzjMo) {
		if (imgInfo->imgtype == 3) {
			LOG("Do_CMD_CODE_IMGINFO 过车 Error zzjMo不存在");
		}

		LOG("未找到对应的转辙机对象, id=%s", to_string(imgInfo->sid).c_str());
		return;
	}

	if (imgInfo->imgtype == 3) {
		LOG("Do_CMD_CODE_IMGINFO 过车 zzjTag: %s", zzjMo->getTag().c_str());
	}

	string zzj = zzjMo->getName("");

	EnterCriticalSection(&m_csEqp);
	LeaveCriticalSection(&m_csEqp);

	BYTE location = imgInfo->fixorinvert;
	BYTE acqreason = imgInfo->imgtype;

	string zzjTag = zzjMo->getTag();
	if(tagToHumidity.find(zzjTag) != tagToHumidity.end() && tagToTemperature.find(zzjTag) != tagToTemperature.end()) {
		string theTag = zzjMo->getTag() + ".缺口";

		string folder = db.getPath_dataFolder(theTag, stTime);
		string imgPath = folder + "/" + stTime.toStampHMS() + ".jpg";

		if (imgInfo->imgtype == 3) {
			LOG("Do_CMD_CODE_IMGINFO 过车 theTag：", theTag.c_str());
		}

		if (imgInfo->imgtype == 3) {
			LOG("Do_CMD_CODE_IMGINFO 过车 imgPath：", imgPath.c_str());
		}

		if (!db.fileExist(imgPath)) {
			saveJpg(theTag, stTime, reinterpret_cast<char*>(imgInfo->lpimg), imgInfo->imglen);

			json Jfile;
			uploadJson(Jfile, strTi, imgInfo, tagToTemperature.at(zzjTag), tagToHumidity.at(zzjTag));

			string sDE = Jfile.dump();
			db.Insert(theTag, sDE, &stTime);

			if (imgInfo->imgtype == 3) {
				LOG("Do_CMD_CODE_IMGINFO 过车 执行成功");
			}
		}
		else {
			if (imgInfo->imgtype == 3) {
				LOG("Do_CMD_CODE_IMGINFO 过车 Error db.fileExist(imgPath)");
			}
		}
	}
	else {
		if (imgInfo->imgtype == 3) {
			if (tagToHumidity.find(zzjTag) == tagToHumidity.end()) {
				LOG("Do_CMD_CODE_IMGINFO 过车 Error tagToHumidity.find(zzjTag) == tagToHumidity.end()");
			}

			if (tagToTemperature.find(zzjTag) == tagToTemperature.end()) {
				LOG("Do_CMD_CODE_IMGINFO 过车 Error tagToTemperature.find(zzjTag) == tagToTemperature.end()");
			}
		}
	}
}

void ioDev_dcqk::Do_CMD_CODE_YWINFO(LPVOID pData)
{
	//auto gbkTag = charCodec::utf8_to_gb(m_strTagBind);
	auto* pStation = prj.queryObj(m_strTagBind, "zh");
	if (!pStation) return;
	StOilLevelInfo* pInfo = (StOilLevelInfo*)pData;
	for (int i = 0; i < pInfo->cnt; i++) {
		auto list = (StSdataRecord*)(pInfo->lpdata);
		StSdataRecord* pRecord = &(list[i]);
		
		OBJ* zzjMo = pStation->getObjByID(to_string(pRecord->sid));
		if (!zzjMo) {
			//log
			continue;
		}
		tagToTemperature[zzjMo->getTag()] = pRecord->temperature;
		tagToHumidity[zzjMo->getTag()] = pRecord->humidity;
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


//查询视频列表
void ioDev_dcqk::QueryVedioList()
{
	uint32_t tNow = time(NULL);
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = 0xFF;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;
	data.datalen = sizeof(StVedioListReq);

	StVedioListReq stReqTrigger = { 0 };
	stReqTrigger.cmdid = CMD_CODE_VEDIOLIST;
	stReqTrigger.resqid = 0xFF;
	stReqTrigger.filldata[0] = 0xFF;
	stReqTrigger.filldata[1] = 0xFF;
	stReqTrigger.filldata[2] = 0xFF;
	stReqTrigger.endtime = tNow;
	StVedioListReq stReqPassCar = stReqTrigger;

	stReqTrigger.vediotype = 0x01; // 扳动视频

	stReqPassCar.vediotype = 0x02; // 过车视频

	vector<BYTE> buf;
	int len = 0;

	time_t t = timeopt::SysTime2Unix(m_stLastQueryVedioTime);

	EnterCriticalSection(&m_csEqp);
	for (auto &it : m_mapEqp)
	{
		bool bNeedQuery = false;
		EnterCriticalSection(&it.second->m_csVedioList);
		bNeedQuery = it.second->lstVedioCache.empty();
		LeaveCriticalSection(&it.second->m_csVedioList);

		if (!bNeedQuery) continue;

		if (it.second->timeLastTriggerVedio == 0)
		{
			it.second->timeLastTriggerVedio = tNow - 10 * 24 * 60 * 60; // 10天前
		}
		if (it.second->timeLastPassCarVedio == 0)
		{
			it.second->timeLastPassCarVedio = tNow - 10 * 24 * 60 * 60; // 10天前
		}

		stReqTrigger.begintime = it.second->timeLastTriggerVedio + 1;
		stReqPassCar.begintime = it.second->timeLastPassCarVedio + 1;
		stReqTrigger.sid = it.first;
		stReqPassCar.sid = it.first;

		data.lpdata = &stReqTrigger;

		Parse315Protocol::Unparse(data, buf, len);
		sendData((unsigned char*)buf.data(), len);


		data.lpdata = &stReqPassCar;
		Parse315Protocol::Unparse(data, buf, len);
		sendData((unsigned char*)buf.data(), len);
	}
	LeaveCriticalSection(&m_csEqp);
}


//查询视频
void ioDev_dcqk::QueryVedio(WORD sid, StVedioRecord *pST)
{
	if (pST == NULL) return;
	StFrame data;
	ZeroMemory(&data, sizeof(StFrame));

	memcpy_s(data.fheader, 5, FRAME_HEADER_315, 5);
	data.protocode = PROTOCAL_CODE;
	data.dataversion = 0xFF;
	data.ftype = FRAME_TYPE_DATA;
	data.ftail = FRAME_TAIL_315;
	data.datalen = sizeof(StVedioFileReq);

	StVedioFileReq stReqVedioFile = { 0 };
	stReqVedioFile.cmdid = CMD_CODE_VEDIOFILE;
	stReqVedioFile.vediotype = pST->filldata;
	stReqVedioFile.sid = sid;
	stReqVedioFile.filldata = 0xFFFFFFFF;
	stReqVedioFile.time = pST->time;

	vector<BYTE> buf;
	int len = 0;
	data.lpdata = &stReqVedioFile;

	Parse315Protocol::Unparse(data, buf, len);
	sendData((unsigned char*)buf.data(), len);
}
#endif