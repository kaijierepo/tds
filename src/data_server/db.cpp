#include "pch.h"
#include "db.h"
#include <iostream>
#include <sstream>
#include <filesystem>
#include "logger.h"
#include "yyjson.h"
#include "scriptManager.h"
#include "prj.h"
#include "tdsSession.h"
#include "scriptEngine.h"

database db;

database::database()
{
	m_path = fs::appPath() + "/db";
}

string database::getPath_deFile(string strTag, TIME stTime)
{
	strTag = str::replace(strTag,".", "/");
	string strURL= str::format("/%04d%02d/%02d/", stTime.wYear, stTime.wMonth, stTime.wDay);
	strURL += strTag;
	string timeStamp = str::format("%02d%02d%02d", stTime.wHour, stTime.wMinute, stTime.wSecond);
	strURL += "/" + timeStamp;
	return strURL;
}

string database::getPath_dbRoot()
{
	return m_path;
}

string database::getName_deFile(string tag, TIME time)
{
	string timeStamp = str::format("%02d%02d%02d", time.wHour, time.wMinute, time.wSecond);
	return timeStamp;
}

//8个不能做文件名的非法字符
string ic1 = str::format("[%02X]", '\\');
string ic2 = str::format("[%02X]", ':');
string ic3 = str::format("[%02X]", '*');
string ic4 = str::format("[%02X]", '?');
string ic5 = str::format("[%02X]", '\"');
string ic6 = str::format("[%02X]", '<');
string ic7 = str::format("[%02X]", '>');
string ic8 = str::format("[%02X]", '|');
string ic9 = str::format("[%02X]", '/');

//对9个文件名非法字符进行转义  / \ : * ? " < > |
string database::changeCharForFileName(string s) {
	string out;
	for (int i = 0; i < s.length(); i++)
	{
		char c = s[i];
		if (c == '\\')
		{
			out.append(ic1);
		}
		else if (c == ':')
		{
			out.append(ic2);
		}
		else if (c == '*')
		{
			out.append(ic3);
		}
		else if (c == '?')
		{
			out.append(ic4);
		}
		else if (c == '\"')
		{
			out.append(ic5);
		}
		else if (c == '<')
		{
			out.append(ic6);
		}
		else if (c == '>')
		{
			out.append(ic7);
		}
		else if (c == '|')
		{
			out.append(ic8);
		}
		else if (c == '/')
		{
			out.append(ic9);
		}
		else
		{
			out.append(1, c);
		}
	}
	return out;
}

string database::getPath_dataFolder(string strTag, TIME date)
{
	strTag = changeCharForFileName(strTag);
	strTag = str::replace(strTag,".", "/");
	string strURL= str::format("/%04d%02d/%02d/", date.wYear, date.wMonth, date.wDay);
	strURL += strTag;
	strURL = m_path  + strURL;
	return strURL;
}

bool database::getDBFile(TIME t, string tag, string fileName)
{
	string folder = str::format("/%04d%02d", t.wYear, t.wMonth);
	if (t.wDay != 0) {
		folder += str::format("/%02d", t.wDay);
	}


	
	return false;
}

string database::getPath_dbFile(string strTag,TIME date,string deType)
{
	string folder = getPath_dataFolder(strTag,date);
	if(deType == "")
		return folder + "/" + m_dbFmt.deListName;
	else if (deType == "curveIdx") {
		return folder + "/" + m_dbFmt.curveIdxListName;
	}
	else if (deType == "curve") {
		return folder + "/" + date.toStampHMS()  + m_dbFmt.curveDeNameSuffix;
	}
	else {
		return folder + "/" + m_dbFmt.deListName;
	}
}



void database::Insert(string strTag, TIME stTime, json& jData, json dataFile)
{
	string folderPath = getPath_dataFolder(strTag, stTime);
	string dlPath = folderPath + "/" + m_dbFmt.deListName;
	if(!fs::fileExist(folderPath))
		fs::createFolderOfPath(folderPath.c_str());
	json jDE;
	jDE["time"] = timeopt::st2str(stTime);
	jDE[m_dbFmt.deItemKey_value.c_str()] = jData;
	if(dataFile != nullptr)
	jDE["dataFile"] = dataFile;
	if (!fs::fileExist(dlPath.c_str()))
	{
		json jDataList;
		jDataList.push_back(jDE);
		string str = jDataList.dump(2);
		if (!fs::writeFile(dlPath, str))
		{
			LOG("[error]写入数据库文件失败,路径:%s,数据:%s", dlPath.c_str(), str.c_str());
		}
	}
	else
	{
#ifdef _WIN32
		FILE* fp = _wfopen(charCodec::utf8_to_utf16(dlPath).c_str(), L"rb+");
#else
		FILE* fp = fopen(dlPath.c_str(), "rb+");
#endif
		
		if (fp)
		{
			fseek(fp, 0L, SEEK_END);
			long len = ftell(fp);

			if (len > 0)
			{
				fseek(fp, len - 1, SEEK_SET);
				std::string d = ",";
				d += jDE.dump(2);
				d += "]";
				fwrite(d.c_str(), 1, d.length(), fp);
			}
			else
			{
				json jDataList;
				jDataList.push_back(jDE);
				string str = jDataList.dump(2);
				fwrite(str.c_str(), 1, str.length(), fp);
			}
			
			fclose(fp);
		}
	}
}

//位号集合的时间截面
struct TAG_SET_TIME_SECTION {
	
};

struct DE_TEMP {
	yyjson_mut_val* de;
	string sortVal;  //用于本次排序的字段的数值

};


//yyjson调试查看不方便，必要时使用此类函数打印出json进行调试
//mut_val 必须拷贝后再put到新的 mut_obj里面，没有拷贝直接put原来get到的 mut_val，会把原来的值给改掉
string printfTimeSection(map<string, yyjson_mut_val*>* timeSection) {
	LOG("********time section dump*********");
	if (timeSection == nullptr)
		LOG("null");
	else {
		for (auto& i : *timeSection) {
			LOG(i.first);
			char* sz = yyjson_mut_val_write(i.second, 0, nullptr);
			LOG(sz);
		}
	}
	return "";
}

