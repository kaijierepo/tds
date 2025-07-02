#include "common.h"
#include <filesystem>
#include <stdarg.h>
#include <random>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <chrono>
#include "mongoose.h"

using namespace std;

namespace common {
	string& getCharCodec() {
		static string charCodec = "utf8";
		return charCodec;
	}

	void setCharCodec(string codec) {
		string& cc = getCharCodec();
		cc = codec;
	}

	unsigned short N_CRC16(unsigned char* updata, long long len)
	{
		unsigned char uchCRCHi = 0xff;
		unsigned char uchCRCLo = 0xff;
		long long  uindex;
		while (len--)
		{
			uindex = uchCRCHi ^ *updata++;
			uchCRCHi = uchCRCLo ^ auchCRCHi[uindex];
			uchCRCLo = auchCRCLo[uindex];
		}
		return (uchCRCHi << 8 | uchCRCLo);
	}

	void endianSwap(void* pData, int len)
	{
		char* pNew = new char[len];
		for (int i = 0;i<len;i++)
		{
			pNew[i] = ((char*)pData)[len - 1 - i];
		}
		memcpy(pData, pNew, len);
		delete [] pNew;
	}
}


namespace charCodec {

	string utf16_to_utf8(wstring instr) //utf-8-->ansi
	{
		string str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 4 + 2;
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_UTF8, 0, instr.c_str(), -1, charstr, (int)MAX_STRSIZE, NULL, NULL);
		str = charstr;
		delete [] charstr;
#else
		
#endif
		return str;
	}
	string utf16_to_gb(wstring instr)
	{
		string str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_ACP, 0, instr.c_str(), -1, charstr,(int) MAX_STRSIZE, NULL, NULL);
		str = charstr;
		delete [] charstr;
#else
		
#endif
		return str;
	}
	wstring utf8_to_utf16(string instr) //utf-8-->ansi
	{
		wstring str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_UTF8, 0, (char*)instr.data(), -1, wcharstr, (int)MAX_STRSIZE);
		str = wcharstr;
		delete[] wcharstr;
		
#else
		
#endif
		return str;
	}
	wstring gb_to_utf16(string instr)
	{
		wstring str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_ACP, 0, (char*)instr.data(), -1, wcharstr,(int)MAX_STRSIZE);
		str = wcharstr;
		delete [] wcharstr;
#else
		
#endif
		return str;
	}
	
	string utf8_to_gb(string instr) //utf-8-->ansi
	{
		string str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_UTF8, 0, (char*)instr.data(), -1, wcharstr, (int)MAX_STRSIZE);
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_ACP, 0, wcharstr, -1, charstr,(int) MAX_STRSIZE, NULL, NULL);
		str = charstr;
		delete [] wcharstr;
		delete [] charstr;
#else
		//int ret = 0;
		//size_t inlen = instr.size() + 1;
		//size_t outlen = 2*inlen;

		//// duanqn: The iconv function in Linux requires non-const char *
		//// So we need to copy the source string
		//char* inbuf = (char*)malloc(inlen);
		//memset(inbuf,0,inlen);
		//char* inbuf_hold = inbuf;   // iconv may change the address of inbuf
		//							// so we use another pointer to keep the address
		//memcpy(inbuf, instr.data(), instr.length());

		//char* outbuf =(char*)malloc(outlen);
		//memset(outbuf, 0, outlen);
		//iconv_t cd;
		//cd = iconv_open("GBK", "UTF-8");
		//if (cd != (iconv_t)-1) {
		//	ret = iconv(cd, &inbuf, &inlen, &outbuf, &outlen);
		//	if (ret != 0) {
		//		printf("iconv failed err: %s\n", strerror(errno));
		//	}

		//	iconv_close(cd);
		//}
		//free(inbuf_hold);   // Don't pass in inbuf as it may have been modified

		//if(outbuf!=nullptr){
		//	str = outbuf;
		//	free(outbuf);
		//}
		str = instr;
#endif
		return str;
	}
	string gb_to_utf8(string instr) //ansi-->utf-8
	{
		string str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_ACP, 0, (char*)instr.data(), -1, wcharstr,(int) MAX_STRSIZE);
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_UTF8, 0, wcharstr, -1, charstr, (int)MAX_STRSIZE, NULL, NULL);
		str = charstr;
		delete [] wcharstr;
		delete [] charstr;
#else
		//int ret = 0;
		//size_t inlen = instr.length() + 1;
		//size_t outlen = 2*inlen;

		//// duanqn: The iconv function in Linux requires non-const char *
		//// So we need to copy the source string
		//char* inbuf = (char*)malloc(inlen);
		//char* inbuf_hold = inbuf;   // iconv may change the address of inbuf
		//							// so we use another pointer to keep the address
		//memcpy(inbuf, instr.data(), instr.length());

		//char* outbuf = (char*)malloc(outlen);
		//memset(outbuf, 0, outlen);
		//iconv_t cd;

		//cd = iconv_open("UTF-8", "GBK");
		//if (cd != (iconv_t)-1) {
		//	ret = iconv(cd, &inbuf, &inlen, &outbuf, &outlen);
		//	if (ret != 0)
		//		printf("iconv failed err: %s\n", strerror(errno));
		//	iconv_close(cd);
		//}
		//free(inbuf_hold);   // Don't pass in inbuf as it may have been modified
		//str = outbuf;
		//free(outbuf);
		str = instr;
