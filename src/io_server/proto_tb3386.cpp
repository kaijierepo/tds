#include "proto_tb3386.h"
#include "logger.h"
#include <cstdio>


bool Parse315Protocol::b26res4byte = false;
bool Parse315Protocol::b2Flen2byte = false;


uint32_t g_dw0x23ExCmdID = 0x00;	//道岔缺口扩展配置命令ID

bool CVedioParser::Parse(StVedioFrame& data, void*, int)
{
	return true;
}

bool CVedioParser::Unparse(StVedioFrame& data, vector<uint8_t>& buf, int& len)
{
	len = data.datalen + 5 + 4 + 4;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	uint8_t* pos = (uint8_t*)buf.data();
	memcpy(pos, &data, 5 + 4);
	pos += 5 + 4;

	if (data.datalen != 0)
	{
		StDataBasic* basic = (StDataBasic*)data.lpdata;

		vector<uint8_t> subbuf;
		int sublen = 0;
		bool success = false;

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
			return false;
		}

		if (sublen != data.datalen)
		{
			return false;
		}

		memcpy(pos, subbuf.data(), sublen);

		pos += sublen;
		//delete[] subbuf;

	}

	memcpy(pos, &data.ftail, 4);
	pos += 4;

	return true;
}

bool CVedioParser::Release(StVedioFrame& data)
{
	if (data.datalen == 0)
		return true;

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

	delete (uint8_t*)data.lpdata;

	return true;
}

bool CVedioParser::Release(StVedioFileStart& data)
{
	if (data.titlelen != 0)
		delete[] data.title;

	if (data.pathlen != 0)
		delete[] data.path;

	return true;
}

bool CVedioParser::Release(StVedioSetTitle& data)
{
	if (data.titlelen != 0)
		delete[] data.title;

	return true;
}

bool CVedioParser::Release(StVedioRealPlay& data)
{
	if (data.len != 0)
		delete[] (uint8_t*)data.lpdata;

	return true;
}

bool CVedioParser::Unparse(StVedioFileStart& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 1 + 2 + 2 + 2 + data.titlelen + data.pathlen;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	uint8_t* pos = (uint8_t*)buf.data();

	memcpy(pos, &data, 1 + 1 + 2 + 2);
	pos += 1 + 1 + 2 + 2;

	if (data.titlelen > 0)
	{
		memcpy(pos, data.title, data.titlelen);
		pos += data.titlelen;
	}

	memcpy(pos, &data.pathlen, 2);
	pos += 2;

	if (data.pathlen > 0)
	{
		memcpy(pos, data.path, data.pathlen);
		pos += data.pathlen;
	}

	return true;
}

bool CVedioParser::Unparse(StVedioFileStop& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 1;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool CVedioParser::Unparse(StVedioSetTitle& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 1 + 2 + data.titlelen;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	uint8_t* pos = (uint8_t*)buf.data();

	memcpy(pos, &data, 1 + 1 + 2);
	pos += 1 + 1 + 2;

	if (data.titlelen > 0)
	{
		memcpy(pos, data.title, data.titlelen);
		pos += data.titlelen;
	}

	return true;
}

bool CVedioParser::Unparse(StVedioRealCtrl& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 1 + 1 + 2 + 1 + 2 + 2;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool CVedioParser::Unparse(StVedioRealPlay& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 1 + 2 + 2 + data.len;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	uint8_t* pos = (uint8_t*)buf.data();

	memcpy(pos, &data, 1 + 1 + 2 + 2);
	pos += 1 + 1 + 2 + 2;

	if (data.len > 0)
	{
		memcpy(pos, data.lpdata, data.len);
		pos += data.len;
	}
	return true;
}


//buf指向数据内容 len数据内容的长度  返回data 和 最新位置的buf  void**只是为了方便返回最新位置
bool Parse315Protocol::ParseDataFrm(StFrame& data, void** buf, int len, int dir)
{
	uint8_t* pos = (uint8_t*)(*buf);
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
			return false;
		}
		break;
	}
	case (uint8_t)E_315_PROTOCOL_TYPE::OIL_BOX_VOLUME_0x53:
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
			return false;
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
			return false;
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
			return false;
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
			return false;
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
			return false;
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
				return false;
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
				return false;
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
				return false;
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
				return false;
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
				return false;
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
				return false;
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
				return false;
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
				return false;
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
			return false;
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
			return false;
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
			return false;
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
			return false;
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
			return false;
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
			return false;
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
				return false;
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
				return false;
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
			return false;
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
			return false;
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
	// 			return false;
	// 		}
	// 		break;
	// 	}
	case CMD_CODE_QUERY_POWER_FILE:
	{
		if ((data.datalen - 1) % sizeof(StPowerFileList) == 0)
		{
			//StPowerFileList* lpdata = new StPowerFileList[(data.datalen - 1) / sizeof(StPowerFileList)];
			StPowerFileListF2* lpdata = new StPowerFileListF2;
			lpdata->cmdid = *(uint8_t*)pos;
			lpdata->nListCount = (data.datalen - 1) / sizeof(StPowerFileList);
			lpdata->list = new StPowerFileList[lpdata->nListCount];
			memcpy(lpdata->list, pos + 1, data.datalen - 1);
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			return false;
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
				return false;
			}
		}
		else
		{
			StPowerFileDataResq* lpdata = new StPowerFileDataResq;
			size_t sz = sizeof(StPowerFileDataResq);
			memcpy(lpdata, pos, sz);
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
			//	return false;
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
			memcpy(lpdata, pos, data.datalen);
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			return false;
		}
		break;
	}
	case 0x52:
	{
		if (data.datalen == sizeof(StOilingResultNotify))
		{
			//StPowerFileList* lpdata = new StPowerFileList[(data.datalen - 1) / sizeof(StPowerFileList)];
			StOilingResultNotify* lpdata = new StOilingResultNotify;
			memcpy(lpdata, pos, data.datalen);
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			return false;
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
			return false;
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
			return false;
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
			return false;
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
			return false;
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
			return false;
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
			return false;
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
			return false;
		}
		break;
	}
	default:
		break;
	}
	}
	break;*/
}

bool Parse315Protocol::ParseDataFrmJson(StFrame& data, void** buf, int len, int dir)
{
	uint8_t* pos = (uint8_t*)(*buf);
	StDataBasic* basic = (StDataBasic*)pos;
	switch (basic->cmdid)
	{
	case CMD_CODE_VEDIOLIST://视频列表  //这里BYTE强转为char 小于0x7F没问题 超了就不对了 比如工况0x81
	{
		St315Json* lpdata = new St315Json;
		if (Parse315Protocol::Parse(*lpdata, pos, data.datalen, data.e_frmKind))
		{
			data.lpdata = lpdata;
			pos += data.datalen;
		}
		else
		{
			delete lpdata;
			return false;
		}
		break;
	}
	default:
		break;
	}
	*buf = pos;
	return true;
}

bool Parse315Protocol::Parse(St315Json& data, void* buf, int len, FRAME_KIND& kind)
{
	memset(&data, 0,  sizeof(data));
	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;
	bool failed = false;

	kind = RES;
	size_t sz = 1 + 2 + 4 + 1 + 4;  //命令码	1、 包序号 2、 预留	4、 帧内容类型	1、	 帧内容长度	4、 帧内容	N
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	data.lpContent = new char[len - szcnt];
	memcpy(data.lpContent, pos, len - szcnt);

	return true;
}

//将buf解析为StFrame, 内部包含包的类别 命令还是应答等
bool Parse315Protocol::Parse(StFrame& data, void* buf, int len, int dir)
{
	memset(&data, 0,  sizeof(data));
	data.e_frmKind = GENERAL;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 5 + 1 + 1 + 1 + 4;
	if (sz > (unsigned)len)
		return false;
	memcpy(&data, pos, sz);
	if (data.datalen == 0)
	{
		data.datalen = len - 16;
	}
	pos += sz;

	sz = 5 + 1 + 1 + 1 + 4 + data.datalen + 4;
	if (sz != (unsigned)len)//严格一点
		return false;

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
				return false;
			}
			break;
		}
		case FRAME_TYPE_DATA:
		{
			if (!ParseDataFrm(data, (void**)&pos, data.datalen, dir)) { //pos传地址用于在内部更新最新位置,失败了内部释放内存
				return false;
			};
			break;
		}
		case FRAME_TYPE_JSON://(char)0x3f:  //zgw json  视频url
		{
			if (!ParseDataFrmJson(data, (void**)&pos, data.datalen, dir)) { //pos传地址用于在内部更新最新位置,失败了内部释放内存
				return false;
			};
			break;
		}
		default:
			break;
		}
	}//if

	memcpy(&data.ftail, pos, 4);
	pos += 4;

	if (data.ftail != 0xFFFFFFFF || pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data, dir);
		return false;
	}

	return true;
}

bool Parse315Protocol::Parse(StAlarmListRes& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(S_315_ALARM_INFO);
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lprecord = new uint8_t[sz];
		memcpy(data.lprecord, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

/*
bool Parse315Protocol::Parse(StFrame& data, void* buf, int len, int dir)
{
	memset(&data, 0,  sizeof(data));

	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 5 + 1 + 1 + 1 + 4;
	if (sz > (unsigned)len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	sz = data.datalen + 5 + 1 + 1 + 1 + 4 + 4;
	if (sz > (unsigned)len)
		return false;

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
				return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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
						return false;
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

	memcpy(&data.ftail, pos, 4);
	pos += 4;

	if (data.ftail != 0xFFFFFFFF || pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data, dir);
		return false;
	}

	return true;
}
*/

bool Parse315Protocol::Unparse(StFrame& data, vector<uint8_t>& buf, int& len, uint8_t* cmdBuf, int cmdLen)
{
	//序列化后包的总长度。315头长度 + 命令域长度
	data.datalen = cmdLen;
	int unparselen = data.datalen + 5 + 1 + 1 + 1 + 4 + 4;
	//buf = new uint8_t[unparselen];
	//memset(buf, 0,  unparselen);
	buf.resize(unparselen);

	uint8_t* pos = (uint8_t*)buf.data();

	//拷贝315头信息
	memcpy(pos, &data, 5 + 1 + 1 + 1 + 4);
	pos += 5 + 1 + 1 + 1 + 4;

	if (data.datalen != 0)
	{
		memcpy(pos, cmdBuf, cmdLen);
		pos += cmdLen;
	}

	//拷贝尾 4字节 0xFF
	memcpy(pos, &data.ftail, 4);
	pos += 4;
	len = unparselen;
	return true;
}



bool Parse315Protocol::Unparse(StFrame& data, vector<uint8_t>& buf, int& len, int dir)
{
	int unparselen = data.datalen + 5 + 1 + 1 + 1 + 4 + 4;
	//buf = new uint8_t[unparselen];
	//memset(buf, 0,  unparselen);
	buf.resize(unparselen);

	uint8_t* pos = (uint8_t*)buf.data();

	memcpy(pos, &data, 5 + 1 + 1 + 1 + 4);
	pos += 5 + 1 + 1 + 1 + 4;

	if (data.datalen != 0)
	{
		if (data.lpdata == NULL)
		{
			//delete[] buf;
			return false;
		}

		vector<uint8_t> subbuf;
		int sublen = 0;
		bool success = false;

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
		case FRAME_TYPE_JSON:
		{
			StDataBasic* basic = (StDataBasic*)data.lpdata;
			if (dir == 0)
			{
				switch (basic->cmdid)
				{
				case CMD_CODE_VEDIOLIST:
				{
					success = Parse315Protocol::Unparse(*(St315Json*)data.lpdata, subbuf, sublen);
					break;
				}
				}
			}

			break;
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

			return false;
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

			return false;
		}

		memcpy(pos, subbuf.data(), sublen);
		pos += sublen;

		//if (subbuf)
		//{
		//	delete[] subbuf;
		//}
	}

	memcpy(pos, &data.ftail, 4);
	pos += 4;

	len = unparselen;
	return true;
}

bool Parse315Protocol::Unparse(StOilBoxVolumeResq& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 4;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}
bool Parse315Protocol::Unparse(StAlarmListReq& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 3;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::Unparse(St315Json& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 4 + 1 + 4 + data.frmLength;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	//memcpy(buf, &data, len);  
	int n0 = 12;// 1 + 2 + 4 + 1 + 4;
	memcpy(buf.data(), &data, n0);
	char* p = (char*)buf.data();
	memcpy(&(p[12]), (char*)data.lpContent, data.frmLength);
	return true;
}

//释放内存，dir: 数据包方向  0-微机监测->JHD  1-JHD->微机监测
bool Parse315Protocol::Release(StFrame& data, int dir)
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
			case (uint8_t)E_315_PROTOCOL_TYPE::OIL_BOX_VOLUME_0x53:
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
					delete[] (uint8_t*)lpdata->dataInfo;
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

		delete (uint8_t*)data.lpdata;
	}
	return true;
}

bool Parse315Protocol::Release(StAlarmListRes& data)
{
	if (data.cnt == 0 || data.lprecord == NULL)
		return true;

	delete[] (uint8_t*)data.lprecord;

	return true;
}

bool Parse315Protocol::Release(St315Json& data)
{
	if (data.frmLength == 0 || data.lpContent == NULL)
		return true;

	delete[] data.lpContent;

	return true;
}

uint8_t Parse315Protocol::GetCommandId(const StFrame& data)
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
bool Parse315Protocol::GetFrameData(void*& buf, int& size, int& len)
{
	int nMinLen = 5 + 1 + 1 + 1 + 4 + 4;

	if (buf == NULL || size < nMinLen)
		return false;

	uint8_t* pos = (uint8_t*)buf;

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
		return false;

	int datalen = *(int*)(pos + 5 + 1 + 1 + 1);
	if (datalen == 0)
	{
		//TRACE("报文长度为0\n");
		datalen = size - nMinLen;
	}
	if (datalen < 0 || cnt < nMinLen + datalen)
		return false;

	uint32_t tail = *(uint32_t*)(pos + 5 + 1 + 1 + 1 + 4 + datalen);
	if (tail != FRAME_TAIL_315)
		return false;

	buf = pos;
	size = cnt;
	len = nMinLen + datalen;

	return true;
}

//心跳包 时间4B+0xFF+0xFF+0xFF
bool Parse315Protocol::Parse(StHeartBeat315& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	if (len != 4 + 3)
		return false;

	memcpy(&data, buf, 4 + 3);

	return true;
}

bool Parse315Protocol::Unparse(StHeartBeat315& data, vector<uint8_t>& buf, int& len)
{
	len = 4 + 3;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::Unparse(StDataBasic& data, vector<uint8_t>& buf, int& len)
{
	len = 1;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}


bool Parse315Protocol::Parse(StOilBoxVolumeData& data, void* buf, int len, FRAME_KIND& kind)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;
	bool failed = false;

	data.cmdid = *pos;
	//判断包的类型 命令包还是应答包
	if (7 == len && data.cmdid == (uint8_t)E_315_PROTOCOL_TYPE::OIL_BOX_VOLUME_0x53)
	{
		pos += 5;
		data.v = *(uint16_t*)pos;
		pos + 2;
		kind = CMD;
		return true;
	}
	//应答包：帧头5B + 协议码1B + 数据版本1B + 帧类型1B + 帧内容长度4B + 命令码1B（0x23） + 总配置数(2B) + {转辙机配置i} + 帧尾4B
	//转辙机配置i:......
	kind = RES;
	size_t sz;
	{
		sz = 1 + 2;
		szcnt += sz;
		if (szcnt > len)
			return false;
		memcpy(&data, pos, sz);
		pos += sz;

		if (data.v * sizeof(StOilBoxVolume) > len)
			return false;

		if (data.v != 0)
		{
			sz = sizeof(StOilBoxVolume) * data.v;
			StOilBoxVolume* lpcfg = new StOilBoxVolume[data.v];
			memset(lpcfg, 0,  sz);
			memcpy(lpcfg, pos, sz);

			data.dataInfo = lpcfg;

			pos += sz;
		}
	}

	if (failed || pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}

	return true;
}



//道岔缺口配置信息 buf指向数据内容部分 len数据内容的长度
bool Parse315Protocol::Parse(StGapCfgRes& data, void* buf, int len, FRAME_KIND& kind)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;
	bool failed = false;

	data.cmdid = *pos;
	//判断包的类型 命令包还是应答包
	if (1 == len && (*pos == 0x23
		|| g_dw0x23ExCmdID > 0 && *pos == g_dw0x23ExCmdID))
	{
		pos += 1;
		kind = CMD;
		return true;
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
			return false;
		data.cfgcnt = 1;//1个文件
		//memcpy(&data, pos, sz);
		uint32_t dwFileLen = *(uint32_t*)(pos + 5);
		uint8_t* cbBuffer = new uint8_t[dwFileLen];
		pos += sz;
		memcpy(cbBuffer, pos, dwFileLen);
		data.lpcfg = cbBuffer;
		pos += dwFileLen;
	}
	else
	{
		sz = 1 + 2;
		szcnt += sz;
		if (szcnt > len)
			return false;
		memcpy(&data, pos, sz);
		pos += sz;

		if (data.cfgcnt > len)
			return false;

		if (data.cfgcnt != 0)
		{
			sz = sizeof(StSwitchCfg) * data.cfgcnt;
			StSwitchCfg* lpcfg = new StSwitchCfg[data.cfgcnt];
			memset(lpcfg, 0,  sz);

			for (int i = 0; i < data.cfgcnt; ++i)
			{
				StSwitchCfg& cfg = lpcfg[i];

				sz = 1;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = true;
					break;
				}
				memcpy(&cfg.nlen, pos, sz);
				pos += sz;

				if (cfg.nlen != 0)
				{
					sz = cfg.nlen;
					szcnt += sz;
					if (szcnt > len)
					{
						failed = true;
						break;
					}
					cfg.lpname = new char[sz];
					memcpy(cfg.lpname, pos, sz);
					pos += sz;
				}

				sz = 1;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = true;
					break;
				}
				memcpy(&cfg.stype, pos, sz);
				pos += sz;

				sz = 2;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = true;
					break;
				}
				memcpy(&cfg.sid, pos, sz);
				pos += sz;

				sz = 2;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = true;
					break;
				}
				memcpy(&cfg.cnt, pos, sz);
				pos += sz;

				if (cfg.cnt != 0)
				{
					sz = 2 * cfg.cnt;
					szcnt += sz;
					if (szcnt > len)
					{
						failed = true;
						break;
					}
					cfg.lpinfo = new uint16_t[cfg.cnt];
					memcpy(cfg.lpinfo, pos, sz);
					pos += sz;
				}
			}//for

			data.lpcfg = lpcfg;
		}
	}

	if (failed || pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}

	return true;
}
bool Parse315Protocol::Release(StOilBoxVolumeData& data)
{
	if (data.v == 0 || data.dataInfo == NULL)
		return true;


	delete[] (StOilBoxVolume*)data.dataInfo;

	return true;
}

