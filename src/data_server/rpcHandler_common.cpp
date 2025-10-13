#include "rpcHandler_common.h"
#include "common.h"
#include "tdb.h"
#include "base64.h"
#include <fstream>

RpcHandler_common rpcHandler_common;
map<string, string> g_mapConfFile;

bool RpcHandler_common::handleRpc(const string& method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	bool handled = true;

	if (method == "fs.readFile") {
		if (params["type"] != nullptr && params["type"].get<string>() == "binary") {
			char* p = NULL; int len = 0;
			if (DB_FS::readFile(params["path"].get<string>(), p, len)) {
			}
		}
		else {
			string path = params["path"].get<string>();

			string rootPath = "";
			string rootType;

			if (params["root"].is_string()) {
				rootType = params["root"].get<string>();
			}

			if (rootType == "fms") {
				rootPath = m_fmsPath;
			}
			else if (rootType == "conf") {
				rootPath = m_confPath;
			}
			else if (rootType == "db") {
				rootPath = m_dbPath;
			}
			else if (rootType == "app") {
				rootPath = m_appPath;
			}

			path = rootPath + "/" + path;

			string s;
			if (DB_FS::readFile(path, s)) {
				json j = s;
				rpcResp.result = j.dump();
			}
			else {
				if (!TDB::fileExist(params["path"])) {
					rpcResp.error = makeRPCError(OS_fileNotExist, "file not exist");
				}
				else {
					rpcResp.error = makeRPCError(TEC_FAIL, "fail");
				}
			}
		}
	}
	else if (method == "fs.deleteFile") {
		string path = params["path"].get<string>();

		string rootPath = "";
		string rootType;

		if (params["root"].is_string()) {
			rootType = params["root"].get<string>();
		}

		if (rootType == "fms") {
			rootPath = m_fmsPath;
		}
		else if (rootType == "conf") {
			rootPath = m_confPath;
		}
		else if (rootType == "db") {
			rootPath = m_dbPath;
		}
		else if (rootType == "app") {
			rootPath = m_appPath;
		}

		path = rootPath + "/" + path;

		if (DB_FS::deleteFile(path)) {
			rpcResp.result = "\"ok\"";
		}
		else {
			rpcResp.error = makeRPCError(TEC_FAIL, "fail");
		}
	}
	else if (method == "fs.writeFile") {
		string path = params["path"].get<string>();

		string rootPath = "";
		string rootType;

		if (params["root"].is_string()) {
			rootType = params["root"].get<string>();
		}

		if (rootType == "fms") {
			rootPath = m_fmsPath;
		}
		else if (rootType == "conf") {
			rootPath = m_confPath;
		}
		else if (rootType == "db") {
			rootPath = m_dbPath;
		}
		else if (rootType == "app") {
			rootPath = m_appPath;
		}

		path = rootPath + "/" + path;
		DB_FS::createFolderOfPath(path);

		if (params["data"] != nullptr) {
			string d = params["data"].get<string>();

			string encode = "";
			if (params.contains("encode")) {
				encode = params["encode"].get<string>();
			}

			if (encode == "base64") {
				unsigned char* out = new unsigned char[d.length()];
				int len = base64_decode(d.c_str(), (int)d.length(), out);

				if (DB_FS::writeFile(path, (char*)out, len)) {
					rpcResp.result = "\"ok\"";
				}
				else {
					rpcResp.error = makeRPCError(TEC_FAIL, "fail");
				}

				delete[] out;
			}
			else {
				string ap = fs::toAbsolutePath(path);
				if (DB_FS::writeFile(ap, d)) {
					rpcResp.result = "\"ok\"";
				}
				else {
					rpcResp.error = makeRPCError(TEC_FAIL, "fail");
				}
			}
		}
	}
#ifdef _WIN32
	else if (method == "fs.getCurDir") {
		WCHAR buff[300] = { 0 };
		GetCurrentDirectoryW(300, buff);

		wstring s = buff;
		json j = DB_STR::utf16_to_utf8(s);

		rpcResp.result = j.dump();
	}
#endif
	else if (method == "fs.getFileList") {
		string path = params["path"];

		bool includeFolder = false;
		bool recursive = false;

		if (params["includeFolder"] != nullptr) {
			includeFolder = params["includeFolder"].get<bool>();
		}

		if (params["recursive"] != nullptr) {
			recursive = params["recursive"].get<bool>();
		}

		vector<string> fl;
		DB_FS::getFileList(fl, path, includeFolder, recursive);

		json j = fl;
		rpcResp.result = j.dump();
	}
	else if (method == "fs.exploreFolder") {
		string root = "";
		if (params["root"].is_string()) {
			root = params["root"].get<string>();
		}

		vector<DB_FS::FILE_INFO> fileList;
		vector<DB_FS::FILE_INFO> folderList;

		string path;
		if (root == "fms") {
			path = m_fmsPath;
		}
		else if (root == "conf") {
			path = m_confPath;
		}
		else if (root == "db") {
			path = m_dbPath;
		}
		else if (root == "app") {
			path = m_appPath;
		}

		if (path == "") {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "file service is not started,config fmsPath param in tds.ini");
			return true;
		}

		path = fs::toAbsolutePath(path);

		if (params.contains("path")) {
			string subPath = params["path"];
			path = path + "/" + subPath;
		}

		DB_FS::getFileList(fileList, path);
		DB_FS::getFolderList(folderList, path);

		json infoList = json::array();
		for (int i = 0; i < folderList.size(); i++) {
			DB_FS::FILE_INFO& fi = folderList[i];

			json j;
			j["name"]       = fi.name;
			j["size"]       = fi.len;
			j["modifyTime"] = fi.modifyTime;
			j["isFolder"]   = true;

			infoList.push_back(j);
		}

		for (int i = 0; i < fileList.size(); i++) {
			DB_FS::FILE_INFO& fi = fileList[i];

			json j;
			j["name"]       = fi.name;
			j["size"]       = fi.len;
			j["modifyTime"] = fi.modifyTime;
			j["isFolder"]   = false;

			infoList.push_back(j);
		}

		rpcResp.result = infoList.dump();
	}
	else if (method == "getConfFile") {
		rpc_getconffile(params, rpcResp, session);
	}
	else if (method == "setConfFile") {
		rpc_setconffile(params, rpcResp, session);
	}
	else {
		handled = false;
	}

	return handled;
}

