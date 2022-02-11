#pragma once
#include <Windows.h>
#include "tdsSession.h"

typedef  int(__stdcall* w32mciSendStr)(const char*, char*, int, int);
typedef int(__stdcall* w32mcierror)(int, char*, int);
typedef DWORD(__stdcall* w32mciSendCmd)(MCIDEVICEID mciId, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2);

class Mci
{
public:
	HINSTANCE hins;
	w32mciSendStr mciSendStr;
	w32mciSendCmd mciSendCmd;
	w32mcierror wmcierror;
	Mci();
	~Mci();
	char errorInfo[256];
	MCIDEVICEID m_mciID;
	bool sendStr(std::string command);//error  return false 
	bool sendCmd(MCIDEVICEID mciId, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
	bool sendCmdMsg(MCIDEVICEID mciId, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
};

struct AUDIO_INFO {
	std::string filename;
	string filePath;
	std::string alias;
	int length_ms;
};

class AudioPlayer
{
public:
	Mci mci;


	AudioPlayer();
	~AudioPlayer();
	bool run();
	bool loadPlayList();
	bool load(AUDIO_INFO& ai);
	bool getAudioTimeLen(AUDIO_INFO& ai);
	bool play(AUDIO_INFO ai,int start_ms = 0, int end_ms = -1);
	bool playListItem(int itemIdx, int start_ms = 0, int end_ms = -1);
	bool stop();
	bool pause();
	bool unpause();
	bool test();

	bool rpc_play(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getPlayList(json& params, RPC_RESP& rpcResp, RPC_SESSION session);

	string m_audioPath;
	DWORD m_currentDevId;
	vector<AUDIO_INFO> m_audioList;
	AUDIO_INFO m_currentPlay;
	bool m_paused;
};

extern AudioPlayer audioPlayer;