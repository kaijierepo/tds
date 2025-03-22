#pragma once

//CIP（Common Industrial Protocol）是一种用于工业自动化的通信协议，
// 支持多种网络类型，如 EtherNet / IP、DeviceNet 和 ControlNet
//EtherNet/IP 是 CIP 协议在以太网上的实现。
//EtherNet/IP 使用TCP/IP 传输 CIP 数据包，确保设备间的高效通信。
//协议解析上，EIP的数据包内部封装了CIP的数据包

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



enum CPF_ITEM_ID {
  ITEM_ID_NULL = 0x0000, // 空
  ITEM_ID_UNCONNECTED_MSG = 0x00B2 // 未连接数据
};


/**  EIP Adaption of CIP  2-4.1
 * Ethernet/IP 数据包 24 字节包头结构
 *
 * 1. Command (2 bytes): 命令字段，指示数据包的类型或操作。
 *    - 0x0001: ListIdentity 请求
 *    - 0x0004: ListIdentity 响应
 *    - 0x0063: RegisterSession 请求
 *    - 0x0064: RegisterSession 响应
 *    - 0x0065: UnRegisterSession 请求
 *    - 0x0066: UnRegisterSession 响应
 *    - 0x006F: SendRRData (Request/Response 数据)
 *    - 0x0070: SendUnitData (Unconnected 数据)
 *
 * 2. Length (2 bytes): 数据包的长度（不包括包头本身）。
 *    - 表示从包头之后的数据长度。
 *
 * 3. Session Handle (4 bytes): 会话句柄，用于标识当前会话。
 *    - 由 RegisterSession 响应生成，后续通信需使用此值。
 *
 * 4. Status (4 bytes): 状态字段，指示请求或响应的状态。
 *    - 0x00000000: 成功
 *    - 其他值表示错误代码。
 *
 * 5. Sender Context (8 bytes): 发送方上下文，用于匹配请求和响应。
 *    - 通常由发送方生成，响应包会原样返回。
 *
 * 6. Options (4 bytes): 选项字段，通常为 0。
 *    - 保留字段，未使用时设置为 0。
 */

struct EIP_PKT_HEADER {
	unsigned short command;             // 命令码
	unsigned short length;              // 数据长度
	unsigned char session_handle[4];   // 会话句柄
	unsigned char status[4];           // 状态
	unsigned char sender_context[8];   // 发送方上下文，server端原样返回
	unsigned char options[4];// 可选项

	EIP_PKT_HEADER() {
		memset(this, 0, sizeof(EIP_PKT_HEADER));
	}
};

