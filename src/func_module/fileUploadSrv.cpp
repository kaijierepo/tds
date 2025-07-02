#include "pch.h"
#include "fileUploadSrv.h"

#include "logger.h"

fileUploadSrv fileUploadServer;
std::map<std::string, std::map<int, bool>> uploaded_chunks; // 文件名 -> 块索引 -> 是否已上传  
std::mutex chunk_mutex;

void save_chunk(const std::string& filename, int chunk_index, const std::string& chunk_data) {
	/*auto chunk_obj = req.get_file_value("chunk");
	string oss = tds->conf->fmsPath + "/" + filename + ".part" + to_string(chunk_index);
	if (!fs::fileExist(oss)) {
		fs::createFolderOfPath(oss);
	}
	oss = charCodec::utf8_to_gb(oss);
	std::ofstream ofs(oss.c_str(), std::ios::binary);
	if (ofs) {
		ofs.write(chunk_obj.content.c_str(), chunk_obj.content.size());
		ofs.close();
		LOG(str::format("[文件上传服务]文件片段: %s 已保存", (filename+ ".part" + to_string(chunk_index)).c_str()).c_str());
	}
	else {
		LOG(str::format("[文件上传服务]文件片段: %s 保存失败", (filename + ".part" + to_string(chunk_index)).c_str()).c_str());
	}*/
}

void merge_chunks(const std::string& filename, int total_chunks) {
	//string totalFilePath = tds->conf->fmsPath + "/" + filename;
	//totalFilePath = charCodec::utf8_to_gb(totalFilePath);
	//std::ofstream ofs(totalFilePath.c_str(), std::ios::binary);
	//if (ofs) {
	//	for (int i = 0; i < total_chunks; ++i) {
	//		string OneChunksPath = totalFilePath + ".part" + to_string(i);
	//		std::ifstream ifs(OneChunksPath.c_str(), std::ios::binary);
	//		if (ifs) {
	//			ifs.seekg(0, std::ios::end);
	//			std::streamsize size = ifs.tellg();
	//			ifs.seekg(0, std::ios::beg);

	//			std::vector<char> buffer(size);
	//			ifs.read(buffer.data(), size);
	//			ofs.write(buffer.data(), size);

	//			ifs.close();
	//			std::remove(OneChunksPath.c_str()); // 删除临时块文件  
	//		}
	//		else {
	//			LOG(str::format("[文件上传服务]文件: %s 打开失败,导致合并失败", (filename + ".part" + to_string(i)).c_str()).c_str());
	//			break;
	//		}
	//	}
	//	ofs.close();
	//	LOG(str::format("[文件上传服务]文件: %s 合并成功",filename.c_str()).c_str());
	//}
	//else {
	//	LOG(str::format("[文件上传服务]文件: %s 打开失败,合并失败", filename.c_str()).c_str());
	//}
}

void fileUpload_thread(int port)
{
	//httplib::Server svr;

	//svr.Post("/upload_chunk", [](const httplib::Request& req, httplib::Response& res) {
	//	auto filename_obj = req.get_file_value("filename");
	//	std::string filename = filename_obj.content;
	//	auto chunk_index_obj = req.get_file_value("chunk_index");
	//	int chunk_index = std::stoi(chunk_index_obj.content);
	//	auto chunk_total_obj = req.get_file_value("chunk_total");
	//	int chunk_total = std::stoi(chunk_total_obj.content);
	//	std::lock_guard<std::mutex> lock(chunk_mutex);

	//	// 保存块数据  
	//	save_chunk(filename, chunk_index, chunk_total_obj.content, req);

	//	// 更新已上传块的状态  
	//	uploaded_chunks[filename][chunk_index] = true;

	//	// 检查是否所有块都已上传  
	//	if (uploaded_chunks[filename].size() >= chunk_total)
	//	{
	//		bool all_chunks_uploaded = true;
	//		for (const auto& pair : uploaded_chunks[filename]) {
	//			if (!pair.second) {
	//				all_chunks_uploaded = false;
	//				break;
	//			}
	//		}

	//		if (all_chunks_uploaded) {
	//			//合并段落
	//			merge_chunks(filename, chunk_total);

	//			// 清除已上传块的状态（可选）  
	//			uploaded_chunks.erase(filename);
	//		}
	//	}
	//	res.set_content("Chunk uploaded successfully", "text/plain");
	//	res.set_header("Access-Control-Allow-Origin", "*");//允许所有源，也可以指定请求中的源
	//	res.set_header("Access-Control-Allow-Private-Network", "true");//CORS-RFC1918 允许私有网络请求
	//	res.status = 200;
	//	});
	//svr.listen("0.0.0.0", port);
}

void fileUploadSrv::run(int port)
{
	string log = str::format("[文件上传服务] 端口:%d", port);
	LOG(log.c_str());

	thread t(fileUpload_thread, port);
	t.detach();

}