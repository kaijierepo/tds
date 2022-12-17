#include "pch.h"
#include "db.h"
//#include "../common/simdjson.h"
#include <iostream>
#include <sstream>
#include <filesystem>
#include "logger.h"
#include "yyjson.h"
#include "xiaot/scriptHost.h"
#include "prj.h"
#include "tdsSession.h"

using namespace std::filesystem;
//using namespace simdjson;
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
	strURL = m_path + "/" + strURL;
	return strURL;
}

string database::getPath_dbFile(string strTag,TIME date)
{
	string folder = getPath_dataFolder(strTag,date);
	return folder + "/db.json";
}



void database::Insert(string strTag, TIME stTime, json& jData, json dataFile)
{
	string folderPath = getPath_dataFolder(strTag, stTime);
	string dlPath = folderPath + "/" + "db.json";
	if(!fs::fileExist(folderPath))
		fs::createFolderOfPath(folderPath.c_str());
	json jDE;
	jDE["time"] = timeopt::st2str(stTime);
	jDE["val"] = jData;
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
		FILE* fp = _wfopen(charCodec::utf8toUtf16(dlPath).c_str(), L"rb+");
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

bool database::loadDeList(DE_SELECTOR& deSel, vector<TAG_DB_FILE_SET*>& tagDBFileSet, map<string, yyjson_mut_val*>& mapRlt, yyjson_mut_doc* mut_doc)
{
	//返回的数据元是否需要携带tag字段
	bool withTag = tagDBFileSet.size() > 1 ? true : false;
	if (deSel.tagSel.getTag)
		withTag = true;

	double max = -1000000000;
	double min = 1000000000;
	double avg = 0;
	int count = 0;
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_FILE_SET& fSet = *tagDBFileSet[tagIdx];
		string& tag = fSet.tag; // yyjson 在创建字符串对象的时候，不复制字符串，源字符串内存不能释放.因此使用string&.
		string& relTag = fSet.relTag;

		//加载每个数据文件中的数据
		for (int i = 0; i < fSet.fileList.size(); i++)
		{
			//加载数据元列表
			DB_FILE* pdf = fSet.fileList[i];

			size_t idx, max;
			yyjson_val* val;
			int lastDeTime = 0;
			int currDeTime = 0;
			string deTime = pdf->ymd + " 00:00:00";
			yyjson_arr_foreach(pdf->root, idx, max, val) {
				//下采样机制。每downsampling interval 输出1个数据点;例如dsi=3,则输出第0个，第3个，第6个。。。
				//最后1个下采样间隔全部输出
				if (deSel.interval.type == DST_Count)
				{
					if (idx % deSel.interval.dsi > 0 && idx < max - deSel.interval.dsi) continue;
				}


				yyjson_mut_val* jDE = yyjson_val_mut_copy(mut_doc, val);//把imutableVal拷贝成mutable，保存在mutDoc中
				yyjson_mut_val* yyTime = yyjson_mut_obj_get(jDE, "time");
				string_view szTime = yyjson_mut_get_str(yyTime);

				//先生成完整时间戳，再进行match判断
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

				if (deSel.interval.type == DST_Time) {
					HMS_STR* p = (HMS_STR*)pHms;
					currDeTime = p->getTotalSec();
					if (currDeTime - lastDeTime < deSel.interval.dsti) continue;
				}

				if (withTag)
				{
					//当进行多位号搜索时，需要加入tag标签
					//relTag指向的变量在write_doc之前不能被销毁
					yyjson_mut_val* tagKey = yyjson_mut_str(mut_doc, "tag");
					yyjson_mut_val* tagVal = yyjson_mut_str(mut_doc, relTag.c_str());
					yyjson_mut_obj_put(jDE, tagKey, tagVal);
				}


				if (deSel.condition.bEnable && !deSel.condition.match(jDE))
				{
					continue;
				}

				lastDeTime = currDeTime;
				string sortFlag = "";
				if (deSel.sortKey.length() > 0) {
					yyjson_mut_val* yyVal = yyjson_mut_obj_get(jDE, "val");
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

				mapRlt[sortFlag + deTime + tag + std::to_string(idx)] = jDE; //不同位号的数据按照时间顺序排序.允许 同一个位号多个数据源时间点相同
				count++;

				if (deSel.timeSel.AmountMatch(count))
					return true;
			}
		}
	}
	return true;
}

bool database::loadDeList_tagAsColume(DE_SELECTOR& deSel, vector<TAG_DB_FILE_SET*>& tagDBFileSet, map<string, yyjson_mut_val*>& mapRlt, yyjson_mut_doc* mut_doc)
{
	double max = -1000000000;
	double min = 1000000000;
	double avg = 0;
	int count = 0;
	vector<yyjson_mut_val*> tagColList;
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_FILE_SET& fSet = *tagDBFileSet[tagIdx];
		yyjson_mut_val* tagKey;
		if (deSel.columeLabel == "tag") {
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
		//tagKey = yyjson_mut_str(mut_doc, fSet.colKey.c_str());
		//tagColList.push_back(tagKey);
	}

	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_FILE_SET& fSet = *tagDBFileSet[tagIdx];
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
			yyjson_arr_foreach(pdf->root, idx, max, de) {
				//下采样机制。每downsampling interval 输出1个数据点;例如dsi=3,则输出第0个，第3个，第6个。。。
				//最后1个下采样间隔全部输出
				if (deSel.interval.type == DST_Count)
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

				if (deSel.interval.type == DST_Time) {
					HMS_STR* p = (HMS_STR*)pHms;
					currDeTime = p->getTotalSec();
					if (currDeTime - lastDeTime < deSel.interval.dsti) continue;
				}


				//根据时间戳新建或者记录或者取出记录
				yyjson_mut_val* jRecord;
				map<string, yyjson_mut_val*>::iterator itRecord = mapRlt.find(deTime);
				if (itRecord != mapRlt.end()) {
					jRecord = itRecord->second;
				}
				else {
					jRecord = yyjson_mut_obj(mut_doc);
					//time字段
					yyjson_mut_val* timeKey = yyjson_mut_str(mut_doc, "time");
					yyjson_mut_val* timeVal = yyjson_mut_str(mut_doc, szTime.data());
					yyjson_mut_obj_put(jRecord, timeKey, timeVal);
					//位号字段
					for (int i = 0; i < tagDBFileSet.size(); i++) {
						yyjson_mut_val* tagColKeyInit = yyjson_mut_str(mut_doc,tagDBFileSet[i]->colKey.c_str());
						yyjson_mut_val* tagColValInit = yyjson_mut_null(mut_doc);
						yyjson_mut_obj_put(jRecord, tagColKeyInit, tagColValInit);
					}

					std::pair<string, yyjson_mut_val*> recPair;
					recPair.first = deTime;
					recPair.second = jRecord;
					auto insertRet = mapRlt.insert(recPair);
					itRecord = insertRet.first;
				}
				
				yyjson_mut_val* tagColKey = yyjson_mut_str(mut_doc, fSet.colKey.c_str());
				yyjson_val* yyVal = yyjson_obj_get(de, "val");
				yyjson_mut_val* tagColVal = yyjson_val_mut_copy(mut_doc, yyVal);
				yyjson_mut_obj_put(jRecord, tagColKey, tagColVal);


				mapRlt[deTime] = jRecord; //不同位号的数据按照时间顺序排序.允许 同一个位号多个数据源时间点相同
				count++;
			}
		}
	}
	return true;
}


