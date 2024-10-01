#include "JHDEqpProtocol.h"


//默认为缺口类型
UINT JHDEqpPkt::g_productid = PRODUCT_TYPE_GAP;

JHDEqpPkt::JHDEqpPkt()
{
	len = 0;
	m_iCmdBufLen = 0;
	head = 0x68;
	tail = 0x97;
	m_strUnpackErrorInfo = "";
	m_strCmdContent = "";
}

JHDEqpPkt::~JHDEqpPkt()
{

}

string JHDEqpPkt::PktDataToString()
{
	return str::bytesToHexStr((char*)data, len);

}

void JHDEqpPkt::ClearData()
{
	memset(&head, 0, sizeof(JHDEqpPktData));
}

BOOL JHDEqpPkt::unpack(JHDEqpPkt& data, LPVOID buf, int len)
{
	return data.unpack((unsigned char*)buf, len);
}

//BOOL JHDEqpPkt::unpack()
//{
//	return unpack(data, len);
//}
//
//BOOL JHDEqpPkt::unpack(CByteArray& aryData)
//{
//	return unpack(aryData.GetData(), aryData.GetSize());
//}

//解析数据到包的时候进行了弱校验
bool JHDEqpPkt::unpack(unsigned char* pBuf, int iBufLen, bool bGetCmdInfo)
{
	ZeroMemory(&head, sizeof(JHDEqpPktData));
	BYTE* pOld = data;
	//原始数据
	len = iBufLen;
	data = new BYTE[iBufLen];
	memcpy(data, pBuf, len);
	m_CmdBuf = data + 1 + 4 + 8;

	LPBYTE pos = (LPBYTE)pBuf;
	head = *pos;
	pos += 1;

	DWORD dw = *(DWORD*)(pos);
	ctrlhead.dir = dw & 0x01;
	ctrlhead.ceid = (dw >> 1) & 0x03FF;
	ctrlhead.cmdid = (dw >> 11) & 0x1F;
	ctrlhead.flag = (dw >> 16) & 0x0F;
	ctrlhead.dlen = ((dw >> 20) & 0x0FFF);
	pos += 4;

	if (head != FRAME_HEADER)
	{
		if (pOld)
			delete pOld;

		return false;
	}

	if (ctrlhead.dlen > MAX_DATA_SIZE
		|| (UINT)iBufLen < ctrlhead.dlen + 1 + 4 + 1 + 1)
	{
		if (pOld)
			delete pOld;

		return false;
	}

	if (ctrlhead.cmdid == GSZB2016Ext_Protocol_Cmd || ctrlhead.cmdid == GSZB2016_GONGWU_Protocol_Cmd)
	{
		cmdData.ep2016Data.protover = *pos;
		pos += 1;

		cmdData.ep2016Data.productid = *pos;
		pos += 1;

		cmdData.ep2016Data.cmdid2 = *(WORD*)pos;
		pos += 2;

		cmdData.ep2016Data.srcid = *(WORD*)pos;
		pos += 2;

		cmdData.ep2016Data.dstid = *(WORD*)pos;
		pos += 2;

		cmdData.ep2016Data.lpdata = data + 1 + 4 + 8;;
		pos += ctrlhead.dlen - 8;
	}
	else if (ctrlhead.cmdid == D520Ext_Protocol_Cmd)
	{
		cmdData.d520ExtData.cmd2 = *pos;
		pos += 1;

		cmdData.d520ExtData.lpdata = data + 1 + 4 + 1;
		pos += ctrlhead.dlen - 1;
	}
	else if (ctrlhead.cmdid == UpgradeExt_Protocol_Cmd)
	{
		cmdData.upgradeExtData.cmd2 = *pos;
		pos += 1;

		cmdData.upgradeExtData.lpdata = data + 1 + 4 + 1;
		pos += ctrlhead.dlen - 1;
	}
	else
	{
		cmdData.data = data + 1 + 4;
		pos += ctrlhead.dlen;
	}

	check = *pos;
	pos += 1;

	tail = *pos;
	pos += 1;

	pos = ((LPBYTE)pBuf);
	BYTE check = 0;
	for (int i = 0; (UINT)i < 1 + 4 + ctrlhead.dlen; ++i)
		check += pos[i];
	if (check != check || tail != FRAME_TAIL)
	{
		if (pOld)
			delete pOld;

		return false;
	}

	if (bGetCmdInfo)
	{
		m_iCmdBufLen = ctrlhead.dlen - 8;
		m_strCmdName = GetCmdName();
	}

	if (pOld)
		delete pOld;

	return true;
}

bool JHDEqpPkt::pack(BYTE* buf, int iLen)
{
	JHDEqpPkt& data = *this;
	iLen = data.ctrlhead.dlen + 1 + 4 + 1 + 1;
	buf = new BYTE[iLen];


	LPBYTE pos = (LPBYTE)buf;

	*pos = this->head;
	pos += 1;

	DWORD dw = (data.ctrlhead.dir & 0x01)
		| ((data.ctrlhead.ceid & 0x03FF) << 1)
		| ((data.ctrlhead.cmdid & 0x1F) << 11)
		| ((data.ctrlhead.flag & 0x0F) << 16)
		| (((data.ctrlhead.dlen) & 0x0FFF) << 20);
	*(DWORD*)pos = dw;
	pos += 4;


	if (data.ctrlhead.cmdid == GSZB2016Ext_Protocol_Cmd
		|| data.ctrlhead.cmdid == GSZB2016_GONGWU_Protocol_Cmd)
	{
		*pos = data.cmdData.ep2016Data.protover;
		pos += 1;

		*pos = data.cmdData.ep2016Data.productid;
		pos += 1;

		*(WORD*)pos = data.cmdData.ep2016Data.cmdid2;
		pos += 2;

		*(WORD*)pos = data.cmdData.ep2016Data.srcid;
		pos += 2;

		*(WORD*)pos = data.cmdData.ep2016Data.dstid;
		pos += 2;


		if (data.ctrlhead.dlen < 8)
		{
			return false;
		}


		if (data.ctrlhead.dlen && data.cmdData.ep2016Data.lpdata)
		{
			memcpy_s(pos, data.ctrlhead.dlen - 8, data.cmdData.ep2016Data.lpdata, data.ctrlhead.dlen - 8); //二级命令的命令数据区
			pos += data.ctrlhead.dlen - 8;
		}
	}
	else if (data.ctrlhead.cmdid == D520Ext_Protocol_Cmd)
	{
		*pos = data.cmdData.d520ExtData.cmd2;
		pos += 1;

		memcpy_s(pos, data.ctrlhead.dlen - 1, data.cmdData.d520ExtData.lpdata, data.ctrlhead.dlen - 1);
		pos += data.ctrlhead.dlen - 1;
	}
	else if (data.ctrlhead.cmdid == UpgradeExt_Protocol_Cmd)
	{
		*pos = data.cmdData.upgradeExtData.cmd2;
		pos += 1;

		memcpy_s(pos, data.ctrlhead.dlen - 1, data.cmdData.upgradeExtData.lpdata, data.ctrlhead.dlen - 1);
		pos += data.ctrlhead.dlen - 1;
	}
	else
	{
		if (data.ctrlhead.dlen)
		{
			memcpy_s(pos, data.ctrlhead.dlen, data.cmdData.data, data.ctrlhead.dlen); //二级命令的命令数据区
			pos += data.ctrlhead.dlen;
		}
	}

	BYTE check = 0;
	pos = (LPBYTE)buf;
	for (int i = 0; (UINT)i < 1 + 4 + data.ctrlhead.dlen; ++i)
		check += pos[i];

	pos = (LPBYTE)buf + 1 + 4 + data.ctrlhead.dlen;
	*pos = check;
	pos += 1;

	*pos = data.tail;
	pos += 1;

	return true;
}

bool JHDEqpPkt::pack()
{
	return pack((BYTE*)data, len);
}

//BOOL JHDEqpPkt::pack(CByteArray& aryData)
//{
//	pack(data, len);
//	if (len > 0)
//	{
//		aryData.SetSize(len);
//		memcpy(aryData.GetData(), data, len);
//		return TRUE;
//	}
//
//	return FALSE;
//}

