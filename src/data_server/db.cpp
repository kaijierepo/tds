#include "pch.h"
#include "db.h"
#include "../common/simdjson.h"
#include <iostream>
#include <sstream>
#include <filesystem>
#include "logger.h"
#include "yyjson.h"
#include "xiaot/scriptHost.h"


using namespace std::filesystem;
using namespace simdjson;
database db;

database::database()
{
	m_path = fs::appPath() + "/db";
}

string database::getPath_deFile(string strTag, SYSTEMTIME stTime)
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

string database::getName_deFile(string tag, SYSTEMTIME time)
{
	string timeStamp = str::format("%02d%02d%02d", time.wHour, time.wMinute, time.wSecond);
	return timeStamp;
}

//对9个文件名非法字符进行转义  / \ : * ? " < > |
string database::changeCharForFileName(string s) {
	unsigned char c = '/';
	string r = str::format("[%02X]", c);
	s = str::replace(s, "/", r);

	c = '\\';
	r = str::format("[%02X]", c);
	s = str::replace(s, "\\", r);

	c = ':';
	r = str::format("[%02X]", c);
	s = str::replace(s, ":", r);

	c = '*';
	r = str::format("[%02X]", c);
	s = str::replace(s, "*", r);

	c = '?';
	r = str::format("[%02X]", c);
	s = str::replace(s, "?", r);

	c = '\"';
	r = str::format("[%02X]", c);
	s = str::replace(s, "\"", r);

	c = '<';
	r = str::format("[%02X]", c);
	s = str::replace(s, "<", r);

	c = '>';
	r = str::format("[%02X]", c);
	s = str::replace(s, ">", r);

	c = '|';
	r = str::format("[%02X]", c);
	s = str::replace(s, "|", r);

	return s;
}

string database::getPath_dataFolder(string strTag, SYSTEMTIME date)
{
	strTag = changeCharForFileName(strTag);
	strTag = str::replace(strTag,".", "/");
	string strURL= str::format("/%04d%02d/%02d/", date.wYear, date.wMonth, date.wDay);
	strURL += strTag;
	strURL = m_path + "/" + strURL;
	return strURL;
}

string database::getPath_dbFile(string strTag,SYSTEMTIME date)
{
	string folder = getPath_dataFolder(strTag,date);
	return folder + "/db.json";
}



void database::Insert(string strTag, SYSTEMTIME stTime, json& jData, json dataFile)
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


bool database::Select(string tag, TIME_SELECTOR& timeSelector, string filter, DB_DATA_SET& result)
{
	return Select_simdjson(tag, timeSelector, filter, result);
}

bool database::Select_yyjson(vector<string> tagSet, TIME_SELECTOR& timeSelector, string filter,string& result)
{
	TIME_SELECTOR& tf = timeSelector;
	time_t loadTime = tf.endTime;
	string strDataFmt = "";
	string strRawDataFmt = "";
	SYSTEMTIME stTemp;

	ATTRI_SELECTOR attriFilter;
	attriFilter.init(filter);

	double max = -1000000000;
	double min = 1000000000;
	double avg = 0;
	int count = 0;

	vector<yyjson_doc*> src_doc;
	vector< yyjson_mut_doc*> src_mut_doc;
	map<string, yyjson_mut_val*> mapRlt;

	for (int tagIdx = 0; tagIdx < tagSet.size(); tagIdx++)
	{
		string& tag = tagSet[tagIdx]; // yyjson 在创建字符串对象的时候，不复制字符串，源字符串内存不能释放.因此使用string&.
		
		//按天加载数据库文件
		for (; loadTime >= tf.startTime; loadTime -= 24 * 60 * 60)
		{
			//加载数据元列表
			stTemp = timeopt::Unix2SysTime(loadTime);
			string dbFile = getPath_dbFile(tag, stTemp);
			string dbData;
			fs::readFile(dbFile, dbData);
			if (dbData == "")
				continue;

			// Read JSON and get root,转为带 mut,因为后面会修改里面的值
			yyjson_doc* doc = yyjson_read(dbData.c_str(), dbData.length(), 0);
			src_doc.push_back(doc);
			yyjson_mut_doc* mut_doc = yyjson_mut_doc_new(NULL);
			src_mut_doc.push_back(mut_doc);
			yyjson_val* root = yyjson_doc_get_root(doc);


			size_t idx, max;
			yyjson_val* val;
			yyjson_arr_foreach(root, idx, max, val) {
				yyjson_mut_val* jDE = yyjson_val_mut_copy(mut_doc, val);
				yyjson_mut_val* yyTime = yyjson_mut_obj_get(jDE, "time");
				string_view szTime = yyjson_mut_get_str(yyTime);

				//先生成完整时间戳，再进行match判断
				if (szTime.length() == 19)
				{
					szTime = szTime.substr(11, 8); //取出时分秒
				}
				string strTime = timeopt::TimeToYMD(stTemp) + " " + string(szTime);
				if (!tf.Match(strTime))
					continue;


				//当进行多位号搜索时，需要加入tag标签
				yyjson_mut_val* tagKey = yyjson_mut_str(mut_doc, "tag");
				yyjson_mut_val* tagVal = yyjson_mut_str(mut_doc, tag.c_str());
				yyjson_mut_obj_put(jDE, tagKey, tagVal);


				if (attriFilter.bEnable && !attriFilter.match(jDE))
				{
					continue;
				}

				mapRlt[strTime  + tag + str::fromInt(idx)] = jDE; //不同位号的数据按照时间顺序排序.允许 同一个位号多个数据源时间点相同
				count++;
				if (tf.AmountMatch(count))
					goto DATA_SET_LOADED;
			}
		}
	}

	return true;

DATA_SET_LOADED:

	//使用新的yyjson doc对象输出结果. 将多个位号，多个时间段的原始数据合并成1个json查询结果对象
	yyjson_mut_doc* rlt_mut_doc = yyjson_mut_doc_new(NULL);
	yyjson_mut_val* rlt_mut_root = yyjson_mut_arr(rlt_mut_doc); //创建一个数组
	yyjson_mut_doc_set_root(rlt_mut_doc, rlt_mut_root);

	for (auto& i : mapRlt)
	{
		yyjson_mut_arr_append(rlt_mut_root, i.second);
	}
	
	size_t len = 0;
	result = yyjson_mut_write(rlt_mut_doc, 0, &len);

	//释放结果
	yyjson_mut_doc_free(rlt_mut_doc);
	//释放源
	for (int i = 0; i < src_doc.size(); i++)
	{
		yyjson_doc_free(src_doc[i]);
		yyjson_mut_doc_free(src_mut_doc[i]);
	}

	return true;
}

