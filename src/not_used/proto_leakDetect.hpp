#pragma once

#include "common.h"
#include "proto/proto_common.h"

/*  采样文件格式
3字节  长度 大端  （长度 = 3(长度字段) + 3(标记) + n(数据)）
3字节  压缩标记  01 00 00
n字节  压缩数据
3字节  采样时间
25字节 GPS数据
*/

#pragma pack(1)
#define PKT_HEAD_LEN 9  
#define PKT_TAIL_LEN 3
#define PKT_FRAME_LEN 12

namespace LDP_CMD {
	const unsigned char heatbeat_0xFF = 0xFF;
	const unsigned char acqSignal_0x22 = 0x22;
	const unsigned char readData_0x23 = 0x23;
	const unsigned char setAcqTime_0x25 = 0x25;
};

namespace RW_FLAG {
	const unsigned char READ = 0;
	const unsigned char WRITE = 1;
};

namespace ACQ_SIGNAL {
	const unsigned char START_0xAA = 0xAA;
	const unsigned char END_0x55 = 0x55;
}

namespace RET_CODE {
	const unsigned char SUCCESS_0x55 = 0x55;
	const unsigned char FAIL_0xAA = 0xAA;
}


struct LDP_FILE_TAIL {
	unsigned char acqTimeHour;
	unsigned char acqTimeMin;
	unsigned char acqTimeSec;
	char gps[25];
};


struct LDP_PKT : public DEV_PKT{
	unsigned short head; //头
	unsigned char cmdCode; //命令码
	unsigned char id[4];  //设备id
	unsigned char rwFlag; //读写标记
	unsigned char paramLen; //参数长度
	unsigned char checkCode;
	unsigned short tail;

	LDP_PKT() {
		head = 0xA5A5;
		tail = 0x5A5A;
		checkCode = 0xFF;
	}

	void setID(int iId) {
		id[3] = LOBYTE(LOWORD(iId));
		id[2] = HIBYTE(LOWORD(iId));
		id[1] = LOBYTE(HIWORD(iId));
		id[0] = HIBYTE(HIWORD(iId));
	}

	int getID() {
		return id[3];
	}

	unsigned char calcCheckCode(unsigned char* pData, int len) {
		byte crc = 0;
		for (int j = 0; j < len; j++)
		{
			crc ^= pData[j];
			for (int i = 0; i < 8; i++)
			{
				if ((byte)(crc & 0x01) > 0) crc = (byte)((byte)(crc >> 1) ^ 0x8C);
				else crc >>= 1;
			}
		}
		return crc;
	}

	virtual string GetCmdName() override
	{  
		if (cmdCode == LDP_CMD::acqSignal_0x22) {
			if (cmd_data[0] == ACQ_SIGNAL::START_0xAA)
			{
				return "开始AD采样0x22";
			}
			else if (cmd_data[0] == ACQ_SIGNAL::END_0x55)
			{
				return "结束AD采样0x22";
			}
			else
				return "采集信号0x22";
		}
		else if (cmdCode == LDP_CMD::heatbeat_0xFF) {
			return "心跳0xFF";
		}
		else if (cmdCode == LDP_CMD::readData_0x23) {
			return "读取文件0x23";
		}
		else if (cmdCode == LDP_CMD::setAcqTime_0x25) {
			if (rwFlag == RW_FLAG::READ)
			{
				return "读采集时间0x25";
			}
			else if (rwFlag == RW_FLAG::WRITE)
			{
				return "写采集时间0x25";
			}
			return "0x25";
		}
	}