BOOL JHDEqpPkt::Release(JHDEqpPkt& data, int dir)
{
	if (data.cmdData.ep2016Data.lpdata)
		delete data.cmdData.ep2016Data.lpdata;
	return TRUE;
}


string JHDEqpPkt::GetPktDesc()
{
	JHDEqpPkt& pkt = *this;
	int iLen = pkt.len;
	//生成解析信息
	string str;// = ("[解析数据  ] ") + strFrame;
	//string temp;

	//temp.Format(("[总包长     ] %d"), iLen);
	//str = str + ("\r\n") + temp;


	//temp.Format(("0x%02X"), pkt.head);
	//str = str + ("\r\n") + ("[包头          ] ") + temp;
	//if (pkt.ctrlhead.dir == 0)temp = ("下行"); if (pkt.ctrlhead.dir == 1)temp = ("上行");
	//str = str + ("\r\n") + ("[数据方向  ] ") + temp;
	//temp.Format(("%d"), pkt.ctrlhead.ceid);
	//str = str + ("\r\n") + ("[设备ID      ] ") + temp;
	//temp.Format(("0x%02X"), pkt.ctrlhead.cmdid);
	//str = str + ("\r\n") + ("[命令ID      ] (") + temp + (") (") + pkt.GetCmdName() + (")");
	//temp.Format(("0x%02X"), pkt.ctrlhead.flag);
	//str = str + ("\r\n") + ("[FLAG         ] ") + temp;
	//temp.Format(("%d"), pkt.ctrlhead.dlen);
	//str = str + ("\r\n") + ("[数据长度  ] ") + temp;

	//if (pkt.ctrlhead.cmdid == GSZB2016Ext_Protocol_Cmd)
	//{
	//	str = str + ("\r\n") + ("[协议版本   ] EVP2");

	//	temp.Format(("%d"), pkt.cmdData.ep2016Data.protover);
	//	str = str + ("\r\n") + ("[子协议版本 ] ") + temp;
	//	temp.Format(("0x%02X"), pkt.cmdData.ep2016Data.productid);
	//	str = str + ("\r\n") + ("[产品类型  ] ") + temp;
	//	temp.Format(("0x%04X"), pkt.cmdData.ep2016Data.cmdid2);
	//	str = str + ("\r\n") + ("[二级命令  ] (") + temp + ") (" + pkt.GetCmdName() + (")");
	//	temp.Format(("0x%02X%02X ,   %d.%d ,  %d"), HIBYTE(pkt.cmdData.ep2016Data.srcid), LOBYTE(pkt.cmdData.ep2016Data.srcid), HIBYTE(pkt.cmdData.ep2016Data.srcid), LOBYTE(pkt.cmdData.ep2016Data.srcid), pkt.cmdData.ep2016Data.srcid);
	//	str = str + ("\r\n") + ("[源ID          ] ") + temp;
	//	temp.Format(("0x%02X%02X ,   %d.%d ,  %d"), HIBYTE(pkt.cmdData.ep2016Data.dstid), LOBYTE(pkt.cmdData.ep2016Data.dstid), HIBYTE(pkt.cmdData.ep2016Data.dstid), LOBYTE(pkt.cmdData.ep2016Data.dstid), pkt.cmdData.ep2016Data.dstid);
	//	str = str + ("\r\n") + ("[目标ID      ] ") + temp;
	//	string strData;
	//	if (pkt.cmdData.ep2016Data.lpdata)
	//		CGlobalFunction::ByteArrayToSpaceString((char*)pkt.cmdData.ep2016Data.lpdata, pkt.ctrlhead.dlen - 8, strData);
	//	string str1;
	//	str1.Format(("%d"), pkt.ctrlhead.dlen - 8);
	//	str = str + ("\r\n") + ("[命令数据  ][") + str1 + ("]") + strData;
	//	str = str + ("\r\n") + ("[命令概要  ]\r\n");
	//	str = str + pkt.m_strCmdContent;
	//}
	//else if (pkt.ctrlhead.cmdid == D520Ext_Protocol_Cmd)
	//{
	//	str = str + ("\r\n") + ("[协议版本   ] EVP1");

	//	temp.Format(("0x%02X"), pkt.cmdData.d520ExtData.cmd2);
	//	str = str + ("\r\n") + ("[二级命令码 ] (") + temp + ") (" + pkt.GetCmdName() + (")");

	//	string strData;
	//	if (pkt.cmdData.d520ExtData.lpdata)
	//		CGlobalFunction::ByteArrayToSpaceString((char*)pkt.cmdData.d520ExtData.lpdata, pkt.ctrlhead.dlen - 1, strData);
	//	string str1;
	//	str1.Format(("%d"), pkt.ctrlhead.dlen);
	//	str = str + ("\r\n") + ("[命令数据  ][") + str1 + ("]") + strData;
	//	str = str + ("\r\n") + ("[命令概要  ]\r\n");
	//	str = str + pkt.m_strCmdContent;
	//}
	//else if (pkt.ctrlhead.cmdid == UpgradeExt_Protocol_Cmd)
	//{
	//	str = str + ("\r\n") + ("[协议版本   ] EVP0 低速载波模块升级子协议");

	//	temp.Format(("0x%02X"), pkt.cmdData.upgradeExtData.cmd2);
	//	str = str + ("\r\n") + ("[二级命令码 ] (") + temp + ") (" + pkt.GetCmdName() + (")");

	//	string strData;
	//	if (pkt.cmdData.upgradeExtData.lpdata)
	//		CGlobalFunction::ByteArrayToSpaceString((char*)pkt.cmdData.upgradeExtData.lpdata, pkt.ctrlhead.dlen - 1, strData);
	//	string str1;
	//	str1.Format(("%d"), pkt.ctrlhead.dlen);
	//	str = str + ("\r\n") + ("[命令数据  ][") + str1 + ("]") + strData;
	//	str = str + ("\r\n") + ("[命令概要  ]\r\n");
	//	str = str + pkt.m_strCmdContent;
	//}
	//else
	//{
	//	str = str + ("\r\n") + ("[协议版本   ] EVP0");

	//	string strData;
	//	if (pkt.data.data)
	//		CGlobalFunction::ByteArrayToSpaceString((char*)pkt.data.data, pkt.ctrlhead.dlen, strData);
	//	string str1;
	//	str1.Format(("%d"), pkt.ctrlhead.dlen);
	//	str = str + ("\r\n") + ("[命令数据  ][") + str1 + ("]") + strData;
	//	str = str + ("\r\n") + ("[命令概要  ]\r\n");
	//	str = str + pkt.m_strCmdContent;
	//}

	//temp.Format(("0x%02X"), pkt.check);
	//str = str + ("\r\n") + ("[校验位      ] ") + temp;
	//temp.Format(("0x%02X"), pkt.tail);
	//str = str + ("\r\n") + ("[包尾          ] ") + temp;

	return str;
}

BOOL JHDEqpPkt::IsValidPkt(BYTE* pData, int iLen, int& iPktLen)
{
	if (pData[0] != 0x68)
		return FALSE;

	if (iLen < 5)
		return FALSE;

	StCtrlHead* pHead = (StCtrlHead*)(pData + 1);

	iPktLen = 1/*头*/ + 4/*控制头*/ + pHead->dlen + 1/*校验*/ + 1/*尾*/;

	if (iLen < iPktLen)
		return FALSE;

	if (pData[iPktLen - 1] != 0x97)
		return FALSE;

	return TRUE;
}

size_t JHDEqpPkt::IsValidPkt_JEP(unsigned char* pData, size_t iLen)
{
	if (pData[0] != 0x68)
		return 0;

	if (iLen < 5)
		return 0;

	StCtrlHead* pHead = (StCtrlHead*)(pData + 1);

	int iPktLen = 1/*头*/ + 4/*控制头*/ + pHead->dlen + 1/*校验*/ + 1/*尾*/;

	if (iLen < iPktLen)
		return 0;

	if (pData[iPktLen - 1] != 0x97)
		return 0;

	return iPktLen;
}

