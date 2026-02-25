#include "rpcHandler_common.h"
#include "base64.h"
#include <fstream>
#include <sstream>
#if (defined(_MSVC_LANG) && _MSVC_LANG < 201703L) || (!defined(_MSVC_LANG) && defined(__cplusplus) && __cplusplus < 201703L)
#include <experimental/filesystem>
namespace stdfs = std::experimental::filesystem;
#else
#include <filesystem>
namespace stdfs = std::filesystem;
#endif

RpcHandler_common rpcHandler_common;
map<string, string> g_mapConfFile;

static std::wstring utf8_to_utf16(const string& u8str) {
	const char* utf8_str = u8str.c_str();
	size_t length = u8str.length();
	if (!utf8_str || length == 0) {
		return std::wstring();
	}

	// 预分配足够的空间（最坏情况：每个ASCII字符对应1个wchar_t）
	std::wstring result;
	result.reserve(length);

	const uint8_t* data = reinterpret_cast<const uint8_t*>(utf8_str);
	const uint8_t* end = data + length;

	while (data < end) {
		uint8_t c = *data;

		if (c < 0x80) {
			// 单字节UTF-8 (0-0x7F)
			result.push_back(static_cast<wchar_t>(c));
			data++;
		}
		else if ((c & 0xE0) == 0xC0) {
			// 双字节UTF-8 (0x80-0x7FF)
			if (data + 1 >= end) {
				throw std::runtime_error("Invalid UTF-8 sequence: incomplete 2-byte sequence");
			}

			uint32_t code_point = ((c & 0x1F) << 6) | (data[1] & 0x3F);
			result.push_back(static_cast<wchar_t>(code_point));
			data += 2;
		}
		else if ((c & 0xF0) == 0xE0) {
			// 三字节UTF-8 (0x800-0xFFFF)
			if (data + 2 >= end) {
				throw std::runtime_error("Invalid UTF-8 sequence: incomplete 3-byte sequence");
			}

			uint32_t code_point = ((c & 0x0F) << 12) |
				((data[1] & 0x3F) << 6) |
				(data[2] & 0x3F);
			result.push_back(static_cast<wchar_t>(code_point));
			data += 3;
		}
		else if ((c & 0xF8) == 0xF0) {
			// 四字节UTF-8 (0x10000-0x10FFFF)，需要UTF-16代理对
			if (data + 3 >= end) {
				throw std::runtime_error("Invalid UTF-8 sequence: incomplete 4-byte sequence");
			}

			uint32_t code_point = ((c & 0x07) << 18) |
				((data[1] & 0x3F) << 12) |
				((data[2] & 0x3F) << 6) |
				(data[3] & 0x3F);

			// 转换为UTF-16代理对
			code_point -= 0x10000;
			wchar_t high_surrogate = static_cast<wchar_t>((code_point >> 10) + 0xD800);
			wchar_t low_surrogate = static_cast<wchar_t>((code_point & 0x3FF) + 0xDC00);

			result.push_back(high_surrogate);
			result.push_back(low_surrogate);
			data += 4;
		}
		else {
			throw std::runtime_error("Invalid UTF-8 sequence: invalid leading byte");
		}
	}

	// 调整容量以释放多余空间
	result.shrink_to_fit();
	return result;
}

static std::string utf16_to_utf8(const wstring& u16str) {
	const wchar_t* utf16_str = u16str.c_str();
	size_t length = u16str.length();
	if (!utf16_str || length == 0) {
		return std::string();
	}

	// 预分配足够的空间（最坏情况：每个UTF-16代码单元对应3字节）
	std::string result;
	result.reserve(length * 3);

	const wchar_t* data = utf16_str;
	const wchar_t* end = data + length;

	while (data < end) {
		uint32_t code_unit = static_cast<uint32_t>(*data);

		if (code_unit < 0xD800 || code_unit > 0xDFFF) {
			// 不是代理对，直接处理
			if (code_unit < 0x80) {
				// 单字节UTF-8
				result.push_back(static_cast<char>(code_unit));
			}
			else if (code_unit < 0x800) {
				// 双字节UTF-8
				result.push_back(static_cast<char>(0xC0 | (code_unit >> 6)));
				result.push_back(static_cast<char>(0x80 | (code_unit & 0x3F)));
			}
			else {
				// 三字节UTF-8
				result.push_back(static_cast<char>(0xE0 | (code_unit >> 12)));
				result.push_back(static_cast<char>(0x80 | ((code_unit >> 6) & 0x3F)));
				result.push_back(static_cast<char>(0x80 | (code_unit & 0x3F)));
			}
			data++;
		}
		else {
			// 处理代理对
			if (code_unit > 0xDBFF || data + 1 >= end) {
				throw std::runtime_error("Invalid UTF-16 sequence: invalid surrogate pair");
			}

			uint32_t high_surrogate = code_unit;
			uint32_t low_surrogate = static_cast<uint32_t>(*(data + 1));

			if (low_surrogate < 0xDC00 || low_surrogate > 0xDFFF) {
				throw std::runtime_error("Invalid UTF-16 sequence: invalid low surrogate");
			}

			// 计算实际代码点
			uint32_t code_point = ((high_surrogate - 0xD800) << 10) +
				(low_surrogate - 0xDC00) + 0x10000;

			// 四字节UTF-8
			result.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
			result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
			result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
			result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));

			data += 2;
		}
	}

	// 调整容量以释放多余空间
	result.shrink_to_fit();
	return result;
}

