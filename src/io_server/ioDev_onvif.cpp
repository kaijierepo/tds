#include "pch.h"
#include <sstream>
#include <fstream>
#include "json.hpp"
#include "ioDev_onvif.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "secure.h"
#include "../data_server/tAlmSrv.h"

struct PTZPosition {
	float pan;    // 水平角度
	float tilt;   // 垂直角度
	float zoom;   // 变焦值
	bool isValid; // 位置是否有效
};

struct PresetInfo {
	std::string token;
	std::string name;
	PTZPosition position;
	bool isSet;
};

ioDev* createDev_onvif()
{
	return new ioDev_onvif();
}

class createReg_onvif {
public:
	createReg_onvif();
};
createReg_onvif::createReg_onvif()
{
	mapDevCreateFunc["onvif"] = createDev_onvif;
	mapDevTypeLabel["onvif"] = "Onvif摄像头";
}
createReg_onvif regonvif;

std::vector<PresetInfo> parsePresetsResponse(const std::string& response) {
	std::vector<PresetInfo> presets;

	size_t pos = 0;
	while (true) {
		size_t presetStart = response.find("<tptz:Preset", pos);
		if (presetStart == std::string::npos) break;

		size_t presetEnd = response.find("</tptz:Preset>", presetStart);
		if (presetEnd == std::string::npos) break;

		std::string presetXml = response.substr(presetStart,
			presetEnd - presetStart + std::string("</tptz:Preset>").length());

		PresetInfo preset;
		preset.isSet = false;
		preset.position.isValid = false;

		// 提取token
		size_t tokenStart = presetXml.find("token=\"");
		if (tokenStart != std::string::npos) {
			tokenStart += 7;
			size_t tokenEnd = presetXml.find("\"", tokenStart);
			if (tokenEnd != std::string::npos) {
				preset.token = presetXml.substr(tokenStart, tokenEnd - tokenStart);
			}
		}

		// 提取name
		size_t nameStart = presetXml.find("<tt:Name>");
		if (nameStart != std::string::npos) {
			nameStart += 9;
			size_t nameEnd = presetXml.find("</tt:Name>", nameStart);
			if (nameEnd != std::string::npos) {
				preset.name = presetXml.substr(nameStart, nameEnd - nameStart);
			}
		}

		// 提取PTZ位置信息
		size_t ptzStart = presetXml.find("<tt:PTZPosition>");
		if (ptzStart != std::string::npos) {
			// 提取pan值
			size_t panStart = presetXml.find("<tt:PanTilt x=\"", ptzStart);
			if (panStart != std::string::npos) {
				panStart += 15; // "<tt:PanTilt x=\"" 的长度
				size_t panEnd = presetXml.find("\"", panStart);
				if (panEnd != std::string::npos) {
					try {
						preset.position.pan = std::stof(presetXml.substr(panStart, panEnd - panStart));
						if (preset.position.pan != 0)
						{
							preset.position.isValid = true;
						}
					}
					catch (...) {
						preset.position.pan = 0.0f;
					}
				}
			}

			// 提取tilt值
			size_t tiltStart = presetXml.find("y=\"", ptzStart);
			if (tiltStart != std::string::npos) {
				tiltStart += 3; // "y=\"" 的长度
				size_t tiltEnd = presetXml.find("\"", tiltStart);
				if (tiltEnd != std::string::npos) {
					try {
						preset.position.tilt = std::stof(presetXml.substr(tiltStart, tiltEnd - tiltStart));
					}
					catch (...) {
						preset.position.tilt = 0.0f;
					}
				}
			}

			// 提取zoom值
			size_t zoomStart = presetXml.find("<tt:Zoom x=\"");
			if (zoomStart != std::string::npos) {
				zoomStart += 12; // "<tt:Zoom x=\"" 的长度
				size_t zoomEnd = presetXml.find("\"", zoomStart);
				if (zoomEnd != std::string::npos) {
					try {
						preset.position.zoom = std::stof(presetXml.substr(zoomStart, zoomEnd - zoomStart));
					}
					catch (...) {
						preset.position.zoom = 0.0f;
					}
				}
			}
		}

		// 通过position是否有效来判断预置位是否设置
		preset.isSet = preset.position.isValid;

		// 只要有token就添加到列表
		if (preset.isSet) {
			presets.push_back(preset);
		}

		pos = presetEnd + 1;
	}

	return presets;
}