//buf监测内存指针
//size内存总大小
//startCheckPos 从哪个位置开始检测有效帧
//framePos 当前找到的帧头位置
//len找到的帧长度
//continueCheck是否需要继续查找有效帧，某个帧头后面的数据不符合帧格式，可能为无效数据，跳过去下一个帧头后面可能有有效帧; 当某次检测到帧头后无有效帧，但帧头后面的数据大于最小包长时，都应该continueCheck
//返回值，TRUE提取一帧 ，FALSE未提取一帧，此时framePos的帧头位置可能有效
BOOL JHDEqpPkt::GetFrameData(LPBYTE buf, int size, int startCheckPos, int& framePos, int& len, BOOL& continueCheck)
{
	continueCheck = TRUE;
	//检测包长是否满足要求
	int nMinLen = 1 + 4 + 1 + 1;
	len = 0;
	if (buf == NULL || size < nMinLen)
	{
		framePos = -1;  //长度不够，无需检测后续头
		continueCheck = FALSE;
		return FALSE;
	}


	//从指定位置开始检测包头
	framePos = startCheckPos; //监测到的帧的开始位置 headPos表示从那个位置开始检测
	LPBYTE pos = (LPBYTE)buf;
	pos = pos + startCheckPos;
	BOOL haveHead = FALSE;
	int cnt = size - startCheckPos;  //实际待监测的包数据，从本次开始位置开始计算
	while (cnt > 0)
	{
		if (*pos == FRAME_HEADER)
		{
			haveHead = TRUE;
			break;
		}
		pos++;
		framePos++;
		cnt--;
	}

	//未找到包头，返回失败
	if (haveHead == FALSE)
	{
		framePos = -1;
		continueCheck = FALSE;
		return FALSE; //没找到头部
	}


	//监测包头后数据长度是否有效
	if (cnt < nMinLen)
	{
		continueCheck = FALSE;
		return FALSE;
	}


	StCtrlHead ctrlhead;
	memcpy_s(&ctrlhead, sizeof(StCtrlHead), pos + 1, sizeof(StCtrlHead));

	DWORD dw = *(DWORD*)(pos + 1);
	ctrlhead.dir = dw & 0x01;
	ctrlhead.ceid = (dw >> 1) & 0x03FF;
	ctrlhead.cmdid = (dw >> 11) & 0x1F;
	ctrlhead.flag = (dw >> 16) & 0x0F;
	ctrlhead.dlen = (dw >> 20) & 0x0FFF;

	int datalen = ctrlhead.dlen;
	if (datalen > MAX_DATA_SIZE)
	{
		return FALSE;
	}
	if (cnt < nMinLen + datalen)
		return FALSE;

	BYTE tail = *(pos + 1 + 4 + 1 + datalen);
	if (tail != FRAME_TAIL)
		return FALSE;

	buf = pos;
	len = nMinLen + datalen;

	return TRUE;
}

DWORD JHDEqpPkt::GetCmdCode(const JHDEqpPkt& data)
{
	return data.cmdData.ep2016Data.cmdid2;
}

BOOL JHDEqpPkt::InitFrame(JHDEqpPkt& data)
{
	data.head = 0x68;
	data.cmdData.ep2016Data.protover = 0x01;
	data.cmdData.ep2016Data.productid = 0x31;
	data.tail = 0x97;

	data.ctrlhead.ceid = 0x3FC;
	data.ctrlhead.cmdid = 0x1B;
	data.ctrlhead.flag = 0;

	return TRUE;
}

BOOL JHDEqpPkt::ExtractOnePacket(BYTE* pData, DWORD& dwLen)
{
	int iLen = dwLen;
	BOOL bRet = ExtractOnePacket(pData, iLen);
	dwLen = iLen;
	return bRet;
}

BOOL JHDEqpPkt::ExtractOnePacket(BYTE* pData, int& iLen, BOOL bDelFromSrc)
{
	if (iLen == 0)
	{
		return FALSE;
	}

	int iFramePos = 0;
	int iDataLen = 0;
	BYTE* pBuf = pData;
	int iBufSize = iLen;

	int startCheckPos = 0; //从0位置开始检测有效帧
	while (1)
	{
		BOOL continueCheck = FALSE;
		BOOL ret = GetFrameData(pBuf, iBufSize, startCheckPos, iFramePos, iDataLen, continueCheck);

		//if (iFramePos > 0 && ret)
		//	ASSERT(FALSE);


		if (ret)
		{
			pBuf = pData;
			iBufSize = iLen;
			memcpy(data, pBuf + iFramePos, iDataLen); //提取包数据
			len = iDataLen;

			int iLeftSize = iBufSize - iFramePos - iDataLen; //提取包后剩余的数据长度
			if (iLeftSize < 0)
			{
				//ASSERT(FALSE);
				return FALSE; //2016采集发送心跳包后会进入此处，原因暂不明，先做保护 
			}

			if (bDelFromSrc)
			{
				memcpy(pBuf, pBuf + iFramePos + iDataLen, iLeftSize);
				iLen = iLeftSize;
			}

			return TRUE;
		}
		else
		{
			//检测到了帧头，但是该帧头后续没有有效帧。  尝试往下找下1个帧头
			if (continueCheck && iFramePos != -1)
			{
				startCheckPos = iFramePos + 1;
				continue;
			}
			else
			{
				return FALSE;
			}
		}

	}

	return FALSE;
}


//BOOL JHDEqpPkt::ExtractOnePacket(CByteArray& aryData)
//{
//	if (aryData.GetCount() == 0)
//	{
//		return FALSE;
//	}
//
//	int iFramePos = 0;
//	int iDataLen = 0;
//	BYTE* pBuf = aryData.GetData();
//	int iBufSize = aryData.GetSize();
//
//	int startCheckPos = 0; //从0位置开始检测有效帧
//	while (1)
//	{
//		BOOL continueCheck = FALSE;
//		BOOL ret = GetFrameData(pBuf, iBufSize, startCheckPos, iFramePos, iDataLen, continueCheck);
//
//		if (ret)
//		{
//			pBuf = aryData.GetData();
//			iBufSize = aryData.GetSize();
//
//			data = new BYTE[iDataLen];
//			memcpy(data, pBuf + iFramePos, iDataLen); //提取包数据
//			len = iDataLen;
//
//
//			int iLeftSize = iBufSize - iFramePos - iDataLen; //提取包后剩余的数据长度
//			memcpy(pBuf, pBuf + iFramePos + iDataLen, iLeftSize);
//			aryData.SetSize(iLeftSize);
//			return TRUE;
//		}
//		else
//		{
//			//检测到了帧头，但是该帧头后续没有有效帧。  尝试往下找下1个帧头
//			if (continueCheck && iFramePos != -1)
//			{
//				startCheckPos = iFramePos + 1;
//				continue;
//			}
//			else
//			{
//				return FALSE;
//			}
//		}
//
//	}
//
//	return FALSE;
//}

//BOOL JHDEqpPkt::Pack_V2_GETBUSADDRESS(int iDesID)
//{
//	head = 0x68;
//	tail = 0x97;
//	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
//	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
//	ctrlhead.ceid = iDesID;
//	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
//	cmdData.ep2016Data.protover = 0x01;
//	cmdData.ep2016Data.productid = PRODUCT_TYPE_COMMON;
//	cmdData.ep2016Data.srcid = 0xFFFF;
//	cmdData.ep2016Data.dstid = iDesID;
//	cmdData.ep2016Data.cmdid2 = COMM_CMD_GET_ID;
//
//	cmdData.ep2016Data.lpdata = (void*)&m_stModuleBusAddress;
//	ctrlhead.dlen = 8 + sizeof(StModuleID);
//
//	return pack((BYTE*)data, len);
//}

//BOOL JHDEqpPkt::Pack_V2_GETDEVINFO(int iDesID)
//{
//	head = 0x68;
//	tail = 0x97;
//	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
//	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
//	ctrlhead.ceid = iDesID;
//	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
//	cmdData.ep2016Data.protover = 0x01;
//	cmdData.ep2016Data.productid = PRODUCT_TYPE_COMMON;
//	cmdData.ep2016Data.srcid = 0xFFFF;
//	cmdData.ep2016Data.dstid = iDesID;
//	cmdData.ep2016Data.cmdid2 = COMM_CMD_GET_DEVINFO;
//
//	cmdData.ep2016Data.lpdata = (void*)&m_stModuleInfoParam;
//	ctrlhead.dlen = 8 + sizeof(StModuleInfo);
//
//	return pack(data, len);
//}