static bool createFolderOfPath(string strFile) {
	size_t iDotPos = strFile.rfind('.');
	size_t iSlashPos = strFile.rfind('/');
	if (iDotPos != string::npos && iDotPos > iSlashPos) {//is a file
		strFile = strFile.substr(0, iSlashPos);
	}

#ifdef _WIN32
	return stdfs::create_directories(utf8_to_utf16(strFile));
#else
	stdfs::path p = strFile;
	return stdfs::create_directories(p);
#endif
}

static bool readFile(string path, string& data)
{
	FILE* fp = nullptr;
#ifdef _WIN32
	_wfopen_s(&fp, utf8_to_utf16(path).c_str(), L"rb");
#else
	fp = fopen(path.c_str(), "rb");
#endif
	if (fp)
	{
		fseek(fp, 0, SEEK_END);
		long len = ftell(fp);
		data.resize(len);
		fseek(fp, 0, SEEK_SET);
		fread((void*)data.data(), 1, len, fp);
		fclose(fp);
		return true;
	}
	return false;
}

static bool writeFile(string path, const char* data, size_t len)
{
	createFolderOfPath(path);

	FILE* fp = nullptr;
#ifdef _WIN32
	wstring wpath = utf8_to_utf16(path);
	_wfopen_s(&fp, wpath.c_str(), L"wb");
#else
	fp = fopen(path.c_str(), "wb");
#endif
	if (fp)
	{
		fwrite(data, 1, len, fp);
		fclose(fp);
		return true;
	}
	else
	{
		printf("fopen fail,path=%s", path.c_str());
	}
	return false;
}

struct FS_FILE_INFO {
	string modifyTime;
	string createTime;
	size_t len;
	string accessTime;
	string name;
	string path;
	string folderPath;
};

static string fileTimeToString(stdfs::file_time_type ftime) {
	std::chrono::system_clock::time_point sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now()
	);
	time_t iUnix = std::chrono::system_clock::to_time_t(sctp);
	tm time_tm;
#ifdef _WIN32
	localtime_s(&time_tm, &iUnix);
#else
	localtime_r(&iUnix, &time_tm);  //for thread safty linux recommends localtime_r,windows recommends localtime_s
#endif
	int iYear = time_tm.tm_year + 1900;
	int iMonth = time_tm.tm_mon + 1;
	int iDay = time_tm.tm_mday;
	int iHour = time_tm.tm_hour;
	int iMin = time_tm.tm_min;
	int iSec = time_tm.tm_sec;
	char sTime[50] = { 0 };
	sprintf(sTime, "%04d-%02d-%02d %02d:%02d:%02d", iYear, iMonth, iDay, iHour, iMin, iSec);
	return sTime;
}


static void getFolderList(vector<FS_FILE_INFO>& list, string strFolder, bool recursive = false) {
	try
	{
		wstring wstrFolder = utf8_to_utf16(strFolder);
		for (auto& i : stdfs::directory_iterator(wstrFolder)) {
			if (stdfs::is_directory(i.path())) {
				FS_FILE_INFO fi;
				fi.path = utf16_to_utf8(i.path().wstring());
				size_t pos = fi.path.rfind("/");
				fi.folderPath = fi.path.substr(0, pos);
				fi.name = fi.path.substr(pos + 1, fi.path.length() - pos - 1);

				for (auto& entry : stdfs::recursive_directory_iterator(i.path())) {
					if (stdfs::is_regular_file(entry.path())) {
						fi.len += stdfs::file_size(entry.path());
					}
				}

				stdfs::file_time_type ftime = stdfs::last_write_time(i.path());
				fi.modifyTime = fileTimeToString(ftime);

				list.push_back(fi);

				if (recursive) {
					getFolderList(list, utf16_to_utf8(i.path().wstring()), recursive);
				}
			}
		}
	}
	catch (exception&) {
	}
}


void getFileList(vector<FS_FILE_INFO>& list, string strFolder, bool recursive = false, string suffix = "*", vector<string>* exclude = nullptr) {
	try
	{
		wstring wstrFolder = utf8_to_utf16(strFolder);
		for (auto& i : stdfs::directory_iterator(wstrFolder)) {
			FS_FILE_INFO fi;
			fi.path = utf16_to_utf8(i.path().wstring());
			fi.name = utf16_to_utf8(i.path().filename().wstring());
			if (exclude != nullptr) {
				bool excluded = false;
				for (int i = 0; i < exclude->size(); i++) {
					string ep = exclude->at(i);
					if (fi.name == ep) {
						excluded = true;
						break;
					}
				}

				if (excluded) {
					continue;
				}
			}

			if (stdfs::is_directory(i.path())) {
				if (recursive) {
					getFileList(list, utf16_to_utf8(i.path().wstring()), recursive, suffix, exclude);
				}
			}
			else {
				//std::filesystem::file_time_type ft = i.last_write_time();
				//std::time_t tt = decltype(ft)::clock::to_time_t();
				if (suffix != "*" && fi.path.find(suffix) == string::npos)
					continue;
				size_t pos = fi.path.rfind("/");
				fi.folderPath = fi.path.substr(0, pos);
				fi.len = stdfs::file_size(i.path());
				stdfs::file_time_type ftime = stdfs::last_write_time(i.path());
				fi.modifyTime = fileTimeToString(ftime);
				list.push_back(fi);
			}
		}
	}
	catch (exception&) {
	}
}


