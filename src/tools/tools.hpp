#pragma once
#include "common.h"
#include "logger.h"
using namespace std;
namespace Tools {
	void replaceStrInFile(string filePath, string oldStr, string newStr) {
		string s;
		if (!fs::readFile(filePath, s)) {
			LOG("读取文件失败");
			return;
		}

		s = str::replace(s, oldStr, newStr);
		if (!fs::writeFile(filePath, s)) {
			LOG("写入文件失败");
			return;
		}
	}
}