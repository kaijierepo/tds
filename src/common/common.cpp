#include "pch.h"
#include "common.h"



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

	void endianSwap(char* pData, int len)
	{
		char* pNew = new char[len];
		for (int i = 0;i<len;i++)
		{
			pNew[i] = pData[len - 1 - i];
		}
		memcpy(pData, pNew, len);
		delete pNew;
	}
}

namespace str {
	std::string format(const char* pszFmt, ...)
	{
		std::string str;
		va_list args;
		va_start(args, pszFmt);
		{
			int nLength = _vscprintf(pszFmt, args);
			nLength += 1;  //上面返回的长度是包含\0，这里加上
			std::vector<char> vectorChars(nLength);
			_vsnprintf_s(vectorChars.data(), nLength, nLength, pszFmt, args);
			str.assign(vectorChars.data());
		}
		va_end(args);
		return str;
	}
}

namespace charCodec {

	string utf16toUtf8(wstring instr) //utf-8-->ansi
	{
		int MAX_STRSIZE = instr.length() * 4 + 2;
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_UTF8, 0, instr.c_str(), -1, charstr, MAX_STRSIZE, NULL, NULL);
		string str = charstr;
		delete charstr;
		return str;
	}

	string utf16toAnsi(wstring instr)
	{
		int MAX_STRSIZE = instr.length() * 2 + 2;
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_ACP, 0, instr.c_str(), -1, charstr, MAX_STRSIZE, NULL, NULL);
		string str = charstr;
		delete charstr;
		return str;
	}

	string utf8toAnsi(string instr) //utf-8-->ansi
	{
		int MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_UTF8, 0, (char*)instr.data(), -1, wcharstr, MAX_STRSIZE);
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_ACP, 0, wcharstr, -1, charstr, MAX_STRSIZE, NULL, NULL);
		string charstrtemp(charstr);
		delete wcharstr;
		delete charstr;
		return charstrtemp;
	}

	wstring utf8toUtf16(string instr) //utf-8-->ansi
	{
		int MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_UTF8, 0, (char*)instr.data(), -1, wcharstr, MAX_STRSIZE);
		wstring str = wcharstr;
		delete wcharstr;
		return str;
	}

	wstring ansiToUtf16(string instr)
	{
		int MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_ACP, 0, (char*)instr.data(), -1, wcharstr, MAX_STRSIZE);
		wstring str = wcharstr;
		delete wcharstr;
		return str;
	}

	

	string utf16ToAuto(wstring instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			s = charCodec::utf16toAnsi(instr);
		}
		else
		{
			s = charCodec::utf16toUtf8(instr);
		}
		return s;
	}

	//GB2312 value region  A1A1－FEFE  for chinese chars is B0A1-F7FE。
	string ansi2Utf8(string instr) //ansi-->utf-8
	{
		int MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_ACP, 0, (char*)instr.data(), -1, wcharstr, MAX_STRSIZE);
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_UTF8, 0, wcharstr, -1, charstr, MAX_STRSIZE, NULL, NULL);
		string charstrtemp(charstr);
		delete wcharstr;
		delete charstr;
		return charstrtemp;
	}

	bool hasGB2312(string s)
	{
		for (int i = 0; i < s.length(); i++)
		{
			byte b = s.at(i);
			if (b >= 0xA1 && b <= 0xFE) //gb2312
			{
				if (i + 1 < s.length())
				{
					byte bNext = s.at(i + 1);
					if (bNext >= 0xA1 && bNext <= 0xFE)
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	
	bool isValidGB2312(string s, int& errorPos, string& errorChar)
	{
		for (int i = 0; i < s.length();)
		{
			byte b = s.at(i);
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
						byte bNext = s.at(i + 1);
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
		int pos = 0;
		string errorChar;
		return isValidGB2312(s, pos, errorChar);
	}

	string ToUtf8(LPCTSTR wstr) //-->utf-8
	{
#ifdef UNICODE
		int srcLen = lstrlen(wstr);
		string str = "";
		int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
		if (len > 0)
		{
			char* des = new char[len + 1];
			memset(des, 0, len + 1);
			WideCharToMultiByte(CP_UTF8, 0, wstr, -1, des, len, NULL, NULL);
			str = des;
		}
		return str;
#else
		string str = wstr;
		return str;
#endif
	}

	wstring autoToUtf16(string instr)
	{
		wstring w;
		if (common::getCharCodec() == "gb2312")
		{
			w = charCodec::ansiToUtf16(instr);
		}
		else
		{
			w = charCodec::utf8toUtf16(instr);
		}
		return w;
	}

	string autoToUtf8(string instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			s = charCodec::ansi2Utf8(instr);
			return s;
		}
		else
		{
			return instr;
		}
	}

	string autoToAnsi(string instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			return instr;
		}
		else
		{
			return charCodec::utf8toAnsi(instr);
		}
	}

	string ansiToAuto(string instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			return instr;
		}
		else
		{
			return charCodec::ansi2Utf8(instr);
		}
	}

	string utf8ToAuto(string instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			return charCodec::utf8toAnsi(instr);
		}
		else
		{
			return instr;
		}
	}
}
namespace str {