#endif
		return str;
	}

	//GB2312 value region  A1A1－FEFE  for chinese chars is B0A1-F7FE。
	bool hasGB2312(string s)
	{
		for (size_t i = 0; i < s.length(); i++)
		{
			int b = (int)(unsigned char)s.at(i);
			if (b >= 0xA1 && b <= 0xFE) //gb2312
			{
				if (i + 1 < s.length())
				{
					int bNext = (int)(unsigned char)s.at(i + 1);
					if (bNext >= 0xA1 && bNext <= 0xFE)
					{
						return true;
					}
				}
			}
		}

		return false;
	}
	bool isValidGB2312(string s, size_t& errorPos, string& errorChar)
	{
		for (size_t i = 0; i < s.length();)
		{
			int b = (int)(unsigned char)s.at(i);
			if (b > 0 && b < 127) //ascii
			{
				i++;
				continue;
			}
			else
			{
				if (b >= 0xA1 && b <= 0xFE) //gb2312
				{
					if (i + 1 < s.length())
					{
						int bNext = (int)(unsigned char)s.at(i + 1);
						if (bNext >= 0xA1 && bNext <= 0xFE)
						{
							i += 2;
							continue;
						}
						else
						{
							errorPos = i;
							errorChar = str::format("%02X%02X", b, bNext);
							return false;
						}
					}
					else // invalid length
					{
						errorPos = i;
						errorChar = "invalid length";
						return false;
					}
				}
				else // wrong hex value
				{
					errorPos = i;
					errorChar = str::format("%02X", b);
					return false;
				}
			}
		}

		return true;
	}
	bool isValidGB2312(string s)
	{
		size_t pos = 0;
		string errorChar;
		return isValidGB2312(s, pos, errorChar);
	}

	string utf16Str_to_utf8(string s) {
		string u8Str;
		for (size_t i = 0; i < s.length() - 5; i++) {
			char c = s[i];
			if (c == '\\' && s[i + 1] == 'u') {
				string strCode = s.substr(i + 2, 4);
				wchar_t wchar;
				vector<unsigned char> vec = str::hexStrToBytes(strCode);
				wchar = vec[0] * 256 + vec[1];
				wstring wStr;
				wStr.push_back(wchar);
				string u8Char = charCodec::utf16_to_utf8(wStr);
				u8Str += u8Char;
				i += 5;
			}
			else {
				u8Str.push_back(c);
			}
		}

		return u8Str;
	}

	string utf16_to_tds(wstring instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			s = charCodec::utf16_to_gb(instr);
		}
		else
		{
			s = charCodec::utf16_to_utf8(instr);
		}
		return s;
	}
	wstring tds_to_utf16(string instr)
	{
		wstring w;
		if (common::getCharCodec() == "gb2312")
		{
			w = charCodec::gb_to_utf16(instr);
		}
		else
		{
			w = charCodec::utf8_to_utf16(instr);
		}
		return w;
	}
	string tds_to_utf8(string instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			s = charCodec::gb_to_utf8(instr);
			return s;
		}
		else
		{
			return instr;
		}
	}
	string tds_to_gb(string instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			return instr;
		}
		else
		{
			return charCodec::utf8_to_gb(instr);
		}
	}
	string gb_to_tds(string instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			return instr;
		}
		else
		{
			return charCodec::gb_to_utf8(instr);
		}
	}
	string utf8_to_tds(string instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			return charCodec::utf8_to_gb(instr);
		}
		else
		{
			return instr;
		}
	}

	bool isUrlReservedChar(char ch) {
		return std::isalnum(static_cast<unsigned char>(ch)) ||
			ch == '-' || ch == '_' || ch == '.' || ch == '!' ||
			ch == '~' || ch == '*' || ch == '\'' || ch == '(' ||
			ch == ')' || ch == ';' || ch == ':' || ch == '@' ||
			ch == '&' || ch == '=' || ch == '+' || ch == '$' ||
			ch == ',' || ch == '/' || ch == '?' || ch == '#' ||
			ch == '[' || ch == ']' || ch == '@';
	}

	// Function to URL encode a UTF-8 string
	std::string urlEncode(const std::string& str) {
		std::ostringstream encoded;

		for (char ch : str) {
			if (isUrlReservedChar(ch)) {
				encoded << ch;
			}
			else {
				encoded << '%' << std::hex << std::setw(2) << std::setfill('0')
					<< static_cast<int>(static_cast<unsigned char>(ch));
			}
		}

		return encoded.str();
	}

}
namespace str {
	bool isASCII(const std::string& str) {
		for (char c : str) {
			if (c < 0 || c > 127) {
				return false;
			}
		}
		return true;
	}

	bool isGB2312(const std::string& str) {
		size_t len = str.length();
		for (size_t i = 0; i < len; ++i) {
			unsigned char c = static_cast<unsigned char>(str[i]);
			// 检查是否为ASCII字符
			if (c <= 0x7F) {
				continue; // ASCII字符，继续检查下一个字符
			}
			else {
				if (c >= 0xA1 && c <= 0xFE) //gb2312
				{
					if (i + 1 < str.length())
					{
						int bNext = (int)(unsigned char)str.at(i + 1);
						if (bNext >= 0xA1 && bNext <= 0xFE)
						{
							return true;
						}
					}
				}
			}
		}
		return false;
	}

	bool isUTF8(const string& str) {
		for (size_t i = 0; i < str.size(); ) {
			if ((str[i] & 0b10000000) == 0b00000000) {
				i++;
			}
			else if ((str[i] & 0b11100000) == 0b11000000) {
				if (i + 1 >= str.size() || (str[i + 1] & 0b11000000) != 0b10000000) {
					return false;
				}
				i += 2;
			}
			else if ((str[i] & 0b11110000) == 0b11100000) {
				if (i + 2 >= str.size() || (str[i + 1] & 0b11000000) != 0b10000000 || (str[i + 2] & 0b11000000) != 0b10000000) {
					return false;
				}
				i += 3;
			}
			else if ((str[i] & 0b11111000) == 0b11110000) {
				if (i + 3 >= str.size() || (str[i + 1] & 0b11000000) != 0b10000000 || (str[i + 2] & 0b11000000) != 0b10000000 || (str[i + 3] & 0b11000000) != 0b10000000) {
					return false;
				}
				i += 4;
			}
			else {
				return false;
			}
		}
		return true;
	}

	std::string parseEscapeChar(string s)
	{
		s = str::replace(s, "\\n", "\n");
		s = str::replace(s, "\\r", "\r");
		s = str::replace(s, "\\t", "\t");
		return s;
	}


	


	string removeChar(string str, char c)
	{
		str.erase(std::remove(str.begin(), str.end(), c), str.end());
		return str;
	}
	string trimFloat(string str)
	{
		while (str.at(str.length() - 1) == '0')
		{
			str = str.substr(0, str.length() - 1);
		}
		if (str.at(str.length() - 1) == '.')
		{
			str = str.substr(0, str.length() - 1);
		}
		return str;
	}
	string fromFloat(double f)
	{
		string s;
		s = str::format("%f", f);
		s = str::trimFloat(s);
		return s;
	}

	vector<char> toChars(string str)
	{
		vector<char> bytes;
		str = removeChar(str, ' ');
		str = removeChar(str, '\t');
		str = removeChar(str, '\r');
		str = removeChar(str, '\n');

		if (0 != str.length() % 2)
		{
			str += "0";
		}

		size_t strLen = 0;
		strLen = str.length();

		for (size_t i = 0; i < strLen / 2; i++)
		{
			char cByteHigh = str.at(i * 2);
			char cByteLow = str.at(i * 2 + 1);
			cByteHigh = toupper(cByteHigh);
			cByteLow = toupper(cByteLow);
			int bHigh = 0, bLow = 0;
			if (cByteHigh >= 'A')
			{
				bHigh = cByteHigh - 'A' + 10;
			}
			else
			{
				bHigh = cByteHigh - '0';
			}

			if (cByteLow >= 'A')
			{
				bLow = cByteLow - 'A' + 10;
			}
			else
			{
				bLow = cByteLow - '0';
			}

			int val = (bHigh * 16 + bLow);
			unsigned char b = (unsigned char)val;
			bytes.push_back((char)b);
		}
		return bytes;
	}
	
	vector<unsigned char> toBytes(string str)
	{
		vector<char> vec = toChars(str);
		vector<unsigned char> vecB;
		for (size_t i = 0; i < vec.size(); i++)
		{
			unsigned char& b = *((unsigned char*)(&vec[i]));
			vecB.push_back(b);
		}
		return vecB;
	}
	
	string bytesToHexStr(vector<char>& bytes)
	{
		string str;
		for (size_t i = 0; i < bytes.size(); i++)
		{
			string b = format("%02X", (unsigned char)bytes[i]);
			str += b;
		}

		return str;
	}

	string bytesToHexStr(vector<unsigned char>& bytes)
	{
		string str;
		for (int i = 0; i < bytes.size(); i++)
		{
			string b = format("%02X", (unsigned char)bytes[i]);
			str += b;
		}

		return str;
	}

	bool isValidHexString(const std::string& str) {
		// 去除空格
		std::string hexStr = std::regex_replace(str, std::regex("\\s+"), "");

		// 检查字符数量是否为偶数
		if (hexStr.length() % 2 != 0) {
			return false;
		}

		// 检查是否只包含有效的16进制字符
		return std::regex_match(hexStr, std::regex("^[0-9a-fA-F]+$"));
	}