bool database::Select_Step_outputRows_SingleCol_timeFill(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc)
{
	map<string, yyjson_mut_val*>& mapRlt = result.mapRlt;
	//返回的数据元是否需要携带tag字段
	bool withTag = tagDBFileSet.size() > 1 ? true : false;
	if (deSel.tagSel.getTag)
		withTag = true;

	map<string, map<string, yyjson_mut_val*>> timeSectionSeries; //时间截面序列，每个时间界面都要补齐各个位号的值


	//生成输出de
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_DATA& fSet = *tagDBFileSet[tagIdx];
		string& tagAlias = fSet.colKey;
		string& tag = fSet.tag;

		for (int j = 0; j < fSet.m_afterAggr.size(); j++) {
			DE_yyjson& deyy = *fSet.m_afterAggr[j];

			//创建一个输出de
			yyjson_mut_val* jRecord;
			if (deSel.bAggr) { //聚合查询重新生成yyjson对象
				jRecord = yyjson_mut_obj(mut_doc);
			}
			else { //非聚合查询直接拷贝，提高速度
				jRecord = deyy.de;
			}
			

			//填入time字段
			string_view szTime = yyjson_mut_get_str(deyy.time);
			yyjson_mut_val* timeKey = yyjson_mut_str(mut_doc, "time");
			yyjson_mut_val* timeVal;
			if (deSel.timeSel.timeFmt == "") {
				timeVal = yyjson_mut_str(mut_doc, szTime.data());
			}
			else {
				deyy.fmtTime = timeopt::toFmt(szTime.data(), deSel.timeSel.timeFmt);
				timeVal = yyjson_mut_str(mut_doc, deyy.fmtTime.c_str());
			}
			yyjson_mut_obj_put(jRecord, timeKey, timeVal);


			//填入time以外的字段
			if (deyy.items.size()>0) {
				for (auto& i : deyy.items) {
					yyjson_mut_val* valKey = yyjson_mut_str(mut_doc, i.first.c_str());
					yyjson_mut_obj_put(jRecord, valKey, i.second);
				}
			}

			//填入val
			if (deyy.val != nullptr) {
				yyjson_mut_val* valKey = yyjson_mut_str(mut_doc, "val");
				yyjson_mut_obj_put(jRecord, valKey, deyy.val);
			}

			//填入tag字段
			if (withTag)
			{
				//当进行多位号搜索时，需要加入tag标签
				//relTag指向的变量在write_doc之前不能被销毁
				yyjson_mut_val* tagKey = yyjson_mut_str(mut_doc, "tag");
				yyjson_mut_val* tagVal = yyjson_mut_str(mut_doc, tagAlias.c_str());
				yyjson_mut_obj_put(jRecord, tagKey, tagVal);
			}

		
			map<string, map<string, yyjson_mut_val*>>::iterator iter = timeSectionSeries.find(szTime.data());
			if (iter == timeSectionSeries.end()) {
				map<string, yyjson_mut_val*> timeSection;
				timeSection[tag] = jRecord;
				timeSectionSeries[szTime.data()] = timeSection;
			}
			else {
				map<string, yyjson_mut_val*>& timeSection = iter->second;
				timeSection[tag] = jRecord;
			}

			result.rowCount++;

			if (deSel.timeSel.AmountMatch(result.rowCount))
				break;
		}
	}


	//时间截面位号补齐。 并设置补全de的时间
	if (deSel.timeFill) {
		int addDeCount = 0;
		map<string, yyjson_mut_val*>* lastSection = nullptr;
		for (auto& iter : timeSectionSeries) {
			map<string, yyjson_mut_val*>& timeSection = iter.second;
			for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
			{
				TAG_DB_DATA& fSet = *tagDBFileSet[tagIdx];
				string& tag = fSet.tag;

				map<string, yyjson_mut_val*>::iterator j = timeSection.find(tag);
				if (j == timeSection.end()) { //该时间截面没有该位号的数据，需要进行补齐
					if (lastSection != nullptr) {
						map<string, yyjson_mut_val*>::iterator k = lastSection->find(tag);
						if (k != lastSection->end()) {
							//创建一个输出de
							yyjson_mut_val* jRecord = yyjson_mut_obj(mut_doc);


							//从当前截面的de拷贝时间。一定有1个数据，使用第一个
							yyjson_mut_val* jTimeRefRec = timeSection.begin()->second;
							yyjson_mut_val* yyTimeSrc = yyjson_mut_obj_get(jTimeRefRec, "time");
							yyjson_mut_val* yyTime = yyjson_mut_val_mut_copy(mut_doc, yyTimeSrc);
							yyjson_mut_val* timeKey = yyjson_mut_str(mut_doc, CONST_STR::time.c_str());
							yyjson_mut_obj_put(jRecord, timeKey, yyTime);


							yyjson_mut_val* jValRefRec = k->second;
							yyjson_mut_val* yyValSrc = yyjson_mut_obj_get(jValRefRec, m_dbFmt.deItemKey_value.c_str());
							yyjson_mut_val* yyVal = yyjson_mut_val_mut_copy(mut_doc, yyValSrc);  //此处一定要copy一次，不可以把yyValSrc直接put到obj里面去，否则序列化的时候数据会错乱，可能指针指向的对象是链表的一个节点，如果同时在两个obj中，yyjson使用链表输出就会错乱
							yyjson_mut_val* valKey = yyjson_mut_str(mut_doc, CONST_STR::val.c_str());
							yyjson_mut_obj_put(jRecord, valKey, yyVal);

							yyjson_mut_val* yyTagSrc = yyjson_mut_obj_get(jValRefRec, "tag");
							yyjson_mut_val* yyTag = yyjson_mut_val_mut_copy(mut_doc, yyTagSrc);
							yyjson_mut_val* tagKey = yyjson_mut_str(mut_doc, CONST_STR::tag.c_str());
							yyjson_mut_obj_put(jRecord, tagKey, yyTag);


							timeSection[tag] = jRecord;
							addDeCount++;
						}
					}
				}
			}
			lastSection = &timeSection;
		}
		
	}
	

	//排序输出de并输出
	for (auto& i : timeSectionSeries) {
		map<string, yyjson_mut_val*>& timeSection = i.second;
		for (auto& j : timeSection) {
			yyjson_mut_val* jRec = j.second;

			string sortFlag = "";
			if (deSel.sortKey.length() > 0) {
				yyjson_mut_val* yyVal = yyjson_mut_obj_get(jRec, m_dbFmt.deItemKey_value.c_str());
				if (yyjson_mut_is_obj(yyVal)) {
					yyjson_mut_val* yySortKey = yyjson_mut_obj_get(yyVal, deSel.sortKey.c_str());
					if (yyjson_mut_is_str(yySortKey)) {
						sortFlag = yyjson_mut_get_str(yySortKey);
					}
					else if (yyjson_mut_is_num(yySortKey)) {
						double f = yyjson_mut_get_real(yySortKey);
						sortFlag = str::fromFloat(f);
					}
				}
			}

			mapRlt[sortFlag + i.first + j.first + std::to_string(result.rowCount)] = jRec; //不同位号的数据按照时间顺序排序.允许 同一个位号多个数据源时间点相同
		}
	}
	
	return true;
}

bool database::Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc)
{
	map<string, yyjson_mut_val*>& mapRlt = result.mapRlt;
	//返回的数据元是否需要携带tag字段
	bool withTag = tagDBFileSet.size() > 1 ? true : false;
	if (deSel.tagSel.getTag)
		withTag = true;


	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_DATA& fSet = *tagDBFileSet[tagIdx];
		string& tagAlias = fSet.colKey;
		string& tag = fSet.tag;

		for (int j = 0; j < fSet.m_afterAggr.size(); j++) {
			DE_yyjson& deyy = *fSet.m_afterAggr[j];

			string_view szTime = yyjson_mut_get_str(deyy.time);

			yyjson_mut_val* jRecord = yyjson_mut_obj(mut_doc);;

			//time字段
			yyjson_mut_val* timeKey = yyjson_mut_str(mut_doc, "time");
			yyjson_mut_val* timeVal;
			if (deSel.timeSel.timeFmt == "") {
				timeVal = yyjson_mut_str(mut_doc, szTime.data());
			}
			else {
				deyy.fmtTime = timeopt::toFmt(szTime.data(), deSel.timeSel.timeFmt);
				timeVal = yyjson_mut_str(mut_doc, deyy.fmtTime.c_str());
			}
	

			yyjson_mut_obj_put(jRecord, timeKey, timeVal);

			yyjson_mut_val* valKey = yyjson_mut_str(mut_doc, m_dbFmt.deItemKey_value.c_str());
			yyjson_mut_obj_put(jRecord, valKey, deyy.val);


			if (withTag)
			{
				//当进行多位号搜索时，需要加入tag标签
				//relTag指向的变量在write_doc之前不能被销毁
				yyjson_mut_val* tagKey = yyjson_mut_str(mut_doc, "tag");
				yyjson_mut_val* tagVal = yyjson_mut_str(mut_doc, tagAlias.c_str());
				yyjson_mut_obj_put(jRecord, tagKey, tagVal);
			}

			string sortFlag = "";
			if (deSel.sortKey.length() > 0) {
				yyjson_mut_val* yyVal = yyjson_mut_obj_get(jRecord, m_dbFmt.deItemKey_value.c_str());
				if (yyjson_mut_is_obj(yyVal)) {
					yyjson_mut_val* yySortKey = yyjson_mut_obj_get(yyVal, deSel.sortKey.c_str());
					if (yyjson_mut_is_str(yySortKey)) {
						sortFlag = yyjson_mut_get_str(yySortKey);
					}
					else if (yyjson_mut_is_num(yySortKey)) {
						double f = yyjson_mut_get_num(yySortKey);
						sortFlag = str::fromFloat(f);
					}
				}

			}

			mapRlt[sortFlag + szTime.data() + tag + std::to_string(result.rowCount)] = jRecord; //不同位号的数据按照时间顺序排序.允许 同一个位号多个数据源时间点相同
			result.rowCount++;

			if (deSel.timeSel.AmountMatch(result.rowCount))
				return true;
		}
	}

	return true;
}