bool database::Select_yyjson(DE_SELECTOR& deSel, SELECT_RLT& result)
{
	//获取需要加载数据的位号集合
	vector<string> tagSet;
	prj.getTagsByTagSelector(tagSet, deSel.tagSel);

	//获取本次查询返回的相对位号，初始化数据库原始数据数据结构
	vector<TAG_DB_FILE_SET*> tagDBFileSet;  //数据库原始文件数据
	for (int i = 0; i < tagSet.size(); i++)
	{
		TAG_DB_FILE_SET* p = new TAG_DB_FILE_SET();
		p->tag = tagSet[i];
		p->relTag = TAG::trimRoot(tagSet[i], deSel.tagSel.m_rootTag);
		tagDBFileSet.push_back(p);
	}
	
	//加载文件原始数据
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_FILE_SET& fSet = *tagDBFileSet[tagIdx];


		time_t loadTime = deSel.timeSel.endTime;
		for (; loadTime >= deSel.timeSel.startTime; loadTime -= 24 * 60 * 60)
		{
			DB_FILE* pdf = new DB_FILE();
			pdf->time = timeopt::Unix2SysTime(loadTime);
			pdf->ymd = timeopt::TimeToYMD(pdf->time);
			pdf->path = getPath_dbFile(fSet.tag, pdf->time);
			fs::readFile(pdf->path, pdf->data);
			if (pdf->data == "") {
				delete pdf;
				continue;
			}
			fSet.fileList.push_back(pdf);

			//从数据库的原始json数据。
			pdf->doc = yyjson_read(pdf->data.c_str(), pdf->data.length(), 0);
			pdf->root = yyjson_doc_get_root(pdf->doc);
		}
		if (fSet.fileList.size() == 0)
			continue;
		//头尾两个数据文件需要进行时间范围检查，中间的不需要
		fSet.fileList[0]->boundaryFile = true;
		fSet.fileList[fSet.fileList.size() - 1]->boundaryFile = true;

		//数据只有1天的，不进行下采样
		if (fSet.fileList.size() <= 1)
			deSel.interval.type = DST_None;
	}
	

	map<string, yyjson_mut_val*> mapRlt;
	yyjson_mut_doc* rlt_mut_doc = yyjson_mut_doc_new(NULL);
	
	if (deSel.tagAsColume) {
		loadDeList_tagAsColume(deSel, tagDBFileSet, mapRlt,rlt_mut_doc);
	}
	else {
		loadDeList(deSel,tagDBFileSet, mapRlt, rlt_mut_doc);
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
		result.dataList = p;
	}
	result.count = mapRlt.size();

	//释放结果
	yyjson_mut_doc_free(rlt_mut_doc);
	//释放源
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_DB_FILE_SET* fSet = tagDBFileSet[tagIdx];
		delete fSet;
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
			json& jOld = jDE["val"];
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
		copy(charCodec::utf8toUtf16(deFileUrl),charCodec::utf8toUtf16(path));
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