	virtual bool pack(unsigned char ucCmdCode,void* pCmdData ,int cmdLen){
		cmdCode = ucCmdCode;
		
		if (cmdLen > 0) {
			cmd_data = new unsigned char[cmdLen];
			memcpy(cmd_data, pCmdData, cmdLen);
		}
		len = PKT_FRAME_LEN + cmdLen;
		data = new unsigned char[len];
		data[0] = 0xA5;
		data[1] = 0xA5;
		data[2] = ucCmdCode;
		memcpy(data + 3, id, 4);
		data[7] = rwFlag;
		data[8] = cmdLen;
		if (cmdLen > 0) 
			memcpy(data + 9, (char*)pCmdData, cmdLen);
		checkCode = calcCheckCode(data + 2, 7 + cmdLen);
		data[9 + cmdLen] = checkCode;
		data[9 + cmdLen + 1] = 0x5A;
		data[9 + cmdLen + 2] = 0x5A;
		return true;
	};
	bool unpack(unsigned char* pktData,int pktLen,bool withDetail = false) override { 
		if (pktData[0] != 0xa5 || pktData[1] != 0xa5)
			return false;
		if (pktData[pktLen - 1] != 0x5a || pktData[pktLen - 2] != 0x5a)
			return false;

		len = pktLen;
		data = new unsigned char[len];
		memcpy(data, pktData, len);
		cmdCode = data[2];
		memcpy(id, pktData + 3, 4);
		rwFlag = data[7];
		paramLen = data[8];
		cmd_data_len = len - PKT_FRAME_LEN;
		cmd_data = new unsigned char[cmd_data_len];
		memcpy(cmd_data, pktData + 9, cmd_data_len);
		checkCode = pktData[pktLen - 3];
		unsigned char checkCodeTrue = calcCheckCode(pktData + 2, pktLen - 5);

		if (withDetail) {
			m_strPktDetail = "包长度:" + str::fromInt(pktLen) + "\r\n";
			m_strPktDetail += "头:" + str::bytesToHexStr((char*)&head, 2) + "\r\n";
			m_strPktDetail += "命令:" + GetCmdName() + "\r\n";
			m_strPktDetail += "ID:" + str::bytesToHexStr(id,4) + "\r\n";
			m_strPktDetail += "读写标记:" + str::format("0x%x",rwFlag) + "\r\n";
			m_strPktDetail += "参数长度:" + str::format("0x%x", paramLen) + "\r\n";
			m_strPktDetail += "命令数据:" + str::bytesToHexStr(cmd_data, cmd_data_len) + "\r\n";
			m_strPktDetail += "校验码:" + str::format("0x%x", checkCode) + "("+ str::format("0x%x", checkCodeTrue) + ")\r\n";
			m_strPktDetail += "尾:" + str::bytesToHexStr((char*)&tail, 2) + "\r\n";
		}

		return true;
	};
};

struct LDP_READ_FILE_REQ {
	unsigned char subPktCount[2];
	unsigned char subPktNo[2];  //从0开始编号
	unsigned char subPktLen[2];

	LDP_READ_FILE_REQ() {
		setSubPktCount(599);
		setSubPktLen(1000);
	}

	unsigned short getSubPktCount() {
		return subPktCount[0] * 256 + subPktCount[1];
	}

	void setSubPktCount(unsigned short count) {
		subPktCount[0] = HIBYTE(count);
		subPktCount[1] = LOBYTE(count);
	}

	unsigned short getSubPktNo() {
		return subPktNo[0] * 256 + subPktNo[1];
	}

	void setSubPktNo(unsigned short count) {
		subPktNo[0] = HIBYTE(count);
		subPktNo[1] = LOBYTE(count);
	}

	unsigned short getSubPktLen() {
		return subPktLen[0] * 256 + subPktLen[1];
	}

	void setSubPktLen(unsigned short count) {
		subPktLen[0] = HIBYTE(count);
		subPktLen[1] = LOBYTE(count);
	}
};


struct LDP_READ_FILE_RESP {
	unsigned char paramLenExt;
	union {
		struct HEAD {
			unsigned char fileLen[3];
			unsigned char flag[3];

			bool isCompressFile() {
				if (flag[0] == 1 && flag[1] == 0 && flag[2] == 0)
					return true;
				return false;
			}

			void setCompressFlag() {
				flag[0] = 1;
				flag[1] = 0;
				flag[2] = 0;
			}

			int getFileLen() {
				return fileLen[0] * 256 * 256 + fileLen[1] * 256 + fileLen[2];
			}
			void setFileLen(int len){
				fileLen[0] = LOBYTE(HIWORD(len));
				fileLen[1] = HIBYTE(LOWORD(len));
				fileLen[2] = LOBYTE(LOWORD(len));
			}
		} head;
		unsigned char data[1000];
	}subPkt;
};

#pragma pack()

