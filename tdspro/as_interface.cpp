#include "as_interface.h"
#include <prj.h>
#include "logger.h"
#include "rpcHandler.h"
#include "userMng.h"
#include <cstdarg>

bool funcImp_obj_isEnableAlarm(string tag, string lang)
{
	OBJ* pObj = prj.queryObj(tag, lang);
	if (pObj) {
		return pObj->m_bEnableAlarm;
	}
	return false; //不存在也算作不使能
}

bool funcImp_obj_setJAlmStatus(string tag, string lang, json & js)
{
	OBJ* pmo = prj.queryObj(tag, lang);
	if (pmo)
	{
		pmo->m_jAlarmStatus = js;  
		return true;
	}
	return false;
}

json funcImp_obj_getTypeTagByTag(string tag)
{
	json jTypeTag = prj.getTypeTagByTag(tag);
	return jTypeTag;
}

void funcImp_log(const char* pszFmt, ...)
{
	std::string str;
	va_list args;
	va_start(args, pszFmt); {
		int nLength = _vscprintf_cross_log(pszFmt, args);
		nLength += 1;  //上面返回的长度是包含\0，这里加上
		std::vector<char> vectorChars(nLength);
		vsnprintf(vectorChars.data(), nLength, pszFmt, args);
		str.assign(vectorChars.data());
	}
	va_end(args);

	LOG(str);
}

bool funcImp_rpcHand_notify(string method,  json& js)
{
	rpcSrv.notify(method, js);

	return true;
}

bool funcImp_sms_notify(string tag, string & msg)
{
	vector<USER_INFO> relateUsers = userMng.getRelateUsers(tag);
	string pl, pnl;

	for (int i = 0; i < relateUsers.size(); i++)
	{
		USER_INFO& ui = relateUsers[i];
		if (ui.phone != "")
		{
			if (pl != "") pl += ",";
			pl += ui.phone;

			if (pnl != "") pnl += ";";
			pnl += ui.name + "," + ui.phone;
		}
	}

	if (pl != "" && tds->smsServer)
	{
		if (tds->smsServer->send(msg, pl))
		{
			LOG("[报警短信通知]报警:" + msg + ",通知人:" + pnl);
		}

	}

	return true;
}

bool funcImp_usrMng_checkTagPermission(string user, string tag)
{
	return userMng.checkTagPermission(user, tag);
}