void JHDEqpPkt::Pack_V2_HEARTBEAT(int iDesID, int srcId, bool bEnableAcq)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_COMMON;
	cmdData.ep2016Data.srcid = srcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = COMM_CMD_HEARTBEAT;

	StHeartBeat cmddata;
	memset(&cmddata, 0, sizeof(StHeartBeat));
	TIME ctNow = timeopt::now();
	//cmddata.clock.Second = ctNow.wSecond();
	//cmddata.clock.Minute = ctNow.wMinute();
	//cmddata.clock.Hour = ctNow.wHour();
	//cmddata.clock.Day = ctNow.wDay();
	//cmddata.clock.Month = ctNow.wMonth();
	//cmddata.clock.Year = ctNow.wYear();
	cmddata.clock = ctNow;
	cmddata.bEnableAcq = bEnableAcq ? 1 : 0;

	cmdData.ep2016Data.lpdata = &cmddata;

	ctrlhead.dlen = 8 + sizeof(cmddata);

	pack((BYTE*)data, len);
}

void JHDEqpPkt::Pack_V2_RestartDev_AD0C(int iDesID)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_COMMON;
	cmdData.ep2016Data.srcid = 0xFFFF;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = COMM_CMD_RESTART;

	BYTE reserve[8];
	memset(reserve, 0, sizeof(reserve));
	cmdData.ep2016Data.lpdata = reserve;

	ctrlhead.dlen = 8 + 8;
	pack(data, len);
}

BOOL JHDEqpPkt::DFT_PicSize(int iPixelType, int& w, int& h)
{
	switch (iPixelType)
	{
	case 1:
		w = 1920;
		h = 1080;
		return TRUE;
	case 2:
		w = 704;
		h = 576;
		return TRUE;
	case 3:
		w = 352;
		h = 288;
		return TRUE;
	case 4:
		w = 480;
		h = 480;
		return TRUE;
	case 5:
		w = 240;
		h = 240;
		return TRUE;
	case 6:
		w = 640;
		h = 480;
		return TRUE;
	case 7:
		w = 320;
		h = 240;
		return TRUE;
	case 8:
		w = 1280;
		h = 720;
		return TRUE;
	case 9:
		w = 1080;
		h = 1920;
		return TRUE;
	case 10:
		w = 576;
		h = 704;
		return TRUE;
	case 11:
		w = 288;
		h = 352;
		return TRUE;
	case 12:
		w = 480;
		h = 640;
		return TRUE;
	case 13:
		w = 240;
		h = 320;
		return TRUE;
	case 14:
		w = 720;
		h = 1280;
		return TRUE;
	}

	return FALSE;
}


void JHDEqpPkt::Pack_V2_StressTest_AD90(int iDesID)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_COMMON;
	cmdData.ep2016Data.srcid = 0xFFFF;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = CMD_STRESS_TEST;

	int iLength = 50 + 9;
	BYTE* bSend = new BYTE[iLength];
	memset(bSend, 0, iLength);
	*(bSend + 5) = iLength;
	memcpy(bSend + 9, ("JHD is doing stresstest."), iLength - 9);

	cmdData.ep2016Data.lpdata = bSend;

	ctrlhead.dlen = 8 + iLength;
	pack(data, len);

	//--组包
	//string strPktData;
	//CGlobalFunction::ByteArrayToSpaceString(data,len,strPktData);
}


void JHDEqpPkt::Pack_V2_GetCommonRealAD_Req(int iDesID)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_COMMON;
	cmdData.ep2016Data.srcid = 0xFFFF;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = COMM_CMD_GET_AI;

	BYTE reserve[16];
	memset(reserve, 0, 16);
	cmdData.ep2016Data.lpdata = reserve;

	ctrlhead.dlen = 8 + 16;
	pack(data, len);
}


BOOL JHDEqpPkt::Pack_V0Upgrade_SendBinData(int iDesID, REQ_UPGRADE_FILE_DATA cmd)
{
	BYTE buf[1000] = { 0 };
	int iPos = 0;
	WORD* pktId = (WORD*)buf;
	*pktId = cmd.wPktID;
	iPos += 2;
	DWORD* dataAddr = (DWORD*)(buf + iPos);
	*dataAddr = cmd.dwDataAddr;
	iPos += 4;
	WORD* dataLen = (WORD*)(buf + iPos);
	*dataLen = cmd.wDataLen;
	iPos += 2;
	memcpy(buf + iPos, cmd.pData, cmd.wDataLen);
	iPos += cmd.wDataLen;
	WORD* crc = (WORD*)(buf + iPos);
	*crc = cmd.wCRC;
	iPos += 2;
	iPos += 4;

	return Pack_V0Upgrade(iDesID, UPGRADE_DOWNLOAD_FILE, buf, iPos);
}