	vector<unsigned char> hexStrToBytes(string hexStr)
	{
		vector<unsigned char> ary;
		hexStr = str::removeChar(hexStr, ' ');
		if (0 != hexStr.length() % 2)
		{
			hexStr += "0";
		}
		size_t strLen = 0;
		strLen = hexStr.length();
		transform(hexStr.begin(), hexStr.end(), hexStr.begin(), ::toupper);

		for (size_t i = 0; i < strLen / 2; i++)
		{
			char cByteHigh = hexStr.at(i * 2);
			char cByteLow = hexStr.at(i * 2 + 1);
			int bHigh = 0, bLow = 0;
			if (cByteHigh >= 'A')
			{
				bHigh = cByteHigh - 'A' + 10;
			}
			else
			{
				bHigh = cByteHigh - '0';
			}

			if (cByteLow >= 'A')
			{
				bLow = cByteLow - 'A' + 10;
			}
			else
			{
				bLow = cByteLow - '0';
			}

			int val = (bHigh * 16 + bLow);
			unsigned char b = (unsigned char)val;
			ary.push_back((unsigned char)b);
		}
		return ary;
	}

	string bytesToHexStr(char* p, size_t len, string splitter)
	{
		string str;
		for (int i = 0; i < len; i++)
		{
			string b = format("%02X", (unsigned char)p[i]);
			str += b;
			str += splitter;
		}

		return str;
	}

	string bytesToHexStr(unsigned char* p, size_t len, string splitter)
	{
		return bytesToHexStr((char*)p, len, splitter);
	}

	string fromInt(int v)
	{
		string s = str::format("%d", v);
		return s;
	}

	string fromInt(long long v)
	{
		string s = str::format("%d", v);
		return s;
	}

	string fromInt(size_t v)
	{
		string s = str::format("%d", v);
		return s;
	}

	string fromBuff(unsigned char* p, size_t len)
	{
		return fromBuff((char*)p, len);
	}

	void fromBuff(unsigned char* p, size_t len,string& s)
	{
		s.resize(len);
		memcpy(&s[0], p, len);
	}

	void fromBuff(char* p, size_t len, string& s)
	{
		s.resize(len);
		memcpy(&s[0], p, len);
	}

	string fromBuff(const char* p, size_t len)
	{
		string s;
		s.resize(len);
		memcpy(&s[0], p, len);
		return s;
	}

	int toInt(string s)
	{
		return atoi(s.c_str());
	}

	string encodeAscII(const string& s)
	{
		string sDest = "";
		char c[2] = { 0 };
		for (int i = 0; i < s.length(); i++)
		{
			c[0] = s[i];
			if (isascii(c[0]))
			{
				sDest += c;
			}
			else
			{
				string hexView = format("\\0x%02X", (unsigned char)c[0]);
				sDest += hexView;
			}
		}
		return sDest;
	}

	bool isInteger(string s)
	{
		for (int i = 0; i < s.size(); i++)
		{
			if (s.at(i) < '0' || s.at(i) > '9')return false;
		}
		return true;
	}

	bool isIp(string s)
	{
		if (s == "localhost")
			return true;


		vector<string> v;
		str::split(v, s, ".");
		if (v.size() != 4)return false;
		for (int i = 0; i < v.size(); i++)
		{
			if (!isInteger(v.at(i)))
				return false;
		}
		for (int i = 0; i < v.size(); i++)
		{
			int n = toInt(v.at(i));
			if (n > 255)return false;
		}
		return true;
	}

	bool parseIpPort(string s, string& ip, int& port)
	{
		size_t ipos = s.find(":");
		if (ipos == string::npos)
			return false;

		string sip = s.substr(0, ipos);
		string sport = s.substr(ipos + 1, s.length() - ipos - 1);

		//if (!isIp(sip))
		//	return false;

		ip = sip;

		if (sport == "")
			return false;

		port = atoi(sport.c_str());

		return true;
	}


	bool In(wchar_t   start, wchar_t   end, wchar_t   code)
	{
		if (code >= start && code <= end)
		{
			return   true;
		}
		return   false;
	}

	char  getShenMu(wchar_t n)
	{
		if (In(0xB0A1, 0xB0C4, n))   return   'a';
		if (In(0XB0C5, 0XB2C0, n))   return   'b';
		if (In(0xB2C1, 0xB4ED, n))   return   'c';
		if (In(0xB4EE, 0xB6E9, n))   return   'd';
		if (In(0xB6EA, 0xB7A1, n))   return   'e';
		if (In(0xB7A2, 0xB8c0, n))   return   'f';
		if (In(0xB8C1, 0xB9FD, n))   return   'g';
		if (In(0xB9FE, 0xBBF6, n))   return   'h';
		if (In(0xBBF7, 0xBFA5, n))   return   'j';
		if (In(0xBFA6, 0xC0AB, n))   return   'k';
		if (In(0xC0AC, 0xC2E7, n))   return   'l';
		if (In(0xC2E8, 0xC4C2, n))   return   'm';
		if (In(0xC4C3, 0xC5B5, n))   return   'n';
		if (In(0xC5B6, 0xC5BD, n))   return   'o';
		if (In(0xC5BE, 0xC6D9, n))   return   'p';
		if (In(0xC6DA, 0xC8BA, n))   return   'q';
		if (In(0xC8BB, 0xC8F5, n))   return   'r';
		if (In(0xC8F6, 0xCBF0, n))   return   's';
		if (In(0xCBFA, 0xCDD9, n))   return   't';
		if (In(0xCDDA, 0xCEF3, n))   return   'w';
		if (In(0xCEF4, 0xD188, n))   return   'x';
		if (In(0xD1B9, 0xD4D0, n))   return   'y';
		if (In(0xD4D1, 0xD7F9, n))   return   'z';
		return   '\0';
	}

	bool hanZi2Pinyin(string hanZi,string& pinyin,bool upperCase)
	{
		string gbstr = charCodec::utf8_to_gb(hanZi);
		vector<char> vecPinyin;
		for (size_t i = 0; i < gbstr.length();)
		{
			int b = (int)(unsigned char)gbstr.at(i);
			if (b > 0 && b < 127) //ascii
			{
				vecPinyin.push_back((char)b);
				i++;
				continue;
			}
			else
			{
				if (b >= 0xA1 && b <= 0xFE) //gb2312
				{
					if (i + 1 < gbstr.length())
					{
						int bNext = (int)(unsigned char)gbstr.at(i + 1);
						if (bNext >= 0xA1 && bNext <= 0xFE)
						{
							wchar_t gbChar = b*256 + bNext;
							char shenMu = getShenMu(gbChar);
							vecPinyin.push_back(shenMu);
							i += 2;
							continue;
						}
						else
						{
							return false;
						}
					}
					else // invalid length
					{
						return false;
					}
				}
				else // wrong hex value
				{
					return false;
				}
			}
		}

	    pinyin = str::fromBuff(vecPinyin.data(), vecPinyin.size());

		if (upperCase) {
			transform(pinyin.begin(), pinyin.end(), pinyin.begin(), ::toupper);
		}

		return true;
	}


	int TwoHexStringToInt(const char* str) {
		return HexCharToInt(str[0]) * 16 + HexCharToInt(str[1]);
	}

	int HexCharToInt(char c) {
		if (c >= '0' && c <= '9') return c - '0';
		else if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return 0;
	}