bool Parse315Protocol::Release(StGapCfgRes& data)
{
	if (data.cfgcnt == 0 || data.lpcfg == NULL)
		return true;

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

	delete[] (StSwitchCfg*)data.lpcfg;

	return true;
}

//道岔缺口数值 buf指向数据内容部分 len数据内容的长度
bool Parse315Protocol::Parse(StGapValue& data, void* buf, int len, FRAME_KIND& kind)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	//判断包的类型 命令包还是应答包
	if (1 == len && *pos == 0x26) {
		data.cmdid = *pos;
		pos += 1;
		kind = CMD;
		return true;
	}
	//应答包：命令码1B（0x26） + 总记录数(2B)+ {数据记录i}
	//数据记录i:......
	kind = RES;

	size_t sz = 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StGapRecord);
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lpdata = new uint8_t[sz];
		memcpy(data.lpdata, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		return Parse_4res(data, buf, len); //尝试按照4字节尾部解析，通号对接每个记录尾部4字节
	}

	return true;
}


bool Parse315Protocol::Parse_4res(StGapValue& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StGapRecord_4res);
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lpdata = new uint8_t[sz];
		memcpy(data.lpdata, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}

	return true;
}


bool Parse315Protocol::Release(StGapValue& data)
{
	if (data.cnt == 0 || data.lpdata == NULL)
		return true;

	delete[] (uint8_t*)data.lpdata;

	return true;
}

