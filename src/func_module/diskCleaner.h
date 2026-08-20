#ifndef TDS_FUNC_MODULE_DISKCLEANER_H
#define TDS_FUNC_MODULE_DISKCLEANER_H

#include <string>
using namespace std;

class DiskCleaner {
public:
	void run();
	
	string dbDir;
	int saveMonthCount;
};

extern DiskCleaner diskCleaner;
#endif /* TDS_FUNC_MODULE_DISKCLEANER_H */