	std::string remove_utf8_bom(const std::string& data) {
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
}
namespace timeopt {
	TIME Unix2SysTime(time_t iUnix,int milli)
	{
		static std::mutex mtx;
		mtx.lock();
		tm time_tm = *localtime(&iUnix);  //线程安全linux下推荐用localtime_r，win下推荐用localtime_s，此处为方便直接加个锁
		mtx.unlock();

		TIME t;
		t.wYear = time_tm.tm_year + 1900;
		t.wMonth = time_tm.tm_mon + 1;
		t.wDay = time_tm.tm_mday;
		t.wHour = time_tm.tm_hour;
		t.wMinute = time_tm.tm_min;
		t.wSecond = time_tm.tm_sec;
		t.wMilliseconds = milli;
		t.wDayOfWeek = time_tm.tm_wday;
		return t;
	}


	TIME now() {
		auto now = std::chrono::system_clock::now();
		//通过不同精度获取相差的毫秒数 <1000毫秒值
		unsigned short milli = (unsigned short)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
			- std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;
		time_t tt = std::chrono::system_clock::to_time_t(now);
		
		return Unix2SysTime(tt, milli);
	}

	TIME nowUTC() {
		auto now = std::chrono::system_clock::now();
		//通过不同精度获取相差的毫秒数 <1000毫秒值
		unsigned short dis_millseconds = (unsigned short)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
			- std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;
		time_t tt = std::chrono::system_clock::to_time_t(now);

		static mutex lock;
		lock.lock();
		tm time_tm = *gmtime(&tt);
		lock.unlock();

		TIME t;
		t.wYear = time_tm.tm_year + 1900;
		t.wMonth = time_tm.tm_mon + 1;
		t.wDay = time_tm.tm_mday;
		t.wHour = time_tm.tm_hour;
		t.wMinute = time_tm.tm_min;
		t.wSecond = time_tm.tm_sec;
		t.wMilliseconds = dis_millseconds;
		t.wDayOfWeek = time_tm.tm_wday;

		return t;
	}

	void now(TIME& t){
		t = now();
	}

	void now(TIME* t) {
		*t = now();
	}

	void sleepMilli(int milliSec)
	{
#ifdef _WIN32
		Sleep(milliSec);
#else
		//怀疑该函数在阿里云服务器会造成高cpu占用，win版本暂时改用Sleep
		std::this_thread::sleep_for(std::chrono::milliseconds(milliSec));
#endif
	}

	string toFmt(string time, string fmt)
	{
		//YYYY-MM-DD hh:mm:ss
		if (time.length() != 19) {
			return "";
		}
		string YYYY = time.substr(0, 4);
		string MM = time.substr(5, 2);
		string DD = time.substr(8, 2);
		string hh = time.substr(11, 2);
		string mm = time.substr(14, 2);
		string ss = time.substr(17, 2);

		string s = fmt;
		 s = str::replace(s, "YYYY", YYYY);
		 s = str::replace(s, "MM", MM);
		 s = str::replace(s, "DD", DD);
		 s = str::replace(s, "hh", hh);
		 s = str::replace(s, "mm", mm);
		 s = str::replace(s, "ss", ss);

		return s;
	}

	string stTimeToStr(TIME time)
	{
		string str = str::format("%4d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
		return str;
	}

	time_t SysTime2Unix(TIME sDT)
	{
		tm temptm = { sDT.wSecond, sDT.wMinute, sDT.wHour,
			sDT.wDay, sDT.wMonth - 1, sDT.wYear - 1900, sDT.wDayOfWeek, 0, 0 };
		time_t iReturn = mktime(&temptm);
		return iReturn;
	}


	TIME str2st(string str)
	{
		TIME t;
		memset(&t, 0, sizeof(t));
		int y, m, d, h, min, s, milli;
		if (str.length()>3 && str[2] != '-' && str[2] != ':') {
			//2023-12-31T16:00:00.000Z
			if (str.length() == 24) {
				sscanf(str.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%dZ",
					&y,
					&m,
					&d,
					&h,
					&min,
					&s,
					&milli);
				t.wYear = y; t.wMonth = m; t.wDay = d; t.wHour = h; t.wMinute = min; t.wSecond = s; t.wMilliseconds = milli;
			}
			//2022-02-22 11:11:11.123   23bytes
			else if (str.length() == 23) {
				sscanf(str.c_str(), "%4d-%2d-%2d %2d:%2d:%2d.%d",
					&y,
					&m,
					&d,
					&h,
					&min,
					&s,
					&milli);
				t.wYear = y; t.wMonth = m; t.wDay = d; t.wHour = h; t.wMinute = min; t.wSecond = s; t.wMilliseconds = milli;
			}
			//2022-02-22 11:11:11   19bytes
			else if (str.length() == 19)
			{
				sscanf(str.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
					&y,
					&m,
					&d,
					&h,
					&min,
					&s);
				t.wYear = y; t.wMonth = m; t.wDay = d; t.wHour = h; t.wMinute = min; t.wSecond = s;
			}
			//2022-02-22 11:11   16bytes
			else if (str.length() == 16)
			{
				sscanf(str.c_str(), "%4d-%2d-%2d %2d:%2d",
					&y,
					&m,
					&d,
					&h,
					&min);
				t.wYear = y; t.wMonth = m; t.wDay = d; t.wHour = h; t.wMinute = min;
			}
			else if (str.length() == 10) //2022-02-02
			{
				sscanf(str.c_str(), "%4d-%2d-%2d",
					&y,
					&m,
					&d);
				t.wYear = y; t.wMonth = m; t.wDay = d;
			}
			else if (str.length() == 8) //12:11:11
			{
				sscanf(str.c_str(), "%2d:%2d:%2d",
					&h,
					&min,
					&s);
				t.wHour = h; t.wMinute = min; t.wSecond = s;
			}
		}
		else if(str::isDigits(str)){
			time_t tt = atoi(str.c_str());
			t = Unix2SysTime(tt);
		}
		
		return t;
	}

	int HMS2Sec(string hms)
	{
		vector<string> v;
		str::split(v, hms, ":");
		int sec = atoi(v[0].c_str()) * 3600 + atoi(v[1].c_str()) * 60 + atoi(v[2].c_str());
		return sec;
	}



	unsigned long duration2sec(string strTime)
	{
		unsigned long dwSecond = 0;

		std::smatch m;
		std::regex e("([0-9]*)([d,h,m,s])");
		std::string strSrc = strTime;
		transform(strSrc.begin(), strSrc.end(), strSrc.begin(), ::tolower);

		while (std::regex_search(strSrc, m, e))
		{
			if (m.size() > 2)
			{
				string strNum = m[1];
				int nNum = atoi(strNum.c_str());
				if (m[2] == "d")
				{
					dwSecond += nNum * 24 * 3600;
				}
				else if (m[2] == "h")
				{
					dwSecond += nNum * 3600;
				}
				else if (m[2] == "m")
				{
					dwSecond += nNum * 60;
				}
				else if (m[2] == "s")
				{
					dwSecond += nNum;
				}

			}
			strSrc = m.suffix().str();
		}

		return dwSecond;
	}


	

	string st2strForFile(TIME t, bool enableMS)
	{
		if (enableMS) {
			string str = str::format("%.4d_%.2d_%.2d %.2d_%.2d_%.2d.%.3d",
				t.wYear, t.wMonth, t.wDay,
				t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
			return str;
		}
		else {
			string str = str::format("%.4d_%.2d_%.2d %.2d_%.2d_%.2d",
				t.wYear, t.wMonth, t.wDay,
				t.wHour, t.wMinute, t.wSecond);
			return str;
		}
	}

	string st2str(TIME t,bool enableMS)
	{
		if(enableMS){
			string str = str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",
			t.wYear, t.wMonth, t.wDay,
			t.wHour, t.wMinute, t.wSecond,t.wMilliseconds);
			return str;
		}
		else{
			string str = str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
				t.wYear, t.wMonth, t.wDay,
				t.wHour, t.wMinute, t.wSecond);
			return str;
		}
	}

