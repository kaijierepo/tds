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
		m_mapSrvNodes[streamId] = pn;
	}

	return m_mapSrvNodes[streamId];
}
