#pragma once
#include <string>
/*用于处理websocket中的所有编码相关,解析websocket格式
1.解析websocket连接请求中的sec，并生成对应的accept码，（通过sha1与base64编码）
2.解析来自客户端的websocket格式数据
3.封装数据成为websocket格式，用于发送到客户端*/

#include <stdint.h>


//PayLoad Len = 0-125       max pay load len = 125
//PayLoad Len = 126(0x7e)   max pay load len = 65,535
//PayLoad Len = 127(0x7f)   max pay load len = 1777777777,777,777,777,777


/* WebSocket framing    copy from RFC6455 5.2
  0 char          1 char          2 char          3 char
  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 
 +-+-+-+-+-------+-+-------------+-------------------------------+
 |F|R|R|R| opcode|M| Payload len | Extended payload length       |
 |I|S|S|S| (4)   |A|     (7)     |          (16/64)              |
 |N|V|V|V|       |S|             | (if payload len==             |
 | |1|2|3|       |K|             |        126(0x7e)/127(0x7f))   |
 +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 | Extended payload length continued, if payload len == 127      |
 + - - - - - - - - - - - - - - - +-------------------------------+
 |                               | Masking-key, if MASK set to 1 |
 +-------------------------------+-------------------------------+
 | Masking-key (continued)       | Payload Data                  |
 +-------------------------------- - - - - - - - - - - - - - - - +
 :                 Payload Data continued ...                    :
 + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
 |                 Payload Data continued ...                    |
 +---------------------------------------------------------------+

>>>>>>>> FIN: 1 bit
 Indicates that this is the final fragment in a message. The first
 fragment MAY also be the final fragment.

 >>>>>>>> RSV1, RSV2, RSV3: 1 bit each
 MUST be 0 unless an extension is negotiated that defines meanings
 for non-zero values. If a nonzero value is received and none of
 the negotiated extensions defines the meaning of such a nonzero
 value, the receiving endpoint MUST _Fail the WebSocket
 Connection_.

 >>>>>>>> Opcode: 4 bits
 Defines the interpretation of the "Payload data". If an unknown
 opcode is received, the receiving endpoint MUST _Fail the
 WebSocket Connection_. The following values are defined.
 * %x0 denotes a continuation frame
 * %x1 denotes a text frame
 * %x2 denotes a binary frame
 * %x3-7 are reserved for further non-control frames
 * %x8 denotes a connection close
 * %x9 denotes a ping
 * %xA denotes a pong
 * %xB-F are reserved for further control frames

 >>>>>>>> Mask: 1 bit
 Defines whether the "Payload data" is masked. If set to 1, a
 masking key is present in masking-key, and this is used to unmask
 the "Payload data" as per Section 5.3. All frames sent from
 client to server have this bit set to 1.
 Payload length: 7 bits, 7+16 bits, or 7+64 bits
 The length of the "Payload data", in bytes: if 0-125, that is the
 payload length. If 126, the following 2 bytes interpreted as a
 16-bit unsigned integer are the payload length. If 127, the
 following 8 bytes interpreted as a 64-bit unsigned integer (the
 most significant bit MUST be 0) are the payload length. Multibyte
 length quantities are expressed in network char order. Note that
 in all cases, the minimal number of bytes MUST be used to encode
 the length, for example, the length of a 124-char-long string
 can’t be encoded as the sequence 126, 0, 124. The payload length
 is the length of the "Extension data" + the length of the
 "Application data". The length of the "Extension data" may be
 zero, in which case the payload length is the length of the
 "Application data".

>>>>>>>> Masking-key: 0 or 4 bytes
 All frames sent from the client to the server are masked by a
 32-bit value that is contained within the frame. This field is
 present if the mask bit is set to 1 and is absent if the mask bit
 is set to 0. See Section 5.3 for further information on client-
 to-server masking.

>>>>>>>>> Payload data: (x+y) bytes
 The "Payload data" is defined as "Extension data" concatenated
 with "Application data".
 Extension data: x bytes
 The "Extension data" is 0 bytes unless an extension has been
 negotiated. Any extension MUST specify the length of the
 "Extension data", or how that length may be calculated, and how
 the extension use MUST be negotiated during the opening handshake.
 If present, the "Extension data" is included in the total payload
 length.

>>>>>>>>> Application data: y bytes
 Arbitrary "Application data", taking up the remainder of the frame
 after any "Extension data". The length of the "Application data"
 is equal to the payload length minus the length of the "Extension
 data".
 */
enum WS_Status
{
	WS_STATUS_CONNECT = 0,
	WS_STATUS_UNCONNECT = 1,
};

enum WS_FrameType
{
	WS_EMPTY_FRAME = 0xF0,
	WS_ERROR_FRAME = 0xF1,

	//frame with different optcode
	WS_CONTINUATION_FRAME = 0x00,
	WS_TEXT_FRAME = 0x01,
	WS_BINARY_FRAME = 0x02,

	WS_CONNECT_FRAME = 0X07,
	WS_CLOSING_FRAME = 0x08,
	WS_PING_FRAME = 0x09,
	WS_PONG_FRAME = 0x0A,
};


//一般调试情况下websocket包包头是6字节，2字节头+4字节maskingKey
class CWSPPkt : public PKT_DATA
{
public:
	CWSPPkt();
	~CWSPPkt();

	char* payloadData;
	int iPayloadLen;
	int iFrmLen;
	WS_FrameType frmType;

	WS_FrameType unpack(LPVOID pBuf, int iBufLen, bool bGetCmdInfo = false);
	int pack(const char* inMessage, size_t len, enum WS_FrameType frameType, bool bFin=1, bool bOpt=1);

	bool isDataFrame();
	static bool isHandShake(string& request);  ////判断是否为客户端发送的升级WS握手请求，"Upgrade: websocket"
	std::string getKey(std::string strKey);          //解码client的key（sha1 + base64），得到解码后的key
	std::string GetHandshakeString(std::string request);

	WS_FrameType GetFrameType(const char *frameData, int len); //获取消息类型

	int fetch_fin(char *msg, int &pos);
	int fetch_opcode(char *msg, int &pos);
	int fetch_mask(char *msg, int &pos);
	int fetch_masking_key(char *msg, int &pos);
	int fetch_payload_length(char *msg, int &pos);
	int fetch_payload(char *msg, int &pos);

	uint8_t fin_;
	uint8_t opcode_;
	uint8_t mask_;
	uint8_t masking_key_[4];
	UINT64 payload_length_;
	char payload_[2048];
};

extern int IsValidPkt_WEBSOCKET(unsigned char* pData, size_t iLen);