
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

	bool checkLogin(string user, string pwd, json& userInfo);
	bool checkToken(string user, string token);
	bool checkTagPermission(string user, string tag); //检查用户对某一个位号是否有权限

	bool isChildMo(string parent, string child);

	json getRoles(string user);
	json getUsers(string user); //获得可以管理的用户列表
	json getMoPermission(string user); //获得可以管理的MO树
	json getUser(string user);

	bool setUsers(json& users);      //保存用户配置，不一定是全部。


	std::map<string, json*> m_mapUsers;
	json m_jUsers;
	json m_jRoles;
	json m_jUI;

	map<string, ACCESS_INFO> m_mapAccessInfo;
};

extern userManager userMng;