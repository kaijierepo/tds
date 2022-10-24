#include "pch.h"
#include "userMng.h"
#include "common/common.h"
#include "obj.h"
#include "logger.h"
#define PBKDF2_SHA256_IMPLEMENTATION
#include "pbkdf2_sha256.h"

userManager userMng;

string getDefaultRoleConf() {
	return R"(
[
     {
	"name":"系统管理员",
	"createTime": "2021-11-22 00:00:00",
 	"permission":[
   {
       "name" : "项目组态"
   },{
       "name" : "监控视图"
   },{
       "name" : "实时状态"
   },{
       "name" : "历史数据"
   },{
       "name" : "报警管理"
   },{
       "name" : "用户管理"
   }
]
      },
     {
	"name":"管理员",
	"createTime": "2021-11-22 00:00:00",
 	"permission":[
{
       "name" : "监控视图"
   },{
       "name" : "实时状态"
   },{
       "name" : "历史数据"
   },{
       "name" : "报警管理"
   },{
       "name" : "用户管理"
   }
]
      },
      {
	"name":"操作员",
	"createTime": "2021-11-22 00:00:00",
 	"permission":[
{
       "name" : "监控视图"
   },{
       "name" : "实时状态"
   },{
       "name" : "历史数据"
   },{
       "name" : "报警管理"
   }
]
      },{
	"name":"观察员",
	"createTime": "2021-11-22 00:00:00",
 	"permission":[
{
       "name" : "监控视图"
   },{
       "name" : "实时状态"
   },{
       "name" : "历史数据"
   },{
       "name" : "报警管理"
   }
]
      }
]
)";
}


string getDefaultUIConf() {
	return R"(
[
   {
       "name" : "项目组态"
   },{
       "name" : "监控视图"
   },{
       "name" : "实时状态"
   },{
       "name" : "历史数据"
   },{
       "name" : "报警管理"
   },{
       "name" : "用户管理"
   }
]
)";
}

string getDefaultUserConf() {
	return R"(
[
    {
        "createTime": "2021-10-10 21:11:12",
        "enable": true,
        "name": "admin",
        "org": "",
        "permission": null,
        "pwd": "123",
        "role": "管理员"
    }
]
			)";
}

bool userManager::loadConf()
{
	m_userConfPath = tds->conf->confPath + "/users/users.json";
	m_roleConfPath = tds->conf->confPath + "/users/roles.json";
	m_uiConfPath = tds->conf->confPath + "/users/ui.json";
	m_tokenConfPath = tds->conf->confPath + "/users/tokens.json";
	m_tokenDynamicPath = tds->conf->dbPath + "/tokens.json";

	if (!fs::fileExist(m_userConfPath))
	{
		string s = getDefaultUserConf();
		fs::writeFile(m_userConfPath, s);
	}

	if (!fs::fileExist(m_roleConfPath))
	{
		string s = getDefaultRoleConf();
		fs::writeFile(m_roleConfPath, s);
	}

	if (!fs::fileExist(m_uiConfPath))
	{
		string s = getDefaultUIConf();
		fs::writeFile(m_uiConfPath, s);
	}


	string sUsers, sRoles;

	fs::readFile(m_userConfPath, sUsers);
	try {
		if (sUsers != "")
		{
			json jUsers = json::parse(sUsers);
			std::unique_lock<shared_mutex> lock(m_csUserConf);
			for (int i = 0; i < jUsers.size(); i++)
			{
				json& jOneUser = jUsers[i];
				string name = jOneUser["name"].get<string>();
				m_mapUsers[name] = jOneUser;
			}
		}
	}
	catch (std::exception& e)
	{
		LOG("[error]加载用户配置失败,error=%s", e.what());
	}

	//如果没有配置，添加一个默认的admin用户，密码123

	fs::readFile(m_roleConfPath, sRoles);
	try {
		if (sRoles != "")
			m_jRoles = json::parse(sRoles);
	}
	catch (std::exception& e)
	{

	}

	string sUI;
	fs::readFile(m_uiConfPath, sUI);
	try {
		if (sUI != "")
			m_jUI = json::parse(sUI);
	}
	catch (std::exception& e)
	{

	}


	//动态token
	string sTokensDynamic;
	fs::readFile(m_tokenDynamicPath, sTokensDynamic);
	try {
		json jTokens;
		if (sTokensDynamic != "")
			jTokens = json::parse(sTokensDynamic);

		for (auto& i : jTokens) {
			ACCESS_INFO ai;
			ai.age = tds->conf->tokenExpireTime*60;
			ai.token = i["token"].get<string>();
			ai.user = i["user"].get<string>();
			ai.stCreate = timeopt::str2st(i["createTime"].get<string>());
			ai.bDynamic = true;
			m_csAccessToken.lock();
			m_mapAccessInfo[ai.token] = ai;
			m_csAccessToken.unlock();
		}
	}
	catch (std::exception& e)
	{

	}

	//静态token
	string sTokens;
	fs::readFile(m_tokenConfPath, sTokens);
	try {
		if (sTokens != "")
			m_jTokens = json::parse(sTokens);

		for (auto& i : m_jTokens) {
			ACCESS_INFO ai;
			ai.age = i["age"].get<int>();
			ai.token = i["token"].get<string>();
			ai.user = i["user"].get<string>();
			ai.bDynamic = false;
			m_csAccessToken.lock();
			m_mapAccessInfo[ai.token] = ai;
			m_csAccessToken.unlock();
		}
	}
	catch (std::exception& e)
	{

	}

	return true;
}