//缺口报警,预警,以及图像/视频信息  buf指向数据内容部分 len数据内容的长度
//命令码1B（0x27）+转辙机ID(2B) + 报警时间(4B) + 报警确认信号(4B, 用于监测回执) + 定反位(1B) + 左右偏标志(1B) + 报警类型(1B)
// + 偏移值(2B) + 缺口值(2B) + 标准值(2B) + 预留(4B, 填充4个0xFF) + 图像 / 视频长度(4B) + 图像 / 视频内容
bool Parse315Protocol::Parse(StAlarmAndImgInfo& data, void* buf, int len, FRAME_KIND& kind)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
	{
		// 发送时，没有发送后半段
		if (szcnt - 4 > len)
			return false;

		memcpy(&data, pos, len);
		pos += len;
	}
	else
	{
		memcpy(&data, pos, sz);
		pos += sz;
	}


	if (data.imglen != 0)
	{
		sz = data.imglen;
		szcnt += sz;
		if (szcnt > len)
			return false;
		if (sz < 100)
		{
			data.lpimg = new uint8_t[sz];
			memcpy(data.lpimg, pos, sz);
		}
		else data.lpimg = NULL;
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(StAlarmAndImgRec& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::Release(StAlarmAndImgInfo& data)
{
	if (data.imglen == 0 || data.lpimg == NULL)
		return true;

	delete[] (uint8_t*)data.lpimg;

	return true;
}

//道岔动作后模拟量及缺口图像信息  命令码：0x28 （卡斯科）
bool Parse315Protocol::Parse(StActionInfo& data, void* buf, int len, FRAME_KIND& kind)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return false;
	//判断包的类型 上送还是回执
	if (sz == len && *pos == 0x27) {
		data.cmdid = *pos;
		pos += 1;
		kind = BACK;
		return true;
	}
	//回执包
	kind = UP_SEND;

	memcpy(&data, pos, sz);
	pos += sz;

	if (data.imglen != 0)
	{
		sz = data.imglen;
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lpimg = new uint8_t[sz];
		memcpy(data.lpimg, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(StActionInfoRec& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::Release(StActionInfo& data)
{
	if (data.imglen == 0 || data.lpimg == NULL)
		return true;

	delete[] (uint8_t*)data.lpimg;

	return true;
}

bool Parse315Protocol::Unparse(StManualOilingResq& data, vector<uint8_t>& buf, int& len)
{
	len = sizeof(StManualOilingResq);
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}


//道岔缺口最新图像0x29
bool Parse315Protocol::Parse(StLastGapImgRes& data, void* buf, int len, FRAME_KIND& kind)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	//判断包的类型 命令包还是应答包
	//命令包 命令码1B（0x29） + 转辙机ID(2B) + 预留(4B, 填充4个0xFF)
	if (7 == len && *pos == 0x29) {
		memcpy(&data, pos, 7);
		pos += 7;
		kind = CMD;
		return true;
	}
	//应答包
	kind = RES;

	size_t sz = 1 + 2 + 4 + 4 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.imglen != 0)
	{
		sz = data.imglen;
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lpimg = new uint8_t[sz];
		memcpy(data.lpimg, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(StLastGapImgReq& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 4;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}


bool Parse315Protocol::Release(StLastGapImgRes& data)
{
	if (data.imglen == 0 || data.lpimg == NULL)
		return true;

	delete[] (uint8_t*)data.lpimg;

	return true;
}


bool Parse315Protocol::ParseReq(StImgListReq& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 4 + 1 + 3;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::ReleaseReq(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Parse(StImgListRes& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 2 + 4;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StFileRecord);
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lprecord = new uint8_t[sz];
		memcpy(data.lprecord, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(StImgListReq& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 4 + 4 + 1 + 3;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::ReleaseReq(StImgListReq& data)
{
	return true;
}

bool Parse315Protocol::Release(StImgListRes& data)
{
	if (data.cnt == 0 || data.lprecord == NULL)
		return true;

	delete[] (uint8_t*)data.lprecord;

	return true;
}


bool Parse315Protocol::ParseReq(StImgInfoReq& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 1 + 3;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (pos != (uint8_t*)buf + len)
	{
		return false;
	}
	return true;
}
bool Parse315Protocol::Parse(StVibrationCurveRes& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));
	if (sizeof(data) >= len) return false;

	memcpy(&data, (uint8_t*)buf, sizeof(data));
	return true;
}

bool Parse315Protocol::Parse(StWorkingConditionValRes& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));
	if (sizeof(data) != len) return false;

	memcpy(&data, (uint8_t*)buf, sizeof(data));
	return true;
}

bool Parse315Protocol::Parse(StOpWorkingConditionRes& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));
	if (sizeof(data) != len) return false;

	memcpy(&data, (uint8_t*)buf, sizeof(data));
	return true;
}

bool Parse315Protocol::Parse(StImgInfoReq& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 1 + 3;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (pos != (uint8_t*)buf + len)
	{
		return false;
	}
	return true;
}

bool Parse315Protocol::Parse(StImgInfoRes& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 1 + 3 + 1 + 1 + 1 + 2 + 2 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.imglen != 0)
	{
		sz = data.imglen;
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lpimg = new uint8_t[sz];
		memcpy(data.lpimg, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(StImgInfoReq& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 4 + 1 + 3;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::Release(StImgInfoRes& data)
{
	if (data.imglen == 0 || data.lpimg == NULL)
		return true;

	delete[] (uint8_t*)data.lpimg;

	return true;
}

bool Parse315Protocol::ParseReq(StVedioListReq& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 1 + 2 + 4 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (pos != (uint8_t*)buf + len)
	{
		return false;
	}
	return true;
}

bool Parse315Protocol::Parse(StVedioListRes& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 1 + 2 + 4 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StVedioRecord);
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lprecord = new uint8_t[sz];
		memcpy(data.lprecord, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(StVedioListReq& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 1 + 2 + 4 + 4 + 4;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::Release(StVedioListRes& data)
{
	if (data.cnt == 0 || data.lprecord == NULL)
		return true;

	delete[] (uint8_t*)data.lprecord;

	return true;
}

/*0x2F 视频文件 begin */
bool Parse315Protocol::ParseReq(StVedioFileReq& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 1 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (pos != (uint8_t*)buf + len)
	{
		return false;
	}
	return true;
}

template<typename T>
bool Parse315Protocol::Parse(T* data, void* buf, int len)
{
	//memset(data, 0,  sizeof(StVedioFileResHead));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 1 + 2 + 4 + 1 + 1 + 2 + 4 + 4 + 2 + 2 + sizeof(T::datalen);
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(data, pos, sz);
	pos += sz;

	if (data->datalen != 0)
	{
		sz = data->datalen;
		szcnt += sz;
		if (szcnt > len)
			return false;
		data->lpdata = new uint8_t[sz];
		memcpy(data->lpdata, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(StVedioFileReq& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 1 + 2 + 4 + 4;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

template<typename T>
bool Parse315Protocol::Release(T* data)
{
	return false;

	if (data->datalen == 0 || data->lpdata == NULL)
		return true;

	data->lpdata;

	return true;
}
/*0x2F 视频文件 end */

/*0xF2 查询阻力文件 begin */
bool Parse315Protocol::Unparse(StPowerFileListResq& data, vector<uint8_t>& buf, int& len)
{
	len = sizeof(StPowerFileListResq);
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;

}
bool Parse315Protocol::Release(StPowerFileListResq& data)
{
	return false;
}

/*0xF2 查询阻力文件 end */

/*0xF3 查询阻力文件 begin */
bool Parse315Protocol::Unparse(StPowerFileDataResq& data, vector<uint8_t>& buf, int& len)
{
	len = sizeof(StPowerFileDataResq);
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;

}
bool Parse315Protocol::Release(StPowerFileDataResq& data)
{
	return false;
}

/*0xF2 查询阻力文件 end */


/*0x22 1DQJ 道岔区段 状态信息 begin*/
bool Parse315Protocol::ParseReq(St1DQJInfo& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 1 + 4;
	if (sz > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (pos != (uint8_t*)buf + len)
	{
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(St1DQJInfo& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 1 + 4;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}
/*0x22 1DQJ 道岔区段 状态信息 end*/

bool Parse315Protocol::Parse(StOilPreCurve& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 1 + 1 + 4 + 1;//14字节的非曲线数据长
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.cnt > len)
		return false;

	bool failed = false;
	if (data.cnt != 0)
	{
		//1条曲线数据的头部信息
		sz = data.cnt * sizeof(StCurve);
		data.lpdata = new uint8_t[sz];
		memset(data.lpdata, 0,  sz);

		for (int i = 0; i < data.cnt; ++i)
		{
			StCurve& curv = ((StCurve*)data.lpdata)[i];

			sz = 1 + 4 + 2;
			szcnt += sz;
			if (szcnt > len)
			{
				failed = true;
				break;
			}
			memcpy(&curv, pos, sz);
			pos += sz;

			if (curv.len != 0)
			{
				sz = curv.len;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = true;
					break;
				}
				curv.lpdata = new uint8_t[sz];
				memcpy(curv.lpdata, pos, sz);
				pos += sz;
			}
		}
	}

	if (failed || pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}

	return true;
}


string Parse315Protocol::GetStrFromData(uint8_t* buf, int dwLen)
{
	int dwLoop;
	string strReturn = "";

	char tmpch[4] = { 0 };

	uint8_t bval = 0;
	for (dwLoop = 0; dwLoop < dwLen; dwLoop++)
	{
		bval = *(buf + dwLoop);
		if (dwLoop == 0)
		{
			snprintf(tmpch, sizeof(tmpch), "%02x ", bval);
			strReturn += tmpch;
		}
		else
		{
			snprintf(tmpch, sizeof(tmpch), "%02x ", bval);
			strReturn += tmpch;
		}
	}

	return strReturn;
}

bool Parse315Protocol::Unparse(StOilPreCurveRec& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 4 + 1 + 1 + 4 + 1;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::Release(StOilPreCurve& data)
{
	if (data.cnt == 0 || data.lpdata == NULL)
		return true;

	for (int i = 0; i < data.cnt; ++i)
	{
		StCurve& curv = ((StCurve*)data.lpdata)[i];

		if (curv.len && curv.lpdata)
			delete[] (uint8_t*)curv.lpdata;
	}

	delete[] (uint8_t*)data.lpdata;

	return true;
}

bool Parse315Protocol::Parse(StOilLevelInfo& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StSdataRecord);
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lpdata = new uint8_t[sz];
		memcpy(data.lpdata, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Release(StOilLevelInfo& data)
{
	if (data.cnt == 0 || data.lpdata == NULL)
		return true;

	delete[] (uint8_t*)data.lpdata;

	return true;
}

/*0x30 实时视频流信息 begin*/
bool Parse315Protocol::ParseReq(StRealCtrlReq& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 1 + 1;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	switch (data.cmdtype)
	{
	case 0x01://请求码流
	{
		sz = 1 + 2 + 2 + 2;
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lpdata = new uint8_t[sz];
		memcpy(data.lpdata, pos, sz);
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
			return false;
		data.lpdata = new uint8_t[sz];
		memcpy(data.lpdata, pos, sz);
		pos += sz;
		break;
	}
	case 0x06:
	{
		sz = 2 + 2;
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lpdata = new uint8_t[sz];
		memcpy(data.lpdata, pos, sz);
		pos += sz;
		break;
	}
	default:
		break;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Parse(StRealCtrlRes& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 1;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	switch (data.result)
	{
	case 0x00:
	{
		sz = 1;
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lpdata = new uint8_t[sz];
		memcpy(data.lpdata, pos, sz);
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
			return false;
		data.lpdata = new uint8_t[sz];
		memcpy(data.lpdata, pos, sz);
		pos += sz;
		break;
	}
	default:
		break;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(StRealCtrlReq& data, vector<uint8_t>& buf, int& len)
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

	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);


	uint8_t* pos = (uint8_t*)buf.data();

	size_t sz = 1 + 2 + 1 + 1;
	memcpy(pos, &data, sz);
	pos += sz;

	sz = len - sz;
	if (sz)
	{
		memcpy(pos, data.lpdata, sz);
		pos += sz;
	}

	return true;
}

bool Parse315Protocol::Release(StRealCtrlReq& data)
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
		return true;

	delete[] (uint8_t*)data.lpdata;

	return true;
}

bool Parse315Protocol::Release(StRealCtrlRes& data)
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
		return true;

	delete[] (uint8_t*)data.lpdata;

	return true;
}
/*0x30 实时视频流信息  end*/

/*0x31 实时码流  begin*/
bool Parse315Protocol::Parse(StRealStream& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 4;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.packlen != 0)
	{
		sz = data.packlen;
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lpdata = new uint8_t[sz];
		memcpy(data.lpdata, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Release(StRealStream& data)
{
	if (data.packlen == 0 || data.lpdata == NULL)
		return true;

	delete[] (uint8_t*)data.lpdata;

	return true;
}
/*0x31 实时码流  end*/
bool Parse315Protocol::Parse(StPowerListReq& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 4 + 1;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (pos != (uint8_t*)buf + len)
	{
		return false;
	}
	return true;
}

bool Parse315Protocol::Parse(StPowerListRes& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StPowerRecord);
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lprecord = new uint8_t[sz];
		memcpy(data.lprecord, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(StPowerListReq& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 4 + 4 + 1;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::Release(StPowerListRes& data)
{
	if (data.cnt == 0 || data.lprecord == NULL)
		return true;

	delete[] (uint8_t*)data.lprecord;

	return true;
}


bool Parse315Protocol::Parse(StPowerInfoRes& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 1 + 1 + 4 + 4 + 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.cnt == 0x01)//直接阻力
	{
		data.lprecord = new uint8_t[sizeof(StPowerCurveInfo)];
		sz = 1 + 4 + 2;
		szcnt += sz;
		memcpy(data.lprecord, pos, sz);
		pos += sz;

		StPowerCurveInfo* pCurveInfo = (StPowerCurveInfo*)(data.lprecord);

		if (pCurveInfo->r[0] != 0x01 && pCurveInfo->r[0] == 0xFF) {
			Parse315Protocol::Release(data);
			return false;
		}
		if (!pCurveInfo || pCurveInfo->curvelen == 0) {
			Parse315Protocol::Release(data);
			return false;
		}
		szcnt += pCurveInfo->curvelen;
		if (pCurveInfo->r[0] == 0x01) {
			szcnt += 14; //扩展配置
		}
		if (szcnt != len)
		{
			Parse315Protocol::Release(data);
			return false;
		}

		if (pCurveInfo->r[0] == 0x01) {
			pCurveInfo->lpcurvedata = new uint8_t[pCurveInfo->curvelen + 14];  //Release 还得加上这里的释放
		}
		else {
			pCurveInfo->lpcurvedata = new uint8_t[pCurveInfo->curvelen];
		}
		memcpy(pCurveInfo->lpcurvedata, pos, len - 20 - 7);

		return true;
	}
	return false;
}

// bool Parse315Protocol::Parse(StNewPowerNotify& data, void* buf, int len)
// {
// 	memset(&data, 0,  sizeof(data));
// 
// 	int szcnt = 0;
// 	uint8_t* pos = (uint8_t*)buf;
// 
// 	size_t sz = sizeof(StNewPowerNotify);//1 + 2 + 2 + 4 + 4 + 4 + 4 + 1;
// 	szcnt += sz;
// 	if (szcnt > len)
// 		return false;
// 	memcpy(&data, pos, sz);
// 
// 
// 	return true;
// }
bool Parse315Protocol::Parse(NewDataFile& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 2 + 4;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);


	return true;
}
bool Parse315Protocol::Parse(StPowerFileData& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;
	size_t sz = sizeof(StPowerFileData) - 4;

	memcpy(&data, pos, sz);
	pos += sz;

	data.dataInfo = new uint8_t[data.fileLen];
	memcpy(data.dataInfo, pos, data.fileLen);


	return true;
}

bool Parse315Protocol::Unparse(StPowerInfoReq& data, vector<uint8_t>& buf, int& len)
{
	len = 1 + 2 + 4 + 1;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::Unparse(StPowerFileData& data, vector<uint8_t>& buf, int& len)
{
	if (len == 9)
	{
		//buf = new uint8_t[len];
//memset(buf, 0,  len);
		buf.resize(len);

		memcpy(buf.data(), &data, len);

		//buf = new uint8_t[len];
		//memset(buf, 0,  len);
		////StNewPowerNotify newData;
		////newData.cmdid = data.cmdid;
		////newData.sid = data.sid;
		////newData.acqObjType = data.acqObjType;
		////newData.time = data.time;
		////memcpy(buf, &newData, sizeof(StNewPowerNotify));
		//memcpy(buf, &data, sizeof(StPowerFileData));
	}
	else
	{

	}


	return true;
}

bool Parse315Protocol::Unparse(CalPower& data, vector<uint8_t>& buf, int& len)
{
	len = sizeof(CalPower);
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);

	memcpy(buf.data(), &data, len);

	return true;
}

bool Parse315Protocol::Parse(DBJFBJInfo& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 4 + 4 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.dataCount != 0)
	{
		sz = data.dataCount * sizeof(ZZJIDAndStatus);
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.curveInfoList = new uint8_t[sz];
		memcpy(data.curveInfoList, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Unparse(DBJFBJInfo& data, vector<uint8_t>& buf, int& len)
{
	len = 3 * data.dataCount + 11;
	//buf = new uint8_t[len];
	//memset(buf, 0,  len);
	buf.resize(len);


	memcpy(buf.data(), &data, 11);
	memcpy(buf.data() + 11, data.curveInfoList, 3 * data.dataCount);

	//memset(buf, 0,  len);
	//memcpy(buf, bufTmp, len);

	return true;
}

bool Parse315Protocol::Release(DBJFBJInfo& data)
{
	if (data.dataCount == 0 || data.curveInfoList == NULL)
		return true;

	delete[] (uint8_t*)data.curveInfoList;

	return true;
}

tstring Parse315Protocol::ToString(const DBJFBJInfo& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机个数:%d"), data.dataCount);
	strRst += buf;

	strRst += "\r\n";

	for (int i = 0; i < data.dataCount; ++i)
	{
		ZZJIDAndStatus& srec = ((ZZJIDAndStatus*)data.curveInfoList)[i];

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("转辙机数据记录%d{"), i);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("转辙机ID:%d"), srec.zzjID);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("定反位:%d"), srec.ZZJStatus);
		strRst += buf;

		strRst += ("}");

		strRst += "\r\n";
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StAlarmListReq& data)
{
	tstring strRst = ("");

	strRst += ("转辙机ID:");
	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("%d"), data.cmdid);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StAlarmListRes& data)
{
	tstring strRst = ("");
	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("数目:%d，\n"), data.cnt);
	strRst += buf;

	S_315_ALARM_INFO* pRecord = (S_315_ALARM_INFO*)data.lprecord;
	for (uint16_t i = 0; i < data.cnt; i++)
	{
		//CTime tm = pRecord[i].time;
		TIME tm = timeopt::Unix2SysTime(pRecord[i].time);
		snprintf(buf, sizeof(buf), ("[%d] 报警时间:%04d-%02d-%02d %02d:%02d:%02d, "), i + 1,
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("报警类型:%d, "), pRecord[i].alarm);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("定反位:%d, "), pRecord[i].fixinvert);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("缺口值:%d, "), pRecord[i].gap);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("采集类型:%d\n"), pRecord[i].acqtype);
		strRst += buf;

	}
	return strRst;
}

bool Parse315Protocol::Release(StPowerInfoRes& data)
{
	if (data.cnt == 0 || data.lprecord == NULL)
		return true;

	StPowerCurveInfo* p1 = (StPowerCurveInfo*)data.lprecord;
	if (p1->lpcurvedata != NULL) {
		delete[] (uint8_t*)p1->lpcurvedata;
		p1->lpcurvedata = NULL;
	}
	delete[] (uint8_t*)data.lprecord;

	return true;
}

tstring Parse315Protocol::ToString(const StFrame& data, int dir)
{
	if (data.lpdata == nullptr) return "";
	tstring strRst = ("");

	if (data.datalen != 0 && data.lpdata != NULL)
	{
		switch (data.ftype)
		{
		case FRAME_TYPE_HEARTBEAT:
			strRst += ("心跳");
			strRst += ("，");
			strRst += Parse315Protocol::ToString(*(StHeartBeat315*)data.lpdata);
			break;
		case FRAME_TYPE_DATA:
		{
			StDataBasic* basic = (StDataBasic*)data.lpdata;
			if (g_dw0x23ExCmdID > 0 && basic->cmdid == g_dw0x23ExCmdID)
			{
				if (data.e_frmKind == CMD)
				{
					strRst += ("缺口扩展配置命令包");
				}
				else if (data.e_frmKind == RES)
				{
					strRst += ("缺口扩展配置应答包");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StGapCfgRes*)data.lpdata);
				}
			}

			switch (basic->cmdid)
			{
			case CMD_CODE_JDSP:
			{
				strRst += ("JDSP:\r\n");
				StJDSP* p = (StJDSP*)data.lpdata;

				if (p)
					strRst += p->jdsp.c_str();
				break;
			}
			case CMD_CODE_GAPCFG://0x23
			{
				if (data.e_frmKind == CMD)
				{
					strRst += ("缺口配置命令包");
				}
				else if (data.e_frmKind == RES)
				{
					strRst += ("缺口配置应答包");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StGapCfgRes*)data.lpdata);
				}
				break;
			}
			case (uint8_t)E_315_PROTOCOL_TYPE::OIL_BOX_VOLUME_0x53:
			{
				char buf[256];
				if (data.e_frmKind == CMD)
				{
					snprintf(buf, sizeof(buf), ("加油箱储油量命令包:转辙机:%d"), ((StOilBoxVolumeData*)data.lpdata)->v);

					strRst += buf;
				}
				else if (data.e_frmKind == RES)
				{
					strRst += ("加油箱储油量应答包:\n");
					//strRst += Parse315Protocol::ToString(*(StGapCfgRes*)data.lpdata);
					StOilBoxVolume* pIt = (StOilBoxVolume*)((StOilBoxVolumeData*)data.lpdata)->dataInfo;
					for (uint16_t k = 0; k < ((StOilBoxVolumeData*)data.lpdata)->v; k++)
					{
						if (pIt[k].value == 0xFF)
						{
							snprintf(buf, sizeof(buf), ("转辙机:%d, 无加油设备\n"), pIt[k].sid);
						}
						else if (pIt[k].value == 0xFFFF)
						{
							snprintf(buf, sizeof(buf), ("转辙机:%d, 设备不在线\n"), pIt[k].sid);
						}
						else if (pIt[k].value == 0)
						{
							snprintf(buf, sizeof(buf), ("转辙机:%d, 油位过低\n"), pIt[k].sid);
						}
						else
						{
							snprintf(buf, sizeof(buf), ("转辙机:%d, 储油量:%d\n"), pIt[k].sid, pIt[k].value);
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
				char buf[256];
				snprintf(buf, sizeof(buf), ("转辙机:%d\n采集时间:%04d-%02d-%02d %02d:%02d:%02d\n值类型:%d\n值:%d.%d"),
					lpsubdata->zzjid, tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond,
					lpsubdata->valType, lpsubdata->value / 10, lpsubdata->value % 10);
				strRst += buf;
				break;
			}
			case (uint8_t)E_315_PROTOCOL_TYPE::GONGKUANG_INIT_VALUE_0x82:
			{
				StOpWorkingConditionRes* lpsubdata = (StOpWorkingConditionRes*)data.lpdata;
				strRst += "工况操作结果\n";
				const char s_szValueType[][32] = { "定位轨距","定位密贴","定位爬行","定位基本轨横移","反位轨距","反位密贴","反位爬行","反位基本轨横移" };
				//CTime tm = lpsubdata->time;
				TIME tm = timeopt::Unix2SysTime(lpsubdata->time);
				char buf[256];
				snprintf(buf, sizeof(buf), ("转辙机:%d\n时间:%04d-%02d-%02d %02d:%02d:%02d\n动作类型:%s\n值类型:%s\nK值:%f\nB值:%f\n返回结果:%d"),
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
				char buf[256];
				snprintf(buf, sizeof(buf), ("转辙机:%d\n采集时间:%04d-%02d-%02d %02d:%02d:%02d\n曲线类型:%d\n采样间隔:%d\n曲线数据长度:%d"),
					lpsubdata->zzjid, tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond,
					lpsubdata->curveType, lpsubdata->acpfreq, lpsubdata->curveLen);
				strRst += buf;

				break;
			}
			case CMD_CODE_GAPVAL://0x26
			{
				if (data.e_frmKind == CMD)//(dir == 0)
				{
					strRst += ("缺口值命令包");
				}
				else if (data.e_frmKind == RES)
				{
					strRst += ("缺口值应答包");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StGapValue*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_ALARM_AND_IMG://0x27
			case CMD_CODE_ALARM://0x97
			{
				if (dir == 0)
				{
					strRst += ("报警/预警信息(缺口图像信息)回执");
					strRst += ("，");
					if (basic->cmdid == CMD_CODE_ALARM_AND_IMG)//0x27
						strRst += Parse315Protocol::ToString0x27(*(StAlarmAndImgRec*)data.lpdata);
					else if (basic->cmdid == CMD_CODE_ALARM)//0x97
						strRst += Parse315Protocol::ToString0x97(*(StAlarmAndImgRec*)data.lpdata);
				}
				else
				{
					strRst += ("报警/预警信息(缺口图像信息)");
					strRst += ("，");
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
					strRst += ("道岔动作后模拟量及缺口图像信息回执");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StActionInfoRec*)data.lpdata);
				}
				else
				{
					strRst += ("道岔动作后模拟量及缺口图像信息");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StActionInfo*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_LASTGAPIMG://0x29
			{
				if (dir == 0)
				{
					strRst += ("道岔缺口最新图像请求");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StLastGapImgReq*)data.lpdata);
				}
				else
				{
					strRst += ("道岔缺口最新图像应答");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StLastGapImgRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_IMGLIST:
			{
				if (dir == 0)
				{
					strRst += ("图像列表请求");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StImgListReq*)data.lpdata);
				}
				else
				{
					strRst += ("图像列表应答");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StImgListRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_IMGINFO:
			{
				if (dir == 0)
				{
					strRst += ("图像信息请求");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StImgInfoReq*)data.lpdata);
				}
				else
				{
					strRst += ("图像信息应答");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StImgInfoRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_VEDIOLIST:
			{
				if (dir == 0)
				{
					strRst += ("视频时间列表请求");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StVedioListReq*)data.lpdata);
				}
				else
				{
					strRst += ("视频时间列表应答");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StVedioListRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_VEDIOFILE:
			{
				if (dir == 0)
				{
					strRst += ("视频文件请求");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StVedioFileReq*)data.lpdata);
				}
				else
				{
					strRst += ("视频文件应答");
					strRst += ("，");
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
					strRst += ("1DQJ及道岔区段状态信息");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(St1DQJInfo*)data.lpdata);
				}
				else
				{
				}
				break;
			}
			case CMD_CODE_YYQX:
			{
				strRst += ("曲线");
				strRst += ("，");
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
					strRst += ("油位及缺口采集设备状态信息");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StOilLevelInfo*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_PARAMETER:
			{
				if (dir == 0)
				{
					strRst += ("参数设置与获取");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StParamSet*)data.lpdata);
				}
				else
				{
					strRst += ("参数设置与获取");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StParamSet*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_REALCTRL:
			{
				if (dir == 0)
				{
					strRst += ("实时视频流控制请求");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StRealCtrlReq*)data.lpdata);
				}
				else
				{
					strRst += ("实时视频流控制应答");
					strRst += ("，");
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
					strRst += ("实时码流");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StRealStream*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_ELECCURVE:
			{
				if (dir == 0)
				{
					strRst += ("室外电参数动作曲线回执");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StElecCurveRec*)data.lpdata);
				}
				else
				{
					strRst += ("室外电参数动作曲线");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StElecCurve*)data.lpdata);
				}
				break;
			}
			//case CMD_CODE_NEW_DATA_FILE:
			//{
			//	strRst += ("阻力新数据");
			//	strRst += ("，");
			//	strRst += Parse315Protocol::ToString(*(StNewPowerNotify*)data.lpdata);
			//	break;
			//}
			case CMD_CODE_QUERY_POWER_FILE:
			{
				strRst += ("阻力文件列表信息，");
				strRst += Parse315Protocol::ToString((StPowerFileListF2*)data.lpdata, data.datalen);
				break;
			}
			case CMD_CODE_DOWNLOAD_DATA_FILE:
			{
				if (dir == 1)
				{
					strRst += ("阻力文件信息，");
					strRst += Parse315Protocol::ToString(*(StPowerFileData*)data.lpdata);
				}
				else
				{
					strRst += ("阻力文件信息，");
					StPowerFileDataResq* pResq = (StPowerFileDataResq*)(data.lpdata);
					strRst += ("，");
					char buf[256] = { 0 };
					snprintf(buf, sizeof(buf), ("转辙机ID:%d,"), pResq->sid);
					strRst += buf;

					//CTime tm = pResq->time;
					TIME tm = timeopt::Unix2SysTime(pResq->time);
					snprintf(buf, sizeof(buf), ("采集时间:%04d-%02d-%02d %02d:%02d:%02d, 对象类型:"),
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
				strRst += ("静态阻力上送");
				strRst += ("，");
				strRst += Parse315Protocol::ToString(*(StStaticPowerList*)data.lpdata);

				break;
			}
			case CMD_CODE_POWERLIST:
			{
				if (dir == 0)
				{
					strRst += ("阻力文件列表请求");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StPowerListReq*)data.lpdata);
				}
				else
				{
					strRst += ("阻力文件列表应答");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StPowerListRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_POWERCONTENT:
			{
				if (dir == 0)
				{
					strRst += ("阻力信息请求");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StPowerInfoReq*)data.lpdata);
				}
				else
				{
					strRst += ("阻力信息应答");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StPowerInfoRes*)data.lpdata);
				}
				break;
			}
			case CMD_CODE_DBJFBJ:
			{
				if (dir == 0)
				{
					strRst += ("定反表状态");
					strRst += ("，");
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
					strRst += ("手动加油结果：");
					strRst += Parse315Protocol::ToString(*(StManualOilingRes*)data.lpdata);
				}
				else
				{
					strRst += ("手动加油结果：");
					strRst += Parse315Protocol::ToString(*(StManualOilingRes*)data.lpdata);
				}
				break;

				//if (data.datalen == sizeof(StManualOilingRes))
				//{
				//	//StPowerFileList* lpdata = new StPowerFileList[(data.datalen - 1) / sizeof(StPowerFileList)];
				//	StManualOilingRes* lpdata = new StManualOilingRes;
				//	memcpy(lpdata, pos, data.datalen);
				//	data.lpdata = lpdata;
				//	pos += data.datalen;
				//}
				//else
				//{
				//	return false;
				//}
				//break;
			}
			case 0x52:
			{
				if (dir == 0)
				{
					strRst += ("加油结果广播:");
					strRst += Parse315Protocol::ToString(*(StOilingResultNotify*)data.lpdata);
				}
				else
				{
					strRst += ("加油结果广播:");
					strRst += Parse315Protocol::ToString(*(StOilingResultNotify*)data.lpdata);
				}
				break;

				//if (data.datalen == sizeof(StOilingResultNotify))
				//{
				//	//StPowerFileList* lpdata = new StPowerFileList[(data.datalen - 1) / sizeof(StPowerFileList)];
				//	StOilingResultNotify* lpdata = new StOilingResultNotify;
				//	memcpy(lpdata, pos, data.datalen);
				//	data.lpdata = lpdata;
				//	pos += data.datalen;
				//}
				//else
				//{
				//	return false;
				//}
				//break;
			}
			case (uint8_t)E_315_PROTOCOL_TYPE::UN_RECOVER_ALARM_0x65:
			{
				if (dir == 0)
				{
					strRst += ("未恢复报警列表请求");
					strRst += ("，");
					strRst += Parse315Protocol::ToString(*(StAlarmListReq*)data.lpdata);
				}
				else
				{
					strRst += ("未恢复报警列表应答");
					strRst += ("，");
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
	tstring strRst = ("");

	strRst += ("时间:");
	//CTime tm = data.hbtime;
	TIME tm = timeopt::Unix2SysTime(data.hbtime);
	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::AStringToTString(const char* lpStr)
{
	return tstring(lpStr ? lpStr : "");
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
	tstring strRst = ("");

	if (data.cmdid == CMD_CODE_GAPCFG)
	{
		char buf[256] = { 0 };
		snprintf(buf, sizeof(buf), ("配置数:%d\r\n"), data.cfgcnt);
		strRst += buf;

		for (int i = 0; i < data.cfgcnt; ++i)
		{
			const StSwitchCfg& scfg = ((StSwitchCfg*)data.lpcfg)[i];

			strRst += ("，");
			snprintf(buf, sizeof(buf), ("配置%d:{"), i);
			strRst += buf;

			char name[50] = { 0 };
			memcpy(name, scfg.lpname, scfg.nlen);
			strRst += ("转辙机:") + AStringToTString(name);

			strRst += ("，");
			strRst += ("转辙机类型:");
			switch (scfg.stype)
			{
			case SWITCH_TYPE_ZD6:
				strRst += ("ZD6");
				break;
			case SWITCH_TYPE_EJ:
				strRst += ("EJ");
				break;
			case SWITCH_TYPE_GK:
				strRst += ("GK");
				break;
			case SWITCH_TYPE_S700K:
				strRst += ("S700K");
				break;
			case SWITCH_TYPE_ZDJ_9:
				strRst += ("ZDJ-9");
				break;
			case SWITCH_TYPE_ZYJ7_I:
				strRst += ("ZYJ7-I");
				break;
			case SWITCH_TYPE_ZYJ7_II:
				strRst += ("ZYJ7-II");
				break;
			case SWITCH_TYPE_ZYJ9:
				strRst += ("ZY(J)9");
				break;
			default:
				strRst += ("未知");
				break;
			}

			strRst += ("，");
			snprintf(buf, sizeof(buf), ("转辙机ID:%d"), scfg.sid);
			strRst += buf;

			strRst += ("，");
			snprintf(buf, sizeof(buf), ("采集信息个数:%d"), scfg.cnt);
			strRst += buf;

			strRst += ("，");
			strRst += ("采集信息类型:{");
			for (int j = 0; j < scfg.cnt; ++j)
			{
				switch (((uint16_t*)scfg.lpinfo)[j])
				{
				case SWITCH_INFO_GAP:
					strRst += ("缺口值,");
					break;
				case SWITCH_INFO_OFFSET:
					strRst += ("偏移值,");
					break;
				case SWITCH_INFO_STD:
					strRst += ("标准值,");
					break;
				case SWITCH_INFO_OIL:
					strRst += ("油位,");
					break;
				case SWITCH_INFO_TEMP:
					strRst += ("温度,");
					break;
				case SWITCH_INFO_HUM:
					strRst += ("湿度,");
					break;
				case SWITCH_INFO_OILPRE:
					strRst += ("油压曲线,");
					break;
				case SWITCH_INFO_VOL:
					strRst += ("电压,");
					break;
				case SWITCH_INFO_ELEC:
					strRst += ("电流,");
					break;
				default:
					//工况
					uint16_t wIndex = ((uint16_t*)scfg.lpinfo)[j];
					if (wIndex > 50 && wIndex < 66)
					{
						//strRst += g_szWorkingConditionName[wIndex - 51];
						strRst += ",";
					}
					else
						strRst += ("未知,");
					break;
				}

			}
			strRst += ("}");

			strRst += ("}");


			strRst += "\r\n";
		}
	}
	else
	{
		char buf[256] = { 0 };
		snprintf(buf, sizeof(buf), ("文件数:%d\r\n"), data.cfgcnt);
		strRst += buf;
		strRst += (char*)data.lpcfg;
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StGapValue& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("记录数:%d\r\n"), data.cnt);
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
		snprintf(buf, sizeof(buf), ("缺口记录%d:{"), i);
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


		strRst += ("时间:");
		//CTime tm = rcd->time;
		TIME tm = timeopt::Unix2SysTime(rcd->time);
		snprintf(buf, sizeof(buf), ("%04d-%02d-%02d %02d:%02d:%02d"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("转辙机ID:%d"), rcd->sid);
		strRst += buf;
		//
		//#ifdef _DEBUG
		//		strRst += ("，");
		//		snprintf(buf, sizeof(buf), ("转辙机名称:%s"), mapName[rcd->sid].c_str());
		//		strRst += buf;
		//#endif // _DEBUG

		strRst += ("，");
		strRst += ("定反位:");
		if (rcd->fixorinvert == 0)
			strRst += ("定位");
		else
			strRst += ("反位");

		strRst += ("，");
		strRst += ("左右偏标志:");
		if (rcd->lrsign == 1)
			strRst += ("左偏");
		else if (rcd->lrsign == 2)
			strRst += ("右偏");
		else
			strRst += ("无效");

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("偏移值:%.2fmm"), rcd->offset / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("缺口值:%.2fmm"), rcd->gap / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("标准值:%.2fmm"), rcd->std / 100.0);
		strRst += buf;

		strRst += ("，");
		strRst += ("缺口值类型:");
		if (rcd->gaptype == 0)
			strRst += ("静态采集");
		else if (rcd->gaptype == 1)
			strRst += ("道岔操纵后采集");
		else if (rcd->gaptype == 2)
			strRst += ("过车时采集");
		else
			strRst += (" ");

		strRst += ("}");

		strRst += "\r\n";
	}

	return strRst;
}

TIME Parse315Protocol::Time_tToSystemTime(time_t t)
{
	TIME st;
	st.fromUnixTime(t);
	return st;
}


bool Parse315Protocol::TimeToString(const TIME& time, string& str)
{
	if (time.wYear > 2000 && time.wDay > 0 && time.wDay < 40 && time.wHour >= 0 && time.wHour <= 24 && time.wMinute >= 0 && time.wMinute <= 60)
	{
		str = str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
			time.wYear, time.wMonth, time.wDay,
			time.wHour, time.wMinute, time.wSecond);
	}
	return true;
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
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("报警时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	uint8_t* cbFill = (uint8_t*)&data.filldata;
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


	strRst += ("，");
	TIME ackTime = Time_tToSystemTime(data.alarmconfirm);
	string strAckTime;
	TimeToString(ackTime, strAckTime);

	if (data.alarmconfirm == 0xffffffff)
	{
		strAckTime = "0xFFFFFFFF";
	}

	snprintf(buf, sizeof(buf), ("报警确认信号:%s"), strAckTime.c_str());
	strRst += buf;

	bool bPowerAlarm = false; //阻力报警
	if (nVer < 2023 && (data.alarmtype == ALARM_TYPE_POWERYJHF || data.alarmtype == ALARM_TYPE_POWERBJHF
		|| data.alarmtype == ALARM_TYPE_POWERYJ || data.alarmtype == ALARM_TYPE_POWERBJ)
		|| nVer >= 2023 && (data.alarmtype == 13 || data.alarmtype == 113))
	{
		strRst += ("，");
		strRst += GetGapAcqTypeName(GapAcqTypeOfFile::egatofTrigger, false) + ("方向:");
		if (data.fixorinvert == 0)
			strRst += ("定->反");
		else
			strRst += ("反->定");

		strRst += ("，");
		strRst += ("报警子类型:");
		if (data.lrsign == 1)
			strRst += ("阻力曲线最大值超标");
		else if (data.lrsign == 2)
			strRst += ("阻力曲线平均值超标");
		else if (data.lrsign == 3)
			strRst += ("阻力曲线相对参考线变化量超标");

		bPowerAlarm = true; //阻力报警
	}
	else
	{
		strRst += ("，");
		strRst += ("定反位:");
		if (data.fixorinvert == 0)
			strRst += ("定位");
		else
			strRst += ("反位");

		strRst += ("，");
		strRst += ("左右偏标志:");
		if (data.lrsign == 1)
			strRst += ("左偏");
		else if (data.lrsign == 2)
			strRst += ("右偏");
		else
			strRst += ("无效");
	}

	strRst += ("，");
	strRst += ("报警类型:") + GetAlarmTypeDesc(data.alarmtype, nVer);


	if (bPowerAlarm)
	{
		strRst += ("，");
		snprintf(buf, sizeof(buf), ("动作杆伸缩方向:%s"), data.offset == 0 ? "拉入" : "伸出");
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("预警/报警起始点数:%d"), data.gap);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("预警/报警结束点数:%d"), data.std);
		strRst += buf;
	}
	else
	{

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("偏移值:%.2fmm"), data.offset / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("缺口值:%.2fmm"), data.gap / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("标准值:%.2fmm"), data.std / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("图像长度:%d"), data.imglen);
		strRst += buf;

		strRst += ("，");
		strRst += ("图像内容:{...}");
	}

	return strRst;
}

tstring Parse315Protocol::ToString0x27(const StAlarmAndImgRec& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("报警时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	string strAckTime = "0xFFFFFFFF";
	if (data.alarmtype >= 100)
	{
		TIME ackTime = Time_tToSystemTime(data.alarmconfirm);
		TimeToString(ackTime, strAckTime);
	}

	snprintf(buf, sizeof(buf), (",报警确认信号:%s"), strAckTime.c_str());
	strRst += buf;
	uint8_t* cbFill = (uint8_t*)&data.filldata;
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
		strRst += ("，");
		strRst += GetGapAcqTypeName(GapAcqTypeOfFile::egatofTrigger, false) + ("方向:");
		if (data.fixorinvert == 0)
			strRst += ("定->反");
		else
			strRst += ("反->定");

		strRst += ("，");
		strRst += ("报警子类型:");
		if (data.lrsign == 1)
			strRst += ("阻力曲线最大值超标");
		else if (data.lrsign == 2)
			strRst += ("阻力曲线平均值超标");
		else if (data.lrsign == 3)
			strRst += ("阻力曲线相对参考线变化量超标");

		bPowerAlarm = true; //阻力报警
	}
	else
	{
		strRst += ("，");
		strRst += ("定反位:");
		if (data.fixorinvert == 0)
			strRst += ("定位");
		else
			strRst += ("反位");

		strRst += ("，");
		strRst += ("左右偏标志:");
		if (data.lrsign == 1)
			strRst += ("左偏");
		else if (data.lrsign == 2)
			strRst += ("右偏");
		else
			strRst += ("无效");
	}

	strRst += ("，");
	strRst += ("报警类型:") + GetAlarmTypeDesc(data.alarmtype, nVer);

	if (bPowerAlarm)
	{
		strRst += ("，");
		snprintf(buf, sizeof(buf), ("动作杆伸缩方向:%s"), data.offset == 0 ? "拉入" : "伸出");
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("预警/报警起始点数:%d"), data.gap);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("预警/报警结束点数:%d"), data.std);
		strRst += buf;
	}
	else
	{
		strRst += ("，");
		snprintf(buf, sizeof(buf), ("偏移值:%.2fmm"), data.offset / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("缺口值:%.2fmm"), data.gap / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("标准值:%.2fmm"), data.std / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("图像长度:%d"), data.imglen);
		strRst += buf;
	}

	return strRst;
}

tstring Parse315Protocol::ToString0x97(const StAlarmAndImgRec& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("报警时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;
	strRst += ("，");
	strRst += ("报警类型:");
	switch (data.alarmtype)
	{
	case ALARM_TYPE_QKYJ:
		strRst += ("缺口预警及预警图像");
		break;
	case ALARM_TYPE_QKBJ:
		strRst += ("缺口告警及告警图像");
		break;
	case ALARM_TYPE_QKSBGZ:
		strRst += ("缺口采集设备故障");
		break;
	case ALARM_TYPE_TXWFSB:
		strRst += ("缺口图像无法识别告警");
		break;
	case ALARM_TYPE_GCKLGD:
		strRst += ("过车时框量过大告警及过车视频");
		break;
	case ALARM_TYPE_ZZJSBGZ:
		strRst += ("转辙机采集设备故障告警");
		break;
	case ALARM_TYPE_WDBJ:
		strRst += ("温度告警");
		break;
	case ALARM_TYPE_SDBJ:
		strRst += ("湿度告警");
		break;
	case ALARM_TYPE_YWYJ:
		strRst += ("油位预警");
		break;
	case ALARM_TYPE_YWBJ:
		strRst += ("油位告警");
		break;
	case ALARM_TYPE_POWERYJ:
		strRst += ("油压预警");
		break;
	case ALARM_TYPE_POWERBJ:
		strRst += ("油压告警");
		break;
	case ALARM_TYPE_TEMPERATURE:
		strRst += ("温度预警");
		break;
	case ALARM_TYPE_HUMILITY:
		strRst += ("湿度预警");
		break;
	case ALARM_TYPE_QKYJHF:
		strRst += ("缺口预警恢复及图像");
		break;
	case ALARM_TYPE_QKBJHF:
		strRst += ("缺口告警恢复及图像");
		break;
	case ALARM_TYPE_QKSBGZHF:
		strRst += ("缺口采集设备故障恢复及图像");
		break;
	case ALARM_TYPE_TXWFSBHF:
		strRst += ("缺口图像无法识别告警恢复及图像");
		break;
	case ALARM_TYPE_ZZJSBGZHF:
		strRst += ("转辙机采集设备故障告警恢复");
		break;
	case ALARM_TYPE_GCKLGDHF:
		strRst += ("过车时框量过大告警恢复及过车视频");
		break;
	case ALARM_TYPE_WDBJHF:
		strRst += ("温度告警恢复");
		break;
	case ALARM_TYPE_SDBJHF:
		strRst += ("温度告警恢复");
		break;
	case ALARM_TYPE_YWYJHF:
		strRst += ("油位预警恢复");
		break;
	case ALARM_TYPE_YWBJHF:
		strRst += ("油位告警恢复");
		break;
	case ALARM_TYPE_POWERYJHF:
		strRst += ("油压预警恢复");
		break;
	case ALARM_TYPE_POWERBJHF:
		strRst += ("油压告警恢复");
		break;
	case ALARM_TYPE_TEMPERATUREHF:
		strRst += ("温度预警恢复");
		break;
	case ALARM_TYPE_HUMILITYHF:
		strRst += ("湿度预警恢复");
		break;
	default:
		strRst += ("未知");
		break;
	}

	strRst += ("，");
	string strAckTime = "0xFFFFFFFF";
	if (data.alarmtype >= 100)
	{
		TIME ackTime = Time_tToSystemTime(data.alarmconfirm);
		TimeToString(ackTime, strAckTime);
	}

	snprintf(buf, sizeof(buf), ("报警确认信号:%s"), strAckTime.c_str());
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

	uint8_t* cbFill = ((uint8_t*)&data.filldata);

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

		tstring sunit = (data.alarmtype % 10 == 7 ? ("℃") : ("%"));
		strRst += acqType + ("，");
		strRst += ("描述:");

		strRst += (data.alarmtype % 10 == 7 ? ("温度:") : ("湿度:"));

		snprintf(buf, sizeof(buf), ("(%.2f%s)"), ((float)data.gap) / 100.0, sunit.c_str());
		strRst += buf;

		if (data.alarmtype <= 100)
		{
			if (data.lrsign == 1)
			{
				snprintf(buf, sizeof(buf), ("大于%s上限(%.2f%s)"), mapAlarmLevel[data.alarmtype].c_str(), ((float)data.std) / 100.0, sunit.c_str());
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				snprintf(buf, sizeof(buf), ("小于%s下限(%.2f%s)"), mapAlarmLevel[data.alarmtype].c_str(), ((float)data.offset) / 100.0, sunit.c_str());
				strRst += buf;
			}
		}

		return strRst;
	}
	//油位报警
	else if (data.alarmtype == ALARM_TYPE_YWYJ || data.alarmtype == ALARM_TYPE_YWYJHF
		|| data.alarmtype == ALARM_TYPE_YWBJ || data.alarmtype == ALARM_TYPE_YWBJHF)
	{
		strRst += acqType + ("，");
		strRst += ("描述:");

		snprintf(buf, sizeof(buf), ("油位(%dmm)"), data.gap);
		strRst += buf;
		if (data.alarmtype <= 100)
		{
			if (data.lrsign == 1)
			{
				snprintf(buf, sizeof(buf), ("大于%s上限(%dmm)"), mapAlarmLevel[data.alarmtype].c_str(), data.std);
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				snprintf(buf, sizeof(buf), ("小于%s下限(%dmm)"), mapAlarmLevel[data.alarmtype].c_str(), data.offset);
				strRst += buf;
			}
		}
		return strRst;
	}
	//油压报警
	else if (data.alarmtype == ALARM_TYPE_POWERYJHF || data.alarmtype == ALARM_TYPE_POWERBJHF
		|| data.alarmtype == ALARM_TYPE_POWERYJ || data.alarmtype == ALARM_TYPE_POWERBJ)
	{
		strRst += ("，");
		strRst += ("描述:");
		if (data.fixorinvert == 1)
			strRst += ("定到反");
		else
			strRst += ("反到定");

		strRst += (":");

		switch (cbFill[0])
		{
		case 1:
			strRst += ("解锁阶段");
			break;
		case 2:
			strRst += ("动作阶段");
			break;
		case 3:
			strRst += ("锁闭阶段");
			break;
		case 4:
			strRst += ("释压阶段");
			break;
		default:
			break;
		}
		snprintf(buf, sizeof(buf), ("油压最大值(%.2fMPa)"), ((float)data.gap) / 100.0);
		strRst += buf;
		if (data.alarmtype <= 100)
		{
			if (data.lrsign == 1)
			{
				snprintf(buf, sizeof(buf), ("大于上限(%.2fMPa)"), ((float)data.std) / 100.0);
				strRst += buf;
			}
			else if (data.lrsign == 2)
			{
				snprintf(buf, sizeof(buf), ("小于下限(%.2fMPa)"), ((float)data.offset) / 100.0);
				strRst += buf;
			}
		}
		return strRst;
	}
	else if (data.alarmtype == ALARM_TYPE_QKYJ || data.alarmtype == ALARM_TYPE_QKYJHF
		|| data.alarmtype == ALARM_TYPE_QKBJ || data.alarmtype == ALARM_TYPE_QKBJHF)
	{
		strRst += ("，");
		strRst += ("描述:");
		switch (cbFill[0])
		{
		case 1:
			strRst += ("静态缺口:");
			break;
		case 2:
			strRst += GetGapAcqTypeName(GapAcqTypeOfFile::egatofTrigger, false) + ("后缺口:");
			break;
		case 3:
			strRst += ("过车缺口:");
			break;
		case 4:
			strRst += ("过车前缺口:");
			break;
		case 5:
			strRst += ("过车后缺口:");
			break;
		default:
			break;
		}

		if (data.fixorinvert == 1)
			strRst += ("反位缺口");
		else
			strRst += ("定位缺口");

		snprintf(buf, sizeof(buf), ("(%.2fmm)"), ((float)data.gap) / 100.0);
		strRst += buf;

		if (data.alarmtype <= 100)
		{
			if (cbFill[3] == 1)
			{
				snprintf(buf, sizeof(buf), ("大于%s上限(%.2fmm)"), mapAlarmLevel[data.alarmtype].c_str(), ((float)(*(uint16_t*)&cbFill[1])) / 100.0);
				strRst += buf;
			}
			else if (cbFill[3] == 2)
			{
				snprintf(buf, sizeof(buf), ("小于%s下限(%.2fmm)"), mapAlarmLevel[data.alarmtype].c_str(), ((float)(*(uint16_t*)&cbFill[1])) / 100.0);
				strRst += buf;
			}
		}
		return strRst;
	}
	else
	{
		strRst += acqType + ("，");
		strRst += ("定反位:");
		if (data.fixorinvert == 0)
			strRst += ("定位");
		else
			strRst += ("反位");

		strRst += ("，");
		strRst += ("左右偏标志:");
		if (data.lrsign == 1)
			strRst += ("左偏");
		else if (data.lrsign == 2)
			strRst += ("右偏");
		else
			strRst += ("无效");
	}

	if (data.alarmtype == ALARM_TYPE_POWERYJHF || data.alarmtype == ALARM_TYPE_POWERBJHF
		|| data.alarmtype == ALARM_TYPE_POWERYJ || data.alarmtype == ALARM_TYPE_POWERBJ)
	{
		strRst += ("，");
		snprintf(buf, sizeof(buf), ("动作杆伸缩方向:%s"), data.offset == 0 ? "拉入" : "伸出");
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("预警/报警起始点数:%d"), data.gap);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("预警/报警结束点数:%d"), data.std);
		strRst += buf;
	}
	else
	{
		strRst += ("，");
		snprintf(buf, sizeof(buf), ("偏移值:%.2fmm"), data.offset / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("缺口值:%.2fmm"), data.gap / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("标准值:%.2fmm"), data.std / 100.0);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("图像长度:%d"), data.imglen);
		strRst += buf;
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StActionInfo& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.collecttime;
	TIME tm = timeopt::Unix2SysTime(data.collecttime);
	snprintf(buf, sizeof(buf), ("图像采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	tm = timeopt::Unix2SysTime(data.actioncttime);
	snprintf(buf, sizeof(buf), ("道岔动作时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	strRst += ("定反位:");
	if (data.fixorinvert == 0)
		strRst += ("定位");
	else
		strRst += ("反位");

	strRst += ("，");
	strRst += ("左右偏标志:");
	if (data.lrsign == 1)
		strRst += ("左偏");
	else if (data.lrsign == 2)
		strRst += ("右偏");
	else
		strRst += ("无效");

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("偏移值:%.2fmm"), data.offset / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("缺口值:%.2fmm"), data.gap / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("标准值:%.2fmm"), data.std / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("图像长度:%d"), data.imglen);
	strRst += buf;

	strRst += ("，");
	strRst += ("图像内容:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const StActionInfoRec& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.collecttime;
	TIME tm = timeopt::Unix2SysTime(data.collecttime);
	snprintf(buf, sizeof(buf), ("图像采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	tm = timeopt::Unix2SysTime(data.actioncttime);
	snprintf(buf, sizeof(buf), ("道岔动作时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	strRst += ("定反位:");
	if (data.fixorinvert == 0)
		strRst += ("定位");
	else
		strRst += ("反位");

	strRst += ("，");
	strRst += ("左右偏标志:");
	if (data.lrsign == 1)
		strRst += ("左偏");
	else if (data.lrsign == 2)
		strRst += ("右偏");
	else
		strRst += ("无效");

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("偏移值:%.2fmm"), data.offset / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("缺口值:%.2fmm"), data.gap / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("标准值:%.2fmm"), data.std / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("图像长度:%d"), data.imglen);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StLastGapImgReq& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StLastGapImgRes& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("图像采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	strRst += ("定反位:");
	if (data.fixorinvert == 0)
		strRst += ("定位");
	else
		strRst += ("反位");

	strRst += ("，");
	strRst += ("左右偏标志:");
	if (data.lrsign == 1)
		strRst += ("左偏");
	else if (data.lrsign == 2)
		strRst += ("右偏");
	else
		strRst += ("无效");

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("偏移值:%.2fmm"), data.offset / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("缺口值:%.2fmm"), data.gap / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("标准值:%.2fmm"), data.std / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("图像/视频长度:%d"), data.imglen);
	strRst += buf;

	strRst += ("，");
	strRst += ("图像/视频内容:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const StImgListReq& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;
	strRst += (", 请求编号:") + to_string(data.resqid);

	strRst += ("，");
	//CTime tm = data.begintime;
	TIME tm = timeopt::Unix2SysTime(data.begintime);
	snprintf(buf, sizeof(buf), ("开始时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	tm = timeopt::Unix2SysTime(data.endtime);
	snprintf(buf, sizeof(buf), ("结束时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	strRst += ("图像类型:");
	if (data.imgtype == 0)
		strRst += ("静态图像列表");
	else if (data.imgtype == 2)
		strRst += ("操纵后图像列表");
	else if (data.imgtype == 3)
		strRst += ("过车时图像列表");
	else
		strRst += ("所有类型");

	return strRst;
}

tstring Parse315Protocol::ToString(const StImgListRes& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;
	strRst += (", 请求编号:") + to_string(data.resqid);

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("记录数:%d"), data.cnt);
	strRst += buf;

	//for (int i = 0; i < data.cnt; ++i)
	//{
	//	StFileRecord& frec = ((StFileRecord*)data.lprecord)[i];

	//	snprintf(buf, sizeof(buf), ("文件记录%d{"), i);
	//	strRst += buf;

	//	CTime tm = frec.time;
	//	snprintf(buf, sizeof(buf), ("时间:%04d-%02d-%02d %02d:%02d:%02d "),
	//		tm.GetYear(), tm.GetMonth(), tm.GetDay(), tm.GetHour(), tm.GetMinute(), tm.GetSecond());
	//	strRst += buf;

	//	snprintf(buf, sizeof(buf), ("定反位(0 定位，1 反位):%d "), frec.fixorinvert);
	//	strRst += buf;

	//	snprintf(buf, sizeof(buf), ("左右偏标志(0 无效，1 左偏，2 右偏):%d "), frec.lrsign);
	//	strRst += buf;

	//	snprintf(buf, sizeof(buf), ("图像类型(315协议中定义，0 静态图像列表， 1 操纵后图像列表， 2 过车时图像列表):%d "), frec.imgtype);
	//	strRst += buf;

	//	snprintf(buf, sizeof(buf), ("偏移值:%.2fmm "), frec.offset / 100.0);
	//	strRst += buf;

	//	snprintf(buf, sizeof(buf), ("缺口值:%.2fmm "), frec.gap / 100.0);
	//	strRst += buf;

	//	snprintf(buf, sizeof(buf), ("标准值:%.2fmm "), frec.std / 100.0);
	//	strRst += buf;

	//	snprintf(buf, sizeof(buf), ("图像长度:%d"), frec.imglen);
	//	strRst += buf;

	//	strRst += ("} ");
	//}

	return strRst;
}

tstring Parse315Protocol::ToString(const StImgInfoReq& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	strRst += ("图像类型:");
	if (data.imgtype == 0)
		strRst += ("静态图像列表");
	else if (data.imgtype == 2)
		strRst += ("操纵后图像列表");
	else if (data.imgtype == 3)
		strRst += ("过车时图像列表");
	else
		strRst += ("所有类型");

	return strRst;
}

tstring Parse315Protocol::ToString(const StImgInfoRes& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	strRst += ("图像类型:");
	if (data.imgtype == 0)
		strRst += ("静态图像");
	else if (data.imgtype == 2)
		strRst += ("操纵后图像");
	else if (data.imgtype == 3)
		strRst += ("过车时图像");
	else
		strRst += (" ");

	strRst += ("，");
	strRst += ("定反位:");
	if (data.fixorinvert == 0)
		strRst += ("定位");
	else
		strRst += ("反位");

	strRst += ("，");
	strRst += ("左右偏标志:");
	if (data.lrsign == 1)
		strRst += ("左偏");
	else if (data.lrsign == 2)
		strRst += ("右偏");
	else
		strRst += ("无效");

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("偏移值:%.2fmm"), data.offset / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("缺口值:%.2fmm"), data.gap / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("标准值:%.2fmm"), data.std / 100.0);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("图像长度:%d"), data.imglen);
	strRst += buf;

	strRst += ("，");
	strRst += ("图像内容:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const StVedioListReq& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	strRst += ("视频类型:");
	if (data.vediotype == 1)
		strRst += ("道岔动作视频");
	else if (data.vediotype == 2)
		strRst += ("过车视频");
	else
		strRst += (" ");
	strRst += (", 请求编号:") + to_string(data.resqid);

	strRst += ("，");
	//CTime tm = data.begintime;
	TIME tm = timeopt::Unix2SysTime(data.begintime);
	snprintf(buf, sizeof(buf), ("开始时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	tm = timeopt::Unix2SysTime(data.endtime);
	snprintf(buf, sizeof(buf), ("结束时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StVedioListRes& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	strRst += ("视频类型:");
	if (data.vediotype == 1)
		strRst += ("道岔动作视频");
	else if (data.vediotype == 2)
		strRst += ("过车视频");
	else
		strRst += (" ");
	strRst += (", 请求编号:") + to_string(data.resqid);

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("记录数:%d \r\n"), data.cnt);
	strRst += buf;

	for (int i = 0; i < data.cnt; ++i)
	{
		StVedioRecord& vrec = ((StVedioRecord*)data.lprecord)[i];

		snprintf(buf, sizeof(buf), ("视频记录%d{"), i);
		strRst += buf;

		//CTime tm = vrec.time;
		TIME tm = timeopt::Unix2SysTime(vrec.time);
		snprintf(buf, sizeof(buf), ("开始录制时间:%04d-%02d-%02d %02d:%02d:%02d "),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("录制前定反位(0 定位，1 反位):%d "), vrec.fixorinvert1);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("录制后定反位(0 定位，1 反位):%d "), vrec.fixorinvert2);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("视频长度:%d "), vrec.len);
		strRst += buf;

		strRst += ("} \r\n");
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StVedioFileReq& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	strRst += ("视频类型:");
	if (data.vediotype == 1)
		strRst += ("道岔动作视频");
	else if (data.vediotype == 2)
		strRst += ("过车视频");
	else
		strRst += (" ");

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("开始时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	return strRst;
}

template<typename T>
tstring Parse315Protocol::ToString(const T* data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data->sid);
	strRst += buf;

	strRst += ("，");
	strRst += ("视频类型:");
	if (data->vediotype == 1)
		strRst += ("道岔动作视频");
	else if (data->vediotype == 2)
		strRst += ("过车视频");
	else
		strRst += (" ");

	strRst += ("，");
	//CTime tm = data->time;
	TIME tm = timeopt::Unix2SysTime(data->time);
	snprintf(buf, sizeof(buf), ("开始时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	strRst += ("录制前定反位:");
	if (data->fixorinvert1 == 0)
		strRst += ("定位");
	else
		strRst += ("反位");

	strRst += ("，");
	strRst += ("录制后定反位:");
	if (data->fixorinvert2 == 0)
		strRst += ("定位");
	else
		strRst += ("反位");

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("视频时长:%d"), data->timelen);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("视频长度:%d"), data->len);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("总包数:%d"), data->packcnt);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("本包序号:%d"), data->curpackid);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("本包内容长:%d"), data->datalen);
	strRst += buf;

	strRst += ("，");
	strRst += ("本包内容:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const St1DQJInfo& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	strRst += ("开关量状态:");
	if (data.status == 1)
		strRst += ("转辙机启动扳动");
	else if (data.status == 2)
		strRst += ("转辙机扳动结束");
	else if (data.status == 3)
		strRst += ("道岔区段有车");
	else if (data.status == 4)
		strRst += ("道岔区段车出清");
	else
		strRst += (" ");

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StOilPreCurve& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("采集频率:%d"), data.collectfreq);
	strRst += buf;

	strRst += ("，");
	strRst += ("定反位操作方向:");
	strRst += GetOptDirDesc(data.direct);

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("曲线条数:%d"), data.cnt);
	strRst += buf;

	uint8_t* cbFill = (uint8_t*)&data.filldata;

	int nVer = (cbFill[1] == 0xFF ? 2015 : (cbFill[1] + 2000));
	for (int i = 0; i < data.cnt; ++i)
	{
		StCurve& cdata = ((StCurve*)data.lpdata)[i];

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("曲线数据[%d]{"), i);
		strRst += buf;

		strRst += ("曲线类型:");
		strRst += GetCurveTypeDesc(cdata.type, nVer);

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("本条曲线数据长度:%d"), cdata.len);
		strRst += buf;

		strRst += ("，");
		strRst += ("曲线数据:{...}");

		strRst += ("}");
	}


	return strRst;
}

tstring Parse315Protocol::ToString(const StOilPreCurveRec& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("采集频率:%d"), data.collectfreq);
	strRst += buf;

	strRst += ("，");
	strRst += ("定反位操作方向:");
	strRst += GetOptDirDesc(data.direct);

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("曲线条数:%d"), data.cnt);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StOilLevelInfo& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机个数:%d"), data.cnt);
	strRst += buf;

	strRst += "\r\n";

	for (int i = 0; i < data.cnt; ++i)
	{
		StSdataRecord& srec = ((StSdataRecord*)data.lpdata)[i];

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("转辙机数据记录%d{"), i);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("转辙机ID:%d"), srec.sid);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("油位:%d"), srec.oillevel);
		strRst += buf;

		strRst += ("，");
		//CTime tm = srec.oiltime;
		TIME tm = timeopt::Unix2SysTime(srec.oiltime);
		snprintf(buf, sizeof(buf), ("油位时间:%04d-%02d-%02d %02d:%02d:%02d"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		strRst += ("，");
		strRst += ("摄像头状态:");
		if (srec.camerastate == 0)
			strRst += ("正常");
		else if (srec.camerastate == 1)
			strRst += ("故障");
		else if (srec.camerastate == 0xFF)
			strRst += ("不监测");
		else
			strRst += (" ");

		strRst += ("，");
		tm = timeopt::Unix2SysTime(srec.camtime);
		snprintf(buf, sizeof(buf), ("摄像头采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("采集设备温度:%0.2f"), srec.temperature / 100.0);
		strRst += buf;

		strRst += ("，");
		tm = timeopt::Unix2SysTime(srec.temptime);
		snprintf(buf, sizeof(buf), ("温度采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("采集设备湿度:%0.2f"), srec.humidity / 100.0);
		strRst += buf;

		strRst += ("，");
		tm = timeopt::Unix2SysTime(srec.humtime);
		snprintf(buf, sizeof(buf), ("湿度采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		strRst += ("}");

		strRst += "\r\n";
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StParamSet& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };

	strRst += ("命令类型:");
	if (data.cmdtype == 1)
		strRst += ("获取");
	else if (data.cmdtype == 2)
		strRst += ("设置");
	else
		strRst += (" ");

	strRst += ("，");
	strRst += ("参数类型:");
	if (data.paramtype == 1)
		strRst += ("报警参数");
	else if (data.paramtype == 2)
		strRst += ("预警参数");
	else
		strRst += (" ");

	strRst += ("，");
	strRst += ("参数内容:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const StRealCtrlReq& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	strRst += ("定反位:");
	if (data.fixorinvert == 0)
		strRst += ("定位");
	else
		strRst += ("反位");

	switch (data.cmdtype)
	{
	case 0x01:
	{
		strRst += ("，");
		strRst += ("命令类型:请求码流");

		StRealReqStream& rreq = *(StRealReqStream*)data.lpdata;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("帧率:%d"), rreq.rate);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("图像宽度:%d"), rreq.width);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("图像高度:%d"), rreq.heigh);
		strRst += buf;

		break;
	}
	case 0x02:
	{
		strRst += ("，");
		strRst += ("命令类型:开始");
		break;
	}
	case 0x04:
	{
		strRst += ("，");
		strRst += ("命令类型:停止");
		break;
	}
	case 0x05:
	{
		strRst += ("，");
		strRst += ("命令类型:设置帧率");

		StRealSetRate& rrate = *(StRealSetRate*)data.lpdata;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("帧率:%d"), rrate.rate);
		strRst += buf;

		break;
	}
	case 0x06:
	{
		strRst += ("，");
		strRst += ("命令类型:设置图像大小");

		StRealSetSize& rsize = *(StRealSetSize*)data.lpdata;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("图像宽度:%d"), rsize.width);
		strRst += buf;

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("图像高度:%d"), rsize.heigh);
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
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	switch (data.result)
	{
	case 0x00:
	{
		strRst += ("，");
		strRst += ("响应结果:失败");

		if (*(uint8_t*)data.lpdata == 1)
		{
			strRst += ("，");
			strRst += ("失败原因: 摄像头正在拍照，请5s后再直播");
		}
		else
		{
			strRst += ("，");
			strRst += ("失败原因: 直播失败");
		}

		break;
	}
	case 0x01:
	{
		strRst += ("，");
		strRst += ("响应结果:成功");
		break;
	}
	case 0x02:
	{
		strRst += ("，");
		strRst += ("响应结果:已有用户建立连接");

		uint32_t ip = *(uint32_t*)data.lpdata;

		unsigned char* pIpByte = (unsigned char*)&ip;
		

		strRst += ("，");
		snprintf(buf, sizeof(buf), ("连接用户IP:%d.%d.%d.%d"), pIpByte[0], pIpByte[1], pIpByte[2], pIpByte[3]);
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
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("实时码流包序号:%d"), data.packid);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("实时码流包长度:%d"), data.packlen);
	strRst += buf;

	strRst += ("，");
	strRst += ("实时码流包数据:{...}");

	return strRst;
}


bool Parse315Protocol::CheckPackData(StFrame& data, void* buf, int len, int dir)
{
	void*  pos = buf;
	int pklen = 0;
	if (!Parse315Protocol::GetFrameData(pos, len, pklen)
		|| pos != buf
		|| pklen != len)
	{
		return false;
	}


	return Parse315Protocol::Parse(data, buf, len, dir);
}

bool Parse315Protocol::Parse(StElecCurve& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 1 + 1 + 4 + 4 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.cnt > len)
		return false;

	bool failed = false;
	if (data.cnt != 0)
	{
		sz = data.cnt * sizeof(StElecCurveData);
		data.lpdata = new uint8_t[sz];
		memset(data.lpdata, 0,  sz);

		for (int i = 0; i < data.cnt; ++i)
		{
			StElecCurveData& curv = ((StElecCurveData*)data.lpdata)[i];

			sz = 1 + 4 + 2 + 2;
			szcnt += sz;
			if (szcnt > len)
			{
				failed = true;
				break;
			}
			memcpy(&curv, pos, sz);
			pos += sz;

			if (curv.datalen != 0 && curv.datalen == curv.datacnt * 2)
			{
				sz = curv.datalen;
				szcnt += sz;
				if (szcnt > len)
				{
					failed = true;
					break;
				}
				curv.lpdata = new uint8_t[sz];
				memcpy(curv.lpdata, pos, sz);
				pos += sz;
			}

		}
	}

	if (failed || pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

bool Parse315Protocol::Release(StElecCurve& data)
{
	if (data.cnt == 0 || data.lpdata == NULL)
		return true;

	for (int i = 0; i < data.cnt; ++i)
	{
		StElecCurveData& curv = ((StElecCurveData*)data.lpdata)[i];

		if (curv.datalen != 0 && curv.lpdata != NULL)
			delete[] (uint8_t*)curv.lpdata;
	}

	delete[] (uint8_t*)data.lpdata;

	return true;
}

// bool Parse315Protocol::Release(StNewPowerNotify& data)
// {
// 	//delete[]data.;
// 
// 	return true;
// 
// }


bool Parse315Protocol::Release(StStaticPowerList& data)
{
	if (data.zzjcnt == 0 || data.lprecord == NULL)
		return true;

	delete[] (uint8_t*)data.lprecord;

	return true;
}

bool Parse315Protocol::Parse(StElecCurveRec& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2 + 4 + 1 + 1 + 4 + 4 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (pos != (uint8_t*)buf + len)
	{
		return false;
	}

	return true;
}

bool Parse315Protocol::Parse(StStaticPowerList& data, void* buf, int len)
{
	memset(&data, 0,  sizeof(data));

	int szcnt = 0;
	uint8_t* pos = (uint8_t*)buf;

	size_t sz = 1 + 2;
	szcnt += sz;
	if (szcnt > len)
		return false;
	memcpy(&data, pos, sz);
	pos += sz;

	if (data.zzjcnt != 0)
	{
		sz = data.zzjcnt * sizeof(StStaticPowerData);
		szcnt += sz;
		if (szcnt > len)
			return false;
		data.lprecord = new uint8_t[sz];
		memcpy(data.lprecord, pos, sz);
		pos += sz;
	}

	if (pos != (uint8_t*)buf + len)
	{
		Parse315Protocol::Release(data);
		return false;
	}
	return true;
}

tstring Parse315Protocol::ToString(const StElecCurve& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("采集频率:%d"), data.freq);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("方向:%s"), GetOptDirDesc(data.dir).c_str());
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("动作次数:%d"), data.acttime);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("曲线条数:%d"), data.cnt);
	strRst += buf;

	if (data.cnt > 0 && data.lpdata != NULL)
	{
		for (int i = 0; i < data.cnt; ++i)
		{
			StElecCurveData& curv = ((StElecCurveData*)data.lpdata)[i];

			strRst += ("，");
			snprintf(buf, sizeof(buf), ("曲线%d{"), i);
			strRst += buf;

			snprintf(buf, sizeof(buf), ("曲线类型:%s"), GetElecCurveTypeDesc(curv.type).c_str());
			strRst += buf;

			strRst += ("，");
			snprintf(buf, sizeof(buf), ("曲线数据点数:%d"), curv.datacnt);
			strRst += buf;

			strRst += ("}");
		}
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StElecCurveRec& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("采集频率:%d"), data.freq);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("方向:%s"), GetOptDirDesc(data.dir).c_str());
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("动作次数:%d"), data.acttime);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("曲线条数:%d"), data.cnt);
	strRst += buf;

	return strRst;
}

tstring Parse315Protocol::ToString(const StPowerFileListF2* pPowerFileList, int nItemCount)
{
	tstring strRst = ("");
	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("文件数量:%d\n"), pPowerFileList->nListCount);
	strRst += buf;

	for (int i = 0; i < pPowerFileList->nListCount; ++i)
	{
		//StStaticPowerData& rec = ((StStaticPowerData*)data.lprecord)[i];


		snprintf(buf, sizeof(buf), ("转辙机ID:%d,"), pPowerFileList->list[i].sid);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("数据类型:%d,"), pPowerFileList->list[i].cbDataType);
		strRst += buf;

		//strRst += " 检测对象类型:" + GetAcqDescByAcqObjType((ACQ_OBJ_TYPE)pPowerFileList->list[i].acqObjType);

		//CTime tm = pPowerFileList->list[i].time;
		TIME tm = timeopt::Unix2SysTime(pPowerFileList->list[i].time);
		snprintf(buf, sizeof(buf), (",开始时间:%04d-%02d-%02d %02d:%02d:%02d\n"),
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;
	}
	return strRst;
}
tstring Parse315Protocol::ToString(const StPowerFileData& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("采集时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，采集对象类型:");
	//strRst += GetAcqDescByAcqObjType((ACQ_OBJ_TYPE)data.acqObjType);

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("结果(0：成功 1：文件不存在 2：文件被占用):%d"), data.result);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("文件总长度:%d"), data.fileLen);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("总包数:%d"), data.pakAll);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("包序号:%d"), data.pakNo);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("本包包长:%d\n"), data.pakLen);
	strRst += buf;

	strRst += ("，");
	strRst += (char*)data.dataInfo;

	return strRst;
}
//
//tstring Parse315Protocol::ToString(const StNewPowerNotify& data)
//{
//	tstring strRst = ("");
//
//	char buf[256] = { 0 };
//
//	//snprintf(buf, sizeof(buf), ("转辙机数量:%d\n"), data.zzjcnt);
//	//strRst += buf;
//
//	//if (data.zzjcnt > 0 && data.lprecord != NULL)
//	//{
//	//	for (int i = 0; i < data.zzjcnt; ++i)
//		{
//			//StStaticPowerData& rec = ((StStaticPowerData*)data.lprecord)[i];
//
//
//			snprintf(buf, sizeof(buf), ("转辙机ID:%d,"), data.sid);
//			strRst += buf;
//
//			snprintf(buf, sizeof(buf), ("数据类型:%d,"), data.cbDataType);
//			strRst += buf;
//			
//			strRst += " 检测对象类型:" + GetAcqDescByAcqObjType((ACQ_OBJ_TYPE)data.acqObjType);
//
//			CTime tm = data.time;
//			snprintf(buf, sizeof(buf), (",开始时间:%04d-%02d-%02d %02d:%02d:%02d\n"),
//				tm.GetYear(), tm.GetMonth(), tm.GetDay(), tm.GetHour(), tm.GetMinute(), tm.GetSecond());
//			strRst += buf;
//		}
//	//}
//
//	return strRst;
//}


