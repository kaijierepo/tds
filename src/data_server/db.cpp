#include "pch.h"
#include "db.h"
#include "../common/simdjson.h"
#include <iostream>
#include <sstream>
using namespace simdjson;
database db;

database::database()
{

}

string database::getFileUrl(string strTag,SYSTEMTIME date)
{
	strTag = str::replace(strTag,".", "/");
	string strURL= str::format("/%04d%02d/%02d/", date.wYear, date.wMonth, date.wDay);
	strURL += strTag;
	string timeStamp = str::format("%02d%02d%02d",date.wHour,date.wMinute,date.wSecond);
	strURL += "/" + timeStamp;
	return strURL;
}

string database::getDBFolder(string strTag, SYSTEMTIME date)
{
	strTag = str::replace(strTag,".", "\\");
	string strURL= str::format("\\%04d%02d\\%02d\\", date.wYear, date.wMonth, date.wDay);
	strURL += strTag;
	strURL = m_path + "\\" + strURL;
	return strURL;
}

string database::getDBFile(string strTag,SYSTEMTIME date)
{
	string folder = getDBFolder(strTag,date);
	return folder + "\\db.json";
}


void database::INSERT_FILE(string strTag, SYSTEMTIME stTime, char* pData, int iLen, string fmt)
{
	string strFileName = str::format("%02d%02d%02d.", stTime.wHour, stTime.wMinute, stTime.wSecond);
	strFileName += fmt;

	string strPath = getDBFolder(strTag.c_str(), stTime);
	string strCurveURL = "\\" + strPath + "\\" + strFileName;

	strFileName = m_path + strCurveURL;
	fs::createFolderOfPath(strFileName.c_str());
	fs::writeFile(strFileName, pData, iLen);
}


void database::INSERT(string strTag, SYSTEMTIME stTime, json& jData, json dataFile)
{
	string dlPath = getDBFolder(strTag, stTime) + "\\" + "db.json";
	fs::createFolderOfPath(dlPath.c_str());
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
		fs::writeFile(dlPath,str);
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

bool database::SELECT(string tag, TIME_SELECTOR& timeSelector, string filter,DB_DATA_SET& result)
{
	TIME_SELECTOR& tf = timeSelector;
	time_t loadTime = tf.endTime;
	string strDataFmt = "";
	string strRawDataFmt = "";
	SYSTEMTIME stTemp;

	CAttriFilter af;
	af.Init(filter);

	double max = -1000000000;
	double min = 1000000000;
	double avg = 0;
	int count = 0;

	for (; loadTime >= tf.startTime; loadTime -= 24 * 60 * 60)
	{
		//加载数据元列表
		stTemp = timeopt::Unix2SysTime(loadTime);
		string dbFile = getDBFile(tag,stTemp);
		string dbData;
		fs::readFile(dbFile,dbData);
		if(dbData == "")
			continue;

		std::unique_ptr<char[]> padded_json_copy{new char[dbData.length() + SIMDJSON_PADDING]};
		memcpy(padded_json_copy.get(), dbData.c_str(), dbData.length());
		memset(padded_json_copy.get() + dbData.length(), 0, SIMDJSON_PADDING);
		simdjson::dom::parser parser;
		simdjson::dom::element dataList = parser.parse(padded_json_copy.get(), dbData.length(), false);

		if (dataList.is_null())
			continue;

		for(dom::object de : dataList)
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
			sDe = sDe.substr(0,sDe.length()-1);// remove the last char "}" ,and append additional attributes
			sDe += ",\"tag\":\"" + tag + "\"";

			bool bHavePic = false;
			simdjson::error_code error;
			error = de["pic"].get(bHavePic);
			if(bHavePic)
			{
				sDe += ",\"pic_url\":\"/db" + getFileUrl(tag,timeopt::str2st(strTime)) + ".jpg\"";
			}

			bool bHaveVideo = false;
			error = de["video"].get(bHaveVideo);
			if (bHaveVideo)
			{
				sDe += ",\"video_url\":\"/db" + getFileUrl(tag, timeopt::str2st(strTime)) + ".mp4\"";
			}

			sDe += "}";

			result[strTime + "+" + tag] = sDe;
		}
	}
	return true;
}


void database::INSERT_FILE(string strTag, SYSTEMTIME DataTime, string strDataFile, string suffix)
{
	/*
	string strPath = GetFolderURL(strTag.c_str(), DataTime);
	fs::createFolderOfPath(strPath.c_str());
	strPath += "\\" + GetHMSTag(DataTime);
	if (suffix.length() > 0)
		strPath += "." + suffix;
	CopyFile(strDataFile.c_str(), strPath.c_str(), false);*/
}


void database::LoadAllFile_FromPath(string strPath, string strExtType, vector<string>& vecFiles, bool bOnlyName, bool bIncludeChild)
{
	strPath += "\\";
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
					string strSubPath = strPath + "\\" + strSubName;
					LoadAllFile_FromPath(strSubPath, strExtType, vecFiles, bOnlyName, bIncludeChild);
				}
			}
		}
		else
		{
			string strTmp = FindFileData.cFileName;//保存文件名，包括后缀名
			string strFilePath = strPath + "\\" + strTmp;

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
	int iPos = strPath.rfind('\\');
	pfi->strName = strPath.substr(iPos+1,strPath.length() - 1 - iPos);

	strPath += "\\";
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
				string strSubPath = strPath + "\\" + strSubName;

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

bool TIME_SELECTOR::Init(string time)
{
	if (time.find("e") != string::npos)
	{
		time = time.substr(0, time.length() - 1);
		m_dataNum = _ttoi(time.c_str());
		SYSTEMTIME sysStTime, sysEdTime;
		GetLocalTime(&sysEdTime);
		TIME_CONDITON tcStartTime, tcEndTime;
		tcStartTime.Init("2020-01-01 00:00:00");
		string str = str::format("%4d-%02d-%02d %02d:%02d:%02d", sysEdTime.wYear, sysEdTime.wMonth, sysEdTime.wDay, sysEdTime.wHour, sysEdTime.wMinute, sysEdTime.wSecond);
		tcEndTime.Init(str);
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
			tc.Init(v.at(i));
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

bool TIME_CONDITON::Init(string condition)
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
	str::replace(regExp, ".", "\\.");
	str::replace(regExp, "*", ".*");
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

CAttriFilter::CAttriFilter()
{
}

bool CAttriFilter::Match(json& jAttri)
{
	return true;
}




bool CAttriFilter::Init(string filter)
{
	/*
	ScriptRunner sr;
	sr.SplitByLogicOperator(filter, cdtList);*/
	return true;
}
