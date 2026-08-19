#ifndef TDS_IO_SERVER_PROTO_VISCA_H
#define TDS_IO_SERVER_PROTO_VISCA_H


#include "proto_common.h"
#pragma pack(1)


//namespace Visca_Pkt_Type {
//	unsigned char Command = 0x01;
//	unsigned char Inquiry = 0x09;
//}


struct VISCA_PKT : public DEV_PKT{
	unsigned char addr; //目标地址与源地址
	unsigned char pktType; //command or inquiry
	unsigned char id[4];  //设备id
	unsigned char rwFlag; //读写标记
	unsigned char paramLen; //参数长度
	unsigned char checkCode;
	unsigned short tail;

	VISCA_PKT() {
		addr = 0x81;
		tail = 0x5A5A;
		checkCode = 0xFF;
	}
};



#pragma pack()


#endif /* TDS_IO_SERVER_PROTO_VISCA_H */
