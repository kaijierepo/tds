#ifndef TDS_NOT_USED_IODEV_LEAKDETECT_H
#define TDS_NOT_USED_IODEV_LEAKDETECT_H

#include "ioDev.h"
#include "tdsSession.h"
#include "../ioProto/proto_leakDetect.hpp"


class ioDev_leakDetect : public ioDev
{
public:
	ioDev_leakDetect();
	~ioDev_leakDetect();
	void DoCycleTask() override;
	bool onRecvPkt(char* pData, int iLen) override; //接收到完整的协议数据包
	bool getCurrentVal();
	bool sendData(char* pData, int iLen);
	bool requestAndWaitResp(LDP_PKT& req, LDP_PKT& resp);

	semaphore m_respSignal;
	LDP_PKT m_currentReq;
	LDP_PKT m_currentResp;
	mutex m_csResp;
	bool getResponse;
	stream2pkt m_fileBuff;
	bool m_bIsReadingFile;
	bool m_bIsWaitingAcq;
};
#endif /* TDS_NOT_USED_IODEV_LEAKDETECT_H */
