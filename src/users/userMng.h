
#include "json.hpp"
#include "tdsSession.h"

using json = nlohmann::json;

class USER_INFO {
public:
	string name;
	string createTime;
	string role;
	string pwd;
	string org;
	string phone;
	bool enable;

	void fromJson(json& j) {
		if (j.contains("name")) name = j["name"].get<string>();
		if (j.contains("createTime")) createTime = j["createTime"].get<string>();
		if (j.contains("role")) role = j["role"].get<string>();
		if (j.contains("pwd")) pwd = j["pwd"].get<string>();
		if (j.contains("org")) org = j["org"].get<string>();
		if (j.contains("phone")) phone = j["phone"].get<string>();
		if (j.contains("enable")) enable = j["enable"].get<bool>();
	}

	void toJson(json& j) {
		j["name"] = name;
		j["createTime"] = createTime;
		j["role"] = role;
		j["pwd"] = pwd;
		j["org"] = org;
		j["phone"] = phone;
		j["enable"] = enable;
	}
};


struct ACCESS_INFO {
	string user;
	string token;
	SYSTEMTIME stCreate;
	int age; //秒为单位，过期时间
};

class userManager {
public:
	bool loadConf();
	bool saveConf();

	bool checkLogin(string user, string pwd, json& userInfo);
	bool logout(string user);
	bool checkToken(string user, string token);
	bool checkTagPermission(string user, string tag); //检查用户对某一个位号是否有权限

	bool isChildMo(string parent, string child);
	bool rpc_changePwd(json& params, json& rlt, json& err);

	json getRoles(string user);
	json getUsers(string user); //获得可以管理的用户列表
	json getMoPermission(string user); //获得可以管理的MO树
	json getUser(string user);
	json getUserByOpenID(string openID); //公众号的openID
	void rpc_deleteUser(json params, RPC_RESP& resp, RPC_SESSION session);

	//保存用户配置，如果已经存在则更新。如果不存在则添加。不一定是全部。相当于是merge操作
	bool setUsers(json& users);
	bool addUser(json& user);
	//设置单个用户，必须已经存在，否则设置失败
	bool setUser(json& user, json& result, json& err);
	
	vector<USER_INFO> getRelateUsers(string tag); //获取与该位号关联的用户的电话号码。 关联表示该位号在该用户的所属组织结构内
	string getPhoneList(vector<USER_INFO>& users);

	//map和json共用相同的数据内存对象。
	std::map<string, json> m_mapUsers;
	std::shared_mutex m_csUserConf;
	
	json m_jRoles;
	json m_jUI;

	string m_userConfPath;
	string m_roleConfPath;
	string m_uiConfPath;

	map<string, ACCESS_INFO> m_mapAccessInfo;
};

extern userManager userMng;