bool database::doAggregateSingleTag(DE_SELECTOR& deSel, std::map<string,string> aggrOpt,vector<yyjson_val*>& src, DE_yyjson& des, yyjson_mut_doc* mut_doc)
{
	for (auto& i : aggrOpt) {
		string aggrType = i.second;
		string aggrKey = i.first;
		yyjson_mut_val* pAggrVal = nullptr; //聚合后的值

		if (aggrType == "first") {
			yyjson_val* pDeSrc = src.at(0);
			yyjson_val* pDeSrcTime = yyjson_obj_get(pDeSrc, "time");
			yyjson_val* pDeSrcVal = yyjson_obj_get(pDeSrc, aggrKey.c_str());

			if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR) //指定了输出类型
			{
				string_view valStr = yyjson_get_str(pDeSrcVal);
				pAggrVal = yyjson_mut_real(mut_doc, atof(valStr.data()));
			}
			else {
				pAggrVal = yyjson_val_mut_copy(mut_doc, pDeSrcVal);
			}
			des.items[aggrKey] = pAggrVal;
			des.time = yyjson_val_mut_copy(mut_doc, pDeSrcTime);
		}
		else if (aggrType == "last") {
			yyjson_val* pDeSrc = src.at(src.size() - 1);
			yyjson_val* pDeSrcTime = yyjson_obj_get(pDeSrc, "time");
			yyjson_val* pDeSrcVal = yyjson_obj_get(pDeSrc, aggrKey.c_str());

			if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR) //指定了输出类型
			{
				string_view valStr = yyjson_get_str(pDeSrcVal);
				pAggrVal = yyjson_mut_real(mut_doc, atof(valStr.data()));
			}
			else {
				pAggrVal = yyjson_val_mut_copy(mut_doc, pDeSrcVal);
			}
			des.items[aggrKey] = pAggrVal;
			des.time = yyjson_val_mut_copy(mut_doc, pDeSrcTime);
		}
		else if (aggrType == "avg") {
			double dbTotal = 0;
			long long count = 0;
			for (int j = 0; j < src.size(); j++) {
				yyjson_val* pDeSrc = src.at(j);
				yyjson_val* pDeSrcVal = yyjson_obj_get(pDeSrc, aggrKey.c_str());
				yyjson_mut_val* pAggrVal = nullptr;
				double db = 0;
				if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR) //指定了输出类型
				{
					string_view valStr = yyjson_get_str(pDeSrcVal);
					db = atof(valStr.data());
				}
				else {
					db = yyjson_get_num(pDeSrcVal);
				}
				dbTotal += db;
				count++;
			}
			double avg = dbTotal / count;

			yyjson_val* pDeSrc = src.at(0);
			yyjson_val* pDeSrcTime = yyjson_obj_get(pDeSrc, "time");
			pAggrVal = yyjson_mut_real(mut_doc, avg);
			des.items[aggrKey] = pAggrVal;
		}
		else if (aggrType == "max") {
			double dbMax = -DBL_MAX;
			yyjson_val* pSelRowDeSrc = nullptr;
			for (int j = 0; j < src.size(); j++) {
				yyjson_val* pDeSrc = src.at(j);
				yyjson_val* pDeSrcVal = yyjson_obj_get(pDeSrc, aggrKey.c_str());
				double db = 0;
				if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR) //指定了输出类型
				{
					string_view valStr = yyjson_get_str(pDeSrcVal);
					db = atof(valStr.data());
				}
				else {
					db = yyjson_get_num(pDeSrcVal);
				}
				if (db > dbMax) {
					pSelRowDeSrc = pDeSrc;
					dbMax = db;
				}
			}
			yyjson_val* pDeSrcTime = yyjson_obj_get(pSelRowDeSrc, "time");
			des.time = yyjson_val_mut_copy(mut_doc, pDeSrcTime);
			pAggrVal = yyjson_mut_real(mut_doc, dbMax);
			des.items[aggrKey] = pAggrVal;
		}
		else if (aggrType == "min") {
			double dbMin = DBL_MAX;
			yyjson_val* pSelRowDeSrc = nullptr;
			for (int j = 0; j < src.size(); j++) {
				yyjson_val* pDeSrc = src.at(j);
				yyjson_val* pDeSrcVal = yyjson_obj_get(pDeSrc, aggrKey.c_str());
				double db = 0;
				if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR) //指定了输出类型
				{
					string_view valStr = yyjson_get_str(pDeSrcVal);
					db = atof(valStr.data());
				}
				else {
					db = yyjson_get_num(pDeSrcVal);
				}
				if (db < dbMin) {
					pSelRowDeSrc = pDeSrc;
					dbMin = db;
				}
			}
			yyjson_val* pDeSrcTime = yyjson_obj_get(pSelRowDeSrc, "time");
			des.time = yyjson_val_mut_copy(mut_doc, pDeSrcTime);
			pAggrVal = yyjson_mut_real(mut_doc, dbMin);
			des.items[aggrKey] = pAggrVal;
		}
		else if (aggrType == "sum") {
			double dbSum = 0;
			for (int j = 0; j < src.size(); j++) {
				yyjson_val* pDeSrc = src.at(j);
				yyjson_val* pDeSrcVal = yyjson_obj_get(pDeSrc, aggrKey.c_str());
				double db = 0;
				if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR) //指定了输出类型
				{
					string_view valStr = yyjson_get_str(pDeSrcVal);
					db = atof(valStr.data());
				}
				else {
					db = yyjson_get_num(pDeSrcVal);
				}
				dbSum += db;
			}
			pAggrVal = yyjson_mut_real(mut_doc, dbSum);
			des.items[aggrKey] = pAggrVal;
		}
		else if (aggrType == "diff") {
			double dbMax = -DBL_MAX;
			double dbMin = DBL_MAX;
			for (int j = 0; j < src.size(); j++) {
				yyjson_val* pDeSrc = src.at(j);
				yyjson_val* pDeSrcVal = yyjson_obj_get(pDeSrc, aggrKey.c_str());
				double db = 0;
				if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR) //指定了输出类型
				{
					string_view valStr = yyjson_get_str(pDeSrcVal);
					db = atof(valStr.data());
				}
				else {
					db = yyjson_get_num(pDeSrcVal);
				}
				if (db > dbMax)
					dbMax = db;
				if (db < dbMin)
					dbMin = db;
			}
			double dbDiff = dbMax - dbMin; //double的减法会造成精度丢失，通过格式化字符串转换一次解决精度丢失问题
			string sDbDiff = str::format("%lf", dbDiff); 
			dbDiff = atof(sDbDiff.c_str());
			pAggrVal = yyjson_mut_real(mut_doc, dbDiff);
			des.items[aggrKey] = pAggrVal;
		}

		if (aggrKey == "val") {
			des.val = pAggrVal;
		}
	}

	return true;
}

bool database::Select_Step_outputRows_MultiCol(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc)
{
	map<string, yyjson_mut_val*>& mapRlt = result.mapRlt;

	
	//组合成数据行
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_DATA& fSet = *tagDBFileSet[tagIdx];

		//某个位号的所有聚合结果，比如共30天按天聚合，那就是30天的结果
		for (int j = 0; j < fSet.m_afterAggr.size(); j++) {
			DE_yyjson& deyy = *fSet.m_afterAggr[j];

			string_view szTime = yyjson_mut_get_str(deyy.time);

			//检查该时间的行对象是否已经存在
			yyjson_mut_val* jRecord;
			map<string, yyjson_mut_val*>::iterator itRecord = mapRlt.find(szTime.data());
			if (itRecord != mapRlt.end()) { //已存在获得该行
				jRecord = itRecord->second;
			}
			else { //不存在则新创建一行
				jRecord = yyjson_mut_obj(mut_doc);
				//time字段
				yyjson_mut_val* timeKey = yyjson_mut_str(mut_doc, "time");
				yyjson_mut_val* timeVal = yyjson_mut_str(mut_doc, szTime.data());
				yyjson_mut_obj_put(jRecord, timeKey, timeVal);
				//位号字段
				for (int i = 0; i < tagDBFileSet.size(); i++) {
					yyjson_mut_val* tagColKeyInit = yyjson_mut_str(mut_doc, tagDBFileSet[i]->colKey.c_str());
					yyjson_mut_val* tagColValInit = yyjson_mut_null(mut_doc);
					bool putted = yyjson_mut_obj_put(jRecord, tagColKeyInit, tagColValInit);
					if (!putted)
					{
						LOG("无法插入字段");
					}
				}

				std::pair<string, yyjson_mut_val*> recPair;
				recPair.first = szTime;
				recPair.second = jRecord;
				auto insertRet = mapRlt.insert(recPair);
				itRecord = insertRet.first;
				mapRlt[szTime.data()] = jRecord; //不同位号的数据按照时间顺序排序.允许 同一个位号多个数据源时间点相同
			}

			//给当前行添加 当前位号数据作为一列
			yyjson_mut_val* tagColKey = yyjson_mut_str(mut_doc, fSet.colKey.c_str());
			yyjson_mut_obj_put(jRecord, tagColKey, deyy.val);

			
			result.rowCount++;
		}
	}


	//数据补齐
	yyjson_mut_val* lastRec = nullptr;
	yyjson_mut_val * curRec = nullptr;
	for (auto& rec : mapRlt) {
		curRec = rec.second;
		if (curRec && lastRec) {
			size_t idx, max;
			yyjson_mut_val* key, * val;
			yyjson_mut_obj_foreach(curRec, idx, max, key, val) {
				if (yyjson_mut_is_null(val)) {
					string_view szKey = yyjson_mut_get_str(key);
					yyjson_mut_val* lastVal = yyjson_mut_obj_get(lastRec, szKey.data());
					yyjson_mut_val* curVal = yyjson_mut_val_mut_copy(mut_doc, lastVal);
					yyjson_mut_obj_put(curRec, key, curVal);
				}
			}
		}
		lastRec = curRec;
	}


	return true;
}

bool DB_FILE::loadFile()
{
	time = timeopt::Unix2SysTime(ttTime);
	ymd = timeopt::TimeToYMD(time);
	path = db.getPath_dbFile(tag, time,deType);
	fs::readFile(path, data);
	if (data == "") {
		return false;
	}

	//从数据库的原始json数据。
	doc = yyjson_read(data.c_str(), data.length(), 0);
	root = yyjson_doc_get_root(doc);
	return true;
}

bool database::Select_yyjson_deFile(string& s)
{
	return true;
}

