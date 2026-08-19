#ifndef TDS_IO_SERVER_PROTO_BACNET_H
#define TDS_IO_SERVER_PROTO_BACNET_H


#include "proto_common.h"

#pragma pack(1)

/* 参考文档
INTERNATIONAL 
STANDARD
ISO
16484 - 5
Sixth edition
2017 - 05
-
Building automation and control
systems(BACS) -
Part 5:
Data communication protocol
*/



// BACnet 协议相关常量定义
const unsigned short BACNET_IP_PORT = 47808;
const unsigned int BACNET_MAX_APDU = 1476;

namespace Bacnet_Service {
	const unsigned char I_Am = 0x00;
	const unsigned char Who_Is = 0x08;
	const unsigned char Read_Property = 12;
	const unsigned char Read_Property_Multiple = 14;
	const unsigned char Write_Property = 15;
}

enum BACNET_OBJECT_TYPE {
	ANALOG_INPUT = 0x0,
	ANALOG_OUTPUT = 0x1,
	ANALOG_VALUE = 0x2,
	BINARY_INPUT = 0x3,
	BINARY_OUTPUT = 0x4,
	BINARY_VALUE = 0x5,
	CALENDAR = 0x6,
	COMMAND = 0x7,
	DEVICE = 0x8,
	EVENT_ENROLLMENT = 0x9,
	BOT_FILE = 0xA,
	BOT_GROUP = 0xB,
	LOOP = 0xC,
	MULTI_STATE_INPUT = 0xD,
	MULTI_STATE_OUTPUT = 0xE,
	MULTI_STATE_VALUE = 0xF,
	NOTIFICATION_CLASS = 0x10,
	PROGRAM = 0x11
};



namespace Application_Tag_Number {
	const unsigned char Null = 0;
	const unsigned char	Boolean = 1;	
	const unsigned char	Unsigned_Integer = 2;
	const unsigned char	Signed_Integer = 3; // (2's complement notation) 
	const unsigned char	Real = 4; // (ANSI / IEEE - 754 floating point)
	const unsigned char	Double = 5; // (ANSI / IEEE - 754 double precision floating point)
	const unsigned char	Octet_String = 6;
	const unsigned char	Character_String = 7;
	const unsigned char	Bit_String = 8;
	const unsigned char	Enumerated = 9;
	const unsigned char	Date = 10;
	const unsigned char	Time = 11;
	const unsigned char	BACnetObjectldentifier = 12;
}


//  6.2.2 Network Layer Protocol Control Information
struct Net_Layer_Ctrl {
	//Network priority Where:
	//  B'11' = Life Safety message
	//	B'10' = Critical Equipment 1nessage
	//	B'01' = Urgent message
	//	B'00' = Normal message
	unsigned char priority : 2;

	//The value of this bit corresponds to the data_ expecting_reply parameter in the N-UNITDATA primitives.
	//	1 indicates that a BACnet - Confirmed - Request - PDU, a segment of a BACnet - ComplexACK - PDU, or a
	//	network layer message expecting a reply is present.
	//	0 indicates that other than a BACnet - Confirmed - Request - PDU, a segment of a BACnet - ComplexACK - PDU,
	//	or a network layer message expecting a reply is present.
	unsigned char expect_reply : 1;

	//Source specifier where:
	//0 = SNET, SLEN, and SADR absent
	//	l = SNET, SLEN, and SADR present
	//	SLEN = 0 Invalid
	//	SLEN > 0 specifies length of SADR field
	unsigned char src : 1;

	unsigned char reserve1 : 1;
	//0 = DNET, DLEN, DADR, and Hop Count absent
	//l = DNET, DLEN, and Hop Count present
	//DLEN = 0 denotes broadcast MAC DADR and DADR field is absent
	//DLEN > 0 specifies length ofDADR field

	unsigned char dest : 1;
	unsigned char reserve : 1;
	//1 indicates that the NSDU conveys a net, vork layer message.Message Type field is present.
	//0 indicates that the NSDU contains a BACnet APDU.Message Type field is absent.
	unsigned char nsdu : 1;
};

// BACnet/IP BACnet Virtual Link Control 消息头
struct BVLC_Header {
	unsigned char type;
	unsigned char  function;
	unsigned char len[2];

	unsigned short getLen() {
		return len[0] * 256 + len[1];
	}