tstring Parse315Protocol::ToString(const StStaticPowerList& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机数量:%d\n"), data.zzjcnt);
	strRst += buf;

	if (data.zzjcnt > 0 && data.lprecord != NULL)
	{
		for (int i = 0; i < data.zzjcnt; ++i)
		{
			StStaticPowerData& rec = ((StStaticPowerData*)data.lprecord)[i];

			strRst += ("，");

			snprintf(buf, sizeof(buf), ("转辙机ID:%d"), rec.zzjid);
			strRst += buf;

			snprintf(buf, sizeof(buf), ("阻力值:%.1f"), (float)rec.powVal);
			strRst += buf;

			//CTime tm = rec.acqtime;
			TIME tm = timeopt::Unix2SysTime(rec.acqtime);
			snprintf(buf, sizeof(buf), ("采集时间:%04d-%02d-%02d %02d:%02d:%02d\n"),
				tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
			strRst += buf;
		}
	}

	return strRst;
}

tstring Parse315Protocol::ToString(const StPowerListReq& data)
{
	tstring strRst = ("");
	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机ID:%d, "), data.sid);
	strRst += buf;

	strRst += ("方向:");
	strRst += GetOptDirDesc(data.direction);
	//CTime tmStart = data.begintime;
	//CTime tmEnd = data.endtime;
	TIME tmStart = timeopt::Unix2SysTime(data.begintime);
	TIME tmEnd = timeopt::Unix2SysTime(data.endtime);

	snprintf(buf, sizeof(buf), (", 查询时间范围:[%04d-%02d-%02d %02d:%02d:%02d] ~ [%04d-%02d-%02d %02d:%02d:%02d]"),
		tmStart.wYear, tmStart.wMonth, tmStart.wDay, tmStart.wHour, tmStart.wMinute, tmStart.wSecond,
		tmEnd.wYear, tmEnd.wMonth, tmEnd.wDay, tmEnd.wHour, tmEnd.wMinute, tmEnd.wSecond);

	strRst += buf;
	return strRst;
}

tstring Parse315Protocol::ToString(const StPowerListRes& data)
{
	tstring strRst = ("");
	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机ID:%d, "), data.sid);
	strRst += buf;

	strRst += ("方向:");
	strRst += GetOptDirDesc(data.direction);


	snprintf(buf, sizeof(buf), (",数目:%d\n"), data.cnt);
	strRst += buf;

	StPowerRecord* pRecord = (StPowerRecord*)data.lprecord;
	for (uint16_t i = 0; i < data.cnt; i++)
	{
		//CTime tm = pRecord[i].time;
		TIME tm = timeopt::Unix2SysTime(pRecord[i].time);
		snprintf(buf, sizeof(buf), ("[%d] 时间:%04d-%02d-%02d %02d:%02d:%02d, "), i + 1,
			tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("采集频率:%d, "), pRecord[i].acqfreq);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("动作次数:%d, "), pRecord[i].movecount);
		strRst += buf;

		strRst += ("方向:");
		strRst += GetOptDirDesc(pRecord[i].dir);

		snprintf(buf, sizeof(buf), (", 计算结果:%d, "), pRecord[i].calresult);
		strRst += buf;

		snprintf(buf, sizeof(buf), ("曲线条数:%d\n"), pRecord[i].curvenum);
		strRst += buf;

	}
	return strRst;
}