bool database::Select_yyjson(DE_SELECTOR& deSel, SELECT_RLT& result)
{
	bool bRet = true;
	//获取需要加载数据的位号集合
	vector<string> tagSet = deSel.tagSel.tagSet;


	//初始化单个位号的 数据内存对象和查询参数
	vector<TAG_DB_DATA*> tagDBFileSet;  //数据库原始文件数据
	for (int i = 0; i < tagSet.size(); i++)
	{
		//生成相对位号
		TAG_DB_DATA& fSet  = *(new TAG_DB_DATA());
		fSet.tag = tagSet[i];
		fSet.relTag = TAG::trimRoot(tagSet[i], deSel.tagSel.m_rootTag);

		//生成位号名称
		if (deSel.vecTagLable.size() == tagSet.size()) {
			fSet.colKey = deSel.vecTagLable[i];
		}
		else {
			if (deSel.tagLabel == "tag") {
				fSet.colKey = fSet.relTag;
			}
			else
			{
				size_t pos = fSet.relTag.rfind(".");
				if (pos != string::npos) {
					fSet.mpName = fSet.relTag.substr(pos + 1, fSet.relTag.size() - pos - 1);
				}
				else {
					fSet.mpName = fSet.relTag;
				}
				fSet.colKey = fSet.mpName;
			}
		}

		//本位号查询参数
		if (deSel.vecAggregate.size() == tagSet.size()) { //多位号聚合模式
			fSet.aggregate = deSel.vecAggregate[i];
			fSet.bAggr = true;
		}
		else if (deSel.aggregate.size() > 0) {//单位号聚合
			fSet.aggregate = deSel.aggregate;
			fSet.bAggr = true;
		}

		tagDBFileSet.push_back(&fSet);
	}
	
	//加载文件原始数据
	Select_Step_loadFile(deSel, tagDBFileSet,result);

	
	map<string, yyjson_mut_val*>& mapRlt = result.mapRlt; //key是排序标记，一般由sortFlag和时间等组合而成
	yyjson_mut_doc* rlt_mut_doc = yyjson_mut_doc_new(NULL);

	if (deSel.deType == "curve") {
		for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
		{
			TAG_DB_DATA& fSet = *tagDBFileSet[tagIdx];
			string& tag = fSet.tag; // yyjson 在创建字符串对象的时候，不复制字符串，源字符串内存不能释放.因此使用string&.
			string& relTag = fSet.relTag;


			//加载每个数据文件中的数据
			for (int i = 0; i < fSet.fileList.size(); i++)
			{
				//加载数据元列表
				DB_FILE* pdf = fSet.fileList[i];
				string s = str::format("%d%d", tagIdx, i);
				yyjson_mut_val* p = yyjson_val_mut_copy(rlt_mut_doc, pdf->root);
				mapRlt[s] = p;
			}
		}
	}
	else {
		//获得选中的数据元.并进行分组
		bRet = Select_Step_loadDataElem(deSel, tagDBFileSet, result, rlt_mut_doc);
		if (!bRet)
			return false;

		//执行聚合
		Select_Step_doAggregate(deSel, tagDBFileSet, result, rlt_mut_doc);

		//输出结果行
		if (deSel.tagAsColume) {
			Select_Step_outputRows_MultiCol(deSel, tagDBFileSet, result, rlt_mut_doc);
		}
		else {
			//if (deSel.timeFill) {
			Select_Step_outputRows_SingleCol_timeFill(deSel, tagDBFileSet, result, rlt_mut_doc);
			//}
			//else
			//	Select_Step_outputRows_SingleCol(deSel,tagDBFileSet, result, rlt_mut_doc);
		}

		//结果行二次计算
		if (deSel.calc == "diff") {
			int idx = 0;
			yyjson_mut_val* lastVal;
			yyjson_mut_val* curVal;
			double dbLast;
			double dbCur;
			for (auto& i : mapRlt)
			{
				curVal = yyjson_mut_obj_get(i.second, m_dbFmt.deItemKey_value.c_str());
				if (!yyjson_mut_is_num(curVal)) {
					break;
				}


				dbCur = yyjson_mut_get_real(curVal);

				if (idx > 0) {
					double diff = dbCur - dbLast;
					yyjson_mut_set_real(curVal, diff);
				}
				idx++;
				dbLast = dbCur;
				lastVal = curVal;
			}
			mapRlt.erase(mapRlt.begin());
		}
	}
	

	//使用新的yyjson doc对象输出结果. 将多个位号，多个时间段的原始数据合并成1个json查询结果对象
	yyjson_mut_val* rlt_mut_root = yyjson_mut_arr(rlt_mut_doc); //创建一个数组
	yyjson_mut_doc_set_root(rlt_mut_doc, rlt_mut_root);

	for (auto& i : mapRlt)
	{
		if(deSel.ascendingSort)
			yyjson_mut_arr_append(rlt_mut_root, i.second);
		else
			yyjson_mut_arr_prepend(rlt_mut_root, i.second);
	}
	
	size_t len = 0;
	if (result.getDE){
		//如果此处p返回null，应该是rlt_mut_doc当中 指向的string类型可能是临时变量，已经被释放了
		char* p = yyjson_mut_write(rlt_mut_doc, 0, &len);
		size_t len = strlen(p);
		result.dataList = p;
	}
	result.rowCount = mapRlt.size();

	//释放结果
	yyjson_mut_doc_free(rlt_mut_doc);
	//释放源
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_DATA* fSet = tagDBFileSet[tagIdx];
		delete fSet;
	}


	return true;
}

bool database::Select_Step_loadFile(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result)
{
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_DATA& fSet = *tagDBFileSet[tagIdx];


		//无周期范围选择，且数据集为单个，跳过其他文件读取，提高性能
		if (deSel.timeSel.timeSetType == TSM_First && deSel.timeSel.periodType == PT_None) {
			time_t loadTime = deSel.timeSel.startTime;
			for (; loadTime <= deSel.timeSel.endTime; loadTime += 24 * 60 * 60)
			{
				DB_FILE* pdf = new DB_FILE(loadTime, fSet.tag);
				pdf->deType = deSel.deType;
				if (!pdf->loadFile()) {
					delete pdf;
					continue;
				}
				fSet.fileList.push_back(pdf);
				break;
			}
		}
		else if (deSel.timeSel.timeSetType == TSM_Last && deSel.timeSel.periodType == PT_None) {
			time_t loadTime = deSel.timeSel.endTime;
			for (; loadTime >= deSel.timeSel.startTime; loadTime -= 24 * 60 * 60)
			{
				DB_FILE* pdf = new DB_FILE(loadTime, fSet.tag);
				pdf->deType = deSel.deType;
				if (!pdf->loadFile()) {
					delete pdf;
					continue;
				}
				fSet.fileList.push_back(pdf);
				break;
			}
		}
		else { //选择单个数据元 TSM_ALL && PT_NONE
			time_t loadTime = deSel.timeSel.endTime;
			for (; loadTime >= deSel.timeSel.startTime; loadTime -= 24 * 60 * 60)
			{
				DB_FILE* pdf = new DB_FILE(loadTime, fSet.tag); 
				pdf->deType = deSel.deType;
				if (!pdf->loadFile()) {
					delete pdf;
					continue;
				}
				fSet.fileList.insert(fSet.fileList.begin(), pdf);
			}
		}




		if (fSet.fileList.size() == 0)
			continue;
		//头尾两个数据文件需要进行时间范围检查，中间的不需要
		fSet.fileList[0]->boundaryFile = true;
		fSet.fileList[fSet.fileList.size() - 1]->boundaryFile = true;

		//数据只有1天的，不进行下采样
		if (fSet.fileList.size() <= 1)
			deSel.interval.type = DOWN_SAMPLING_TYPE::DST_None;

		result.fileCount += fSet.fileList.size();
	}
	return true;
}

bool database::Select_Step_loadDataElem(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* rlt_mut_doc)
{
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_DATA& fSet = *tagDBFileSet[tagIdx];
		string& tag = fSet.tag; // yyjson 在创建字符串对象的时候，不复制字符串，源字符串内存不能释放.因此使用string&.
		string& relTag = fSet.relTag;


		//加载每个数据文件中的数据
		for (int i = 0; i < fSet.fileList.size(); i++)
		{
			//加载数据元列表
			DB_FILE* pdf = fSet.fileList[i];

			size_t idx, max;
			yyjson_val* de;
			int lastDeTime = 0;
			int currDeTime = 0;
			string deTime = pdf->ymd + " 00:00:00";
			string groupKeyVal;

			yyjson_val* deList = nullptr;
			yyjson_type type = yyjson_get_type(pdf->root);
			if (type == YYJSON_TYPE_OBJ) { //带描述信息的文件
				deList = yyjson_obj_get(pdf->root, "data");
			}
			else if (type == YYJSON_TYPE_ARR) {
				deList = pdf->root;
			}
			else {
				continue;
			}


			yyjson_arr_foreach(deList, idx, max, de) {
				//兼容将数字存储成字符串的问题，当有聚合请求时，自动转换字符串为数字类型，返回提示信息
				if (idx == 0 && deSel.bAggr)
				{
					yyjson_val* yyVal = yyjson_obj_get(de, m_dbFmt.deItemKey_value.c_str());
					if (yyVal && yyjson_is_str(yyVal) && deSel.valType == "") 
					{
						for (auto& aggrParam : fSet.aggregate) {
							string aggrType = aggrParam.second;
							if (aggrType == "diff" || aggrType == "avg" || aggrType == "sum" || aggrType == "max" || aggrType == "min") {
								//string err = "data element type is: string, does not support aggregate type:" + aggrType;
								//err += ",use valType=number to cast string value to number value";
								//json jErr = err;
								//result.error = jErr.dump();
								//return false;
								deSel.valType = "number";
								result.info = "aggregate type is " + aggrType + ",auto cast value type string to number";
								break;
							}
						}
					}
				}


				//下采样机制。每downsampling interval 输出1个数据点;例如dsi=3,则输出第0个，第3个，第6个。。。
				//最后1个下采样间隔全部输出
				if (deSel.interval.type == DOWN_SAMPLING_TYPE::DST_Count)
				{
					if (idx % deSel.interval.dsi > 0 && idx < max - deSel.interval.dsi) continue;
				}

				//先生成完整时间戳，再进行match判断
				yyjson_val* yyTime = yyjson_obj_get(de, "time");
				string_view szTime = yyjson_get_str(yyTime);
				const char* pHms = nullptr;
				if (szTime.length() == 19)
				{
					pHms = szTime.data() + 11;//取出时分秒
				}
				else
				{
					pHms = szTime.data();
				}
				memcpy(deTime.data() + 11, pHms, 8);//取出时分秒

				if (pdf->boundaryFile && !deSel.timeSel.Match(deTime))
					continue;

				if (deSel.interval.type == DOWN_SAMPLING_TYPE::DST_Time) {
					HMS_STR* p = (HMS_STR*)pHms;
					currDeTime = p->getTotalSec();
					if (currDeTime - lastDeTime < deSel.interval.dsti)
						continue;
					lastDeTime = currDeTime;
				}

				//条件过滤器，javascript脚本过滤
				if (deSel.condition.bEnable && !deSel.condition.match(de))
				{
					continue;
				}

				if (fSet.bAggr) {
					if (deSel.groupby == "day") {
						groupKeyVal = deTime.substr(0, 10);
						map<string, vector<yyjson_val*>>::iterator it = fSet.m_groupedBeforeAggr.find(groupKeyVal);
						if (it != fSet.m_groupedBeforeAggr.end()) {
							it->second.push_back(de);
						}
						else {
							vector<yyjson_val*> newVec;
							newVec.push_back(de);
							fSet.m_groupedBeforeAggr[groupKeyVal] = newVec;
						}
					}
					else {
						fSet.m_beforeAggr.push_back(de);
					}
				}
				else { 
					DE_yyjson& deyy  = *(new DE_yyjson());
					deyy.deTime = deTime;

					//放入time
					deyy.time = yyjson_mut_str(rlt_mut_doc, deyy.deTime.data());

					//放入val
					yyjson_val* yyVal = yyjson_obj_get(de, m_dbFmt.deItemKey_value.c_str());
					if (yyVal)
						deyy.val = yyjson_val_mut_copy(rlt_mut_doc, yyVal);
					
					//放入time,val以外的所有字段
					deyy.de = yyjson_val_mut_copy(rlt_mut_doc, de);


					if (yyjson_mut_get_type(deyy.val) == YYJSON_TYPE_STR) {
						if (deSel.isValTypeNumber()) //指定了输出类型
						{
							string_view valStr = yyjson_mut_get_str(deyy.val);
							deyy.val = yyjson_mut_real(rlt_mut_doc, atof(valStr.data()));
						}
					}

					fSet.m_afterAggr.push_back(&deyy);
				}

				result.deCount++;
			}
		}
	}
	return true;
}