	void setLen(unsigned short v) {
		len[0] = v / 256;
		len[1] = v % 256;
	}

	BVLC_Header() {
		memset((char*)this, 0, sizeof(this));
		type = 0x81; //BVLL for BACnet / lP
	}
};


struct NPDU_Header {
	unsigned char version;
	Net_Layer_Ctrl ctrl;
	unsigned short DNET;     //destination network address
	unsigned char  DLEN;     //1 octet length of ultimate destination MAC layer address(A value of O indicates a broadcast on the destination network.)
	unsigned char DADR[32];  //Ultilnate destination MAC layer address.
	unsigned char DA[32];    //Local network destination MAC layer address
	unsigned short SNET;     //含义与上面4个相同，下面是source,上面是destination
	unsigned char SLEN;
	unsigned char SADR[32];
	unsigned char SA[32];
	unsigned char hop_count;

	NPDU_Header() {
		memset((char*)this, 0, sizeof(this));
		version = 1;
	}
};


// 定义BACnet协议的APDU类型宏，前缀为 APDU_TYPE
namespace APDU_TYPE {
	const unsigned char CONFIRMED_REQUEST = 0x00; // 确认请求类型的APDU
	const unsigned char UNCONFIRMED_REQUEST = 0x01; // 未经确认的请求类型的APDU
	const unsigned char SIMPLE_ACK = 0x02; // 简单确认类型的APDU
	const unsigned char COMPLEX_ACK = 0x03; // 复杂确认类型的APDU
	const unsigned char SEGMENT_ACK = 0x04; // 分段确认类型的APDU
	const unsigned char Error = 0x05; // 错误类型的APDU
	const unsigned char Reject = 0x06; // 拒绝类型的APDU
	const unsigned char Abort = 0x07; // 放弃类型的APDU
};

//BACnet Virtual Link Layer
//BACnet Virtual Link Control functions
namespace BVLC_Function {
	const unsigned char Original_Broadcast_NPDU = 0x0b;
	const unsigned char Original_Unicast_NPDU = 0x0a;

};

struct APDU_Type {
	unsigned char reserve : 1;
	unsigned char segmented_response_accepted : 1;
	unsigned char more_segments : 1;
	unsigned char segmented_request : 1;
	unsigned char type : 4;   //高位 MSB

	APDU_Type() {
		memset(this, 0, 1);
	}
};

struct APDU_ACCEPT_INFO {
	unsigned char size_of_max_apdu_accepted : 4;
	unsigned char max_segment_response_accepted : 3;
	unsigned char reserve : 1;

	APDU_ACCEPT_INFO() {
		memset(this, 0, 1);
	}
};


struct APDU_Header {
	unsigned char reserve : 1;
	unsigned char segmented_response_accepted : 1;
	unsigned char more_segments : 1;
	unsigned char segmented_request : 1;
	unsigned char type : 4;   //高位 MSB
	unsigned char service_choice;
};

// BACnet 属性标识符
struct BacnetPropertyIdentifier {
	unsigned int propertyIdentifier;
};


struct Bacnet_Tag {
	unsigned char Length_Value_Type : 3;
	unsigned char Class : 1;
	unsigned char TagNumber : 4;

	Bacnet_Tag() {
		memset(this, 0, 1);
	}
};

struct Object_Identifier {
	unsigned char data[4];
	unsigned int get_instance_number() {
		unsigned int v = 0;
		v += (data[1] & 0b00111111) * 256 * 256;
		v += data[2] * 256;
		v += data[3];
		return v;
	}

	void set_instance_numnber(unsigned int v) {
		data[1] = data[1] & 0b11000000;
		unsigned char b = v >> 16;
		data[1] = data[1] | b;
		data[2] = (v >> 8) % 256;
		data[3] = v % 256;
	}

	unsigned int get_object_type() {
		unsigned int v = 0;
		v += data[0] << 2;
		v += data[1] >> 6;
		return v;
	}

	void set_object_type(unsigned int v) {
		data[0] = v >> 2;
		data[1] = data[1] & 0b00111111;
		data[1] = data[1] | ((v % 4) << 6);
	}
};


