#pragma once
#include "json.hpp"
#include "tdscore.h"
using namespace std;


class amo;
class mp;
class database;
class mo
{
public:
	mo();
	virtual ~mo();

	virtual bool loadConf(json& conf);
	bool saveConf(json& conf,string opt = "");

	void removeMp(json& mo);

	string m_moType;
	string m_strName;
	string m_alias;
	bool m_bShow;

	void DeleteChildAMO(string& strName);
	void GetAllChildMO(std::vector<mo*>& aryMO, string type);

	mo* GetRootMO();
	mo* GetFatherMO(string type);//获得指定类型的父节点，或者是自身
	mo* GetChildMO(string type);
	mo* CopyMO();//复制一份与该mo相同的配置
	virtual mo& operator=(mo& right);

	std::vector<mo*> m_childMO;

	mo* m_pParentMO;
	mo* GetProjectMO();//获得当前设备所属的Project节点，MO树根节点

	virtual json getRT();

	virtual string getTag();
	vector<string> GetAlias();
	vector<string> GetAllTagNamePlus();
	virtual string getTagWithRoot();

	mo* GetMOByTag(string strTag);//在以自己为根节点的整颗书检索Tag,找到对应的CMO返回
	mp* GetMPByTag(string strTag);
	void GetMOByTag(std::vector<mo*>* tagVec, string strTag);
	void GetMPByTag(std::vector<mp*>* tagVec, string strTag);
	mp* GetMPByName(string strName);
	mo* GetMOByName(string strName);
	mo* GetMO(string strName);

	string TranslateRelateTag(string rtag);
	string m_status;

	//topo management
	vector<void*> m_vecIODev;//挂接的采集设备.此处暂时用void，防止依赖ioDev.h文件，导致不容易多工程复用。需再考虑更好的办法
	void updateDataLink();

	database* GetDB();
	string GetStatusSummary();//获得当前状态概要，用于在拓扑图上显示
	static void GetAllChildAlarmInfo(mo* pMO, string& strSummary);
	vector<string> getTagPartials(string strTag);
	string getTypeLabel(string type);

	static string AppendTagRoot(string& str);
	static string ResolveTag(string strTagExp, string strTagThis); //strTagContext指位号表达式所在mp的父mo的位号
	static string trimProperty(string& strTagExp);
};

mo* createMO(string type);