tstring Parse315Protocol::ToString(const StPowerInfoReq& data)
{
	tstring strRst = ("");

	return strRst;
}

tstring Parse315Protocol::ToString(const StPowerInfoRes& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };
	snprintf(buf, sizeof(buf), ("转辙机ID:%d"), data.sid);
	strRst += buf;

	strRst += ("，");
	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), ("时间:%04d-%02d-%02d %02d:%02d:%02d"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("采集频率:%d"), data.acqfreq);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("动作次数:%d"), data.movecount);
	strRst += buf;

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("计算结果:%d"), data.calresult);
	strRst += buf;


	strRst += ("，");
	strRst += ("定反位操作方向:");
	strRst += GetOptDirDesc(data.direction);

	strRst += ("，");
	snprintf(buf, sizeof(buf), ("曲线条数:%d"), data.cnt);
	strRst += buf;

	strRst += ("，");
	strRst += ("曲线数据:{...}");

	return strRst;
}

tstring Parse315Protocol::ToString(const StManualOilingRes& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机ID:%d, "), data.sid);
	strRst += buf;

	strRst += ("定反位:");
	if (data.fixorinvert == 0)
		strRst += ("定位,");
	else
		strRst += ("反位, ");
	strRst += ("结果:");
	if (data.result == 0)
		strRst += ("成功");
	if (data.result == 1)
		strRst += ("没有找到设备");
	if (data.result == 2)
		strRst += ("不支持手动加油功能");

	return strRst;
}
tstring Parse315Protocol::ToString(const StOilingResultNotify& data)
{
	tstring strRst = ("");

	char buf[256] = { 0 };

	snprintf(buf, sizeof(buf), ("转辙机ID:%d, "), data.sid);
	strRst += buf;

	strRst += ("定反位:");
	if (data.fixorinvert == 0)
		strRst += ("定位,");
	else
		strRst += ("反位, ");
	strRst += ("加油方式:") + GetJiaYouTypeDesc(data.type);
	strRst += (", 加油结果:");
	if (data.status == 2)
		strRst += ("加油失败,");
	else if (data.status == 1)
		strRst += ("加油成功,");
	else
		strRst += ("未知, ");

	strRst += (", 异常码:");
	if (data.failCode == 0)
		strRst += ("无问题, ");
	else if (data.failCode == 1)
		strRst += ("加油管堵塞 , ");
	else if (data.failCode == 2)
		strRst += ("加油管破损 , ");
	else if (data.failCode == 3)
		strRst += ("加油泵异常 , ");
	else if (data.failCode == 4)
		strRst += ("无压力传感器 , ");
	else if (data.failCode == 5)
		strRst += ("压力值比较小，可能存在漏油");

	//CTime tm = data.time;
	TIME tm = timeopt::Unix2SysTime(data.time);
	snprintf(buf, sizeof(buf), (" \n加油时间:%04d-%02d-%02d %02d:%02d:%02d\n"),
		tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
	strRst += buf;

	//CTime tmTrigger = data.triggerTime;
	TIME tmTrigger = timeopt::Unix2SysTime(data.triggerTime);
	snprintf(buf, sizeof(buf), ("触发时间:%04d-%02d-%02d %02d:%02d:%02d\n"),
		tmTrigger.wYear, tmTrigger.wMonth, tmTrigger.wDay, tmTrigger.wHour, tmTrigger.wMinute, tmTrigger.wSecond);
	strRst += buf;

	strRst += ("持续时间:") + to_string(data.timeLen);

	return strRst;
}

uint8_t Parse315Protocol::GetOptDirIndex(tstring direct)//根据操作方向类型描述文本获取对应值
{
	if (direct == ("定到反"))
		return DIRECT_DW_TO_FW;
	else if (direct == ("反到定"))
		return DIRECT_FW_TO_DW;
	else if (direct == ("定到定"))
		return DIRECT_DW_TO_DW;
	else if (direct == ("反到反"))
		return DIRECT_FW_TO_FW;
	else if (direct == ("定位到故障位"))
		return DIRECT_DW_TO_GZ;
	else if (direct == ("反位到故障位"))
		return DIRECT_FW_TO_GZ;
	else if (direct == ("故障到定位"))
		return DIRECT_GZ_TO_DW;
	else if (direct == ("故障到反位"))
		return DIRECT_GZ_TO_FW;
	else if (direct == ("故障到故障位"))
		return DIRECT_GZ_TO_GZ;
	//else if (direct == ("无效"))                     

	return 0xFF;
}

//根据加油类型值获取对应描述文本
tstring Parse315Protocol::GetJiaYouTypeDesc(uint8_t nType)
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
		return ("定到反");
	case DIRECT_FW_TO_DW:
		return ("反到定");
	case DIRECT_DW_TO_DW:
		return ("定到定");
	case DIRECT_FW_TO_FW:
		return ("反到反");
	case DIRECT_DW_TO_GZ:
		return ("定位到故障位");
	case DIRECT_FW_TO_GZ:
		return ("反位到故障位");
	case DIRECT_GZ_TO_DW:
		return ("故障到定位");
	case DIRECT_GZ_TO_FW:
		return ("故障到反位");
	case DIRECT_GZ_TO_GZ:
		return ("故障到故障位");
	case 9:
		return ("定位过车");
	case 10:
		return ("反位过车");
	default:
		return ("无效[") + to_string(direct) + "]";
		break;
	}
	return ("");
}

