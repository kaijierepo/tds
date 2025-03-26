#pragma once

#include "proto_common.h"

#pragma pack(1)
/* 宏定义 **/
#define NAME_LEN              128   
#define UNKNOWN_RULER_ID     0XFFFFFFFF
#define D07_ADDR_LEN         12                      /* 地址长 */
#define D07_DATA_MAX_NR      240                       /* dlt645 2007 最大数据域字节数算规约ID */
#define D07_DATA_MAX         (D07_DATA_MAX_NR + 4)   /* dlt645 2007 最大数据域字节数 */
#define D07_FRAME_LEN_MIN    12                        /* DLT645 2007 最小帧字节数 */  

/*功能码 2007*/
#define D07_CTRL_RESV                                0x00     // 保留
#define D07_CTRL_SYNC_TIME                            0x08     // 广播校时
#define D07_CTRL_READ_DATA                            0x11    // 读数据
#define D07_CTRL_READ_AFTERDATA                        0x12    // 读后续数据
#define D07_CTRL_READ_ADDR                            0x13    // 读通信地址
#define D07_CTRL_WRITE_DATA                            0x14    // 写数据
#define D07_CTRL_WRITE_ADDR                            0x15    // 写通信地址
#define D07_CTRL_FREEZ_COMM                            0x16    // 冻结命令
#define D07_CTRL_MODIFY_BAUD                        0x17    // 修改通信速率
#define D07_CTRL_MODIFY_PASSWORD                    0x18    // 修改密码
#define D07_CTRL_CLEAR_MAXDEMAND                    0x19    // 最大需量清零
#define D07_CTRL_CLEAR_METER                        0x1A    // 电表清零
#define D07_CTRL_CLEAR_EVENT                        0x1B    // 事件清零
#define D07_CTRL_COMM                                0x1C    // 控制命令

/* 返回值类型 列表*/
typedef enum
{
	E_D07_OK = 0,            /* 返回OK */
	E_D07_ERRO_NULL,        /* 指针为空 */
	E_D07_ERRO_UNKOWN_ID,    // 未知规约ID
	E_D07_ERRO_PARA_NONE,   /* 不存在 */
	E_D07_ERRO_PARA_OUT_RANGE,

	//check frame
	E_D07_ERRO_FRAME_UNCOMP,        /* 不完整的帧数据 */
	E_D07_ERRO_FRAME_0x68,          /* 测试帧帧起始符 0x68 的位置不对 */
	E_D07_ERRO_FRAME_CHECK_SUM,     /* 测试帧检验和不对 */
	E_D07_ERRO_FRAME_END_0x16,      /* 测试帧结束符 0x16 不对 */
	E_D07_ERRO_NONE_FRAME,          // 没有找到帧

}eD07Err;

inline void d07_str2bcd(const char* pstr, unsigned char* pbcd, int len) {
	unsigned char tmpValue;
	int i;
	int j;
	int m;
	int sLen;

	sLen = strlen(pstr);
	for (i = 0; i < sLen; i++)
	{
		if ((pstr[i] < '0')
			|| ((pstr[i] > '9') && (pstr[i] < 'A'))
			|| ((pstr[i] > 'F') && (pstr[i] < 'a'))
			|| (pstr[i] > 'f'))
		{
			sLen = i;
			break;
		}
	}

	sLen = (sLen <= (len * 2)) ? sLen : sLen * 2;
	memset((void*)pbcd, 0x00, len);

	for (i = sLen - 1, j = 0, m = 0; (i >= 0) && (m < len); i--, j++)
	{
		if ((pstr[i] >= '0') && (pstr[i] <= '9'))
		{
			tmpValue = pstr[i] - '0';
		}
		else if ((pstr[i] >= 'A') && (pstr[i] <= 'F'))
		{
			tmpValue = pstr[i] - 'A' + 0x0A;
		}
		else if ((pstr[i] >= 'a') && (pstr[i] <= 'f'))
		{
			tmpValue = pstr[i] - 'a' + 0x0A;
		}
		else
		{
			tmpValue = 0;
		}

		if ((j % 2) == 0)
		{
			pbcd[m] = tmpValue;
		}
		else
		{
			pbcd[m++] |= (tmpValue << 4);
		}

		if ((tmpValue == 0) && (pstr[i] != '0'))
		{
			break;
		}
	}
}