void database::LoadAllFile_FromPath(string strPath, string strExtType, vector<string>& vecFiles, bool bOnlyName, bool bIncludeChild)
{
	strPath += "/";
	char szFind[260];
	char szFile[1000] = { 0 };
	WIN32_FIND_DATA FindFileData;
	strcpy_s(szFind, strPath.c_str());
	strcat_s(szFind, "*.*");
	HANDLE hFind = ::FindFirstFile(szFind, &FindFileData);
	if (INVALID_HANDLE_VALUE == hFind)
		return;

	while (true)
	{
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (FindFileData.cFileName[0] != '.')
			{
				if (bIncludeChild)
				{
					string strSubName = FindFileData.cFileName;
					string strSubPath = strPath + "/" + strSubName;
					LoadAllFile_FromPath(strSubPath, strExtType, vecFiles, bOnlyName, bIncludeChild);
				}
			}
		}
		else
		{
			string strTmp = FindFileData.cFileName;//保存文件名，包括后缀名
			string strFilePath = strPath + "/" + strTmp;

			bool bFindFile = false;

			if (strExtType.length() > 0)//指定后缀
			{
				size_t iPos = strTmp.find(strExtType);
				if (iPos >= 0 && strTmp.length() == iPos + strExtType.length())
				{
					bFindFile = true;
				}
			}
			else
			{
				bFindFile = true;
			}

			if (bFindFile)
			{
				if (bOnlyName)
				{
					vecFiles.push_back(strTmp);
				}
				else
				{
					vecFiles.push_back(strFilePath);
				}
			}
		}
		if (!FindNextFile(hFind, &FindFileData))
			break;
	}
	FindClose(hFind);
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
	fs::writeFile(m_path + "/db.json",s);
	return true;
}