void thread_doHeartbeat(ioDev_onvif* p) {
	p->m_bWorkingThreadRunning = true;
	p->onvif_getDevInfo();
	p->m_bWorkingThreadRunning = false;
}

ioDev_onvif::ioDev_onvif() {
	m_devType = "onvif";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_strUser = "admin";
	m_strPwd = "Tds-666666";
	m_level = "devcie";
	m_curPZTChan = 0;
	m_ptzPollInterval = 0;
	m_bPaused = false;
	m_pauseResumeInterval = 0;
}

ioDev_onvif::~ioDev_onvif()
{
}

bool ioDev_onvif::run() {
	m_bRunning = true;
	if (m_addrType == DEV_ADDR_MODE::tcpServer) {
		string ip;
		if (m_jDevAddr.contains("ip")) {
			ip = m_jDevAddr["ip"].get<string>();
		}
		else {
			LOG("[error]IODev启动失败,设备地址模式=tcpServer,没有找到ip配置");
			return false;
		}


		int port;
		if (m_jDevAddr.contains("port")) {
			port = m_jDevAddr["port"].get<int>();
		}
		else {
			LOG("[error]IODev启动失败,设备地址模式=tcpServer,没有找到port配置");
			return false;
		}
	}

	return true;
}

void ioDev_onvif::DoCycleTask() {
	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval) {
		if (!m_bWorkingThreadRunning) {
			timeopt::now(&m_stLastAcqTime);
			thread t(thread_doHeartbeat, this);
			t.detach();
		}
	}

	if (m_bPaused) {
		if (timeopt::CalcTimePassSecond(m_pauseResumeTime) > m_pauseResumeInterval) {
			m_bPaused = false;
		}
	}

	int ptzPatrolInterval = tds->conf->getInt("ptzPatrolInterval", 0);
	if (!m_bPaused) {
		if (dv_predict && m_channels.size() > 0 && timeopt::CalcTimePassSecond(m_lastPTZPollTime) > ptzPatrolInterval) {
			timeopt::now(&m_lastPTZPollTime);

			//解析通道ptz配置并移动相机
			ioChannel* pC = m_channels[m_curPZTChan];
			string chanId = pC->getAddr();

			//下一个要巡检的通道
			m_curPZTChan++;
			if (m_curPZTChan >= m_channels.size()) {
				m_curPZTChan = 0;
			}

			bool valid = false;
			if (str::isDigits(chanId)) {
				int presetIdx = atoi(chanId.c_str());
				ptz_gotoPreset(presetIdx);

				valid = true;
			}

			//bool valid = false;
			//if (chanId.find(",") != string::npos) {
			//	vector<string> v;
			//	str::split(v, chanId, ",");
			//
			//	if (v.size() == 3) {
			//		float pan = atof(v[0].c_str());
			//		float tilt = atof(v[1].c_str());
			//		float zoom = atof(v[2].c_str());
			//		ptz_gotoAbsolute(pan, tilt, zoom);
			//
			//		valid = true;
			//	}
			//}
			//else {
			//	if (str::isDigits(chanId)) {
			//		int presetIdx = atoi(chanId.c_str());
			//		ptz_gotoPreset(presetIdx);
			//
			//		valid = true;
			//	}
			//}

			if (valid) {
				//等待摄像机移动到位
				int waitMoveTime = 5000;
				timeopt::sleepMilli(waitMoveTime);

				//拍照
				onvif_getSnapshotUri();

				TIME time = timeopt::now();

				//deepVision识别
				string imagePath = fs::appPath() + "/onnx/snapshot.jpg";
				if (fs::fileExist(imagePath)) {
					string imageBuff = "";
					DB_FS::readFile(imagePath, imageBuff);

					int inLen = imageBuff.size();
					char* out = new char[inLen * 2 + 1];
					int outLen = tdb_base64_encode((const unsigned char*)imageBuff.c_str(), inLen, out);
					string imgBase64(out, outLen);
					delete[] out;

					string modelPath = fs::appPath() + "/onnx/railway_n_2402213_1920.onnx";

					size_t c_info_len = 0, c_err_len;
					shared_ptr<char> c_info(new char[100000], [](char* p) { delete[] p; });
					shared_ptr<char> c_err(new char[10000], [](char* p) { delete[] p; });
					bool bRC = dv_predict(FUNC_TYPE::RailDamange.c_str(), modelPath.c_str(), imageBuff.data(), imageBuff.size(), c_info.get(), c_info_len, c_err.get(), c_err_len);

					string info;
					if (bRC) {
						info = c_info.get();
					}

					//报警
					if (!info.empty()) {
						bool alarm = false;

						auto doc = yyjson_read(info.c_str(), info.size(), 0);
						auto root = yyjson_doc_get_root(doc);

						auto objectsObj = yyjson_obj_get(root, "objects");
						yyjson_val* val;
						size_t indx = 0, max = 0;
						yyjson_arr_foreach(objectsObj, indx, max, val) {
							auto idxVal = yyjson_obj_get(val, "idx");
							if (idxVal && yyjson_is_int(idxVal)) {
								int idx = (int)yyjson_get_int(idxVal);
								if (idx == 1) {
									alarm = true;
									break;
								}
							}
						}

						if (alarm) {
							ALARM_INFO ai;
							ai.tag = m_strTagBind + "." + pC->m_strTagBind;
							ai.type = "视频伤损";
							ai.level = ALARM_LEVEL::alarm;
							almSrv.Add(ai);
						}

						yyjson_doc_free(doc);
					}

					//db
					string tag = m_strTagBind + "." + pC->m_strTagBind;

					DB_TIME dbt;
					dbt.fromStr(time.toStr());

					string strIndex = "";
					db.saveImage(tag, dbt, (char*)imageBuff.c_str(), imageBuff.size(), info, strIndex);

					//发中心端
					auto mutDoc = yyjson_mut_doc_new(nullptr);
					auto mutRoot = yyjson_mut_obj(mutDoc);

					yyjson_mut_doc_set_root(mutDoc, mutRoot);

					yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "jsonrpc", "2.0");
					yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "method", "db.saveImage");
					yyjson_mut_obj_add_int(mutDoc, mutRoot, "id", 1);

					auto paramsObj = yyjson_mut_obj(mutDoc);
					yyjson_mut_obj_add_val(mutDoc, mutRoot, "params", paramsObj);

					yyjson_mut_obj_add_strcpy(mutDoc, paramsObj, "tag", tag.c_str());
					yyjson_mut_obj_add_strcpy(mutDoc, paramsObj, "time", time.toStr().c_str());
					yyjson_mut_obj_add_strcpy(mutDoc, paramsObj, "data", imgBase64.c_str());

					if (!info.empty()) {
						yyjson_mut_obj_add_strcpy(mutDoc, paramsObj, "info", info.c_str());
					}

					char* writeResult = yyjson_mut_write(mutDoc, 0, 0);
					if (writeResult) {
						string s = writeResult;
						s += "\n\n";
						sockSrv.sendToAllSessions(s);
						free(writeResult);
					}

					yyjson_mut_doc_free(mutDoc);
				}
			}
		}
	}
}

