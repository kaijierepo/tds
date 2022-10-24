#pragma once

class ioDiscoverer {
public:
	bool run();

	bool runSerialDiscover();
	bool runGenicamDiscover();//discover only one genicam device

	bool doGenicamDiscover();
};