void getFileList(vector<string>& list, string strFolder, bool includeFolder, bool recursive)
{
	vector<FS_FILE_INFO> filist;
	getFileList(filist, strFolder, recursive);
	for (int i = 0; i < filist.size(); i++) {
		FS_FILE_INFO& fi = filist[i];
		list.push_back(fi.path);
	}
}


bool RpcHandler_common::handleRpc(const string& method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	bool handled = true;
	if (method.find("fs.") != string::npos) {
		string path = params["path"].get<string>();
		string rootPath = "";
		if (params["root"].is_string()) {
			rootPath = params["root"];
		}
		else if(params["rootType"].is_string()) {
			string rootType = params["rootType"].get<string>();
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
		}
		path = rootPath + path; //rootPath最后不带 / ，path以 /开始

		if (method == "fs.readFile") {
			if (params["type"] != nullptr && params["type"].get<string>() == "binary") {

			}
			else {
				string s;
				if (readFile(path, s)) {
					json j = s;
					rpcResp.result = j.dump();
				}
				else {
					string path = params["path"];
					if (!stdfs::exists(path)) {
						rpcResp.error = makeRPCError(OS_fileNotExist, "file not exist");
					}
					else {
						rpcResp.error = makeRPCError(TEC_FAIL, "fail");
					}
				}
			}
		}
		else if (method == "fs.deleteFile") {
			try {
				if (stdfs::exists(path)) {
					bool success = stdfs::remove(path);
					rpcResp.result = "\"ok\"";
				}
				else {
					rpcResp.error = makeRPCError(TEC_FAIL, "file not exist");
				}
			}
			catch (const stdfs::filesystem_error& e) {
				string s = e.what();
				rpcResp.error += "文件系统错误:" + s;
			}
		}
		else if (method == "fs.writeFile") {
			createFolderOfPath(path);
			if (params["data"] != nullptr) {
				string d = params["data"].get<string>();

				string encode = "";
				if (params.contains("encode")) {
					encode = params["encode"].get<string>();
				}

				if (encode == "base64") {
					unsigned char* out = new unsigned char[d.length()];
					int len = base64_decode(d.c_str(), (int)d.length(), out);

					if (writeFile(path, (char*)out, len)) {
						rpcResp.result = "\"ok\"";
					}
					else {
						rpcResp.error = makeRPCError(TEC_FAIL, "fail");
					}

					delete[] out;
				}
				else {
					if (writeFile(path, d.c_str(),d.length())) {
						rpcResp.result = "\"ok\"";
					}
					else {
						rpcResp.error = makeRPCError(TEC_FAIL, "fail");
					}
				}
			}
		}
		else if (method == "fs.getFileList") {
			bool includeFolder = false;
			bool recursive = false;

			if (params["includeFolder"] != nullptr) {
				includeFolder = params["includeFolder"].get<bool>();
			}

			if (params["recursive"] != nullptr) {
				recursive = params["recursive"].get<bool>();
			}

			vector<string> fl;
			getFileList(fl, path, includeFolder, recursive);

			json j = fl;
			rpcResp.result = j.dump();
		}
		else if (method == "fs.exploreFolder") {
			vector<FS_FILE_INFO> fileList;
			vector<FS_FILE_INFO> folderList;

			if (path == "") {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "specify path");
				return true;
			}

			getFileList(fileList, path);
			getFolderList(folderList, path);

			json infoList = json::array();
			for (int i = 0; i < folderList.size(); i++) {
				FS_FILE_INFO& fi = folderList[i];

				json j;
				j["name"] = fi.name;
				j["size"] = fi.len;
				j["modifyTime"] = fi.modifyTime;
				j["isFolder"] = true;

				infoList.push_back(j);
			}

			for (int i = 0; i < fileList.size(); i++) {
				FS_FILE_INFO& fi = fileList[i];

				json j;
				j["name"] = fi.name;
				j["size"] = fi.len;
				j["modifyTime"] = fi.modifyTime;
				j["isFolder"] = false;

				infoList.push_back(j);
			}

			rpcResp.result = infoList.dump();
		}
	}

#ifdef CONF_FILE
	else if (method == "getConfFile") {
		rpc_getconffile(params, rpcResp, session);
	}
	else if (method == "setConfFile") {
		rpc_setconffile(params, rpcResp, session);
	}
#endif
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

		readFile(p, conf);

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
		createFolderOfPath(path);

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
			wstring wpath = utf8_to_utf16(path);
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
			bool bRet = writeFile(path, conf.c_str(),conf.length());
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