BOOL JHDEqpPkt::Pack_V0Upgrade(int iDesID, int cmd2, BYTE* pCmd, int iCmdLen)
{
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = UpgradeExt_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	cmdData.upgradeExtData.cmd2 = cmd2;
	cmdData.upgradeExtData.lpdata = pCmd;
	ctrlhead.dlen = 1 + iCmdLen;
	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V0Upgrade_CheckState(int iDesID, REQ_STATE_CHECK cmd)
{
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = UpgradeExt_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	cmdData.upgradeExtData.cmd2 = UPGRADE_DOWNLOAD_FILE;
	cmdData.upgradeExtData.lpdata = (BYTE*)&cmd;
	cmd.wPktID = 0xffff;
	ctrlhead.dlen = 1 + sizeof(cmd);
	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V0Upgrade_Start(int iDesID)
{
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = UpgradeExt_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	cmdData.upgradeExtData.cmd2 = UPGRADE_START;
	cmdData.upgradeExtData.lpdata = NULL;
	ctrlhead.dlen = 1;
	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2ShangSun_Params(int iDesID, int iSrcId, int iType, DAMAGE_DETECT_WORK_PARAM tagDAMAGE_DETECT_WORK_PARAM)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = Shangsun_CMD_WORK_PARAM;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = Shangsun_CMD_WORK_PARAM;

	if (iType == 0)//读取
	{
		ShangsunWorkparamsCmd cmd;
		cmd.byCmdType = iType;

		cmdData.ep2016Data.lpdata = (BYTE*)&cmd;
		ctrlhead.dlen = 8 + sizeof(cmd);
	}
	else if (iType == 1)//设置
	{
		SetShangsunWorkparams cmd;
		cmd.tagShangsunWorkparamsCmd.byCmdType = iType;
		cmd.tagDAMAGE_DETECT_WORK_PARAM = tagDAMAGE_DETECT_WORK_PARAM;

		cmdData.ep2016Data.lpdata = (BYTE*)&cmd;
		ctrlhead.dlen = 8 + sizeof(cmd);
	}

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2ShangSun_LockIn_Params(int iDesID, int iSrcId, int iType, LOCK_IN_AMPLIFIER_PARAM tagLOCK_IN_AMPLIFIER_PARAM)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = Shangsun_CMD_WORK_PARAM;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = Shangsun_CMD_LOCKIN_WORK_PARAM;

	if (iType == 0)//读取
	{
		LockInWorkparamsReqCmd cmd;
		cmd.byCmdType = iType;

		cmdData.ep2016Data.lpdata = (BYTE*)&cmd;
		ctrlhead.dlen = 8 + sizeof(cmd);
	}
	else if (iType == 1)//设置
	{
		SetLockInWorkparams cmd;
		cmd.tagLockInWorkparamsCmd.byCmdType = iType;
		cmd.tagLOCK_IN_AMPLIFIER_PARAM = tagLOCK_IN_AMPLIFIER_PARAM;

		cmdData.ep2016Data.lpdata = (BYTE*)&cmd;
		ctrlhead.dlen = 8 + sizeof(cmd);
	}

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2ShangSun_AE_ACQ(int iDesID, int iSrcId, BYTE* pCmd, int iCmdLen)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = Shangsun_CMD_WORK_PARAM;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = CMD_JDSP_SUBPROTOCOL;

	cmdData.ep2016Data.lpdata = pCmd;
	ctrlhead.dlen = 8 + iCmdLen;

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2ShangsunReg_Get(int iDesID, int iSrcId, int iType, GET_REGISTER_PARAMS_cmd getRegParams)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = Shangsun_CMD_GET_REG;

	cmdData.ep2016Data.lpdata = (BYTE*)&getRegParams;
	ctrlhead.dlen = 8 + sizeof(getRegParams);

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2ShangsunReg_Set(int iDesID, int iSrcId, int iType, BYTE* pData, int nLen)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = Shangsun_CMD_SET_REG;

	cmdData.ep2016Data.lpdata = pData;
	ctrlhead.dlen = 8 + nLen;

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2Vibration_Params(int iDesID, int iSrcId, int iType, VIBRATION_PARAM tagVIBRATION_PARAM)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = Shangsun_CMD_WORK_PARAM;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = Shangsun_CMD_VIBRATION_PARAM;

	if (iType == 0)//读取
	{
		ShangsunWorkparamsCmd cmd;
		cmd.byCmdType = iType;

		cmdData.ep2016Data.lpdata = (BYTE*)&cmd;
		ctrlhead.dlen = 8 + sizeof(cmd);
	}
	else if (iType == 1)//设置
	{
		OptVibrationSensorWorkparams cmd;
		cmd.tagVibrationWorkparamsCmd.byCmdType = iType;
		cmd.tagVIBRATION_PARAM = tagVIBRATION_PARAM;

		cmdData.ep2016Data.lpdata = (BYTE*)&cmd;
		ctrlhead.dlen = 8 + sizeof(cmd);
	}

	return pack(data, len);
}


BOOL JHDEqpPkt::Pack_V2Vibration_ADXL345_Params(int iDesID, int iSrcId, int iType, VIBRATION_ADXL345_PARAM tagVIBRATION_PARAM)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = Shangsun_CMD_WORK_PARAM;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = Shangsun_CMD_VIBRATION_PARAM;

	if (iType == 0)//读取
	{
		Vibration_ADXL345_WorkparamsCmd cmd;
		cmd.byCmdType = iType;

		cmdData.ep2016Data.lpdata = (BYTE*)&cmd;
		ctrlhead.dlen = 8 + sizeof(cmd);
	}
	else if (iType == 1)//设置
	{
		OptVibrationSensor_ADXL345_Workparams cmd;
		cmd.tagVibrationWorkparamsCmd.byCmdType = iType;
		cmd.tagVIBRATION_PARAM = tagVIBRATION_PARAM;

		cmdData.ep2016Data.lpdata = (BYTE*)&cmd;
		ctrlhead.dlen = 8 + sizeof(cmd);
	}

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2Supersonic_Get(int iDesID, int iSrcId, int iType, GET_REGISTER_PARAMS_cmd getRegParams)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_GET_REG;

	cmdData.ep2016Data.lpdata = (BYTE*)&getRegParams;
	ctrlhead.dlen = 8 + sizeof(getRegParams);

	return pack(data, len);
}
BOOL JHDEqpPkt::Pack_V2Supersonic_GetWorkParam(int iDesID, int iSrcId, int iType, GetWorkparams_cmd getWorkParams)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_DEVICE_PARAM;

	cmdData.ep2016Data.lpdata = (BYTE*)&getWorkParams;
	ctrlhead.dlen = 8 + sizeof(getWorkParams);

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2Supersonic_Set(int iDesID, int iSrcId, int iType, BYTE * pData, int nLen)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_SET_REG;

	cmdData.ep2016Data.lpdata = pData;
	ctrlhead.dlen = 8 + nLen;

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2Supersonic_SetWorkParam(int iDesID, int iSrcId, int iType, BYTE* pData, int nLen)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_DEVICE_PARAM;

	cmdData.ep2016Data.lpdata = pData;
	ctrlhead.dlen = 8 + nLen;

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2Supersonic_GetChannelRelay(int iDesID, int iSrcId, int iType, GetChannelRelay_cmd param)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_RW_CHANNEL_RELAY;

	cmdData.ep2016Data.lpdata = (BYTE*)&param;
	ctrlhead.dlen = 8 + sizeof(param);

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2Supersonic_SetChannelRelay(int iDesID, int iSrcId, int iType, BYTE* pData, int nLen)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_RW_CHANNEL_RELAY;

	cmdData.ep2016Data.lpdata = pData;
	ctrlhead.dlen = 8 + nLen;

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2Supersonic_GetCfgParamList(int iDesID, int iSrcId, int iType, GetCfgParamList_cmd param)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_RW_CFG_PARAM_LIST;

	cmdData.ep2016Data.lpdata = (BYTE*)&param;
	ctrlhead.dlen = 8 + sizeof(param);

	return pack(data, len);
}

BOOL JHDEqpPkt::Pack_V2Supersonic_SetCfgParamList(int iDesID, int iSrcId, int iType, BYTE* pData, int nLen)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_RW_CFG_PARAM_LIST;

	cmdData.ep2016Data.lpdata = pData;
	ctrlhead.dlen = 8 + nLen;

	return pack(data, len);
}

//0xB505
BOOL JHDEqpPkt::Pack_V2YiBiaoUpgrade_PrepareUpgrade(int iDesID, int iSrcId, StChaoShengYBStartUpgradeReq upgrade_PARAM, int productype)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_UPDATE_START;


	cmdData.ep2016Data.lpdata = (BYTE*)&upgrade_PARAM;
	ctrlhead.dlen = 8 + sizeof(upgrade_PARAM);
	return pack(data, len);
}

//0xB506
BOOL JHDEqpPkt::Pack_V2YiBiaoUpgrade_SendUpgradeData(int iDesID, int iSrcId, StChaoShengYBUpgradePkg upgrade_PARAM, int productype)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_UPDATE_LOAD;

	LPBYTE cmddata = new BYTE[8 + upgrade_PARAM.size];
	int pos = 0;
	*(WORD*)(cmddata + pos) = upgrade_PARAM.id;
	pos += 2;
	*(WORD*)(cmddata + pos) = upgrade_PARAM.count;
	pos += 2;
	*(WORD*)(cmddata + pos) = upgrade_PARAM.size;
	pos += 2;
	*(WORD*)(cmddata + pos) = upgrade_PARAM.crc16;
	pos += 2;

	//DATA
	memcpy(cmddata + pos, upgrade_PARAM.data, upgrade_PARAM.size);
	pos += upgrade_PARAM.size;
	cmdData.ep2016Data.lpdata = cmddata;

	ctrlhead.dlen = 8 + sizeof(upgrade_PARAM) - 4 + upgrade_PARAM.size;
	return pack(data, len);
}

//0xB507
BOOL JHDEqpPkt::Pack_V2YiBiaoUpgrade_UpgradeReq(int iDesID, int iSrcId, StChaoShengYBExeUpgradeReq upgrade_PARAM, int productype)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = productype;
	cmdData.ep2016Data.srcid = iSrcId;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = SUPERSONIC_CMD_UPDATE_RUN;


	cmdData.ep2016Data.lpdata = (BYTE*)&upgrade_PARAM;
	ctrlhead.dlen = 8 + sizeof(upgrade_PARAM);
	return pack(data, len);
}

//0xF005
BOOL JHDEqpPkt::Pack_V2GeneralUpgrade_PrepareUpgrade(int iDesID, StStartUpgradeReq upgrade_PARAM, int productype)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = 0xFFFF;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = COMM_CMD_UPDATE_START;


	cmdData.ep2016Data.lpdata = (BYTE*)&upgrade_PARAM;
	ctrlhead.dlen = 8 + sizeof(upgrade_PARAM);
	return pack(data, len);
}

