#pragma once

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
	void recursiveExeTask(OBJ* pObj);
};

extern taskServer taskSrv;