tstring Parse315Protocol::GetOptDirDesc(eMOVE_DIRECT direct)
{
	switch (direct)
	{
	case eMOVE_DIRECT::fix2invert:
		return ("定到反");
	case eMOVE_DIRECT::invert2fix:
		return ("反到定");
	case eMOVE_DIRECT::fix2fix:
		return ("定到定");
	case eMOVE_DIRECT::invert2invert:
		return ("反到反");
	case eMOVE_DIRECT::fix2fault:
		return ("定位到故障位");
	case eMOVE_DIRECT::invert2fault:
		return ("反位到故障位");
	case eMOVE_DIRECT::fault2fix:
		return ("故障到定位");
	case eMOVE_DIRECT::fault2invert:
		return ("故障到反位");
	case eMOVE_DIRECT::fault2fault:
		return ("故障到故障位");
	case eMOVE_DIRECT::unknow:
		return ("无效");
		break;
	default:
		T_ASSERT(false);
	}
	return ("");
}

tstring Parse315Protocol::GetCurveTypeDesc(int CurveType, int nVer)//根据曲线类型值获取对应描述文本
{
	if (nVer < 2023)
	{
		switch (CurveType)
		{
		case 0x00: return ("左油压");
		case 0x01: return ("右油压");
		case 0x02: return ("道岔动作电流曲线");
		case 0x03: return ("道岔动作A相电流曲线");
		case 0x04: return ("道岔动作B相电流曲线");
		case 0x05: return ("道岔动作C相电流曲线");
		case 0x06: return ("道岔总功率曲线");
		case 0x07: return ("道岔动作A相电压曲线");
		case 0x08: return ("道岔动作B相电压曲线");
		case 0x09: return ("道岔动作C相电压曲线");
		case 0x0A: return ("道岔阻力曲线");
		default:
			break;
		}
	}
	else
	{
		switch (CurveType)
		{
		case 0x00: return ("左油压");
		case 0x01: return ("右油压");
		case 0x02: return ("电流曲线");
		case 0x03: return ("道岔阻力曲线");
		case 0x04: return ("外锁闭装置锁闭力曲线");
		case 0x05: return ("过车时缺口曲线");
		default:
			break;
		}

	}
	return ("");
}

