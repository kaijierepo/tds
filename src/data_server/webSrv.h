#pragma once
#include "common/mongoose.h"

class WebServer {
public:
	WebServer();
	~WebServer();
	void run(int port, bool https = false);

	bool enableHttps;
};
