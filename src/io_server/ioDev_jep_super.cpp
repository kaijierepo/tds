#include "ioDev_jep_super.h"
#include "ioSrv.h"
#include "JHDEqpProtocol.h"
#include "..\common\dtwrecoge.h"
#include "..\common\common.h"
#include "ioChan.h"

namespace ns_ioDev_jep_super {
	ioDev* createDev()
	{
		return new ioDev_jep_super();
	}
	class createReg {
	public:
		createReg() {
			mapDevCreateFunc["jep-super-device"] = createDev;
			mapDevTypeLabel["jep-super-device"] = "伤损超声分机";
		};
	};
	createReg reg;
}

ioDev_jep_super::ioDev_jep_super()
{
	m_devType = "jep-super-device";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_level = "devcie";
}

ioDev_jep_super::~ioDev_jep_super()
{
}

bool ioDev_jep_super::SendHeartbeatPkt()
{
	JHDEqpPkt req, resp;
	vector<string> vec;  str::split(vec, getIP(), ".");
	int srcId = 0; if (vec.size() == 4) srcId = (WORD)((atoi(vec[2].c_str()) << 8) + (atoi(vec[3].c_str()) & 0xff));
	req.Pack_V2_HEARTBEAT(0, srcId);

	SendPkt(req);
	return true;
}

bool ioDev_jep_super::onRecvData(unsigned char* pData, size_t iLen)
{
	if (!m_bRunning)
		return false;

	JHDEqpPkt pkt;
	if (!pkt.unpack(pData, iLen))
		return false;

	switch (pkt.cmdData.ep2016Data.cmdid2)
	{
	case CMD_JDSP_SUBPROTOCOL:
	{
		handle_CMD_JDSP_SUBPROTOCOL(pkt);
		break;
	}
	case SUPERSONIC_CMD_UPDATE_RESULT:
	case COMM_CMD_UPDATE_RESULT:
	{
		//StUpgradeResult respdata;
		//memcpy(&respdata, (BYTE*)pkt.data.ep2016Data.lpdata, sizeof(respdata));
		////HWND hwnd = ::FindWindow(NULL, SUPERSOIC_HARD_CFG_WINDOW);
		//HWND hwnd = ::FindWindow(NULL, "仪表与分机升级窗口");
		//if (hwnd)
		//	SendMessage(hwnd, UPDATE_OVER_SUPERSONIC, (WPARAM)this, (LPARAM)(&respdata));
	}
	break;
	default:
		break;
	}

	//strLog = "<-------Chaosheng OnRecvData finish------>";
	//m_pLogForRun->Log(LL_WARN, strLog);
	return true;
}

bool ioDev_jep_super::handle_CMD_JDSP_SUBPROTOCOL(JHDEqpPkt& pkt)
{
	if (pkt.ctrlhead.dlen <= 8) {
		return false;
	}
	//string str = (char*)pkt.data.ep2016Data.lpdata, resp = ""; //为什么之前一直没发现异常
	int bufLen = pkt.ctrlhead.dlen - 8;
	char* buf = new char[bufLen + 1]; //之前写成了new char(...) 并且没有delete, 结果jo.parse()一直出错,查了大半天......
	memcpy(buf, pkt.cmdData.ep2016Data.lpdata, bufLen);
	buf[bufLen] = 0;

	//分机发过来的gb2312编码（ansi） 
	string strBuf = charCodec::gb_to_utf8(buf);
	yyjson_doc* jsonDoc = yyjson_read(strBuf.c_str(), strBuf.length(), 0);
	if (!jsonDoc) {

		delete[] buf;
		return false;
	}
	delete[] buf;

	string resp = "";

	yyjson_val* jsonRoot = yyjson_doc_get_root(jsonDoc);
	auto cmdObj = yyjson_obj_get(jsonRoot, "cmd");
	string strCmd = (char*)yyjson_get_str(cmdObj);

	//1.格式化 准备数据元的各部分字段

	//string& strCmd = jo("cmd");
	if (strCmd == "login") {
		setOnline();
	}
	else if ("new_data" == strCmd) {
		//DealNewData(jo);
		Deal_new_data(jsonRoot);
	}
	else if ("broken_report" == strCmd) {//模式0 和 模式1 才有 不再使用了
		//过车后的快速采集 按每个通道的new_data处理
	}
	else if (strCmd == "new_data_notify") { //分机基于振动模块上送的过车开始和结束信号 不一定准确
		Deal_new_data_notify(jsonRoot);
	}
	else if ("failure_inform" == strCmd) {//仪表故障
		Deal_failure_inform(jsonRoot);
	}
	else if (strCmd == "ae.self_diagnose") {//振动模块、温湿度模块故障
		Deal_self_diagnose(jsonRoot);

	}
	yyjson_doc_free(jsonDoc);
	return true;
}

