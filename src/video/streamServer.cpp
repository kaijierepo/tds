#include "pch.h"

#ifdef ENABLE_GENICAM
#include "streamServer.h"

streamServer streamSrv;

void streamServer::pushStream(string streamId, STREAM_DATA& sd)
{
	streamSrvNode* ssn = getSrvNode(streamId);
	ssn->pushStream(sd);
}

void streamServer::asynPushStream(string streamId, STREAM_DATA& sd)
{
	streamSrvNode* ssn = getSrvNode(streamId);
	ssn->asynPushStream(sd.pData,sd.len,sd.info);
}

streamSrvNode* streamServer::getSrvNode(string streamId)
{
	if (m_mapSrvNodes.find(streamId) == m_mapSrvNodes.end())
	{
		streamSrvNode* pn = new streamSrvNode();
		pn->m_streamId = streamId;
		m_mapSrvNodes[streamId] = pn;
	}

	return m_mapSrvNodes[streamId];
}

bool streamServer::startStream(string streamId, STREAM_INFO* si)
{
	streamSrvNode* ssn = getSrvNode(streamId);
	if (ssn && ssn->m_streamPusher)
	{
		return ssn->m_streamPusher->startStream(si);
	}
	else
	{
		return false;
	}
}
#endif