bool userManager::saveConf()
{
	std::shared_lock<shared_mutex> lock(m_csUserConf);
	json jUsers;
	for (auto& i : m_mapUsers)
	{
		jUsers.push_back(i.second);
	}
	string s = jUsers.dump(4);
	fs::writeFile(m_userConfPath, s);
	return true;
}

bool userManager::init()
{
	loadConf();
	return true;
}

void tokenExpire_thread(userManager* p) {
	while (1) {
		Sleep(10000);
		p->m_csAccessToken.lock();
		vector<ACCESS_INFO> tokenList;
		for (auto& i : p->m_mapAccessInfo) {
			if (i.second.isExpired()) {
				tokenList.push_back(i.second);
			}
		}

		for (int i = 0; i < tokenList.size(); i++) {
			ACCESS_INFO ai = tokenList[i];
			p->m_mapAccessInfo.erase(ai.token);
			LOG("[warn]Token过期,token=%s,user=%s,过期时间=%d秒", ai.token.c_str(), ai.user.c_str(),ai.age);
		}
		p->m_csAccessToken.unlock();
		p->saveTokens();
	}
}

bool userManager::run()
{
	if (tds->conf->enableAccessCtrl) {
		thread t(tokenExpire_thread, this);
		t.detach();
	}
	return true;
}

bool userManager::checkLogin(string user, string pwd,json& userInfo)
{
	
	return false;
}

