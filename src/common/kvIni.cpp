#include "kvIni.h"
#include <regex>
#include <locale>
#include <codecvt>
#include <stdexcept>
#include <filesystem>


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


int splitStr(std::vector<std::string>& dst, const std::string& src, std::string separator)
{
	if (src.empty() || separator.empty())
		return 0;

	int nCount = 0;
	std::string temp;
	size_t pos = 0, offset = 0;

	// 分割第1~n-1个
	while ((pos = src.find(separator, offset)) != std::string::npos)
	{
		temp = src.substr(offset, pos - offset);
		if (temp.length() > 0) {
			dst.push_back(temp);
			nCount++;
		}
		else
		{
			dst.push_back("");
			nCount++;
		}
		offset = pos + separator.size();
	}

	// 分割第n个
	temp = src.substr(offset, src.length() - offset);
	if (temp.length() > 0) {
		dst.push_back(temp);
		nCount++;
	}

	return nCount;
}

static string trimPrefix(string s, string prefix)
{
	if (prefix == "")
		return s;

	while (1)
	{
		if (s.find(prefix) == 0)
		{
			s = s.substr(prefix.length(), s.length() - prefix.length());
		}
		else
		{
			break;
		}
	}

	return s;
}

static string trimSuffix(string s, string suffix)
{
	if (suffix == "")
		return s;

	while (1)
	{
		size_t ipos = s.rfind(suffix);
		if (ipos != string::npos && ipos + suffix.length() == s.length())
		{
			s = s.substr(0, ipos);
		}
		else
		{
			break;
		}
	}
	return s;
}

static string trim(std::string s, string toTrim)
{
	s = trimPrefix(s, toTrim);
	s = trimSuffix(s, toTrim);
	return s;
}

static bool readFile(string path, char*& pData, int& len)
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
		len = ftell(fp);
		pData = new char[len];
		fseek(fp, 0, SEEK_SET);
		fread(pData, 1, len, fp);
		fclose(fp);
		return true;
	}
	return false;
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
		char* pdata = new char[len + 2];
		memset(pdata, 0, len + 2);
		fseek(fp, 0, SEEK_SET);
		fread(pdata, 1, len, fp);
		data = pdata;
		fclose(fp);
		delete[] pdata;
		return true;
	}
	return false;
}

static std::string remove_utf8_bom(const std::string& data) {
	// UTF-8 BOM是3个字节: 0xEF 0xBB 0xBF
	const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };

	// 检查字符串是否以BOM开头
	if (data.size() >= 3 &&
		static_cast<unsigned char>(data[0]) == bom[0] &&
		static_cast<unsigned char>(data[1]) == bom[1] &&
		static_cast<unsigned char>(data[2]) == bom[2]) {
		// 返回去除BOM后的子字符串
		return data.substr(3);
	}

	// 如果没有BOM，返回原字符串
	return data;
}

string replaceStr(string str, const string to_replaced, const string newchars)
{
	for (string::size_type pos(0); pos != string::npos; pos += newchars.length())
	{
		pos = str.find(to_replaced, pos);
		if (pos != string::npos)
			str.replace(pos, to_replaced.length(), newchars);
		else
			break;
	}
	return   str;
}

bool createFolderOfPath(string strFile)
{
	strFile = replaceStr(strFile, "\\", "/");
	strFile = replaceStr(strFile, "////", "/");
	strFile = replaceStr(strFile, "///", "/");
	strFile = replaceStr(strFile, "//", "/");

	size_t iDotPos = strFile.rfind('.');
	size_t iSlashPos = strFile.rfind('/');
	if (iDotPos != string::npos && iDotPos > iSlashPos)//是一个文件
	{
		strFile = strFile.substr(0, iSlashPos);
	}
	//如果路径的末尾是/，创建成功也会返回false,因此删除末尾的 /
	if (iSlashPos == strFile.length() - 1) {
		strFile = strFile.substr(0, iSlashPos);
	}

	return std::filesystem::create_directories(utf8_to_utf16(strFile));
}

bool writeFile(string path, char* data, size_t len)
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
		printf("writeFile error");
	}
	return false;
}

static bool appendFile(string path, char* data, size_t len)
{
	FILE* fp = nullptr;
#ifdef _WIN32
	_wfopen_s(&fp, utf8_to_utf16(path).c_str(), L"ab");
#else
	fp = fopen(path.c_str(), "ab");
#endif
	if (fp)
	{
		fwrite(data, 1, len, fp);
		fclose(fp);
		return true;
	}
	return false;
}

