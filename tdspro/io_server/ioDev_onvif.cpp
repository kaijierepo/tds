#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_onvif.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "httplib.h"
#include "secure.h"

using namespace httplib;

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


ioDev_onvif::ioDev_onvif()
{
	m_devType = "onvif";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_strUser = "admin";
	m_strPwd = "Tds-666666";
	m_level = "devcie";
}

ioDev_onvif::~ioDev_onvif()
{
}

bool ioDev_onvif::run()
{
	m_bRunning = true;
	if (m_addrType == DEV_ADDR_MODE::tcpServer)
	{
		string ip;
		if (m_jDevAddr.contains("ip"))
		{
			ip = m_jDevAddr["ip"].get<string>();
		}
		else
		{
			LOG("[error]IODev启动失败,设备地址模式=tcpServer,没有找到ip配置");
			return false;
		}


		int port;
		if (m_jDevAddr.contains("port"))
		{
			port = m_jDevAddr["port"].get<int>();
		}
		else
		{
			LOG("[error]IODev启动失败,设备地址模式=tcpServer,没有找到port配置");
			return false;
		}
	}
	return true;
}


void thread_doHeartbeat(ioDev_onvif* p) {
	p->m_bWorkingThreadRunning = true;
	p->onvif_getDevInfo();
	p->m_bWorkingThreadRunning = false;
}


void ioDev_onvif::DoCycleTask()
{
	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval) {
		if (!m_bWorkingThreadRunning) {
			timeopt::now(&m_stLastAcqTime);
			thread t(thread_doHeartbeat, this);
			t.detach();
		}
	}

	//onvif_getSnapshotUri();
	//onvif_getProfiles();
	//onvif_getPresets();
	//onvif_gotoPresets();
}

bool ioDev_onvif::onRecvPkt(unsigned char* pData, size_t iLen)
{
	setOnline();

	return false;
}


bool ioDev_onvif::getCurrentVal()
{
	return false;
}


string ioDev_onvif::generateAuthStr(string uri,string realm, string nonce,string cnonce) {
	//nc：“现时”计数器，这是一个16进制的数值，即客户端发送出请求的数量（包括当前这个请求），这些请求都使用了当前请求中这个“现时”值。例如，对一个给定的“现时”值，
//在响应的第一个请求中，客户端将发送“nc=00000001”。这个指示值的目的，是让服务器保持这个计数器的一个副本，以便检测重复的请求。如果这个相同的值看到了两次，则这个请求是重复的。
	string nc = "00000001";
	string qop = "auth";


	//计算摘要
	//对用户名、认证域(realm)以及密码的合并值计算 MD5 哈希值，结果称为 HA1。
	//对HTTP方法以及URI的摘要的合并值计算 MD5 哈希值，例如，"GET" 和 "/dir/index.html"，结果称为 HA2。
	//对 HA1、服务器密码随机数(nonce)、请求计数(nc)、客户端密码随机数(cnonce)、保护质量(qop)以及 HA2 的合并值计算 MD5 哈希值。结果即为客户端提供的 response 值。
	string a1 = m_strUser + ":" + realm + ":" + m_strPwd;
	string ha1 = getMD5(a1);
	string a2 = "POST:" + uri;
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
	authStr += "qop=\"auth\"";
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
	authStr += "response=\"" + response + "\"";

	return authStr;
}


string ioDev_onvif::generateNouce()
{
	return "45B6D40AA685B9CC6A367BBF9C33D0BC";
}

void ioDev_onvif::setVal(string& str,string name, string val) {
	str = str::replace(str, "{{" + name + "}}", val);
}

void ioDev_onvif::onvif_getDevInfo()
{
	string body = R"(
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:xsd="http://www.w3.org/2001/XMLSchema">
        <GetDeviceInformation xmlns="http://www.onvif.org/ver10/device/wsdl"/>
    </s:Body>
</s:Envelope>
)";


	doOnvifTransaction(body, "/onvif/device_service",false);
}

void ioDev_onvif::onvif_getSnapshotUri()
{
	string body = R"(<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
        <GetSnapshotUri xmlns="http://www.onvif.org/ver10/media/wsdl">
            <ProfileToken>Profile_1</ProfileToken>
        </GetSnapshotUri>
    </s:Body>
</s:Envelope>)";

	doOnvifTransaction(body, "/onvif/media_service", false);
}

void ioDev_onvif::onvif_getProfiles()
{
	string body = R"(<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
        <GetProfiles xmlns="http://www.onvif.org/ver10/media/wsdl">
        </GetProfiles>
    </s:Body>
</s:Envelope>)";

	doOnvifTransaction(body, "/onvif/media_service", false);
}