bool database::Select_Step_doAggregate(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* rlt_mut_doc)
{
	if (deSel.bAggr) {
		if (deSel.grouped) {
			for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
			{
				TAG_DB_DATA& fSet = *tagDBFileSet[tagIdx];
				//每个group生成一个聚合后 de
				for (auto& i : fSet.m_groupedBeforeAggr) {
					DE_yyjson& de = *(new DE_yyjson()); //聚合结果
					doAggregateSingleTag(deSel, fSet.aggregate, i.second, de, rlt_mut_doc);
					if (deSel.groupByTime)
						de.time = yyjson_mut_str(rlt_mut_doc, i.first.data());

					fSet.m_afterAggr.push_back(&de);
				}
			}
		}
		else {//所有数据聚合的 时间字段填时间范围 
			for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
			{
				TAG_DB_DATA& fSet = *tagDBFileSet[tagIdx];
				if (fSet.m_beforeAggr.size() > 0) {
					DE_yyjson& de = *(new DE_yyjson()); //聚合结果
					doAggregateSingleTag(deSel, fSet.aggregate, fSet.m_beforeAggr, de, rlt_mut_doc);

					if (tagDBFileSet.size() > 1 || de.time == nullptr) {
						de.time = yyjson_mut_str(rlt_mut_doc, deSel.timeSel.selector.c_str());
					}

					fSet.m_afterAggr.push_back(&de);
				}
			}
		}
	}
	return true;
}


/*
//2021.10.21 此时simdjson还不支持使用数组下标访问数组元素
bool database::Select_simdjson(string tag, TIME_SELECTOR& timeSelector, string filter, DB_DATA_SET& result)
{
	TIME_SELECTOR& tf = timeSelector;
	time_t loadTime = tf.endTime;
	string strDataFmt = "";
	string strRawDataFmt = "";
	SYSTEMTIME stTemp;

	CONDITION_SELECTOR af;
	af.init(filter);

	double max = -1000000000;
	double min = 1000000000;
	double avg = 0;
	int count = 0;

	for (; loadTime >= tf.startTime; loadTime -= 24 * 60 * 60)
	{
		//加载数据元列表
		stTemp = timeopt::Unix2SysTime(loadTime);
		string dbFile = getPath_dbFile(tag, stTemp);
		string dbData;
		fs::readFile(dbFile, dbData);
		if (dbData == "")
			continue;

		std::unique_ptr<char[]> padded_json_copy{ new char[dbData.length() + SIMDJSON_PADDING] };
		memcpy(padded_json_copy.get(), dbData.c_str(), dbData.length());
		memset(padded_json_copy.get() + dbData.length(), 0, SIMDJSON_PADDING);
		simdjson::dom::parser parser;
		simdjson::dom::element dataList = parser.parse(padded_json_copy.get(), dbData.length(), false);

		if (dataList.is_null())
			continue;

		for (dom::object de : dataList)
		{
			string_view szTime = de["time"];

			//先生成完整时间戳，再进行match判断
			if (szTime.length() == 19)
			{
				szTime = szTime.substr(11, 8); //取出时分秒
			}
			string strTime = timeopt::TimeToYMD(stTemp) + " " + string(szTime);
			if (!tf.Match(strTime))
				continue;

			stringstream ssDe;
			ssDe << de;
			string sDe = ssDe.str();
			sDe = sDe.substr(0, sDe.length() - 1);// remove the last char "}" ,and append additional attributes
			sDe += ",\"tag\":\"" + tag + "\"";

			bool bHavePic = false;
			simdjson::error_code error;
			error = de["pic"].get(bHavePic);
			if (bHavePic)
			{
				sDe += ",\"pic_url\":\"/db" + getPath_deFile(tag, timeopt::str2st(strTime)) + ".jpg\"";
			}

			bool bHaveVideo = false;
			error = de["video"].get(bHaveVideo);
			if (bHaveVideo)
			{
				sDe += ",\"video_url\":\"/db" + getPath_deFile(tag, timeopt::str2st(strTime)) + ".mp4\"";
			}

			sDe += "}";

			if (af.bEnable && !af.match(sDe))
			{
				continue;
			}

			result[strTime + "+" + tag] = sDe;
			count++;
			if (tf.AmountMatch(count))
				return true;
		}

	}
	return true;
}
*/

bool database::updateJsonObj(json& jOld, json& jNew)
{
	//已经存在的key，用新value更新
	//不存在key，增加
	for (auto& [key, value] : jNew.items()) {
		json& jOldVal = jOld[key];
		json& jNewVal = jNew[key];

		if (jOldVal.is_object())
		{
			updateJsonObj(jOldVal, jNewVal);
		}
		else
		{
			jOld[key] = jNewVal;
		}
	}

	return true;
}

bool database::Update(string tag, TIME stTime, string& sData)
{
	json jData = json::parse(sData);
	return Update(tag, stTime, jData);
}

bool database::Update(string tag, TIME stTime, json& jData)
{
	//加载数据元列表
	string dbFile = getPath_dbFile(tag, stTime);
	string dbData;
	fs::readFile(dbFile, dbData);
	if (dbData == "")
		return false;

	json jDEList = json::parse(dbData);
	string specifyTime = timeopt::st2str(stTime);
	bool findDE = false;
	for (int i = 0; i < jDEList.size(); i++)
	{
		json& jDE = jDEList[i];
		string sHMS = jDE["time"].get<string>();
		if (sHMS.length() > 8)
		{
			sHMS = sHMS.substr(sHMS.length() - 8, 8);
		}
		string specifyHMS = specifyTime.substr(specifyTime.length() - 8, 8);
		if (sHMS == specifyHMS)
		{
			json& jOld = jDE[m_dbFmt.deItemKey_value.c_str()];
			json& jNew = jData;
			findDE = true;
			updateJsonObj(jOld, jNew);
		}
	}
	if (!findDE)
		return false;

	dbData = jDEList.dump(2);
	fs::writeFile(dbFile, dbData);
	return true;
}

bool database::Delete(string tag, TIME stTime)
{
	//加载数据元列表
	string dbFile = getPath_dbFile(tag, stTime);
	string dbData;
	fs::readFile(dbFile, dbData);
	if (dbData == "")
		return false;

	json jDEList = json::parse(dbData);
	string specifyTime = timeopt::st2str(stTime);
	bool findDE = false;
	for (int i = 0; i < jDEList.size(); i++)
	{
		json& jDE = jDEList[i];
		string sHMS = jDE["time"].get<string>();
		if (sHMS.length() > 8)
		{
			sHMS = sHMS.substr(sHMS.length() - 8, 8);
		}
		string specifyHMS = specifyTime.substr(specifyTime.length() - 8, 8);
		if (sHMS == specifyHMS)
		{
			findDE = true;
			jDEList.erase(jDEList.begin() + i);
			break;
		}
	}
	if (!findDE)
		return false;

	dbData = jDEList.dump(2);
	fs::writeFile(dbFile, dbData);
	return true;
}

bool database::Count(string tag, TIME_SELECTOR& timeSelector, string filter, int& iCount)
{
	return false;
}

void database::saveDEFile(string strTag, TIME stTime, string deFileUrl)
{
	deFileUrl = str::replace(deFileUrl, "\\", "/");
	string suffix = parseSuffix(deFileUrl);
	string path = getPath_deFile(strTag, stTime);

	if (suffix != "") //文件
	{
		path += "." + suffix;
	}
	
	path = m_path + path;
	try
	{
		fs::createFolderOfPath(path);
		//copy(charCodec::utf8_to_utf16(deFileUrl),charCodec::utf8_to_utf16(path));
	}
	catch (std::exception& e)
	{
		string log = "saveDEFile fail src=" + deFileUrl + ",des=" + path + "error=" + e.what();
		LOG(log);
	}
}

