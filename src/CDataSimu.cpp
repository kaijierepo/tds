#include "CDataSimu.h"
#include <yyjson.h>
#include <random>
#include "prj.h"
#include "mp.h"

CDataSimu* g_pDataSimu = NULL;

void CDataSimu::startDataSimu() {
	// 创建随机数引擎
	std::random_device rd;
	std::mt19937 gen(rd());

	map<string, MP*> mapAllMP;
	prj.getMpList(mapAllMP);

	for (map<string, MP*>::iterator it = mapAllMP.begin(); it != mapAllMP.end(); it++) {
		MP* pmp = (MP*)it->second;

		if (pmp && pmp->m_simuConf.enable) {
			if (timeopt::CalcTimePassSecond(pmp->m_simuDataTime) > pmp->m_simuConf.interval) {
				timeopt::now(&pmp->m_simuDataTime);

				double value = pmp->m_simuConf.lowLimit;
				if (pmp->m_simuConf.lowLimit != pmp->m_simuConf.highLimit) {
					// 创建均匀分布
					std::uniform_real_distribution<> dis(min(pmp->m_simuConf.lowLimit, pmp->m_simuConf.highLimit), max(pmp->m_simuConf.lowLimit, pmp->m_simuConf.highLimit));

					// 生成随机数并调整小数位
					value = dis(gen);
				}

				json val = value;
				pmp->input(val);
			}
		}
	}
}