bool ioDev_onvif::onRecvPkt(unsigned char* pData, size_t iLen) {
	setOnline();
	return false;
}

bool ioDev_onvif::getCurrentVal() {
	return false;
}

void ioDev_onvif::setVal(string& str, string name, string val) {
	str = str::replace(str, "{{" + name + "}}", val);
}

string ioDev_onvif::generateNouce() {
	return "45B6D40AA685B9CC6A367BBF9C33D0BC";
}

string ioDev_onvif::generateAuthStr(string user , string pwd,string method,string uri, string realm, string nonce, string cnonce) {
	//nc：“现时”计数器，这是一个16进制的数值，即客户端发送出请求的数量（包括当前这个请求），这些请求都使用了当前请求中这个“现时”值。例如，对一个给定的“现时”值，
	//在响应的第一个请求中，客户端将发送“nc=00000001”。这个指示值的目的，是让服务器保持这个计数器的一个副本，以便检测重复的请求。如果这个相同的值看到了两次，则这个请求是重复的。
	string nc = "00000001";
	string qop = "auth";


	//计算摘要
	//对用户名、认证域(realm)以及密码的合并值计算 MD5 哈希值，结果称为 HA1。
	//对HTTP方法以及URI的摘要的合并值计算 MD5 哈希值，例如，"GET" 和 "/dir/index.html"，结果称为 HA2。
	//对 HA1、服务器密码随机数(nonce)、请求计数(nc)、客户端密码随机数(cnonce)、保护质量(qop)以及 HA2 的合并值计算 MD5 哈希值。结果即为客户端提供的 response 值。
	string a1 = user + ":" + realm + ":" + pwd;
	string ha1 = getMD5(a1);
	string a2 = method + ":" + uri;
	string ha2 = getMD5(a2);
	string responseBefore = ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2;
	string response = getMD5(responseBefore);

	//=两边不要有空格
	string authStr = "Digest";
	authStr += " ";
	authStr += "username=\"admin\"";
	authStr += ",";
	authStr += "realm=\"" + realm + "\"";
	authStr += ",";
	authStr += "response=\"" + response + "\"";
	authStr += ",";
	authStr += "algorithm=MD5";
	authStr += ",";
	authStr += "uri=\"" + uri + "\"";
	authStr += ",";
	authStr += "nonce=\"" + nonce + "\"";
	authStr += ",";
	authStr += "nc=" + nc;
	authStr += ",";
	authStr += "cnonce=\"" + cnonce + "\"";
	authStr += ",";
	authStr += "qop=auth";

	return authStr;
}

