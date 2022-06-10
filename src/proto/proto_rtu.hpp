#pragma once
#include "tdscore.h"
#include "stream2pkt.h"

/*
Modbus RTU

| 设备地址 | 功能码  |  数据    |  crc   |
|  8bit    |  8bit   |  n*8bit  |  16bit |


功能码	说明
01	读取线圈状态
02	读取输入状态
03	读取保持寄存器
04	读取输入寄存器
05	强置单线圈
06	预置单寄存器
07	读取异常状态
08	回送诊断校验
09	编程（只用于484）
10	控询
11	读取事件计数
12	读取通信事件记录
13	编程（184 / 384 / 484 / 584等）
14	探寻
15	强置多线圈
16	预置多线圈
17	报告多寄存器
18	可使主机模拟编程功能
19	重置通信链路
20	读取通用参数
21	写入通用参数
22	屏蔽写寄存器
23	读 / 写多个寄存器
43	读设备别识码
22 - 42, 44 - 64	保留作为扩展功能
65 - 72	保留以备用功能所用
73 - 119	非法功能
120 - 127	保留，留作内部作用
128 - 255	保留，用于异常应答 



功能码	描述	         PLC地址位	    寄存器地址位	位/字操作	操作数量
01H	 读线圈寄存器	    00001-09999	    0000H-FFFFH	    位操作	 单个或多个
02H	读离散输入寄存器	10001-19999		0000H-FFFFH	    位操作	 单个或多个
03H	读保持寄存器	    40001-49999		0000H-FFFFH	    字操作	 单个或多个
04H	读输入寄存器	    30001-39999		0000H-FFFFH	    字操作	 单个或多个
05H	写单个线圈寄存器	00001-09999		0000H-FFFFH	     位操作	    单个
06H	写单个保持寄存器	40001-49999		0000H-FFFFH	     字操作	    单个
0FH	写多个线圈寄存器	00001-09999		0000H-FFFFH	     位操作	    多个
10H	写多个保持寄存器	40001-49999		0000H-FFFFH	     字操作	    多个

异常码说明

ExceptionCode： Function Code的最左边Bit设定为 1
举例:86 01,功能码06最左边Bit设定为1，即为86
ErrorCode参照下表
Modbus错误码（10进制）
功能码	说明
01	非法功能。对于服务器（或从站）来说，询问中接收到的功能码是不可允许的操作，可能是因为功能码仅适用于新设备而被选单元中不可实现同时，还指出服务器（或从站）在错误状态中处理这种请求，例如：它是未配置的，且要求返回寄存器值。
02	非法数据地址。对于服务器（或从站）来说，询问中接收的数据地址是不可允许的地址，特别是参考号和传输长度的组合是无效的。对于带有100个寄存器的控制器来说，偏移量96和长度4的请求会成功，而偏移量96和长度5的请求将产生异常码02。
03	非法数据值。对于服务器（或从站）来说，询问中包括的值是不可允许的值。该值指示了组合请求剩余结构中的故障。例如：隐含长度是不正确的。modbus协议不知道任何特殊寄存器的任何特殊值的重要意义，寄存器中被提交存储的数据项有一个应用程序期望之外的值。
04	从站设备故障。当服务器（或从站）正在设法执行请求的操作时，产生不可重新获得的差错。
05	确认。与编程命令一起使用，服务器（或从站）已经接受请求，并且正在处理这个请求，但是需要长持续时间进行这些操作，返回这个响应防止在客户机（或主站）中发生超时错误，客户机（或主机）可以继续发送轮询程序完成报文来确认是否完成处理。
06	从属设备忙。与编程命令一起使用。服务器(或从站)正在处理长持续时间的程序命令。张服务器(或从站)空闲时，用户(或主站)应该稍后重新传输报文。
08	存储奇偶差错。与功能码20和21以及参考类型6一起使用，指示扩展文件区不能通过一致性校验。服务器(或从站)设法读取记录文件，但是在存储器中发现一个奇偶校验错误。客户机(或主方)可以重新发送请求，但可以在服务器(或从站)设备上要求服务。
10	不可用网关路径。与网关一起使用，指示网关不能为处理请求分配输入端口至输出端口的内部通信路径。通常意味着网关是错误配置的或过载的。
11	网关目标设备响应失败。与网关一起使用，指示没有从目标设备中获得响应。通常意味着设备未在网络中。
*/

namespace MODBUS_EXP_CODE {
	const  char illegalFunction = 1;
	const  char illegalDataAddress= 2;
	const  char illegalDataValue = 3;
	const  char serverDeviceFailure = 4;
	const  char acknowledge = 5;
	const  char serverDeviceBusy = 6;
	const  char memoryParityError = 8;
	const  char gatewayPathUnavailable = 10;
	const  char gatewayTargetDeviceFailedToRespond = 11;
}

inline string getExpCodeDesc(char code)
{
	if (code == MODBUS_EXP_CODE::illegalFunction)
	{
		return "非法功能码";
	}
	else if (code == MODBUS_EXP_CODE::illegalDataAddress)
	{
		return "非法数据地址";
	}
	else if (code == MODBUS_EXP_CODE::illegalDataValue)
	{
		return "非法数据值";
	}
	else if (code == MODBUS_EXP_CODE::serverDeviceFailure)
	{
		return "从站设备故障";
	}
	else if (code == MODBUS_EXP_CODE::acknowledge)
	{
		return "确认";
	}
	else if (code == MODBUS_EXP_CODE::serverDeviceBusy)
	{
		return "从属设备忙";
	}
	else if (code == MODBUS_EXP_CODE::memoryParityError)
	{
		return "存储奇偶差错";
	}
	else if (code == MODBUS_EXP_CODE::gatewayPathUnavailable)
	{
		return "不可用网关路径";
	}
	else if (code == MODBUS_EXP_CODE::gatewayTargetDeviceFailedToRespond)
	{
		return "网关目标设备响应失败";
	}
	
	return "未知错误";
}


