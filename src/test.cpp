#define UNIT_TEST

#ifdef UNIT_TEST
#include "common.h"
#include "logger.h"
#include <filesystem>

void testFs() {
	string appPath = fs::appPath();
	LOG("appPath:" + appPath);
	string testPath = appPath + "/测试/第一层/第二层/测试.txt";
	string testPath1 = appPath + "/测试/第1层/第二层/测试1.txt";
	string testPath2 = appPath + "/测试/第1层/第二层/测试2.txt";
	fs::createFolderOfPath(testPath);
	string fileData = "abcdef测试";
	fs::writeFile(testPath, fileData);
	fs::writeFile(testPath1, fileData);
	fs::writeFile(testPath2, fileData);
	bool b = fs::fileExist(testPath1);
	LOG("exist %d", b);
	bool b1 = fs::fileExist(appPath + "1/2/3.dd");
	vector<fs::FILE_INFO> fl,fl1;
	fs::getFileList(fl,appPath + "/测试/第1层/第二层");

	fs::getFolderList(fl1, appPath + "/测试");

	fs::deleteFile(testPath2);

	std::filesystem::path p_u16 = charCodec::utf8_to_utf16(testPath);
	std::filesystem::path p_gb = charCodec::utf8_to_gb(testPath);

	std::error_code error;
	std::filesystem::file_status file_status1 = std::filesystem::status(p_u16, error);
	std::filesystem::file_status file_status2 = std::filesystem::status(p_gb, error);

	LOG(p_u16.u8string());
	LOG(p_gb.u8string());
}

void testChnPath() {
	
}


class UnitTest {
public:
	UnitTest() {
		testFs();
	}
};

UnitTest ut;

#endif