map<string, string> ioDev_onvif::parseKeyValStr(string s) {
	map<string, string> mapKV;
	vector<string> pairs;
	str::split(pairs, s, ",");
	for (int i = 0; i < pairs.size(); i++) {
		string pair = pairs[i];
		vector<string> keyVal;
		str::split(keyVal, pair, "=");
		mapKV[str::trim(keyVal[0])] = str::trim(str::trim(keyVal[1]), "\"");
	}
	return mapKV;
}

bool ioDev_onvif::isAddrValid(){
	string ip;
	if (m_jDevAddr.contains("ip")) {
		return true;
	}
	else {
		return false;
	}
	return false;
}

void ioDev_onvif::onvif_getDevInfo() {
	string body = R"(
		<?xml version="1.0" encoding="utf-8"?>
		<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
		    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:xsd="http://www.w3.org/2001/XMLSchema">
		        <GetDeviceInformation xmlns="http://www.onvif.org/ver10/device/wsdl"/>
		    </s:Body>
		</s:Envelope>
	)";


	doOnvifTransaction(body, "/onvif/device_service", "onvif_getDevInfo", false);
}

void ioDev_onvif::onvif_getSnapshotUri() {
	//string body = R"(
	//	<?xml version="1.0" encoding="UTF-8"?>
	//	<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
	//	    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
	//	        <GetSnapshotUri xmlns="http://www.onvif.org/ver10/media/wsdl">
	//	            <ProfileToken>Profile_1</ProfileToken>
	//	        </GetSnapshotUri>
	//	    </s:Body>
	//	</s:Envelope>
	//)";

	//doOnvifTransaction(body, "/onvif/media_service", "onvif_getSnapshotUri", false);
	doOnvifTransaction_getSnapShot();
}