bool database::saveDEFile(string tag, TIME stTime, unsigned char* pData, int len, string suffix)
{
	string path = getPath_deFile(tag, stTime);
	path = m_path + path + "." + suffix;
	if (fs::writeFile(path,(char*) pData, len))
	{
		return true;
	}
	else
	{
		return false;
	}
}


bool database::create(string strDBUrl,string name)
{
	strDBUrl = str::replace(strDBUrl, "\\", "/");
	m_path = fs::toAbsolutePath(strDBUrl);
	fs::createFolderOfPath(m_path);
	json dbInfo;
	dbInfo["name"] = name;
	string createTime = timeopt::nowStr();
	dbInfo["create_time"] = createTime;
	string s = dbInfo.dump(2);
	fs::writeFile(m_path + "/" + m_dbFmt.deListName,s);
	return true;
}

bool database::Open(string strDBUrl,string name)
{
	if (strDBUrl == "")
		return false;

	m_path = str::replace(strDBUrl,"\\","/");
	m_dbFmt.deListName = tds->conf->getStr("deListName", "db.json");
	m_dbFmt.curveIdxListName = tds->conf->getStr("curveIdxListName", "curve_list.jdb");
	m_dbFmt.curveDeNameSuffix = tds->conf->getStr("curveDeNameSuffix", ".curve.jdb");
	m_dbFmt.deItemKey_value = tds->conf->getStr("deItemKey_value", "val");

	m_name = name;
	return true;
}

void database::Close()
{

}

//解析聚合操作
//参数1： "aggregate":"max"
//参数2 
// "aggregate":{
//	    "max":"max",
//      "min":"min",
//      "avg":"avg"
// }
map<string, string> database::getAggrOpt(json& jAggr) {
	map<string, string> aggrOpt;
	if (jAggr.is_string()) { //单位号的val字段聚合
		aggrOpt[db.m_dbFmt.deItemKey_value] = jAggr.get<string>();
	}
	else if (jAggr.is_object()) { //单位号的指定字段聚合,可指定多个字段聚合
		for (auto i : jAggr.items()) {
			aggrOpt[i.key()] = i.value();
		}
	}
	return aggrOpt;
}

string database::parseDESelector(json params, DE_SELECTOR& deSel)
{
	//parse time selector
	std::string strTime = "";
	std::string strStartDate, strEndDate;
	TIME stStartDate, stEndDate;
	if (params["time"].is_null()) { return makeRPCError(TEC_paramMissing, "param missing:\"time\""); }
	try { strTime = params["time"].get<string>(); }
	catch (...)
	{
		return makeRPCError(TEC_WrongParamFmt, "wrong param format:\"time\" param should be a string");
	}
	if (!deSel.timeSel.init(strTime))
		return makeRPCError(TEC_TIME_SELECTOR_FMT_ERROR, "time selector format error:" + deSel.timeSel.error);

	if (params["timeFmt"].is_string()) {
		deSel.timeSel.timeFmt = params["timeFmt"];
	}

	//parse tag selector
	std::string strRootTag;
	json jTag;

	if (params.contains("columeTag")) {//colume模式暂时只支持 精确指定位号模式，不支持通配
		jTag = params["columeTag"];
		deSel.tagAsColume = true;
	}
	else if (params.contains("tag"))
	{
		jTag = params["tag"];
	}
	else if (params.contains("colume")) {
		deSel.tagAsColume = true;
		json jColList = params["colume"];
		jTag = json::array();
		for (auto& jCol : jColList) {
			jTag.push_back(jCol["tag"]);
			deSel.vecAggregate.push_back(getAggrOpt(jCol["aggregate"]));
			deSel.bAggr = true;
			deSel.vecTagLable.push_back(jCol["label"].get<string>());
		}
	}
	else
		return makeRPCError(TEC_paramMissing, " tag or colume must be specified");


	

		

	if (params["rootTag"]!= nullptr)
	{
		strRootTag = params["rootTag"];
	}


	if (!deSel.tagSel.init(jTag,strRootTag))
		return makeRPCError(TEC_TAG_SELECTOR_FMT_ERROR, "tag selector format error:" + deSel.tagSel.error);

	if (params["getTag"].is_boolean()) {
		deSel.tagSel.getTag = params["getTag"].get<bool>();
	}


	//监控对象类型
	if (params["type"].is_string())
		deSel.tagSel.type = params["type"].get<string>();


	if (params["deType"].is_string())
		deSel.deType = params["deType"];

	//parse interval selector
	json jDsi = params["interval"];
	if (jDsi.is_number())
	{
		deSel.interval.type = DOWN_SAMPLING_TYPE::DST_Count;
		deSel.interval.dsi = jDsi.get<int>();
	}
	else if (jDsi.is_string())
	{
		string sDsti = params["interval"].get<string>();
		deSel.interval.dsti = timeopt::dhmsSpan2Seconds(sDsti);
		if (deSel.interval.dsti > 0)
			deSel.interval.type = DOWN_SAMPLING_TYPE::DST_Time;
	}

	//parse condition selector
	string filter;
	if (params["match"] != nullptr) {
		filter = params["match"].get<string>();
		deSel.condition.init(filter);
	}

	if (params["a-sort"] != nullptr) {
		deSel.ascendingSort = true;
		deSel.sortKey = params["a-sort"].get<string>();
	}
	else if (params["d-sort"] != nullptr) {
		deSel.ascendingSort = false;
		deSel.sortKey = params["d-sort"].get<string>();
	}


	if (params["tagAsColume"].is_boolean()) {
		deSel.tagAsColume = params["tagAsColume"].get<bool>();
	}

	if (params["valType"].is_string()) {
		deSel.valType = params["valType"];
	}


	//name模式或者 tag模式的 colLabel
	json jColLabel = params["columeLabel"];
	if (jColLabel.is_string()) {
		deSel.tagLabel = params["columeLabel"].get<string>();

		//name 与 tag 是关键字，表示使用什么做label,否则认为这是一个 指定的label，仅在只有一个位号时有效
		if (deSel.tagLabel != "name" && deSel.tagLabel != "tag") {
			deSel.vecTagLable.push_back(deSel.tagLabel);
		}
	}
	//自定义columeLabel
	else if (jColLabel.is_array()) {
		for (auto& i : jColLabel) {
			deSel.vecTagLable.push_back(i.get<string>());
		}
	}


	if (params["groupby"].is_string()) {
		deSel.groupby = params["groupby"].get<string>();
		deSel.grouped = true;

		if (deSel.groupby == "day") {
			deSel.groupByTime = true;
		}
		else if (deSel.groupby == "month") {
			deSel.groupByTime = true;
		}
		else if (deSel.groupby == "hour") {
			deSel.groupByTime = true;
		}
	}


	json jAggr = params["aggregate"];
	if (jAggr == nullptr)
		jAggr = params["aggr"];

	if (jAggr.is_array()) { //多位号聚合
		for (auto& i : jAggr) {
			deSel.vecAggregate.push_back(getAggrOpt(i));
		}
		deSel.bAggr = true;
	}
	else if(jAggr.is_object() || jAggr.is_string()){
		deSel.aggregate = getAggrOpt(jAggr);
		deSel.bAggr = true;
	}
		
	return "";
}

void database::rpc_db_select(json params, RPC_RESP& resp, RPC_SESSION session)
{
	DE_SELECTOR deSel;

	string rootTag = "";
	if (params.contains("rootTag")) {
		rootTag = params["rootTag"];
	}
	rootTag = TAG::addRoot(rootTag, session.org);
	params["rootTag"] = rootTag;
	resp.error = parseDESelector(params, deSel);
	if (resp.error != "") return;

	if (params["calc"].is_string()) {
		deSel.calc = params["calc"];
	}

	if (params["timeFill"].is_boolean()) {
		deSel.timeFill = params["timeFill"].get<bool>();
	}

	//选出位号
	prj.getTagsByTagSelector(deSel.tagSel.tagSet, deSel.tagSel);

	SELECT_RLT result;
	if (deSel.tagSel.tagSet.size() == 0) {
		resp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "specified tag not found");
	}
	else {
		try
		{
			db.Select_yyjson(deSel, result);
			if (result.error != "") {
				resp.error = result.error;
			}
			else {
				resp.result = result.dataList;
			}
		}
		catch (std::exception& e)
		{
			json jerror = e.what();
			resp.error = jerror.dump();
		}
	}

	resp.info = result.info;
	resp.dbQueryInfo = "tags:" + str::fromInt(deSel.tagSel.tagSet.size()) + ",files:" + str::fromInt(result.fileCount) +  ",data elements:" + str::fromInt(result.deCount) + ",rows:" + str::fromInt(result.rowCount);
}

void database::rpc_db_count(json params, RPC_RESP& resp, RPC_SESSION session)
{
	DE_SELECTOR deSel;

	params["root"] = session.org;
	resp.error = parseDESelector(params, deSel);
	if (resp.error != "") return;

	SELECT_RLT result;
	result.getDE = false;
	try
	{
		db.Select_yyjson(deSel, result);
	}
	catch (std::exception& e)
	{
		json jerror = e.what();
		resp.error = jerror.dump();
	}

	json jRlt = result.rowCount;
	resp.result = jRlt.dump();
}