	string& trimPrefix(string& s, string prefix)
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
			int ipos = s.rfind(suffix);
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
	string fromFloat(float f)
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

		int strLen = 0;
		strLen = str.length();

		for (int i = 0; i < strLen / 2; i++)
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
	
	vector<byte> toBytes(string str)
	{
		vector<char> vec = toChars(str);
		vector<byte> vecB;
		for (int i = 0; i < vec.size(); i++)
		{
			byte& b = *((byte*)(&vec[i]));
			vecB.push_back(b);
		}
		return vecB;
	}
	
	string bytesToHexStr(vector<char>& bytes)
	{
		string str;
		for (int i = 0; i < bytes.size(); i++)
		{
			string b = format("%02X", (unsigned char)bytes[i]);
			str += b;
		}

		return str;
	}

	string bytesToHexStr(vector<byte>& bytes)
	{
		string str;
		for (int i = 0; i < bytes.size(); i++)
		{
			string b = format("%02X", (unsigned char)bytes[i]);
			str += b;
		}

		return str;
	}

	vector<byte> hexStrToBytes(string hexStr)
	{
		vector<byte> ary;
		hexStr = str::removeChar(hexStr, ' ');
		if (0 != hexStr.length() % 2)
		{
			hexStr += "0";
		}
		int strLen = 0;
		strLen = hexStr.length();
		transform(hexStr.begin(), hexStr.end(), hexStr.begin(), ::toupper);

		for (int i = 0; i < strLen / 2; i++)
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
			ary.push_back((byte)b);
		}
		return ary;
	}

	string bytesToHexStr(char* p, int len, string splitter)
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

	string bytesToHexStr(byte* p, int len, string splitter)
	{
		return bytesToHexStr((char*)p, len, splitter);
	}

	string fromInt(int v)
	{
		string s = str::format("%d", v);
		return s;
	}

	string fromInt(size_t v)
	{
		string s = str::format("%d", v);
		return s;
	}

	string fromBuff(const char* p, int len)
	{
		char* tmp = new char[len + 1];
		memcpy(tmp, p, len);
		tmp[len] = 0;
		string s = tmp;
		delete tmp;
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
		int ipos = s.find(":");
		if (ipos == string::npos)
			return false;

		string sip = s.substr(0, ipos);
		string sport = s.substr(ipos + 1, s.length() - ipos - 1);

		if (!isIp(sip))
			return false;

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
		string gbstr = charCodec::utf8toAnsi(hanZi);
		vector<char> vecPinyin;
		for (int i = 0; i < gbstr.length();)
		{
			byte b = gbstr.at(i);
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
						byte bNext = gbstr.at(i + 1);
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
	string stTimeToStr(SYSTEMTIME time)
	{
		string str = str::format("%4d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
		return str;
	}

	DWORD SysTime2Unix(SYSTEMTIME sDT)
	{
		tm temptm = { sDT.wSecond, sDT.wMinute, sDT.wHour,
			sDT.wDay, sDT.wMonth - 1, sDT.wYear - 1900, sDT.wDayOfWeek, 0, 0 };
		DWORD iReturn = (DWORD)mktime(&temptm);
		return iReturn;
	}

	SYSTEMTIME Unix2SysTime(time_t iUnix)
	{
		SYSTEMTIME sDT;
		time_t tIn = (time_t)iUnix;
		tm temptm;
		localtime_s(&temptm, &tIn);
		sDT.wYear = 1900 + temptm.tm_year;
		sDT.wMonth = 1 + temptm.tm_mon;
		sDT.wDay = temptm.tm_mday;
		sDT.wDayOfWeek = temptm.tm_wday;
		sDT.wHour = temptm.tm_hour;
		sDT.wMinute = temptm.tm_min;
		sDT.wSecond = temptm.tm_sec;
		sDT.wMilliseconds = 0;
		return sDT;
	}

	SYSTEMTIME str2st(string str)
	{
		SYSTEMTIME t;
		int year, month, day, hour, min, sec;
		sscanf_s(str.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
			&year,
			&month,
			&day,
			&hour,
			&min,
			&sec);
		t.wYear = year;
		t.wMonth = month;
		t.wDay = day;
		t.wHour = hour;
		t.wMinute = min;
		t.wSecond = sec;
		t.wMilliseconds = 0;
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

	DWORD duration2sec(string strTime)
	{
		DWORD dwSecond = 0;

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
		int pos = time1.find("d");
		if (pos == string::npos)
			pos = time1.find("D");
		if (pos != string::npos) {
			strDay = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n1 = atof(strDay.c_str()) * 24 * 3600;
		}
		pos = time1.find("h");
		if (pos == string::npos)
			pos = time1.find("H");
		if (pos != string::npos) {
			strH = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n2 = atof(strH.c_str()) * 3600;
		}
		pos = time1.find("m");
		if (pos == string::npos)
			pos = time1.find("M");
		if (pos != string::npos) {
			strM = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n3 = atof(strM.c_str()) * 60;
		}
		pos = time1.find("s");
		if (pos == string::npos)
			pos = time1.find("S");
		if (pos != string::npos) {
			strS = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n4 = atof(strS.c_str());
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
			int pos = time1.find("d");
			if (pos == string::npos)
				pos = time1.find("D");
			if (pos != string::npos) {
				strDay = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n1 = atof(strDay.c_str()) * 24 * 3600;
			}
			pos = time1.find("h");
			if (pos == string::npos)
				pos = time1.find("H");
			if (pos != string::npos) {
				strH = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n2 = atof(strH.c_str()) * 3600;
			}
			pos = time1.find("m");
			if (pos == string::npos)
				pos = time1.find("M");
			if (pos != string::npos) {
				strM = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n3 = atof(strM.c_str()) * 60;
			}
			pos = time1.find("s");
			if (pos == string::npos)
				pos = time1.find("S");
			if (pos != string::npos) {
				strS = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n4 = atof(strS.c_str());
			}
			SYSTEMTIME stNow;
			GetLocalTime(&stNow);
			time_t endTime = timeopt::SysTime2Unix(stNow);
			time_t startTime = endTime - n1 - n2 - n3 - n4;
			SYSTEMTIME  stStart = timeopt::Unix2SysTime(startTime);
			string strNow = timeopt::stTimeToStr(stNow);
			string strStart = timeopt::stTimeToStr(stStart);
			time = strStart + "~" + strNow;
		}
		return time;
	}

	string st2str(SYSTEMTIME t)
	{
		string str = str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
			t.wYear, t.wMonth, t.wDay,
			t.wHour, t.wMinute, t.wSecond);
		return str;
	}

	string st2strWithMilli(SYSTEMTIME t)
	{
		string str = str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",
			t.wYear, t.wMonth, t.wDay,
			t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		return str;
	}

	string TimeToYMD(const SYSTEMTIME time)
	{
		string str;
		if (time.wYear > 2000 && time.wDay > 0 && time.wDay < 40 && time.wHour >= 0 && time.wHour <= 24 && time.wMinute >= 0 && time.wMinute <= 60)
		{
			str = str::format("%.4d-%.2d-%.2d", time.wYear, time.wMonth, time.wDay);
		}
		return str;
	}
	int CalcTimePassSecond(SYSTEMTIME lastTime)
	{
		time_t last = SysTime2Unix(lastTime);
		time_t now = time(NULL);
		time_t milli = now - last;
		return milli;
	}

	int CalcTimeDiffSecond(SYSTEMTIME newTime,SYSTEMTIME oldTime)
	{
		time_t newT = SysTime2Unix(newTime);
		time_t oldT = SysTime2Unix(oldTime);
		time_t seconds = newT - oldT;
		return seconds;
	}

	int CalcTimePassMilliSecond(SYSTEMTIME lastTime)
	{
		time_t last = SysTime2Unix(lastTime);
		SYSTEMTIME stNow;
		GetLocalTime(&stNow);
		time_t now = SysTime2Unix(stNow);
		int second = now - last;
		int milli = stNow.wMilliseconds - lastTime.wMilliseconds;
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

	void setAsTimeOrg(SYSTEMTIME& st)
	{
		memset(&st, 0, sizeof(SYSTEMTIME));
		st.wYear = 1970;
		st.wMonth = 1;
		st.wDay = 1;
	}

	bool isValidTime(SYSTEMTIME& st)
	{
		if (st.wYear == 0 || st.wYear == 1970)
			return false;
		return true;
	}

	string nowStr(bool enableMS)
	{
		time_t timestamp = getTick();
		__int64 milli = timestamp + (__int64)8 * 60 * 60 * 1000;
		auto mTime = std::chrono::milliseconds(milli);
		auto tp = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(mTime);
		auto tt = std::chrono::system_clock::to_time_t(tp);
		std::tm now;
		::gmtime_s(&now, &tt);
		char res[64] = { 0 };
		if (enableMS)
			sprintf_s(res, _countof(res), "%4d-%02d-%02d %02d:%02d:%02d.%03d", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, static_cast<int>(milli % 1000));
		else
		{
			sprintf_s(res, _countof(res), "%4d-%02d-%02d %02d:%02d:%02d", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
		}

		return std::string(res);
	}

	SYSTEMTIME addTime(SYSTEMTIME base, int h, int m, int s) {
		time_t tBase = SysTime2Unix(base);
		tBase += h * 3600 + m * 60 + s;
		SYSTEMTIME st = Unix2SysTime(tBase);
		return st;
	}

	bool isValidTimeStr(string time) {
		if (time.length() != 19)
			return false;
		if (time.at(4) != '-' || time.at(7) != '-')
			return false;
		if (time.at(13) != ':' || time.at(16) != ':')
			return false;
		return true;
	}
}


namespace sys {
	vector<string> getCOMList()
	{
		vector<string> list;
		HKEY hkey;
		int result;
		int i = 0;
		string strComName;//串口名称   
		string strDrName;//串口详细名称   
		result = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
			_T("Hardware\\DeviceMap\\SerialComm"),
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
				strComName = charCodec::utf16ToAuto(commName);
				strDrName = charCodec::utf16ToAuto(portName);
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
		return list;
	}

	vector<COM_INFO> getCOMInfoList() {
		vector<COM_INFO> ary;
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
			string comInfo = charCodec::utf16ToAuto(utf16str);

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
		return ary;
	}
	string getLastError(string szReason)
	{
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

		string szErrMsg = "";

		if (dwLen == 0)
		{
			DWORD dwFmtErrCode = GetLastError(); //FormatMessage 引起的错误代码
			szErrMsg = str::format("FormatMessage failed with %u\n", dwFmtErrCode);
		}

		if (lpMsgBuf)
		{
			wstring utf16msg = (LPWSTR)lpMsgBuf;
			string utf8Msg = charCodec::utf16ToAuto(utf16msg);
			szErrMsg = str::format("%s\n Code = %u, Mean = %s", szReason.c_str(), dwErrCode, utf8Msg.c_str());
		}

		if (lpMsgBuf)
		{
			// Free the buffer.
			LocalFree(lpMsgBuf);
			lpMsgBuf = NULL;
		}

		return szErrMsg;
	}
}



namespace fs {
	//带后缀 .XXX 作为文件路径
	//不带后缀作为文件夹路径。不要输入无后缀的文件路径
	void createFolderOfPath(string strFile)
	{
		strFile = str::replace(strFile, "\\", "/");
		strFile = str::replace(strFile, "////", "/");
		strFile = str::replace(strFile, "///", "/");
		strFile = str::replace(strFile, "//", "/");

		size_t iDotPos = strFile.rfind('.');
		size_t iSlashPos = strFile.rfind('/');
		if (iDotPos > iSlashPos)//是一个文件
		{
			strFile = strFile.substr(0, iSlashPos);
		}

		size_t iStartPos = 0;
		while (1)
		{
			size_t iSlash = strFile.find('/', iStartPos);

			if (iSlash == string::npos)//路径为文件夹的情况
			{
				size_t iDot = strFile.find('.', iStartPos);
				if (iDot == string::npos)
					CreateDirectoryW(charCodec::autoToUtf16(strFile).c_str(), NULL);
				break;
			}

			if (iSlash + 1 == strFile.length())//最后字符为 \\ 的情况
				break;

			string strFolder = strFile.substr(0, iSlash);
			wstring wstrFolder = charCodec::autoToUtf16(strFolder).c_str();
			CreateDirectoryW(wstrFolder.c_str(), NULL);
			iStartPos = iSlash + 1;
		}
	}

	string appName()
	{
#ifdef WINDOWS
		//windows获取到的是反斜杠，tds内统一使用斜杠
		TCHAR p[MAX_PATH] = { 0 };
		GetModuleFileName(NULL, p, MAX_PATH);//获取可执行模块的路径
		string strPath = (char*)p;
		int nEnd = strPath.rfind('\\');//取最后的"\"号之前地址
		strPath = strPath.substr(nEnd+1, strPath.length() - nEnd - 1);
		if (common::getCharCodec() == "gb2312")
			strPath = strPath;
		else
			strPath = charCodec::ansi2Utf8(strPath);
		strPath = str::trimSuffix(strPath, ".exe");
		return strPath;
#elif LINUX
		return "";
#else
		return "";
#endif
	}


	string appPath()
	{
#ifdef WINDOWS
		//windows获取到的是反斜杠，tds内统一使用斜杠
		TCHAR p[MAX_PATH] = { 0 };
		GetModuleFileName(NULL, p, MAX_PATH);//获取可执行模块的路径
		string strPath = (char*)p;
		size_t nEnd = strPath.rfind('\\');//取最后的"\"号之前地址
		strPath = strPath.substr(0, nEnd);
		if (common::getCharCodec() == "gb2312")
			strPath = strPath;
		else
			strPath = charCodec::ansi2Utf8(strPath);
		strPath = str::replace(strPath, "\\", "/");
		return strPath;
#elif LINUX
		return "";
#else
		return "";
#endif
	}
	string toAbsolutePath(string str)
	{
		str = charCodec::autoToAnsi(str);
		char absPath[1024] = { 0 };
		_fullpath(absPath, str.c_str(), 1024);
		str = absPath;
		str = str::replace(str, "\\", "/");
		str = charCodec::ansiToAuto(str);
		return str;
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
		_wfopen_s(&fp,charCodec::autoToUtf16(path).c_str(), L"rb");
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
		_wfopen_s(&fp,charCodec::autoToUtf16(path).c_str(), L"rb");
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
			delete pdata;
			return true;
		}
		return false;
	}
	bool writeFile(string path, char* data, size_t len)
	{
		fs::createFolderOfPath(path);
		wstring wpath = charCodec::autoToUtf16(path);

		FILE* fp = nullptr;
		_wfopen_s(&fp,wpath.c_str(), L"wb");
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
		wstring wpath = charCodec::autoToUtf16(path);
		FILE* fp = nullptr;
		_wfopen_s(&fp,wpath.c_str(), L"ab");
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
		WIN32_FIND_DATAW FindFileData;
		HANDLE hFind;

		hFind = FindFirstFileW(charCodec::autoToUtf16(pszFileName).c_str(), &FindFileData);

		if (hFind == INVALID_HANDLE_VALUE)
			return false;
		else
		{
			FindClose(hFind);
			return true;
		}
		return false;
	}

	bool deleteFile(string path) {
		wstring wpath = charCodec::autoToUtf16(path);
		int iret = _wremove(wpath.c_str());
		return iret == 0;
	}

	bool getFileInfo(string path, FILE_INFO& fi)
	{
		wstring wpath = charCodec::utf8toUtf16(path);
		struct _stat64 tmpInfo;
		if (_wstat64(wpath.c_str(), &tmpInfo) != 0)
		{
			return false;
		}
		fi.modifyTime = timeopt::st2str(timeopt::Unix2SysTime(static_cast<int>(tmpInfo.st_mtime)));
		fi.accessTime = timeopt::st2str(timeopt::Unix2SysTime(static_cast<int>(tmpInfo.st_atime)));
		fi.createTime = timeopt::st2str(timeopt::Unix2SysTime(static_cast<int>(tmpInfo.st_ctime)));
		fi.len = tmpInfo.st_size;
		return true;
	}


	 void getFolderList(vector<string>& list, string strFolder)
	{
		wstring wstrFolder = charCodec::autoToUtf16(strFolder);
		wstring dirNew;
		dirNew = wstrFolder;
		dirNew += L"\\*.*";    // 在目录后面加上"\\*.*"进行第一次搜索

		intptr_t handle;
		_wfinddata64i32_t findData;

		handle = _wfindfirst(dirNew.c_str(), &findData);
		if (handle == -1)        // 检查是否成功
			return;

		do
		{
			if (findData.attrib & _A_SUBDIR)
			{
				if (wcscmp(findData.name, L".") == 0 || wcscmp(findData.name, L"..") == 0)
					continue;

				//list.push_back(charCodec::utf16toUtf8(findData.name));

				// 在目录后面加上"\\"和搜索到的目录名进行下一次搜索
				dirNew = wstrFolder.c_str();
				dirNew += L"\\";
				dirNew += findData.name;


				list.push_back(charCodec::utf16ToAuto(findData.name));
			}
		} while (_wfindnext(handle, &findData) == 0);

		_findclose(handle);    // 关闭搜索句柄
	}


	 void getFileList(vector<string>& list,string strFolder,bool includeFolder,bool recursive)
	{
		wstring wstrFolder = charCodec::autoToUtf16(strFolder);
		wstring dirNew;
		dirNew = wstrFolder;
		dirNew += L"\\*.*";    // 在目录后面加上"\\*.*"进行第一次搜索

		intptr_t handle;
		_wfinddata64i32_t findData;

		handle = _wfindfirst(dirNew.c_str(), &findData);
		if (handle == -1)        // 检查是否成功
			return;

		do
		{
			if (findData.attrib & _A_SUBDIR)
			{
				if (wcscmp(findData.name, L".") == 0 || wcscmp(findData.name, L"..") == 0)
					continue;

				//list.push_back(charCodec::utf16toUtf8(findData.name));

				// 在目录后面加上"\\"和搜索到的目录名进行下一次搜索
				dirNew = wstrFolder.c_str();
				dirNew += L"\\";
				dirNew += findData.name;

				if (includeFolder)
					list.push_back(charCodec::utf16ToAuto(findData.name));

				if(recursive)
					getFileList(list,charCodec::utf16ToAuto(dirNew));
			}
			else
			{
				list.push_back(charCodec::utf16ToAuto(findData.name));
			}
		} while (_wfindnext(handle, &findData) == 0);

		_findclose(handle);    // 关闭搜索句柄
	}

	
	vector<string> fileDlg(bool isMultiSelect, bool IsOpen, bool IsPickFolder, char* filter, char* title, char* fileName,char* defExt,char* initDirectory)
	{
		vector<string> vecPath;
		//文件名
		wstring filename;
		wstring wFilter;
		vector<wstring> vecWFilter;
		if (filter)
		{
			string sFilter = filter;
			vector<string> vecFilter;
			str::split(vecFilter, filter, "|");
			for (int i = 0; i < vecFilter.size(); i++)
			{
				string s = vecFilter[i];
				vecWFilter.push_back(charCodec::autoToUtf16(s));
			}
		}
		std::wstring wDir;
		if (initDirectory)
			wDir = charCodec::autoToUtf16(initDirectory).c_str();//初始目录为默认
		std::wstring wTitle;
		if (title)
			wTitle = charCodec::autoToUtf16(title).c_str();
		std::wstring wDefExt;
		if (defExt)
			wDefExt = charCodec::autoToUtf16(defExt).c_str();
		std::wstring wFileName;
		if (fileName)
			wFileName = charCodec::autoToUtf16(fileName).c_str();


		CoInitialize(nullptr);
		if (!isMultiSelect)
		{
			IFileDialog* pfd = NULL;
			HRESULT hr = NULL;
			if (IsOpen)
				hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
			else
				hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
			if (SUCCEEDED(hr))
			{
				DWORD dwFlags;
				hr = pfd->GetOptions(&dwFlags);
				if (IsPickFolder)
					hr = pfd->SetOptions(dwFlags | FOS_PICKFOLDERS);
				else
					hr = pfd->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);

				COMDLG_FILTERSPEC* fileType = new COMDLG_FILTERSPEC[vecWFilter.size()/2];
				for (int i = 0; i < vecWFilter.size() / 2; i++)
				{
					COMDLG_FILTERSPEC& fs = fileType[i];
					fs.pszName = vecWFilter[i * 2].c_str();
					fs.pszSpec = vecWFilter[i * 2 + 1].c_str();
				}
				hr = pfd->SetFileTypes((UINT)vecWFilter.size()/2, fileType);
				hr = pfd->SetFileTypeIndex(1);
				

				if (!IsOpen)
				{
					hr = pfd->SetFileName(wFileName.c_str());
					hr = pfd->SetDefaultExtension(wDefExt.c_str());
				}
				

				hr = pfd->Show(GetForegroundWindow()); //Show dialog
					//if (SUCCEEDED(hr))
					//{
					//	if (!IsOpen)       //Capture user change when select differen file extension.
					//	{
					//		if (nType == 2)
					//		{
					//			UINT  unFileIndex(1);
					//			hr = pfd->GetFileTypeIndex(&unFileIndex);
					//			switch (unFileIndex)
					//			{
					//			case 0:
					//				hr = pfd->SetDefaultExtension(L"txt");
					//				break;
					//			case 1:
					//				hr = pfd->SetDefaultExtension(L"csv");
					//				break;
					//			case 2:
					//				hr = pfd->SetDefaultExtension(L"ini");
					//				break;
					//			default:
					//				hr = pfd->SetDefaultExtension(L"txt");
					//				break;
					//			}
					//		}
					//	}
					//}
					if (SUCCEEDED(hr))
					{
						IShellItem* pSelItem;
						hr = pfd->GetResult(&pSelItem);
						if (SUCCEEDED(hr))
						{
							LPWSTR pszFilePath = NULL;
							hr = pSelItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &pszFilePath);
							string sutf8 = charCodec::utf16toUtf8(pszFilePath);
							sutf8 = str::replace(sutf8, "\\", "/");
							vecPath.push_back(sutf8);
							CoTaskMemFree(pszFilePath);
						}
						pSelItem->Release();
					}
				}
				pfd->Release();
			}
		else  //Open dialog with multi select allowed;
		{
			IFileOpenDialog* pfd = NULL;
			HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
			if (SUCCEEDED(hr))
			{
				DWORD dwFlags;
				hr = pfd->GetOptions(&dwFlags);
				hr = pfd->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT);
					
				COMDLG_FILTERSPEC* fileType = new COMDLG_FILTERSPEC[vecWFilter.size()];
				for (int i = 0; i < vecWFilter.size(); i++)
				{
					COMDLG_FILTERSPEC& fs = fileType[i];
					fs.pszName = vecWFilter[i * 2].c_str();
					fs.pszSpec = vecWFilter[i * 2 + 1].c_str();
				}
				hr = pfd->SetFileTypes(vecWFilter.size(), fileType);
				hr = pfd->SetFileTypeIndex(1);

				hr = pfd->Show(NULL);
				if (SUCCEEDED(hr))
				{
					IShellItemArray* pSelResultArray;
					hr = pfd->GetResults(&pSelResultArray);
					if (SUCCEEDED(hr))
					{
						DWORD dwNumItems = 0; // number of items in multiple selection
						hr = pSelResultArray->GetCount(&dwNumItems);  // get number of selected items
						for (DWORD i = 0; i < dwNumItems; i++)
						{
							IShellItem* pSelOneItem = NULL;
							PWSTR pszFilePath = NULL; // hold file paths of selected items
							hr = pSelResultArray->GetItemAt(i, &pSelOneItem); // get a selected item from the IShellItemArray
							if (SUCCEEDED(hr))
							{
								hr = pSelOneItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
								vecPath.push_back(charCodec::utf16toUtf8(pszFilePath));
								if (SUCCEEDED(hr))
								{
									/*szSelected += pszFilePath;
									if (i < (dwNumItems - 1))
										szSelected += L"\n";*/
									CoTaskMemFree(pszFilePath);
								}
								pSelOneItem->Release();
							}
						}
						pSelResultArray->Release();
					}
				}
			}
			pfd->Release();
		}

		return vecPath;
	}
}
namespace path {
	string normalization(string& s)
	{
		s = str::replace(s, "\\\\", "/");
		s = str::replace(s, "\\", "/");
		s = str::replace(s, "//", "/");
		return s;
	}
}