void ioDev_onvif::onvif_getProfiles() {
	string body = R"(
		<?xml version="1.0" encoding="UTF-8"?>
		<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
		    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
		        <GetProfiles xmlns="http://www.onvif.org/ver10/media/wsdl">
		        </GetProfiles>
		    </s:Body>
		</s:Envelope>
	)";

	doOnvifTransaction(body, "/onvif/media_service", "onvif_getProfiles", false);
}

void ioDev_onvif::onvif_getPresets() {
	string body = R"(
		<?xml version="1.0" encoding="UTF-8"?>
		<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
		    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
		        <GetPresets xmlns="http://www.onvif.org/ver10/PTZ/wsdl">
					<ProfileToken>Profile_1</ProfileToken>
		        </GetPresets>
		    </s:Body>
		</s:Envelope>
	)";

	doOnvifTransaction(body, "/onvif/PTZ", "onvif_getPresets", false);
}

void ioDev_onvif::ptz_startMove(string dir, float panSpeed, float tiltSpeed) {
	if (panSpeed > 1)
		panSpeed = 1;
	if (tiltSpeed > 1)
		tiltSpeed = 1;

	if (dir == "up" || dir == "down")
		panSpeed = 0;
	else if (dir == "left" || dir == "right")
		tiltSpeed = 0;

	if (dir == "left")
		panSpeed *= -1;
	if (dir == "down")
		tiltSpeed *= -1;

	string body = R"(
		<?xml version="1.0" encoding="utf-8"?>
		<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
		    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:xsd="http://www.w3.org/2001/XMLSchema">
		        <ContinuousMove xmlns="http://www.onvif.org/ver20/ptz/wsdl">
		            <ProfileToken>Profile_1</ProfileToken>
		            <Velocity>
		                <PanTilt x="{{panSpeed}}" y="{{tiltSpeed}}" space="http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace" xmlns="http://www.onvif.org/ver10/schema"/>
		            </Velocity>
		        </ContinuousMove>
		    </s:Body>
		</s:Envelope>
	)";

	setVal(body, "panSpeed", str::fromFloat(panSpeed));
	setVal(body, "tiltSpeed", str::fromFloat(tiltSpeed));

	doOnvifTransaction(body, "/onvif/PTZ", "ptz_startMove");
}

void ioDev_onvif::ptz_stopMove() {
	string body = R"(
		<?xml version="1.0" encoding="utf-8"?>
		<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
		    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:xsd="http://www.w3.org/2001/XMLSchema">
		        <Stop xmlns="http://www.onvif.org/ver20/ptz/wsdl">
		            <ProfileToken>Profile_1</ProfileToken>
		            <PanTilt>true</PanTilt>
		            <Zoom>false</Zoom>
		        </Stop>
		    </s:Body>
		</s:Envelope>
	)";

	doOnvifTransaction(body, "/onvif/PTZ", "ptz_stopMove");
}

void ioDev_onvif::ptz_startZoom(string dir, float zoomSpeed) {
	if (zoomSpeed > 1)
		zoomSpeed = 1;

	if (dir == "out")
		zoomSpeed *= -1;

	string body = R"(
		<?xml version="1.0" encoding="utf-8"?>
		<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
			<s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
				xmlns:xsd="http://www.w3.org/2001/XMLSchema">
				<ContinuousMove xmlns="http://www.onvif.org/ver20/ptz/wsdl">
					<ProfileToken>Profile_1</ProfileToken>
					<Velocity>
						<Zoom x="{{zoomSpeed}}"
							xmlns="http://www.onvif.org/ver10/schema"/>
					</Velocity>
				</ContinuousMove>
			</s:Body>
		</s:Envelope>
	)";


	setVal(body, "zoomSpeed", str::fromFloat(zoomSpeed));

	doOnvifTransaction(body, "/onvif/PTZ", "ptz_startZoom");
}

