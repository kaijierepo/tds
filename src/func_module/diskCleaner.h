#pragma once
#include <string>
using namespace std;

class DiskCleaner
{
public:
	void run();
	
	string dbDir;
	int saveMonthCount;
};