bool database::Open(string strDBUrl,string name)
{
	if (strDBUrl == "")
		return false;

	m_path = fs::toAbsolutePath(strDBUrl);

	if(!fs::fileExist(m_path + "/db.json"))
		create(strDBUrl,name);

	string s;
	fs::readFile(m_path + "/db.json",s);
	json j = json::parse(s);

	m_name = j["name"];
	
	return true;
}

void database::Close()
{

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

	//parse tag selector
	std::string strRootTag;
	json jTag;
	if (!params.contains("tag"))
	{
		return makeRPCError(TEC_paramMissing, "param tag missing or not string type");
	}
	else
		jTag = params["tag"];
		

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
	if (params["type"] != nullptr)
		deSel.tagSel.type = params["type"].get<string>();

	//parse interval selector
	json jDsi = params["interval"];
	if (jDsi.is_number())
	{
		deSel.interval.type = DST_Count;
		deSel.interval.dsi = jDsi.get<int>();
	}
	else if (jDsi.is_string())
	{
		string sDsti = params["interval"].get<string>();
		deSel.interval.dsti = timeopt::dhmsSpan2Seconds(sDsti);
		if (deSel.interval.dsti > 0)
			deSel.interval.type = DST_Time;
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

	if (params["columeLabel"].is_string()) {
		deSel.columeLabel = params["columeLabel"].get<string>();
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

	SELECT_RLT result;
	try
	{
		db.Select_yyjson(deSel, result);
	}
	catch (std::exception& e)
	{
		json jerror = e.what();
		resp.error = jerror.dump();
	}

	resp.result = result.dataList;
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

	json jRlt = result.count;
	resp.result = jRlt.dump();
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
}


TIME_SELECTOR::TIME_SELECTOR()
{
	startTime = 0;
	endTime = 0;
	m_dataNum = 0;
}

bool TIME_SELECTOR::Match(string& deTime)
{
	//deTime.st = timeopt::str2st(timeTag);
	//deTime.tt = timeopt::SysTime2Unix(deTime.st);
	for (int i = 0; i < vecCondition.size(); i++)
	{
		TIME_CONDITON& tc = vecCondition.at(i);
		if (!tc.Match(deTime))
			return false;
	}
	return true;
}

bool TIME_SELECTOR::AmountMatch(int amount)
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

bool TIME_SELECTOR::init(string time)
{
	if (time.find("e") != string::npos)
	{
		time = time.substr(0, time.length() - 1);
		m_dataNum = _ttoi(time.c_str());
		TIME sysStTime, sysEdTime;
		timeopt::now(&sysEdTime);
		TIME_CONDITON tcStartTime, tcEndTime;
		tcStartTime.init("2020-01-01 00:00:00");
		string str = str::format("%4d-%02d-%02d %02d:%02d:%02d", sysEdTime.wYear, sysEdTime.wMonth, sysEdTime.wDay, sysEdTime.wHour, sysEdTime.wMinute, sysEdTime.wSecond);
		tcEndTime.init(str);
		startTime = tcStartTime.startTime;
		endTime = tcEndTime.endTime;
	}
	else
	{
		//获得条件列表
		vector<string> v;
		if (time.find("&&") != string::npos)//组合条件 仅限于绝对日期区间模式
		{
			str::split(v, time, "&&");
		}
		else
		{
			v.push_back(time);
		}

		//解析条件
		for (int i = 0; i < v.size(); i++)
		{
			TIME_CONDITON tc;
			tc.init(v.at(i));
			vecCondition.push_back(tc);
		}

		//获得整体时间范围，用于数据库遍历
		for (int i = 0; i < vecCondition.size(); i++)
		{
			TIME_CONDITON& tc = vecCondition.at(i);
			if (!tc.IsHMS)
			{
				stStart = tc.stStart;
				stEnd = tc.stEnd;
				startTime = tc.startTime;
				endTime = tc.endTime;
				break;//实际需要多个条件组合，目前不太会遇到这个场景，以后实现
			}
		}
	}
	return true;
}

bool TIME_CONDITON::init(string condition)
{
	if (condition.find('-') != string::npos)//年月日绝对区间模式
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
	}
	else
	{
		if (condition.find(':') != string::npos)//时分秒模式
		{
			IsHMS = true;
			size_t pos = condition.find("~");
			strStart = condition.substr(0, pos);
			strEnd = condition.substr(pos + 1, condition.length() - pos - 1);
			startHMS = timeopt::HMS2Sec(strStart);
			endHMS = timeopt::HMS2Sec(strEnd);
		}
		else//相对时间模式
		{
			condition = timeopt::rel2abs(condition);
			int pos = condition.find("~");
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
		}
	}
	return true;
}

bool TIME_CONDITON::Match(string& deTime)
{
	if (IsHMS)
	{
		//int iTime = det.st.wHour*60*60 + det.st.wMinute*60 + det.st.wSecond;
		//if (iTime >= startHMS && iTime <= endHMS)
		//	return true;
	}
	else
	{
		//字符串直接比较应该可以获得比先转换 time_t 跟高的性能
		if (deTime >= strStart && deTime <= strEnd)
			return true;
	}
	return false;
}

bool TAG_SELECTOR::init(string tag, string rootTag){
	m_rootTag = rootTag;
	if (tag.find("*") != string::npos)
	{
		mode = TSM_fuzzy_match;
		tagExp = TAG::addRoot(tag, rootTag);
		regExp = tagExp;
		regExp = str::replace(regExp, ".", "\\.");
		regExp = str::replace(regExp, "*", ".*");
	}
	else
	{
		mode = TSM_single;
		tag = TAG::addRoot(tag, rootTag);
		singleTag = tag;
	}

	return true;
}

bool TAG_SELECTOR::init(json tag, string rootTag)
{
	mode = TSM_invalid;
	if (tag.is_string()) {
		return init(tag.get<string>(), rootTag);
	}
	else if(tag.is_array()){
		m_rootTag = rootTag;
		for (auto& i : tag) {
			if (i.is_string()) {
				string sTag = TAG::addRoot(i.get<string>(), rootTag);
				multiTag.push_back(sTag);
			}
		}
		mode = TSM_multi;
		return true;
	}

	return false;
}

bool TAG_SELECTOR::match(string tag){
		//exact match
		if (mode == TSM_single)
		{
			if(tag == singleTag)
				return true;
			else
				return false;
		}
		else if(mode == TSM_multi)
		{
			for (int i = 0; i < multiTag.size(); i++) {
				if (tag == multiTag[i])
					return true;
			}
			return false;
		}
		//fuzzy match
		else
		{
			std::regex reg(regExp);
			if (std::regex_match(tag, reg))
			{
				return true;
			}
			else
			{
				return false;
			}
		}
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
		yyjson_mut_val* jVal = yyjson_mut_obj_get(de, "val");
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
	if (jDe["val"].is_object())
	{
		json& jVal = jDe["val"];
		scriptHost::setScriptEngineObj(jVal, global_object);
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

bool CONDITION_SELECTOR::init(string filter)
{
#ifdef ENABLE_JERRY_SCRIPT
	if (filter.length() > 0)
	{
		filterExp = filter;
		jerry_init(JERRY_INIT_EMPTY);
		bEnable = true;
		global_object = jerry_get_global_object();
	}
#endif
	return true;
}
