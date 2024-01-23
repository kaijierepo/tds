#pragma once

#include "proto_common.h"
#include "proto_cip.hpp"

#pragma pack(1)


#define EIP_CMD_NOP             0x00    // 空操作
#define EIP_CMD_LIST_SERVICES   0x04    // 列出所有服务
#define EIP_CMD_LIST_IDENTITY   0x63    // 列出身份信息
#define EIP_CMD_LIST_INTERFACES 0x64    // 列出接口信息
#define EIP_CMD_REGISTER_SESSION 0x65   // 注册会话
#define EIP_CMD_UNREGISTER_SESSION 0x66 // 注销会话
#define EIP_CMD_SEND_RR_DATA    0x6F    // 发送请求/响应数据
#define EIP_CMD_SEND_UNIT_DATA  0x70    // 发送单元数据
#define EIP_CMD_INDICATE_STATUS 0x72    // 指示状态
#define EIP_CMD_CANCEL          0x7E    // 取消
#define EIP_CMD_MULTI_SERVICE_PACKET 0x0A // 多服务数据包


#define ITEM_ID_NULL 0x0000 // 空
#define ITEM_ID_UNCONNECTED_MSG 0x00B2 // 未连接数据


struct EIP_PKT_HEADER {
	unsigned short command;             // 命令码
	unsigned short length;              // 数据长度
	unsigned char session_handle[4];   // 会话句柄
	unsigned short status;           // 状态
	unsigned char sender_context[8];   // 发送方上下文，server端原样返回
	unsigned char options[6];// 可选项

	EIP_PKT_HEADER() {
		memset(this, 0, sizeof(EIP_PKT_HEADER));
	}
};


struct SEND_RR_DATA_REQ {
	unsigned char interface_handle[4];
	unsigned short timeout;
};


struct COMMON_PKT_ITEM_HEADER {
	unsigned short type_id;
	unsigned short length;
};

struct COMMON_PKT {
	unsigned short item_count;
};


class EIP_PKT : public DEV_PKT{
public:
	EIP_PKT_HEADER header;
	unsigned char* cmdData;

	EIP_PKT() {

	}

	~EIP_PKT() {

	}


	EIP_PKT(const EIP_PKT& r)
	{
		copy(r);
	}

	EIP_PKT& operator=(const EIP_PKT& pd) {
		copy(pd);
		return *this;
	}

	bool unpack() override {
		memcpy(&header, data, sizeof(header));
		cmdData = new unsigned char[len - sizeof(header)];
		memcpy(cmdData, data + sizeof(header), len - sizeof(header));
		return true;
	};
	bool unpack(unsigned char* p, int len, bool withDetail = false) override {
		setData(p, len);
		return unpack();
	};
	bool pack() override {
		if (data)delete data;
		len = 24 + header.length;
		data = new unsigned char[len];
		memcpy(data, &header, 24);
		memcpy(data + 24, cmdData, header.length);
		return true;
	}

	bool pack_sendRRData_cmd(CIP_PKT& cipPkt) {
		DEV_PKT& req = *this;
		unsigned char interface_handle[4] = { 0 };
		req.pushData(interface_handle, 4);
		unsigned short timeout = 10;
		req.pushData(&timeout, 2);
		//common packet format
		unsigned short item_count = 2;
		req.pushData(&item_count, 2);
		//address item
		COMMON_PKT_ITEM_HEADER addrItem;
		addrItem.type_id = ITEM_ID_NULL;
		addrItem.length = 0;
		req.pushData(&addrItem, sizeof(addrItem));
		//data item
		COMMON_PKT_ITEM_HEADER& dataItem = *(COMMON_PKT_ITEM_HEADER*)req.pushData(nullptr, sizeof(COMMON_PKT_ITEM_HEADER));
		dataItem.type_id = ITEM_ID_UNCONNECTED_MSG;

		dataItem.length = cipPkt.len;
		req.pushData(cipPkt.data, cipPkt.len);

		return true;
	}


	bool pack_ControlLogix_readTag(string plcTag,unsigned char* sessionHandle,unsigned char slot) {
		EIP_PKT& req = *this;
		req.clear();

		CIP_PKT cip_readTag;
		cip_readTag.pack_ControlLogix_readTag(plcTag);

		CIP_PKT cip_unconnectMsg;
		cip_unconnectMsg.pack_unconnected_msg_send(cip_readTag,slot);

		EIP_PKT cmdSendRR;
		cmdSendRR.pack_sendRRData_cmd(cip_unconnectMsg);
		

		EIP_PKT_HEADER header;
		header.command = EIP_CMD_SEND_RR_DATA;
		memcpy(header.session_handle, sessionHandle, 4);
		header.length = cmdSendRR.len;
		req.pushData(&header, sizeof(header));
		req.pushData(cmdSendRR.data, cmdSendRR.len);
		return true;
	}
};

#pragma pack()

