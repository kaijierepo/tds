#ifndef TDS_IO_SERVER_PROTO_CAN_H
#define TDS_IO_SERVER_PROTO_CAN_H


#include "proto/proto_common.h"
//Can总线协议
//连续bit转换成字节,高位在前  ":"符号为取位域
//取位域时,先取低位,再取高位
struct NET_CAN_ORG  //size = 5 bytes
{
	char DLC : 4;
	char CAN : 4;
	char ID29_25 : 5;  //ID高位在前.是这个字节中的低5位
	char Invalid : 3;  //29个bit,4个字节中有3个bit无效    
	char ID24_17;
	char ID16_9;
	char ID8_1;
};

/*
比特序(bit order)
字节序是一个对象中的多个字节之间的顺序问题，比特序就是一个字节中的8个比特位(bit)之间的顺序问题。一般情况下系统的比特序和字节序是保持一致的。
一个字节由8个bit组成，这8个bit也存在如何排序的情况，跟字节序类似的有最高有效比特位、最低有效比特位。
比特序1 0 0 1 0 0 1 0在大端系统中最高有效比特位为1、最低有效比特位为0，字节的值为0x92。在小端系统中最高、最低有效比特位则相反为0、1，字节的值为0x49。
跟字节序类似，要想保持一个字节值不变那么就要使系统能正确的识别最高、最低有效比特位。
*/
struct NET_CAN_HEAD_V2  //size = 5 bytes
{
	char DLC : 4;     //DLC
	char r0 : 2;   //bit4-5
	char RTR : 1;  //bit6
	char IDE : 1;  //bit7    =1表示扩展帧  =0表示标准帧
	char Address1 : 1;
	char B : 1;       //广播   0：普通帧  1：广播帧
	char G : 1;       //优先级  0：高级  1：低级
	char MS : 1;      //M/S   0:自主帧 1：应答帧
	char DIR : 1;     //DIR   0:下发  1:上送
	char R0 : 3;      //头部预留3位置
	char Type : 3;    //Type   100，4 自主单帧   000，0 应答单帧  011，3，非结束多帧  010，2， 结束多帧
	char Address : 5; //地址
	char InxFrame;//Index of frame  从0开始编号
	char SumFrame;//Sum of frame
	NET_CAN_HEAD_V2()
	{
		r0 = 0;
		RTR = 0;
		IDE = 1;
		DLC = 0;
		Address1 = 0;
		B = 0;
		G = 0;
		MS = 0;
		DIR = 0x0;
		R0 = 0;
		Type = 0;
		Address = 0;
		SumFrame = 0x0;
		InxFrame = 0x0;
	}

};

struct CAN_PKT_V2 //size = 13字节
{
	NET_CAN_HEAD_V2 sHead;
	char arrData[8];
	CAN_PKT_V2()
	{
		//memset(arrData,0, sizeof(arrData));
	}
};
#endif /* TDS_IO_SERVER_PROTO_CAN_H */
