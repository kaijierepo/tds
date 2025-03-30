#pragma once

#include "proto_common.h"
#include "proto_logix_plc.hpp"

#pragma pack(1)

//@Common CIP Specific 2-4
struct CIP_Request {
	unsigned char service;
	unsigned char request_path_size;
	vector<unsigned short> path;  //This is an array of bytes whose contents convey the path of the request(Class ID, Instance ID, etc.) for this transaction
	vector<unsigned char> req_data; //Service specific data to be delivered in the Explicit Messaging Request.If no additional data is to be sent with the Explicit Messaging Request,then this array will be empty
};

struct CIP_Response {
	unsigned char  reply_service;
	unsigned char  reserved;
	unsigned char  general_status;
	unsigned char  size_of_additional_status;
	vector<unsigned short> additional_status;
	vector<unsigned char> response_data;
};


enum Connection_Manager_Object_Service {
	Forward_Open = 0x54,
	Unconnected_Send = 0x52,
	Forward_Close = 0x4E
};


enum CIP_SERVICE {
	Read_Tag = 0x4c,
	Read_Tag_Fragmented = 0x52,
	Write_Tag = 0x4d,
	Write_Tag_Fragmented = 0x53
};

struct TAG_VAL {
	uint16_t INT_val;
	uint16_t SINT_val;
	uint32_t DINT_val;
	uint32_t DWORD_val;
	float REAL_val;
	uint16_t BOOL_val;
};


inline string getReadTagServiceErrorCodeDesc(unsigned char errCode) {
	if (errCode == 0x04)
		return "A syntax error was detected decoding the Request Path.";
	else if (errCode == 0x05)
		return "Request Path destination unknown : Probably instance number is not present";
	else if (errCode == 0x06)
		return "Insufficient Packet Space : Not enough room in the response buffer for all the data.";
	else if (errCode == 0x13)
		return "Insufficient Request Data : Data too short for expected parameters.";
	else if (errCode == 0x26)
		return "The Request Path Size received was shorter or longer than expected.";
	else if (errCode == 0xFF)
		return "General Error : Access beyond end of the object.";	
	return "";
}

inline string getWriteTagServiceErrorCodeDesc(unsigned char errCode, vector<unsigned short> additional_status) {
	if (errCode == 0x04)
		return "A syntax error was detected decoding the Request Path.";
	else if (errCode == 0x05)
		return "Request Path destination unknown : Probably instance number is not present";
	else if (errCode == 0x10) {
		if (additional_status.size() > 0) {
			if (additional_status[0] == 0x2101) {
				return "Device state conflict: keyswitch position: The requestor is changing force information in HARD RUN mode.";
			}
			else if (additional_status[0] == 0x2802) {
				return "Device state conflict: Safety Status: The controller is in a state in which Safety Memory cannot be modified.";
			}
		}	
	}
	else if (errCode == 0x13)
		return "Insufficient Request Data : Data too short for expected parameters.";
	else if (errCode == 0x26)
		return "The Request Path Size received was shorter or longer than expected.";
	else if (errCode == 0xFF) {
		if (additional_status.size() > 0) {
			if (additional_status[0] == 0x2105) {
				return "General Error: Number of Elements extends beyond the end of the requested tag.";
			}
			else if (additional_status[0] == 0x2107) {
				return "General Error: Tag type used n request does not match the target tag’s data type.";
			}
		}
	}
	return "";
}

struct Path_Segment {
	unsigned char path_segment_type : 3;

};

//CIP Common Specification Table 3 - 5.15.Unconnected Send Service ParametersTable 
struct Unconnected_Send_Service_Parameters {
	unsigned char priority : 4;
	unsigned char tick_time : 4;
	unsigned char time_out_ticks;
	unsigned short message_request_size;
	CIP_Request msg_req;
	unsigned char pad;
	unsigned char route_path_size;
	unsigned char reserved;
	unsigned char route_path[2];
};