namespace common {
	string guid() {
		GUID   m_guid;
		string   strGUID;
		if (S_OK == ::CoCreateGuid(&m_guid))
		{
			strGUID = str::format("%08X-%04X-%04x-%02X%02X-%02X%02X%02X%02X%02X%02X",
				m_guid.Data1, m_guid.Data2, m_guid.Data3,
				m_guid.Data4[0], m_guid.Data4[1],
				m_guid.Data4[2], m_guid.Data4[3],
				m_guid.Data4[4], m_guid.Data4[5],
				m_guid.Data4[6], m_guid.Data4[7]);
		}
		return strGUID;
	}

	float randomFloat(float min, float max) {
		SYSTEMTIME st;
		GetLocalTime(&st);
		//当前毫秒作为随机数种子
		int seed = abs(st.wMilliseconds - rand() % 1000);
		seed = seed % 100;
		float diffRate = float(seed) / 100.0;
		float diff = (max - min)*diffRate;
		float v = min + diff;
		return v;
	}

	int randomInt(int min, int max) {
		SYSTEMTIME st;
		GetLocalTime(&st);
		//当前毫秒作为随机数种子
		int seed = abs(st.wMilliseconds - rand() % 1000);
		seed = seed % 100;
		float diffRate = float(seed) / 100.0;
		float diff = (max - min) * diffRate;
		float v = min + diff;
		return v;
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
	static char buildtime[256] = { 0 };
	sprintf_s(buildtime, 256,"%d-%02d-%02d %s", YEAR, MONTH + 1, DAY, __TIME__);
	string s = buildtime;
	return s;
}

string getbuilddate()
{
	static char buildtime[256] = { 0 };
	sprintf_s(buildtime,256, "%d-%02d-%02d", YEAR, MONTH + 1, DAY);
	string s = buildtime;
	return s;
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