void database::rpc_db_getFile(json params, RPC_RESP& resp, RPC_SESSION session)
{
	DE_SELECTOR deSel;

	string rootTag = "";
	if (params.contains("rootTag")) {
		rootTag = params["rootTag"];
	}
	rootTag = TAG::addRoot(rootTag, session.org);
	params["rootTag"] = rootTag;
	resp.error = parseDESelector(params, deSel);
	if (resp.error != "") return;

	//选出位号
	prj.getTagsByTagSelector(deSel.tagSel.tagSet, deSel.tagSel);

	SELECT_RLT result;
	if (deSel.tagSel.tagSet.size() == 0) {
		resp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "specified tag not found");
	}
	else {
		try
		{
			db.Select_yyjson(deSel, result);
			resp.result = result.dataList;
		}
		catch (std::exception& e)
		{
			json jerror = e.what();
			resp.error = jerror.dump();
		}
	}

}

string database::parseSuffix(string deFileUrl)
{
	string suffix = "";
	size_t posDot = deFileUrl.rfind(".");
	size_t posSlash = deFileUrl.rfind("/");
	if (posDot != string::npos)
	{
		if (posSlash != string::npos)
		{
			if (posSlash < posDot)
			{
				suffix = deFileUrl.substr(posDot + 1, deFileUrl.size() - posDot - 1);
			}
		}
		else
		{
			suffix = deFileUrl.substr(posDot + 1, deFileUrl.size() - posDot - 1);
		}
	}

	return suffix;
}

string database::dataSet2String(DB_DATA_SET& dataSet)
{
	string result = "]";
	bool first = true;
	//desending
	for(auto i:dataSet)
	{
		if(first)
			result = i.second + result;
		else
		{
			result = i.second + "," + result;
		}
		
		first = false;
	}

	result = "[" + result;
	return result;
}


void database::GetFileTreeOfPath(FILE_ITEM* pfi, string strPath)
{
#ifdef _WIN32
	size_t iPos = strPath.rfind('/');
	pfi->strName = strPath.substr(iPos+1,strPath.length() - 1 - iPos);

	strPath += "/";
	char szFind[260];
	char szFile[1000] = { 0 };
	WIN32_FIND_DATA FindFileData;
	strcpy(szFind, strPath.c_str());
	strcat(szFind, "*.*");
	HANDLE hFind = ::FindFirstFile(szFind, &FindFileData);
	if (INVALID_HANDLE_VALUE == hFind)
		return;

	while (true)
	{
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (FindFileData.cFileName[0] != '.')
			{
				string strSubName = FindFileData.cFileName;
				string strSubPath = strPath + "/" + strSubName;

				FILE_ITEM* pSub = new FILE_ITEM;
				pSub->strName = strSubName;
				pfi->childItem.push_back(pSub);
				pSub->parentItem = pfi;
				GetFileTreeOfPath(pSub, strSubPath);
			}
		}
		else
		{
			string strTmp = FindFileData.cFileName;//保存文件名，包括后缀名
			if (strTmp.find(".jdb") != string::npos)
			{
				FILE_ITEM* pSub = new FILE_ITEM;
				pSub->strName = strTmp;
				pfi->childItem.push_back(pSub);
				pSub->parentItem = pfi;
			}
		}
		if (!FindNextFile(hFind, &FindFileData))
			break;
	}
	FindClose(hFind);
#endif
}


TIME_SELECTOR::TIME_SELECTOR()
{
	startTime = 0;
	endTime = 0;
	m_dataNum = 0;
	periodType = PT_None;
}

bool TIME_SELECTOR::Match(string& deTime)
{
	if (deTime >= strStart && deTime <= strEnd)
		return true;
	return false;
}

bool TIME_SELECTOR::AmountMatch(size_t amount)
{
	if (m_dataNum != 0)//次数过滤启用
	{
		if (amount < m_dataNum)
		{
			return false;
		}
		return true;
	}
	else//次数过滤未启用
	{
		return false;
	}
}

//普通年
int monthLastDay[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
//闰年
int monthLastDay_leapYear[12] = { 31,29,31,30,31,30,31,31,30,31,30,31 };

bool isLeapYear(int year) {
	if (year % 4 == 0) {
		if (year % 100 == 0) {
			if (year % 400 == 0) {
				return true;
			}
		}
		else {
			return true;
		}
	}
	return false;
}


int getMonthLastDay(int year, int month) {
	if (isLeapYear(year)) {
		return monthLastDay_leapYear[month];
	}
	else {
		return monthLastDay[month];
	}
};


string time2DbFileDate(string& time) {
	string dbFileDate;
	//2020-02
	if (time.length() == 7) {
		string sYear = time.substr(0, 4);
		string sMonth = time.substr(5, 2);
		int y = atoi(sYear.c_str());
		int m = atoi(sMonth.c_str());
		int d = getMonthLastDay(y, m);
		string sDay = str::format("%2d", d);
		dbFileDate = time + "-" + sDay;
		return dbFileDate;
	}
	//2020-02-03
	else if (time.length() == 10) {
		return time;
	}
	else
	{
		return "";
	}
}

bool TIME_SELECTOR::init(string time)
{
	selector = time;

	//时间宏替换
	if (time.find("this month") != string::npos) {
		string t = timeopt::nowStr(false);
		t = t.substr(0, 7);
		time = str::replace(time, "this month", t);
	}
	else if (time.find("this day") != string::npos) {
		string t = timeopt::nowStr(false);
		t = t.substr(0, 10);
		time = str::replace(time, "this day", t);
	}
	else if (time.find("today") != string::npos) {
		string t = timeopt::nowStr(false);
		t = t.substr(0, 10);
		time = str::replace(time, "today", t);
	}

	//集合选择
	if (time.find("head@") != string::npos) {
		timeSetType = TSM_First;
		time = str::trimPrefix(time, "head@");
	}
	else if (time.find("tail@") != string::npos) {
		timeSetType = TSM_Last;
		time = str::trimPrefix(time, "tail@");
	}
	else {
		timeSetType = TSM_All;
	}

	//周期选择
	if (time.find("day@") != string::npos) {
		periodType = PT_Day;
		time = str::trimPrefix(time, "day@");
	}
	else if (time.find("hour@") != string::npos) {
		periodType = PT_Hour;
		 time = str::trimPrefix(time, "hour@");
	}
	else if (time.find("month@") != string::npos) {
		periodType = PT_Month;
		time = str::trimPrefix(time, "month@");
	}
	else {
		periodType = PT_None;
	}


	if (time.find("e") != string::npos)
	{
		time = time.substr(0, time.length() - 1);
		m_dataNum = atoi(time.c_str());
		string timeRange ="2020-01-01 00:00:00~" + timeopt::nowStr();
		parseTimeRange(timeRange);
	}
	else if (time.find("-") == string::npos) { // 1d2h3m 的时间区段模式
		string timeRange = timeopt::rel2abs(time);
		parseTimeRange(timeRange);
	}
	else {
		string timeRange = shortSel2StardardSel(time);
		parseTimeRange(timeRange);
	}
	return true;
}


string TIME_SELECTOR::shortSel2StardardSel(string time)
{
	//2020-02
	if (time[4] == '-' && time.length() == 7) {
		string sYear = time.substr(0, 4);
		string sMonth = time.substr(5, 2);
		int y = atoi(sYear.c_str());
		int m = atoi(sMonth.c_str());
		int d = getMonthLastDay(y, m);
		string sDayEnd = str::format("%2d", d);
		return time + "-01 00:00:00~" + time + "-" + sDayEnd + " 23:59:59";
	}
	//2020-02-02
	else if (time[4] == '-' && time.length() == 10) {
		return time + " 00:00:00~" + time + " 23:59:59";
	}
	return time;
}

bool TIME_SELECTOR::parseTimeRange(string condition)
{
	size_t pos = condition.find("~");
	 strStart = condition.substr(0, pos);
	 strEnd = condition.substr(pos + 1, condition.length() - pos - 1);
	if (strStart.find(":") == string::npos)
		strStart += " 00:00:00";
	if (strEnd.find(":") == string::npos)
		strEnd += " 23:59:59";
	stStart = timeopt::str2st(strStart);
	stEnd = timeopt::str2st(strEnd);
	startTime = timeopt::SysTime2Unix(stStart); 
	endTime = timeopt::SysTime2Unix(stEnd);
	return true;
}



bool TAG_SELECTOR::init(string tag, string rootTag){
	m_rootTag = rootTag;
	if (tag.find("*") != string::npos)
	{
		//如果tag是 * ,rootTag是杭州，那么通配选择是  杭州.*
		//TAG::addRoot后会加上.  , 这样子可以避免 通配选择变成  杭州*, 如果是  杭州* ,会错误的选中例如  杭州(仿真).温度 这类不该选中的位号
		string tagExp = TAG::addRoot(tag, rootTag);
		string regExp = tagExp;
		regExp = str::replace(regExp, ".", "\\.");
		regExp = str::replace(regExp, "*", ".*");
		fuzzyMatchExp.push_back(tagExp);
		fuzzyMatchRegExp.push_back(regExp);
	}
	else
	{
		tag = TAG::addRoot(tag, rootTag);
		exactMatchExp.push_back(tag);
	}

	return true;
}

bool TAG_SELECTOR::init(json tag, string rootTag)
{
	if (tag.is_string()) {
		return init(tag.get<string>(), rootTag);
	}
	else if(tag.is_array()){
		m_rootTag = rootTag;
		for (auto& i : tag) {
			if (i.is_string()) {
				init(i.get<string>(), rootTag);
			}
		}
		return true;
	}

	return false;
}

bool TAG_SELECTOR::match(string tag){
	for (int i = 0; i < exactMatchExp.size(); i++) {
		string& exp = exactMatchExp[i];
		if (exp == tag) {
			return true;
		}
	}

	for (int i = 0; i < fuzzyMatchRegExp.size(); i++) {
		string& sreg = fuzzyMatchRegExp[i];
		std::regex reg(sreg);
		if (std::regex_match(tag, reg))
		{
			return true;
		}
	}
	return false;
}

bool TAG_SELECTOR::singleSelMode()
{
	if (fuzzyMatchExp.size() == 0 && exactMatchExp.size() == 1) {
		return true;
	}
	return false;
}

CONDITION_SELECTOR::CONDITION_SELECTOR()
{
	bEnable = false;
}

CONDITION_SELECTOR::~CONDITION_SELECTOR()
{
#ifdef ENABLE_JERRY_SCRIPT
	if (filterExp.length() > 0)
	{
		jerry_release_value(global_object);
		jerry_cleanup();
	}
#endif
}

#ifdef ENABLE_JERRY_SCRIPT
bool CONDITION_SELECTOR::setScriptEngineObj(yyjson_val* jObj, jerry_value_t engineObj)
{
	size_t idx, maxIdx;
	yyjson_val* key, * value;
	yyjson_obj_foreach(jObj, idx, maxIdx, key, value) {
		jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)yyjson_get_str(key));
		jerry_value_t prop_value;
		if (yyjson_is_str(value))
			prop_value = jerry_create_string_from_utf8((const jerry_char_t*)yyjson_get_str(value));
		else if (yyjson_is_uint(value)) //此处 int类型和float类型要分开处理，由于float的精度问题，如果int转float，在脚本中判断 == 的时候可能会失败
		{
			uint64_t digits[1] = { yyjson_get_uint(value) };
			prop_value = jerry_create_bigint(digits, 1, false);
		}
		else if (yyjson_is_sint(value))
		{
			uint64_t digits[1] = { yyjson_get_sint(value) };
			prop_value = jerry_create_bigint(digits, 1, true);
		}
		else if (yyjson_is_real(value))
			prop_value = jerry_create_number(yyjson_get_real(value));
		else if (yyjson_is_bool(value))
			prop_value = jerry_create_boolean(yyjson_get_bool(value));
		else if (yyjson_is_obj(value))
		{
			prop_value = jerry_create_object();
			setScriptEngineObj(value, prop_value);
		}


		jerry_value_t set_result = jerry_set_property(engineObj, prop_name, prop_value);
		if (jerry_value_is_error(set_result)) {
			jerry_error_t error = jerry_get_error_type(set_result);
		}
		jerry_release_value(set_result);
		jerry_release_value(prop_name);
		jerry_release_value(prop_value);
	}
	return true;
}