class CIP_PKT : public DEV_PKT {
public:
	//调用Connection Manager Object的服务转发数据包，因此多封装了一层
	//ab1756使用该机制，micro800未使用该机制
	//回包的格式是一样的，Connection Manager Object的包结构不会在回包中体现。
	bool pack_unconnected_msg_send(CIP_PKT& msg,unsigned char slot) {
		DEV_PKT& req = *this;
		req.clear();
		//cip header unconnected service
		//servcie + req path size + req path  6
		//serice is defined in CIP Common Specification 3-5.5 Connection Manager Object Instance Object Specific Services
		//此处可以简单看成是一个固定长度的 connnectedServiceHeader,len = 10
		unsigned char service = Connection_Manager_Object_Service::Unconnected_Send;
		req.pushData(service);
		unsigned char req_path_size = 2;  // 2 words
		req.pushData(req_path_size);
		unsigned char path_segment_type = 0x20;  //8-bit class segment
		req.pushData(path_segment_type);
		unsigned char class_type = 0x06;   //class manager
		req.pushData(class_type);
		path_segment_type = 0x24;  //8-bit instance segment
		req.pushData(path_segment_type);
		unsigned char instance_type = 0x01;  
		req.pushData(instance_type);
		string s = "04 7d"; //priority tick time; 2 定义设备在发送高优先级数据前的等待时间,等待这么长时间后就优先发送
		req.pushData(s);


		unsigned short& embedded_message_request_size = *(unsigned short*)req.pushData(nullptr, 2);//2
		embedded_message_request_size = msg.len;

		req.pushData(msg.data, msg.len);

		//下面4字节
		unsigned char route_path_size = 1; //1 word;
		req.pushData(route_path_size);
		unsigned char reserved = 0;
		req.pushData(reserved);
		unsigned char objAddr[2];
		objAddr[0] = 1; //port = backplane
		objAddr[1] = slot;
		req.pushData(objAddr, 2);
		return true;
	}

	bool pack_CIPService_readTag(string plcTag) {
		DEV_PKT& req = *this;
		req.clear();
		unsigned char service = CIP_SERVICE::Read_Tag;
		req.pushData(service);
		//计算path的大小
		unsigned char tagLen = plcTag.length();
		int req_path_byte_size = tagLen + 2;
		bool needPad = false;
		if (req_path_byte_size % 2 > 0) {
			req_path_byte_size++;
			needPad = true;
		}

		//@Logix 5000 Data Access P14
		unsigned char req_path_size = req_path_byte_size / 2;
		req.pushData(req_path_size);
		unsigned char path_segment_type = 0x91;//ANSI Extended Symbol Segment
		req.pushData(path_segment_type);
		req.pushData(tagLen);
		req.pushData(plcTag.data(), plcTag.length());
		if (needPad)
		{
			req.pushData(nullptr, 1);
		}
		unsigned short req_data = 1; //number of elements to read
		req.pushData(req_data);
		return true;
	}


	//使用位号名称读取的格式
	// servcie  1
	// path size 1
	// path_segment_type 1
	// tag asc   n
	// 
	bool pack_CIPService_writeTag(string plcTag,unsigned short tagType, TAG_VAL tagVal) {
		DEV_PKT& req = *this;
		req.clear();

		// request service
		unsigned char service = CIP_SERVICE::Write_Tag;
		req.pushData(service);

		// request path size
		//计算path的大小
		unsigned char tagLen = plcTag.length();
		//一个字节是 0x91表示字符串寻址，1个字节是长度,最后一个padding字符1，是长度是2的倍数1
		// 0x91 len 1stChar ... NthChar (1) 
		int req_path_byte_size = tagLen + 2; 
		bool needPad = false;
		if (req_path_byte_size % 2 > 0) {
			req_path_byte_size++;
			needPad = true;
		}
		unsigned char req_path_size = req_path_byte_size / 2;
		req.pushData(req_path_size);

		//request path 
		unsigned char path_segment_type = 0x91;//ANSI Extended Symbol Segment
		req.pushData(path_segment_type);
		req.pushData(tagLen);
		req.pushData(plcTag.data(), plcTag.length());
		if (needPad)
		{
			req.pushData(nullptr, 1);
		}

		//request data
		unsigned short tag_type = tagType;
		req.pushData(tag_type);
		unsigned short number_of_elements_to_write = 1;
		req.pushData(number_of_elements_to_write);
		if (tagType == LOGIX_TAG_VAL_TYPE::Bool) {
			req.pushData(tagVal.BOOL_val);
		}
		else if (tagType == LOGIX_TAG_VAL_TYPE::Int) {
			req.pushData(tagVal.INT_val);
		}
		else if (tagType == LOGIX_TAG_VAL_TYPE::DInt) {
			req.pushData(tagVal.DINT_val);
		}
		else if (tagType == LOGIX_TAG_VAL_TYPE::DWord) {
			req.pushData(tagVal.DWORD_val);
		}
		else if (tagType == LOGIX_TAG_VAL_TYPE::Real) {
			req.pushData(tagVal.REAL_val);
		}
		else if (tagType == LOGIX_TAG_VAL_TYPE::SInt) {
			req.pushData(tagVal.SINT_val);
		}
		return true;
	}
};

#pragma pack()

