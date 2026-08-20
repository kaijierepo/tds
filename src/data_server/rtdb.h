#ifndef TDS_DATA_SERVER_RTDB_H
#define TDS_DATA_SERVER_RTDB_H

#include <map>
#include <mutex>
#include <string>
#include <fstream>

struct mg_http_message;
struct mg_connection;

/*
实时数据库 RTDB，类似 redis 的内存 key-value 存储，通过 HTTP 接口访问：
  POST   http://127.0.0.1:667/rtdb/<key>    请求体为 value，写入（key 不存在则新建，存在则覆盖）
  GET    http://127.0.0.1:667/rtdb/<key>    返回 value
  DELETE http://127.0.0.1:667/rtdb/<key>    删除 key
  GET    http://127.0.0.1:667/rtdb/         返回所有 key=value（每行一条）
*/
class Rtdb {
public:
	Rtdb();                                                          // 构造，标记未初始化
	void set(const std::string& key, const std::string& value);      // 写入
	bool get(const std::string& key, std::string& value);            // 读取，不存在返回 false
	bool del(const std::string& key);                                // 删除
	size_t size();                                                   // key 数量
	std::string dump();                                              // 所有 key=value，每行一条
	std::string m_path;

private:
	void ensureInit();                                               // 首次使用时加载持久化数据
	void persist();                                                  // 全量保存到磁盘

	std::map<std::string, std::string> m_mapData;       // key -> value
	std::mutex m_csData;                                // 访问锁
	std::string m_strPersistFile;                       // 持久化文件：数据库目录/rtdb/rtdb.json
	std::ofstream m_ofsPersist;                         // 常开的持久化文件句柄（不关闭，利用内核缓存）
	bool m_bInited;                                     // 是否已从磁盘加载
};

extern Rtdb rtdb;

// /rtdb/* HTTP 接口处理函数，注册到 g_mapHttpHandler
bool rtdb_handler(mg_http_message* hm, struct mg_connection* c);

#endif /* TDS_DATA_SERVER_RTDB_H */
