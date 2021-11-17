
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

	bool checkLogin(string user, string pwd, json& userInfo);
	bool checkTagPermission(string user, string tag); //检查用户对某一个位号是否有权限

	bool isChildMo(string parent, string child);


	json getUsers(string user); //获得可以管理的用户列表
	json getMoPermission(string user); //获得可以管理的MO树
	json getUser(string user);

	bool setUsers(json& users);      //保存用户配置，不一定是全部。


	std::map<string, json*> m_mapUsers;
	json m_jUsers;
	json m_jRoles;
};

extern userManager userMng;