BOOL ioDev_jep_super::Deal_new_data(yyjson_val* jsonRoot, bool bQuick)
{
	//1. 提取ip、通道、 采集方式信息、tag、data 、 data_attr、温度
	//"192.168.4.10/21"  ip/通道,无用
	string strId = yyjson_get_str(yyjson_obj_get(jsonRoot, "id"));
	str::replace(strId, "\\", "/");
	int nPos = strId.find("/");
	string theIp, theChnl;  BYTE byChnl;
	if (nPos != string::npos) {
		theIp = strId.substr(0, nPos);
		theChnl = strId.substr(nPos + 1); // '1' ~ '8'
		byChnl = *(theChnl.c_str()) - 0x30 - 1; // 0 ~ 7
	}
	if (byChnl < 0 || byChnl >= SUPER_SONIC_CHNL_NUMS) {
		//log  //如果是超声曲线，则通道没法正常更新。不能再发采集命令。
		return false;
	}

	auto infoObj = yyjson_obj_get(jsonRoot, "info");
	string info;
	if (infoObj)
		info = yyjson_get_str(infoObj);
	if (info == "") {
		info = "00000000";//兼容老版本分机程序
	}


	yyjson_val* jsData = yyjson_obj_get(jsonRoot, "data");
	if (!jsData) {
		return false;
	}

	yyjson_val* jsDataAttr = yyjson_obj_get(jsData, "data_attr");

	yyjson_val* element = yyjson_arr_get_first(jsDataAttr);
	float fCurTemp = 0.0;
	if (element && string(yyjson_get_str(yyjson_obj_get(element, "name"))) == "temperature") {
		fCurTemp = yyjson_get_real(yyjson_obj_get(element, "val"));
	}
	if (m_justStarted) {
		//记录分机第一包数据的温度
		m_fTempWhenStarted = fCurTemp;
		m_justStarted = false;
	}

	//2. 转换原始数据,计算统计值 [-11, -183, -193,...] -> [{"x":10,"y":-11},...]
	string end_time;
	auto obj0 = yyjson_obj_get(jsData, "end_time");
	if (obj0) 	end_time = yyjson_get_str(obj0);

	string start_time;
	obj0 = yyjson_obj_get(jsData, "start_time");
	if (obj0) start_time = yyjson_get_str(obj0);

	int single_size = -1;
	obj0 = yyjson_obj_get(jsData, "single_size");
	if (yyjson_is_int(obj0)) single_size = yyjson_get_int(obj0);
	else if (yyjson_is_str(obj0)) single_size = atoi(yyjson_get_str(obj0));
	else
		return false;

	int central_freq = 0x7fffffff;
	obj0 = yyjson_obj_get(jsData, "central_freq");
	if (yyjson_is_int(obj0)) central_freq = yyjson_get_int(obj0);
	else if (yyjson_is_str(obj0)) central_freq = atoi(yyjson_get_str(obj0));
	else
		return false;

	int step = -1;
	obj0 = yyjson_obj_get(jsData, "step");
	if (yyjson_is_int(obj0)) step = yyjson_get_int(obj0);
	else if (yyjson_is_str(obj0)) step = atoi(yyjson_get_str(obj0));
	else
		return false;

	yyjson_val* jsPt = yyjson_obj_get(jsData, "pt");
	if (false == yyjson_is_arr(jsPt)) {

		return false;
	}

	//	原始数据的时间(实验或现场)
	//string sSourceTime;
	//joData.Get("sourcetime", sSourceTime);  好像没用
	//string strTestLog = StringUtils::format("sourcetime-----%s;", sSourceTime);
	//m_pLogChnl[byChnl]->Log(LL_WARN, strTestLog);  

	int sin_size = single_size;
	int cen_freq = central_freq;
	int nStep = step;

	yyjson_mut_doc* mut_doc = yyjson_mut_doc_new(0);//对应joDataEle
	yyjson_mut_val* joXYList = yyjson_mut_arr(mut_doc);

	int maxY = -0xfffffff;  int minY = 0xfffffff;  int index = -1;
	float sum = 0; float fAvg = 0;
	for (int i = sin_size; i >= -sin_size; i--) {
		int x = cen_freq - nStep * i + m_tanTouCentralFrq;
		int y = -0xfffffff;

		yyjson_val* element = yyjson_arr_get(jsPt, sin_size - i);
		y = yyjson_get_int(element);

		if (y == -0xfffffff) {
			continue;
		}

		//yyjson_mut_val* val_obj = yyjson_mut_obj(mut_doc);
		//yyjson_mut_obj_add_int(mut_doc, val_obj, "x", x);
		//yyjson_mut_obj_add_int(mut_doc, val_obj, "y", y);
		//yyjson_mut_arr_add_val(joXYList, val_obj);
		yyjson_mut_val* val_obj = yyjson_mut_arr(mut_doc);
		yyjson_mut_arr_add_val(val_obj, yyjson_mut_int(mut_doc, x));
		yyjson_mut_arr_add_val(val_obj, yyjson_mut_int(mut_doc, y));
		yyjson_mut_arr_add_val(joXYList, val_obj);

		//统计
		if (i == sin_size) {
			maxY = y;
			minY = y;
			index = x;
		}
		else {
			if (y > maxY) {
				maxY = y;
				index = x;
			}
			if (y < minY)
				minY = y;
		}
		sum += y;
	}
	fAvg = sum / (2 * sin_size + 1);

	m_LastMax[byChnl] = maxY;
	m_LastMaxFrq[byChnl] = index;

	size_t len0 = 0;
	char* debug = yyjson_mut_val_write(joXYList, YYJSON_WRITE_PRETTY_TWO_SPACES, &len0);
	free(debug);
	//3.存储时间、 采集方式

	TIME  stDataTime;
	if (end_time == "") {
		timeopt::now(&stDataTime);
	}
	else {
		stDataTime = timeopt::str2st(end_time);
	}

	//采集方式
	//4种 0: 模式2常规采,1:模式2快速采集,2:模式2人工采, 0x80:模式0常规采
	//string strCapWay = 2 * sin_size + 1 == 21 ? "quick" : "normal";
	int cmdType = str::TwoHexStringToInt(info.substr(0, 2).c_str());
	string strCapWay = "normal";
	if (cmdType == 0) {

	}
	else if (cmdType == 1) {
		strCapWay = "quick";
	}
	else if (cmdType == 2) {
		strCapWay = "manu";
	}
	else if (cmdType == 0x80) {
		strCapWay = "normal";
	}

	//4. 计算 是否和过车有重叠
	//曲线结束时间之前最近的一个过车信号，是结束信号且小于曲线结束时间则无重叠
	//根据重叠计算 是否正被干扰 、 当前一条相对上条的期间内是否有振动（后续再补充 上下位机需保证振动信号的处理最优先）
	//timeOpt::SysTimeToUnix(timeOpt::StringToStTime(start_time));

	int  nEndTime = timeopt::SysTime2Unix(stDataTime);
	int nStartTime;
	if (start_time == "") {
		nStartTime = nEndTime - yyjson_mut_arr_size(joXYList) * 0.1; //往大了预估  一个点0.1s
	}
	else {
		nStartTime = timeopt::SysTime2Unix(timeopt::str2st(start_time));
	}

	string strStartTi = timeopt::st2str(timeopt::Unix2SysTime(nStartTime));

	string strTime1 = timeopt::st2str(stDataTime);
	string theTi = "1e~" + strTime1;
	string strCarState = "none";
	/*  振动信号很多的话 读历史文件性能差  且内部用到了cjson。 后续改为内存中判断
	//查询在nStartTime之后的历史数据 结果集只要第一个
	CJsonObject dataSet; CJsonObject meta;
	string strTag0 = daochaName + ".过车信号p";
	g_pLocalEqpMng->DB->SELECT(strTag0, theTi, "", dataSet, meta, 1);
	int dataSetSize = dataSet.GetArraySize();
	if (0 == dataSetSize) {
		//可能没历史数据 按无过车处理
	}
	else if (1 == dataSetSize) {
		bool bVal = false;   dataSet[0].Get("val", bVal);
		string& theTi = dataSet[0]("time");
		int nTheTime = timeOpt::SysTimeToUnix(timeOpt::StringToStTime(theTi));
		if (nTheTime < nStartTime && !bVal) {
			strCarState = "none";
		}
		else {
			strCarState = "overlap";
		}
	}
	*/
	string strDisturb = "none"; //第一条算none  快速采集为none   当前是否正被振动干扰

	//5.计算常规曲线中 当前和上条、上上条的差异
	string strLastDiff, strLast2Diff;
	int nLastStart = 0, nLastEnd = 0, nLast2Start = 0, nLast2End = 0;
	if (strCapWay == "normal")
	{
		int si = m_vecNormalHisBuf[byChnl].size();
		if (si >= 2) {
			auto& ele = m_vecNormalHisBuf[byChnl][si - 1]; //上一条
			if (ele.m_joXYList_doc) {
				yyjson_mut_val* joXYList0 = yyjson_mut_doc_get_root(ele.m_joXYList_doc);
				yyjson_mut_val* joXYList00 = yyjson_mut_doc_get_root(m_vecNormalHisBuf[byChnl][si - 2].m_joXYList_doc);

				if (yyjson_mut_is_arr(joXYList0) && yyjson_mut_is_arr(joXYList00)) {
					vector<double> p, p0, p00;
					yyjson_XYJoToList(joXYList, p);
					yyjson_XYJoToList(joXYList0, p0);
					yyjson_XYJoToList(joXYList00, p00);

					//double dtw = DTWDistanceFun(pCur->data(), pCur->size(), pBase->data(), pBase->size(), pBase->size() / 10);
					float dis0 = DTWDistanceFun(p.data(), p.size(), p0.data(), p0.size(), p.size()/10);
					float dis00 = DTWDistanceFun(p.data(), p.size(), p00.data(), p00.size(), p.size() / 10);

					//if (FilterCurveBySelf(joXYList, byChnl)) { // 这个干扰以及期间干扰通过上一步骤判断 这里不用了
					//	strDisturb = "jump";

						//string strLog = StringUtils::format("jump:result[%f],p1[%s];compare[%f]",
							//dis0, timeOpt::TimeToString(timeOpt::UnixToSysTime(ele.nStartTime)).c_str(),   fRes);
						//m_pLogChnl[byChnl]->Log(LL_WARN, strLog);
					//}

					//if (strDisturb != "jump")
					{
						strLastDiff = str::format("%.1f", dis0);
						nLastStart = ele.nStartTime;
						nLastEnd = ele.nEndTime;

						strLast2Diff = str::format("%.1f", dis00);
						nLast2Start = m_vecNormalHisBuf[byChnl][si - 2].nStartTime;
						nLast2End = m_vecNormalHisBuf[byChnl][si - 2].nEndTime;
					}
				}
			}
		}

		//if (strDisturb != "jump")
		{
			S_NormalHisBuffer  one;

			yyjson_mut_doc* mut_doc0 = yyjson_mut_doc_new(0);
			yyjson_mut_val* joXYList1 = yyjson_mut_val_mut_copy(mut_doc0, joXYList);
			yyjson_mut_doc_set_root(mut_doc0, joXYList1);

			one.m_joXYList_doc = mut_doc0;
			one.nStartTime = nEndTime;

			if (m_vecNormalHisBuf[byChnl].size() >= 2) {
				yyjson_mut_doc* mut_doc0 = m_vecNormalHisBuf[byChnl].begin()->m_joXYList_doc;
				yyjson_mut_doc_free(mut_doc0);
				m_vecNormalHisBuf[byChnl].erase(m_vecNormalHisBuf[byChnl].begin());
			}
			m_vecNormalHisBuf[byChnl].push_back(one);
		}
	}

	//6.准备并更新数据元 
	yyjson_mut_val* joDataEle = yyjson_mut_obj(mut_doc);
	yyjson_mut_doc_set_root(mut_doc, joDataEle);

	yyjson_mut_obj_add_strcpy(mut_doc, joDataEle, "data_fmt", "point.xy");
	yyjson_mut_obj_add_strcpy(mut_doc, joDataEle, "start", to_string(nStartTime).c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joDataEle, "end", to_string(nEndTime).c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joDataEle, "unit_y", "RL");
	yyjson_mut_obj_add_strcpy(mut_doc, joDataEle, "unit_x", "Hz");

	yyjson_mut_obj_add_strcpy(mut_doc, joDataEle, "CarSta", strCarState.c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joDataEle, "Disturb", strDisturb.c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joDataEle, "CapWay", strCapWay.c_str());

	{
		yyjson_mut_val* jo = yyjson_mut_obj(mut_doc);
		yyjson_mut_obj_add_strcpy(mut_doc, jo, "start", to_string(nLastStart).c_str());
		yyjson_mut_obj_add_strcpy(mut_doc, jo, "end", to_string(nLastEnd).c_str());
		yyjson_mut_obj_add_strcpy(mut_doc, jo, "val", strLastDiff.c_str());
		yyjson_mut_obj_add_val(mut_doc, joDataEle, "lastDi", jo);
	}
	{
		yyjson_mut_val* jo = yyjson_mut_obj(mut_doc);
		yyjson_mut_obj_add_strcpy(mut_doc, jo, "start", to_string(nLast2Start).c_str());
		yyjson_mut_obj_add_strcpy(mut_doc, jo, "end", to_string(nLast2End).c_str());
		yyjson_mut_obj_add_strcpy(mut_doc, jo, "val", strLast2Diff.c_str());
		yyjson_mut_obj_add_val(mut_doc, joDataEle, "last2Di", jo);
	}
	yyjson_mut_obj_add_val(mut_doc, joDataEle, "data", joXYList);

	//data_attr
	//attr_fields:["name","label","val"]
	//data_attr:[[],...]

	yyjson_mut_val* joAttrFields = yyjson_mut_arr(mut_doc);
	yyjson_mut_val* joOne = yyjson_mut_strcpy(mut_doc, "name");
	yyjson_mut_arr_add_val(joAttrFields, joOne);
	yyjson_mut_arr_add_val(joAttrFields, yyjson_mut_strcpy(mut_doc, "label"));
	yyjson_mut_arr_add_val(joAttrFields, yyjson_mut_strcpy(mut_doc, "val"));
	yyjson_mut_obj_add_val(mut_doc, joDataEle, "attr_fields", joAttrFields);

	yyjson_mut_val* joAttr = yyjson_mut_arr(mut_doc);

	yyjson_mut_val* joItem = yyjson_mut_arr(mut_doc);
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "startTi"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, charCodec::gb_to_utf8(string("开始时间")).c_str()));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, strStartTi.c_str()));
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_arr(mut_doc);
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "Max"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, charCodec::gb_to_utf8(string("最大值")).c_str()));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, to_string(maxY).c_str()));
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_arr(mut_doc);
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "Min"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, charCodec::gb_to_utf8(string("最小值")).c_str()));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, to_string(minY).c_str()));
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_arr(mut_doc);
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "Avg"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, charCodec::gb_to_utf8(string("平均值")).c_str()));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, str::format("%.4f", fAvg).c_str()));
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_arr(mut_doc);
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "FeaFreq"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, charCodec::gb_to_utf8(string("特征频率")).c_str()));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, to_string(index).c_str()));
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_arr(mut_doc);
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "CarSta"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, charCodec::gb_to_utf8(string("过车状态")).c_str()));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, strCarState.c_str()));
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_arr(mut_doc);
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "Disturb"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, charCodec::gb_to_utf8(string("干扰状态")).c_str()));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, strDisturb.c_str()));
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_arr(mut_doc);
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "CapWay"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, charCodec::gb_to_utf8(string("采集方式")).c_str()));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, strCapWay.c_str()));
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_arr(mut_doc);
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "lastDi"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "last"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, strLastDiff.c_str()));
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_arr(mut_doc);
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "last2Di"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, "last2"));
	yyjson_mut_arr_add_val(joItem, yyjson_mut_strcpy(mut_doc, strLast2Diff.c_str()));
	yyjson_mut_arr_add_val(joAttr, joItem);


