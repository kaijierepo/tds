#ifndef TDS_IO_SERVER_PROTO_LOGIX_PLC_H
#define TDS_IO_SERVER_PROTO_LOGIX_PLC_H


#include "proto_common.h"

namespace LOGIX_TAG_VAL_TYPE {
	const unsigned short Bool = 0x00c1;  //1
	const unsigned short SInt = 0x00c2;  //1
	const unsigned short Int = 0x00c3;   //2
	const unsigned short DInt = 0x00c4;  //4
	const unsigned short Real = 0x00cA;  //4
	const unsigned short DWord = 0x00D3;  //4
	const unsigned short LInt = 0x00c5;   //8
}


#define READ_TAG_SERVICE_ERROR_INFO_0x04   "A syntax error was detected decoding the Request Path"
#define READ_TAG_SERVICE_ERROR_INFO_0x05   "Request Path destination unknown: Probably instance number is not present"


#pragma pack(1)




#pragma pack()


#endif /* TDS_IO_SERVER_PROTO_LOGIX_PLC_H */
