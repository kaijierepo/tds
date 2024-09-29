#include "Parse315Protocol.h"
#include "logger.h"
#include "tchar.h"
#include "inaddr.h"


BOOL Parse315Protocol::b26res4byte = FALSE;
BOOL Parse315Protocol::b2Flen2byte = FALSE;


DWORD g_dw0x23ExCmdID = 0x00;	//道岔缺口扩展配置命令ID

BOOL CVedioParser::Parse(StVedioFrame& data, LPVOID, int)
{
	return TRUE;
}

BOOL CVedioParser::Unparse(StVedioFrame& data, vector<BYTE>& buf, int& len)
{
	len = data.datalen + 5 + 4 + 4;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	LPBYTE pos = (LPBYTE)buf.data();
	memcpy_s(pos, 5 + 4, &data, 5 + 4);
	pos += 5 + 4;

	if (data.datalen != 0)
	{
		StDataBasic* basic = (StDataBasic*)data.lpdata;

		vector<BYTE> subbuf;
		int sublen = 0;
		BOOL success = FALSE;

		switch (basic->cmdid)
		{
		case VEDIO_START_FILE:
			success = CVedioParser::Unparse(*(StVedioFileStart*)data.lpdata, subbuf, sublen);
			break;
		case VEDIO_STOP_FILE:
			success = CVedioParser::Unparse(*(StVedioFileStop*)data.lpdata, subbuf, sublen);
			break;
		case VEDIO_SET_TITLE:
			success = CVedioParser::Unparse(*(StVedioSetTitle*)data.lpdata, subbuf, sublen);
			break;
		case VEDIO_REAL_CTRL:
			success = CVedioParser::Unparse(*(StVedioRealCtrl*)data.lpdata, subbuf, sublen);
			break;
		case VEDIO_REAL_STREAM:
			success = CVedioParser::Unparse(*(StVedioRealPlay*)data.lpdata, subbuf, sublen);
			break;
		default:
			break;
		}

		if (!success)
		{
			return FALSE;
		}

		if (sublen != data.datalen)
		{
			return FALSE;
		}

		memcpy_s(pos, sublen, subbuf.data(), sublen);

		pos += sublen;
		//delete[] subbuf;

	}

	memcpy_s(pos, 4, &data.ftail, 4);
	pos += 4;

	return TRUE;
}

BOOL CVedioParser::Release(StVedioFrame& data)
{
	if (data.datalen == 0)
		return TRUE;

	StDataBasic* basic = (StDataBasic*)data.lpdata;

	switch (basic->cmdid)
	{
	case VEDIO_START_FILE:
		CVedioParser::Release(*(StVedioFileStart*)data.lpdata);
		break;
	case VEDIO_STOP_FILE:
		break;
	case VEDIO_SET_TITLE:
		CVedioParser::Release(*(StVedioSetTitle*)data.lpdata);
		break;
	case VEDIO_REAL_CTRL:
		break;
	case VEDIO_REAL_STREAM:
		CVedioParser::Release(*(StVedioRealPlay*)data.lpdata);
		break;
	default:
		break;
	}

	delete data.lpdata;

	return TRUE;
}

BOOL CVedioParser::Release(StVedioFileStart& data)
{
	if (data.titlelen != 0)
		delete[] data.title;

	if (data.pathlen != 0)
		delete[] data.path;

	return TRUE;
}

BOOL CVedioParser::Release(StVedioSetTitle& data)
{
	if (data.titlelen != 0)
		delete[] data.title;

	return TRUE;
}

BOOL CVedioParser::Release(StVedioRealPlay& data)
{
	if (data.len != 0)
		delete[] data.lpdata;

	return TRUE;
}

BOOL CVedioParser::Unparse(StVedioFileStart& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 1 + 2 + 2 + 2 + data.titlelen + data.pathlen;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	LPBYTE pos = (LPBYTE)buf.data();

	memcpy_s(pos, 1 + 1 + 2 + 2, &data, 1 + 1 + 2 + 2);
	pos += 1 + 1 + 2 + 2;

	if (data.titlelen > 0)
	{
		memcpy_s(pos, data.titlelen, data.title, data.titlelen);
		pos += data.titlelen;
	}

	memcpy_s(pos, 2, &data.pathlen, 2);
	pos += 2;

	if (data.pathlen > 0)
	{
		memcpy_s(pos, data.pathlen, data.path, data.pathlen);
		pos += data.pathlen;
	}

	return TRUE;
}