//0xF006
BOOL JHDEqpPkt::Pack_V2GeneralUpgrade_SendUpgradeData(int iDesID, StUpgradePkg upgrade_PARAM, int productype)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = PRODUCT_TYPE_SUPERSONIC;
	cmdData.ep2016Data.srcid = 0xFFFF;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = COMM_CMD_UPDATE_LOAD;

	LPBYTE cmddata = new BYTE[10 + upgrade_PARAM.size];
	int pos = 0;
	*(DWORD*)(cmddata + pos) = upgrade_PARAM.id;
	pos += 2;
	*(DWORD*)(cmddata + pos) = upgrade_PARAM.count;
	pos += 2;
	*(WORD*)(cmddata + pos) = upgrade_PARAM.size;
	pos += 2;
	*(DWORD*)(cmddata + pos) = upgrade_PARAM.Nsize;
	pos += 2;
	*(DWORD*)(cmddata + pos) = upgrade_PARAM.crc16;
	pos += 2;

	//DATA
	memcpy(cmddata + pos, upgrade_PARAM.data, upgrade_PARAM.size);
	pos += upgrade_PARAM.size;
	cmdData.ep2016Data.lpdata = cmddata;

	ctrlhead.dlen = 8 + sizeof(upgrade_PARAM) - 4 + upgrade_PARAM.size;
	return pack(data, len);
}

//0xF007
BOOL JHDEqpPkt::Pack_V2GeneralUpgrade_UpgradeReq(int iDesID, StUpgradeReq upgrade_PARAM, int productype)
{
	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = iDesID;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = productype;
	cmdData.ep2016Data.srcid = 0xFFFF;
	cmdData.ep2016Data.dstid = iDesID;
	cmdData.ep2016Data.cmdid2 = COMM_CMD_UPDATE_RUN;


	cmdData.ep2016Data.lpdata = (BYTE*)&upgrade_PARAM;
	ctrlhead.dlen = 8 + sizeof(upgrade_PARAM);
	return pack(data, len);
}


BOOL JHDEqpPkt::PackByGSZB2016Cmd2(UINT cmd2, LPVOID lpdata, int len, UINT productid, UINT dstid)
{
	if (productid == 0)
		productid = GetDefaultProductID();

	head = 0x68;
	tail = 0x97;
	ctrlhead.dir = EQP_PKT_DIR_TOEQP;
	ctrlhead.cmdid = GSZB2016Ext_Protocol_Cmd;
	ctrlhead.ceid = dstid;
	ctrlhead.flag = GSZB2016Ext_FLAG_VAL;
	cmdData.ep2016Data.protover = 0x01;
	cmdData.ep2016Data.productid = productid;
	cmdData.ep2016Data.srcid = 0xFFFF;
	cmdData.ep2016Data.dstid = dstid;
	cmdData.ep2016Data.cmdid2 = cmd2;

	cmdData.ep2016Data.lpdata = lpdata;
	ctrlhead.dlen = 8 + len;

	return pack(data, len);
}

string JHDEqpPkt::GetCmdDesc_EPV0_Upgrade(int cmd_upgrade, JHDEqpPkt* pPkt)
{
	if (cmd_upgrade == UPGRADE_READ_VER)
	{
		return "EVP0低速模块升级 读取版本";
	}
	else if (cmd_upgrade == UPGRADE_DOWNLOAD_FILE)
	{
		WORD& wPktId = *(WORD*)pPkt->cmdData.upgradeExtData.lpdata;
		if (wPktId == 0x00)
		{
			return "EVP0低速模块升级 发送文件信息请求升级";
		}
		else if (wPktId == 0xffff)
		{
			BYTE checkState = pPkt->cmdData.upgradeExtData.lpdata[2];
			if (checkState == 0)
				m_strCmdContent = "包完整性检查";
			else if (checkState == 1)
				m_strCmdContent = "CRC检查";
			return "EVP0低速模块升级 文件检查";
		}
		else
		{
			string str = str::format("EVP0低速模块升级 下载升级包编号%d", wPktId);
			return str.c_str();
		}
	}
	else if (cmd_upgrade == UPGRADE_START)
	{
		return "EVP0低速模块升级 开始升级";
	}
	return "";
}

string JHDEqpPkt::GetCmdDesc_EPV1(int cmd_v1, JHDEqpPkt* pPkt)
{
	//2012-11-19 油压油位 2级命令





////5.20	采集器主动上报缺口采集按键触发信号
//#define BY_CMD2_REPORTRIGGER 0x06
////5.21	主机推送识别后的表示缺口偏移值到采集器
//#define BY_CMD2_SEND_GAP 0x07
////5.22	读取道岔当前工务参数
//#define BY_CMD2_GONGWU_READDATA 0x30
////5.23	读取道岔工务参数测量归零参数
//#define BY_CMD2_GONGWU_READ_ZEROPARAMDATA 0x31
////5.24	设置道岔工务参数测量归零参数
//#define BY_CMD2_GONGWU_SET_ZEROPARAMDATA 0x32
////5.25	采集器模拟采样通道校正命令
//#define BY_CMD2_ACQ_CHANNEL_CHECK 0x28
////5.15.1读取爬行
//#define BY_CMD2_GONGWU_READDATA_PAXING 0x33
////5.4.2读取密贴
//#define BY_CMD2_GONGWU_READDATA_MITIE 0x34
////5.15.3读取轨距
//#define BY_CMD2_GONGWU_READDATA_GUIJU 0x35
//



///*命令响应状态：
//0--请等待，已经开始采集缺口数据；
//1-无对应缺口采集设备；
//2-其它错误；*/
//#define BY_YYYWRIGGER_STATUS_WAIT 0x00
//#define BY_YYYWRIGGER_STATUS_NONE 0x01
//#define BY_YYYWRIGGER_STATUS_OTHER 0x02




////5.12	设置采集器地址
//#define BY_CMD_YYYW_SET_ID 0x20
////5.13	设置通讯参数
//#define BY_CMD_YYYW_SET_COMMPARAM 0x21
////5.14	广播读取采集器地址
//#define BY_CMD_YYYW_READ_ID 0x22
////5.15	广播设置采集器地址
//#define BY_CMD_YYYW_BROAD_SET_ID 0x23
////5.16	配置采集器工作参数
//#define BY_CMD_YYYW_SET_PARAM 0x24
////5.17	读取采集器工作参数
//#define BY_CMD_YYYW_READ_PARAM 0x25
////5.18	配置振动采集策略
//#define BY_CMD_YYYW_SET_ZHENDONG_RULE 0x26
////5.19	读取振动采集策略
//#define BY_CMD_YYYW_READ_ZHENDONG_RULE 0x27


	if (cmd_v1 == BY_CMD2_BUTTON_PHOTO)
	{
		return ("EVP1 按钮拍照事件");
	}
	else if (cmd_v1 == BY_CMD2_READ_YOUYA_DATA)
	{
		return ("EVP1 读取待上送扳动过程油压数据记录");
	}
	else if (cmd_v1 == BY_CMD2_READ_MOVE_WAVE_DATA)
	{
		return ("EVP1 读取待上送扳动过程加速度数据记录");
	}
	else if (cmd_v1 == BY_CMD2_READ_TRAIN_WAVE_DATA)
	{
		return ("EVP1 读取待上送过车过程加速度数据记录");
	}
	else if (cmd_v1 == BY_CMD2_REPORT_MOVE)
	{
		return ("EVP1 检测到道岔扳动状态变化");
	}
	else if (cmd_v1 == BY_CMD2_REPORT_YOUYA)
	{
		return ("EVP1 采集器主动上报静态油压");
	}
	else if (cmd_v1 == BY_CMD2_REPORT_YOUWEI)
	{
		return ("EVP1 采集器主动上报油位数据");
	}
	else if (cmd_v1 == BY_CMD2_REPORT_TRAIN)
	{
		return ("EVP1 采集器主动上报过车信号");
	}
	else if (cmd_v1 == BY_CMD2_REPORT_STATUS)
	{
		return ("EVP1 采集器主动上报当前工作状态");
	}
	else if (cmd_v1 == BY_CMD_YYYW_READ_CURRENT)
	{
		return ("EVP1 读取当前油压油位");
	}
	else if (cmd_v1 == BY_CMD2_YYYW_READ_STATUS)
	{
		return ("EVP1 读取当前数据状态");
	}
	else if (cmd_v1 == BY_CMD2_YYYW_DATA_OK)
	{
		return ("EVP1 确认油压曲线数据");
	}
	else if (cmd_v1 == BY_CMD2_SEND_CURRENT_GAP)
	{
		return ("主机推送识别后的表示缺口偏移值到采集器");
	}
	else
	{
		return ("EVP1 unknown");
	}



}

