#include "pch.h"
#include "as.h"
#include "common.h"
#include "prj.h"
#include <regex>
#include "rpcHandler.h"
#include "db.h"
#include "tds.h"
#include "csvTable.h"


string csvTable::getFilePath(int y,int m){
	string p ;
	if(bOneFilePerMonth)
	{
		string strYM = str::format("%04d%02d", y, m);
		p = db.m_path + filePath + "_" + strYM + ".csv";
	}
	else
	{
		p = db.m_path + filePath  + ".csv";
	}
	return p;
}

string csvTable::getFilePath(string time){
	if(time == "")
		return db.m_path + filePath  + ".csv";

	SYSTEMTIME st = timeopt::str2st(time);
	int y,m;
	y = st.wYear;
	m = st.wMonth;
	return getFilePath(y,m);
}

void csvTable::loadFile(string strFile)
{
	if (buffData.size() == 0 || strFile != buffDataFilePath)
	{
		clearBuff();
		buffDataFilePath = strFile;
		string strDBData;
		fs::readFile(strFile, strDBData);
		vector<string> recLines;
		str::split(recLines, strDBData, "\r\n");
		if (recLines.size() < 1)
			return;

		for (int i = 1; i < recLines.size(); i++)
		{
			string& sline = recLines.at(i);
			if (str::trim(sline) == "")
				continue;

			vector<string> vecVals;
			parseCSVLine(sline, vecVals);

			json* pJ = new json();
			json& j = *pJ;

			for (int i = 0; i < m_vecKeys.size(); i++)
			{
				string& val = vecVals[i];
				if (val != "")
					j[m_vecKeys[i]] = json::parse(val);
				else
					j[m_vecKeys[i]] = nullptr;
			}

			buffData.push_back(pJ);
		}
	}
}

bool csvTable::parseCSVLine(const string& line, vector<string>& cols)
{
	string el;
	bool bInQuotation = false;
	for(int i=0;i<line.length();i++)
	{
		char* p = (char*)line.c_str() + i;
		if(!bInQuotation && *p == ',')
		{
			cols.push_back(el);
			el = "";
		}
		else if(*p =='\"')
		{
			bInQuotation = !bInQuotation;
			el += *p;
		}
		else
		{
			el += *p;
		}
	}
	cols.push_back(el);
	
	return true;
}

bool csvTable::toCSVLine(json& de,string& line)
{
	line = "";
	for(int i=0;i<m_vecKeys.size();i++)
	{
		if (i > 0)
			line += ",";

		string& k = m_vecKeys[i];
		if (de.contains(k))
		{
			line += de[k].dump();
		}
	}
	return true;
}

void csvTable::clearBuff()
{
	for (int i = 0; i < buffData.size(); i++)
	{
		delete buffData[i];
	}
	buffData.clear();
}


void csvTable::init(string file, vector<string>& header)
{
	filePath = file;
	m_vecKeys = header;
}

void csvTable::add(json& jRec)
{
	string time = timeopt::nowStr();
	loadFile(getFilePath(time));
	json* pJ = new json();
	json& j = *pJ;
	j = json::parse(jRec.dump());
	buffData.push_back(pJ);
	saveFile(getFilePath(time));
}

void csvTable::saveFile(string strFile)
{
	string data;
	string line;
	
	//生成表头
	for (int i = 0; i < m_vecKeys.size(); i++)
	{
		string& k = m_vecKeys[i];
		if (line != "")
			line += ",";
		line += k;
	}

	data = line + "\r\n";

	for (int i=0; i < buffData.size(); i++)
	{
		toCSVLine(*buffData[i],line);

		data += line + "\r\n";
	}
	fs::createFolderOfPath(strFile);
	fs::writeFile(strFile, data);
}