#if 0
	yyjson_mut_val* joAttr = yyjson_mut_arr(mut_doc);

	yyjson_mut_val* joItem = yyjson_mut_obj(mut_doc);
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "name", "startTi");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "label", ANSItoUTF8("开始时间").c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "val", strStartTi.c_str());
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_obj(mut_doc);
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "name", "Max");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "label", ANSItoUTF8("最大值").c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "val", to_string(maxY).c_str());
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_obj(mut_doc);
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "name", "Min");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "label", ANSItoUTF8("最小值").c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "val", to_string(minY).c_str());
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_obj(mut_doc);
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "name", "Avg");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "label", ANSItoUTF8("平均值").c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "val", StringUtils::format("%.4f", fAvg).c_str());
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_obj(mut_doc);
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "name", "FeaFreq");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "label", ANSItoUTF8("特征频率").c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "val", to_string(index).c_str());
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_obj(mut_doc);
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "name", "CarSta");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "label", ANSItoUTF8("过车状态").c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "val", strCarState.c_str());
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_obj(mut_doc);
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "name", "Disturb");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "label", ANSItoUTF8("干扰状态").c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "val", strDisturb.c_str());
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_obj(mut_doc);
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "name", "CapWay");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "label", ANSItoUTF8("采集方式").c_str());
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "val", strCapWay.c_str()); //normal 正常轮训 quick 快速轮训
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_obj(mut_doc);
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "name", "lastDi");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "label", "last");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "val", strLastDiff.c_str());
	yyjson_mut_arr_add_val(joAttr, joItem);

	joItem = yyjson_mut_obj(mut_doc);
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "name", "last2Di");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "label", "last2");
	yyjson_mut_obj_add_strcpy(mut_doc, joItem, "val", strLast2Diff.c_str());
	yyjson_mut_arr_add_val(joAttr, joItem);
