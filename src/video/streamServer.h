#pragma once
#include "streamSrvNode.h"


class streamServer {
public:
	//key是streamid
	//可以是ioAddr也可以是tag
	void pushStream(string streamId, STREAM_DATA& sd);
	void asynPushStream(string streamId, STREAM_DATA& sd);
	streamSrvNode* getSrvNode(string streamId);
	map<string, streamSrvNode*> m_mapSrvNodes; 
	bool startStream(string streamId,STREAM_INFO* si=NULL);
};

extern streamServer streamSrv;