struct UCMM_HEADER {
	unsigned char interface_handle[4];
	unsigned short timeout;
	unsigned short item_count;
	unsigned short addr_type_id;
	unsigned short addr_len;
	unsigned short data_type_id;
	unsigned short data_len;
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
	UCMM_HEADER ucmm_header;
	CIP_Request cip_req;
	CIP_Response cip_resp;


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
		//if (data)delete data;
		//len = 24 + header.length;
		//data = new unsigned char[len];
		//memcpy(data, &header, 24);
		//memcpy(data + 24, cmdData, header.length);
		return true;
	}
	//* UCMM Request pkt structure  @EIP Adaption of CIP  3-3.1
	// EIP header 24bytes
	// Cmd specific
	//    - Interface handle
	//    - timeout
	//    Common Pkt Formt
	//       - ItemCount
	//       - Addr item 1
	//       - Data item 1
	//       - Addr item 2
	//            - Addr Type id
	//			  - Addr len
	//       - Data item 2
	//            - Data Type id
	//            - data len
	//            - data  CIP Message Router Request @CIP Common Specification 2-4  vendor specific
	//                  -Service            USINT             Service code of the request.
	//                  -Request_Path_Size  USINT             The number of 16 bit words in the Request_Path field(next element).
	//	                -Request_Path       Padded EPATH      This is an array of bytes whose contents convey the path of the request(Class ID, Instance ID, etc.) for this transaction.
	//	                -Request_Data       Array of octet    Service specific data to be delivered in the Explicit Messaging Request.If no additional data is to be sent with the Explicit Messaging Request, then this array will be empty
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

	bool pack_Micro800_readTag(string plcTag, unsigned char* sessionHandle) {
		EIP_PKT& req = *this;
		req.clear();

		CIP_PKT cip_readTag;
		cip_readTag.pack_CIPService_readTag(plcTag);

		EIP_PKT cmdSendRR;
		cmdSendRR.pack_sendRRData_cmd(cip_readTag);


		EIP_PKT_HEADER header;
		header.command = EIP_CMD_SEND_RR_DATA;
		memcpy(header.session_handle, sessionHandle, 4);
		header.length = cmdSendRR.len;
		req.pushData(&header, sizeof(header));
		req.pushData(cmdSendRR.data, cmdSendRR.len);
		return true;
	}

	bool pack_Micro800_writeTag(string plcTag, unsigned char* sessionHandle, unsigned short tagType, TAG_VAL tagVal) {
		EIP_PKT& req = *this;
		req.clear();

		CIP_PKT cip_readTag;
		cip_readTag.pack_CIPService_writeTag(plcTag,tagType,tagVal);

		EIP_PKT cmdSendRR;
		cmdSendRR.pack_sendRRData_cmd(cip_readTag);


		EIP_PKT_HEADER header;
		header.command = EIP_CMD_SEND_RR_DATA;
		memcpy(header.session_handle, sessionHandle, 4);
		header.length = cmdSendRR.len;
		req.pushData(&header, sizeof(header));
		req.pushData(cmdSendRR.data, cmdSendRR.len);
		return true;
	}

	bool unpack_Micro800_writeTag(CIP_Response& cip_resp) {
		int h1 = sizeof(EIP_PKT_HEADER);
		int h2 = sizeof(UCMM_HEADER);
		memcpy(&header, data, sizeof(EIP_PKT_HEADER));
		memcpy(&ucmm_header, data + sizeof(EIP_PKT_HEADER), sizeof(UCMM_HEADER));

		unsigned char* pCip = data + sizeof(EIP_PKT_HEADER) + sizeof(UCMM_HEADER);

		cip_resp.reply_service = pCip[0];
		cip_resp.reserved = pCip[1];
		cip_resp.general_status = pCip[2];
		cip_resp.size_of_additional_status = pCip[3];
		if (cip_resp.size_of_additional_status > 0) {
			for (int i = 4; i+2 < ucmm_header.data_len;) {
				unsigned short* additional_status = (unsigned short*)(pCip + i);
				cip_resp.additional_status.push_back(*additional_status);
				i += 2;
			}
		}
		else {
			for (int i = 4; i < ucmm_header.data_len; i++) {
				cip_resp.response_data.push_back(pCip[i]);
			}
		}
		return true;
	}

	bool unpack_Micro800_readTag(CIP_Response& cip_resp) {
		int h1 = sizeof(EIP_PKT_HEADER);
		int h2 = sizeof(UCMM_HEADER);
		memcpy(&header, data, sizeof(EIP_PKT_HEADER));
		memcpy(&ucmm_header, data + sizeof(EIP_PKT_HEADER), sizeof(UCMM_HEADER));

		unsigned char* pCip = data + sizeof(EIP_PKT_HEADER) + sizeof(UCMM_HEADER);
		
		cip_resp.reply_service = pCip[0];
		cip_resp.reserved = pCip[1];
		cip_resp.general_status = pCip[2];
		cip_resp.size_of_additional_status = pCip[3];
		if (cip_resp.size_of_additional_status > 0) {
			for (int i = 4; i < ucmm_header.data_len; i++) {
				cip_resp.additional_status.push_back(pCip[i]);
			}
		}
		else {
			for (int i = 4; i < ucmm_header.data_len; i++) {
				cip_resp.response_data.push_back(pCip[i]);
			}
		}
		return true;
	}

	bool pack_ControlLogix_readTag(string plcTag,unsigned char* sessionHandle,unsigned char slot) {
		EIP_PKT& req = *this;
		req.clear();

		CIP_PKT cip_readTag;
		cip_readTag.pack_CIPService_readTag(plcTag);

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

