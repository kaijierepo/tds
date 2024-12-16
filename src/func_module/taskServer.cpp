#include "pch.h"
#include "common.h"
#include "prj.h"
#include "rpcHandler.h"
#include "taskServer.h"
#include "scriptEngine.h"
#include "logger.h"

taskServer taskSrv;

taskServer::taskServer(void)
{
	
}


taskServer::~taskServer(void)
{
}

void taskServer::recursiveExeTask(OBJ* pObj) {
	TIME now = timeopt::now();
	for (int i = 0; i < pObj->m_scheduleTasks.size();i++) {
		SCHEDULE_TASK* pTask = &pObj->m_scheduleTasks[i];
		if (pTask->executionMode == "weeklyRepeat") {
			TIME exeTime = pTask->getExeTime();
			if (pTask->week[exeTime.wDayOfWeek]) {
				time_t pass = timeopt::CalcTimePassSecond(exeTime);
				if (pass >= 0 && pass < 5) {
					LOG("[warn]执行计划任务,%s", pTask->toDescStr().c_str());
					scriptManager.runScriptFileAsyn(pTask->script, pObj->getTag());
				}
			}
		}
		else if(pTask->executionMode == "customTimeRepeat") {
      if (pTask->isInterval()) {
#ifdef DEBUG
				LOG("[warn]执行计划任务,%s", pTask->toDescStr().c_str());
#endif // DEBUG
        scriptManager.runScriptFileAsyn(pTask->script, pObj->getTag());
				pTask->updateExecuteTime();
      }
		}
	}

	for (int i = 0; i < pObj->m_childObj.size(); i++) {
		OBJ* childObj = pObj->m_childObj[i];
		recursiveExeTask(childObj);
	}
}

void taskSrv_workThread(taskServer* p) {
	while (1) {
		timeopt::sleepMilli(5000);

		prj.m_csPrj.lock_shared();
		p->recursiveExeTask(&prj);
		prj.m_csPrj.unlock_shared();
	}
}

void taskServer::run()
{
	bool enable = tds->conf->getInt("enableTaskServer", 1) == 1;
	if (!enable)
		return;

	thread t(taskSrv_workThread,this);
	t.detach();
}



