#include "pch.h"
#include "userMng.h"
#include "common/common.hpp"

userManager userMng;
bool userManager::loadConf()
{
	string s;
	if (!fs::readFile("conf/users.json", s))
	{
		return false;
	}

	try {
		json j = json::parse(s);
		m_jUsers = j["users"];
		m_jRoles = j["roles"];

		for (int i = 0; i < m_jUsers.size(); i++)
		{
			json jOneUser = m_jUsers[i];
			USER_INFO ui;
			ui.name = jOneUser["name"].get<string>();
			ui.pwd = jOneUser["pwd"].get<string>();
			ui.org = jOneUser["org"].get<string>();
			ui.enable = jOneUser["enable"].get<bool>();
			ui.role = jOneUser["role"].get<string>();
			m_mapUsers[ui.name ] = jOneUser;
		}
	}
	catch (std::exception& e)
	{

	}

	return false;
}

bool userManager::loginCheck(string user, string pwd)
{
	if (m_mapUsers.find(user) != m_mapUsers.end())
	{
		json& jUser = m_mapUsers[user];
		string truePwd = jUser["pwd"].get<string>();
		if (pwd == truePwd)
			return true;
	}

	return false;
}

bool userManager::isChildMo(string parent, string child)
{
	if (child.find(parent) != string::npos)
	{
		return true;
	}
	return false;
}

json userManager::getUsers(string adminUser)
{
	json jRet = json::array();
	if (m_mapUsers.find(adminUser) != m_mapUsers.end())
	{
		json& jUser = m_mapUsers[adminUser];
		string role = jUser["role"].get<string>();
		if (role == "管理员")
		{
			string org = jUser["org"].get<string>();

			for (auto& i : m_mapUsers)
			{
				string orgTmp = i.second["org"].get<string>();
				if (isChildMo(org,orgTmp))
				{
					jRet.push_back(i.second);
				}
			}
		}
	} 
	return jRet;
}

bool userManager::setUsers(json& users)
{
	return false;
}