void ioDev_onvif::onvif_getPresets()
{
	string body = R"(<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
        <GetPresets xmlns="http://www.onvif.org/ver10/PTZ/wsdl">
			<ProfileToken>Profile_1</ProfileToken>
        </GetPresets>
    </s:Body>
</s:Envelope>)";

	doOnvifTransaction(body, "/onvif/PTZ", false);
}

void ioDev_onvif::onvif_gotoPresets()
{
	string body = R"(<?xml version="1.0" encoding="UTF-8"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
    <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
        <GotoPreset xmlns="http://www.onvif.org/ver10/PTZ/wsdl">
			<ProfileToken>Profile_1</ProfileToken>
			<PresetToken>1</PresetToken>
        </GotoPreset>
    </s:Body>
</s:Envelope>)";

	doOnvifTransaction(body, "/onvif/PTZ", false);	
}


void ioDev_onvif::ptz_startMove(string dir, float panSpeed, float tiltSpeed)
{
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

	doOnvifTransaction(body, "/onvif/PTZ");
}

map<string,string> ioDev_onvif::parseKeyValStr(string s)
{
	map<string, string> mapKV;
	vector<string> pairs;
	str::split(pairs,s, ",");
	for (int i = 0; i < pairs.size(); i++) {
		string pair = pairs[i];
		vector<string> keyVal;
		str::split(keyVal, pair, "=");
		mapKV[str::trim(keyVal[0])] = str::trim(str::trim(keyVal[1]),"\"");
	}
	return mapKV;
}

void ioDev_onvif::ptz_stopMove()
{
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

	doOnvifTransaction(body, "/onvif/PTZ");
}

bool ioDev_onvif::isAddrValid()
{
	string ip;
	if (m_jDevAddr.contains("ip")) {
		return true;
	}
	else
	{
		return false;
	} 
	return false;
}

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

bool ioDev_onvif::doOnvifTransaction(string msg,string uri,bool log)
{
	if (!isAddrValid())
		return false;

	string ip = m_jDevAddr["ip"];
	string addr = "http://" + ip;

	httplib::Client cli(addr);
	httplib::Params params;
	auto res = cli.Post(uri, msg, "application/soap+xml; charset=utf-8");

	if(log)
		LOG("[Onvif]请求,地址:" + addr  + uri + "\r\nSoap Message:" + msg);

	//获取摘要盘问
	if (res != nullptr) {
		string s = res->get_header_value("WWW-Authenticate");
		s = str::trimPrefix(s, "Digest ");
		map<string, string> mapKV = parseKeyValStr(s);
		string realm = mapKV["realm"];
		string nonce = mapKV["nonce"];

		string authStr = generateAuthStr(uri, realm, nonce, generateNouce());

		httplib::Headers header = {
			{"Authorization",authStr}
		};

		res = cli.Post(uri, header, msg, "application/soap+xml; charset=utf-8");

		if (res != nullptr) {

			std::ofstream file("C://Users//kang//Desktop//temp//gotoPreset.txt", std::ios::binary);
			if (!file) {
				return false;
			}

			file.write(res->body.c_str(), res->body.size());


			if (res->status == 401) {
				LOG("[Onvif]用户名密码验证失败,user=%s,pwd=%s,地址:%s", m_strUser.c_str(), m_strPwd.c_str(), getDevAddrStr().c_str());
			}

			if (log) {
				LOG("[Onvif]响应,地址:" + addr + uri + "\r\nSoap Message:" + res->body);
			}

			if (uri == "/onvif/media_service")
			{
				string response = res->body;
				size_t urlStart = response.find("<tt:Uri>");
				size_t urlEnd = response.find("</tt:Uri>");
				if (urlStart != std::string::npos && urlEnd != std::string::npos) {
					string picUrl = response.substr(urlStart + 8, urlEnd - urlStart - 8);

					cli.set_basic_auth(m_strUser.c_str(), m_strPwd.c_str());
					auto res = cli.Get(picUrl.c_str());

					std::ofstream file("C://Users//kang//Desktop//temp//snapshot.jpg", std::ios::binary);
					if (!file) {
						return false;
					}

					file.write(res->body.c_str(), res->body.size());
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

void ioDev_onvif::ptz_startZoom(string dir, float zoomSpeed)
{
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

	doOnvifTransaction(body, "/onvif/PTZ");
}

void ioDev_onvif::ptz_stopZoom()
{
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

	doOnvifTransaction(body, "/onvif/PTZ");
}




