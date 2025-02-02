#include "pch.h"
#include "common.h"
#include "prj.h"
#include "rpcHandler.h"
#include "taskServer.h"
#include "scriptEngine.h"
#include "logger.h"
#include "mp.h"
#include "logServer.h"

taskServer taskSrv;

taskServer::taskServer(void)
{
	
}


taskServer::~taskServer(void)
{
}


void thread_task_output(OBJ* pObj, SCHEDULE_TASK* pTask) {
	taskSrv.doOutput(pObj, pTask);
}

void taskServer::doOutput(OBJ* pObj,SCHEDULE_TASK* pTask) {
	string tag = TAG::addRoot(pTask->outputTag, pObj->getTag());
	MP* pmp = prj.GetMPByTag(tag,"zh");

	if (pmp == nullptr) {
		LOG("[warn]计划任务没有找到位号:%s", tag.c_str());
		return;
	}

	string ov = pTask->outputVal;
	if (ov != "") {
		json jVal;
		if (ov == "1") {
			jVal = true;
		}
		else if (ov == "0") {
			jVal = false;
		}
		else if (ov.find(".") == string::npos) {
			int i = atoi(ov.c_str());
			jVal = i;
		}
		else {
			float f = atof(ov.c_str());
			jVal = f;
		}

		json err, rlt;
		pmp->output(jVal,rlt,err);

		json logParams;
		logParams["src"] = "计划任务";
		logParams["object"] = tag;
		logParams["type"] = "控制输出";
		logParams["org"] = "";
		logParams["host"] = "";
		logParams["info"] = "设置为:" + ov;
		logSrv.addLog(logParams);
	}
}

int GetDayOfWeekChs(int wDayOfWeek) {
	if (wDayOfWeek == 0)
		return 6;
	else
		return wDayOfWeek - 1;
}

void taskServer::recursiveExeTask(OBJ* pObj) {
	if (!pObj->m_bEnableTask) {
		return;
	}

	TIME now = timeopt::now();
	for (int i = 0; i < pObj->m_scheduleTasks.size();i++) {
		SCHEDULE_TASK* pTask = &pObj->m_scheduleTasks[i];
		if (pTask->mode == "weeklyRepeat") {
			TIME t; t.setNow();
			HMS nowHMS = t.getHMS();
			int dayOfWeekChs = GetDayOfWeekChs(t.wDayOfWeek);
			if (pTask->week[dayOfWeekChs]) {
				bool hmsReach = nowHMS > pTask->time;
				if (pTask->lastHmsReach == false && hmsReach == true) { //该机制避免同一个任务在定时器作用下重复执行
					LOG("[warn]执行计划任务,%s", pTask->toDescStr().c_str());
					if (pTask->type == "runScript") {
						scriptManager.runScriptFileAsyn(pTask->script, pObj->getTag());
					}
					else if (pTask->type == "output") {
						thread t(thread_task_output,pObj, pTask);
						t.detach();
					}
				}
				pTask->lastHmsReach = hmsReach;
			}
		}
		else if(pTask->mode == "customTimeRepeat") {
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