#endif
	yyjson_mut_obj_add_val(mut_doc, joDataEle, "data_attr", joAttr);

	yyjson_mut_obj_add_strcpy(mut_doc, joDataEle, "time", strTime1.c_str());
	debug = yyjson_mut_val_write(joDataEle, 0, &len0);
	free(debug);


	//存储数据 并计算   
	string region = m_strRail == "0" ? "尖轨区" : "心轨区";
	/*
	CJsonObject joInfo;  发给前置机的 没用了
	joInfo.Add("type", "ori");//original
	string installPos = string(m_strMonitorObjTag) + "." + region;
	joInfo.Add("installPos", installPos);
	*/

	/*
	curve.list
	{
		"data_fmt":	"de_idx.curve.list",
		"data":	[{
				"time":	"11:32:18",
				"data_attr":	[{..},..],
				"file":	{...}//curve文件里的东西
				},
				...
		]
	}
	*/
	//string datafmt = "curve.xy";
	//if (0 != g_pSysPara->m_nDamageCurveSaveType)
	//	datafmt = "curve.list"; //把曲线都存到list文件中  影响性能 不用了
	//pMpDPQX_S->UpdateData_yyjson(stDataTime, datafmt, mut_doc, joDataEle, "", nullptr, "");

	ioChannel* pCh = getChanByDevAddr(theChnl);
	string sMpTag;
	if (pCh)
	{
		//pCh->input(joDataEle, &stDataTime);

		sMpTag = charCodec::utf8_to_gb(pCh->getTagBind());
		
	}

	//g_localDamageMng.ProcessDamage(stDataTime, szTag, "");

	//保证实时和历史数据已全部存好 再更新 方便后续处理


	yyjson_mut_doc_free(mut_doc);
	mut_doc = NULL;
	return true;
}

