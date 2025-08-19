#include "rpcHandler_common.h"
#include "common.h"
#include "base64.h"

RpcHandler_common rpcHandler_common;

bool RpcHandler_common::handleRpc(const string& method,json& params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	bool handled = true;

	if (method == "fs.readFile") {
		if (params["type"] != nullptr && params["type"].get<string>() == "binary") {
			char* p = NULL; int len = 0;
			if (fs::readFile(params["path"].get<string>(), p, len)) {
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
			if (fs::readFile(path, s)) {
				json j = s;
				rpcResp.result = j.dump();
			}
			else {
				if (!fs::fileExist(params["path"])) {
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

		if (fs::deleteFile(path)) {
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
		fs::createFolderOfPath(path);

		if (params["data"] != nullptr) {
			string d = params["data"].get<string>();

			string encode = "";
			if (params.contains("encode")) {
				encode = params["encode"].get<string>();
			}

			if (encode == "base64") {
				unsigned char* out = new unsigned char[d.length()];
				int len = base64_decode(d.c_str(), (int)d.length(), out);

				if (fs::writeFile(path, (char*)out, len)) {
					rpcResp.result = "\"ok\"";
				}
				else {
					rpcResp.error = makeRPCError(TEC_FAIL, "fail");
				}

				delete[] out;
			}
			else {
				string ap = fs::toAbsolutePath(path);
				if (fs::writeFile(ap, d)) {
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
		json j = charCodec::utf16_to_utf8(s);

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
		fs::getFileList(fl, path, includeFolder, recursive);

		json j = fl;
		rpcResp.result = j.dump();
	}
	else if (method == "fs.exploreFolder") {
		string root = "";
		if (params["root"].is_string()) {
			root = params["root"].get<string>();
		}

		vector<fs::FILE_INFO> fileList;
		vector<fs::FILE_INFO> folderList;

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

		fs::getFileList(fileList, path);
		fs::getFolderList(folderList, path);

		json infoList = json::array();
		for (int i = 0; i < folderList.size(); i++) {
			fs::FILE_INFO& fi = folderList[i];

			json j;
			j["name"]       = fi.name;
			j["size"]       = fi.len;
			j["modifyTime"] = fi.modifyTime;
			j["isFolder"]   = true;

			infoList.push_back(j);
		}

		for (int i = 0; i < fileList.size(); i++) {
			fs::FILE_INFO& fi = fileList[i];

			json j;
			j["name"]       = fi.name;
			j["size"]       = fi.len;
			j["modifyTime"] = fi.modifyTime;
			j["isFolder"]   = false;

			infoList.push_back(j);
		}

		rpcResp.result = infoList.dump();
	}
	else {
		handled = false;
	}

	return handled;
}