tstring Parse315Protocol::GetAlarmTypeDesc(int Alarmtype, int nVer)//根据报警类型值获取对应描述文本
{
	if (nVer < 2023)
	{
		switch (Alarmtype)
		{
		case ALARM_TYPE_QKYJ:
			return ("缺口预警及预警图像");
			break;
		case ALARM_TYPE_QKBJ:
			return ("缺口报警及报警图像");
			break;
		case ALARM_TYPE_QKSBGZ:
			return ("缺口采集设备故障");
			break;
		case ALARM_TYPE_TXWFSB:
			return ("缺口图像无法识别报警");
			break;
		case ALARM_TYPE_GCKLGD:
			return ("过车时框量过大报警及过车视频");
			break;
		case ALARM_TYPE_ZZJSBGZ:
			return ("转辙机采集设备故障报警");
			break;
		case ALARM_TYPE_WDBJ:
			return ("温度报警");
			break;
		case ALARM_TYPE_SDBJ:
			return ("湿度报警");
			break;
		case ALARM_TYPE_YWYJ:
			return ("油位预警");
			break;
		case ALARM_TYPE_YWBJ:
			return ("油位报警");
			break;
		case ALARM_TYPE_POWERYJ:
			return ("阻力预警");
			break;
		case ALARM_TYPE_POWERBJ:
			return ("阻力报警");
			break;
		case ALARM_TYPE_QKYJHF:
			return ("缺口预警恢复及图像");
			break;
		case ALARM_TYPE_QKBJHF:
			return ("缺口报警恢复及图像");
			break;
		case ALARM_TYPE_QKSBGZHF:
			return ("缺口采集设备故障恢复及图像");
			break;
		case ALARM_TYPE_TXWFSBHF:
			return ("缺口图像无法识别报警恢复及图像");
			break;
		case ALARM_TYPE_ZZJSBGZHF:
			return ("转辙机采集设备故障报警恢复");
			break;
		case ALARM_TYPE_GCKLGDHF:
			return ("过车时框量过大报警恢复及过车视频");
			break;
		case ALARM_TYPE_WDBJHF:
			return ("温度报警恢复");
			break;
		case ALARM_TYPE_SDBJHF:
			return ("湿度报警恢复");
			break;
		case ALARM_TYPE_YWYJHF:
			return ("油位预警恢复");
			break;
		case ALARM_TYPE_YWBJHF:
			return ("油位报警恢复");
			break;
		case ALARM_TYPE_POWERYJHF:
			return ("阻力预警恢复");
			break;
		case ALARM_TYPE_POWERBJHF:
			return ("阻力报警恢复");
			break;
		default:
			return ("未知");
			break;
		}
	}
	else
	{
		switch (Alarmtype)
		{
		case 1:
			return ("转换后缺口预警及预警图像");
			break;
		case 2:
			return ("转换后缺口报警及报警图像");
			break;
		case 3:
			return ("缺口采集设备故障");
			break;
		case 4:
			return ("缺口图像无法识别报警");
			break;
		case 5:
			return ("过车时旷量过大报警及过车视频");
			break;
		case 6:
			return ("转辙机采集设备故障报警");
			break;
		case 7:
			return ("温度报警");
			break;
		case 8:
			return ("湿度报警");
			break;
		case 9:
			return ("油位预警");
			break;
		case 10:
			return ("油位报警");
			break;
		case 11:
			return ("油压预警");
			break;
		case 12:
			return ("油压报警");
			break;
		case 13:
			return ("道岔转换阻力超限报警");
			break;
		case 14:
			return ("外锁闭装置锁闭力超限报警");
			break;
		case 15:
			return ("过车时缺口值");
			break;
		case 16:
			return ("静态缺口预警");
			break;
		case 17:
			return ("静态缺口报警");
			break;
		case 101:
			return ("转换后缺口预警恢复及图像");
			break;
		case 102:
			return ("转换后缺口报警恢复及图像");
			break;
		case 103:
			return ("缺口采集设备故障恢复及图像");
			break;
		case 104:
			return ("缺口图像无法识别报警恢复及图像");
			break;
		case 105:
			return ("转辙机采集设备故障报警恢复");
			break;
		case 106:
			return ("过车时旷量过大报警恢复及过车视频");
			break;
		case 107:
			return ("温度报警恢复");
			break;
		case 108:
			return ("湿度报警恢复");
			break;
		case 109:
			return ("油位预警恢复");
			break;
		case 110:
			return ("油位报警恢复");
			break;
		case 111:
			return ("油压预警恢复");
			break;
		case 112:
			return ("油压报警恢复");
			break;
		case 113:
			return ("道岔转换阻力报警恢复");
			break;
		case 114:
			return ("外锁闭装置锁闭力超限报警恢复");
			break;
		case 115:
			return ("过车时缺口报警恢复");
			break;
		case 116:
			return ("静态缺口报警恢复");
			break;
		case 117:
			return ("静态缺口报警恢复");
			break;
		default:
			return ("未知");
			break;
		}
	}
	return ("");
}