namespace MODBUS_REG_TYPE {
    const string discreteInput = "Discrete-Input";
    const string coil = "Coil";
    const string inputRegister = "Input-Register";
    const string holdingRegister = "Holding-Register";
}

namespace MODBUS_FUNCTION_CODE {
    const unsigned char readCoils = 1;
    const unsigned char readDiscreteInputs = 2; 
    const unsigned char readHoldingRegisters = 3;
    const unsigned char readInputRegisters = 4;
	const unsigned char writeSingleCoil = 5;
	const unsigned char writeSingleRegister = 6;
	const unsigned char writeMultipleRegister = 0x10;
};

#pragma pack(1)
//modbus包框架
class MB_RTU_PKT : public PKT_DATA {
public:
	unsigned char eqp_addr;
	unsigned char fun_code;
	char crc_H;
	char crc_L;

	MB_RTU_PKT() {

	}

	~MB_RTU_PKT() {

	}

	void copy(const MB_RTU_PKT& r) {
		PKT_DATA::copy(r);
		eqp_addr = r.eqp_addr;
		fun_code = r.fun_code;
		crc_H = r.crc_H;
		crc_L = r.crc_L;
	}

	MB_RTU_PKT(const MB_RTU_PKT& r)
	{
		copy(r);
	}

	MB_RTU_PKT& operator=(const MB_RTU_PKT& pd) {
		copy(pd);
		return *this;
	}

	bool unpack() override {
		eqp_addr = (unsigned char)data[0];
		fun_code = (unsigned char)data[1];
		cmd_data_len = len - 4;
		if (cmd_data_len > 0)
		{
			cmd_data = new char[cmd_data_len];
			memcpy(cmd_data, data + 2, cmd_data_len);
		}
		return true;
	};
	bool unpack(char* p, int len) override {
		setData(p, len);
		return unpack();
	};
	bool pack() override {
		len = 4 + cmd_data_len;
		data = new char[len];
		data[0] = eqp_addr;
		data[1] = fun_code;
		memcpy(data + 2, cmd_data, cmd_data_len);
		WORD crc = common::N_CRC16((unsigned char*)data, len -2 );
		crc_H = HIBYTE(crc);
		crc_L = LOBYTE(crc);
		data[2 + cmd_data_len] = crc_H;
		data[2 + cmd_data_len + 1] = crc_L;
		return true;
	}
};
//命令数据区结构
struct RTU_REQ_read {
	unsigned char start_reg_addr_H;
	unsigned char start_reg_addr_L;
	unsigned char reg_num_H;
	unsigned char reg_num_L;
	void setRegNum(unsigned short n) {
		reg_num_H = HIBYTE(n);
		reg_num_L = LOBYTE(n);
	}
	void setStartReg(unsigned short s) {
		start_reg_addr_H = HIBYTE(s);
		start_reg_addr_L = LOBYTE(s);
	}
	int getRegNum() {
		unsigned short num = reg_num_H * 256 + reg_num_L;
		return num;
	}
	int getStartReg() {
		int start = start_reg_addr_H * 256 + start_reg_addr_L;
		return start;
	}
};
struct RTU_RESP_read{
	unsigned char byte_count;
	char reg_data[255];
};

struct RTU_REQ_writeSingleCoil{
	unsigned char addr_H;
	unsigned char addr_L;
	unsigned char val_H;
	unsigned char val_L;
	void setOffset(unsigned short a) {
		addr_H = HIBYTE(a);
		addr_L = LOBYTE(a);
	}
	void setVal(bool v) {
		if (v)
		{
			val_H = 0xFF;
			val_L = 0;
		}
		else
		{
			val_H = 0;
			val_L = 0;
		}
	}


	int getOffset() {
		int offset = addr_H * 256 + addr_L;
		return offset;
	}
	int getVal() {
		if (val_H == 0xFF)
			return 1;
		else
			return 0;
	}
};

struct RTU_REQ_writeSingleReg {
	unsigned char addr_H;
	unsigned char addr_L;
	unsigned char val_H;
	unsigned char val_L;
	void setOffset(unsigned short a) {
		addr_H = HIBYTE(a);
		addr_L = LOBYTE(a);
	}
	void setVal(short v) {
		val_H = HIBYTE(v);
		val_L = LOBYTE(v);
	}
	void setVal(unsigned short v) {
		val_H = HIBYTE(v);
		val_L = LOBYTE(v);
	}
	int getOffset() {
		int offset = addr_H * 256 + addr_L;
		return offset;
	}
	int getVal() {
		if (val_H == 0xFF)
			return 1;
		else
			return 0;
	}
};

struct RTU_REQ_writeMultiReg {
	unsigned char start_reg_addr_H;
	unsigned char start_reg_addr_L;
	unsigned char reg_num_H;
	unsigned char reg_num_L;
	unsigned char byte_count;
	char reg_data[255];
	void setRegNum(unsigned short n) {
		reg_num_H = HIBYTE(n);
		reg_num_L = LOBYTE(n);
	}
	void setStartReg(unsigned short s) {
		start_reg_addr_H = HIBYTE(s);
		start_reg_addr_L = LOBYTE(s);
	}
	int getRegNum() {
		unsigned short num = reg_num_H * 256 + reg_num_L;
		return num;
	}
	int getStartReg() {
		int start = start_reg_addr_H * 256 + start_reg_addr_L;
		return start;
	}
};
#pragma pack()

