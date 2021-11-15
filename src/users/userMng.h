
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

class userManager {
public:
	bool loadConf();

	bool loginCheck(string user, string pwd);

	bool isChildMo(string parent, string child);

	json getUsers(string adminUser); //获得管理员用户拥有管理权限的用户列表
	bool setUsers(json& users);      //保存用户配置，不一定是全部。

	std::map<string, json> m_mapUsers;
	json m_jUsers;
	json m_jRoles;
};

extern userManager userMng;