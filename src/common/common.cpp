#include "common.h"
#include <filesystem>
#include <stdarg.h>
#include <random>
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
		delete pNew;
	}
}


int _vscprintf_cross(const char* format, va_list pargs) {
	int retval;
	va_list argcopy;
	va_copy(argcopy, pargs);
	retval = vsnprintf(NULL, 0, format, argcopy);
	va_end(argcopy);
	return retval;
}

namespace str {
	std::string format(const char* pszFmt, ...)
	{
		std::string str;
		va_list args;
		va_start(args, pszFmt);
		{
			int nLength = _vscprintf_cross(pszFmt, args);
			nLength += 1;  //上面返回的长度是包含\0，这里加上
			std::vector<char> vectorChars(nLength);
			vsnprintf(vectorChars.data(), nLength, pszFmt, args);
			str.assign(vectorChars.data());
		}
		va_end(args);
		return str;
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
		delete charstr;
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
		delete charstr;
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
		delete wcharstr;
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
		delete wcharstr;
		delete charstr;
#else
		int ret = 0;
		size_t inlen = instr.size() + 1;
		size_t outlen = 2*inlen;

		// duanqn: The iconv function in Linux requires non-const char *
		// So we need to copy the source string
		char* inbuf = (char*)malloc(inlen);
		memset(inbuf,0,inlen);
		char* inbuf_hold = inbuf;   // iconv may change the address of inbuf
									// so we use another pointer to keep the address
		memcpy(inbuf, instr.data(), instr.length());

		char* outbuf =(char*)malloc(outlen);
		memset(outbuf, 0, outlen);
		iconv_t cd;
		cd = iconv_open("GBK", "UTF-8");
		if (cd != (iconv_t)-1) {
			ret = iconv(cd, &inbuf, &inlen, &outbuf, &outlen);
			if (ret != 0) {
				printf("iconv failed err: %s\n", strerror(errno));
			}

			iconv_close(cd);
		}
		free(inbuf_hold);   // Don't pass in inbuf as it may have been modified

		if(outbuf!=nullptr){
			str = outbuf;
			free(outbuf);
		}
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
		delete wcharstr;
		delete charstr;
#else
		int ret = 0;
		size_t inlen = instr.length() + 1;
		size_t outlen = 2*inlen;

		// duanqn: The iconv function in Linux requires non-const char *
		// So we need to copy the source string
		char* inbuf = (char*)malloc(inlen);
		char* inbuf_hold = inbuf;   // iconv may change the address of inbuf
									// so we use another pointer to keep the address
		memcpy(inbuf, instr.data(), instr.length());

		char* outbuf = (char*)malloc(outlen);
		memset(outbuf, 0, outlen);
		iconv_t cd;

		cd = iconv_open("UTF-8", "GBK");
		if (cd != (iconv_t)-1) {
			ret = iconv(cd, &inbuf, &inlen, &outbuf, &outlen);
			if (ret != 0)
				printf("iconv failed err: %s\n", strerror(errno));
			iconv_close(cd);
		}
		free(inbuf_hold);   // Don't pass in inbuf as it may have been modified
		str = outbuf;
		free(outbuf);
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
}
namespace str {

	string trimPrefix(string s, string prefix)
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

	string trimSuffix(string s, string suffix)
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

	string trim(std::string s, string toTrim)
	{
		s = trimPrefix(s, toTrim);
		s = trimSuffix(s, toTrim);
		return s;
	}


	string replace(string str, const string to_replaced, const string newchars)
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

	bool isDigits(char* pData, int len) {
		for (int i = 0; i < len; i++)
		{
			char c = pData[i];
			if (c >= '0' && c <= '9')
			{
				continue;
			}
			else
			{
				return false;
			}
		}
		return true;
	}

