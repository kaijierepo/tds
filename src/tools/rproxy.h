#ifndef TDS_TOOLS_RPROXY_H
#define TDS_TOOLS_RPROXY_H

#include "httplib.h"
#include "json.hpp"
using json = nlohmann::json;

class RProxy {
public:
	RProxy();
	void run();

	json m_jConf;
};

extern RProxy* rpProxy;
#endif /* TDS_TOOLS_RPROXY_H */
