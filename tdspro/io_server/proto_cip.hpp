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
	unsigned short reply_service;
	unsigned char  reserved;
	unsigned char  general_status;
	unsigned char  size_of_additional_status;
	vector<unsigned short> additional_status;
	vector<unsigned char> response_data;
};



enum CIP_SERVICE {
	Read_Tag = 0x4c,
	Read_Tag_Fragmented = 0x52,
	Write_Tag = 0x4d,
	Write_Tag_Fragmented = 0x53
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
	bool pack_unconnected_msg_send(CIP_PKT& msg,unsigned char slot) {
		DEV_PKT& req = *this;
		req.clear();
		//cip header unconnected service
		//servcie + req path size + req path  6
		string s = "52 02 20 06 24 01";
		req.pushData(s);
		s = "04 7d"; //priority tick time; 2
		req.pushData(s);
		unsigned short& message_request_size = *(unsigned short*)req.pushData(nullptr, 2);//2
		message_request_size = msg.len;

		req.pushData(msg.data, msg.len);//14

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
	bool pack_ControlLogix_readTag(string plcTag) {
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
};

#pragma pack()