	string st2strWithMilli(TIME t)
	{
		string str = str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",
			t.wYear, t.wMonth, t.wDay,
			t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		return str;
	}

	string TimeToYMD(const TIME time)
	{
		string str;
		if (time.wYear > 2000 && time.wDay > 0 && time.wDay < 40 && time.wHour >= 0 && time.wHour <= 24 && time.wMinute >= 0 && time.wMinute <= 60)
		{
			str = str::format("%.4d-%.2d-%.2d", time.wYear, time.wMonth, time.wDay);
		}
		return str;
	}

	string TimeToHMS(const TIME time)
	{
		string str;
		if (time.wYear > 2000 && time.wDay > 0 && time.wDay < 40 && time.wHour >= 0 && time.wHour <= 24 && time.wMinute >= 0 && time.wMinute <= 60)
		{
			str = str::format("%.2d:%.2d:%.2d", time.wHour, time.wMinute, time.wSecond);
		}
		return str;
	}
	string TimeToHMSForFile(const TIME time)
	{
		string str;
		str = str::format("%.2d%.2d%.2d", time.wHour, time.wMinute, time.wSecond);
		return str;
	}


	time_t calcTimePassSecond(string lastTime)
	{
		TIME t = timeopt::str2st(lastTime);
		time_t last = SysTime2Unix(t);
		time_t now = time(NULL);
		time_t milli = now - last;
		return milli;
	}

	time_t CalcTimePassSecond(TIME lastTime)
	{
		time_t last = SysTime2Unix(lastTime);
		time_t now = time(NULL);
		time_t milli = now - last;
		return milli;
	}

	time_t CalcTimeDiffSecond(TIME newTime,TIME oldTime)
	{
		time_t newT = SysTime2Unix(newTime);
		time_t oldT = SysTime2Unix(oldTime);
		time_t seconds = newT - oldT;
		return seconds;
	}

	long long CalcTimePassMilliSecond(TIME lastTime)
	{
		time_t last = SysTime2Unix(lastTime);
		TIME stNow;
		timeopt::now(&stNow);
		time_t now = SysTime2Unix(stNow);
		time_t second = now - last;
		time_t milli = stNow.wMilliseconds - lastTime.wMilliseconds;
		milli = second * 1000 + milli;
		return milli;
	}

	long long calcTimePassMilliSecond(string lastTime)
	{
		TIME last = str2st(lastTime);
		return CalcTimePassMilliSecond(last);
	}

	long long calcTimePassMilliSecond(TIME lastTime)
	{
		TIME nowTime = now();
		time_t now = SysTime2Unix(nowTime);
		time_t last = SysTime2Unix(lastTime);
		time_t second = now - last;
		time_t milli = nowTime.wMilliseconds - lastTime.wMilliseconds;
		milli = second * 1000 + milli;
		return milli;
	}

	long long calcTimePassMilliSecond(TIME lastTime, TIME nowTime)
	{
		time_t now = SysTime2Unix(nowTime);
		time_t last = SysTime2Unix(lastTime);
		time_t second = now - last;
		time_t milli = nowTime.wMilliseconds - lastTime.wMilliseconds;
		milli = second * 1000 + milli;
		return milli;
	}

	time_t getTick() {
		std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> tp =
			std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
		auto tmp = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
		time_t timestamp = tmp.count();
		return timestamp;
	}

	void setAsTimeOrg(TIME& st)
	{
		memset(&st, 0, sizeof(TIME));
		st.wYear = 1970;
		st.wMonth = 1;
		st.wDay = 1;
	}

	bool isValidTime(TIME& st)
	{
		if (st.wYear == 0 || st.wYear == 1970)
			return false;
		return true;
	}

	string nowStr(bool enableMS)
	{
		TIME t = now();

		return st2str(t,enableMS);
	}

	string nowStrIso() {
		TIME t = nowUTC();
		string s = str::format("%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		return s;
	}

	string nowStrForFile(bool enableMS)
	{
		TIME t = now();
		return st2strForFile(t, enableMS);
	}

	TIME addTime(TIME base, int h, int m, int s) {
		time_t tBase = SysTime2Unix(base);
		tBase += h * 3600 + m * 60 + s;
		TIME st = Unix2SysTime(tBase);
		return st;
	}

	bool isValidTimeStr(string time) {
		if (time.length() != 19 && time.length() != 23)
			return false;
		if (time.at(4) != '-' || time.at(7) != '-')
			return false;
		if (time.at(13) != ':' || time.at(16) != ':')
			return false;
		if (time.length() == 23 && time.at(19) != '.')
			return false;

		return true;
	}
}


namespace sys {
	vector<COM_INFO> getCOMInfoList() {
		vector<COM_INFO> ary;
#ifdef ENABLE_SERIAL
		HDEVINFO hDevInfo;
		SP_DEVINFO_DATA DeviceInfoData;
		DWORD i = 0;
		hDevInfo = SetupDiGetClassDevsW((LPGUID)&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
		/*
		GUID_DEVCLASS_FDC软盘控制器
		GUID_DEVCLASS_DISPLAY显示卡
		GUID_DEVCLASS_CDROM光驱
		GUID_DEVCLASS_KEYBOARD键盘
		GUID_DEVCLASS_COMPUTER计算机
		GUID_DEVCLASS_SYSTEM系统
		GUID_DEVCLASS_DISKDRIVE磁盘驱动器
		GUID_DEVCLASS_MEDIA声音、视频和游戏控制器
		GUID_DEVCLASS_MODEMMODEM
		GUID_DEVCLASS_MOUSE鼠标和其他指针设备
		GUID_DEVCLASS_NET网络设备器
		GUID_DEVCLASS_USB通用串行总线控制器
		GUID_DEVCLASS_FLOPPYDISK软盘驱动器
		GUID_DEVCLASS_UNKNOWN未知设备
		GUID_DEVCLASS_SCSIADAPTERSCSI 和 RAID 控制器
		GUID_DEVCLASS_HDCIDE ATA/ATAPI 控制器
		GUID_DEVCLASS_PORTS端口（COM 和 LPT）
		GUID_DEVCLASS_MONITOR监视器
		*/

		if (hDevInfo == INVALID_HANDLE_VALUE)
		{
			DWORD dwError = GetLastError();
			// Insert error handling here.   
			return ary;
		}

		// Enumerate through all devices in Set.        
		DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
		for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData); i++)
		{
			DWORD DataT = 0;
			WCHAR buffer[256] = { 0 };
			DWORD buffersize = sizeof(buffer);

			while (!SetupDiGetDeviceRegistryPropertyW(hDevInfo,
				&DeviceInfoData,
				SPDRP_FRIENDLYNAME,
				&DataT,
				(PBYTE)buffer,
				buffersize,
				&buffersize))
			{
				if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
				{
					// Change the buffer size.   
					//if (buffer) LocalFree(buffer);   
				}
				else
				{
					// Insert error handling here. 
					break;
				}
			}

			wstring utf16str = buffer;
			string comInfo = charCodec::utf16_to_tds(utf16str);

			int iLeftBracket = comInfo.find("(");
			if (iLeftBracket == string::npos)
				continue;

			size_t iRightBracket = comInfo.find(")");

			string portNum = comInfo.substr(iLeftBracket + 1, iRightBracket - iLeftBracket - 1);

			//vspd 创建的虚拟串口是  COM1->COM2的格式
			size_t iFPos = portNum.find("->");
			if (iFPos != string::npos)
			{
				portNum = portNum.substr(iFPos + 2, portNum.size() - iFPos - 2);
			}

			COM_INFO ci;
			ci.portNum = portNum;
			ci.desc = comInfo.substr(0, iLeftBracket);

			ary.insert(ary.begin(), ci);


			//if (buffer)                                                                            
			//{
			//	LocalFree(buffer);
			//}
		}
		if (GetLastError() != NO_ERROR && GetLastError() != ERROR_NO_MORE_ITEMS)
		{
			return ary;
		}