static std::string base64_decode(const std::string& in) {
	std::string out;
	std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	int val = 0, valb = -8;
	for (unsigned char c : in) {
		if (c == '=') break;
		if (base64_chars.find(c) == std::string::npos) break;
		val = (val << 6) + base64_chars.find(c);
		valb += 6;
		if (valb >= 0) {
			out.push_back(char((val >> valb) & 0xFF));
			valb -= 8;
		}
	}
	return out;
}

void RpcHandler_common::rpc_getconffile(json params, RPC_RESP& resp, RPC_SESSION& session) {
	string p = "";
	if (!params["path"].is_string()) {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param path error");
		return;
	}

	p = params["path"].get<string>();
	if (p != "") {
		string conf = "";
		p = m_confPath + "/" + p;

		DB_FS::normalizationPath(p);
		DB_FS::readFile(p, conf);

		json j;
		j["path"] = p;
		j["data"] = conf;
		resp.result = j.dump();
	}
	else {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "path not specified");
	}
}

void RpcHandler_common::rpc_setconffile(json params, RPC_RESP& resp, RPC_SESSION& session) {
	string path = "";
	string encode = "";
	if (!params["path"].is_string()) {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param path error");
		return;
	}

	if (!params["data"].is_string()) {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param data error");
		return;
	}

	if (params["encode"].is_string()) {
		encode = params["encode"].get<string>();
	}

	string rPath = params["path"].get<string>();
	if (rPath != "") {
		string conf = params["data"].get<string>();
		path = m_confPath + "/" + rPath;
		DB_FS::createFolderOfPath(path);

		if (encode == "base64") {
			//移除Data URI scheme中的前缀 
			if (conf.find("data:") == 0) {
				size_t pos = conf.find(",");
				if (pos > 0) {
					conf = conf.substr(pos + 1, conf.size() - pos);
				}
			}

			std::string image_data = base64_decode(conf);
#ifdef _WIN32
			wstring wpath = DB_STR::gb_to_utf16(path);
			// Write the binary data to a file
			std::ofstream image_file(wpath, std::ios::out | std::ios::binary);
#else
			std::ofstream image_file(path, std::ios::out | std::ios::binary);
#endif
			if (image_file.is_open()){
				image_file.write(image_data.c_str(), image_data.length());
				image_file.close();
			}

			resp.result = RPC_OK;
		}
		else {
			bool bRet = DB_FS::writeFile(path, conf);
			if (bRet) {
				g_mapConfFile[rPath] = conf; //更新内存中的配置文件]
			}

			resp.result = RPC_OK;
		}
	}
	else {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "path not specified");
	}
}