#ifndef TDS_FUNC_MODULE_ALIDDNS_H
#define TDS_FUNC_MODULE_ALIDDNS_H

#include <string>
#include <map>
#include "json.hpp"
#include "tds.h"
using json = nlohmann::json;
using namespace std;

class AliDDNS : public i_tdsPlugin {
public:
	AliDDNS();
	bool init() override;
	bool run() override;
	bool doCycleTask();

	string getPubIp();
	json getSubdomainRecords(string subDomain);
	bool updateDomainRecord(string recordID,string RR,string Type,string value);

	TIME m_lastUpdateTime;

	bool m_bEnable;
	vector<string> m_pubIpService;
	string key;
	string secret;
	int interval;
	vector<string> ddnsDomainNameList;
	string addr;
};

extern AliDDNS aliDDNS;

#endif /* TDS_FUNC_MODULE_ALIDDNS_H */