void ioDev_onvif::ptz_stopZoom() {
	string body = R"(
		<?xml version="1.0" encoding="utf-8"?>
		<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
			<s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
				xmlns:xsd="http://www.w3.org/2001/XMLSchema">
				<Stop xmlns="http://www.onvif.org/ver20/ptz/wsdl">
					<ProfileToken>Profile_1</ProfileToken>
					<PanTilt>false</PanTilt>
					<Zoom>true</Zoom>
				</Stop>
			</s:Body>
		</s:Envelope>
	)";

	doOnvifTransaction(body, "/onvif/PTZ", "ptz_stopZoom");
}

void ioDev_onvif::ptz_gotoPreset(int presetIdx) {
	string body = R"(
		<?xml version="1.0" encoding="UTF-8"?>
		<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
		    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
		        <GotoPreset xmlns="http://www.onvif.org/ver10/PTZ/wsdl">
					<ProfileToken>Profile_1</ProfileToken>
					<PresetToken>%d</PresetToken>
		        </GotoPreset>
		    </s:Body>
		</s:Envelope>
	)";

	body = str::format(body.c_str(), presetIdx, presetIdx);
	doOnvifTransaction(body, "/onvif/PTZ", "onvif_gotoPresets", false);
}

void ioDev_onvif::ptz_gotoPreset(string preset) {
	ioChannel* channel = getChanByTag(preset);
	if (channel) {
		string channelID = channel->getAddr();

		if (str::isDigits(channelID)) {
			int presetIdx = atoi(channelID.c_str());
			ptz_gotoPreset(presetIdx);
		}
	}
}

void ioDev_onvif::ptz_gotoAbsolute(float pan, float tilt, float zoom) {
	string body = R"(
		<?xml version="1.0" encoding="utf-8"?>
		<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
		    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
		        xmlns:xsd="http://www.w3.org/2001/XMLSchema">
		        <AbsoluteMove xmlns="http://www.onvif.org/ver20/ptz/wsdl">
		            <ProfileToken>Profile_1</ProfileToken>
		            <Position>
		                <PanTilt x="%f" y="%f"
		                    space="http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace"
		                    xmlns="http://www.onvif.org/ver10/schema" />
		                <Zoom x="%f" space="http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace"
		                    xmlns="http://www.onvif.org/ver10/schema" />
		            </Position>
		        </AbsoluteMove>
		    </s:Body>
		</s:Envelope>
	)";

	body = str::format(body.c_str(), pan, tilt, zoom);
	doOnvifTransaction(body, "/onvif/PTZ", "ptz_gotoAbsolute", false);
}

void ioDev_onvif::ptz_pausePresetPatrol(int parseTime) {
	m_bPaused = true;
	m_pauseResumeInterval = parseTime;

	timeopt::now(&m_pauseResumeTime);
}