		// Cleanup   
		SetupDiDestroyDeviceInfoList(hDevInfo);
#endif
		return ary;
	}
	string getLastError(string szReason)
	{
		string szErrMsg = "";
#ifdef WINDOWS
		DWORD dwErrCode = GetLastError(); //之前的错误代码

		LPVOID lpMsgBuf = NULL;
		DWORD dwLen = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dwErrCode,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPWSTR)&lpMsgBuf,
			0,
			NULL
		);



		if (dwLen == 0)
		{
			DWORD dwFmtErrCode = GetLastError(); //FormatMessage 引起的错误代码
			szErrMsg = str::format("FormatMessage failed with %u\n", dwFmtErrCode);
		}

		if (lpMsgBuf)
		{
			wstring utf16msg = (LPWSTR)lpMsgBuf;
			string utf8Msg = charCodec::utf16_to_tds(utf16msg);
			szErrMsg = str::format("%s\n Code = %u, Mean = %s", szReason.c_str(), dwErrCode, utf8Msg.c_str());
		}

		if (lpMsgBuf)
		{
			// Free the buffer.
			LocalFree(lpMsgBuf);
			lpMsgBuf = NULL;
		}
#endif

		return szErrMsg;
	}

	unsigned long getThreadId() {
#ifdef _WIN32
		return GetCurrentThreadId();
#else
		// TODO: linux
		return 0;
#endif
	}
}


namespace fs {
	string normalizationPath(string& s)
	{
		s = str::replace(s, "\\\\", "/");
		s = str::replace(s, "\\", "/");
		s = str::replace(s, "//", "/");
		return s;
	}
	//带后缀 .XXX 作为文件路径
	//不带后缀作为文件夹路径。不要输入无后缀的文件路径
	//filesystem::path 统一用 wstring utf16输入，可以做到windows与linux兼容
	bool createFolderOfPath(string strFile)
	{
		strFile = str::replace(strFile, "\\", "/");
		strFile = str::replace(strFile, "////", "/");
		strFile = str::replace(strFile, "///", "/");
		strFile = str::replace(strFile, "//", "/");

		size_t iDotPos = strFile.rfind('.');
		size_t iSlashPos = strFile.rfind('/');
		if (iDotPos!= string::npos && iDotPos > iSlashPos)//是一个文件
		{
			strFile = strFile.substr(0, iSlashPos);
		}
		//如果路径的末尾是/，创建成功也会返回false,因此删除末尾的 /
		if (iSlashPos == strFile.length() - 1) {
			strFile = strFile.substr(0, iSlashPos);
		}

#ifdef _WIN32
	#ifndef _WINXP
		return filesystem::create_directories(charCodec::tds_to_utf16(strFile));
	#endif
#else
		filesystem::path p = strFile;
		return filesystem::create_directories(p);
#endif
	}

	bool copy(string src,string dest) {
		return true;
	}

	string appPath()
	{
		string str;

		//不要使用std::filesystem::current_path(),这个是当前运行目录，和程序目录可能不一致

#ifdef _WIN32
		//windows获取到的是反斜杠，tds内统一使用斜杠
		TCHAR p[MAX_PATH] = { 0 };
		GetModuleFileName(NULL, p, MAX_PATH);//获取可执行模块的路径

		string strPath = (char*)p;
		size_t nEnd = strPath.rfind('\\');//取最后的"\"号之前地址

		strPath = strPath.substr(0, nEnd);
		strPath = charCodec::gb_to_tds(strPath);

		str = str::replace(strPath, "\\", "/");
#else 
		char* p = NULL;
		const int len = 256;

		/// to keep the absolute path of executable's path
		char arr_tmp[len] = { 0 };
		int n = readlink("/proc/self/exe", arr_tmp, len);

		if (NULL != (p = strrchr(arr_tmp, '/'))) {
			*p = '\0';
		}
		else {
			return std::string("");
		}

		str = arr_tmp;
#endif

		return str;
	}

	string appName()
	{
		string str;
#ifdef _WIN32
		//windows获取到的是反斜杠，tds内统一使用斜杠
		TCHAR p[MAX_PATH] = { 0 };
		GetModuleFileName(NULL, p, MAX_PATH);//获取可执行模块的路径
		string strPath = (char*)p;
		size_t nEnd = strPath.rfind('\\');//取最后的"\"号之前地址
		str = strPath.substr(nEnd+1,strPath.length() - nEnd - 1 );
		str = charCodec::gb_to_tds(str);
		str = str::trimSuffix(str,".exe");
#else
		char* p = NULL;
		const int len = 256;
		/// to keep the absolute path of executable's path
		char arr_tmp[len] = { 0 };
		int n = readlink("/proc/self/exe", arr_tmp, len);
		if (NULL != (p = strrchr(arr_tmp, '/')))
		{
			str = p+1;
		}
		else
		{
			return std::string("");
		}
#endif
		return str;
	}

	string toAbsolutePath(string path, string currentPath) {
		if (path.empty()) {
			return currentPath;
		}
		if (currentPath.empty()) {
			return path;
		}
		if (path[0] == '/') { //linux 
			return path;
		}
		if (path.size() >= 2 && path[1] == ':') { //windows
			return path;
		}
		string absPath = currentPath;
		if (absPath.back() != '/') {
			absPath += '/';
		}
		int i = 0;
		while (i < path.size()) {
			if (path[i] == '.') {
				if (i + 1 == path.size() || path[i + 1] == '/') {
					i += 2;
					continue;
				}
				if (i + 2 == path.size() || (path[i + 1] == '.' && path[i + 2] == '/')) {
					i += 3;
					int j = absPath.size() - 2;
					while (j >= 0 && absPath[j] != '/') {
						--j;
					}
					absPath.erase(j + 1);
					continue;
				}
			}
			int j = i + 1;
			while (j < path.size() && path[j] != '/') {
				++j;
			}
			absPath += path.substr(i, j - i) + '/';
			i = j + 1;
		}
		if (!absPath.empty() && absPath.back() == '/') {
			absPath.pop_back();
		}
		return absPath;
	}

