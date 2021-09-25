#pragma once

class ioDiscoverer {
public:
	bool run();

	bool runSerialDiscover();
	bool runGenicamDiscover();//genicam发现只用于发现第一台设备

	bool doGenicamDiscover();
};