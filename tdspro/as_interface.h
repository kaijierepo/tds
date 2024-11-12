#pragma once
#include <string>
#include "json.hpp"
using json = nlohmann::json;
using namespace std;

bool funcImp_obj_isEnableAlarm(std::string tag, std::string lang);
bool funcImp_obj_setJAlmStatus(string tag, string lang, json& js);
json funcImp_obj_getTypeTagByTag(string tag);