	string toAbsolutePath(string str)
	{
/*
		char absPath[1024] = { 0 };
#ifdef _WIN32
		str = charCodec::tds_to_gb(str);
		_fullpath(absPath, str.c_str(), 1024);
		str = absPath;
		str = str::replace(str, "\\", "/");
		str = charCodec::gb_to_tds(str);
#else
		realpath((char*)str.data(),absPath);
#endif
		return str;
*/
		string cP = fs::appPath();
		return toAbsolutePath(str, cP);
	}
	string getExt(string strFilePath)
	{
		size_t pos = strFilePath.rfind(".");
		if (pos != strFilePath.npos)
		{
			string str = strFilePath.substr(pos, strFilePath.length() - pos);
			return str;
		}
		return "";
	}
	bool readFile(string path, char*& pData, int& len)
	{
		FILE* fp = nullptr;
#ifdef _WIN32
		_wfopen_s(&fp,charCodec::tds_to_utf16(path).c_str(), L"rb");
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
	bool readFile(string path, unsigned char*& pData, int& len)
	{
		char* p = nullptr;
		bool bRet = readFile(path, p, len);
		pData = (unsigned char*)p;
		return bRet;
	}
	bool readFile(string path, string& data)
	{
		FILE* fp = nullptr;
#ifdef _WIN32
		_wfopen_s(&fp,charCodec::tds_to_utf16(path).c_str(), L"rb");
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
	bool writeFile(string path, unsigned char* data, size_t len)
	{
		return writeFile(path, (char*)data, len);
	}
	bool writeFile(string path, char* data, size_t len)
	{
		fs::createFolderOfPath(path);

		FILE* fp = nullptr;
#ifdef _WIN32
		wstring wpath = charCodec::tds_to_utf16(path);
		_wfopen_s(&fp,wpath.c_str(), L"wb");
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
#ifdef _WIN32
			string info = str::format("writeFile,path=%s,len=%d", path.c_str(), len);
			string err = sys::getLastError(info);
			err = charCodec::tds_to_gb(err);
			printf("[error]%s", err.c_str());
#endif
		}
		return false;
	}

	bool appendFile(string path, char* data, size_t len)
	{
		FILE* fp = nullptr;
#ifdef _WIN32
		_wfopen_s(&fp,charCodec::tds_to_utf16(path).c_str(), L"ab");
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

	bool appendFile(string path, string data)
	{
		return appendFile(path, (char*)data.data(), data.length());
	}
	bool writeFile(string path, string& data)
	{
		return writeFile(path, (char*)data.c_str(), data.length());
	}
	bool fileExist(string pszFileName)
	{
#ifndef _WINXP
#ifdef _WIN32
		std::filesystem::path filePath = charCodec::tds_to_utf16(pszFileName);
#else
		std::filesystem::path filePath = pszFileName;
#endif

		if (std::filesystem::exists(filePath)) {
			return true;
		}
		else if(std::filesystem::is_directory(filePath)){
			return true;
		}
		return  false;
#else
		wstring filePath = charCodec::tds_to_utf16(pszFileName);
		DWORD fileAttributes = GetFileAttributesW(filePath.c_str());
		return (fileAttributes != INVALID_FILE_ATTRIBUTES && !(fileAttributes & FILE_ATTRIBUTE_DIRECTORY));
#endif
	}

	bool deleteFile(string path) {
#ifndef _WINXP
		bool ret;
		try
		{
			ret = std::filesystem::remove(charCodec::tds_to_utf16(path));
		}
		catch (const std::exception& e)
		{
			std::cerr << "std::filesystem::remove error: " << e.what() << std::endl;
			return false;
			ret = false;
		}
		return ret;
#else
		std::wstring filePath = charCodec::tds_to_utf16(path);

		if (DeleteFileW(filePath.c_str())) {
			return true;
		}
		else {
			return false;
		}
#endif
	}

	bool copyFile(const std::string& src, const std::string& dest) {
#ifdef _WIN32
		// 在 Windows 上，将 UTF-8 路径转换为 UTF-16
		std::wstring src_wstr = charCodec::utf8_to_utf16(src);
		std::wstring dest_wstr = charCodec::utf8_to_utf16(dest);

		std::ifstream srcFile(src_wstr, std::ios::binary);
		if (!srcFile) {
			std::cerr << "Failed to open source file: " << src << std::endl;
			return false;
		}

		std::ofstream destFile(dest_wstr, std::ios::binary);
		if (!destFile) {
			std::cerr << "Failed to open destination file: " << dest << std::endl;
			return false;
		}
#else
		std::ifstream srcFile(src, std::ios::binary);
		if (!srcFile) {
			std::cerr << "Failed to open source file: " << src << std::endl;
			return false;
		}

		std::ofstream destFile(dest, std::ios::binary);
		if (!destFile) {
			std::cerr << "Failed to open destination file: " << dest << std::endl;
			return false;
		}
#endif


		destFile << srcFile.rdbuf();

		if (!destFile) {
			std::cerr << "Failed to write to destination file: " << dest << std::endl;
			return false;
		}

		srcFile.close();
		destFile.close();

		return true;
	}

	bool deleteFolder(string path) {
		try {
			wstring wsPath = charCodec::tds_to_utf16(path);
			if (std::filesystem::exists(wsPath) && std::filesystem::is_directory(wsPath)) {
				std::filesystem::remove_all(wsPath);
				return true;
			}
			else {
				return false;
			}
		}
		catch (const std::filesystem::filesystem_error& e) {
			std::cerr << "Filesystem error: " << e.what() << std::endl;
			return false;
		}
	}

	float getFolderSizeMB(const std::string& folder)
	{
		wstring wsFolder = charCodec::tds_to_utf16(folder);
		uintmax_t total_size = 0;
		if (std::filesystem::exists(wsFolder) && std::filesystem::is_directory(wsFolder)) {
			for (const auto& entry : std::filesystem::recursive_directory_iterator(wsFolder)) {
				if (std::filesystem::is_regular_file(entry)) {
					total_size += std::filesystem::file_size(entry);
				}
			}
		}
		return total_size / (1024 * 1024);
	}

	float getFreeDiskSizeGB(const std::string& path)
	{
		wstring wsPath = charCodec::tds_to_utf16(path);
		std::filesystem::space_info si = std::filesystem::space(wsPath);
		return si.available / (1024 * 1024 * 1024);
	}


	 void getFolderList(vector<fs::FILE_INFO>& list, string strFolder, bool recursive) {
#ifndef _WINXP
		 try
		 {
			 wstring wstrFolder = charCodec::tds_to_utf16(strFolder);
			 for (auto& i : filesystem::directory_iterator(wstrFolder)) {
				 if (i.is_directory()) {
					 FILE_INFO fi;
					 fi.path = charCodec::gb_to_tds(i.path().string());
					 fi.path = str::replace(fi.path, "\\", "/");
					 size_t pos = fi.path.rfind("/");
					 fi.folderPath = fi.path.substr(0, pos);
					 fi.name = fi.path.substr(pos + 1, fi.path.length() - pos - 1);
					 fi.len = i.file_size();
					 auto ftime = i.last_write_time();
					 // 将 file_time_type 转换为 system_clock::time_point
					 auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
						 ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now()
						 );
					 auto ti = std::chrono::system_clock::to_time_t(sctp);
					 std::stringstream ss;
					 ss << std::put_time(std::localtime(&ti), "%Y-%m-%d %H:%M:%S");
					 fi.modifyTime = ss.str();

					 list.push_back(fi);

					 if (recursive) {
						 getFolderList(list, charCodec::gb_to_tds(i.path().string()), recursive);
					 }
				 }
			 }
		 }
		 catch (exception&) {
		 }
#else
#endif
	 }


	 void getFileList(vector<fs::FILE_INFO>& list, string strFolder, bool recursive, string suffix, vector<string>* exclude){
#ifndef _WINXP
		 try
		 {
			 wstring wstrFolder = charCodec::tds_to_utf16(strFolder); 
			 for (auto& i : filesystem::directory_iterator(wstrFolder)) {
				 FILE_INFO fi;
				 fi.path = charCodec::gb_to_tds(i.path().string());
				 fi.name = charCodec::gb_to_tds(i.path().filename().string());
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

				 if (i.is_directory()) {
					 if (recursive) {
						 getFileList(list, charCodec::gb_to_tds(i.path().string()), recursive, suffix,exclude);
					 } 
				 }
				 else {
					 //std::filesystem::file_time_type ft = i.last_write_time();
					 //std::time_t tt = decltype(ft)::clock::to_time_t();
					 fi.path = str::replace(fi.path, "\\", "/");
					 if (suffix != "*" && fi.path.find(suffix) == string::npos)
						 continue;
					 size_t pos = fi.path.rfind("/");
					 fi.folderPath = fi.path.substr(0, pos);
					 fi.len = i.file_size();
					 auto ftime = i.last_write_time();
					 // 将 file_time_type 转换为 system_clock::time_point
					 auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
						 ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now()
						 );
					auto ti = std::chrono::system_clock::to_time_t(sctp);
					 std::stringstream ss;
					 ss << std::put_time(std::localtime(&ti), "%Y-%m-%d %H:%M:%S");
					 fi.modifyTime = ss.str();
					 list.push_back(fi);
				 }
			 }
		 }	
		 catch (exception&) {
		 }
#else
#endif
	}