string JHDEqpPkt::GetCmdName()
{
	//#define BY_CMD_SET_AUDIOINFO	0x0b	//01011	设置音频参数	双向	2009-08-21
	//#define BY_CMD_ON_OFF 0x14	//10100	开关图像组	双向	2009-4-17

	if (ctrlhead.cmdid == BY_CMD_SETCLOCK)
	{
		return ("EPV0 设置时钟");
	}
	else if (ctrlhead.cmdid == BY_CMD_SETADDNO)
	{
		return ("EPV0 设置设备id");
	}
	else if (ctrlhead.cmdid == BY_CMD_SET_COMMPARAM)
	{
		return ("EPV0 设置通信参数");
	}
	else if (ctrlhead.cmdid == BY_CMD_RESTART)
	{
		return ("EPV0 重启设备");
	}
	else if (ctrlhead.cmdid == BY_CMD_SET_VIDEOINFO)
	{
		return ("EPV0 设置码流参数");
	}
	else if (ctrlhead.cmdid == BY_CMD_SETCANANDID)
	{
		return ("EPV0 设置查询Can参数");
	}
	else if (ctrlhead.cmdid == BY_CMD_READSTATUS)
	{
		return ("EPV0 查询设备状态");
	}
	else if (ctrlhead.cmdid == BY_CMD_READ_IMAGE_DATA)
	{
		if (ctrlhead.dir == EQP_PKT_DIR_TOEQP) //下行
		{

		}
		else if (ctrlhead.dir == EQP_PKT_DIR_TOHOST)
		{

		}
		return ("EPV0 读取文件数据");
	}
	else if (ctrlhead.cmdid == 0x13)
	{
		return ("EPV0 确认文件数据");
	}
	else if (ctrlhead.cmdid == BY_CMD_OPEN_RECORD)
	{
		if (ctrlhead.dir == EQP_PKT_DIR_TOEQP) //下行
		{
		}
		else if (ctrlhead.dir == EQP_PKT_DIR_TOHOST)
		{

		}

		return ("EPV0 开关录像");
	}
	else if (ctrlhead.cmdid == BY_CMD_OPEN_STREAM)
	{
		return ("EPV0 开关实时码流");
	}
	else if (ctrlhead.cmdid == BY_CMD_READ_FILEINFO)
	{
		return ("EPV0 读取文件信息");
	}
	else if (ctrlhead.cmdid == D520Ext_Protocol_Cmd) //EPV1扩展
	{
		return JHDEqpPkt::GetCmdDesc_EPV1(cmdData.d520ExtData.cmd2, this);
	}
	else if (ctrlhead.cmdid == UpgradeExt_Protocol_Cmd) //EPV0低速载波模块升级
	{
		return JHDEqpPkt::GetCmdDesc_EPV0_Upgrade(cmdData.upgradeExtData.cmd2, this);
	}
	else if (ctrlhead.cmdid == GSZB2016Ext_Protocol_Cmd)
	{
		return JHDEqpPkt::GetCmdDesc_EPV2(cmdData.ep2016Data.cmdid2, this);
	}
	else
	{
		return ("其他命令");
	}
}



UINT JHDEqpPkt::SetDefaultProductID(UINT productid)
{
	if (productid != 0)
		g_productid = productid;
	else
		g_productid = PRODUCT_TYPE_GAP;

	return g_productid;
}

UINT JHDEqpPkt::GetDefaultProductID()
{
	return g_productid;
}

WORD JHDEqpPkt::ShortToJHDShort(short s)
{
	WORD w;
	if (s >= 0)
	{
		w = s;
	}
	else
	{
		w = abs(s);
		w = w | 0x8000;
	}

	return w;
}

short JHDEqpPkt::JHDShortToShort(WORD s)
{
	int sign = s & 0x8000; // 0为正，1为负
	s = s & 0x7FFF;
	if (!sign) return s;
	else return -s;
}


string JHDEqpPkt::GetCmdDesc_EPV2(int cmd2, JHDEqpPkt* pPkt)
{
	/*if (cmd2 == COMM_CMD_HEARTBEAT)
	{
		return string("EPV2 心跳");
	}
	else if (cmd2 == COMM_CMD_GET_ID)
	{
		return ("EPV2 读取ID");
	}
	else if (cmd2 == COMM_CMD_SET_ID)
	{
		return ("EPV2 设置ID");
	}
	else if (cmd2 == COMM_CMD_GET_DEVINFO)
	{
		return ("EPV2 读取模块制造信息");
	}
	else if (cmd2 == COMM_CMD_SET_DEVINFO)
	{
		return ("EPV2 设置模块制造信息");
	}
	else if (cmd2 == COMM_CMD_UPDATE_START)
	{
		return ("EPV2 开始升级");
	}
	else if (cmd2 == COMM_CMD_UPDATE_LOAD)
	{
		return ("EPV2 发送升级包");
	}
	else if (cmd2 == COMM_CMD_UPDATE_RUN)
	{
		return ("EPV2 执行升级");
	}
	else if (cmd2 == COMM_CMD_UPDATE_RESULT)
	{
		return ("EPV2 上报升级结果");
	}
	else if (cmd2 == COMM_CMD_GET_NETINFO)
	{
		return ("EPV2 读取网络参数");
	}
	else if (cmd2 == COMM_CMD_SET_NETINFO)
	{
		return ("EPV2 设置网络参数");
	}
	else if (cmd2 == COMM_CMD_RESTART)
	{
		return ("EPV2 重启设备");
	}
	else if (cmd2 == COMM_CMD_RESTORE)
	{
		return ("EPV2 恢复出厂设置");
	}
	else if (cmd2 == COMM_CMD_TIMING)
	{
		return ("EPV2 时钟校准");
	}
	else if (cmd2 == EQP_CMD_GET_FILE_LIST)
	{
		return ("EPV2 获取文件列表");
	}
	else if (cmd2 == EQP_CMD_GET_FILE_DATA)
	{
		return ("EPV2 读取文件数据");
	}*/
	//else if (cmd2 == EQP_CMD_DELETE_FILE)
	//{
	//	return ("EPV2 删除文件");
	//}
	//else if (cmd2 == EQp_CMD_STOP_READFILE)
	//{
	//	return ("EPV2 停止文件读取");
	//}
	//else if (cmd2 == EQP_CMD_REPORT_MOVE_START)
	//{
	//	return ("EPV2 上报道岔扳动启动");
	//}
	//else if (cmd2 == EQP_CMD_REPORT_MOVE_END)
	//{
	//	return ("EPV2 上报道岔扳动结束");
	//}
	//else if (cmd2 == EQP_CMD_REPORT_ACQ_FINISH)
	//{
	//	return ("EPV2 上报事件采集结果");
	//}
	//else if (cmd2 == EQP_CMD_SET_MOVE_START)
	//{
	//	return ("EPV2 下报扳动开始");
	//}
	//else if (cmd2 == EQP_CMD_SET_MOVE_END)
	//{
	//	return ("EPV2 下报扳动结束");
	//}
	//else if (cmd2 == EQP_CMD_SET_CROSS_START)
	//{
	//	return ("EPV2 下报过车开始");
	//}
	//else if (cmd2 == EQP_CMD_SET_CROSS_EDN)
	//{
	//	return ("EPV2 下报过车结束");
	//}
	//else if (cmd2 == CMD_STRESS_TEST)
	//{
	//	return ("EPV2 压力测试");
	//}
	//else if (cmd2 == COMM_CMD_GET_AI)
	//{
	//	return ("EPV2 获取模拟量通道实时值");
	//}
	//else if (cmd2 == EQP_CMD_CAN_ONLINE_LIST)
	//{
	//	return ("EPV2 Can网关在线设备列表");
	//}
	//else if (cmd2 == COMM_CMD_EQP_ERROR)
	//{
	//	return ("EPV2 设备异常主动上送通知");
	//}
	//else if (cmd2 == CMD_GET_JIAYOU_LEVEL)
	//{
	//	return ("获得当前加油装置油位");
	//}
	//else if (cmd2 == CMD_DO_JIAYOU)
	//{
	//return "执行加油";
	//}
	//else if (cmd2 == CMD_GET_JIAYOU_PRESSURE)
	//{
	//	return ("获取加油压力");
	//}
	//else if (cmd2 == CMD_DO_JIAYOU_RESULT) {
	//	return ("加油结果通知");
	//}
	////EPV2 直接阻力采集产品命令
	//else if (cmd2 == IMDPOWER_CMD_READ_CUR_POWER)
	//{
	//	return ("读取当前阻力");
	//}
	//else if (cmd2 == IMDPOWER_CMD_GET_POWER_CHECK_PARAM)
	//{
	//	return ("读取阻力校准参数");
	//}
	//else if (cmd2 == IMDPOWER_CMD_SET_POWER_CHECK_PARAM)
	//{
	//	return ("设置阻力校准参数");
	//}
	//else if (cmd2 == IMDPOWER_CMD_GET_ELEC_CHECK_PARAM)
	//{
	//	return ("读取电流校准参数");
	//}
	//else if (cmd2 == IMDPOWER_CMD_SET_ELEC_CHECK_PARAM)
	//{
	//	return ("设置电流校准参数");
	//}

	//else if (cmd2 == IMDPOWER_CMD_SET_WORK_PARAM)
	//{
	//	return ("设置阻力工作参数");
	//}
	//else if (cmd2 == IMDPOWER_CMD_GET_WORK_PARAM)
	//{
	//	return ("读取阻力工作参数");
	//}
	//else
	//{
	//	return ("未知命令");
	//}
return "未知命令";
}


