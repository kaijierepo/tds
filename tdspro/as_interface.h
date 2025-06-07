#pragma once
#include <string>
#include "json.hpp"
using json = nlohmann::json;
using namespace std;

bool funcImp_obj_setJAlmStatus(string tag, string lang, json& js);
json funcImp_obj_getTypeTagByTag(string tag);
void funcImp_log(const char* pszFmt, ...);
bool funcImp_rpcHand_notify(string method, json& js);
bool funcImp_sms_notify(string tag, string& msg);
bool funcImp_usrMng_checkTagPermission(string user, string tag);
