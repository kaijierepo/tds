#include "pch.h"
#include "userMng.h"
#include "common/common.hpp"
#include "mo.h"

userManager userMng;
bool userManager::loadConf()
{
	string sUsers, sRoles;
	string userConf = tds->conf->projectConfPath + "/users/users.json";
	if (!fs::readFile(userConf, sUsers))
	{
		return false;
	}
	try {
		if (sUsers != "")
			m_jUsers = json::parse(sUsers);

		for (int i = 0; i < m_jUsers.size(); i++)
		{
			json& jOneUser = m_jUsers[i];
			USER_INFO ui;
			ui.name = jOneUser["name"].get<string>();
			//ui.pwd = jOneUser["pwd"].get<string>();
			//ui.org = jOneUser["org"].get<string>();
			//ui.enable = jOneUser["enable"].get<bool>();
			//ui.role = jOneUser["role"].get<string>();
			m_mapUsers[ui.name] = &jOneUser;
		}
	}
	catch (std::exception& e)
	{

	}


	if (!fs::readFile("conf/users/roles.json", sRoles))
	{
		return false;
	}
	try {
		if (sRoles != "")
			m_jRoles = json::parse(sRoles);
	}
	catch (std::exception& e)
	{

	}

	string sUI;
	if (!fs::readFile("conf/users/ui.json", sUI))
	{
		return false;
	}
	try {
		if (sUI != "")
			m_jUI = json::parse(sUI);
	}
	catch (std::exception& e)
	{

	}

	return true;
}

bool userManager::checkLogin(string user, string pwd,json& userInfo)
{
	if (m_mapUsers.find(user) != m_mapUsers.end())
	{
		json* pUser = m_mapUsers[user];
		json& jUser = *pUser;
		string truePwd = jUser["pwd"].get<string>();
		if (pwd == truePwd)
		{
			userInfo = jUser;
			string keyPwd = "pwd";
			userInfo.erase(keyPwd);

			//生成token
			string token = common::guid();
			userInfo["token"] = token;

			ACCESS_INFO ai;
			ai.age = 600;
			GetLocalTime(&ai.stCreate);
			ai.token = token;
			ai.user = user;

			m_mapAccessInfo[user] = ai;

			return true;
		}
			
	}

	return false;
}

bool userManager::logout(string user)
{
	m_mapAccessInfo.erase(user);
	return true;
}

bool userManager::checkToken(string user, string token)
{
	if (m_mapAccessInfo.find(user) == m_mapAccessInfo.end())
	{
		return false;
	}
	string trueToken = m_mapAccessInfo[user].token;
	if (trueToken != token)
	{
		return false;
	}
	return true;
}

bool userManager::checkTagPermission(string user, string tag)
{
	json jUser = userMng.getUser(user);
	if (jUser.is_null())return false; 
	tag = TAG::addRoot(tag);

	//用户所属组织
	string org = jUser["org"].get<string>();
	org = TAG::addRoot(org);

	//位号是否在用户所属的组织结构中，不在则一定没有权限
	if (tag.find(org) == string::npos)
		return false;

	//管理员级别默认拥有所有权限。简化操作，无需去设置管理员的权限
	if (jUser["role"].get<string>() == "管理员")
		return true;

	json moPermission = userMng.getMoPermission(user);
	if(moPermission.is_null())return false;
	//生成以用户所属组织为根节点的位号，不包含根节点。为空表示根节点，有权限
	tag = str::trimPrefix(tag,org);
	tag = str::trimPrefix(tag, ".");
	if (tag == "")
		return true;
	
	//检查权限树中是否有该位号
	return TAG::hasTag(moPermission, tag);
}

bool userManager::isChildMo(string parent, string child)
{
	if (child.find(parent) != string::npos)
	{
		return true;
	}
	return false;
}

json userManager::getRoles(string user)
{
	return m_jRoles;
}

json userManager::getUsers(string user)
{
	json jRet = json::array();
	if (m_mapUsers.find(user) != m_mapUsers.end())
	{
		json* pUser = m_mapUsers[user];
		json& jUser = *pUser;
		string role = jUser["role"].get<string>();
		if (role == "管理员")
		{
			string org = jUser["org"].get<string>();

			for (auto& i : m_mapUsers)
			{
				json& userTmp = *i.second;
				string orgTmp = userTmp["org"].get<string>();
				if (isChildMo(org,orgTmp))
				{
					jRet.push_back(userTmp);
				}
			}
		}
	} 
	return jRet;
}

bool userManager::setUsers(json& users)
{
	for (int i = 0; i < users.size(); i++)
	{
		json& oneUser = users[i];
		string name = oneUser["name"].get<string>();
		if (m_mapUsers.find(name) != m_mapUsers.end())
		{
			json& userTmp = *m_mapUsers[name];
			userTmp = oneUser;
		}
		else
		{
			m_jUsers.push_back(oneUser);
			m_mapUsers[name] = &m_jUsers[m_jUsers.size()-1];
		}
	}

	string s = m_jUsers.dump(4);
	fs::writeFile("conf/users/users.json", s);

	return true;
}

json userManager::getMoPermission(string user)
{
	try {
		if (m_mapUsers.find(user) != m_mapUsers.end())
		{
			json& jUser = *m_mapUsers[user];
			return jUser["permission"]["mo"];
		}
	}
	catch (std::exception& e)
	{
		return nullptr;
	}
}

json userManager::getUser(string user)
{
	if (m_mapUsers.find(user) != m_mapUsers.end())
	{
		json& jUser = *m_mapUsers[user];
		return jUser;
	}

	return nullptr;
}