inline BACNET_OBJECT_TYPE getBacnetObjType(string sType) {
	if (sType == "analog-input") {
		return BACNET_OBJECT_TYPE::ANALOG_INPUT;
	}
	else if (sType == "analog-output") {
		return BACNET_OBJECT_TYPE::ANALOG_OUTPUT;
	}
	else if (sType == "binary-input") {
		return BACNET_OBJECT_TYPE::BINARY_INPUT;
	}
	else if (sType == "binary-output") {
		return BACNET_OBJECT_TYPE::BINARY_OUTPUT;
	}
	else {
		return BACNET_OBJECT_TYPE::ANALOG_INPUT;
	}
}

struct BACNET_VAL {
	float fVal;
	bool bVal;
};


class BACNET_PKT : public DEV_PKT{
public:
	BVLC_Header BVLC_header;
	NPDU_Header NPDU_header;
	APDU_Header APDU_header;

	unsigned char* cmdData;

	BACNET_PKT() {

	}

	~BACNET_PKT() {

	}


	BACNET_PKT(const BACNET_PKT& r)
	{
		copy(r);
	}

	BACNET_PKT& operator=(const BACNET_PKT& pd) {
		copy(pd);
		return *this;
	}

	bool unpack() override {
		//memcpy(&header, data, sizeof(header));
		//cmdData = new unsigned char[len - sizeof(header)];
		//memcpy(cmdData, data + sizeof(header), len - sizeof(header));
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

	bool pack_who_is() {
		BVLC_header.function = BVLC_Function::Original_Broadcast_NPDU;

		NPDU_header.ctrl.dest = 1;
		NPDU_header.DNET = 0xffff;
		NPDU_header.DLEN = 0;
		NPDU_header.hop_count = 0xff;

		APDU_header.type = APDU_TYPE::UNCONFIRMED_REQUEST;
		APDU_header.service_choice = Bacnet_Service::Who_Is; 
		return true;
	}

	bool pack_write_current_val(unsigned char invokeId, BACNET_OBJECT_TYPE objType, unsigned int instanceId, BACNET_VAL val) {
		transactionId = invokeId;

		DEV_PKT APDU;
		APDU_Type type;
		type.type = APDU_TYPE::CONFIRMED_REQUEST;
		unsigned char bType = *(unsigned char*)&type;
		APDU.pushData(bType);

		APDU_ACCEPT_INFO accpetInfo;
		accpetInfo.max_segment_response_accepted = 0;
		accpetInfo.size_of_max_apdu_accepted = 5;
		unsigned char bAccpetInfo = *(unsigned char*)&accpetInfo;
		APDU.pushData(bAccpetInfo);

		APDU.pushData(invokeId);

		APDU.pushData(Bacnet_Service::Write_Property);

		Bacnet_Tag bt;
		bt.Class = 1; //context specific tag;
		bt.TagNumber = 0;
		bt.Length_Value_Type = 4;
		unsigned char bTag_objIdent = *(unsigned char*)&bt;
		APDU.pushData(bTag_objIdent);

		Object_Identifier objIdent;
		objIdent.set_object_type(objType);
		objIdent.set_instance_numnber(instanceId);
		APDU.pushData(objIdent.data, 4);

		bt.Class = 1; //context specific tag;
		bt.TagNumber = 1;
		bt.Length_Value_Type = 1;
		unsigned char byteTag = *(unsigned char*)&bt;
		APDU.pushData(byteTag);

		unsigned char property_ident = 85; //present value
		APDU.pushData(property_ident);

		unsigned char tag_3e = 0x3e; //opening tag;
		APDU.pushData(tag_3e);

		bt.Class = 0; //application tag;
		if (objType == BACNET_OBJECT_TYPE::BINARY_OUTPUT) {
			bt.TagNumber = Application_Tag_Number::Enumerated;
			bt.Length_Value_Type = 1;
		}
		else if (objType == BACNET_OBJECT_TYPE::ANALOG_OUTPUT) {
			bt.TagNumber = Application_Tag_Number::Real;
			bt.Length_Value_Type = 4;
		}
		byteTag = *(unsigned char*)&bt;
		APDU.pushData(byteTag);

		if (objType == BACNET_OBJECT_TYPE::BINARY_OUTPUT) {
			unsigned char ucVal = val.bVal ? 1 : 0;
			APDU.pushData(ucVal);
		}
		else if (objType == BACNET_OBJECT_TYPE::ANALOG_OUTPUT) {
			APDU.pushData(&val.fVal,4);
		}

		unsigned char tag_3f = 0x3f; //closing tag;
		APDU.pushData(tag_3f);

		bt.Class = 1; //context specific tag
		bt.TagNumber = 4;
		bt.Length_Value_Type = 1;
		byteTag = *(unsigned char*)&bt;
		APDU.pushData(byteTag);

		unsigned char priority = 16;
		APDU.pushData(priority);

		DEV_PKT NPDU;
		unsigned char version = 1;
		NPDU.pushData(version);
		unsigned char ctrl = 4; //expecting reply
		NPDU.pushData(ctrl);
		NPDU.pushData(APDU.data, APDU.len);

		unsigned char BVLC_type = 0x81; //Anex J Bacnet/IP
		pushData(BVLC_type);
		unsigned char BVLC_func = BVLC_Function::Original_Unicast_NPDU;
		pushData(BVLC_func);
		unsigned short BVLC_len = NPDU.len + 4;
		unsigned char hiByte = BVLC_len/256;
		unsigned char loByte = BVLC_len%256;
		pushData(hiByte);
		pushData(loByte);
		pushData(NPDU.data, NPDU.len);

		return true;
	}

	bool pack_read_current_val(unsigned char invokeId, BACNET_OBJECT_TYPE objType,unsigned int instanceId) {
		transactionId = invokeId;
		
		DEV_PKT APDU;
		APDU_Type type;
		type.type = APDU_TYPE::CONFIRMED_REQUEST;
		type.segmented_response_accepted = 1;
		unsigned char bType = *(unsigned char*)&type;
		APDU.pushData(bType);

		APDU_ACCEPT_INFO accpetInfo;
		accpetInfo.max_segment_response_accepted = 7;
		accpetInfo.size_of_max_apdu_accepted = 5;
		unsigned char bAccpetInfo = *(unsigned char*)&accpetInfo;
		APDU.pushData(bAccpetInfo);

		APDU.pushData(invokeId);

		APDU.pushData(Bacnet_Service::Read_Property_Multiple);

		Bacnet_Tag tag_objIdent;
		tag_objIdent.Class = 1; //context specific tag;
		tag_objIdent.TagNumber = 0;
		tag_objIdent.Length_Value_Type = 4;
		unsigned char bTag_objIdent = *(unsigned char*)&tag_objIdent;
		APDU.pushData(bTag_objIdent);

		Object_Identifier objIdent;
		objIdent.set_object_type(objType);
		objIdent.set_instance_numnber(instanceId);
		APDU.pushData(objIdent.data, 4);

		unsigned char openingTag = 0x1e;
		APDU.pushData(openingTag);

		Bacnet_Tag bt;
		bt.Class = 1;
		bt.TagNumber = 0;
		bt.Length_Value_Type = 1;
		unsigned char bBt = *(unsigned char*)&bt;
		APDU.pushData(bBt);

		unsigned char propertyIdent = 85; //present value
		APDU.pushData(&propertyIdent, 1);

		bt.Class = 1;
		bt.TagNumber = 0;
		bt.Length_Value_Type = 1;
		bBt = *(unsigned char*)&bt;
		APDU.pushData(&bt, 1);

		propertyIdent = 111; //present value
		APDU.pushData(&propertyIdent, 1);

		unsigned char closingTag = 0x1f;
		APDU.pushData(closingTag);

		DEV_PKT NPDU;
		unsigned char version = 1;
		NPDU.pushData(version);
		unsigned char ctrl = 4; //expecting reply
		NPDU.pushData(ctrl);
		NPDU.pushData(APDU.data, APDU.len);

		unsigned char BVLC_type = 0x81; //Anex J Bacnet/IP
		pushData(BVLC_type);
		unsigned char BVLC_func = BVLC_Function::Original_Unicast_NPDU;
		pushData(BVLC_func);
		unsigned short BVLC_len = NPDU.len + 4;
		unsigned char hiByte = BVLC_len/256;
		unsigned char loByte = BVLC_len%256;
		pushData(hiByte);
		pushData(loByte);
		pushData(NPDU.data, NPDU.len);

		return true;
	}
};

#pragma pack()


#endif /* TDS_IO_SERVER_PROTO_BACNET_H */
