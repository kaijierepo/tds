#include "pch.h"
#include "userMng.h"
#include "common/common.hpp"
#include "mo.h"

userManager userMng;
bool userManager::loadConf()
{
	m_userConfPath = tds->conf->projectConfPath + "/users/users.json";
	m_roleConfPath = tds->conf->projectConfPath + "/users/roles.json";
	m_uiConfPath = tds->conf->projectConfPath + "/users/ui.json";


	string sUsers, sRoles;

	if (!fs::readFile(m_userConfPath, sUsers))
	{
		return false;
	}
	try {
		if (sUsers != "")
			m_jUsers = json::parse(sUsers);

		std::unique_lock<shared_mutex> lock(m_csUserConf);
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

	
	if (!fs::readFile(m_roleConfPath, sRoles))
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
	if (!fs::readFile(m_uiConfPath, sUI))
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

bool userManager::saveConf()
{
	string s = m_jUsers.dump(4);
	fs::writeFile(m_userConfPath, s);
	return true;
}

bool userManager::checkLogin(string user, string pwd,json& userInfo)
{
	std::shared_lock<shared_mutex> lock(m_csUserConf);
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

bool userManager::rpc_changePwd(json& params, json& rlt, json& err)
{
	string error;
	string user = params["user"].get<string>();
	string oldPwd = params["oldPwd"].get<string>();
	string newPwd = params["newPwd"].get<string>();
	json jUser = getUser(user);
	string currentPwd = jUser["pwd"].get<string>();
	if (oldPwd == currentPwd)
	{
		if (m_mapUsers.find(user) != m_mapUsers.end())
		{
			json& userTmp = *m_mapUsers[user];
			userTmp["pwd"] = newPwd;
			saveConf();
			rlt = "ok";
		}
		else
		{
			err = "用户名不存在";
		}
	}
	else
	{
		err = "当前密码输入不正确";
	}

	return true;
}

json userManager::getRoles(string user)
{
	std::shared_lock<shared_mutex> lock(m_csUserConf);
	if (m_mapUsers.find(user) != m_mapUsers.end())
	{
		json jRet = json::array();
		json* pUser = m_mapUsers[user];
		json& jUser = *pUser;
		string role = jUser["role"].get<string>();

		for (int i = 0; i < m_jRoles.size(); i++)
		{
			json& jR = m_jRoles[i];

			//只有系统管理员才能够看到系统管理员角色
			if (jR["name"].get<string>() == "系统管理员")
			{
				if (role != "系统管理员")
				{
					continue;
				}
			}

			jRet.push_back(jR);
		}
		return jRet;
	}
	else
	{
		return m_jRoles;
	}
}

json userManager::getUsers(string user)
{
	json jRet = json::array();
	std::shared_lock<shared_mutex> lock(m_csUserConf);
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
	std::unique_lock<shared_mutex> lock(m_csUserConf);
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

	saveConf();

	return true;
}

bool userManager::setUser(json& user,json& result,json& err)
{
	std::unique_lock<shared_mutex> lock(m_csUserConf);
	
	string name = user["name"].get<string>();
	if (m_mapUsers.find(name) != m_mapUsers.end())
	{
		json& userTmp = *m_mapUsers[name];
		userTmp = user;
	}
	else
	{
		err = "用户名不存在";
		return false;
	}
	

	string s = m_jUsers.dump(4);
	fs::writeFile(m_userConfPath, s);

	return true;
}

json userManager::getMoPermission(string user)
{
	try {
		std::shared_lock<shared_mutex> lock(m_csUserConf);
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
	std::shared_lock<shared_mutex> lock(m_csUserConf);
	if (m_mapUsers.find(user) != m_mapUsers.end())
	{
		json& jUser = *m_mapUsers[user];
		return jUser;
	}

	return nullptr;
}