//2021.10.21 此时simdjson还不支持使用数组下标访问数组元素
bool database::Select_simdjson(string tag, TIME_SELECTOR& timeSelector, string filter, DB_DATA_SET& result)
{
	TIME_SELECTOR& tf = timeSelector;
	time_t loadTime = tf.endTime;
	string strDataFmt = "";
	string strRawDataFmt = "";
	SYSTEMTIME stTemp;

	ATTRI_SELECTOR af;
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

bool database::Update(string tag, SYSTEMTIME stTime, string& sData)
{
	json jData = json::parse(sData);
	return Update(tag, stTime, jData);
}

bool database::Update(string tag, SYSTEMTIME stTime, json& jData)
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

bool database::Delete(string tag, SYSTEMTIME stTime)
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

void database::saveDEFile(string strTag, SYSTEMTIME stTime, string deFileUrl)
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
				int iPos = strTmp.find(strExtType);
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

string database::parseSuffix(string deFileUrl)
{
	string suffix = "";
	int posDot = deFileUrl.rfind(".");
	int posSlash = deFileUrl.rfind("/");
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
	int iPos = strPath.rfind('/');
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

bool TIME_SELECTOR::Match(string& timeTag)
{
	for (int i = 0; i < vecCondition.size(); i++)
	{
		TIME_CONDITON& tc = vecCondition.at(i);
		if (!tc.Match(timeTag))
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
		SYSTEMTIME sysStTime, sysEdTime;
		GetLocalTime(&sysEdTime);
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
		int pos = condition.find("~");
		string strStart = condition.substr(0, pos);
		string strEnd = condition.substr(pos + 1, condition.length() - pos - 1);
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
			int pos = condition.find("~");
			string strStart = condition.substr(0, pos);
			string strEnd = condition.substr(pos + 1, condition.length() - pos - 1);
			startHMS = timeopt::HMS2Sec(strStart);
			endHMS = timeopt::HMS2Sec(strEnd);
		}
		else//相对时间模式
		{
			condition = timeopt::rel2abs(condition);
			int pos = condition.find("~");
			string strStart = condition.substr(0, pos);
			string strEnd = condition.substr(pos + 1, condition.length() - pos - 1);
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

bool TIME_CONDITON::Match(string& timeTag)
{
	if (IsHMS)
	{
		string hms = timeTag.substr(11, 8);
		int iTime = timeopt::HMS2Sec(hms);
		if (iTime >= startHMS && iTime <= endHMS)
			return true;
	}
	else
	{
		time_t tt = timeopt::SysTime2Unix(timeopt::str2st(timeTag));
		if (tt >= startTime && tt <= endTime)
			return true;
	}
	return false;
}

bool TAG_SELECTOR::init(string tag){
	tagExp = tag;
	regExp = tagExp;
	regExp = str::replace(regExp, ".", "\\.");
	regExp = str::replace(regExp, "*", ".*");

	if (tagExp.find('*') == string::npos)
	{
		singleMode = true;
	}
	else
	{
		singleMode = false;
	}

	return true;
}

bool TAG_SELECTOR::match(string tag){
		//exact match
		if (tagExp.find('*') == string::npos)
		{
			if(tagExp == tag)
				return true;
			else
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

ATTRI_SELECTOR::ATTRI_SELECTOR()
{
	bEnable = false;
}

ATTRI_SELECTOR::~ATTRI_SELECTOR()
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
bool ATTRI_SELECTOR::setScriptEngineObj(yyjson_mut_val* jObj, jerry_value_t engineObj)
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

bool ATTRI_SELECTOR::match(yyjson_mut_val* de)
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

bool ATTRI_SELECTOR::match(string& de)
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

bool ATTRI_SELECTOR::init(string filter)
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
