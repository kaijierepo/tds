#pragma once

#include <mutex>
#include "db.h"
#include "json.hpp"
#include "tds.h"


class csvTable {
public:
	//bind with disk data file
	void init(string file,vector<string>& header);

	//table options
	void add(json& jRec);


public:


	csvTable() {
		bOneFilePerMonth = false;
	}
	string getFilePath(string time = "");
	string getFilePath(int y, int m);
	void loadFile(string strFile);
	bool parseCSVLine(const string& line, vector<string>& cols);
	bool toCSVLine(json& de, string& line);
	void saveFile(string strFile);
	
	string filePath;
	bool bOneFilePerMonth;

	void clearBuff();		 //清空缓存
	vector<json*> buffData;  //缓存数据
	string buffDataFilePath; //缓存数据的文件路径
	vector<string> m_vecKeys; //列头
};