bool userManager::checkToken(string user, string token)
{
	if (tds->conf->testToken != "" && tds->conf->testToken == token)
		return true;

	std::unique_lock<mutex> lock(m_csAccessToken);
	if (m_mapAccessInfo.find(token) == m_mapAccessInfo.end())
	{
		return false;
	}
	ACCESS_INFO ai = m_mapAccessInfo[token];
	if (ai.user != user)
		return false;
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
			json& userTmp = m_mapUsers[user];
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
		json& jUser = m_mapUsers[user];
		string role = jUser["role"].get<string>();
		string name = jUser["name"].get<string>();

		for (int i = 0; i < m_jRoles.size(); i++)
		{
			json& jR = m_jRoles[i];

			//只有系统管理员才能够看到系统管理员角色
			if (jR["name"].get<string>() == "系统管理员")
			{
				if (role != "系统管理员" && name != "admin")
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

json userManager::rpc_getUsers(json params, RPC_RESP& resp, RPC_SESSION session)
{
	string user = session.user;
	json jRet = json::array();
	std::shared_lock<shared_mutex> lock(m_csUserConf);
	if (m_mapUsers.find(user) != m_mapUsers.end())
	{
		json& jUser = m_mapUsers[user];
		string role = jUser["role"].get<string>();
		if (role == "管理员" || role == "系统管理员")
		{
			string org = jUser["org"].get<string>();

			for (auto& i : m_mapUsers)
			{
				json userTmp = i.second;
				string orgTmp = userTmp["org"].get<string>();
				if (isChildMo(org,orgTmp))
				{
					//当管理员用户查看 所辖范围内的用户时
					//用户的所属组织结构位号为管理员的用户位号。非系统位号。
					string userTag = TAG::sysTag2userTag(orgTmp, org);
					userTmp["org"] = userTag;
					jRet.push_back(userTmp);
				}
			}
		}
	} 
	return jRet;
}

bool userManager::rpc_setUsers(json params, RPC_RESP& resp, RPC_SESSION session)
{
	json users = json::array();
	if (params.is_object())
		users.push_back(params);
	else
		users = params;

	m_csUserConf.lock();
	for (int i = 0; i < users.size(); i++)
	{
		json& oneUser = users[i];

		//组织结构的用户位号转为系统位号
		//浙江省.杭州市.拱墅区
		//所属杭州市的管理员用户配置一个拱墅区的用户时，指定的用户组织结构为用户位号  “拱墅区”
		//管理员用户组织结构为 浙江省.杭州市
		//因此指定用户的组织结构的系统位号为   “浙江省.杭州市.拱墅区”
		string org = oneUser["org"].get<string>();
		org = TAG::addRoot(org, session.org);
		oneUser["org"] = org;

		string name = oneUser["name"].get<string>();
		if (m_mapUsers.find(name) != m_mapUsers.end())
		{
			json& userTmp = m_mapUsers[name];
			userTmp = oneUser;
		}
		else
		{
			m_mapUsers[name] = oneUser;
		}
	}
	m_csUserConf.unlock();

	saveConf();

	return true;
}

bool userManager::rpc_addUser(json params, RPC_RESP& resp, RPC_SESSION session)
{
	json users = json::array();
	users.push_back(params);
	return rpc_setUsers(users,resp,session);
}

bool userManager::rpc_updateToken(json params, RPC_RESP& resp, RPC_SESSION session)
{
	bool changed = false;
	m_csAccessToken.lock();
	if (m_mapAccessInfo.find(session.token)!= m_mapAccessInfo.end()) {
		ACCESS_INFO& ai = m_mapAccessInfo[session.token];
		GetLocalTime(&ai.stCreate);
		ai.age = 5; //老的 Token 继续维护5秒的生存期。 使得客户端再收到新token前使用老token发的命令仍能被执行

		ACCESS_INFO aiTmp;
		aiTmp.user = ai.user;
		aiTmp.token = common::guid();
		aiTmp.bDynamic = true;
		aiTmp.age = tds->conf->tokenExpireTime * 60;
		GetLocalTime(&aiTmp.stCreate);
		m_mapAccessInfo[aiTmp.token] = aiTmp;
		json rlt;
		rlt["token"] = aiTmp.token;
		rlt["tokenExpire"] = aiTmp.age;
		resp.result = rlt.dump();
		changed = true;
	}
	else
	{
		resp.error = makeRPCError(RPC_ERROR_CODE::AUTH_tokenError, "invalid token");
	}
	m_csAccessToken.unlock();

	if (changed)
	{
		saveTokens();
	}
	return true;
}


bool userManager::setUser(json& user,json& result,json& err)
{
	{
		std::unique_lock<shared_mutex> lock(m_csUserConf);
		string name = user["name"].get<string>();
		if (m_mapUsers.find(name) != m_mapUsers.end())
		{
			json& userTmp = m_mapUsers[name];
			userTmp = user;
		}
		else
		{
			err = "用户名不存在";
			return false;
		}
	}
	
	saveConf();
	return true;
}

vector<USER_INFO> userManager::getRelateUsers(string tag)
{
	vector<USER_INFO> vec;
	try {
		std::shared_lock<shared_mutex> lock(m_csUserConf);

		for (auto& i : m_mapUsers)
		{
			json& jUser = i.second;
			string org = jUser["org"].get<string>();
			if (tag.find(org) != string::npos)
			{
				USER_INFO ui;
				ui.fromJson(jUser);
				vec.push_back(ui);
			}
		}
	}
	catch (std::exception& e)
	{

	}

	return vec;
}

string userManager::getPhoneList(vector<USER_INFO>& users)
{
	string pl;

	for (int i = 0; i < users.size(); i++)
	{
		USER_INFO& ui = users[i];
		if (ui.phone != "")
		{
			if (pl != "") pl += ",";
			pl += ui.phone;
		}
	}

	return pl;
}

json userManager::getMoPermission(string user)
{
	try {
		std::shared_lock<shared_mutex> lock(m_csUserConf);
		if (m_mapUsers.find(user) != m_mapUsers.end())
		{
			json& jUser = m_mapUsers[user];
			return jUser["permission"]["mo"];
		}
		else {
			return nullptr;
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
		json& jUser = m_mapUsers[user];
		return jUser;
	}

	return nullptr;
}

json userManager::getUserByOpenID(string openID)
{
	json j = nullptr;
	std::shared_lock<shared_mutex> lock(m_csUserConf);
	for (auto& i : m_mapUsers)
	{
		json& jUser = i.second;
		if (jUser.contains("gzhOpenID"))
		{
			string tmp = jUser["gzhOpenID"].get<string>();
			if (openID == tmp)
				return jUser;
		}
	}
	return j;
}

bool userManager::saveTokens()
{
	m_csAccessToken.lock();
	json jTokens = json::array();
	for (auto& i : m_mapAccessInfo) {
		json j;
		j["age"] = i.second.age;
		j["user"] = i.second.user;
		j["token"] = i.second.token;
		j["createTime"] = timeopt::st2str(i.second.stCreate);
		jTokens.push_back(j);
	}
	m_csAccessToken.unlock();
	string s = jTokens.dump(2);
	fs::writeFile(m_tokenDynamicPath, s);
	return false;
}

void userManager::rpc_deleteUser(json params, RPC_RESP& resp, RPC_SESSION session)
{
	string name = params["name"].get<string>();
	std::shared_lock<shared_mutex> lock(m_csUserConf);
	m_mapUsers.erase(name);
	saveConf();
	resp.result = "\"ok\"";
}


void userManager::rpc_login(json params, RPC_RESP& resp, RPC_SESSION session)
{
	try {
		string user = params["user"].get<string>();
		string pwd = "";
		string sign = "";
		if(params.contains("pwd"))
			pwd = params["pwd"].get<string>();
		if (params.contains("sign"))
			sign = params["sign"].get<string>();

		if (pwd == "" && sign == "") {
			resp.error = makeRPCError(RPC_ERROR_CODE::AUTH_signatureMissing, "param sign must be specified to login.use HMAC-SHA256 to generate signature");
			goto LOGIN_END;
		}

		bool loginSuccess = false;
		json userInfo;
		std::shared_lock<shared_mutex> lock(m_csUserConf);
		if (m_mapUsers.find(user) != m_mapUsers.end())
		{
			json& jUser = m_mapUsers[user];

			//校验密码或者签名
			if (pwd != "")
			{
				string truePwd = jUser["pwd"].get<string>();
				if (pwd == truePwd)
				{
					loginSuccess = true;
				}
				else {
					resp.error = makeRPCError(RPC_ERROR_CODE::AUTH_passwordError, "password error", "密码错误");
					goto LOGIN_END;
				}
			}
			else {
				string truePwd = jUser["pwd"].get<string>();
				string time = "";
				if (params.contains("time"))
					time = params["time"].get<string>();
				else {
					resp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "param time must be specified to login");
					goto LOGIN_END;
				}

				string msg = user + time;
				uint8_t out[SHA256_DIGESTLEN] = { 0 };
				hmac_sha256_calc(out, (uint8_t*)msg.data(), msg.length(), (uint8_t*)truePwd.data(), truePwd.length());
				string trueSign = str::bytesToHexStr((char*)out, SHA256_DIGESTLEN, "");

				if (trueSign == sign)
				{
					loginSuccess = true;
				}
				else {
					resp.error = makeRPCError(RPC_ERROR_CODE::AUTH_signatureInvalid, "invalid signature");
					goto LOGIN_END;
				}
			}

			//校验成功
			if (loginSuccess) {
				userInfo = jUser;
				string keyPwd = "pwd";
				userInfo.erase(keyPwd);
				userInfo.erase("createTime");
				userInfo.erase("enable");
				//生成token
				string token = common::guid();
				userInfo["token"] = token;
				ACCESS_INFO ai;
				ai.age = tds->conf->tokenExpireTime * 60;
				GetLocalTime(&ai.stCreate);
				ai.token = token;
				ai.user = user;
				ai.bDynamic = true;
				userInfo["tokenExpire"] = ai.age;
				m_csAccessToken.lock();
				m_mapAccessInfo[token] = ai;
				m_csAccessToken.unlock();
				saveTokens();
				resp.result = userInfo.dump(4);
			}
			else {

			}
		}
		else {
			resp.error = makeRPCError(RPC_ERROR_CODE::AUTH_passwordError, "user not found","用户不存在");
		}
	}
	catch (std::exception& e)
	{
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "request data error");
	}

LOGIN_END:

	if (resp.error != "")
	{
		LOG("[warn]login fail,info:%s,error:%s", params.dump().c_str(), resp.error.c_str());
	}
	else {
		LOG("[keyinfo]login success,info:%s,result:%s", params.dump().c_str(), resp.result.c_str());
	}
}

void userManager::rpc_logout(json params, RPC_RESP& resp, RPC_SESSION session)
{
	string token;
	if (params.contains("token")) {
		token = params["token"].get<string>();
		m_csAccessToken.lock();
		m_mapAccessInfo.erase(token);
		m_csAccessToken.unlock();
		saveTokens();
		resp.result = "\"ok\"";
	}
	else {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param token");
	}
}

