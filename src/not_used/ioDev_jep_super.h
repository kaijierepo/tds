#ifndef TDS_NOT_USED_IODEV_JEP_SUPER_H
#define TDS_NOT_USED_IODEV_JEP_SUPER_H

#include "ioDev.h"
#include "JHDEqpProtocol.h"

#define SUPER_SONIC_CHNL_NUMS 6

struct S_NormalHisBuffer
{
	//CJsonObject m_joXYList;
	//yyjson_mut_val* m_joXYList;
	yyjson_mut_doc* m_joXYList_doc;
	int nStartTime;
	int nEndTime;
	int nCount = 0;	//	计数器 zzx add
	int nCount2 = 0;//	计数器2 zzx add
	int nCount3 = 0;//	计数器3 zzx add
};

class ioDev_jep_super :
    public ioDev
{
public:
	ioDev_jep_super();
	~ioDev_jep_super();

	bool SendHeartbeatPkt() override;

	bool onRecvData(unsigned char* pData, size_t iLen) override;

	bool handle_CMD_JDSP_SUBPROTOCOL(JHDEqpPkt& pkt);

	BOOL Deal_new_data(yyjson_val* jsonRoot, bool bQuick = false);

	BOOL Deal_failure_inform(yyjson_val* jsonRoot);

	BOOL Deal_self_diagnose(yyjson_val* jsonRoot);

	BOOL Deal_new_data_notify(yyjson_val* jsonRoot);

protected:
	void yyjson_XYJoToList(yyjson_mut_val* joXYList, vector<double>& p);

protected:
	int m_tanTouCentralFrq;//90000 、75000 //可能有的分机新探头 有的老探头  目前要求同一个分机的6对探头不能既有新的又有老的 并且新仪表对应新探头老仪表对应老探头
//最好在加6个通道成员 管理每个通道的探头的信息
	int m_SingleNumsMax;//62  新仪表据说是100 但是63~100测试有问题  统一为62吧
	int m_StepsMax;//100、500  老仪表100 新仪表500

		//上条曲线的max 和 对应的频点
	int m_LastMax[SUPER_SONIC_CHNL_NUMS];// { -1, -1,-1,-1,-1,-1 };
	int m_LastMaxFrq[SUPER_SONIC_CHNL_NUMS];

	string m_strRail = "0"; //0 尖轨区   1心轨区

		//存上条和上上条常规曲线的内容和时间 用于过滤 及 和当前的快速采集对比  2个XB箱 快速采集突变最大的那个区段超门限 触发2次特定采集
	vector<S_NormalHisBuffer> m_vecNormalHisBuf[SUPER_SONIC_CHNL_NUMS];


	//开启采集会设置一个justStarted标记, 把收到的第一次数据的温度存下来
	bool m_justStarted = false;
	float m_fTempWhenStarted = FLT_MIN;
};


#endif /* TDS_NOT_USED_IODEV_JEP_SUPER_H */