inline void d07_bcd2str(unsigned char* inBCD, char* outStr, int len)
{
	int i = 0, j = 0;
	unsigned char c1 = 0, c0 = 0;

	if (NULL == inBCD || NULL == outStr || len < 1)
	{
		return;
	}

	for (i = len - 1, j = 0; i >= 0; i--)
	{
		c0 = inBCD[i] & 0xF;
		c1 = (inBCD[i] >> 4) & 0xF;
		if (c1 >= 0 && c1 <= 9)
		{
			outStr[j++] = c1 + '0';
		}
		else
		{
			outStr[j++] = c1 + 'A';
		}

		if (c0 >= 0 && c0 <= 9)
		{
			outStr[j++] = c0 + '0';
		}
		else
		{
			outStr[j++] = c0 + 'A';
		}
	}

}

struct DI {
	unsigned char DI0;
	unsigned char DI1;
	unsigned char DI2;
	unsigned char DI3;

	void setDI(string sDI) {
		d07_str2bcd(sDI.c_str(), (unsigned char*)this, 4);
		DI0 += 0x33;
		DI1 += 0x33;
		DI2 += 0x33;
		DI3 += 0x33;
	}
};

class DLT645_2007_PKT : public DEV_PKT{
public:
	unsigned char preamble;
	unsigned char head;
	unsigned char addr[6];
	unsigned char head1;
	unsigned char ctrlCode;
	unsigned char dLen;
	unsigned char* d;
	unsigned char cs;  //校验码
	unsigned char tail;

	string sDI;

	DLT645_2007_PKT() {
		head = 0x68;
		head1 = 0x68;
		tail = 0x16;
		d = nullptr;
	}

	~DLT645_2007_PKT() {
		if (d != nullptr)
		{
			delete d;
		}
	}

	string GetPktDesc() override
	{ 
		return ""; 
	};

	void setDataRegion(unsigned char* pD, unsigned char len) {
		if (d != nullptr)
		{
			delete d;
		}
		d = new unsigned char[len];
		dLen = len;
		memcpy(d, pD, len);
	}

	static void setDI(string s, unsigned char* pDI) {
		d07_str2bcd(s.c_str(), (unsigned char*)pDI, 4);
		pDI[0] += 0x33;
		pDI[1] += 0x33;
		pDI[2] += 0x33;
		pDI[3] += 0x33;
	}

	unsigned char* getDIBuff() {
		return d; //请求响应的di都是在数据域的前4个字节
	}

	void setAddr(string sAddr) {
		d07_str2bcd(sAddr.c_str(), addr, 6);
	}

	

	void copy(const DLT645_2007_PKT& r) {
		DEV_PKT::copy(r);
		head = r.head;
		memcpy(addr,r.addr,6);
		head1 = r.head1;
		ctrlCode = r.ctrlCode;
		dLen = r.dLen;
		if (r.d != nullptr) {
			d = new unsigned char[r.dLen];
			memcpy(d, r.d, dLen);
		}
		cs = r.cs;  //校验码
		tail = r.tail;
	}

	DLT645_2007_PKT(const DLT645_2007_PKT& r)
	{
		copy(r);
	}

	DLT645_2007_PKT& operator=(const DLT645_2007_PKT& pd) {
		copy(pd);
		return *this;
	}

	bool unpack() override {
		int preambleCount = 0;
		//支持0-n的任意个前导字符
		while (preambleCount < len) {
			if (data[preambleCount] == 0xFE) {
				preambleCount++;
			}
			else {
				break;
			}
		}

		preamble = preambleCount;
		unsigned char* p = data + preambleCount;
		head = p[0] ;
		memcpy(addr, p + 1, 6);
		head1 = p[7];
		ctrlCode = p[8];
		dLen = p[9];

		if (d != nullptr) {
			delete d;
		}
		d = new unsigned char[dLen];

		memcpy( d, p + 10, dLen);
		cs = p[10 + dLen];
		tail = p[11 + dLen];

		return true;
	};
	bool unpack(unsigned char* p, int len, bool withDetail = false) override {
		setData(p, len);
		return unpack();
	};
	bool pack() override {
		if (data)delete data;
		len = D07_FRAME_LEN_MIN + dLen;
		data = new unsigned char[len];
		data[0] = head;
		memcpy(data + 1, addr, 6);
		data[7] = head1;
		data[8] = ctrlCode;
		data[9] = dLen;
		memcpy(data + 10, d, dLen);
		cs = 0;
		for (int i = 0; i < 10 + dLen; i++) {
			cs += data[i];
		}
		data[10 + dLen] = cs;
		data[11 + dLen] = tail;
		return true;
	}

	string getAddrString()
	{
		std::string str;
		for (int i = 5; i < 0;i--) {
			str += to_string(addr[i]);
		}
		return str;
	}
};

#pragma pack()

