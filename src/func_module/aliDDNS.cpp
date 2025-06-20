#include "pch.h"
#include "aliDDNS.h"
#include "httplib.h"
#include "common.h"
#include "logger.h"
#include "base64.h"
#include "tds.h"
#include "secure.h"


AliDDNS aliDDNS;

/**
 * uri编码字符串.
 *
 * \param in 必须是utf-8编码的！
 * \return uri编码后的字符串。
 */
std::string encodeURIComponent(std::string in)
{
    std::stringstream ssUri;
    for (char c : in)
    {
        //0-9A-Za-z-_.!~*'()
        if ((c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')
            || c == '!'
            || (c >= '\'' && c <= '*')
            || c == '-'
            || c == '.'
            || c == '_'
            || c == '~')
        {
            ssUri << c;
        }
        else
        {
            ssUri << "%";
            if ((c & 0xf0) == 0)
                ssUri << 0;
            ssUri << setiosflags(ios::uppercase) << hex << (c & 0xff);
        }
    }
    return ssUri.str();
}

string percentEncode(string s){
    // JavaScript 的 encodeURIComponent 规则: encodeURIComponent escapes all characters except the following: alphabetic, decimal digits, - _ . ! ~ * ' ( )
    // 阿里云要求的规则: 对于字符 A-Z、a-z、0-9以及字符“-”、“_”、“.”、“~”不编码; 把编码后的字符串中加号（+）替换成%20、星号（*）替换成%2A、%7E替换回波浪号（~）
    // 因此需要手动处理的有: ! * ' ( )
    // 这里可能会有潜在的问题, 因为阿里的示例代码是 Java 写的, Java 的 URLEncoder 规则和 JavaScript 的 encodeURIComponent 规则有些差别
    s = encodeURIComponent(s);
    s = str::replace(s,"!", "%21");
    s = str::replace(s, "!", "%21");
    s = str::replace(s, "'", "%27");
    s = str::replace(s, "(", "%28");
    s = str::replace(s, ")", "%29");
    s = str::replace(s, "+", "%20");
    s = str::replace(s, "*", "%2A");
    s = str::replace(s, ":", "%3A");
    s = str::replace(s, "%7E", "~");

    return s;
};


// 注意这里的 canonicalizedQueryString 中的 K-V 对实际上会经过两次编码, 因为之前在构造 query string 时就编码过一次了
string getStringToSign(string canonicalizedQueryString) {
    return "GET&" + percentEncode("/") + "&" + percentEncode(canonicalizedQueryString);
};

AliDDNS::AliDDNS()
{
    timeopt::setAsTimeOrg(m_lastUpdateTime);
    m_pubIpService.push_back("api.ipify.org");
    m_pubIpService.push_back("whatismyip.akamai.com");
    m_pubIpService.push_back("myip.dnsomatic.com");
    m_bEnable = true;
    PLUGIN_INFO pi;
    string name = "aliDDNS";
    tds->plugins[name] = this;
}

bool AliDDNS::init()
{
     key = tds->conf->getStr("aliKeyID", "");
     secret = tds->conf->getStr("aliKeySecret", "");
     interval = tds->conf->getInt("ddnsInterval", 600);
     m_bEnable = tds->conf->getInt("enableDDNS", 1) == 1;
     string strDdnsDomainNameList = tds->conf->getStr("ddnsDomainName", "");
     str::split(ddnsDomainNameList, strDdnsDomainNameList, ",");
     addr = "http://alidns.aliyuncs.com";

    return true;
}

void ddnsWorkThread(AliDDNS* p) {
    while (1) {
        timeopt::sleepMilli(1000);
        aliDDNS.doCycleTask();
    }
}

bool AliDDNS::run()
{
    if (!m_bEnable)
        return false;

    thread t(ddnsWorkThread, this);
    t.detach();
    return true;
}

bool AliDDNS::doCycleTask()
{
    if (timeopt::CalcTimePassSecond(m_lastUpdateTime) < interval) {
        return true;
    }

    if (key == "" || secret == "" ) {
        return true;
    }

    if (ddnsDomainNameList.size() == 0)
        return true;


    string pubIp = getPubIp();
    if (pubIp == "") {
        LOG("[阿里DDNS ]无法获取到公网IP地址");
        return false;
    }
    LOG("[阿里DDNS ] 获取到公网IP地址：" + pubIp);

    for (int i = 0; i < ddnsDomainNameList.size(); i++) {
        string ddnsDomainName = ddnsDomainNameList[i];
        m_lastUpdateTime = timeopt::now();

        json jRecordId = getSubdomainRecords(ddnsDomainName);
        if (jRecordId == nullptr)
            return false;
        /*解析记录格式
        {
            "Status": "ENABLE",
            "Line": "default",
            "RR": "grace",
            "Locked": false,
            "Type": "A",
            "DomainName": "liangtusoft.com",
            "Value": "101.68.139.24",
            "RecordId": "818347638768457728",
            "TTL": 600,
            "Weight": 1
        }
        */
        if (!jRecordId.contains("RecordId"))return true;
        if (!jRecordId.contains("Value"))return true;
        string recordId = jRecordId["RecordId"];
        string domainValue = jRecordId["Value"];
        LOG("[阿里DDNS ] 获取到解析记录,RecordId:%s,Domain:%s,Value:%s", recordId.c_str(), ddnsDomainName.c_str(), domainValue.c_str());


        if (pubIp == domainValue) {
            LOG("[阿里DDNS ] 解析记录一致，无需更新");
        }
        else {
            size_t pos = ddnsDomainName.find(".");
            if (pos == string::npos) {
                return true;
            }
            string RR = ddnsDomainName.substr(0, pos);

            updateDomainRecord(recordId, RR, "A", pubIp);
        }
    }

    return false;
}

string AliDDNS::getPubIp()
{
    for (int i = 0; i < m_pubIpService.size(); i++) {
        string pubSrvAddr = m_pubIpService[i];
        httplib::Client cli(pubSrvAddr);
        auto res = cli.Get("/");
        if (res != nullptr && res->body != "") {
            string ip = res->body;
            if (str::isIp(ip)) {
                return ip;
            }
        }
    }
    return "";
}

//https://help.aliyun.com/document_detail/146255.html
json AliDDNS::getSubdomainRecords(string subDomain)
{
	httplib::Client cli(addr);
    int nonce = common::randomInt(100000000, 1000000000 - 1);

    //拿到本域名对应的recordID
	std::map<string,string> mapParams = {
      {"AccessKeyId",key},
		{"Action","DescribeSubDomainRecords"},
		{"Format","JSON"},
		{"SignatureMethod","HMAC-SHA1"},
		{"SignatureNonce",str::fromInt(nonce)},
		{"SignatureVersion","1.0"},
		{"SubDomain",subDomain},
		{"Timestamp",timeopt::nowStrIso()},
      {"Version", "2015-01-09"}
	};

    //构造请求字符串.中间的等等于号不进行percentEncode，key和param分别percentEncode
    string queryStr;
    for (auto& i : mapParams) {
        string pair = percentEncode(i.first) + "=" + percentEncode(i.second);
        if (queryStr != "") {
            queryStr += "&";
        }
        queryStr += pair;
    }

    //构造签名字符串
    string strToSign = getStringToSign(queryStr);

    //签名
    string keySecret = secret + "&";
    string signature = create_HMAC_SHA1_Base64(strToSign, keySecret);

    //完整queryStr
    queryStr += "&Signature=" + percentEncode(signature);


    //请求
    string path = "/?" + queryStr;
	auto res = cli.Get(path);

    string recordID;
    try {
        if (res != nullptr) {
            if (res->status == 200) {
                json j = json::parse(res->body);
                json jRecords = j["DomainRecords"]["Record"];
                if (jRecords.size() > 0) {
                    json jRecord = jRecords[0];
                    return jRecord;
                }
            }
            else if(res->body != ""){
                LOG("[error][阿里云DDNS]请求返回错误信息:" + res->body);
            }
        }
    }
    catch (exception& e) {
        EXCEPTION(e);
    }

    return nullptr;
}

bool AliDDNS::updateDomainRecord(string recordID, string RR, string Type, string value)
{
    httplib::Client cli(addr);
    int nonce = common::randomInt(100000000, 1000000000 - 1);

    //拿到本域名对应的recordID
    std::map<string, string> mapParams = {
        {"AccessKeyId",key},
        {"Action","UpdateDomainRecord"},
        {"Format","JSON"},
        {"SignatureMethod","HMAC-SHA1"},
        {"Version", "2015-01-09"},
        {"SignatureNonce",str::fromInt(nonce)},
        {"SignatureVersion","1.0"},
        {"Timestamp",timeopt::nowStrIso()},
        {"RR",RR},
        {"RecordId",recordID},
        {"Type",Type},
        {"Value",value}
    };

    //构造请求字符串.中间的等等于号不进行percentEncode，key和param分别percentEncode
    string queryStr;
    for (auto& i : mapParams) {
        string pair = percentEncode(i.first) + "=" + percentEncode(i.second);
        if (queryStr != "") {
            queryStr += "&";
        }
        queryStr += pair;
    }

    //构造签名字符串
    string strToSign = getStringToSign(queryStr);
    //LOG("strToSign: " + strToSign);

    //签名
    string keySecret = secret + "&";
    string signature = create_HMAC_SHA1_Base64(strToSign, keySecret);

    //完整queryStr
    queryStr += "&Signature=" + percentEncode(signature);


    //请求
    string path = "/?" + queryStr;
    auto res = cli.Get(path);


    try {
        if (res != nullptr) {
            if (res->status == 200) {
                LOG("[阿里云DDNS] DNS解析更新成功");
            }
            else if (res->body != "") {
                LOG("[warn][阿里云DDNS]请求返回错误信息:" + res->body);
            }
        }
    }
    catch (exception& e) {
        EXCEPTION(e);
    }

    return true;
}