BOOL ioDev_jep_super::Deal_failure_inform(yyjson_val* jsonRoot)
{
#if 0
	string strLog = StringUtils::format("IP:[%s]. FailureError:%s.", m_strIP.GetString(), jo.ToString().c_str());
	m_pLogForRun->Log(LL_WARN, strLog);

	//	zzx Add
	SYSTEMTIME stDataTime;
	GetLocalTime(&stDataTime);

	string sfmt = jo("cmd");
	string sErrorCode = jo("error_code");
	string sError_desc = jo("error_desc");

	string ip = GetCommIP();

	CJsonObject& joReq = jo;
	joReq.Delete("cmd");
	joReq.Delete("error_code");
	joReq.Delete("error_desc");
	joReq.Add("module", string(m_strMonitorObjTag));
	joReq.Add("error_type", sErrorCode);

	string sFormat = joReq.ToFormattedString();

	CMonitorObject* pMo = g_pLocalEqpMng->GetMOByName(m_strMonitorObjTag);
	CMonitorPoint* pMp = nullptr;
	if (pMo)
	{
		//	发送伤损告警给TDS
		{
			string sTime = timeOpt::StTime2str(stDataTime);

			//string sErrorDesc = StringUtils::format("仪表故障：%s,%s", sErrorCode.c_str(), sError_desc.c_str());
			string sErrorDesc = StringUtils::format("道岔：%s，%s", m_strMonitorObjTag.GetString(), m_strIP.GetString());

			CString sStationName = g_pSysPara->GetConfigVal("TdsStationName", "");
			if (!sStationName.IsEmpty())
			{
				sStationName += ".";
				sStationName += m_strMonitorObjTag;
			}
			else
			{
				sStationName = m_strMonitorObjTag;
			}
			string sErrorTag = StringUtils::format("%s.[%s]", sStationName.GetString(), m_strIP.GetString());

			CJsonObject joHttpData;
			joHttpData.Add("jsonrpc", "2.0");
			joHttpData.Add("method", "updateAlarmStatus");
			CJsonObject joParam;
			joParam.Add("time", sTime);
			joParam.Add("desc", sErrorDesc);
			joParam.Add("level", "二级");
			joParam.Add("tag", sErrorTag);
			joParam.Add("type", "系统故障-仪表通信故障");
			joParam.Add("id", "");

			joHttpData.Add("params", joParam);
			joHttpData.Add("id", 1);
			joHttpData.Add("dbPath", "faults");
			string body = joHttpData.ToString();
			body = ANSItoUTF8(body);  //发送ansi返回 400

			thread(
				[pHttpClt, pcsHttpClt, body]() {
					std::unique_lock<shared_mutex> lock(*pcsHttpClt);
					//HttpSendMsgToTds(pHttpClt,  body); //暂时屏蔽
				}
			).detach();

			joHttpData.Replace("dbPath", "faultsDevelop");
			joHttpData.Add("dbTest", "true");
			body = joHttpData.ToString();
			body = ANSItoUTF8(body);  //发送ansi返回 400

			thread(
				[pHttpClt, pcsHttpClt, body]() {
					std::unique_lock<shared_mutex> lock(*pcsHttpClt);
					HttpSendMsgToTds(pHttpClt, body);  //暂时屏蔽
				}
			).detach();
		}

		//生成数据
		string sTime = timeOpt::StTime2str(stDataTime);

		vector<string> arrMp;
		g_pDataAcqServer->GetAllMPTagByIp(m_strIP.GetString(), arrMp);

		srand((unsigned)time(0));

		//float fVal = rand() % 5000 / (100.0*1000);

		for (auto i : arrMp)
		{
			CMonitorPoint* pMp = g_pLocalEqpMng->GetMPByTag(i);
			if (pMp == nullptr)
				continue;

			float fVal = static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (0.05)));

			string strInxTag = pMp->m_pParentMO->GetTagName() + ".伤损指数";

			if (pMp->m_pParentMO->GetTagName().find("子段") != string::npos)
			{
				strInxTag = pMp->m_pParentMO->m_pParentMO->GetTagName() + ".伤损指数";
			}

			auto pMpInx = g_pLocalEqpMng->GetMPByTag(strInxTag);
			if (pMpInx) {
				time_t t0 = timeOpt::SysTime2Unix(stDataTime);
				auto st0 = timeOpt::Unix2SysTime(t0 + rand() % 60);
				CJsonObject jo; jo.Add("invalid", true);
				pMpInx->UpdateData(st0, fVal, &jo, "");//不同区段的时间应有差异
			}

			CString sStationName = g_pSysPara->GetConfigVal("TdsStationName", "");
			if (!sStationName.IsEmpty())
			{
				string sTmp = sStationName.GetString();
				sTmp += ".";
				strInxTag = sTmp + strInxTag;
			}

			CJsonObject joHttpData;
			joHttpData.Add("jsonrpc", "2.0");
			joHttpData.Add("method", "input");

			CJsonObject joParam;
			joParam.Add("tag", strInxTag);
			joParam.Add("time", sTime);
			joParam.Add("val", fVal);
			joParam.Add("invalid", true);

			joHttpData.Add("params", joParam);
			joHttpData.Add("id", 1);

			string body = joHttpData.ToString();
			body = ANSItoUTF8(body);  //发送ansi返回 400

			g_localDamageMng.m_pThreadPool->enqueue([pHttpClt, pcsHttpClt, body]() {
				std::unique_lock<shared_mutex> lock(*pcsHttpClt);
				HttpSendMsgToTds(pHttpClt, body);
				});

			//	上送温度
			fVal = 10 + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (20 - 10)));

			strInxTag = pMp->m_pParentMO->GetTagName() + ".温度";

			if (pMp->m_pParentMO->GetTagName().find("子段") != string::npos)
			{
				strInxTag = pMp->m_pParentMO->m_pParentMO->GetTagName() + ".温度";
			}

			if (!sStationName.IsEmpty())
			{
				string sTmp = sStationName.GetString();
				sTmp += ".";
				strInxTag = sTmp + strInxTag;
			}

			joParam.Replace("tag", strInxTag);
			joParam.Replace("val", fVal);
			joHttpData.Replace("params", joParam);

			body = joHttpData.ToString();
			body = ANSItoUTF8(body);  //发送ansi返回 400

			g_localDamageMng.m_pThreadPool->enqueue([pHttpClt, pcsHttpClt, body]() {
				std::unique_lock<shared_mutex> lock(*pcsHttpClt);
				HttpSendMsgToTds(pHttpClt, body);
				});


			//	上送湿度
			fVal = 20 + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (30 - 20)));

			strInxTag = pMp->m_pParentMO->GetTagName() + ".湿度";

			if (pMp->m_pParentMO->GetTagName().find("子段") != string::npos)
			{
				strInxTag = pMp->m_pParentMO->m_pParentMO->GetTagName() + ".湿度";
			}

			if (!sStationName.IsEmpty())
			{
				string sTmp = sStationName.GetString();
				sTmp += ".";
				strInxTag = sTmp + strInxTag;
			}

			joParam.Replace("tag", strInxTag);
			joParam.Replace("val", fVal);
			joHttpData.Replace("params", joParam);

			body = joHttpData.ToString();
			body = ANSItoUTF8(body);  //发送ansi返回 400

			g_localDamageMng.m_pThreadPool->enqueue([pHttpClt, pcsHttpClt, body]() {
				std::unique_lock<shared_mutex> lock(*pcsHttpClt);
				HttpSendMsgToTds(pHttpClt, body);
				});
		}



		pMp = pMo->GetMPByName("仪表故障");
	}

	if (pMp)
	{
		//	如果道岔上有配监测点
		pMp->SetCurValJo(joReq, stDataTime);
		string sTag = m_strMonitorObjTag + ".仪表故障";
		pMp->GetDB()->INSERT_(sTag.c_str(), stDataTime, joReq, sfmt);
	}