BOOL JHDEqpPkt::IsSameCmd(BYTE* pReq, int iReqLen, BYTE* pResp, int iRespLen)
{
	JHDEqpPkt req, resp;
	if (req.unpack(pReq, iReqLen) && resp.unpack(pResp, iRespLen))
	{
		return IsSameCmd(req, resp);
	}

	return FALSE;
}



BOOL JHDEqpPkt::IsSameCmd(JHDEqpPkt& pkt1, JHDEqpPkt& pkt2)
{
	BOOL bSameCmd = TRUE;
	if (pkt1.ctrlhead.cmdid != pkt2.ctrlhead.cmdid)
	{
		bSameCmd = FALSE;
	}
	else
	{
		if (pkt1.ctrlhead.cmdid == BY_CMD_D520ExtensionProtocol)
		{
			if (pkt1.cmdData.d520ExtData.cmd2 != pkt2.cmdData.d520ExtData.cmd2)
			{
				bSameCmd = FALSE;
			}
		}
		else if (pkt1.ctrlhead.cmdid == GSZB2016Ext_Protocol_Cmd)
		{
			if (pkt1.cmdData.ep2016Data.cmdid2 != pkt2.cmdData.ep2016Data.cmdid2)
			{
				bSameCmd = FALSE;
			}
		}
	}

	return bSameCmd;
}

BOOL JHDEqpPkt::IsSameCmd(CMD_IDENT id, JHDEqpPkt& pkt)
{
	BOOL bSameCmd = TRUE;
	if (id.cmd1 == pkt.ctrlhead.cmdid)
	{
		bSameCmd = TRUE;
	}
	else
	{
		if (id.cmd1 == BY_CMD_D520ExtensionProtocol)
		{
			if (id.cmd2 != pkt.cmdData.d520ExtData.cmd2)
			{
				bSameCmd = FALSE;
			}
		}
		else if (id.cmd1 == GSZB2016Ext_Protocol_Cmd)
		{
			if (id.cmd2 != pkt.cmdData.ep2016Data.cmdid2)
			{
				bSameCmd = FALSE;
			}
		}
	}

	return bSameCmd;
}



//string JHDEqpPkt::GetCmdIdent()
//{
//	string str;
//	if(ctrlhead.cmdid == D520Ext_Protocol_Cmd) //EPV1扩展
//	{
//		str.Format(("%d_%d"),D520Ext_Protocol_Cmd,cmdData.d520ExtData.cmd2);
//	}
//	else if(ctrlhead.cmdid == GSZB2016Ext_Protocol_Cmd)
//	{
//		str.Format(("%d_%d"),GSZB2016Ext_Protocol_Cmd,cmdData.ep2016Data.cmdid2);
//	}
//	else
//	{
//		str.Format(("%d_0"),ctrlhead.cmdid);
//	}
//	
//	return str;
//}

void JHDEqpPkt::GetCmdID(int& cmd1, int& cmd2)
{
	cmd1 = ctrlhead.cmdid;
	if (ctrlhead.cmdid == D520Ext_Protocol_Cmd) //EPV1扩展
	{
		cmd2 = cmdData.d520ExtData.cmd2;
	}
	else if (ctrlhead.cmdid == GSZB2016Ext_Protocol_Cmd)
	{
		cmd2 = cmdData.ep2016Data.cmdid2;
	}
	else if (ctrlhead.cmdid == UpgradeExt_Protocol_Cmd)
	{
		cmd2 = cmdData.upgradeExtData.cmd2;
	}
	else
	{
		cmd2 = 0;
	}
}


string JHDEqpPkt::GetCmdID()
{
	int cmd1, cmd2;
	GetCmdID(cmd1, cmd2);

	string str = str::format(("0x%02X-0x%04X"), cmd1, cmd2);
	

	return str;
}


BYTE JHDEqpPkt::GetCheckSum(PBYTE pBuf, DWORD dwDataLen)
{
	BYTE byReturn = 0;
	DWORD dwLoop;
	for (dwLoop = 0; dwLoop < dwDataLen; dwLoop++)
		byReturn += pBuf[dwLoop];
	return byReturn;
}

void JHDEqpPkt::LogCommData(BOOL bSendOrRecv, void* pData, int iLen, int iID, string strIP, BOOL bLogCommData /*= FALSE*/)
{

}


JHD_EQP_PROTO_VER JHDEqpPkt::GetVer()
{
	if (ctrlhead.cmdid == GSZB2016Ext_Protocol_Cmd)
	{
		return EPV2;
	}
	else
	{
		return EPV1;
	}
}

//vs2010是大端存储方案
DWORD JHDEqpPkt::IPToJHDDWORD(string strIp)
{
	DWORD ret = 0;
	//BYTE b[4];
	//string strTmp;
	//int pos = 0, lastpos = 0;
	//for (int i = 0; i < 3; i++) {
	//	pos = strIp.find(("."), lastpos);
	//	if (pos == -1) return 0;
	//	string strRusult = strIp.Mid(lastpos, pos - lastpos);
	//	b[i] = toi(strRusult);
	//	lastpos = pos + 1;
	//}
	//string strRusult = strIp.Right(strIp.GetLength() - lastpos);
	//b[3] = toi(strRusult);
	//for (int i = 3; i >= 0; i--) {
	//	int data = b[i];
	//	data <<= i * 8;
	//	ret += data;
	//}
	return ret;
}

//vs2010是大端存储方案
string JHDEqpPkt::JHDDWORDToIP(DWORD ip)
{
	BYTE b[4];
	for (int i = 0; i < 4; i++) {
		b[i] = ip >> (i * 8);
	}

	string strIp = str::format(("%d.%d.%d.%d"), b[0], b[1], b[2], b[3]);
	
	return strIp;
}

JHDEqpPkt& JHDEqpPkt::operator=(const JHDEqpPkt& right)
{
	if (this == &right) return *this;
	head = right.head;
	ctrlhead = right.ctrlhead;
	data = right.data;
	check = right.check;
	tail = right.tail;
	m_strCmdName = right.m_strCmdName;

	strPktData = right.strPktData;
	strCmdData = right.strCmdData;
	m_strCmdContent = right.m_strCmdContent;
	m_strUnpackErrorInfo = right.m_strUnpackErrorInfo;
	len = right.m_iCmdBufLen;
	data = new BYTE[len];
	memcpy(data, right.data, len);
	m_CmdBuf = data + 1 + 4 + 8;
	return *this;
}