void onvifSnapshot(string ip, int port,string user,string pwd, DIGIST_INFO& di) {
	string sport = str::fromInt(port);
	string path = "/onvif-http/snapshot?Profile_1";
	string url = "http://" + ip + ":" + sport + path;

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		mg_printf(connect,
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"\r\n",
			path.c_str(), ip.c_str());

		TIME tStart = timeopt::now();
		while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
			mg_mgr_poll(&mgr, 100);
		}
	}

	mg_mgr_free(&mgr);


	//获取摘要盘问
	if (data.done) {
		string head = mg_get_header_value(data.head, "WWW-Authenticate");
		head = str::trimPrefix(head, "Digest ");

		map<string, string> mapKV = ioDev_onvif::parseKeyValStr(head);
		string realm = mapKV["realm"];
		string nonce = mapKV["nonce"];

		string authStr = ioDev_onvif::generateAuthStr(user,pwd,"GET",path, realm, nonce, ioDev_onvif::generateNouce());

		mg_mgr_init(&mgr);

		data.reset();
		connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

		if (connect) {
			mg_printf(connect,
				"GET %s HTTP/1.1\r\n"
				"Host: %s\r\n"
				"Authorization: %s\r\n"
				"\r\n",
				path.c_str(), ip.c_str(), authStr.c_str());

			TIME tStart = timeopt::now();
			while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
				mg_mgr_poll(&mgr, 100);
			}
		}

		mg_mgr_free(&mgr);

		di.authorization = authStr;
		di.nouce = nonce;
		di.realm = realm;

		std::ofstream file(fs::appPath() + "/onvif_snapshot.jpg", std::ios::binary);
		if (file) {
			file.write(data.body.c_str(), data.body.size());
		}
	}
}


bool ioDev_onvif::doOnvifTransaction_getSnapShot() {
	if (!isAddrValid()) {
		return false;
	}

	int nPort = m_jDevAddr["port"].get<int>();

	string ip = m_jDevAddr["ip"];
	string port = str::fromInt(nPort);
	string path = "/onvif-http/snapshot?Profile_1";
	string url = "http://" + ip + ":" + port + path;

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		mg_printf(connect,
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"\r\n",
			path.c_str(), ip.c_str());

		TIME tStart = timeopt::now();
		while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
			mg_mgr_poll(&mgr, 100);
		}
	}

	mg_mgr_free(&mgr);


	//获取摘要盘问
	if (data.done) {
		string head = mg_get_header_value(data.head, "WWW-Authenticate");
		head = str::trimPrefix(head, "Digest ");

		map<string, string> mapKV = parseKeyValStr(head);
		string realm = mapKV["realm"];
		string nonce = mapKV["nonce"];

		string authStr = generateAuthStr(m_strUser,m_strPwd,"GET",path, realm, nonce, generateNouce());

		mg_mgr_init(&mgr);

		data.reset();
		connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

		if (connect) {
			mg_printf(connect,
				"GET %s HTTP/1.1\r\n"
				"Host: %s\r\n"
				"Authorization: %s\r\n"
				"\r\n",
				path.c_str(), ip.c_str(), authStr.c_str());

			TIME tStart = timeopt::now();
			while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
				mg_mgr_poll(&mgr, 100);
			}
		}

		mg_mgr_free(&mgr);

		if (data.done) {
			doOnvifTransaction_onvif_getSnapshotUri(data);
		}

		setOnline();
		return true;
	}
	else {
		setOffline();
	}

	return false;
}