static bool appendFile(string path, string data)
{
	return appendFile(path, (char*)data.data(), data.length());
}
bool writeFile(string path, string& data)
{
	return writeFile(path, (char*)data.c_str(), data.length());
}

bool KV_INI::load(string path)
{
	m_path = path;
	//配置文件当中的值  如果有值，说明是命令行设置，命令行优先级最高
	readFile(path, m_strConf);
	m_strConf = remove_utf8_bom(m_strConf);
	vector<string> confItems;
	splitStr(confItems, m_strConf, "\n");

	bool isAppConf = path.find("appConf.ini") != string::npos;

	//去掉注释
	for (int i = 0; i < confItems.size(); i++)
	{
		string ci = confItems[i];
		KV_INI_LINE oneLine;

		//appConf.ini 里可以忽略confPath=C:\A\B前面的confPath部分
		//直接配置为  C:\A\B
		if (isAppConf) { 
			if (ci.find("#") == -1 && ci.find("=") == -1 &&
				(ci.find("/") != string::npos || ci.find("\\") != string::npos))
			{
				oneLine.key = "confPath";
				oneLine.val = ci;
				oneLine.val = trim(oneLine.val, "\r");
				oneLine.val = trim(oneLine.val, " ");
				oneLine.val = trim(oneLine.val, "\t");
				oneLine.type = CONF_ITEM;
				mapConf.push_back(oneLine);
				continue;
			}
		}


		if (ci.find("#") == -1 && ci.find("=") == -1)
		{
			oneLine.type = BLANK;
			mapConf.push_back(oneLine);
			continue;
		}
		else
		{
			size_t pos = ci.find("#");
			if (pos != string::npos)
			{
				oneLine.note = ci.substr(pos + 1);
				ci = ci.substr(0, pos);
				oneLine.note = trim(oneLine.note, "\r");
			}

			pos = ci.find("=");
			if (pos != string::npos)
			{
				oneLine.key = ci.substr(0, pos);
				oneLine.val = ci.substr(pos + 1, ci.length() - pos - 1);

				oneLine.val = trim(oneLine.val, "\r");
				oneLine.key = trim(oneLine.key, "\t");//去除键值对中间的tab,如果路径内存在tab这个会被读为\t,然后导致创建文件夹失败.
				oneLine.val = trim(oneLine.val, "\t");
				oneLine.key = trim(oneLine.key, " ");
				oneLine.val = trim(oneLine.val, " ");
			}
			else {  //

			}

			if (oneLine.key != "")
			{
				oneLine.type = CONF_ITEM;
				mapConf.push_back(oneLine);
				continue;
			}
			else
			{
				oneLine.type = NOTE;
				mapConf.push_back(oneLine);
				continue;
			}
		}
	}

	return true;
}

bool KV_INI::save(string path)
{
	string temp = "";
	for (auto& line : mapConf)
	{
		if (line.type == NOTE)
		{
			temp += ("#" + line.note + "\n");
		}
		else if (line.type == CONF_ITEM)
		{
			temp += (line.key+"="+line.val);
			if (line.note != "")
			{
				temp += ("\t\t\t\t\t\t#" + line.note);
			}
			temp += "\n";
		}
		else if (line.type== BLANK)
		{
			temp += "\n";
		}
	}

	m_strConf = temp;
	writeFile(path, m_strConf);
	return true;
}

void KV_INI::setVal(string key, int val)
{
	string s = to_string(val);
	setVal(key, s);
}

void KV_INI::setVal(string key, string val)
{
	bool exist = false;
	for (auto& line : mapConf) {
		if (line.type == CONF_ITEM) {
			if (line.key == key) {
				line.val = val;
				exist = true;
				break;
			}
		}
	}

	if (!exist) {
		KV_INI_LINE line;
		line.type = CONF_ITEM;
		line.key = key;
		line.val = val;
		mapConf.push_back(line);
	}
	//序列化成文件
	save(m_path);
}

int KV_INI::getValInt(string key, int defaultVal)
{
	bool bFind = false;
	int iResult = 0;
	for (auto& line : mapConf) {
		if (line.type == CONF_ITEM) {
			if (line.key == key) {
				iResult = atoi(line.val.c_str());
				bFind = true;
				break;
			}
		}
	}

	if (bFind)
	{
		return iResult;
	}
	else
		return defaultVal;
}

string KV_INI::getValStr(string key, string defaultVal)
{
	bool bFind = false;
	string strResult = "";
	for (auto& line : mapConf) {
		if (line.type == CONF_ITEM) {
			if (line.key == key) {
				strResult = line.val;
				bFind = true;
				break;
			}
		}
	}

	if (bFind)
	{
		return strResult;
	}
	else
		return defaultVal;
}