#endif
	return 1;
}

BOOL ioDev_jep_super::Deal_self_diagnose(yyjson_val* jsonRoot)
{
#if 0
	string strLog = StringUtils::format("IP:[%s]. Self_diagnoseError:%s.", m_strIP.GetString(), jo.ToString().c_str());
	m_pLogForRun->Log(LL_WARN, strLog);

	CJsonObject joSend = jo;
	joSend.Delete(">");
	joSend.Add("identify", string(g_pLocalEqpMng->m_strName));
	joSend.Add("ip", string(m_strIP));
	joSend.Add("daocha", string(m_strMonitorObjTag));//目前伤损必须配在道岔上
	joSend.Add(">", "");

	string strResp;
	//g_pLocalHealthMng->DealSelfDiagnose(joSend, strResp); //已移至前置机中 

	//发送给前置机
	//g_pLocalHealthMng->SendtoHM(joSend.ToString());

	//	zzx Add
	SYSTEMTIME stDataTime;
	GetLocalTime(&stDataTime);

	//	发送伤损告警给TDS
	{
		string sTime = timeOpt::StTime2str(stDataTime);

		string sErrorType;
		if (jo("module") == "vibration")
		{
			sErrorType = "系统自检-震动传感器故障";
		}
		else if (jo("module") == "SHT3X")
		{
			sErrorType = "系统自检-温湿度传感器故障";
		}
		else
		{
			sErrorType = "系统自检-未知故障";
		}

		string sErrorDesc = StringUtils::format("%s，道岔：%s，分机IP：%s", sErrorType.c_str(), m_strMonitorObjTag.GetString(), m_strIP.GetString());

		CString sStationName = g_pSysPara->GetConfigVal("TdsStationName", "");

		if (!sStationName.IsEmpty())
		{
			sStationName += ".";
			sStationName += m_strMonitorObjTag;
		}
		else
		{
			sStationName = m_strMonitorObjTag;
		}

		string sErrorTag = StringUtils::format("%s[.%s]", sStationName.GetString(), m_strIP.GetString());

		CJsonObject joHttpData;
		joHttpData.Add("jsonrpc", "2.0");
		joHttpData.Add("method", "updateAlarmStatus");
		CJsonObject joParam;
		joParam.Add("time", sTime);
		joParam.Add("desc", sErrorDesc);
		joParam.Add("level", "二级");
		joParam.Add("tag", sErrorTag);
		joParam.Add("type", sErrorType);
		joParam.Add("id", "");

		joHttpData.Add("params", joParam);
		joHttpData.Add("id", 1);

		string body = joHttpData.ToString();
		body = ANSItoUTF8(body);  //发送ansi返回 400

		thread(
			[pHttpClt, body]() {
				//HttpSendMsgToTds(pHttpClt,body);  //暂时屏蔽
			}
		).detach();

		joHttpData.Add("dbPath", "alarms2");
		body = joHttpData.ToString();
		body = ANSItoUTF8(body);  //发送ansi返回 400

		thread(
			[pHttpClt, body]() {
				//HttpSendMsgToTds(pHttpClt,body);  //暂时屏蔽
			}
		).detach();
	}

	string sfmt = jo("cmd");

	string info = jo("error_info");
	vector<string> vec;
	StringUtils::split(vec, info, ",");
	string ip = GetCommIP();
	string tags;
	for (auto& it : vec) {
		string id = ip + "/" + it;
		string szTag = g_pDataAcqServer->GetMPTagByGlobalDataChannelID(id);

		CMonitorPoint* pMp = g_pLocalEqpMng->GetMPByTag(szTag);

		if (!pMp)
			continue;

		//	统一写入区段下的故障信息监测点
		string sMpName = pMp->m_pParentMO->GetTagName();

		if (pMp->m_pParentMO->GetTagName().find("子段") != string::npos)
		{
			sMpName = pMp->m_pParentMO->m_pParentMO->GetTagName();
		}

		sMpName += ".故障信息";
		pMp = g_pLocalEqpMng->GetMPByTag(sMpName);

		if (!pMp)
			continue;

		CJsonObject joReq = jo;
		joReq.Delete("cmd");
		joReq.Delete("error_info");
		joReq.Add("chnl", id);

		//string sTest = joReq.ToString(); 

		pMp->SetCurValJo(joReq, stDataTime);
		pMp->GetDB()->INSERT_(pMp->GetTagName().c_str(), stDataTime, joReq, sfmt);
	}

#endif
	return 1;
}