	 void getFileList(vector<string>& list, string strFolder, bool includeFolder, bool recursive)
	 {
		 vector<fs::FILE_INFO> filist;
		 getFileList(filist, strFolder, recursive);
		 for (int i = 0; i < filist.size(); i++) {
			 fs::FILE_INFO& fi = filist[i];
			 list.push_back(fi.path);
		 }
	 }
}

namespace common {
	string uuid() {
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<> dis(0, 15);
		std::string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
		int pos = 0;
		for (char& c : uuid) {
			if (c == 'x' || c == 'y') {
				int n = dis(gen);
				int r = (n & 0x3) | 0x8;
				c = (c == 'x') ? "0123456789abcdef"[n] : "89ab"[r];
			}
			++pos;
		}
		return uuid;
	}

	double randomFloat(double min, double max) {
		TIME st;
		timeopt::now(&st);
		//当前毫秒作为随机数种子
		int seed = abs(st.wMilliseconds - rand() % 1000);
		seed = seed % 100;
		double diffRate = double(seed) / 100.0;
		double diff = (max - min)*diffRate;
		double v = min + diff;
		return v;
	}

	int randomInt(int min, int max) {
		TIME st;
		timeopt::now(&st);
		//当前毫秒作为随机数种子
		int seed = abs(st.wMilliseconds - rand() % 1000);
		seed = seed % 100;
		double diffRate = double(seed) / 100.0;
		double diff = (max - min) * diffRate;
		double v = min + diff;
		return (int)v;
	}
}


namespace buffer {
	unsigned char setBit(unsigned char byte, int bitIdx, int val) {
		if (val) {
			unsigned char byteSet = 1 << bitIdx;
			byte |= byteSet;
		}
		else {
			unsigned char byteSet = 1 << bitIdx;
			byteSet = 0xFF - byteSet;
			byte &= byteSet;
		}
		return byte;
	}
	unsigned char setBit(unsigned char byte, int bitIdx, bool val) {
		return setBit(byte, bitIdx, val ? 1 : 0);
	}
}

#include <stdio.h>

void setThreadName2(string name)
{
#ifdef _DEBUG
	//SetThreadDescription(GetCurrentThread(), charCodec::utf8toUtf16(name).c_str());
#endif
}

void setThreadName(string name)
{
	setThreadName2(name);
}


bool TAG::hasTag(json& tree, string tag)
{
	vector<string> nodeNames;
	split(nodeNames, tag, ".");
	json* node = &tree;
	for (int i = 0; i < nodeNames.size(); i++)
	{
		string name = nodeNames[i];


		//查找子节点中有没有是 指定name的节点。如果有node指向该节点，继续查找node的子节点中是否有下一个name
		if ((*node)["children"] == nullptr)
			return false;
		json& jChildren = (*node)["children"];
		bool bHaveChild = false;
		if (jChildren.is_array())//具体指定
		{
			for (int j = 0; j < jChildren.size(); j++)
			{
				json& child = jChildren[j];
				if (child["name"].get<string>() == name)
				{
					bHaveChild = true;
					node = &child;
				}
			}
		}
		else if (jChildren.is_string() && jChildren.get<string>() == "*") //通配符指定，所有子节点
		{
			return true;
		}

		if (!bHaveChild)return false;
	}

	return true;
}

json TAG::mapTree2List(json mapTree)
{
	for (auto& [k, v] : mapTree.items())
	{

	}

	return json();
}

void mg_http_data::reset() {
	head.clear();
	body.clear();
	done = false;
	status = 0;
}

void mg_connect_fn(struct mg_connection* connect, int ev, void* ev_data) {
	mg_http_data* data = (mg_http_data*)connect->fn_data;
	if (ev == MG_EV_HTTP_MSG) {
		struct mg_http_message* hm = (struct mg_http_message*)ev_data;

		data->head.assign(hm->head.ptr, hm->head.len);
		data->body.assign(hm->body.ptr, hm->body.len);
		data->status = mg_http_status(hm);

		data->done = true;
		connect->is_closing = 1;
	}
	else if (ev == MG_EV_ERROR) {
		data->done = true;
		connect->is_closing = 1;
	}
}

std::string mg_get_url_encode(const std::string& str) {
	const char* in = str.c_str();
	size_t inLen = strlen(in);
	size_t outLen = 3 * inLen + 1;
	char* out = new char[outLen];

	size_t resultLen = mg_url_encode(in, inLen, out, outLen);
	std::string result(out, resultLen);

	delete[] out;
	return result;
}

std::string mg_get_header_value(const std::string& headers, const std::string& key) {
	std::istringstream stream(headers);

	std::string line;
	std::string key_lower = key;

	std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);

	while (std::getline(stream, line)) {
		auto pos = line.find(':');
		if (pos != std::string::npos) {
			std::string header_key = line.substr(0, pos);
			std::string header_val = line.substr(pos + 1);

			header_key.erase(header_key.find_last_not_of(" \t\r\n") + 1);
			header_val.erase(0, header_val.find_first_not_of(" \t\r\n"));

			std::string header_key_lower = header_key;
			std::transform(header_key_lower.begin(), header_key_lower.end(), header_key_lower.begin(), ::tolower);

			if (header_key_lower == key_lower) {
				return header_val;
			}
		}
	}

	return "";
}

static const char base64_chars[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

std::string mg_base64_encode(const std::string& in) {
	std::string out;
	int val = 0, valb = -6;
	for (unsigned char c : in) {
		val = (val << 8) + c;
		valb += 8;
		while (valb >= 0) {
			out.push_back(base64_chars[(val >> valb) & 0x3F]);
			valb -= 6;
		}
	}
	if (valb > -6) out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
	while (out.size() % 4) out.push_back('=');
	return out;
}