BOOL CVedioParser::Unparse(StVedioFileStop& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 1;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL CVedioParser::Unparse(StVedioSetTitle& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 1 + 2 + data.titlelen;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	LPBYTE pos = (LPBYTE)buf.data();

	memcpy_s(pos, 1 + 1 + 2, &data, 1 + 1 + 2);
	pos += 1 + 1 + 2;

	if (data.titlelen > 0)
	{
		memcpy_s(pos, data.titlelen, data.title, data.titlelen);
		pos += data.titlelen;
	}

	return TRUE;
}

BOOL CVedioParser::Unparse(StVedioRealCtrl& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 1 + 1 + 2 + 1 + 2 + 2;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL CVedioParser::Unparse(StVedioRealPlay& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 1 + 2 + 2 + data.len;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	LPBYTE pos = (LPBYTE)buf.data();

	memcpy_s(pos, 1 + 1 + 2 + 2, &data, 1 + 1 + 2 + 2);
	pos += 1 + 1 + 2 + 2;

	if (data.len > 0)
	{
		memcpy_s(pos, data.len, data.lpdata, data.len);
		pos += data.len;
	}
	return TRUE;
}


//buf指向数据内容 len数据内容的长度  返回data 和 最新位置的buf  LPVOID*只是为了方便返回最新位置
BOOL Parse315Protocol::ParseDataFrm(StFrame& data, LPVOID* buf, int len, int dir)
{
	LPBYTE pos = (LPBYTE)(*buf);
	StDataBasic* basic = (StDataBasic*)pos;
	//TRACE("\n收到命令cmd:0x%02x\n", basic->cmdid);
	switch (basic->cmdid)
	{
	case CMD_CODE_1DQJINFO:
	{
		St1DQJInfo* cbBuffer = new St1DQJInfo;
		memcpy(cbBuffer, pos, 8);
		data.lpdata = cbBuffer;
		pos += 8;

		break;
	}
	case CMD_CODE_JDSP://0xF0
	{
		StJDSP* lpdata = new StJDSP;
		char* szJdsp = new char[data.datalen - 4 - 1 + 1];
		memset(szJdsp, 0, data.datalen - 4 - 1 + 1);
		memcpy(szJdsp, pos + 1 + 4, data.datalen - 1 - 4);
		lpdata->jdsp = szJdsp;
		lpdata->cmdid = pos[0];
		memcpy(lpdata->res, pos + 1, 4);
		data.lpdata = lpdata;
		pos += data.datalen;
		delete szJdsp;
		break;
	}
	case CMD_CODE_GAPCFG://0x23
	{
		StGapCfgRes* lpdata = new StGapCfgRes;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen, data.e_frmKind))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case (BYTE)E_315_PROTOCOL_TYPE::OIL_BOX_VOLUME_0x53:
	{
		StOilBoxVolumeData* lpdata = new StOilBoxVolumeData;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen, data.e_frmKind))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_GAPVAL://0x26
	{
		StGapValue* lpdata = new StGapValue;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen, data.e_frmKind))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_ALARM_AND_IMG://0x27
	case CMD_CODE_ALARM:	//0x97
	{
		StAlarmAndImgInfo* lpdata = new StAlarmAndImgInfo;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen, data.e_frmKind))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_ACTION_INFO://0x28
	{
		StActionInfo* lpdata = new StActionInfo;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen, data.e_frmKind))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_LASTGAPIMG://0x29
	{
		StLastGapImgRes* lpdata = new StLastGapImgRes;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen, data.e_frmKind))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_IMGLIST:
	{
		if (dir == 1)
		{
			StImgListRes* lpdata = new StImgListRes;
			if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		else
		{
			StImgListReq* lpdata = new StImgListReq;
			if (Parse315Protocol::ParseReq(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		break;
	}
	case CMD_CODE_IMGINFO:
	{
		if (dir == 1)
		{
			StImgInfoRes* lpdata = new StImgInfoRes;
			if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		else
		{
			StImgInfoReq* lpdata = new StImgInfoReq;
			if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		break;
	}
	case CMD_CODE_VEDIOLIST:
	{
		if (dir == 1)
		{
			StVedioListRes* lpdata = new StVedioListRes;
			if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		else
		{
			StVedioListReq* lpdata = new StVedioListReq;
			if (Parse315Protocol::ParseReq(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		break;
	}
	case CMD_CODE_VEDIOFILE:
	{
		if (b2Flen2byte)
		{
			auto lpdata = new StVedioFileRes2;
			if (Parse315Protocol::Parse(lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		else
		{
			auto lpdata = new StVedioFileRes4;
			if (Parse315Protocol::Parse(lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		break;
	}
	case CMD_CODE_YYQX:
	{
		StOilPreCurve* lpdata = new StOilPreCurve;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_YWINFO:
	{
		StOilLevelInfo* lpdata = new StOilLevelInfo;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_REALCTRL:
	{
		StRealCtrlRes* lpdata = new StRealCtrlRes;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_REALSTREAM:
	{
		StRealStream* lpdata = new StRealStream;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_ELECCURVE:
	{
		StElecCurve* lpdata = new StElecCurve;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_POWSTATIC:
	{
		StStaticPowerList* lpdata = new StStaticPowerList;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_POWERLIST:
	{
		if (dir == 1)
		{
			StPowerListRes* lpdata = new StPowerListRes;
			if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		else
		{
			StPowerListReq* lpdata = new StPowerListReq;
			if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		break;
	}
	case CMD_CODE_POWERCONTENT:
	{
		StPowerInfoRes* lpdata = new StPowerInfoRes;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case CMD_CODE_NEW_DATA_FILE:
	{
		NewDataFile* lpdata = new NewDataFile;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	// 	case CMD_CODE_NEW_DATA_FILE:
	// 	{
	// 		StNewPowerNotify* lpdata = new StNewPowerNotify;
	// 		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
	// 		{
	// 			data.lpdata = lpdata;
	// 			pos += data.datalen;
	// 		}
	// 		else
	// 		{
	// 			delete lpdata;
	// 			return FALSE;
	// 		}
	// 		break;
	// 	}
	case CMD_CODE_QUERY_POWER_FILE:
	{
		if ((data.datalen - 1) % sizeof(StPowerFileList) == 0)
		{
			//StPowerFileList* lpdata = new StPowerFileList[(data.datalen - 1) / sizeof(StPowerFileList)];
			StPowerFileListF2* lpdata = new StPowerFileListF2;
			lpdata->cmdid = *(BYTE*)pos;
			lpdata->nListCount = (data.datalen - 1) / sizeof(StPowerFileList);
			lpdata->list = new StPowerFileList[lpdata->nListCount];
			CopyMemory(lpdata->list, pos + 1, data.datalen - 1);
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			return FALSE;
		}
		break;
	}
	case CMD_CODE_DOWNLOAD_DATA_FILE:
	{
		if (dir == 1)
		{
			StPowerFileData* lpdata = new StPowerFileData;
			if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
		}
		else
		{
			StPowerFileDataResq* lpdata = new StPowerFileDataResq;
			size_t sz = sizeof(StPowerFileDataResq);
			memcpy_s(lpdata, sz, pos, sz);
			data.lpdata = lpdata;
			pos += data.datalen;
			//if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
			//{
			//	data.lpdata = lpdata;
			//	pos += data.datalen;
			//}
			//else
			//{
			//	delete lpdata;
			//	return FALSE;
			//}
		}
		break;
	}
	case 0x51:
	{
		if (data.datalen == sizeof(StManualOilingRes))
		{
			//StPowerFileList* lpdata = new StPowerFileList[(data.datalen - 1) / sizeof(StPowerFileList)];
			StManualOilingRes* lpdata = new StManualOilingRes;
			CopyMemory(lpdata, pos, data.datalen);
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			return FALSE;
		}
		break;
	}
	case 0x52:
	{
		if (data.datalen == sizeof(StOilingResultNotify))
		{
			//StPowerFileList* lpdata = new StPowerFileList[(data.datalen - 1) / sizeof(StPowerFileList)];
			StOilingResultNotify* lpdata = new StOilingResultNotify;
			CopyMemory(lpdata, pos, data.datalen);
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			return FALSE;
		}
		break;
	}
	case (uint8_t)E_315_PROTOCOL_TYPE::DBJ_FBJ_0x60:
	{
		DBJFBJInfo* lpdata = new DBJFBJInfo;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case (uint8_t)E_315_PROTOCOL_TYPE::UN_RECOVER_ALARM_0x65:
	{
		StAlarmListRes* lpdata = new StAlarmListRes;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case (uint8_t)E_315_PROTOCOL_TYPE::GONGKUANG_REAL_VAL_0x81:
	{
		StWorkingConditionValRes* lpdata = new StWorkingConditionValRes;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case (uint8_t)E_315_PROTOCOL_TYPE::GONGKUANG_INIT_VALUE_0x82:
	{
		StOpWorkingConditionRes* lpdata = new StOpWorkingConditionRes;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	case (uint8_t)E_315_PROTOCOL_TYPE::GONGKUANG_ZD_CURVE_0x83:
	{
		StVibrationCurveRes* lpdata = new StVibrationCurveRes;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	default:
		break;
	}

	if (g_dw0x23ExCmdID > 0 && basic->cmdid == g_dw0x23ExCmdID)
	{
		StGapCfgRes* lpdata = new StGapCfgRes;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen, data.e_frmKind))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
	}
	*buf = pos;
	return true;
	//if (dir == 1)
	//
	//dir = 0
	/*else
	{
	switch (basic->cmdid)
	{
	case CMD_CODE_ELECCURVE:
	{
		StElecCurveRec* lpdata = new StElecCurveRec;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return FALSE;
		}
		break;
	}
	default:
		break;
	}
	}
	break;*/
}

//将buf解析为StFrame, 内部包含包的类别 命令还是应答等
BOOL Parse315Protocol::Parse(StFrame& data, LPVOID buf, int len, int dir)
{
	ZeroMemory(&data, sizeof(data));
	data.e_frmKind = GENERAL;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 5 + 1 + 1 + 1 + 4;
	if (sz > (unsigned)len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	if (data.datalen == 0)
	{
		data.datalen = len - 16;
	}
	pos += sz;

	sz = 5 + 1 + 1 + 1 + 4 + data.datalen + 4;
	if (sz != (unsigned)len)//严格一点
		return FALSE;

	//这里pos指向数据部分
	if (data.datalen > 0)
	{
		switch (data.ftype)
		{

		case FRAME_TYPE_HEARTBEAT:
		{
			StHeartBeat315* lpdata = new StHeartBeat315;
			if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
			break;
		}
		case FRAME_TYPE_DATA:
		{
			if (!ParseDataFrm(data, (LPVOID*)&pos, data.datalen, dir)) { //pos传地址用于在内部更新最新位置,失败了内部释放内存
				return false;
			};
			break;
		}
		default:
			break;
		}
	}//if

	memcpy_s(&data.ftail, 4, pos, 4);
	pos += 4;

	if (data.ftail != 0xFFFFFFFF || pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data, dir);
		return FALSE;
	}

	return TRUE;
}

BOOL Parse315Protocol::Parse(StAlarmListRes& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(S_315_ALARM_INFO);
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lprecord = new BYTE[sz];
		memcpy_s(data.lprecord, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

/*
BOOL Parse315Protocol::Parse(StFrame& data, LPVOID buf, int len, int dir)
{
	ZeroMemory(&data, sizeof(data));

	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 5 + 1 + 1 + 1 + 4;
	if (sz > (unsigned)len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	sz = data.datalen + 5 + 1 + 1 + 1 + 4 + 4;
	if (sz > (unsigned)len)
		return FALSE;

	if (data.datalen > 0)
	{
		switch (data.ftype)
		{

		case FRAME_TYPE_HEARTBEAT:
		{
			StHeartBeat315* lpdata = new StHeartBeat315;
			if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
			{
				data.lpdata = lpdata;
				pos += data.datalen;
			}
			else
			{
				delete lpdata;
				return FALSE;
			}
			break;
		}
		case FRAME_TYPE_DATA:
		{
			StDataBasic* basic = (StDataBasic*)pos;
			if (dir == 1)
			{
				switch (basic->cmdid)
				{
				case CMD_CODE_JDSP:
				{
					StJDSP* lpdata = new StJDSP;
					char* szJdsp = new char[data.datalen - 4 - 1 + 1];
					memset(szJdsp, 0, data.datalen - 4 - 1 + 1);
					memcpy(szJdsp, pos + 1 + 4, data.datalen - 1 - 4);
					lpdata->jdsp = szJdsp;
					lpdata->cmdid = pos[0];
					memcpy(lpdata->res, pos + 1, 4);
					data.lpdata = lpdata;
					pos += data.datalen;
					delete szJdsp;
					break;
				}
				case CMD_CODE_GAPCFG:
				{
					StGapCfgRes* lpdata = new StGapCfgRes;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_GAPVAL:
				{
					StGapValue* lpdata = new StGapValue;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_ALARM_AND_IMG:
				{
					StAlarmAndImgInfo* lpdata = new StAlarmAndImgInfo;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_ACTION_INFO:
				{
					StActionInfo* lpdata = new StActionInfo;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_LASTGAPIMG:
				{
					StLastGapImgRes* lpdata = new StLastGapImgRes;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_IMGLIST:
				{
					StImgListRes* lpdata = new StImgListRes;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_IMGINFO:
				{
					StImgInfoRes* lpdata = new StImgInfoRes;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_VEDIOLIST:
				{
					StVedioListRes* lpdata = new StVedioListRes;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_VEDIOFILE:
				{
					StVedioFileRes* lpdata = new StVedioFileRes;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_YYQX:
				{
					StOilPreCurve* lpdata = new StOilPreCurve;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_YWINFO:
				{
					StOilLevelInfo* lpdata = new StOilLevelInfo;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_REALCTRL:
				{
					StRealCtrlRes* lpdata = new StRealCtrlRes;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_REALSTREAM:
				{
					StRealStream* lpdata = new StRealStream;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_ELECCURVE:
				{
					StElecCurve* lpdata = new StElecCurve;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_POWERLIST:
				{
					StPowerListRes* lpdata = new StPowerListRes;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_POWERCONTENT:
				{
					StPowerInfoRes* lpdata = new StPowerInfoRes;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_NEW_DATA_FILE:
				{
					NewDataFile* lpdata = new NewDataFile;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				case CMD_CODE_DOWNLOAD_DATA_FILE:
				{
					NewDataFileInfo* lpdata = new NewDataFileInfo;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				default:
					break;
				}
			}
			else
			{
				switch (basic->cmdid)
				{
				case CMD_CODE_ELECCURVE:
				{
					StElecCurveRec* lpdata = new StElecCurveRec;
					if (Parse315Protocol::Parse(*lpdata, pos, data.datalen))
					{
						data.lpdata = lpdata;
						pos += data.datalen;
					}
					else
					{
						delete lpdata;
						return FALSE;
					}
					break;
				}
				default:
					break;
				}
			}
			break;
		}
		default:
			break;
		}
	}

	memcpy_s(&data.ftail, 4, pos, 4);
	pos += 4;

	if (data.ftail != 0xFFFFFFFF || pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data, dir);
		return FALSE;
	}

	return TRUE;
}
*/

BOOL Parse315Protocol::Unparse(StFrame& data, vector<BYTE>& buf, int& len, BYTE* cmdBuf, int cmdLen)
{
	//序列化后包的总长度。315头长度 + 命令域长度
	data.datalen = cmdLen;
	int unparselen = data.datalen + 5 + 1 + 1 + 1 + 4 + 4;
	//buf = new BYTE[unparselen];
	//ZeroMemory(buf, unparselen);
	buf.resize(unparselen);

	LPBYTE pos = (LPBYTE)buf.data();

	//拷贝315头信息
	memcpy_s(pos, 5 + 1 + 1 + 1 + 4, &data, 5 + 1 + 1 + 1 + 4);
	pos += 5 + 1 + 1 + 1 + 4;

	if (data.datalen != 0)
	{
		memcpy_s(pos, cmdLen, cmdBuf, cmdLen);
		pos += cmdLen;
	}

	//拷贝尾 4字节 0xFF
	memcpy_s(pos, 4, &data.ftail, 4);
	pos += 4;
	len = unparselen;
	return TRUE;
}



BOOL Parse315Protocol::Unparse(StFrame& data, vector<BYTE>& buf, int& len, int dir)
{
	int unparselen = data.datalen + 5 + 1 + 1 + 1 + 4 + 4;
	//buf = new BYTE[unparselen];
	//ZeroMemory(buf, unparselen);
	buf.resize(unparselen);

	LPBYTE pos = (LPBYTE)buf.data();

	memcpy_s(pos, 5 + 1 + 1 + 1 + 4, &data, 5 + 1 + 1 + 1 + 4);
	pos += 5 + 1 + 1 + 1 + 4;

	if (data.datalen != 0)
	{
		if (data.lpdata == NULL)
		{
			//delete[] buf;
			return FALSE;
		}

		vector<BYTE> subbuf;
		int sublen = 0;
		BOOL success = FALSE;

		switch (data.ftype)
		{
		case FRAME_TYPE_HEARTBEAT:
		{
			success = Parse315Protocol::Unparse(*(StHeartBeat315*)data.lpdata, subbuf, sublen);
			break;
		}
		case FRAME_TYPE_DATA:
		{
			StDataBasic* basic = (StDataBasic*)data.lpdata;
			if (dir == 0)
			{
				if (g_dw0x23ExCmdID > 0 && basic->cmdid == g_dw0x23ExCmdID)
				{
					success = Parse315Protocol::Unparse(*(StDataBasic*)data.lpdata, subbuf, sublen);
				}

				switch (basic->cmdid)
				{
				case CMD_CODE_GAPCFG:
				case CMD_CODE_GAPVAL:
				{
					success = Parse315Protocol::Unparse(*(StDataBasic*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_ALARM_AND_IMG:
				case CMD_CODE_ALARM:
				{
					success = Parse315Protocol::Unparse(*(StAlarmAndImgRec*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_ACTION_INFO:
				{
					success = Parse315Protocol::Unparse(*(StActionInfoRec*)data.lpdata, subbuf, sublen);
					break;
				}
				case 0x51:
				{
					success = Parse315Protocol::Unparse(*(StManualOilingResq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_LASTGAPIMG:
				{
					success = Parse315Protocol::Unparse(*(StLastGapImgReq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_IMGLIST:
				{
					success = Parse315Protocol::Unparse(*(StImgListReq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_IMGINFO:
				{
					success = Parse315Protocol::Unparse(*(StImgInfoReq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_VEDIOLIST:
				{
					success = Parse315Protocol::Unparse(*(StVedioListReq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_VEDIOFILE:
				{
					success = Parse315Protocol::Unparse(*(StVedioFileReq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_QUERY_POWER_FILE:
				{
					success = Parse315Protocol::Unparse(*(StPowerFileListResq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_DOWNLOAD_DATA_FILE:
				{
					success = Parse315Protocol::Unparse(*(StPowerFileDataResq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_1DQJINFO:
				{
					success = Parse315Protocol::Unparse(*(St1DQJInfo*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_YYQX:
				{
					success = Parse315Protocol::Unparse(*(StOilPreCurveRec*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_REALCTRL:
				{
					success = Parse315Protocol::Unparse(*(StRealCtrlReq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_POWERLIST:
				{
					success = Parse315Protocol::Unparse(*(StPowerListReq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_POWERCONTENT:
				{
					success = Parse315Protocol::Unparse(*(StPowerInfoReq*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_DCSCURVE:
				{
					success = Parse315Protocol::Unparse(*(CalPower*)data.lpdata, subbuf, sublen);
					break;
				}
				case CMD_CODE_DBJFBJ:
				{
					success = Parse315Protocol::Unparse(*(DBJFBJInfo*)data.lpdata, subbuf, sublen);
					break;
				}
				case (uint8_t)E_315_PROTOCOL_TYPE::UN_RECOVER_ALARM_0x65:
				{
					success = Parse315Protocol::Unparse(*(StAlarmListReq*)data.lpdata, subbuf, sublen);
					break;
				}
				case (uint8_t)E_315_PROTOCOL_TYPE::OIL_BOX_VOLUME_0x53:
				{
					success = Parse315Protocol::Unparse(*(StOilBoxVolumeResq*)data.lpdata, subbuf, sublen);
					break;
				}
				default:
					break;
				}
			}
		}
		default:
			break;
		}

		if (!success)
		{
			//if (buf)
			//{
			//	delete[] buf;
			//}
			buf.clear();

			return FALSE;
		}

		if (sublen != data.datalen)
		{
			//if (subbuf)
			//{
			//	delete[] subbuf;
			//}
			//if (buf)
			//{
			//	delete[] buf;
			//}
			subbuf.clear();
			buf.clear();

			return FALSE;
		}

		memcpy_s(pos, sublen, subbuf.data(), sublen);
		pos += sublen;

		//if (subbuf)
		//{
		//	delete[] subbuf;
		//}
	}

	memcpy_s(pos, 4, &data.ftail, 4);
	pos += 4;

	len = unparselen;
	return TRUE;
}

BOOL Parse315Protocol::Unparse(StOilBoxVolumeResq& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 4;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}
BOOL Parse315Protocol::Unparse(StAlarmListReq& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 3;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

//释放内存，dir: 数据包方向  0-微机监测->JHD  1-JHD->微机监测
BOOL Parse315Protocol::Release(StFrame& data, int dir)
{
	if (data.datalen != 0 && data.lpdata != NULL)
	{
		switch (data.ftype)
		{
		case FRAME_TYPE_HEARTBEAT:
			break;
		case FRAME_TYPE_DATA:
		{
			StDataBasic* basic = (StDataBasic*)data.lpdata;
			if (g_dw0x23ExCmdID > 0 && basic->cmdid == g_dw0x23ExCmdID)
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StGapCfgRes*)data.lpdata);
				}
			}


			switch (basic->cmdid)
			{
			case CMD_CODE_GAPCFG:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StGapCfgRes*)data.lpdata);
				}
				break;
			}
			case (BYTE)E_315_PROTOCOL_TYPE::OIL_BOX_VOLUME_0x53:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StOilBoxVolumeData*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_GAPVAL:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StGapValue*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_ALARM_AND_IMG:
			case CMD_CODE_ALARM:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StAlarmAndImgInfo*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_ACTION_INFO:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StActionInfo*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_LASTGAPIMG:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StLastGapImgRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_IMGLIST:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StImgListRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_IMGINFO:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StImgInfoRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_VEDIOLIST:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StVedioListRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_VEDIOFILE:
			{
				if (dir != 0)
				{
					if (b2Flen2byte)
					{
						Parse315Protocol::Release((StVedioFileRes2*)data.lpdata);
					}
					else
					{
						Parse315Protocol::Release((StVedioFileRes4*)data.lpdata);
					}
				}
				break;
			}
			case CMD_CODE_YYQX:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StOilPreCurve*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_YWINFO:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StOilLevelInfo*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_REALCTRL:
			{
				if (dir == 0)
				{
					Parse315Protocol::Release(*(StRealCtrlReq*)data.lpdata);
				}
				else
				{
					Parse315Protocol::Release(*(StRealCtrlRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_REALSTREAM:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StRealStream*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_ELECCURVE:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StElecCurve*)data.lpdata);
				}
				break;
			}
			// 			case CMD_CODE_NEW_DATA_FILE:
			// 			{
			// 				if (dir != 0)
			// 				{
			// 					Parse315Protocol::Release(*(StNewPowerNotify*)data.lpdata);
			// 				}
			// 				break;
			// 			}
			case CMD_CODE_QUERY_POWER_FILE:
			{
				if (dir != 0)
				{
					StPowerFileListF2* lpdata = (StPowerFileListF2*)data.lpdata;
					delete[] lpdata->list;
					//delete lpdata;
				}
				break;
			}
			case CMD_CODE_DOWNLOAD_DATA_FILE:
			{
				if (dir != 0)
				{
					StPowerFileData* lpdata = (StPowerFileData*)data.lpdata;
					delete[] lpdata->dataInfo;
					//delete lpdata;
				}
				break;
			}
			case CMD_CODE_POWSTATIC:
			{
				if (dir != 0)
				{
					Parse315Protocol::Release(*(StStaticPowerList*)data.lpdata);
				}
				break;
			}
			default:
				break;
			}
			break;
		}
		default:
			break;
		}

		delete data.lpdata;
	}
	return TRUE;
}

BOOL Parse315Protocol::Release(StAlarmListRes& data)
{
	if (data.cnt == 0 || data.lprecord == NULL)
		return TRUE;

	delete[] data.lprecord;

	return TRUE;
}

BYTE Parse315Protocol::GetCommandId(const StFrame& data)
{
	if (data.datalen != 0 && data.lpdata != NULL)
	{
		switch (data.ftype)
		{
		case FRAME_TYPE_HEARTBEAT:
			break;
		case FRAME_TYPE_DATA:
		{
			StDataBasic* basic = (StDataBasic*)data.lpdata;
			return basic->cmdid;
		}
		}
	}

	return 0;
}

//buf:缓冲区   size:缓冲区大小   len:取出的数据长度
BOOL Parse315Protocol::GetFrameData(LPVOID& buf, int& size, int& len)
{
	int nMinLen = 5 + 1 + 1 + 1 + 4 + 4;

	if (buf == NULL || size < nMinLen)
		return FALSE;

	LPBYTE pos = (LPBYTE)buf;

	int cnt = size;
	while (cnt > 0)
	{
		if (*pos == 'q')
		{
			if (cnt >= nMinLen
				&& memcmp(pos, FRAME_HEADER_315, 5) == 0)
			{
				break;
			}
		}

		++pos;
		--cnt;
	}

	if (cnt < nMinLen)
		return FALSE;

	int datalen = *(int*)(pos + 5 + 1 + 1 + 1);
	if (datalen == 0)
	{
		//TRACE("报文长度为0\n");
		datalen = size - nMinLen;
	}
	if (datalen < 0 || cnt < nMinLen + datalen)
		return FALSE;

	DWORD tail = *(DWORD*)(pos + 5 + 1 + 1 + 1 + 4 + datalen);
	if (tail != FRAME_TAIL_315)
		return FALSE;

	buf = pos;
	size = cnt;
	len = nMinLen + datalen;

	return TRUE;
}

//心跳包 时间4B+0xFF+0xFF+0xFF
BOOL Parse315Protocol::Parse(StHeartBeat315& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	if (len != 4 + 3)
		return FALSE;

	memcpy_s(&data, 4 + 3, buf, 4 + 3);

	return TRUE;
}

BOOL Parse315Protocol::Unparse(StHeartBeat315& data, vector<BYTE>& buf, int& len)
{
	len = 4 + 3;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL Parse315Protocol::Unparse(StDataBasic& data, vector<BYTE>& buf, int& len)
{
	len = 1;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}


BOOL Parse315Protocol::Parse(StOilBoxVolumeData& data, LPVOID buf, int len, FRAME_KIND& kind)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;
	BOOL failed = FALSE;

	data.cmdid = *pos;
	//判断包的类型 命令包还是应答包
	if (7 == len && data.cmdid == (BYTE)E_315_PROTOCOL_TYPE::OIL_BOX_VOLUME_0x53)
	{
		pos += 5;
		data.v = *(WORD*)pos;
		pos + 2;
		kind = CMD;
		return TRUE;
	}
	//应答包：帧头5B + 协议码1B + 数据版本1B + 帧类型1B + 帧内容长度4B + 命令码1B（0x23） + 总配置数(2B) + {转辙机配置i} + 帧尾4B
	//转辙机配置i:......
	kind = RES;
	size_t sz;
	{
		sz = 1 + 2;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		memcpy_s(&data, sz, pos, sz);
		pos += sz;

		if (data.v * sizeof(StOilBoxVolume) > len)
			return FALSE;

		if (data.v != 0)
		{
			sz = sizeof(StOilBoxVolume) * data.v;
			StOilBoxVolume* lpcfg = new StOilBoxVolume[data.v];
			ZeroMemory(lpcfg, sz);
			CopyMemory(lpcfg, pos, sz);

			data.dataInfo = lpcfg;

			pos += sz;
		}
	}

	if (failed || pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}

	return TRUE;
}



//道岔缺口配置信息 buf指向数据内容部分 len数据内容的长度
BOOL Parse315Protocol::Parse(StGapCfgRes& data, LPVOID buf, int len, FRAME_KIND& kind)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;
	BOOL failed = FALSE;

	data.cmdid = *pos;
	//判断包的类型 命令包还是应答包
	if (1 == len && (*pos == 0x23
		|| g_dw0x23ExCmdID > 0 && *pos == g_dw0x23ExCmdID))
	{
		pos += 1;
		kind = CMD;
		return TRUE;
	}
	//应答包：帧头5B + 协议码1B + 数据版本1B + 帧类型1B + 帧内容长度4B + 命令码1B（0x23） + 总配置数(2B) + {转辙机配置i} + 帧尾4B
	//转辙机配置i:......
	kind = RES;
	size_t sz;
	if (g_dw0x23ExCmdID > 0 && *pos == g_dw0x23ExCmdID)
	{
		sz = 1 + 4 + 4; // cmdid + 预留(4B) + 文件长度
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.cfgcnt = 1;//1个文件
		//memcpy_s(&data, sz, pos, sz);
		DWORD dwFileLen = *(DWORD*)(pos + 5);
		BYTE* cbBuffer = new BYTE[dwFileLen];
		pos += sz;
		memcpy_s(cbBuffer, dwFileLen, pos, dwFileLen);
		data.lpcfg = cbBuffer;
		pos += dwFileLen;
	}
	else
	{
		sz = 1 + 2;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		memcpy_s(&data, sz, pos, sz);
		pos += sz;

		if (data.cfgcnt > len)
			return FALSE;

		if (data.cfgcnt != 0)
		{
			sz = sizeof(StSwitchCfg) * data.cfgcnt;
			StSwitchCfg* lpcfg = new StSwitchCfg[data.cfgcnt];
			ZeroMemory(lpcfg, sz);

			for (int i = 0; i < data.cfgcnt; ++i)
			{
				StSwitchCfg& cfg = lpcfg[i];

				sz = 1;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = TRUE;
					break;
				}
				memcpy_s(&cfg.nlen, sz, pos, sz);
				pos += sz;

				if (cfg.nlen != 0)
				{
					sz = cfg.nlen;
					szcnt += sz;
					if (szcnt > len)
					{
						failed = TRUE;
						break;
					}
					cfg.lpname = new CHAR[sz];
					memcpy_s(cfg.lpname, sz, pos, sz);
					pos += sz;
				}

				sz = 1;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = TRUE;
					break;
				}
				memcpy_s(&cfg.stype, sz, pos, sz);
				pos += sz;

				sz = 2;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = TRUE;
					break;
				}
				memcpy_s(&cfg.sid, sz, pos, sz);
				pos += sz;

				sz = 2;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = TRUE;
					break;
				}
				memcpy_s(&cfg.cnt, sz, pos, sz);
				pos += sz;

				if (cfg.cnt != 0)
				{
					sz = 2 * cfg.cnt;
					szcnt += sz;
					if (szcnt > len)
					{
						failed = TRUE;
						break;
					}
					cfg.lpinfo = new WORD[cfg.cnt];
					memcpy_s(cfg.lpinfo, sz, pos, sz);
					pos += sz;
				}
			}//for

			data.lpcfg = lpcfg;
		}
	}

	if (failed || pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}

	return TRUE;
}
BOOL Parse315Protocol::Release(StOilBoxVolumeData& data)
{
	if (data.v == 0 || data.dataInfo == NULL)
		return TRUE;


	delete[] data.dataInfo;

	return TRUE;
}

BOOL Parse315Protocol::Release(StGapCfgRes& data)
{
	if (data.cfgcnt == 0 || data.lpcfg == NULL)
		return TRUE;

	if (data.cmdid == CMD_CODE_GAPCFG)
	{
		for (int i = 0; i < data.cfgcnt; ++i)
		{
			StSwitchCfg& cfg = ((StSwitchCfg*)data.lpcfg)[i];

			if (cfg.nlen && cfg.lpname)
				delete[] cfg.lpname;

			if (cfg.cnt && cfg.lpinfo)
				delete[] cfg.lpinfo;
		}
	}

	delete[] data.lpcfg;

	return TRUE;
}

//道岔缺口数值 buf指向数据内容部分 len数据内容的长度
BOOL Parse315Protocol::Parse(StGapValue& data, LPVOID buf, int len, FRAME_KIND& kind)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	//判断包的类型 命令包还是应答包
	if (1 == len && *pos == 0x26) {
		data.cmdid = *pos;
		pos += 1;
		kind = CMD;
		return TRUE;
	}
	//应答包：命令码1B（0x26） + 总记录数(2B)+ {数据记录i}
	//数据记录i:......
	kind = RES;

	size_t sz = 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StGapRecord);
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpdata = new BYTE[sz];
		memcpy_s(data.lpdata, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		return Parse_4res(data, buf, len); //尝试按照4字节尾部解析，通号对接每个记录尾部4字节
	}

	return TRUE;
}


BOOL Parse315Protocol::Parse_4res(StGapValue& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StGapRecord_4res);
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpdata = new BYTE[sz];
		memcpy_s(data.lpdata, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}

	return TRUE;
}


BOOL Parse315Protocol::Release(StGapValue& data)
{
	if (data.cnt == 0 || data.lpdata == NULL)
		return TRUE;

	delete[] data.lpdata;

	return TRUE;
}

//缺口报警,预警,以及图像/视频信息  buf指向数据内容部分 len数据内容的长度
//命令码1B（0x27）+转辙机ID(2B) + 报警时间(4B) + 报警确认信号(4B, 用于监测回执) + 定反位(1B) + 左右偏标志(1B) + 报警类型(1B)
// + 偏移值(2B) + 缺口值(2B) + 标准值(2B) + 预留(4B, 填充4个0xFF) + 图像 / 视频长度(4B) + 图像 / 视频内容
BOOL Parse315Protocol::Parse(StAlarmAndImgInfo& data, LPVOID buf, int len, FRAME_KIND& kind)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
	{
		// 发送时，没有发送后半段
		if (szcnt - 4 > len)
			return FALSE;

		memcpy_s(&data, len, pos, len);
		pos += len;
	}
	else
	{
		memcpy_s(&data, sz, pos, sz);
		pos += sz;
	}


	if (data.imglen != 0)
	{
		sz = data.imglen;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		if (sz < 100)
		{
			data.lpimg = new BYTE[sz];
			memcpy_s(data.lpimg, sz, pos, sz);
		}
		else data.lpimg = NULL;
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(StAlarmAndImgRec& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL Parse315Protocol::Release(StAlarmAndImgInfo& data)
{
	if (data.imglen == 0 || data.lpimg == NULL)
		return TRUE;

	delete[] data.lpimg;

	return TRUE;
}

//道岔动作后模拟量及缺口图像信息  命令码：0x28 （卡斯科）
BOOL Parse315Protocol::Parse(StActionInfo& data, LPVOID buf, int len, FRAME_KIND& kind)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	//判断包的类型 上送还是回执
	if (sz == len && *pos == 0x27) {
		data.cmdid = *pos;
		pos += 1;
		kind = BACK;
		return TRUE;
	}
	//回执包
	kind = UP_SEND;

	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.imglen != 0)
	{
		sz = data.imglen;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpimg = new BYTE[sz];
		memcpy_s(data.lpimg, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(StActionInfoRec& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL Parse315Protocol::Release(StActionInfo& data)
{
	if (data.imglen == 0 || data.lpimg == NULL)
		return TRUE;

	delete[] data.lpimg;

	return TRUE;
}

BOOL Parse315Protocol::Unparse(StManualOilingResq& data, vector<BYTE>& buf, int& len)
{
	len = sizeof(StManualOilingResq);
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}


//道岔缺口最新图像0x29
BOOL Parse315Protocol::Parse(StLastGapImgRes& data, LPVOID buf, int len, FRAME_KIND& kind)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	//判断包的类型 命令包还是应答包
	//命令包 命令码1B（0x29） + 转辙机ID(2B) + 预留(4B, 填充4个0xFF)
	if (7 == len && *pos == 0x29) {
		memcpy_s(&data, 7, pos, 7);
		pos += 7;
		kind = CMD;
		return TRUE;
	}
	//应答包
	kind = RES;

	size_t sz = 1 + 2 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.imglen != 0)
	{
		sz = data.imglen;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpimg = new BYTE[sz];
		memcpy_s(data.lpimg, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(StLastGapImgReq& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 4;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}


BOOL Parse315Protocol::Release(StLastGapImgRes& data)
{
	if (data.imglen == 0 || data.lpimg == NULL)
		return TRUE;

	delete[] data.lpimg;

	return TRUE;
}


BOOL Parse315Protocol::ParseReq(StImgListReq& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 4 + 1 + 3;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::ReleaseReq(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Parse(StImgListRes& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 2 + 4;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StFileRecord);
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lprecord = new BYTE[sz];
		memcpy_s(data.lprecord, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(StImgListReq& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 4 + 4 + 1 + 3;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL Parse315Protocol::ReleaseReq(StImgListReq& data)
{
	return TRUE;
}

BOOL Parse315Protocol::Release(StImgListRes& data)
{
	if (data.cnt == 0 || data.lprecord == NULL)
		return TRUE;

	delete[] data.lprecord;

	return TRUE;
}


BOOL Parse315Protocol::ParseReq(StImgInfoReq& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 1 + 3;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (pos != (LPBYTE)buf + len)
	{
		return FALSE;
	}
	return TRUE;
}
BOOL Parse315Protocol::Parse(StVibrationCurveRes& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));
	if (sizeof(data) >= len) return FALSE;

	CopyMemory(&data, (BYTE*)buf, sizeof(data));
	return TRUE;
}

BOOL Parse315Protocol::Parse(StWorkingConditionValRes& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));
	if (sizeof(data) != len) return FALSE;

	CopyMemory(&data, (BYTE*)buf, sizeof(data));
	return TRUE;
}

BOOL Parse315Protocol::Parse(StOpWorkingConditionRes& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));
	if (sizeof(data) != len) return FALSE;

	CopyMemory(&data, (BYTE*)buf, sizeof(data));
	return TRUE;
}

BOOL Parse315Protocol::Parse(StImgInfoReq& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 1 + 3;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (pos != (LPBYTE)buf + len)
	{
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Parse(StImgInfoRes& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 1 + 3 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.imglen != 0)
	{
		sz = data.imglen;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpimg = new BYTE[sz];
		memcpy_s(data.lpimg, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(StImgInfoReq& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 4 + 1 + 3;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL Parse315Protocol::Release(StImgInfoRes& data)
{
	if (data.imglen == 0 || data.lpimg == NULL)
		return TRUE;

	delete[] data.lpimg;

	return TRUE;
}

BOOL Parse315Protocol::ParseReq(StVedioListReq& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 1 + 2 + 4 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (pos != (LPBYTE)buf + len)
	{
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Parse(StVedioListRes& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 1 + 2 + 4 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StVedioRecord);
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lprecord = new BYTE[sz];
		memcpy_s(data.lprecord, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(StVedioListReq& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 1 + 2 + 4 + 4 + 4;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL Parse315Protocol::Release(StVedioListRes& data)
{
	if (data.cnt == 0 || data.lprecord == NULL)
		return TRUE;

	delete[] data.lprecord;

	return TRUE;
}

/*0x2F 视频文件 begin */
BOOL Parse315Protocol::ParseReq(StVedioFileReq& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 1 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (pos != (LPBYTE)buf + len)
	{
		return FALSE;
	}
	return TRUE;
}

template<typename T>
BOOL Parse315Protocol::Parse(T* data, LPVOID buf, int len)
{
	//ZeroMemory(data, sizeof(StVedioFileResHead));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 1 + 2 + 4 + 1 + 1 + 2 + 4 + 4 + 2 + 2 + sizeof(T::datalen);
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(data, sz, pos, sz);
	pos += sz;

	if (data->datalen != 0)
	{
		sz = data->datalen;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data->lpdata = new BYTE[sz];
		memcpy_s(data->lpdata, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(StVedioFileReq& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 1 + 2 + 4 + 4;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

template<typename T>
BOOL Parse315Protocol::Release(T* data)
{
	return FALSE;

	if (data->datalen == 0 || data->lpdata == NULL)
		return TRUE;

	data->lpdata;

	return TRUE;
}
/*0x2F 视频文件 end */

/*0xF2 查询阻力文件 begin */
BOOL Parse315Protocol::Unparse(StPowerFileListResq& data, vector<BYTE>& buf, int& len)
{
	len = sizeof(StPowerFileListResq);
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;

}
BOOL Parse315Protocol::Release(StPowerFileListResq& data)
{
	return FALSE;
}

/*0xF2 查询阻力文件 end */

/*0xF3 查询阻力文件 begin */
BOOL Parse315Protocol::Unparse(StPowerFileDataResq& data, vector<BYTE>& buf, int& len)
{
	len = sizeof(StPowerFileDataResq);
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;

}
BOOL Parse315Protocol::Release(StPowerFileDataResq& data)
{
	return FALSE;
}

/*0xF2 查询阻力文件 end */


/*0x22 1DQJ 道岔区段 状态信息 begin*/
BOOL Parse315Protocol::ParseReq(St1DQJInfo& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 1 + 4;
	if (sz > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (pos != (LPBYTE)buf + len)
	{
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(St1DQJInfo& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 1 + 4;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}
/*0x22 1DQJ 道岔区段 状态信息 end*/

BOOL Parse315Protocol::Parse(StOilPreCurve& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 1 + 1 + 4 + 1;//14字节的非曲线数据长
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.cnt > len)
		return FALSE;

	BOOL failed = FALSE;
	if (data.cnt != 0)
	{
		//1条曲线数据的头部信息
		sz = data.cnt * sizeof(StCurve);
		data.lpdata = new BYTE[sz];
		ZeroMemory(data.lpdata, sz);

		for (int i = 0; i < data.cnt; ++i)
		{
			StCurve& curv = ((StCurve*)data.lpdata)[i];

			sz = 1 + 4 + 2;
			szcnt += sz;
			if (szcnt > len)
			{
				failed = TRUE;
				break;
			}
			memcpy_s(&curv, sz, pos, sz);
			pos += sz;

			if (curv.len != 0)
			{
				sz = curv.len;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = TRUE;
					break;
				}
				curv.lpdata = new BYTE[sz];
				memcpy_s(curv.lpdata, sz, pos, sz);
				pos += sz;
			}
		}
	}

	if (failed || pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}

	return TRUE;
}


string Parse315Protocol::GetStrFromData(BYTE* buf, int dwLen)
{
	int dwLoop;
	string strReturn = "";

	char tmpch[4] = { 0 };

	BYTE bval = 0;
	for (dwLoop = 0; dwLoop < dwLen; dwLoop++)
	{
		bval = *(buf + dwLoop);
		if (dwLoop == 0)
		{
			sprintf_s(tmpch, sizeof(tmpch), "%02x ", bval);
			strReturn += tmpch;
		}
		else
		{
			sprintf_s(tmpch, sizeof(tmpch), "%02x ", bval);
			strReturn += tmpch;
		}
	}

	return strReturn;
}

BOOL Parse315Protocol::Unparse(StOilPreCurveRec& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 4 + 1 + 1 + 4 + 1;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL Parse315Protocol::Release(StOilPreCurve& data)
{
	if (data.cnt == 0 || data.lpdata == NULL)
		return TRUE;

	for (int i = 0; i < data.cnt; ++i)
	{
		StCurve& curv = ((StCurve*)data.lpdata)[i];

		if (curv.len && curv.lpdata)
			delete[] curv.lpdata;
	}

	delete[] data.lpdata;

	return TRUE;
}

BOOL Parse315Protocol::Parse(StOilLevelInfo& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StSdataRecord);
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpdata = new BYTE[sz];
		memcpy_s(data.lpdata, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Release(StOilLevelInfo& data)
{
	if (data.cnt == 0 || data.lpdata == NULL)
		return TRUE;

	delete[] data.lpdata;

	return TRUE;
}

/*0x30 实时视频流信息 begin*/
BOOL Parse315Protocol::ParseReq(StRealCtrlReq& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 1 + 1;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	switch (data.cmdtype)
	{
	case 0x01://请求码流
	{
		sz = 1 + 2 + 2 + 2;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpdata = new BYTE[sz];
		memcpy_s(data.lpdata, sz, pos, sz);
		pos += sz;
		break;
	}
	case 0x02:
		break;
	case 0x04:
		break;
	case 0x05:
	{
		sz = 1;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpdata = new BYTE[sz];
		memcpy_s(data.lpdata, sz, pos, sz);
		pos += sz;
		break;
	}
	case 0x06:
	{
		sz = 2 + 2;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpdata = new BYTE[sz];
		memcpy_s(data.lpdata, sz, pos, sz);
		pos += sz;
		break;
	}
	default:
		break;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Parse(StRealCtrlRes& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 1;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	switch (data.result)
	{
	case 0x00:
	{
		sz = 1;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpdata = new BYTE[sz];
		memcpy_s(data.lpdata, sz, pos, sz);
		pos += sz;
		break;
	}
	case 0x01:
		break;
	case 0x02:
	{
		sz = 4;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpdata = new BYTE[sz];
		memcpy_s(data.lpdata, sz, pos, sz);
		pos += sz;
		break;
	}
	default:
		break;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(StRealCtrlReq& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 1 + 1;

	switch (data.cmdtype)
	{
	case 0x01:
		len += 1 + 2 + 2 + 2;
		break;
	case 0x02:
	case 0x04:
		break;
	case 0x05:
		len += 1;
		break;
	case 0x06:
		len += 2 + 2;
		break;
	default:
		break;
	}

	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);


	LPBYTE pos = (LPBYTE)buf.data();

	size_t sz = 1 + 2 + 1 + 1;
	memcpy_s(pos, sz, &data, sz);
	pos += sz;

	sz = len - sz;
	if (sz)
	{
		memcpy_s(pos, sz, data.lpdata, sz);
		pos += sz;
	}

	return TRUE;
}

BOOL Parse315Protocol::Release(StRealCtrlReq& data)
{
	size_t sz = 0;
	switch (data.cmdtype)
	{
	case 0x01:
		sz = 1 + 2 + 2 + 2;
		break;
	case 0x02:
	case 0x04:
		break;
	case 0x05:
		sz = 1;
		break;
	case 0x06:
		sz = 2 + 2;
		break;
	default:
		break;
	}

	if (sz == 0 || data.lpdata == NULL)
		return TRUE;

	delete[] data.lpdata;

	return TRUE;
}

BOOL Parse315Protocol::Release(StRealCtrlRes& data)
{
	size_t sz = 0;
	switch (data.result)
	{
	case 0x00:
		sz = 1;
		break;
	case 0x01:
		break;
	case 0x02:
		sz = 4;
		break;
	default:
		break;
	}

	if (sz == 0 || data.lpdata == NULL)
		return TRUE;

	delete[] data.lpdata;

	return TRUE;
}
/*0x30 实时视频流信息  end*/

/*0x31 实时码流  begin*/
BOOL Parse315Protocol::Parse(StRealStream& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.packlen != 0)
	{
		sz = data.packlen;
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lpdata = new BYTE[sz];
		memcpy_s(data.lpdata, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Release(StRealStream& data)
{
	if (data.packlen == 0 || data.lpdata == NULL)
		return TRUE;

	delete[] data.lpdata;

	return TRUE;
}
/*0x31 实时码流  end*/
BOOL Parse315Protocol::Parse(StPowerListReq& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 4 + 1;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (pos != (LPBYTE)buf + len)
	{
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Parse(StPowerListRes& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StPowerRecord);
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lprecord = new BYTE[sz];
		memcpy_s(data.lprecord, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(StPowerListReq& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 4 + 4 + 1;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL Parse315Protocol::Release(StPowerListRes& data)
{
	if (data.cnt == 0 || data.lprecord == NULL)
		return TRUE;

	delete[] data.lprecord;

	return TRUE;
}


BOOL Parse315Protocol::Parse(StPowerInfoRes& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 1 + 1 + 4 + 4 + 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	int n0 = memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.cnt == 0x01)//直接阻力
	{
		data.lprecord = new BYTE[sizeof(StPowerCurveInfo)];
		sz = 1 + 4 + 2;
		szcnt += sz;
		n0 = memcpy_s(data.lprecord, sz, pos, sz);
		pos += sz;

		StPowerCurveInfo* pCurveInfo = (StPowerCurveInfo*)(data.lprecord);

		if (pCurveInfo->r[0] != 0x01 && pCurveInfo->r[0] == 0xFF) {
			Parse315Protocol::Release(data);
			return FALSE;
		}
		if (!pCurveInfo || pCurveInfo->curvelen == 0) {
			Parse315Protocol::Release(data);
			return FALSE;
		}
		szcnt += pCurveInfo->curvelen;
		if (pCurveInfo->r[0] == 0x01) {
			szcnt += 14; //扩展配置
		}
		if (szcnt != len)
		{
			Parse315Protocol::Release(data);
			return FALSE;
		}

		if (pCurveInfo->r[0] == 0x01) {
			pCurveInfo->lpcurvedata = new BYTE[pCurveInfo->curvelen + 14];  //Release 还得加上这里的释放
		}
		else {
			pCurveInfo->lpcurvedata = new BYTE[pCurveInfo->curvelen];
		}
		n0 = memcpy_s(pCurveInfo->lpcurvedata, len - 20 - 7, pos, len - 20 - 7);

		return TRUE;
	}
	return FALSE;
}

// BOOL Parse315Protocol::Parse(StNewPowerNotify& data, LPVOID buf, int len)
// {
// 	ZeroMemory(&data, sizeof(data));
// 
// 	int szcnt = 0;
// 	LPBYTE pos = (LPBYTE)buf;
// 
// 	size_t sz = sizeof(StNewPowerNotify);//1 + 2 + 2 + 4 + 4 + 4 + 4 + 1;
// 	szcnt += sz;
// 	if (szcnt > len)
// 		return FALSE;
// 	memcpy_s(&data, sz, pos, sz);
// 
// 
// 	return TRUE;
// }
BOOL Parse315Protocol::Parse(NewDataFile& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 2 + 4;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);


	return TRUE;
}
BOOL Parse315Protocol::Parse(StPowerFileData& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;
	size_t sz = sizeof(StPowerFileData) - 4;

	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	data.dataInfo = new BYTE[data.fileLen];
	memcpy_s(data.dataInfo, data.fileLen, pos, data.fileLen);


	return TRUE;
}

BOOL Parse315Protocol::Unparse(StPowerInfoReq& data, vector<BYTE>& buf, int& len)
{
	len = 1 + 2 + 4 + 1;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL Parse315Protocol::Unparse(StPowerFileData& data, vector<BYTE>& buf, int& len)
{
	if (len == 9)
	{
		//buf = new BYTE[len];
//ZeroMemory(buf, len);
		buf.resize(len);

		memcpy_s(buf.data(), len, &data, len);

		//buf = new BYTE[len];
		//ZeroMemory(buf, len);
		////StNewPowerNotify newData;
		////newData.cmdid = data.cmdid;
		////newData.sid = data.sid;
		////newData.acqObjType = data.acqObjType;
		////newData.time = data.time;
		////memcpy_s(buf, sizeof(StNewPowerNotify), &newData, sizeof(StNewPowerNotify));
		//CopyMemory(buf, &data, sizeof(StPowerFileData));
	}
	else
	{

	}


	return TRUE;
}

BOOL Parse315Protocol::Unparse(CalPower& data, vector<BYTE>& buf, int& len)
{
	len = sizeof(CalPower);
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);

	memcpy_s(buf.data(), len, &data, len);

	return TRUE;
}

BOOL Parse315Protocol::Parse(DBJFBJInfo& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 4 + 4 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.dataCount != 0)
	{
		sz = data.dataCount * sizeof(ZZJIDAndStatus);
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.curveInfoList = new BYTE[sz];
		memcpy_s(data.curveInfoList, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Unparse(DBJFBJInfo& data, vector<BYTE>& buf, int& len)
{
	len = 3 * data.dataCount + 11;
	//buf = new BYTE[len];
	//ZeroMemory(buf, len);
	buf.resize(len);


	memcpy_s(buf.data(), 11, &data, 11);
	memcpy_s(buf.data() + 11, 3 * data.dataCount, data.curveInfoList, 3 * data.dataCount);

	//ZeroMemory(buf, len);
	//memcpy_s(buf, len, bufTmp, len);

	return TRUE;
}

BOOL Parse315Protocol::Release(DBJFBJInfo& data)
{
	if (data.dataCount == 0 || data.curveInfoList == NULL)
		return TRUE;

	delete[] data.curveInfoList;

	return TRUE;
}

tstring Parse315Protocol::ToString(const DBJFBJInfo& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机个数:%d"), data.dataCount);
	strRst += buf;

	strRst += "\r\n";

	for (int i = 0; i < data.dataCount; ++i)
	{
		ZZJIDAndStatus& srec = ((ZZJIDAndStatus*)data.curveInfoList)[i];

		strRst += _T("，");
		_stprintf_s(buf, _T("转辙机数据记录%d{"), i);
		strRst += buf;

		_stprintf_s(buf, _T("转辙机ID:%d"), srec.zzjID);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("定反位:%d"), srec.ZZJStatus);
		strRst += buf;

		strRst += _T("}");

		strRst += "\r\n";
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StAlarmListReq& data)
{
	tstring strRst = _T("");

	strRst += _T("转辙机ID:");
	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("%d"), data.cmdid);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StAlarmListRes& data)
{
	tstring strRst = _T("");
	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("数目:%d，\n"), data.cnt);
	strRst += buf;

	S_315_ALARM_INFO* pRecord = (S_315_ALARM_INFO*)data.lprecord;
	for (WORD i = 0; i < data.cnt; i++)
	{
		//CTime tm = pRecord[i].time;
		TIME tm = timeopt::Unix2SysTime(pRecord[i].time);
		_stprintf_s(buf, _T("[%d] 报警时间:%04d-%02d-%02d %02d:%02d:%02d, "), i + 1,
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		_stprintf_s(buf, _T("报警类型:%d, "), pRecord[i].alarm);
		strRst += buf;

		_stprintf_s(buf, _T("定反位:%d, "), pRecord[i].fixinvert);
		strRst += buf;

		_stprintf_s(buf, _T("缺口值:%d, "), pRecord[i].gap);
		strRst += buf;

		_stprintf_s(buf, _T("采集类型:%d\n"), pRecord[i].acqtype);
		strRst += buf;

	}
	return strRst;
}

BOOL Parse315Protocol::Release(StPowerInfoRes& data)
{
	if (data.cnt == 0 || data.lprecord == NULL)
		return TRUE;

	StPowerCurveInfo* p1 = (StPowerCurveInfo*)data.lprecord;
	if (p1->lpcurvedata != NULL) {
		delete[] p1->lpcurvedata;
		p1->lpcurvedata = NULL;
	}
	delete[] data.lprecord;

	return TRUE;
}

tstring Parse315Protocol::ToString(const StFrame& data, int dir)
{
	if (data.lpdata == nullptr) return "";
	tstring strRst = _T("");

	if (data.datalen != 0 && data.lpdata != NULL)
	{
		switch (data.ftype)
		{
		case FRAME_TYPE_HEARTBEAT:
			strRst += _T("心跳");
			strRst += _T("，");
			strRst += Parse315Protocol::ToString(*(StHeartBeat315*)data.lpdata);
			break;
		case FRAME_TYPE_DATA:
		{
			StDataBasic* basic = (StDataBasic*)data.lpdata;
			if (g_dw0x23ExCmdID > 0 && basic->cmdid == g_dw0x23ExCmdID)
			{
				if (data.e_frmKind == CMD)
				{
					strRst += _T("缺口扩展配置命令包");
				}
				else if (data.e_frmKind == RES)
				{
					strRst += _T("缺口扩展配置应答包");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StGapCfgRes*)data.lpdata);
				}
			}

			switch (basic->cmdid)
			{
			case CMD_CODE_JDSP:
			{
				strRst += _T("JDSP:\r\n");
				StJDSP* p = (StJDSP*)data.lpdata;

				if (p)
					strRst += p->jdsp.c_str();
				break;
			}
			case CMD_CODE_GAPCFG://0x23
			{
				if (data.e_frmKind == CMD)
				{
					strRst += _T("缺口配置命令包");
				}
				else if (data.e_frmKind == RES)
				{
					strRst += _T("缺口配置应答包");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StGapCfgRes*)data.lpdata);
				}
				break;
			}
			case (BYTE)E_315_PROTOCOL_TYPE::OIL_BOX_VOLUME_0x53:
			{
				TCHAR buf[256];
				if (data.e_frmKind == CMD)
				{
					_stprintf_s(buf, _T("加油箱储油量命令包:转辙机:%d"), ((StOilBoxVolumeData*)data.lpdata)->v);

					strRst += buf;
				}
				else if (data.e_frmKind == RES)
				{
					strRst += _T("加油箱储油量应答包:\n");
					//strRst += Parse315Protocol::ToString(*(StGapCfgRes*)data.lpdata);
					StOilBoxVolume* pIt = (StOilBoxVolume*)((StOilBoxVolumeData*)data.lpdata)->dataInfo;
					for (WORD k = 0; k < ((StOilBoxVolumeData*)data.lpdata)->v; k++)
					{
						if (pIt[k].value == 0xFF)
						{
							_stprintf_s(buf, _T("转辙机:%d, 无加油设备\n"), pIt[k].sid);
						}
						else if (pIt[k].value == 0xFFFF)
						{
							_stprintf_s(buf, _T("转辙机:%d, 设备不在线\n"), pIt[k].sid);
						}
						else if (pIt[k].value == 0)
						{
							_stprintf_s(buf, _T("转辙机:%d, 油位过低\n"), pIt[k].sid);
						}
						else
						{
							_stprintf_s(buf, _T("转辙机:%d, 储油量:%d\n"), pIt[k].sid, pIt[k].value);
						}
						strRst += buf;
					}
				}
				break;
			}
			case (uint8_t)E_315_PROTOCOL_TYPE::GONGKUANG_REAL_VAL_0x81:
			{
				StWorkingConditionValRes* lpsubdata = (StWorkingConditionValRes*)data.lpdata;
				strRst += "工况参数实时值\n";
				//time_t tm = lpsubdata->time;
				TIME tm = timeopt::Unix2SysTime(lpsubdata->time);
				TCHAR buf[256];
				_stprintf_s(buf, _T("转辙机:%d\n采集时间:%04d-%02d-%02d %02d:%02d:%02d\n值类型:%d\n值:%d.%d"),
					lpsubdata->zzjid, tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond,
					lpsubdata->valType, lpsubdata->value / 10, lpsubdata->value % 10);
				strRst += buf;
				break;
			}
			case (uint8_t)E_315_PROTOCOL_TYPE::GONGKUANG_INIT_VALUE_0x82:
			{
				StOpWorkingConditionRes* lpsubdata = (StOpWorkingConditionRes*)data.lpdata;
				strRst += "工况操作结果\n";
				const TCHAR s_szValueType[][32] = { "定位轨距","定位密贴","定位爬行","定位基本轨横移","反位轨距","反位密贴","反位爬行","反位基本轨横移" };
				//CTime tm = lpsubdata->time;
				TIME tm = timeopt::Unix2SysTime(lpsubdata->time);
				TCHAR buf[256];
				_stprintf_s(buf, _T("转辙机:%d\n时间:%04d-%02d-%02d %02d:%02d:%02d\n动作类型:%s\n值类型:%s\nK值:%f\nB值:%f\n返回结果:%d"),
					lpsubdata->zzjid, tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond,
					lpsubdata->actionType == 0 ? "获取" : "设置", lpsubdata->valType > 8 ? "无效值" : s_szValueType[lpsubdata->valType - 1],
					lpsubdata->valueK * 1.0 / 1000000, lpsubdata->valueB * 1.0 / 1000000, lpsubdata->result);
				strRst += buf;
				break;
			}
			case (uint8_t)E_315_PROTOCOL_TYPE::GONGKUANG_ZD_CURVE_0x83:
			{
				StVibrationCurveRes* lpsubdata = (StVibrationCurveRes*)data.lpdata;
				strRst += "振动曲线更新 \n";
				//CTime tm = lpsubdata->time;
				TIME tm = timeopt::Unix2SysTime(lpsubdata->time);
				TCHAR buf[256];
				_stprintf_s(buf, _T("转辙机:%d\n采集时间:%04d-%02d-%02d %02d:%02d:%02d\n曲线类型:%d\n采样间隔:%d\n曲线数据长度:%d"),
					lpsubdata->zzjid, tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond,
					lpsubdata->curveType, lpsubdata->acpfreq, lpsubdata->curveLen);
				strRst += buf;

				break;
			}
			case CMD_CODE_GAPVAL://0x26
			{
				if (data.e_frmKind == CMD)//(dir == 0)
				{
					strRst += _T("缺口值命令包");
				}
				else if (data.e_frmKind == RES)
				{
					strRst += _T("缺口值应答包");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StGapValue*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_ALARM_AND_IMG://0x27
			case CMD_CODE_ALARM://0x97
			{
				if (dir == 0)
				{
					strRst += _T("报警/预警信息(缺口图像信息)回执");
					strRst += _T("，");
					if (basic->cmdid == CMD_CODE_ALARM_AND_IMG)//0x27
						strRst += Parse315Protocol::ToString0x27(*(StAlarmAndImgRec*)data.lpdata);
					else if (basic->cmdid == CMD_CODE_ALARM)//0x97
						strRst += Parse315Protocol::ToString0x97(*(StAlarmAndImgRec*)data.lpdata);
				}
				else
				{
					strRst += _T("报警/预警信息(缺口图像信息)");
					strRst += _T("，");
					if (basic->cmdid == CMD_CODE_ALARM_AND_IMG)//0x27
						strRst += Parse315Protocol::ToString(*(StAlarmAndImgInfo*)data.lpdata);
					else if (basic->cmdid == CMD_CODE_ALARM)//0x97
						strRst += Parse315Protocol::ToString0x97(*(StAlarmAndImgInfo*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_ACTION_INFO://0x28
			{
				if (dir == 0)
				{
					strRst += _T("道岔动作后模拟量及缺口图像信息回执");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StActionInfoRec*)data.lpdata);
				}
				else
				{
					strRst += _T("道岔动作后模拟量及缺口图像信息");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StActionInfo*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_LASTGAPIMG://0x29
			{
				if (dir == 0)
				{
					strRst += _T("道岔缺口最新图像请求");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StLastGapImgReq*)data.lpdata);
				}
				else
				{
					strRst += _T("道岔缺口最新图像应答");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StLastGapImgRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_IMGLIST:
			{
				if (dir == 0)
				{
					strRst += _T("图像列表请求");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StImgListReq*)data.lpdata);
				}
				else
				{
					strRst += _T("图像列表应答");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StImgListRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_IMGINFO:
			{
				if (dir == 0)
				{
					strRst += _T("图像信息请求");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StImgInfoReq*)data.lpdata);
				}
				else
				{
					strRst += _T("图像信息应答");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StImgInfoRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_VEDIOLIST:
			{
				if (dir == 0)
				{
					strRst += _T("视频时间列表请求");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StVedioListReq*)data.lpdata);
				}
				else
				{
					strRst += _T("视频时间列表应答");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StVedioListRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_VEDIOFILE:
			{
				if (dir == 0)
				{
					strRst += _T("视频文件请求");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StVedioFileReq*)data.lpdata);
				}
				else
				{
					strRst += _T("视频文件应答");
					strRst += _T("，");
					if (b2Flen2byte)
					{
						strRst += Parse315Protocol::ToString((StVedioFileRes2*)data.lpdata);
					}
					else
					{
						strRst += Parse315Protocol::ToString((StVedioFileRes4*)data.lpdata);
					}
				}
				break;
			}
			case CMD_CODE_1DQJINFO:
			{
				if (dir == 0)
				{
					strRst += _T("1DQJ及道岔区段状态信息");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(St1DQJInfo*)data.lpdata);
				}
				else
				{
				}
				break;
			}
			case CMD_CODE_YYQX:
			{
				strRst += _T("曲线");
				strRst += _T("，");
				strRst += Parse315Protocol::ToString(*(StOilPreCurve*)data.lpdata);

				break;
			}
			case CMD_CODE_YWINFO:
			{
				if (dir == 0)
				{
				}
				else
				{
					strRst += _T("油位及缺口采集设备状态信息");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StOilLevelInfo*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_PARAMETER:
			{
				if (dir == 0)
				{
					strRst += _T("参数设置与获取");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StParamSet*)data.lpdata);
				}
				else
				{
					strRst += _T("参数设置与获取");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StParamSet*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_REALCTRL:
			{
				if (dir == 0)
				{
					strRst += _T("实时视频流控制请求");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StRealCtrlReq*)data.lpdata);
				}
				else
				{
					strRst += _T("实时视频流控制应答");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StRealCtrlRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_REALSTREAM:
			{
				if (dir == 0)
				{
				}
				else
				{
					strRst += _T("实时码流");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StRealStream*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_ELECCURVE:
			{
				if (dir == 0)
				{
					strRst += _T("室外电参数动作曲线回执");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StElecCurveRec*)data.lpdata);
				}
				else
				{
					strRst += _T("室外电参数动作曲线");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StElecCurve*)data.lpdata);
				}
				break;
			}
			//case CMD_CODE_NEW_DATA_FILE:
			//{
			//	strRst += _T("阻力新数据");
			//	strRst += _T("，");
			//	strRst += Parse315Protocol::ToString(*(StNewPowerNotify*)data.lpdata);
			//	break;
			//}
			case CMD_CODE_QUERY_POWER_FILE:
			{
				strRst += _T("阻力文件列表信息，");
				strRst += Parse315Protocol::ToString((StPowerFileListF2*)data.lpdata, data.datalen);
				break;
			}
			case CMD_CODE_DOWNLOAD_DATA_FILE:
			{
				if (dir == 1)
				{
					strRst += _T("阻力文件信息，");
					strRst += Parse315Protocol::ToString(*(StPowerFileData*)data.lpdata);
				}
				else
				{
					strRst += _T("阻力文件信息，");
					StPowerFileDataResq* pResq = (StPowerFileDataResq*)(data.lpdata);
					strRst += _T("，");
					TCHAR buf[256] = { 0 };
					_stprintf_s(buf, _T("转辙机ID:%d,"), pResq->sid);
					strRst += buf;

					//CTime tm = pResq->time;
					TIME tm = timeopt::Unix2SysTime(pResq->time);
					_stprintf_s(buf, _T("采集时间:%04d-%02d-%02d %02d:%02d:%02d, 对象类型:"),
						tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
					strRst += buf;

					//strRst += Parse315Protocol::GetAcqDescByAcqObjType((ACQ_OBJ_TYPE)pResq->acqObjType);

					if (pResq->cbReadMode == 0)
					{
						strRst += ",一次性读取";
					}
					else
					{
						strRst += ",分包读取, 分包号:";
						strRst += to_string(pResq->wSubID);
					}

					//strRst += Parse315Protocol::ToString(*(StPowerFileDataResq*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_POWSTATIC:
			{
				strRst += _T("静态阻力上送");
				strRst += _T("，");
				strRst += Parse315Protocol::ToString(*(StStaticPowerList*)data.lpdata);

				break;
			}
			case CMD_CODE_POWERLIST:
			{
				if (dir == 0)
				{
					strRst += _T("阻力文件列表请求");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StPowerListReq*)data.lpdata);
				}
				else
				{
					strRst += _T("阻力文件列表应答");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StPowerListRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_POWERCONTENT:
			{
				if (dir == 0)
				{
					strRst += _T("阻力信息请求");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StPowerInfoReq*)data.lpdata);
				}
				else
				{
					strRst += _T("阻力信息应答");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StPowerInfoRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_DBJFBJ:
			{
				if (dir == 0)
				{
					strRst += _T("定反表状态");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(DBJFBJInfo*)data.lpdata);
				}
				else
				{
				}
				break;
			}
			case 0x51:
			{
				if (dir == 0)
				{
					strRst += _T("手动加油结果：");
					strRst += Parse315Protocol::ToString(*(StManualOilingRes*)data.lpdata);
				}
				else
				{
					strRst += _T("手动加油结果：");
					strRst += Parse315Protocol::ToString(*(StManualOilingRes*)data.lpdata);
				}
				break;

				//if (data.datalen == sizeof(StManualOilingRes))
				//{
				//	//StPowerFileList* lpdata = new StPowerFileList[(data.datalen - 1) / sizeof(StPowerFileList)];
				//	StManualOilingRes* lpdata = new StManualOilingRes;
				//	CopyMemory(lpdata, pos, data.datalen);
				//	data.lpdata = lpdata;
				//	pos += data.datalen;
				//}
				//else
				//{
				//	return FALSE;
				//}
				//break;
			}
			case 0x52:
			{
				if (dir == 0)
				{
					strRst += _T("加油结果广播:");
					strRst += Parse315Protocol::ToString(*(StOilingResultNotify*)data.lpdata);
				}
				else
				{
					strRst += _T("加油结果广播:");
					strRst += Parse315Protocol::ToString(*(StOilingResultNotify*)data.lpdata);
				}
				break;

				//if (data.datalen == sizeof(StOilingResultNotify))
				//{
				//	//StPowerFileList* lpdata = new StPowerFileList[(data.datalen - 1) / sizeof(StPowerFileList)];
				//	StOilingResultNotify* lpdata = new StOilingResultNotify;
				//	CopyMemory(lpdata, pos, data.datalen);
				//	data.lpdata = lpdata;
				//	pos += data.datalen;
				//}
				//else
				//{
				//	return FALSE;
				//}
				//break;
			}
			case (uint8_t)E_315_PROTOCOL_TYPE::UN_RECOVER_ALARM_0x65:
			{
				if (dir == 0)
				{
					strRst += _T("未恢复报警列表请求");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StAlarmListReq*)data.lpdata);
				}
				else
				{
					strRst += _T("未恢复报警列表应答");
					strRst += _T("，");
					strRst += Parse315Protocol::ToString(*(StAlarmListRes*)data.lpdata);
				}
				break;
			}
			default:
				break;
			}
			break;
		}
		default:
			break;
		}

	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StHeartBeat315& data)
{
	tstring strRst = _T("");

	strRst += _T("时间:");
	//CTime tm = data.hbtime;
	TIME tm = timeopt::Unix2SysTime(data.hbtime);
	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::AStringToTString(LPCSTR lpStr)
{
	tstring strRst;

#ifdef _UNICODE
	int len = MultiByteToWideChar(CP_ACP, 0, lpStr, -1, NULL, 0);
	if (len == 0)
		return strRst;

	wchar_t* wszBuf = new wchar_t[len];
	if (MultiByteToWideChar(CP_ACP, 0, lpStr, -1, wszBuf, len) == 0)
	{
		delete[] wszBuf;
		return strRst;
	}

	strRst = wszBuf;
	delete[] wszBuf;

#else
	strRst = lpStr;
#endif

	return strRst;
}


//tstring Parse315Protocol::GetAcqDescByAcqObjType(ACQ_OBJ_TYPE acqObjType)
//{
//	if (acqObjType == ACQ_OBJ_TYPE::AOT_ZZJ_QDDL)
//	{
//		return "驱动电路";
//	}
//	else if (acqObjType == ACQ_OBJ_TYPE::AOT_ZZJ_ZHENGDONG)
//	{
//		return "振动";
//	}
//	else if (acqObjType == ACQ_OBJ_TYPE::AOT_ZZJ_ELECPARAM)
//	{
//		return "电参数";
//	}
//	else if (acqObjType == ACQ_OBJ_TYPE::AOT_ZZJ_POWER)
//	{
//		return "转换阻力";
//	}
//	else if (acqObjType == ACQ_OBJ_TYPE::AOT_ZZJ_YY)
//	{
//		return "油压";
//	}
//
//	return "";
//}

tstring Parse315Protocol::ToString(const StGapCfgRes& data)
{
	tstring strRst = _T("");

	if (data.cmdid == CMD_CODE_GAPCFG)
	{
		TCHAR buf[256] = { 0 };
		_stprintf_s(buf, _T("配置数:%d\r\n"), data.cfgcnt);
		strRst += buf;

		for (int i = 0; i < data.cfgcnt; ++i)
		{
			const StSwitchCfg& scfg = ((StSwitchCfg*)data.lpcfg)[i];

			strRst += _T("，");
			_stprintf_s(buf, _T("配置%d:{"), i);
			strRst += buf;

			char name[50] = { 0 };
			memcpy_s(name, 50, scfg.lpname, scfg.nlen);
			strRst += _T("转辙机:") + AStringToTString(name);

			strRst += _T("，");
			strRst += _T("转辙机类型:");
			switch (scfg.stype)
			{
			case SWITCH_TYPE_ZD6:
				strRst += _T("ZD6");
				break;
			case SWITCH_TYPE_EJ:
				strRst += _T("EJ");
				break;
			case SWITCH_TYPE_GK:
				strRst += _T("GK");
				break;
			case SWITCH_TYPE_S700K:
				strRst += _T("S700K");
				break;
			case SWITCH_TYPE_ZDJ_9:
				strRst += _T("ZDJ-9");
				break;
			case SWITCH_TYPE_ZYJ7_I:
				strRst += _T("ZYJ7-I");
				break;
			case SWITCH_TYPE_ZYJ7_II:
				strRst += _T("ZYJ7-II");
				break;
			case SWITCH_TYPE_ZYJ9:
				strRst += _T("ZY(J)9");
				break;
			default:
				strRst += _T("未知");
				break;
			}

			strRst += _T("，");
			_stprintf_s(buf, _T("转辙机ID:%d"), scfg.sid);
			strRst += buf;

			strRst += _T("，");
			_stprintf_s(buf, _T("采集信息个数:%d"), scfg.cnt);
			strRst += buf;

			strRst += _T("，");
			strRst += _T("采集信息类型:{");
			for (int j = 0; j < scfg.cnt; ++j)
			{
				switch (((WORD*)scfg.lpinfo)[j])
				{
				case SWITCH_INFO_GAP:
					strRst += _T("缺口值,");
					break;
				case SWITCH_INFO_OFFSET:
					strRst += _T("偏移值,");
					break;
				case SWITCH_INFO_STD:
					strRst += _T("标准值,");
					break;
				case SWITCH_INFO_OIL:
					strRst += _T("油位,");
					break;
				case SWITCH_INFO_TEMP:
					strRst += _T("温度,");
					break;
				case SWITCH_INFO_HUM:
					strRst += _T("湿度,");
					break;
				case SWITCH_INFO_OILPRE:
					strRst += _T("油压曲线,");
					break;
				case SWITCH_INFO_VOL:
					strRst += _T("电压,");
					break;
				case SWITCH_INFO_ELEC:
					strRst += _T("电流,");
					break;
				default:
					//工况
					WORD wIndex = ((WORD*)scfg.lpinfo)[j];
					if (wIndex > 50 && wIndex < 66)
					{
						//strRst += g_szWorkingConditionName[wIndex - 51];
						strRst += ",";
					}
					else
						strRst += _T("未知,");
					break;
				}

			}
			strRst += _T("}");

			strRst += _T("}");


			strRst += "\r\n";
		}
	}
	else
	{
		TCHAR buf[256] = { 0 };
		_stprintf_s(buf, _T("文件数:%d\r\n"), data.cfgcnt);
		strRst += buf;
		strRst += (char*)data.lpcfg;
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StGapValue& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("记录数:%d\r\n"), data.cnt);
	strRst += buf;

	//#ifdef _DEBUG
	//	std::map<int, std::string> mapName{ {1,"W0102J1"},
	//		{2,"W0102J2"},
	//		{3,"W0104J1"},
	//		{4,"W0104J2"},
	//		{5,"W0106J1"},
	//		{6,"W0106J2"},
	//		{7,"W0108J1"},
	//		{8,"W0108J2"},
	//		{9,"W0110J1"},
	//		{10,"W0110J2"}
	//	};
	//#endif // _DEBUG

	for (int i = 0; i < data.cnt; ++i)
	{
		_stprintf_s(buf, _T("缺口记录%d:{"), i);
		strRst += buf;

		const StGapRecord* rcd;
		if (b26res4byte)
		{
			rcd = (StGapRecord*)(&((StGapRecord_4res*)data.lpdata)[i]);
		}
		else
		{
			rcd = (StGapRecord*)(&((StGapRecord*)data.lpdata)[i]);
		}


		strRst += _T("时间:");
		//CTime tm = rcd->time;
		TIME tm = timeopt::Unix2SysTime(rcd->time);
		_stprintf_s(buf, _T("%04d-%02d-%02d %02d:%02d:%02d"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("转辙机ID:%d"), rcd->sid);
		strRst += buf;
		//
		//#ifdef _DEBUG
		//		strRst += _T("，");
		//		_stprintf_s(buf, _T("转辙机名称:%s"), mapName[rcd->sid].c_str());
		//		strRst += buf;
		//#endif // _DEBUG

		strRst += _T("，");
		strRst += _T("定反位:");
		if (rcd->fixorinvert == 0)
			strRst += _T("定位");
		else
			strRst += _T("反位");

		strRst += _T("，");
		strRst += _T("左右偏标志:");
		if (rcd->lrsign == 1)
			strRst += _T("左偏");
		else if (rcd->lrsign == 2)
			strRst += _T("右偏");
		else
			strRst += _T("无效");

		strRst += _T("，");
		_stprintf_s(buf, _T("偏移值:%.2fmm"), rcd->offset / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("缺口值:%.2fmm"), rcd->gap / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("标准值:%.2fmm"), rcd->std / 100.0);
		strRst += buf;

		strRst += _T("，");
		strRst += _T("缺口值类型:");
		if (rcd->gaptype == 0)
			strRst += _T("静态采集");
		else if (rcd->gaptype == 1)
			strRst += _T("道岔操纵后采集");
		else if (rcd->gaptype == 2)
			strRst += _T("过车时采集");
		else
			strRst += _T(" ");

		strRst += _T("}");

		strRst += "\r\n";
	}

	return strRst;
}

SYSTEMTIME Parse315Protocol::Time_tToSystemTime(time_t t)
{
	tm temptm;
	localtime_s(&temptm, &t);
	SYSTEMTIME st = { 1900 + temptm.tm_year,
		1 + temptm.tm_mon,
		temptm.tm_wday,
		temptm.tm_mday,
		temptm.tm_hour,
		temptm.tm_min,
		temptm.tm_sec,
		0 };
	return st;
}


BOOL Parse315Protocol::TimeToString(const SYSTEMTIME& time, string& str)
{
	if (time.wYear > 2000 && time.wDay > 0 && time.wDay < 40 && time.wHour >= 0 && time.wHour <= 24 && time.wMinute >= 0 && time.wMinute <= 60)
	{
		str = str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
			time.wYear, time.wMonth, time.wDay,
			time.wHour, time.wMinute, time.wSecond);
	}
	return TRUE;
}

enum  WIF_VERSION
{
	WIF_VERSION_2015 = 2015,
	WIF_VERSION_2020 = 2020,
	WIF_VERSION_2023 = 2023,
};

enum  GapAcqTypeOfFile
{
	egatofUntyped = 0,//未指定
	egatofTrigger = 1,//扳动采集
	egatofInterval = 2,//周期采集
	egatofNormal = 3,//日常采集
	egatofImd = 4,//即时采集
	egatofCrsCar = 5,//过车采集
	egatofGlobal = 6,//全站采集
	egatofWIF = 7,//微机监测请求
	egatofLast
};


std::map<WIF_VERSION, std::map<GapAcqTypeOfFile, string>> GapAcqTypeName =
{
	{WIF_VERSION::WIF_VERSION_2015,{
		{GapAcqTypeOfFile::egatofUntyped, "未知"     },
		{GapAcqTypeOfFile::egatofTrigger, "扳动"     },
		{GapAcqTypeOfFile::egatofInterval, "周期"    },
		{GapAcqTypeOfFile::egatofNormal, "日常"      },
		{GapAcqTypeOfFile::egatofImd, "即时"         },
		{GapAcqTypeOfFile::egatofCrsCar, "过车"      },
		{GapAcqTypeOfFile::egatofGlobal, "全站"      },
		{GapAcqTypeOfFile::egatofWIF, "微机"			}}},
	{WIF_VERSION::WIF_VERSION_2020,{
		{GapAcqTypeOfFile::egatofUntyped, "未知"     },
		{GapAcqTypeOfFile::egatofTrigger, "扳动"     },
		{GapAcqTypeOfFile::egatofInterval, "周期"    },
		{GapAcqTypeOfFile::egatofNormal, "日常"      },
		{GapAcqTypeOfFile::egatofImd, "即时"         },
		{GapAcqTypeOfFile::egatofCrsCar, "过车"      },
		{GapAcqTypeOfFile::egatofGlobal, "全站"      },
		{GapAcqTypeOfFile::egatofWIF, "微机"			}}},
	{WIF_VERSION::WIF_VERSION_2023,{
		{GapAcqTypeOfFile::egatofUntyped, "未知"     },
		{GapAcqTypeOfFile::egatofTrigger, "转换过程"     },
		{GapAcqTypeOfFile::egatofInterval, "静态"    },
		{GapAcqTypeOfFile::egatofNormal, "静态"      },
		{GapAcqTypeOfFile::egatofImd, "静态"         },
		{GapAcqTypeOfFile::egatofCrsCar, "过车时"      },
		{GapAcqTypeOfFile::egatofGlobal, "静态"      },
		{GapAcqTypeOfFile::egatofWIF, "静态"			}}}
};

string GetGapAcqTypeName(GapAcqTypeOfFile eType, bool bNeedAcqText/* = true*/)
{
	//当前版本
	auto pFindVersion = GapAcqTypeName.find(WIF_VERSION::WIF_VERSION_2015);
	if (pFindVersion == GapAcqTypeName.end()) return "";

	//当前采集类型
	auto pFindType = pFindVersion->second.find(eType);
	if (pFindType == pFindVersion->second.end()) return "";

	string strName = pFindType->second + (bNeedAcqText ? "采集" : "");

	return strName;
}


tstring Parse315Protocol::ToString(const StAlarmAndImgInfo& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("报警时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	BYTE* cbFill = (BYTE*)&data.filldata;
	switch (cbFill[0])
	{
	case 3:
		strRst += ",采集类型:" + GetGapAcqTypeName(GapAcqTypeOfFile::egatofCrsCar, true);
		break;
	case 2:
		strRst += ",采集类型:" + GetGapAcqTypeName(GapAcqTypeOfFile::egatofTrigger, true);
		break;

	default:
		strRst += ",采集类型:" + GetGapAcqTypeName(GapAcqTypeOfFile::egatofInterval, true);
		break;
	}

	int nVer = (cbFill[1] == 0xFF ? 2015 : (cbFill[1] + 2000));


	strRst += _T("，");
	SYSTEMTIME ackTime = Time_tToSystemTime(data.alarmconfirm);
	string strAckTime;
	TimeToString(ackTime, strAckTime);

	if (data.alarmconfirm == 0xffffffff)
	{
		strAckTime = "0xFFFFFFFF";
	}

	_stprintf_s(buf, _T("报警确认信号:%s"), strAckTime);
	strRst += buf;

	bool bPowerAlarm = false; //阻力报警
	if (nVer < 2023 && (data.alarmtype == ALARM_TYPE_POWERYJHF || data.alarmtype == ALARM_TYPE_POWERBJHF
		|| data.alarmtype == ALARM_TYPE_POWERYJ || data.alarmtype == ALARM_TYPE_POWERBJ)
		|| nVer >= 2023 && (data.alarmtype == 13 || data.alarmtype == 113))
	{
		strRst += _T("，");
		strRst += GetGapAcqTypeName(GapAcqTypeOfFile::egatofTrigger, false) + _T("方向:");
		if (data.fixorinvert == 0)
			strRst += _T("定->反");
		else
			strRst += _T("反->定");

		strRst += _T("，");
		strRst += _T("报警子类型:");
		if (data.lrsign == 1)
			strRst += _T("阻力曲线最大值超标");
		else if (data.lrsign == 2)
			strRst += _T("阻力曲线平均值超标");
		else if (data.lrsign == 3)
			strRst += _T("阻力曲线相对参考线变化量超标");

		bPowerAlarm = true; //阻力报警
	}
	else
	{
		strRst += _T("，");
		strRst += _T("定反位:");
		if (data.fixorinvert == 0)
			strRst += _T("定位");
		else
			strRst += _T("反位");

		strRst += _T("，");
		strRst += _T("左右偏标志:");
		if (data.lrsign == 1)
			strRst += _T("左偏");
		else if (data.lrsign == 2)
			strRst += _T("右偏");
		else
			strRst += _T("无效");
	}

	strRst += _T("，");
	strRst += _T("报警类型:") + GetAlarmTypeDesc(data.alarmtype, nVer);


	if (bPowerAlarm)
	{
		strRst += _T("，");
		_stprintf_s(buf, _T("动作杆伸缩方向:%s"), data.offset == 0 ? "拉入" : "伸出");
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("预警/报警起始点数:%d"), data.gap);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("预警/报警结束点数:%d"), data.std);
		strRst += buf;
	}
	else
	{

		strRst += _T("，");
		_stprintf_s(buf, _T("偏移值:%.2fmm"), data.offset / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("缺口值:%.2fmm"), data.gap / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("标准值:%.2fmm"), data.std / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("图像长度:%d"), data.imglen);
		strRst += buf;

		strRst += _T("，");
		strRst += _T("图像内容:{...}");
	}

	return strRst;
}

tstring Parse315Protocol::ToString0x27(const StAlarmAndImgRec& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("报警时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	string strAckTime = "0xFFFFFFFF";
	if (data.alarmtype >= 100)
	{
		SYSTEMTIME ackTime = Time_tToSystemTime(data.alarmconfirm);
		TimeToString(ackTime, strAckTime);
	}

	_stprintf_s(buf, _T(",报警确认信号:%s"), strAckTime);
	strRst += buf;
	BYTE* cbFill = (BYTE*)&data.filldata;
	switch (cbFill[0])
	{
	case 3:
		strRst += ",采集类型:" + GetGapAcqTypeName(GapAcqTypeOfFile::egatofCrsCar, true);
		break;
	case 2:
		strRst += ",采集类型:" + GetGapAcqTypeName(GapAcqTypeOfFile::egatofTrigger, true);
		break;

	default:
		strRst += ",采集类型:" + GetGapAcqTypeName(GapAcqTypeOfFile::egatofInterval, true);
		break;
	}
	int nVer = (cbFill[1] == 0xFF ? 2015 : (cbFill[1] + 2000));

	bool bPowerAlarm = false; //阻力报警
	if (nVer < 2023 && (data.alarmtype == ALARM_TYPE_POWERYJHF || data.alarmtype == ALARM_TYPE_POWERBJHF
		|| data.alarmtype == ALARM_TYPE_POWERYJ || data.alarmtype == ALARM_TYPE_POWERBJ)
		|| nVer >= 2023 && (data.alarmtype == 13 || data.alarmtype == 113))
	{
		strRst += _T("，");
		strRst += GetGapAcqTypeName(GapAcqTypeOfFile::egatofTrigger, false) + _T("方向:");
		if (data.fixorinvert == 0)
			strRst += _T("定->反");
		else
			strRst += _T("反->定");

		strRst += _T("，");
		strRst += _T("报警子类型:");
		if (data.lrsign == 1)
			strRst += _T("阻力曲线最大值超标");
		else if (data.lrsign == 2)
			strRst += _T("阻力曲线平均值超标");
		else if (data.lrsign == 3)
			strRst += _T("阻力曲线相对参考线变化量超标");

		bPowerAlarm = true; //阻力报警
	}
	else
	{
		strRst += _T("，");
		strRst += _T("定反位:");
		if (data.fixorinvert == 0)
			strRst += _T("定位");
		else
			strRst += _T("反位");

		strRst += _T("，");
		strRst += _T("左右偏标志:");
		if (data.lrsign == 1)
			strRst += _T("左偏");
		else if (data.lrsign == 2)
			strRst += _T("右偏");
		else
			strRst += _T("无效");
	}

	strRst += _T("，");
	strRst += _T("报警类型:") + GetAlarmTypeDesc(data.alarmtype, nVer);

	if (bPowerAlarm)
	{
		strRst += _T("，");
		_stprintf_s(buf, _T("动作杆伸缩方向:%s"), data.offset == 0 ? "拉入" : "伸出");
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("预警/报警起始点数:%d"), data.gap);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("预警/报警结束点数:%d"), data.std);
		strRst += buf;
	}
	else
	{
		strRst += _T("，");
		_stprintf_s(buf, _T("偏移值:%.2fmm"), data.offset / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("缺口值:%.2fmm"), data.gap / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("标准值:%.2fmm"), data.std / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("图像长度:%d"), data.imglen);
		strRst += buf;
	}

	return strRst;
}

tstring Parse315Protocol::ToString0x97(const StAlarmAndImgRec& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("报警时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;
	strRst += _T("，");
	strRst += _T("报警类型:");
	switch (data.alarmtype)
	{
	case ALARM_TYPE_QKYJ:
		strRst += _T("缺口预警及预警图像");
		break;
	case ALARM_TYPE_QKBJ:
		strRst += _T("缺口告警及告警图像");
		break;
	case ALARM_TYPE_QKSBGZ:
		strRst += _T("缺口采集设备故障");
		break;
	case ALARM_TYPE_TXWFSB:
		strRst += _T("缺口图像无法识别告警");
		break;
	case ALARM_TYPE_GCKLGD:
		strRst += _T("过车时框量过大告警及过车视频");
		break;
	case ALARM_TYPE_ZZJSBGZ:
		strRst += _T("转辙机采集设备故障告警");
		break;
	case ALARM_TYPE_WDBJ:
		strRst += _T("温度告警");
		break;
	case ALARM_TYPE_SDBJ:
		strRst += _T("湿度告警");
		break;
	case ALARM_TYPE_YWYJ:
		strRst += _T("油位预警");
		break;
	case ALARM_TYPE_YWBJ:
		strRst += _T("油位告警");
		break;
	case ALARM_TYPE_POWERYJ:
		strRst += _T("油压预警");
		break;
	case ALARM_TYPE_POWERBJ:
		strRst += _T("油压告警");
		break;
	case ALARM_TYPE_TEMPERATURE:
		strRst += _T("温度预警");
		break;
	case ALARM_TYPE_HUMILITY:
		strRst += _T("湿度预警");
		break;
	case ALARM_TYPE_QKYJHF:
		strRst += _T("缺口预警恢复及图像");
		break;
	case ALARM_TYPE_QKBJHF:
		strRst += _T("缺口告警恢复及图像");
		break;
	case ALARM_TYPE_QKSBGZHF:
		strRst += _T("缺口采集设备故障恢复及图像");
		break;
	case ALARM_TYPE_TXWFSBHF:
		strRst += _T("缺口图像无法识别告警恢复及图像");
		break;
	case ALARM_TYPE_ZZJSBGZHF:
		strRst += _T("转辙机采集设备故障告警恢复");
		break;
	case ALARM_TYPE_GCKLGDHF:
		strRst += _T("过车时框量过大告警恢复及过车视频");
		break;
	case ALARM_TYPE_WDBJHF:
		strRst += _T("温度告警恢复");
		break;
	case ALARM_TYPE_SDBJHF:
		strRst += _T("温度告警恢复");
		break;
	case ALARM_TYPE_YWYJHF:
		strRst += _T("油位预警恢复");
		break;
	case ALARM_TYPE_YWBJHF:
		strRst += _T("油位告警恢复");
		break;
	case ALARM_TYPE_POWERYJHF:
		strRst += _T("油压预警恢复");
		break;
	case ALARM_TYPE_POWERBJHF:
		strRst += _T("油压告警恢复");
		break;
	case ALARM_TYPE_TEMPERATUREHF:
		strRst += _T("温度预警恢复");
		break;
	case ALARM_TYPE_HUMILITYHF:
		strRst += _T("湿度预警恢复");
		break;
	default:
		strRst += _T("未知");
		break;
	}

	strRst += _T("，");
	string strAckTime = "0xFFFFFFFF";
	if (data.alarmtype >= 100)
	{
		SYSTEMTIME ackTime = Time_tToSystemTime(data.alarmconfirm);
		TimeToString(ackTime, strAckTime);
	}

	_stprintf_s(buf, _T("报警确认信号:%s"), strAckTime);
	strRst += buf;

	static std::map<int, std::string> mapAlarmLevel = {
	{ALARM_TYPE_QKYJ, "预警"},		//缺口预警及预警图像
	{ ALARM_TYPE_QKBJ			, "告警" },		//缺口告警及告警图像
	{ ALARM_TYPE_QKSBGZ		, "预警" },		//缺口采集设备故障，此时没有缺口值及缺口图像，左右偏移标志填无效(00),总包数填1，本包序号0，图像总长度0， 本帧图像长度0.
	{ ALARM_TYPE_TXWFSB		, "告警" },		//缺口图像无法识别告警
	{ ALARM_TYPE_GCKLGD		, "告警" },		//过车时框量过大告警及过车视频
	{ ALARM_TYPE_ZZJSBGZ		, "告警" },		//转辙机采集设备故障告警
	{ ALARM_TYPE_WDBJ			, "告警" },		//温度告警
	{ ALARM_TYPE_SDBJ			, "告警" },		//湿度告警
	{ ALARM_TYPE_YWYJ			, "预警" },		//油位预警（预留）
	{ ALARM_TYPE_YWBJ			, "告警" },		//油位告警（预留）
	{ ALARM_TYPE_POWERYJ      , "预警" },        //阻力预警
	{ ALARM_TYPE_POWERBJ      , "告警" },        //阻力告警
	{ ALARM_TYPE_TEMPERATURE  , "预警" },		//温度预警
	{ ALARM_TYPE_HUMILITY		, "预警" },		//湿度预警
	{ ALARM_TYPE_QKYJHF		, "预警" },		//缺口预警恢复及图像
	{ ALARM_TYPE_QKBJHF		, "告警" },		//缺口告警恢复及图像
	{ ALARM_TYPE_QKSBGZHF		, "预警" },		//缺口采集设备故障恢复及图像
	{ ALARM_TYPE_TXWFSBHF		, "告警" },		//缺口图像无法识别告警恢复及图像
	{ ALARM_TYPE_ZZJSBGZHF	, "告警" },		//转辙机采集设备故障告警恢复
	{ ALARM_TYPE_GCKLGDHF		, "告警" },		//过车时框量过大告警恢复及过车视频
	{ ALARM_TYPE_WDBJHF		, "告警" },		//温度告警恢复
	{ ALARM_TYPE_SDBJHF		, "告警" },		//温度告警恢复
	{ ALARM_TYPE_YWYJHF		, "预警" },		//油位预警恢复（预留）
	{ ALARM_TYPE_YWBJHF		, "告警" },		//油位告警恢复（预留）
	{ ALARM_TYPE_POWERYJHF    , "预警" },        //阻力预警恢复
	{ ALARM_TYPE_POWERBJHF    , "告警" },        //阻力告警恢复
	{ ALARM_TYPE_TEMPERATUREHF, "预警" },		//温度预警恢复
	{ ALARM_TYPE_HUMILITYHF	, "预警" } };		//湿度预警恢复

	BYTE* cbFill = ((BYTE*)&data.filldata);

	string acqType;
	switch (cbFill[0])
	{
	case 3:
		acqType = ",采集类型:" + GetGapAcqTypeName(GapAcqTypeOfFile::egatofCrsCar, true);
		break;
	case 2:
		acqType = ",采集类型:" + GetGapAcqTypeName(GapAcqTypeOfFile::egatofTrigger, true);
		break;

	default:
		acqType = ",采集类型:" + GetGapAcqTypeName(GapAcqTypeOfFile::egatofInterval, true);
		break;
	}

	//温湿度
	if (data.alarmtype == ALARM_TYPE_TEMPERATURE || data.alarmtype == ALARM_TYPE_WDBJ
		|| data.alarmtype == ALARM_TYPE_HUMILITY || data.alarmtype == ALARM_TYPE_SDBJ
		|| data.alarmtype == ALARM_TYPE_TEMPERATUREHF || data.alarmtype == ALARM_TYPE_WDBJHF
		|| data.alarmtype == ALARM_TYPE_HUMILITYHF || data.alarmtype == ALARM_TYPE_SDBJHF)
	{

		tstring sunit = (data.alarmtype % 10 == 7 ? _T("℃") : _T("%"));
		strRst += acqType + _T("，");
		strRst += _T("描述:");

		strRst += (data.alarmtype % 10 == 7 ? _T("温度:") : _T("湿度:"));

		_stprintf_s(buf, _T("(%.2f%s)"), ((float)data.gap) / 100.0, sunit.c_str());
		strRst += buf;

		if (data.alarmtype <= 100)
		{
			if (data.lrsign == 1)
			{
				_stprintf_s(buf, _T("大于%s上限(%.2f%s)"), mapAlarmLevel[data.alarmtype].c_str(), ((float)data.std) / 100.0, sunit.c_str());
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				_stprintf_s(buf, _T("小于%s下限(%.2%s)"), mapAlarmLevel[data.alarmtype].c_str(), ((float)data.offset) / 100.0, sunit.c_str());
				strRst += buf;
			}
		}

		return strRst;
	}
	//油位报警
	else if (data.alarmtype == ALARM_TYPE_YWYJ || data.alarmtype == ALARM_TYPE_YWYJHF
		|| data.alarmtype == ALARM_TYPE_YWBJ || data.alarmtype == ALARM_TYPE_YWBJHF)
	{
		strRst += acqType + _T("，");
		strRst += _T("描述:");

		_stprintf_s(buf, _T("油位(%dmm)"), data.gap);
		strRst += buf;
		if (data.alarmtype <= 100)
		{
			if (data.lrsign == 1)
			{
				_stprintf_s(buf, _T("大于%s上限(%dmm)"), mapAlarmLevel[data.alarmtype].c_str(), data.std);
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				_stprintf_s(buf, _T("小于%s下限(%dmm)"), mapAlarmLevel[data.alarmtype].c_str(), data.offset);
				strRst += buf;
			}
		}
		return strRst;
	}
	//油压报警
	else if (data.alarmtype == ALARM_TYPE_POWERYJHF || data.alarmtype == ALARM_TYPE_POWERBJHF
		|| data.alarmtype == ALARM_TYPE_POWERYJ || data.alarmtype == ALARM_TYPE_POWERBJ)
	{
		strRst += _T("，");
		strRst += _T("描述:");
		if (data.fixorinvert == 1)
			strRst += _T("定到反");
		else
			strRst += _T("反到定");

		strRst += _T(":");

		switch (cbFill[0])
		{
		case 1:
			strRst += _T("解锁阶段");
			break;
		case 2:
			strRst += _T("动作阶段");
			break;
		case 3:
			strRst += _T("锁闭阶段");
			break;
		case 4:
			strRst += _T("释压阶段");
			break;
		default:
			break;
		}
		_stprintf_s(buf, _T("油压最大值(%.2fMPa)"), ((float)data.gap) / 100.0);
		strRst += buf;
		if (data.alarmtype <= 100)
		{
			if (data.lrsign == 1)
			{
				_stprintf_s(buf, _T("大于上限(%.2fMPa)"), ((float)data.std) / 100.0);
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				_stprintf_s(buf, _T("小于下限(%.2fMPa)"), ((float)data.offset) / 100.0);
				strRst += buf;
			}
		}
		return strRst;
	}
	else if (data.alarmtype == ALARM_TYPE_QKYJ || data.alarmtype == ALARM_TYPE_QKYJHF
		|| data.alarmtype == ALARM_TYPE_QKBJ || data.alarmtype == ALARM_TYPE_QKBJHF)
	{
		strRst += _T("，");
		strRst += _T("描述:");
		switch (cbFill[0])
		{
		case 1:
			strRst += _T("静态缺口:");
			break;
		case 2:
			strRst += GetGapAcqTypeName(GapAcqTypeOfFile::egatofTrigger, false) + _T("后缺口:");
			break;
		case 3:
			strRst += _T("过车缺口:");
			break;
		case 4:
			strRst += _T("过车前缺口:");
			break;
		case 5:
			strRst += _T("过车后缺口:");
			break;
		default:
			break;
		}
		if (data.fixorinvert == 1)
			strRst += _T("反位缺口");
		else
			strRst += _T("定位缺口");

		_stprintf_s(buf, _T("(%.2fmm)"), ((float)data.gap) / 100.0);
		strRst += buf;

		if (data.alarmtype <= 100)
		{
			if (cbFill[3] == 1)
			{
				_stprintf_s(buf, _T("大于%s上限(%.2fmm)"), mapAlarmLevel[data.alarmtype].c_str(), ((float)(*(WORD*)&cbFill[1])) / 100.0);
				strRst += buf;
			}
			else if (cbFill[3] == 2)
			{
				_stprintf_s(buf, _T("小于%s下限(%.2fmm)"), mapAlarmLevel[data.alarmtype].c_str(), ((float)(*(WORD*)&cbFill[1])) / 100.0);
				strRst += buf;
			}
		}
		return strRst;
	}
	else
	{
		strRst += acqType + _T("，");
		strRst += _T("定反位:");
		if (data.fixorinvert == 0)
			strRst += _T("定位");
		else
			strRst += _T("反位");

		strRst += _T("，");
		strRst += _T("左右偏标志:");
		if (data.lrsign == 1)
			strRst += _T("左偏");
		else if (data.lrsign == 2)
			strRst += _T("右偏");
		else
			strRst += _T("无效");
	}

	if (data.alarmtype == ALARM_TYPE_POWERYJHF || data.alarmtype == ALARM_TYPE_POWERBJHF
		|| data.alarmtype == ALARM_TYPE_POWERYJ || data.alarmtype == ALARM_TYPE_POWERBJ)
	{
		strRst += _T("，");
		_stprintf_s(buf, _T("动作杆伸缩方向:%s"), data.offset == 0 ? "拉入" : "伸出");
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("预警/报警起始点数:%d"), data.gap);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("预警/报警结束点数:%d"), data.std);
		strRst += buf;
	}
	else
	{
		strRst += _T("，");
		_stprintf_s(buf, _T("偏移值:%.2fmm"), data.offset / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("缺口值:%.2fmm"), data.gap / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("标准值:%.2fmm"), data.std / 100.0);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("图像长度:%d"), data.imglen);
		strRst += buf;
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StActionInfo& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.collecttime;
	TIME tm = timeopt::Unix2SysTime(data.collecttime);
	_stprintf_s(buf, _T("图像采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	tm = timeopt::Unix2SysTime(data.actioncttime);
	_stprintf_s(buf, _T("道岔动作时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("定反位:");
	if (data.fixorinvert == 0)
		strRst += _T("定位");
	else
		strRst += _T("反位");

	strRst += _T("，");
	strRst += _T("左右偏标志:");
	if (data.lrsign == 1)
		strRst += _T("左偏");
	else if (data.lrsign == 2)
		strRst += _T("右偏");
	else
		strRst += _T("无效");

	strRst += _T("，");
	_stprintf_s(buf, _T("偏移值:%.2fmm"), data.offset / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("缺口值:%.2fmm"), data.gap / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("标准值:%.2fmm"), data.std / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("图像长度:%d"), data.imglen);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("图像内容:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const StActionInfoRec& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.collecttime;
	TIME tm = timeopt::Unix2SysTime(data.collecttime);
	_stprintf_s(buf, _T("图像采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	tm = timeopt::Unix2SysTime(data.actioncttime);
	_stprintf_s(buf, _T("道岔动作时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("定反位:");
	if (data.fixorinvert == 0)
		strRst += _T("定位");
	else
		strRst += _T("反位");

	strRst += _T("，");
	strRst += _T("左右偏标志:");
	if (data.lrsign == 1)
		strRst += _T("左偏");
	else if (data.lrsign == 2)
		strRst += _T("右偏");
	else
		strRst += _T("无效");

	strRst += _T("，");
	_stprintf_s(buf, _T("偏移值:%.2fmm"), data.offset / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("缺口值:%.2fmm"), data.gap / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("标准值:%.2fmm"), data.std / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("图像长度:%d"), data.imglen);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StLastGapImgReq& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StLastGapImgRes& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("图像采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("定反位:");
	if (data.fixorinvert == 0)
		strRst += _T("定位");
	else
		strRst += _T("反位");

	strRst += _T("，");
	strRst += _T("左右偏标志:");
	if (data.lrsign == 1)
		strRst += _T("左偏");
	else if (data.lrsign == 2)
		strRst += _T("右偏");
	else
		strRst += _T("无效");

	strRst += _T("，");
	_stprintf_s(buf, _T("偏移值:%.2fmm"), data.offset / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("缺口值:%.2fmm"), data.gap / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("标准值:%.2fmm"), data.std / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("图像/视频长度:%d"), data.imglen);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("图像/视频内容:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const StImgListReq& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;
	strRst += _T(", 请求编号:") + to_string(data.resqid);

	strRst += _T("，");
	//CTime tm = data.begintime;
	TIME tm = timeopt::Unix2SysTime(data.begintime);
	_stprintf_s(buf, _T("开始时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	tm = timeopt::Unix2SysTime(data.endtime);
	_stprintf_s(buf, _T("结束时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("图像类型:");
	if (data.imgtype == 0)
		strRst += _T("静态图像列表");
	else if (data.imgtype == 2)
		strRst += _T("操纵后图像列表");
	else if (data.imgtype == 3)
		strRst += _T("过车时图像列表");
	else
		strRst += _T("所有类型");

	return strRst;
}

tstring Parse315Protocol::ToString(const StImgListRes& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;
	strRst += _T(", 请求编号:") + to_string(data.resqid);

	strRst += _T("，");
	_stprintf_s(buf, _T("记录数:%d"), data.cnt);
	strRst += buf;

	//for (int i = 0; i < data.cnt; ++i)
	//{
	//	StFileRecord& frec = ((StFileRecord*)data.lprecord)[i];

	//	_stprintf_s(buf, _T("文件记录%d{"), i);
	//	strRst += buf;

	//	CTime tm = frec.time;
	//	_stprintf_s(buf, _T("时间:%04d-%02d-%02d %02d:%02d:%02d "),
	//		tm.GetYear(), tm.GetMonth(), tm.GetDay(), tm.GetHour(), tm.GetMinute(), tm.GetSecond());
	//	strRst += buf;

	//	_stprintf_s(buf, _T("定反位(0 定位，1 反位):%d "), frec.fixorinvert);
	//	strRst += buf;

	//	_stprintf_s(buf, _T("左右偏标志(0 无效，1 左偏，2 右偏):%d "), frec.lrsign);
	//	strRst += buf;

	//	_stprintf_s(buf, _T("图像类型(315协议中定义，0 静态图像列表， 1 操纵后图像列表， 2 过车时图像列表):%d "), frec.imgtype);
	//	strRst += buf;

	//	_stprintf_s(buf, _T("偏移值:%.2fmm "), frec.offset / 100.0);
	//	strRst += buf;

	//	_stprintf_s(buf, _T("缺口值:%.2fmm "), frec.gap / 100.0);
	//	strRst += buf;

	//	_stprintf_s(buf, _T("标准值:%.2fmm "), frec.std / 100.0);
	//	strRst += buf;

	//	_stprintf_s(buf, _T("图像长度:%d"), frec.imglen);
	//	strRst += buf;

	//	strRst += _T("} ");
	//}

	return strRst;
}

tstring Parse315Protocol::ToString(const StImgInfoReq& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("图像类型:");
	if (data.imgtype == 0)
		strRst += _T("静态图像列表");
	else if (data.imgtype == 2)
		strRst += _T("操纵后图像列表");
	else if (data.imgtype == 3)
		strRst += _T("过车时图像列表");
	else
		strRst += _T("所有类型");

	return strRst;
}

tstring Parse315Protocol::ToString(const StImgInfoRes& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("图像类型:");
	if (data.imgtype == 0)
		strRst += _T("静态图像");
	else if (data.imgtype == 2)
		strRst += _T("操纵后图像");
	else if (data.imgtype == 3)
		strRst += _T("过车时图像");
	else
		strRst += _T(" ");

	strRst += _T("，");
	strRst += _T("定反位:");
	if (data.fixorinvert == 0)
		strRst += _T("定位");
	else
		strRst += _T("反位");

	strRst += _T("，");
	strRst += _T("左右偏标志:");
	if (data.lrsign == 1)
		strRst += _T("左偏");
	else if (data.lrsign == 2)
		strRst += _T("右偏");
	else
		strRst += _T("无效");

	strRst += _T("，");
	_stprintf_s(buf, _T("偏移值:%.2fmm"), data.offset / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("缺口值:%.2fmm"), data.gap / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("标准值:%.2fmm"), data.std / 100.0);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("图像长度:%d"), data.imglen);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("图像内容:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const StVedioListReq& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("视频类型:");
	if (data.vediotype == 1)
		strRst += _T("道岔动作视频");
	else if (data.vediotype == 2)
		strRst += _T("过车视频");
	else
		strRst += _T(" ");
	strRst += _T(", 请求编号:") + to_string(data.resqid);

	strRst += _T("，");
	//CTime tm = data.begintime;
	TIME tm = timeopt::Unix2SysTime(data.begintime);
	_stprintf_s(buf, _T("开始时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	tm = timeopt::Unix2SysTime(data.endtime);
	_stprintf_s(buf, _T("结束时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StVedioListRes& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("视频类型:");
	if (data.vediotype == 1)
		strRst += _T("道岔动作视频");
	else if (data.vediotype == 2)
		strRst += _T("过车视频");
	else
		strRst += _T(" ");
	strRst += _T(", 请求编号:") + to_string(data.resqid);

	strRst += _T("，");
	_stprintf_s(buf, _T("记录数:%d \r\n"), data.cnt);
	strRst += buf;

	for (int i = 0; i < data.cnt; ++i)
	{
		StVedioRecord& vrec = ((StVedioRecord*)data.lprecord)[i];

		_stprintf_s(buf, _T("视频记录%d{"), i);
		strRst += buf;

		//CTime tm = vrec.time;
		TIME tm = timeopt::Unix2SysTime(vrec.time);
		_stprintf_s(buf, _T("开始录制时间:%04d-%02d-%02d %02d:%02d:%02d "),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		_stprintf_s(buf, _T("录制前定反位(0 定位，1 反位):%d "), vrec.fixorinvert1);
		strRst += buf;

		_stprintf_s(buf, _T("录制后定反位(0 定位，1 反位):%d "), vrec.fixorinvert2);
		strRst += buf;

		_stprintf_s(buf, _T("视频长度:%d "), vrec.len);
		strRst += buf;

		strRst += _T("} \r\n");
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StVedioFileReq& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("视频类型:");
	if (data.vediotype == 1)
		strRst += _T("道岔动作视频");
	else if (data.vediotype == 2)
		strRst += _T("过车视频");
	else
		strRst += _T(" ");

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("开始时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	return strRst;
}

template<typename T>
tstring Parse315Protocol::ToString(const T* data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data->sid);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("视频类型:");
	if (data->vediotype == 1)
		strRst += _T("道岔动作视频");
	else if (data->vediotype == 2)
		strRst += _T("过车视频");
	else
		strRst += _T(" ");

	strRst += _T("，");
	//CTime tm = data->time;
	TIME tm = timeopt::Unix2SysTime(data->time);
	_stprintf_s(buf, _T("开始时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("录制前定反位:");
	if (data->fixorinvert1 == 0)
		strRst += _T("定位");
	else
		strRst += _T("反位");

	strRst += _T("，");
	strRst += _T("录制后定反位:");
	if (data->fixorinvert2 == 0)
		strRst += _T("定位");
	else
		strRst += _T("反位");

	strRst += _T("，");
	_stprintf_s(buf, _T("视频时长:%d"), data->timelen);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("视频长度:%d"), data->len);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("总包数:%d"), data->packcnt);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("本包序号:%d"), data->curpackid);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("本包内容长:%d"), data->datalen);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("本包内容:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const St1DQJInfo& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("开关量状态:");
	if (data.status == 1)
		strRst += _T("转辙机启动扳动");
	else if (data.status == 2)
		strRst += _T("转辙机扳动结束");
	else if (data.status == 3)
		strRst += _T("道岔区段有车");
	else if (data.status == 4)
		strRst += _T("道岔区段车出清");
	else
		strRst += _T(" ");

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StOilPreCurve& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("采集频率:%d"), data.collectfreq);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("定反位操作方向:");
	strRst += GetOptDirDesc(data.direct);

	strRst += _T("，");
	_stprintf_s(buf, _T("曲线条数:%d"), data.cnt);
	strRst += buf;

	BYTE* cbFill = (BYTE*)&data.filldata;

	int nVer = (cbFill[1] == 0xFF ? 2015 : (cbFill[1] + 2000));
	for (int i = 0; i < data.cnt; ++i)
	{
		StCurve& cdata = ((StCurve*)data.lpdata)[i];

		strRst += _T("，");
		_stprintf_s(buf, _T("曲线数据[%d]{"), i);
		strRst += buf;

		strRst += _T("曲线类型:");
		strRst += GetCurveTypeDesc(cdata.type, nVer);

		strRst += _T("，");
		_stprintf_s(buf, _T("本条曲线数据长度:%d"), cdata.len);
		strRst += buf;

		strRst += _T("，");
		strRst += _T("曲线数据:{...}");

		strRst += _T("}");
	}


	return strRst;
}

tstring Parse315Protocol::ToString(const StOilPreCurveRec& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("采集频率:%d"), data.collectfreq);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("定反位操作方向:");
	strRst += GetOptDirDesc(data.direct);

	strRst += _T("，");
	_stprintf_s(buf, _T("曲线条数:%d"), data.cnt);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StOilLevelInfo& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机个数:%d"), data.cnt);
	strRst += buf;

	strRst += "\r\n";

	for (int i = 0; i < data.cnt; ++i)
	{
		StSdataRecord& srec = ((StSdataRecord*)data.lpdata)[i];

		strRst += _T("，");
		_stprintf_s(buf, _T("转辙机数据记录%d{"), i);
		strRst += buf;

		_stprintf_s(buf, _T("转辙机ID:%d"), srec.sid);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("油位:%d"), srec.oillevel);
		strRst += buf;

		strRst += _T("，");
		//CTime tm = srec.oiltime;
		TIME tm = timeopt::Unix2SysTime(srec.oiltime);
		_stprintf_s(buf, _T("油位时间:%04d-%02d-%02d %02d:%02d:%02d"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		strRst += _T("，");
		strRst += _T("摄像头状态:");
		if (srec.camerastate == 0)
			strRst += _T("正常");
		else if (srec.camerastate == 1)
			strRst += _T("故障");
		else if (srec.camerastate == 0xFF)
			strRst += _T("不监测");
		else
			strRst += _T(" ");

		strRst += _T("，");
		tm = timeopt::Unix2SysTime(srec.camtime);
		_stprintf_s(buf, _T("摄像头采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("采集设备温度:%0.2f"), srec.temperature / 100.0);
		strRst += buf;

		strRst += _T("，");
		tm = timeopt::Unix2SysTime(srec.temptime);
		_stprintf_s(buf, _T("温度采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("采集设备湿度:%0.2f"), srec.humidity / 100.0);
		strRst += buf;

		strRst += _T("，");
		tm = timeopt::Unix2SysTime(srec.humtime);
		_stprintf_s(buf, _T("湿度采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		strRst += _T("}");

		strRst += "\r\n";
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StParamSet& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	strRst += _T("命令类型:");
	if (data.cmdtype == 1)
		strRst += _T("获取");
	else if (data.cmdtype == 2)
		strRst += _T("设置");
	else
		strRst += _T(" ");

	strRst += _T("，");
	strRst += _T("参数类型:");
	if (data.paramtype == 1)
		strRst += _T("报警参数");
	else if (data.paramtype == 2)
		strRst += _T("预警参数");
	else
		strRst += _T(" ");

	strRst += _T("，");
	strRst += _T("参数内容:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const StRealCtrlReq& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("定反位:");
	if (data.fixorinvert == 0)
		strRst += _T("定位");
	else
		strRst += _T("反位");

	switch (data.cmdtype)
	{
	case 0x01:
	{
		strRst += _T("，");
		strRst += _T("命令类型:请求码流");

		StRealReqStream& rreq = *(StRealReqStream*)data.lpdata;

		strRst += _T("，");
		_stprintf_s(buf, _T("帧率:%d"), rreq.rate);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("图像宽度:%d"), rreq.width);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("图像高度:%d"), rreq.heigh);
		strRst += buf;

		break;
	}
	case 0x02:
	{
		strRst += _T("，");
		strRst += _T("命令类型:开始");
		break;
	}
	case 0x04:
	{
		strRst += _T("，");
		strRst += _T("命令类型:停止");
		break;
	}
	case 0x05:
	{
		strRst += _T("，");
		strRst += _T("命令类型:设置帧率");

		StRealSetRate& rrate = *(StRealSetRate*)data.lpdata;

		strRst += _T("，");
		_stprintf_s(buf, _T("帧率:%d"), rrate.rate);
		strRst += buf;

		break;
	}
	case 0x06:
	{
		strRst += _T("，");
		strRst += _T("命令类型:设置图像大小");

		StRealSetSize& rsize = *(StRealSetSize*)data.lpdata;

		strRst += _T("，");
		_stprintf_s(buf, _T("图像宽度:%d"), rsize.width);
		strRst += buf;

		strRst += _T("，");
		_stprintf_s(buf, _T("图像高度:%d"), rsize.heigh);
		strRst += buf;

		break;
	}
	default:
		break;
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StRealCtrlRes& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	switch (data.result)
	{
	case 0x00:
	{
		strRst += _T("，");
		strRst += _T("响应结果:失败");

		if (*(LPBYTE)data.lpdata == 1)
		{
			strRst += _T("，");
			strRst += _T("失败原因: 摄像头正在拍照，请5s后再直播");
		}
		else
		{
			strRst += _T("，");
			strRst += _T("失败原因: 直播失败");
		}

		break;
	}
	case 0x01:
	{
		strRst += _T("，");
		strRst += _T("响应结果:成功");
		break;
	}
	case 0x02:
	{
		strRst += _T("，");
		strRst += _T("响应结果:已有用户建立连接");

		DWORD ip = *(LPDWORD)data.lpdata;

		IN_ADDR addr;
		addr.S_un.S_addr = ip;

		strRst += _T("，");
		_stprintf_s(buf, _T("连接用户IP:%d.%d.%d.%d"), addr.S_un.S_un_b.s_b1, addr.S_un.S_un_b.s_b2, addr.S_un.S_un_b.s_b3, addr.S_un.S_un_b.s_b4);
		strRst += buf;

		break;
	}
	default:
		break;
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StRealStream& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("实时码流包序号:%d"), data.packid);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("实时码流包长度:%d"), data.packlen);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("实时码流包数据:{...}");

	return strRst;
}


BOOL Parse315Protocol::CheckPackData(StFrame& data, LPVOID buf, int len, int dir)
{
	LPVOID  pos = buf;
	int pklen = 0;
	if (!Parse315Protocol::GetFrameData(pos, len, pklen)
		|| pos != buf
		|| pklen != len)
	{
		return FALSE;
	}


	return Parse315Protocol::Parse(data, buf, len, dir);
}

BOOL Parse315Protocol::Parse(StElecCurve& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 1 + 1 + 4 + 4 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.cnt > len)
		return FALSE;

	BOOL failed = FALSE;
	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StElecCurveData);
		data.lpdata = new BYTE[sz];
		ZeroMemory(data.lpdata, sz);

		for (int i = 0; i < data.cnt; ++i)
		{
			StElecCurveData& curv = ((StElecCurveData*)data.lpdata)[i];

			sz = 1 + 4 + 2 + 2;
			szcnt += sz;
			if (szcnt > len)
			{
				failed = TRUE;
				break;
			}
			memcpy_s(&curv, sz, pos, sz);
			pos += sz;

			if (curv.datalen != 0 && curv.datalen == curv.datacnt * 2)
			{
				sz = curv.datalen;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = TRUE;
					break;
				}
				curv.lpdata = new BYTE[sz];
				memcpy_s(curv.lpdata, sz, pos, sz);
				pos += sz;
			}

		}
	}

	if (failed || pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

BOOL Parse315Protocol::Release(StElecCurve& data)
{
	if (data.cnt == 0 || data.lpdata == NULL)
		return TRUE;

	for (int i = 0; i < data.cnt; ++i)
	{
		StElecCurveData& curv = ((StElecCurveData*)data.lpdata)[i];

		if (curv.datalen != 0 && curv.lpdata != NULL)
			delete[] curv.lpdata;
	}

	delete[] data.lpdata;

	return TRUE;
}

// BOOL Parse315Protocol::Release(StNewPowerNotify& data)
// {
// 	//delete[]data.;
// 
// 	return TRUE;
// 
// }


BOOL Parse315Protocol::Release(StStaticPowerList& data)
{
	if (data.zzjcnt == 0 || data.lprecord == NULL)
		return TRUE;

	delete[]data.lprecord;

	return TRUE;
}

BOOL Parse315Protocol::Parse(StElecCurveRec& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2 + 4 + 1 + 1 + 4 + 4 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (pos != (LPBYTE)buf + len)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL Parse315Protocol::Parse(StStaticPowerList& data, LPVOID buf, int len)
{
	ZeroMemory(&data, sizeof(data));

	int szcnt = 0;
	LPBYTE pos = (LPBYTE)buf;

	size_t sz = 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return FALSE;
	memcpy_s(&data, sz, pos, sz);
	pos += sz;

	if (data.zzjcnt != 0)
	{
		sz = data.zzjcnt * sizeof(StStaticPowerData);
		szcnt += sz;
		if (szcnt > len)
			return FALSE;
		data.lprecord = new BYTE[sz];
		memcpy_s(data.lprecord, sz, pos, sz);
		pos += sz;
	}

	if (pos != (LPBYTE)buf + len)
	{
		Parse315Protocol::Release(data);
		return FALSE;
	}
	return TRUE;
}

tstring Parse315Protocol::ToString(const StElecCurve& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("采集频率:%d"), data.freq);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("方向:%s"), GetOptDirDesc(data.dir).c_str());
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("动作次数:%d"), data.acttime);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("曲线条数:%d"), data.cnt);
	strRst += buf;

	if (data.cnt > 0 && data.lpdata != NULL)
	{
		for (int i = 0; i < data.cnt; ++i)
		{
			StElecCurveData& curv = ((StElecCurveData*)data.lpdata)[i];

			strRst += _T("，");
			_stprintf_s(buf, _T("曲线%d{"), i);
			strRst += buf;

			_stprintf_s(buf, _T("曲线类型:%s"), GetElecCurveTypeDesc(curv.type).c_str());
			strRst += buf;

			strRst += _T("，");
			_stprintf_s(buf, _T("曲线数据点数:%d"), curv.datacnt);
			strRst += buf;

			strRst += _T("}");
		}
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StElecCurveRec& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("采集频率:%d"), data.freq);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("方向:%s"), GetOptDirDesc(data.dir).c_str());
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("动作次数:%d"), data.acttime);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("曲线条数:%d"), data.cnt);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StPowerFileListF2* pPowerFileList, int nItemCount)
{
	tstring strRst = _T("");
	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("文件数量:%d\n"), pPowerFileList->nListCount);
	strRst += buf;

	for (int i = 0; i < pPowerFileList->nListCount; ++i)
	{
		//StStaticPowerData& rec = ((StStaticPowerData*)data.lprecord)[i];


		_stprintf_s(buf, _T("转辙机ID:%d,"), pPowerFileList->list[i].sid);
		strRst += buf;

		_stprintf_s(buf, _T("数据类型:%d,"), pPowerFileList->list[i].cbDataType);
		strRst += buf;

		//strRst += " 检测对象类型:" + GetAcqDescByAcqObjType((ACQ_OBJ_TYPE)pPowerFileList->list[i].acqObjType);

		//CTime tm = pPowerFileList->list[i].time;
		TIME tm = timeopt::Unix2SysTime(pPowerFileList->list[i].time);
		_stprintf_s(buf, _T(",开始时间:%04d-%02d-%02d %02d:%02d:%02d\n"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;
	}
	return strRst;
}
tstring Parse315Protocol::ToString(const StPowerFileData& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，采集对象类型:");
	//strRst += GetAcqDescByAcqObjType((ACQ_OBJ_TYPE)data.acqObjType);

	strRst += _T("，");
	_stprintf_s(buf, _T("结果(0：成功 1：文件不存在 2：文件被占用):%d"), data.result);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("文件总长度:%d"), data.fileLen);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("总包数:%d"), data.pakAll);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("包序号:%d"), data.pakNo);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("本包包长:%d\n"), data.pakLen);
	strRst += buf;

	strRst += _T("，");
	strRst += (char*)data.dataInfo;

	return strRst;
}
//
//tstring Parse315Protocol::ToString(const StNewPowerNotify& data)
//{
//	tstring strRst = _T("");
//
//	TCHAR buf[256] = { 0 };
//
//	//_stprintf_s(buf, _T("转辙机数量:%d\n"), data.zzjcnt);
//	//strRst += buf;
//
//	//if (data.zzjcnt > 0 && data.lprecord != NULL)
//	//{
//	//	for (int i = 0; i < data.zzjcnt; ++i)
//		{
//			//StStaticPowerData& rec = ((StStaticPowerData*)data.lprecord)[i];
//
//
//			_stprintf_s(buf, _T("转辙机ID:%d,"), data.sid);
//			strRst += buf;
//
//			_stprintf_s(buf, _T("数据类型:%d,"), data.cbDataType);
//			strRst += buf;
//			
//			strRst += " 检测对象类型:" + GetAcqDescByAcqObjType((ACQ_OBJ_TYPE)data.acqObjType);
//
//			CTime tm = data.time;
//			_stprintf_s(buf, _T(",开始时间:%04d-%02d-%02d %02d:%02d:%02d\n"),
//				tm.GetYear(), tm.GetMonth(), tm.GetDay(), tm.GetHour(), tm.GetMinute(), tm.GetSecond());
//			strRst += buf;
//		}
//	//}
//
//	return strRst;
//}


tstring Parse315Protocol::ToString(const StStaticPowerList& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机数量:%d\n"), data.zzjcnt);
	strRst += buf;

	if (data.zzjcnt > 0 && data.lprecord != NULL)
	{
		for (int i = 0; i < data.zzjcnt; ++i)
		{
			StStaticPowerData& rec = ((StStaticPowerData*)data.lprecord)[i];

			strRst += _T("，");

			_stprintf_s(buf, _T("转辙机ID:%d"), rec.zzjid);
			strRst += buf;

			_stprintf_s(buf, _T("阻力值:%.1f"), (float)rec.powVal);
			strRst += buf;

			//CTime tm = rec.acqtime;
			TIME tm = timeopt::Unix2SysTime(rec.acqtime);
			_stprintf_s(buf, _T("采集时间:%04d-%02d-%02d %02d:%02d:%02d\n"),
				tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
			strRst += buf;
		}
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StPowerListReq& data)
{
	tstring strRst = _T("");
	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机ID:%d, "), data.sid);
	strRst += buf;

	strRst += _T("方向:");
	strRst += GetOptDirDesc(data.direction);
	//CTime tmStart = data.begintime;
	//CTime tmEnd = data.endtime;
	TIME tmStart = timeopt::Unix2SysTime(data.begintime);
	TIME tmEnd = timeopt::Unix2SysTime(data.endtime);

	_stprintf_s(buf, _T(", 查询时间范围:[%04d-%02d-%02d %02d:%02d:%02d] ~ [%04d-%02d-%02d %02d:%02d:%02d]"),
		tmStart.wYear, tmStart.wMonth, tmStart.wDay, tmStart.wHour, tmStart.wMinute, tmStart.wSecond,
		tmEnd.wYear, tmEnd.wMonth, tmEnd.wDay, tmEnd.wHour, tmEnd.wMinute, tmEnd.wSecond);

	strRst += buf;
	return strRst;
}

tstring Parse315Protocol::ToString(const StPowerListRes& data)
{
	tstring strRst = _T("");
	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机ID:%d, "), data.sid);
	strRst += buf;

	strRst += _T("方向:");
	strRst += GetOptDirDesc(data.direction);


	_stprintf_s(buf, _T(",数目:%d\n"), data.cnt);
	strRst += buf;

	StPowerRecord* pRecord = (StPowerRecord*)data.lprecord;
	for (WORD i = 0; i < data.cnt; i++)
	{
		//CTime tm = pRecord[i].time;
		TIME tm = timeopt::Unix2SysTime(pRecord[i].time);
		_stprintf_s(buf, _T("[%d] 时间:%04d-%02d-%02d %02d:%02d:%02d, "), i + 1,
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		_stprintf_s(buf, _T("采集频率:%d, "), pRecord[i].acqfreq);
		strRst += buf;

		_stprintf_s(buf, _T("动作次数:%d, "), pRecord[i].movecount);
		strRst += buf;

		strRst += _T("方向:");
		strRst += GetOptDirDesc(pRecord[i].dir);

		_stprintf_s(buf, _T(", 计算结果:%d, "), pRecord[i].calresult);
		strRst += buf;

		_stprintf_s(buf, _T("曲线条数:%d\n"), pRecord[i].curvenum);
		strRst += buf;

	}
	return strRst;
}

tstring Parse315Protocol::ToString(const StPowerInfoReq& data)
{
	tstring strRst = _T("");

	return strRst;
}

tstring Parse315Protocol::ToString(const StPowerInfoRes& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };
	_stprintf_s(buf, _T("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += _T("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("采集频率:%d"), data.acqfreq);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("动作次数:%d"), data.movecount);
	strRst += buf;

	strRst += _T("，");
	_stprintf_s(buf, _T("计算结果:%d"), data.calresult);
	strRst += buf;


	strRst += _T("，");
	strRst += _T("定反位操作方向:");
	strRst += GetOptDirDesc(data.direction);

	strRst += _T("，");
	_stprintf_s(buf, _T("曲线条数:%d"), data.cnt);
	strRst += buf;

	strRst += _T("，");
	strRst += _T("曲线数据:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const StManualOilingRes& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机ID:%d, "), data.sid);
	strRst += buf;

	strRst += _T("定反位:");
	if (data.fixorinvert == 0)
		strRst += _T("定位,");
	else
		strRst += _T("反位, ");
	strRst += _T("结果:");
	if (data.result == 0)
		strRst += _T("成功");
	if (data.result == 1)
		strRst += _T("没有找到设备");
	if (data.result == 2)
		strRst += _T("不支持手动加油功能");

	return strRst;
}
tstring Parse315Protocol::ToString(const StOilingResultNotify& data)
{
	tstring strRst = _T("");

	TCHAR buf[256] = { 0 };

	_stprintf_s(buf, _T("转辙机ID:%d, "), data.sid);
	strRst += buf;

	strRst += _T("定反位:");
	if (data.fixorinvert == 0)
		strRst += _T("定位,");
	else
		strRst += _T("反位, ");
	strRst += _T("加油方式:") + GetJiaYouTypeDesc(data.type);
	strRst += _T(", 加油结果:");
	if (data.status == 2)
		strRst += _T("加油失败,");
	else if (data.status == 1)
		strRst += _T("加油成功,");
	else
		strRst += _T("未知, ");

	strRst += _T(", 异常码:");
	if (data.failCode == 0)
		strRst += _T("无问题, ");
	else if (data.failCode == 1)
		strRst += _T("加油管堵塞 , ");
	else if (data.failCode == 2)
		strRst += _T("加油管破损 , ");
	else if (data.failCode == 3)
		strRst += _T("加油泵异常 , ");
	else if (data.failCode == 4)
		strRst += _T("无压力传感器 , ");
	else if (data.failCode == 5)
		strRst += _T("压力值比较小，可能存在漏油");

	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	_stprintf_s(buf, _T(" \n加油时间:%04d-%02d-%02d %02d:%02d:%02d\n"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	//CTime tmTrigger = data.triggerTime;
	TIME tmTrigger = timeopt::Unix2SysTime(data.triggerTime);
	_stprintf_s(buf, _T("触发时间:%04d-%02d-%02d %02d:%02d:%02d\n"),
		tmTrigger.wYear, tmTrigger.wMonth, tmTrigger.wDay, tmTrigger.wHour, tmTrigger.wMinute, tmTrigger.wSecond);
	strRst += buf;

	strRst += _T("持续时间:") + to_string(data.timeLen);

	return strRst;
}

BYTE Parse315Protocol::GetOptDirIndex(tstring direct)//根据操作方向类型描述文本获取对应值
{
	if (direct == _T("定到反"))
		return DIRECT_DW_TO_FW;
	else if (direct == _T("反到定"))
		return DIRECT_FW_TO_DW;
	else if (direct == _T("定到定"))
		return DIRECT_DW_TO_DW;
	else if (direct == _T("反到反"))
		return DIRECT_FW_TO_FW;
	else if (direct == _T("定位到故障位"))
		return DIRECT_DW_TO_GZ;
	else if (direct == _T("反位到故障位"))
		return DIRECT_FW_TO_GZ;
	else if (direct == _T("故障到定位"))
		return DIRECT_GZ_TO_DW;
	else if (direct == _T("故障到反位"))
		return DIRECT_GZ_TO_FW;
	else if (direct == _T("故障到故障位"))
		return DIRECT_GZ_TO_GZ;
	//else if (direct == _T("无效"))                     

	return 0xFF;
}

//根据加油类型值获取对应描述文本
tstring Parse315Protocol::GetJiaYouTypeDesc(BYTE nType)
{
	switch (nType)
	{
	case 0:// JIAYOU_TYPE::By_Interval:
		return "周期方式";
	case 1://JIAYOU_TYPE::By_MoveTime:
		return "扳动次数方式";
	case 4://JIAYOU_TYPE::By_Remote:
		return "远程手动加油";
	case 3://JIAYOU_TYPE::By_Manual:
		return "手动方式";
	case 2://JIAYOU_TYPE::By_Power:
		return "阻力方式";
	}
	return "";
}
tstring Parse315Protocol::GetOptDirDesc(int direct)
{
	switch (direct)
	{
	case DIRECT_DW_TO_FW:
		return _T("定到反");
	case DIRECT_FW_TO_DW:
		return _T("反到定");
	case DIRECT_DW_TO_DW:
		return _T("定到定");
	case DIRECT_FW_TO_FW:
		return _T("反到反");
	case DIRECT_DW_TO_GZ:
		return _T("定位到故障位");
	case DIRECT_FW_TO_GZ:
		return _T("反位到故障位");
	case DIRECT_GZ_TO_DW:
		return _T("故障到定位");
	case DIRECT_GZ_TO_FW:
		return _T("故障到反位");
	case DIRECT_GZ_TO_GZ:
		return _T("故障到故障位");
	case 9:
		return _T("定位过车");
	case 10:
		return _T("反位过车");
	default:
		return _T("无效[") + to_string(direct) + "]";
		break;
	}
	return _T("");
}

tstring Parse315Protocol::GetOptDirDesc(eMOVE_DIRECT direct)
{
	switch (direct)
	{
	case eMOVE_DIRECT::fix2invert:
		return _T("定到反");
	case eMOVE_DIRECT::invert2fix:
		return _T("反到定");
	case eMOVE_DIRECT::fix2fix:
		return _T("定到定");
	case eMOVE_DIRECT::invert2invert:
		return _T("反到反");
	case eMOVE_DIRECT::fix2fault:
		return _T("定位到故障位");
	case eMOVE_DIRECT::invert2fault:
		return _T("反位到故障位");
	case eMOVE_DIRECT::fault2fix:
		return _T("故障到定位");
	case eMOVE_DIRECT::fault2invert:
		return _T("故障到反位");
	case eMOVE_DIRECT::fault2fault:
		return _T("故障到故障位");
	case eMOVE_DIRECT::unknow:
		return _T("无效");
		break;
	default:
		ASSERT(FALSE);
	}
	return _T("");
}

tstring Parse315Protocol::GetCurveTypeDesc(int CurveType, int nVer)//根据曲线类型值获取对应描述文本
{
	if (nVer < 2023)
	{
		switch (CurveType)
		{
		case 0x00: return _T("左油压");
		case 0x01: return _T("右油压");
		case 0x02: return _T("道岔动作电流曲线");
		case 0x03: return _T("道岔动作A相电流曲线");
		case 0x04: return _T("道岔动作B相电流曲线");
		case 0x05: return _T("道岔动作C相电流曲线");
		case 0x06: return _T("道岔总功率曲线");
		case 0x07: return _T("道岔动作A相电压曲线");
		case 0x08: return _T("道岔动作B相电压曲线");
		case 0x09: return _T("道岔动作C相电压曲线");
		case 0x0A: return _T("道岔阻力曲线");
		default:
			break;
		}
	}
	else
	{
		switch (CurveType)
		{
		case 0x00: return _T("左油压");
		case 0x01: return _T("右油压");
		case 0x02: return _T("电流曲线");
		case 0x03: return _T("道岔阻力曲线");
		case 0x04: return _T("外锁闭装置锁闭力曲线");
		case 0x05: return _T("过车时缺口曲线");
		default:
			break;
		}

	}
	return _T("");
}

tstring Parse315Protocol::GetAlarmTypeDesc(int Alarmtype, int nVer)//根据报警类型值获取对应描述文本
{
	if (nVer < 2023)
	{
		switch (Alarmtype)
		{
		case ALARM_TYPE_QKYJ:
			return _T("缺口预警及预警图像");
			break;
		case ALARM_TYPE_QKBJ:
			return _T("缺口报警及报警图像");
			break;
		case ALARM_TYPE_QKSBGZ:
			return _T("缺口采集设备故障");
			break;
		case ALARM_TYPE_TXWFSB:
			return _T("缺口图像无法识别报警");
			break;
		case ALARM_TYPE_GCKLGD:
			return _T("过车时框量过大报警及过车视频");
			break;
		case ALARM_TYPE_ZZJSBGZ:
			return _T("转辙机采集设备故障报警");
			break;
		case ALARM_TYPE_WDBJ:
			return _T("温度报警");
			break;
		case ALARM_TYPE_SDBJ:
			return _T("湿度报警");
			break;
		case ALARM_TYPE_YWYJ:
			return _T("油位预警");
			break;
		case ALARM_TYPE_YWBJ:
			return _T("油位报警");
			break;
		case ALARM_TYPE_POWERYJ:
			return _T("阻力预警");
			break;
		case ALARM_TYPE_POWERBJ:
			return _T("阻力报警");
			break;
		case ALARM_TYPE_QKYJHF:
			return _T("缺口预警恢复及图像");
			break;
		case ALARM_TYPE_QKBJHF:
			return _T("缺口报警恢复及图像");
			break;
		case ALARM_TYPE_QKSBGZHF:
			return _T("缺口采集设备故障恢复及图像");
			break;
		case ALARM_TYPE_TXWFSBHF:
			return _T("缺口图像无法识别报警恢复及图像");
			break;
		case ALARM_TYPE_ZZJSBGZHF:
			return _T("转辙机采集设备故障报警恢复");
			break;
		case ALARM_TYPE_GCKLGDHF:
			return _T("过车时框量过大报警恢复及过车视频");
			break;
		case ALARM_TYPE_WDBJHF:
			return _T("温度报警恢复");
			break;
		case ALARM_TYPE_SDBJHF:
			return _T("湿度报警恢复");
			break;
		case ALARM_TYPE_YWYJHF:
			return _T("油位预警恢复");
			break;
		case ALARM_TYPE_YWBJHF:
			return _T("油位报警恢复");
			break;
		case ALARM_TYPE_POWERYJHF:
			return _T("阻力预警恢复");
			break;
		case ALARM_TYPE_POWERBJHF:
			return _T("阻力报警恢复");
			break;
		default:
			return _T("未知");
			break;
		}
	}
	else
	{
		switch (Alarmtype)
		{
		case 1:
			return _T("转换后缺口预警及预警图像");
			break;
		case 2:
			return _T("转换后缺口报警及报警图像");
			break;
		case 3:
			return _T("缺口采集设备故障");
			break;
		case 4:
			return _T("缺口图像无法识别报警");
			break;
		case 5:
			return _T("过车时旷量过大报警及过车视频");
			break;
		case 6:
			return _T("转辙机采集设备故障报警");
			break;
		case 7:
			return _T("温度报警");
			break;
		case 8:
			return _T("湿度报警");
			break;
		case 9:
			return _T("油位预警");
			break;
		case 10:
			return _T("油位报警");
			break;
		case 11:
			return _T("油压预警");
			break;
		case 12:
			return _T("油压报警");
			break;
		case 13:
			return _T("道岔转换阻力超限报警");
			break;
		case 14:
			return _T("外锁闭装置锁闭力超限报警");
			break;
		case 15:
			return _T("过车时缺口值");
			break;
		case 16:
			return _T("静态缺口预警");
			break;
		case 17:
			return _T("静态缺口报警");
			break;
		case 101:
			return _T("转换后缺口预警恢复及图像");
			break;
		case 102:
			return _T("转换后缺口报警恢复及图像");
			break;
		case 103:
			return _T("缺口采集设备故障恢复及图像");
			break;
		case 104:
			return _T("缺口图像无法识别报警恢复及图像");
			break;
		case 105:
			return _T("转辙机采集设备故障报警恢复");
			break;
		case 106:
			return _T("过车时旷量过大报警恢复及过车视频");
			break;
		case 107:
			return _T("温度报警恢复");
			break;
		case 108:
			return _T("湿度报警恢复");
			break;
		case 109:
			return _T("油位预警恢复");
			break;
		case 110:
			return _T("油位报警恢复");
			break;
		case 111:
			return _T("油压预警恢复");
			break;
		case 112:
			return _T("油压报警恢复");
			break;
		case 113:
			return _T("道岔转换阻力报警恢复");
			break;
		case 114:
			return _T("外锁闭装置锁闭力超限报警恢复");
			break;
		case 115:
			return _T("过车时缺口报警恢复");
			break;
		case 116:
			return _T("静态缺口报警恢复");
			break;
		case 117:
			return _T("静态缺口报警恢复");
			break;
		default:
			return _T("未知");
			break;
		}
	}
	return _T("");
}

tstring Parse315Protocol::GetElecCurveTypeDesc(int ElecCurveType)
{
	switch (ElecCurveType)
	{
	case ELEC_CURVE_TYPE_DZXJNUAB:
		return _T("动作线电压Uab");
	case ELEC_CURVE_TYPE_DZXJNUBC:
		return _T("动作线电压Ubc");
	case ELEC_CURVE_TYPE_DZXJNUAC:
		return _T("动作线电压Uac");
	case ELEC_CURVE_TYPE_DZDLIA:
		return _T("动作电流Ia");
	case ELEC_CURVE_TYPE_DZDLIB:
		return _T("动作电流Ib");
	case ELEC_CURVE_TYPE_DZDLIC:
		return _T("动作电流Ic");
	case ELEC_CURVE_TYPE_GLYS:
		return _T("功率因素");
	case ELEC_CURVE_TYPE_ZYGGL:
		return _T("总有功功率");
	case ELEC_CURVE_TYPE_ZL:
		return _T("阻力");
	case ELEC_CURVE_TYPE_DZDYDW1:
		return _T("动作电压定位1");
	case ELEC_CURVE_TYPE_DZDYFW1:
		return _T("动作电压反位1");
	case ELEC_CURVE_TYPE_DZDYDW2:
		return _T("动作电压定位2");
	case ELEC_CURVE_TYPE_DZDYFW2:
		return _T("动作电压反位2");
	case ELEC_CURVE_TYPE_DZDLDW1:
		return _T("动作电流定位1");
	case ELEC_CURVE_TYPE_DZDLFW1:
		return _T("动作电流反位1");
	case ELEC_CURVE_TYPE_DZDLDW2:
		return _T("动作电流定位2");
	case ELEC_CURVE_TYPE_DZDLFW2:
		return _T("动作电流反位2");
	default:
		break;
	}
	return _T("");
}