bool CONDITION_SELECTOR::setScriptEngineObj(yyjson_mut_val* jObj, jerry_value_t engineObj)
{
	size_t idx, maxIdx;
	yyjson_mut_val* key, * value;
	yyjson_mut_obj_foreach(jObj, idx, maxIdx, key, value) {
		jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)yyjson_mut_get_str(key));
		jerry_value_t prop_value;
		if (yyjson_mut_is_str(value))
			prop_value = jerry_create_string_from_utf8((const jerry_char_t*)yyjson_mut_get_str(value));
		else if (yyjson_mut_is_uint(value)) //此处 int类型和float类型要分开处理，由于float的精度问题，如果int转float，在脚本中判断 == 的时候可能会失败
		{
			uint64_t digits[1] = {yyjson_mut_get_uint(value)};
			prop_value = jerry_create_bigint(digits,1,false);
		}
		else if (yyjson_mut_is_sint(value))
		{
			uint64_t digits[1] = { yyjson_mut_get_sint(value) };
			prop_value = jerry_create_bigint(digits, 1, true);
		}
		else if (yyjson_mut_is_real(value))
			prop_value = jerry_create_number(yyjson_mut_get_real(value));
		else if (yyjson_mut_is_bool(value))
			prop_value = jerry_create_boolean(yyjson_mut_get_bool(value));
		else if (yyjson_mut_is_obj(value))
		{
			prop_value = jerry_create_object();
			setScriptEngineObj(value, prop_value);
		}


		jerry_value_t set_result = jerry_set_property(engineObj, prop_name, prop_value);
		if (jerry_value_is_error(set_result)) {
			jerry_error_t error = jerry_get_error_type(set_result);
		}
		jerry_release_value(set_result);
		jerry_release_value(prop_name);
		jerry_release_value(prop_value);
	}
	return true;
}
#endif

bool CONDITION_SELECTOR::match(yyjson_mut_val* de)
{
#ifdef ENABLE_JERRY_SCRIPT
	if (!bEnable)
		return true;

	bool bMatch = true;

	if (yyjson_mut_is_obj(de))
	{
		yyjson_mut_val* jVal = yyjson_mut_obj_get(de, db.m_dbFmt.deItemKey_value.c_str());
		setScriptEngineObj(jVal, global_object);
	}
	else
	{

	}

	/* Run the demo script with 'eval' */
	jerry_value_t eval_ret = jerry_eval((jerry_char_t*)filterExp.c_str(),
		filterExp.length(),
		JERRY_PARSE_NO_OPTS);

	/* Check if there was any error (syntax or runtime) */
	bool run_ok = !jerry_value_is_error(eval_ret);
	jerry_error_t error = jerry_get_error_type(eval_ret);
	jerry_release_value(eval_ret);

	if (run_ok)
	{
		bMatch = jerry_value_to_boolean(eval_ret);
		return bMatch;
	}
	else
	{
		db_exception e;
		if (error == JERRY_ERROR_REFERENCE)
			e.m_error = "db exception: error when execute filter script,reference not found!";
		else if (error == JERRY_ERROR_TYPE)
		{
			// A.str1.indexOf("xxx") 如果A不存在 str1成员，会抛出此错误
			e.m_error = "db exception: error when execute filter script,error type!";
		}
		else
			e.m_error = "db exception: error when execute filter script";
		throw e;
	}
	//过滤器执行出错，统一不过滤
#endif
	return true;
}

bool CONDITION_SELECTOR::match(string& de)
{
#ifdef ENABLE_JERRY_SCRIPT
	if (!bEnable)
		return true;

	bool bMatch = true;
	json jDe = json::parse(de);
	//将数据元的属性
	if (jDe[db.m_dbFmt.deItemKey_value.c_str()].is_object())
	{
		json& jVal = jDe[db.m_dbFmt.deItemKey_value.c_str()];
		jsonVal2jerryVal(jVal, global_object);
	}
	else
	{

	}


	/* Run the demo script with 'eval' */
	jerry_value_t eval_ret = jerry_eval((jerry_char_t*)filterExp.c_str(),
		filterExp.length(),
		JERRY_PARSE_NO_OPTS);

	/* Check if there was any error (syntax or runtime) */
	bool run_ok = !jerry_value_is_error(eval_ret);
	jerry_error_t error = jerry_get_error_type(eval_ret);
	jerry_release_value(eval_ret);

	if (run_ok)
	{
		bMatch = jerry_value_to_boolean(eval_ret);
		return bMatch;
	}
	else
	{
		db_exception e;
		if(error == JERRY_ERROR_REFERENCE)
			e.m_error = "db exception: error when execute filter script,reference not found!";
		else
			e.m_error = "db exception: error when execute filter script";
		throw e;
	}
	//过滤器执行出错，统一不过滤
#endif
	return true;
}

bool CONDITION_SELECTOR::match(yyjson_val* de)
{
#ifdef ENABLE_JERRY_SCRIPT
	if (!bEnable)
		return true;

	bool bMatch = true;

	if (yyjson_is_obj(de))
	{
		setScriptEngineObj(de, global_object);
	}
	else
	{

	}

	/* Run the demo script with 'eval' */
	jerry_value_t eval_ret = jerry_eval((jerry_char_t*)filterExp.c_str(),
		filterExp.length(),
		JERRY_PARSE_NO_OPTS);

	/* Check if there was any error (syntax or runtime) */
	bool run_ok = !jerry_value_is_error(eval_ret);
	jerry_error_t error = jerry_get_error_type(eval_ret);
	jerry_release_value(eval_ret);

	if (run_ok)
	{
		bMatch = jerry_value_to_boolean(eval_ret);
		return bMatch;
	}
	else
	{
		db_exception e;
		if (error == JERRY_ERROR_REFERENCE)
			e.m_error = "db exception: error when execute filter script,reference not found!";
		else if (error == JERRY_ERROR_TYPE)
		{
			// A.str1.indexOf("xxx") 如果A不存在 str1成员，会抛出此错误
			e.m_error = "db exception: error when execute filter script,error type!";
		}
		else
			e.m_error = "db exception: error when execute filter script";
		throw e;
	}
	//过滤器执行出错，统一不过滤
#endif
	return true;
}

bool CONDITION_SELECTOR::init(string filter)
{
#ifdef ENABLE_JERRY_SCRIPT
	if (filter.length() > 0)
	{
		filterExp = filter;
		tls_context = jerry_create_context(1024, context_alloc_fn, NULL);;
		jerry_init(JERRY_INIT_EMPTY);
		bEnable = true;
		global_object = jerry_get_global_object();
	}
#endif
	return true;
}