bool ioDev_onvif::doOnvifTransaction(string msg, string uri, string method, bool log) {
	if (!isAddrValid()) {
		return false;
	}

	int nPort = m_jDevAddr["port"].get<int>();

	string ip = m_jDevAddr["ip"];
	string port = str::fromInt(nPort);
	string path = uri;
	string url = "http://" + ip + ":" + port + path;
	string body = msg;

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		mg_printf(connect,
			"POST %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"Content-Type: application/json\r\n"
			"Content-Length: %u\r\n"
			"\r\n"
			"%s",
			path.c_str(), ip.c_str(), (unsigned int)body.size(), body.c_str()
		);

		TIME tStart = timeopt::now();
		while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
			mg_mgr_poll(&mgr, 100);
		}
	}

	mg_mgr_free(&mgr);

	if (log) {
		LOG("[Onvif]请求,地址:" + url + "\r\nSoap Message:" + msg);
	}

	//获取摘要盘问
	if (data.done) {
		string head = mg_get_header_value(data.head, "WWW-Authenticate");
		head = str::trimPrefix(head, "Digest ");

		map<string, string> mapKV = parseKeyValStr(head);
		string realm = mapKV["realm"];
		string nonce = mapKV["nonce"];

		string authStr = generateAuthStr(m_strUser,m_strPwd,"POST",uri, realm, nonce, generateNouce());

		mg_mgr_init(&mgr);

		data.reset();
		connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

		if (connect) {
			mg_printf(connect,
				"POST %s HTTP/1.0\r\n"
				"Host: %s\r\n"
				"Content-Type: application/soap+xml; charset=utf-8\r\n"
				"Authorization: %s\r\n"
				"Content-Length: %u\r\n"
				"Connection: close\r\n"
				"\r\n"
				"%s",
				path.c_str(), ip.c_str(), authStr.c_str(), (unsigned int)body.size(), body.c_str()
			);

			TIME tStart = timeopt::now();
			while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
				mg_mgr_poll(&mgr, 100);
			}
		}

		mg_mgr_free(&mgr);

		if (data.done) {
			if (data.status == 401) {
				LOG("[Onvif]用户名密码验证失败,user=%s,pwd=%s,地址:%s", m_strUser.c_str(), m_strPwd.c_str(), getDevAddrStr().c_str());
			}

			if (log) {
				LOG("[Onvif]响应,地址:%s%s\r\nSoap Message:%s", ip.c_str(), path.c_str(), data.body.c_str());
			}

			if (uri == "/onvif/media_service") {
				size_t urlStart = data.body.find("<tt:Uri>");
				size_t urlEnd = data.body.find("</tt:Uri>");
				//第一次请求图片，获取realm和nouce
				if (urlStart != std::string::npos && urlEnd != std::string::npos) {
					string picUrl = data.body.substr(urlStart + 8, urlEnd - urlStart - 8);
					string snapShotUrl = "/onvif-http/snapshot?Profile_1";
					picUrl = str::format("http://%s:%s/onvif-http/snapshot?Profile_1", ip, port);

					mg_mgr_init(&mgr);

					data.reset();
					connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

					if (connect) {
						mg_printf(connect,
							"GET %s HTTP/1.0\r\n"
							"Host: %s\r\n"
							"Connection: close\r\n"
							"\r\n",
							picUrl.c_str(), ip.c_str()
						);

						TIME tStart = timeopt::now();
						while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
							mg_mgr_poll(&mgr, 100);
						}
					}

					mg_mgr_free(&mgr);
					//第2次请求图片，digist认证
					if (data.done) {
						head = mg_get_header_value(data.head, "WWW-Authenticate");
						head = str::trimPrefix(head, "Digest ");
						mapKV = parseKeyValStr(head);
						realm = mapKV["realm"];
						nonce = mapKV["nonce"];
						authStr = generateAuthStr(m_strUser,m_strPwd,"GET",snapShotUrl, realm, nonce, generateNouce());

						mg_mgr_init(&mgr);

						data.reset();
						connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

						if (connect) {
							mg_printf(connect,
								"GET %s HTTP/1.0\r\n"
								"Host: %s\r\n"
								"Authorization: %s\r\n"
								"Connection: close\r\n"
								"\r\n",
								picUrl.c_str(), ip.c_str(), authStr.c_str()
							);

							TIME tStart = timeopt::now();
							while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
								mg_mgr_poll(&mgr, 100);
							}
						}

						mg_mgr_free(&mgr);

						if (method == "onvif_getSnapshotUri") {
							doOnvifTransaction_onvif_getSnapshotUri(data);
						}
					}
				}
			}
		}

		setOnline();
		return true;
	}
	else {
		setOffline();
	}

	return false;
}

bool ioDev_onvif::doOnvifTransaction_onvif_getSnapshotUri(mg_http_data& data) {
	std::ofstream file(fs::appPath() + "/onnx/snapshot.jpg", std::ios::binary);
	if (!file) {
		return false;
	}

	file.write(data.body.c_str(), data.body.size());
	return true;
}