BOOL ioDev_jep_super::Deal_new_data_notify(yyjson_val* jsonRoot)
{
#if 0
	//过车状态
	string szTag;  SYSTEMTIME stDataTime;
	string szTime = jo("time");
	if (szTime == "") {
		CJsonObject& joData = jo["data"];
		szTime = joData("time");
	}
	if (szTime == "") {
		GetLocalTime(&stDataTime);
	}
	else {
		stDataTime = timeOpt::StringToStTime(szTime.c_str());
	}
	if (stDataTime.wYear < 2000) {
		//error
		//break;
	}

	if (jo("id").rfind("过车状态") > 0) {

		//处理过车信号
		CDaoChaSignalService* pGMT = (CDaoChaSignalService*)GetDaoChaSignalService();
		if (pGMT) {

			//要求配置采集设备的m_strMonitorObjTag时,必须二级结构： xx道岔.xx尖轨xx段  道岔后面的仅用于区分本道岔的两台电测分机 或两台超声分机, 而不是代表该分机仅监测该轨段。
			//只能配在道岔上
			string ss = m_strMonitorObjTag;
			vector<string> vec;
			string& strDaoCha = ss;

			CGroupInfo* pGroup = g_pLocalEqpMng->GetGroup(strDaoCha.c_str());
			if (pGroup == NULL)
				return FALSE;

			bool bVal; string sVal;
			if (jo["data"].Get("val", bVal)) {
				sVal = bVal ? "true" : "false";
			}
			else {
				//log error
				return false;
			}

			//SYSTEMTIME sSignalDateTime;
			//GetLocalTime(&sSignalDateTime); //取下位机数据包里的时间

			SignalSourceInfo sSignalSourceInfo;
			sSignalSourceInfo.eSignalSource = SIGNAL_EQP;
			sSignalSourceInfo.strZZJName = ""; //伤损设备不在转辙机上
			sSignalSourceInfo.iSignalDateTime = CGlobal::SystemTimeToInt64(stDataTime);

			pGMT->Report(pGroup->m_strName, bVal, sSignalSourceInfo, DAOCHA_SIGNAL_TYPE::REC_TYPE_CROSS, false);
		}

	}
#endif
	return 1;
}

void ioDev_jep_super::yyjson_XYJoToList(yyjson_mut_val* joXYList, vector<double>& p)
{
	size_t idx = 0;  size_t max = 0;  yyjson_mut_val* item;
	yyjson_mut_arr_foreach(joXYList, idx, max, item) {
		if (yyjson_mut_arr_size(item) != 2)
			continue;
		auto Obj = yyjson_mut_arr_get(item, 1);
		if (!Obj) continue;
		float val = 0;
		if (yyjson_mut_is_int(Obj)) {
			val = yyjson_mut_get_int(Obj);
		}
		else if (yyjson_mut_is_str(Obj)) {
			val = atof(yyjson_mut_get_str(Obj));
		}
		else if (yyjson_mut_is_real(Obj)) {
			val = yyjson_mut_get_real(Obj);
		}

		p.push_back(val);
	}
}