tstring Parse315Protocol::GetElecCurveTypeDesc(int ElecCurveType)
{
	switch (ElecCurveType)
	{
	case ELEC_CURVE_TYPE_DZXJNUAB:
		return ("动作线电压Uab");
	case ELEC_CURVE_TYPE_DZXJNUBC:
		return ("动作线电压Ubc");
	case ELEC_CURVE_TYPE_DZXJNUAC:
		return ("动作线电压Uac");
	case ELEC_CURVE_TYPE_DZDLIA:
		return ("动作电流Ia");
	case ELEC_CURVE_TYPE_DZDLIB:
		return ("动作电流Ib");
	case ELEC_CURVE_TYPE_DZDLIC:
		return ("动作电流Ic");
	case ELEC_CURVE_TYPE_GLYS:
		return ("功率因素");
	case ELEC_CURVE_TYPE_ZYGGL:
		return ("总有功功率");
	case ELEC_CURVE_TYPE_ZL:
		return ("阻力");
	case ELEC_CURVE_TYPE_DZDYDW1:
		return ("动作电压定位1");
	case ELEC_CURVE_TYPE_DZDYFW1:
		return ("动作电压反位1");
	case ELEC_CURVE_TYPE_DZDYDW2:
		return ("动作电压定位2");
	case ELEC_CURVE_TYPE_DZDYFW2:
		return ("动作电压反位2");
	case ELEC_CURVE_TYPE_DZDLDW1:
		return ("动作电流定位1");
	case ELEC_CURVE_TYPE_DZDLFW1:
		return ("动作电流反位1");
	case ELEC_CURVE_TYPE_DZDLDW2:
		return ("动作电流定位2");
	case ELEC_CURVE_TYPE_DZDLFW2:
		return ("动作电流反位2");
	default:
		break;
	}
	return ("");
}