#ifndef TDS_FUNC_MODULE_TASKSERVER_H
#define TDS_FUNC_MODULE_TASKSERVER_H


#include <mutex>
#include "json.hpp"



class taskServer
{
public:
	void init();
	void run();

public:
	taskServer(void);
	~taskServer(void);
	void doOutput(OBJ* pObj, SCHEDULE_TASK* pTask);
	void recursiveExeTask(OBJ* pObj);
};

extern taskServer taskSrv;
#endif /* TDS_FUNC_MODULE_TASKSERVER_H */
