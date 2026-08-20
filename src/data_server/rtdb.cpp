#include "pch.h"
#include "rtdb.h"
#include "common.h"
#include "yyjson.h"

#include <vector>
#include <filesystem>
#include <cstring>

Rtdb rtdb;

Rtdb::Rtdb() {
	m_bInited = false;
}

// 首次使用时从持久化文件加载数据
void Rtdb::ensureInit() {
	std::lock_guard<std::mutex> lock(m_csData);
	if (m_bInited)
		return;

	m_strPersistFile = m_path + "/rtdb.json";
	fs::createFolderOfPath(m_strPersistFile);

	std::string strData;
	if (fs::readFile(m_strPersistFile, strData) && !strData.empty()) {
		yyjson_doc* pDoc = yyjson_read(strData.c_str(), strData.size(), 0);
		if (pDoc) {
			yyjson_val* pRoot = yyjson_doc_get_root(pDoc);
			if (yyjson_is_obj(pRoot)) {
				size_t nIdx, nMax;
				yyjson_val* pKey;
				yyjson_val* pVal;
				yyjson_obj_foreach(pRoot, nIdx, nMax, pKey, pVal) {
					m_mapData[yyjson_get_str(pKey)] = yyjson_get_str(pVal);
				}
			}
			yyjson_doc_free(pDoc);
		}
	}

	// 打开持久化文件并保持句柄不关闭，数据写入后仅 flush 到内核缓冲，由内核决定落盘时机
	std::filesystem::path pathPersist;
#ifdef _WIN32
	pathPersist = charCodec::tds_to_utf16(m_strPersistFile);
#else
	pathPersist = m_strPersistFile;
#endif
	m_ofsPersist.open(pathPersist, std::ios::out | std::ios::trunc | std::ios::binary);

	m_bInited = true;
}

// 全量数据保存为 JSON 对象 {key: value}，写入常开的文件句柄
void Rtdb::persist() {
	std::lock_guard<std::mutex> lock(m_csData);
	if (!m_ofsPersist.is_open())
		return;

	yyjson_mut_doc* pDoc = yyjson_mut_doc_new(NULL);
	yyjson_mut_val* pObj = yyjson_mut_obj(pDoc);
	yyjson_mut_doc_set_root(pDoc, pObj);
	for (std::map<std::string, std::string>::iterator it = m_mapData.begin(); it != m_mapData.end(); ++it) {
		yyjson_mut_obj_add_str(pDoc, pObj, it->first.c_str(), it->second.c_str());
	}

	char* pJson = yyjson_mut_write(pDoc, 0, NULL);
	if (pJson) {
		m_ofsPersist.seekp(0, std::ios::beg);              // 每次从文件头覆盖写入
		m_ofsPersist.write(pJson, (std::streamsize)strlen(pJson));
		m_ofsPersist.flush();                              // 刷到内核缓冲，不 fsync，减少磁盘操作频率

		// 新内容可能比旧内容短，截断文件尾部的旧数据残留
		std::error_code ec;
#ifdef _WIN32
		std::filesystem::resize_file(charCodec::tds_to_utf16(m_strPersistFile), (uintmax_t)m_ofsPersist.tellp(), ec);
#else
		std::filesystem::resize_file(m_strPersistFile, (uintmax_t)m_ofsPersist.tellp(), ec);
#endif
		free(pJson);
	}
	yyjson_mut_doc_free(pDoc);
}

void Rtdb::set(const std::string& key, const std::string& value) {
	ensureInit();
	{
		std::lock_guard<std::mutex> lock(m_csData);
		m_mapData[key] = value;
	}
	persist();
}

bool Rtdb::get(const std::string& key, std::string& value) {
	ensureInit();
	std::lock_guard<std::mutex> lock(m_csData);
	std::map<std::string, std::string>::iterator it = m_mapData.find(key);
	if (it == m_mapData.end())
		return false;
	value = it->second;
	return true;
}

bool Rtdb::del(const std::string& key) {
	ensureInit();
	bool bDel;
	{
		std::lock_guard<std::mutex> lock(m_csData);
		bDel = m_mapData.erase(key) > 0;
	}
	persist();
	return bDel;
}

size_t Rtdb::size() {
	ensureInit();
	std::lock_guard<std::mutex> lock(m_csData);
	return m_mapData.size();
}

std::string Rtdb::dump() {
	ensureInit();
	std::lock_guard<std::mutex> lock(m_csData);
	std::string strAll;
	for (std::map<std::string, std::string>::iterator it = m_mapData.begin(); it != m_mapData.end(); ++it) {
		strAll += it->first + "=" + it->second + "\r\n";
	}
	return strAll;
}

// 发送响应，支持跨域
static void rtdb_send_response(struct mg_connection* c, const std::string& strStatus, const std::string& strBody) {
	std::string strContentLen = std::to_string(strBody.size());
	std::string strHeader =
		"HTTP/1.1 " + strStatus + "\r\n"
		"Access-Control-Allow-Origin:*\r\n"
		"Access-Control-Allow-Private-Network: true\r\n"
		"Content-Type: text/plain; charset=utf-8\r\n"
		"Content-Length:" + strContentLen + "\r\n\r\n";

	mg_send(c, strHeader.c_str(), strHeader.size());
	mg_send(c, strBody.c_str(), strBody.size());
	c->is_resp = 0;
}

bool rtdb_handler(mg_http_message* hm, struct mg_connection* c) {
	std::string strUri = str::fromBuff(hm->uri.ptr, hm->uri.len);
	std::string strKey;
	if (strUri != "/rtdb" && strUri != "/rtdb/") {
		strKey = str::trimPrefix(strUri, "/rtdb/");
	}

	// URL 解码，key 可能含空格、中文等
	if (!strKey.empty()) {
		std::vector<char> bufDecoded(strKey.size() + 1, 0);
		int nDecoded = mg_url_decode(strKey.c_str(), strKey.size(), &bufDecoded[0], bufDecoded.size(), false);
		if (nDecoded > 0)
			strKey.assign(&bufDecoded[0], nDecoded);
	}

	// 不带 key：返回所有 key=value
	if (strKey.empty()) {
		std::string strBody = rtdb.dump();
		rtdb_send_response(c, "200 OK", strBody);
		return true;
	}

	if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
		std::string strValue;
		if (rtdb.get(strKey, strValue))
			rtdb_send_response(c, "200 OK", strValue);
		else
			rtdb_send_response(c, "404 Not Found", "key not found: " + strKey);
		return true;
	}

	if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
		std::string strValue = str::fromBuff(hm->body.ptr, hm->body.len);
		rtdb.set(strKey, strValue);
		rtdb_send_response(c, "200 OK", "ok");
		return true;
	}

	if (mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
		rtdb.del(strKey);
		rtdb_send_response(c, "200 OK", "ok");
		return true;
	}

	rtdb_send_response(c, "405 Method Not Allowed", "only GET/POST/DELETE supported");
	return true;
}
