#pragma once
#include "httplib.h"
#include "json.hpp"
using json = nlohmann::json;

class FileWatcher {
public:
	FileWatcher();
	void run(const std::string dir_path);

	json m_jConf;
};

extern FileWatcher fileWatcher;