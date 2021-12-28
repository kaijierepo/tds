
#include "json.hpp"

using json = nlohmann::json;

struct USER_INFO {
	string name;
	string createTime;
	string role;
	string pwd;
	string org;
	bool enable;
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

	//保存用户配置，如果已经存在则更新。如果不存在则添加。不一定是全部。相当于是merge操作
	bool setUsers(json& users);
	//设置单个用户，必须已经存在，否则设置失败
	bool setUser(json& user, json& result, json& err);
	
	
	//map和json共用相同的数据内存对象。
	std::map<string, json*> m_mapUsers;
	json m_jUsers;

	std::shared_mutex m_csUserConf;
	
	json m_jRoles;
	json m_jUI;

	string m_userConfPath;
	string m_roleConfPath;
	string m_uiConfPath;

	map<string, ACCESS_INFO> m_mapAccessInfo;
};

extern userManager userMng;