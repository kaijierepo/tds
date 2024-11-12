#include "as_interface.h"
#include <prj.h>
//#include "logger.h"
#include "rpcHandler.h"

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

//void funcImp_log(const char* fmt, ...)
//{
//	LOG(fmt,);
//}

bool funcImp_rpcHand_notify(string method,  json& js)
{
	rpcSrv.notify(method, js);

	return true;
}

