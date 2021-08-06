#pragma once
#include "json.hpp"
#include "tdscore.h"
using namespace std;
using json = nlohmann::json;


class amo;
class MP;
class database;
class MO
{
public:
	MO();
	virtual ~MO();

	virtual bool loadConf(json& conf);
	bool toJson(json& conf, json params);

	void removeMp(json& mo);
	void clearChildren();

	string m_moType;
	string m_strName;
	string m_alias;
	bool m_bShow;


	MO* createChildMO(string subTag, string moType);
	void DeleteChildAMO(string& strName);
	void GetAllChildMO(std::vector<MO*>& aryMO, string type);

	MO* GetRootMO();
	MO* GetFatherMO(string type);//获得指定类型的父节点，或者是自身
	MO* GetChildMO(string type);
	MO* CopyMO();//复制一份与该mo相同的配置
	virtual MO& operator=(MO& right);

	std::vector<MO*> m_childMO;

	MO* m_pParentMO;
	MO* GetProjectMO();//获得当前设备所属的Project节点，MO树根节点

	virtual json getRT();

	virtual string getTag();
	vector<string> GetAlias();
	vector<string> GetAllTagNamePlus();
	virtual string getTagWithRoot();

	MO* GetMOByTag(string strTag);//在以自己为根节点的整颗书检索Tag,找到对应的CMO返回
	MP* GetMPByTag(string strTag);
	void GetMOByTag(std::vector<MO*>* tagVec, string strTag);
	void GetMPByTag(std::vector<MP*>* tagVec, string strTag);
	MP* GetMPByName(string strName);
	MO* GetMOByName(string strName);
	MO* GetMO(string strName);

	string TranslateRelateTag(string rtag);
	string m_status;

	//topo management
	vector<void*> m_vecIODev;//挂接的采集设备.此处暂时用void，防止依赖ioDev.h文件，导致不容易多工程复用。需再考虑更好的办法
	void updateDataLink();

	database* GetDB();
	string GetStatusSummary();//获得当前状态概要，用于在拓扑图上显示
	void GetAllChildAlarmInfo(string& strSummary);
	vector<string> getTagPartials(string strTag);
	string getTypeLabel(string type);

	static string AppendTagRoot(string& str);
	static string ResolveTag(string strTagExp, string strTagThis); //strTagContext指位号表达式所在mp的父mo的位号
	static string trimProperty(string& strTagExp);
};

MO* createMO(string type);