	bool isDigits(string s)
	{
		for (int i = 0; i < s.length(); i++)
		{
			char c = s[i];
			if (c >= '0' && c <= '9')
			{
				continue;
			}
			else
			{
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


	

	int split(std::vector<std::string>& dst, const std::string& src, std::string separator)
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

	string encodeAscII(string s)
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
		std::this_thread::sleep_for(std::chrono::milliseconds(milliSec));
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
		//2022-02-22 11:11:11.123   23bytes
		int y, m, d, h, min, s, milli;
		if (str[2] != '-' && str[2] != ':') {
			if (str.length() == 23) {
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
		else {
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

	bool isRelative(string time)
	{
		if (time.find("d") != string::npos || time.find("h") != string::npos
			|| time.find("m") != string::npos || time.find("s") != string::npos ||
			time.find("D") != string::npos || time.find("H") != string::npos
			|| time.find("M") != string::npos || time.find("S") != string::npos)
		{
			return true;
		}
		return false;
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

	int dhmsSpan2Seconds(string timeSpan) {
		string time1 = timeSpan;
		string strDay = "", strH = "", strM = "", strS = "";
		int n1 = 0, n2 = 0, n3 = 0, n4 = 0;
		size_t pos = time1.find("d");
		if (pos == string::npos)
			pos = time1.find("D");
		if (pos != string::npos) {
			strDay = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n1 = (int) (atof(strDay.c_str()) * 24 * 3600);
		}
		pos = time1.find("h");
		if (pos == string::npos)
			pos = time1.find("H");
		if (pos != string::npos) {
			strH = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n2 = (int)(atof(strH.c_str()) * 3600);
		}
		pos = time1.find("m");
		if (pos == string::npos)
			pos = time1.find("M");
		if (pos != string::npos) {
			strM = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n3 = (int)(atof(strM.c_str()) * 60);
		}
		pos = time1.find("s");
		if (pos == string::npos)
			pos = time1.find("S");
		if (pos != string::npos) {
			strS = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n4 = (int)atof(strS.c_str());
		}

		return n1 + n2 + n3 + n4;
	}


	string rel2abs(string time)
	{
		string strTime1 = time;
		if (isRelative(time)) {
			//相对时间区间模式
			string time1 = strTime1;
			string strDay = "", strH = "", strM = "", strS = "";
			int n1 = 0, n2 = 0, n3 = 0, n4 = 0;
			size_t pos = time1.find("d");
			if (pos == string::npos)
				pos = time1.find("D");
			if (pos != string::npos) {
				strDay = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n1 = (int)atof(strDay.c_str()) * 24 * 3600;
			}
			pos = time1.find("h");
			if (pos == string::npos)
				pos = time1.find("H");
			if (pos != string::npos) {
				strH = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n2 = (int)atof(strH.c_str()) * 3600;
			}
			pos = time1.find("m");
			if (pos == string::npos)
				pos = time1.find("M");
			if (pos != string::npos) {
				strM = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n3 = (int)atof(strM.c_str()) * 60;
			}
			pos = time1.find("s");
			if (pos == string::npos)
				pos = time1.find("S");
			if (pos != string::npos) {
				strS = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n4 = (int)atof(strS.c_str());
			}
			TIME stNow;
			timeopt::now(&stNow);
			time_t endTime = timeopt::SysTime2Unix(stNow);
			time_t startTime = endTime - n1 - n2 - n3 - n4;
			TIME  stStart = timeopt::Unix2SysTime(startTime);
			string strNow = timeopt::stTimeToStr(stNow);
			string strStart = timeopt::stTimeToStr(stStart);
			time = strStart + "~" + strNow;
		}
		return time;
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
	vector<string> getCOMList()
	{
		vector<string> list;
#ifdef ENABLE_SERIAL
		HKEY hkey;
		int result;
		int i = 0;
		string strComName;//串口名称   
		string strDrName;//串口详细名称   
		result = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
			"Hardware\\DeviceMap\\SerialComm",
			NULL,
			KEY_READ,
			&hkey);
		if (ERROR_SUCCESS == result)   //   打开串口注册表      
		{
			WCHAR portName[0x100], commName[0x100];
			DWORD dwLong, dwSize;
			do
			{
				dwSize = sizeof(portName) / sizeof(TCHAR);
				dwLong = dwSize;
				result = RegEnumValueW(hkey, i, portName, &dwLong, NULL, NULL, (LPBYTE)commName, &dwSize);
				if (ERROR_NO_MORE_ITEMS == result)
				{
					//   枚举串口   
					break;   //   commName就是串口名字"COM2"   
				}
				strComName = charCodec::utf16_to_tds(commName);
				strDrName = charCodec::utf16_to_tds(portName);
				// 从右往左边开始查找第一个'\\'，获取左边字符串的长度   
				size_t len = strDrName.rfind('\\');
				// 获取'\\'左边的字符串   
				string strFilePath = strDrName.substr(0, len + 1);
				// 获取'\\'右边的字符串   
				string fileName = strDrName.substr(len + 1, strDrName.length() - len - 1);
				fileName = strComName + ": " + fileName;
				list.push_back(fileName);
				i++;
			} while (1);
			RegCloseKey(hkey);
		}
#endif
		return list;
	}

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
	void createFolderOfPath(string strFile)
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
#ifdef _WIN32
	#ifndef _WINXP
		filesystem::create_directories(charCodec::tds_to_utf16(strFile));
	#endif
#else
		filesystem::create_directories(strFile);
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
		if (NULL != (p = strrchr(arr_tmp, '/')))
			*p = '\0';
		else
		{
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
		_wfopen_s(&fp,charCodec::tds_to_utf16(path).c_str(), L"wb");
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
			string err = sys::getLastError();
			printf("[error]%s", err.c_str());
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
		std::error_code error;
		auto file_status = std::filesystem::status(charCodec::tds_to_utf16(pszFileName), error);
		if (error) {
			return false;
		}
 
		if (std::filesystem::exists(file_status)) {
			return true;
		}
		else if(std::filesystem::is_directory(file_status)){
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
		return std::filesystem::remove(charCodec::tds_to_utf16(path));
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
				 if (exclude != nullptr) {
					 bool excluded = false;
					 for (int i = 0; i < exclude->size(); i++) {
						 string ep = exclude->at(i);
						 if (fi.path.find(ep) != string::npos) {
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
					 fi.name = fi.path.substr(pos + 1, fi.path.length() - pos - 1);
					 fi.len = i.file_size();
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

/**
__LINE__ 当前语句所在的行号, 以10进制整数标注.
__FILE__ 当前源文件的文件名, 以字符串常量标注.
__DATE__ 程序被编译的日期, 以"Mmm dd yyyy"格式的字符串标注.
__TIME__ 程序被编译的时间, 以"hh:mm:ss"格式的字符串标注, 该时间由asctime返回.
 */

#define YEAR ((((__DATE__ [7] - '0') * 10 + (__DATE__ [8] - '0')) * 10 \
    + (__DATE__ [9] - '0')) * 10 + (__DATE__ [10] - '0'))

#define MONTH (__DATE__ [2] == 'n' ? (__DATE__ [1] == 'a' ? 0 : 5)  \
    : __DATE__ [2] == 'b' ? 1 \
    : __DATE__ [2] == 'r' ? (__DATE__ [0] == 'M' ? 2 : 3) \
    : __DATE__ [2] == 'y' ? 4 \
    : __DATE__ [2] == 'l' ? 6 \
    : __DATE__ [2] == 'g' ? 7 \
    : __DATE__ [2] == 'p' ? 8 \
    : __DATE__ [2] == 't' ? 9 \
    : __DATE__ [2] == 'v' ? 10 : 11)

#define DAY ((__DATE__ [4] == ' ' ? 0 : __DATE__ [4] - '0') * 10 \
    + (__DATE__ [5] - '0'))

#define DATE_AS_INT (((YEAR - 2000) * 12 + MONTH) * 31 + DAY)

string getbuildtime()
{
	return str::format("%d-%02d-%02d %s", YEAR, MONTH + 1, DAY, __TIME__);
}

string getbuilddate()
{
	return str::format("%d-%02d-%02d", YEAR, MONTH + 1, DAY);
}

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

void TIME::setDate(Date t)
{
	wYear = t.wYear;
	wMonth = t.wMonth;
	wDay = t.wDay;
	wDayOfWeek = t.wDayOfWeek;
}

void TIME::setHMS(HMS t)
{
	wHour = t.wHour;
	wMinute = t.wMinute;
	wSecond = t.wSecond;
	wMilliseconds = t.wMilliseconds;
}

string TIME::toStr()
{
	return timeopt::st2str(*this);
}

string TIME::toDateStr()
{
	string s = str::format("%04d-%02d-%02d", wYear, wMonth, wDay);
	return s;
}

string TIME::toTimeStr()
{
	string s = str::format("%02d:%02d:%02d", wHour, wMinute, wSecond);
	return s;
}

string TIME::toStampHMS()
{
	string s = str::format("%02d%02d%02d", wHour, wMinute, wSecond);
	return s;
}

string Date::toStr()
{
	string s = str::format("%04d-%02d-%02d", wYear, wMonth, wDay);
	return s;
}

void Date::fromStr(string s)
{
	int y, m, d;
	sscanf(s.c_str(), "%4d-%2d-%2d",&y,&m,&d);
	wYear = y;
	wMonth = m;
	wDay = d;
}

string HMS::toStr()
{
	string s = str::format("%02d:%02d:%02d", wHour, wMinute, wSecond);
	return s;
}

void HMS::fromStr(string str)
{
	int h, m, s;
	sscanf(str.c_str(), "%2d:%2d:%2d",&h,&m,&s);
	wHour = h;
	wMinute = m;
	wSecond = s;
}
