#include "pch.h"
#include "video/audioPlayer.h"
#include<iostream>
#include<random>
#include<time.h>
#include "common.hpp"
#include "logger.h"


Mci::Mci()
{
	HINSTANCE hins = LoadLibraryA("winmm.dll");
	mciSendStr = (w32mciSendStr)GetProcAddress(hins, "mciSendStringA");
	mciSendCmd = (w32mciSendCmd)GetProcAddress(hins, "mciSendCommandA");
	wmcierror = (w32mcierror)GetProcAddress(hins, "mciGetErrorStringA");
}
Mci::~Mci()
{
	FreeLibrary(hins);
}
bool Mci::sendStr(std::string command)
{
	int errcode = mciSendStr(command.c_str(), buf, 254, 0);
	if (errcode)
	{
		wmcierror(errcode, buf, 254);
		return false;
	}
	return true;
}

bool Mci::sendCmd(MCIDEVICEID mciId, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	return mciSendCmd(mciId, uMsg, dwParam1, dwParam2);
}


AudioPlayer audioPlayer;

AudioPlayer::AudioPlayer()
{
	
}
AudioPlayer::~AudioPlayer()
{
	std::string cmd;
	cmd = "close " + m_currentPlay.alias;
	mci.sendStr(cmd);
}
bool AudioPlayer::run()
{
	loadPlayList();
	return true;
}
bool AudioPlayer::loadPlayList()
{
	m_audioPath = tds->conf->projectConfPath + "audio";
	vector<string> fileList;
	fs::getFileList(fileList, m_audioPath);

	for (int i = 0; i < fileList.size(); i++)
	{
		AUDIO_INFO ai;
		ai.filename = m_audioPath + "/" + fileList[i];
		ai.filename = str::replace(ai.filename, "/", "\\");
		m_audioList.push_back(ai);
	}

	if (m_audioList.size() > 0)
	{
		for (int i = 0; i < m_audioList.size(); i++)
		{
			LOG("[音频播放器] 加载音乐 " + m_audioList[i].filename);
		}
	}
	return true;
}

bool AudioPlayer::load(AUDIO_INFO& ai)
{
	string cmd = "open " + ai.filename;
	if (mci.sendStr(cmd) == false)
		return false;
	cmd = "set " + ai.alias + " time format milliseconds";
	if (mci.sendStr(cmd) == false)
		return false;
	cmd = "status " + ai.alias + " length";
	if (mci.sendStr(cmd) == false)
		return false;
	ai.length_ms = atoi(mci.buf);
	return true;
}
bool AudioPlayer::play(AUDIO_INFO ai,int start_ms, int end_ms)
{
	if (end_ms == -1) end_ms = ai.length_ms;
	std::string cmd;
	char start_str[16], end_str[16];
	_itoa(start_ms, start_str, 10);
	_itoa(end_ms, end_str, 10);
	cmd = "play " + ai.alias + " from ";
	cmd.append(start_str);
	cmd.append(" to ");
	cmd.append(end_str);
	m_currentPlay = ai;
	return mci.sendStr(cmd);
}

bool AudioPlayer::playListItem(int itemIdx, int start_ms, int end_ms)
{
	if (itemIdx > m_audioList.size() - 1)
	{
		return false;
	}
	AUDIO_INFO ai = m_audioList[itemIdx];

	//打开设备
	MCI_OPEN_PARMS mciOpen;
	memset(&mciOpen, 0, sizeof(mciOpen));
	mciOpen.lpstrElementName = ai.filename.c_str();
	mci.sendCmd(NULL, MCI_OPEN, MCI_OPEN_ELEMENT, (DWORD_PTR)&mciOpen); //发送打开相关设备的命令
	//检测播放总长度
	DWORD wDeviceID = mciOpen.wDeviceID; //得到打开的设备的ID
	MCI_STATUS_PARMS mciStatusParms;
	mciStatusParms.dwItem = MCI_STATUS_LENGTH;
	mci.sendCmd(wDeviceID, MCI_STATUS, MCI_WAIT | MCI_STATUS_ITEM, (DWORD_PTR)&mciStatusParms); //发送状态命令
	DWORD lLength = mciStatusParms.dwReturn;
	//播放设备
	MCI_PLAY_PARMS mciPlay;
	mci.sendCmd(wDeviceID, MCI_PLAY, NULL, (DWORD_PTR)&mciPlay);

	return true;
}

bool AudioPlayer::stop(AUDIO_INFO ai)
{
	std::string cmd;
	cmd = "stop " + ai.alias;
	if (mci.sendStr(cmd) == false)
		return false;
	cmd = "seek " + ai.alias + " to start";
	if (mci.sendStr(cmd) == false)
		return false;
	return true;
}
bool AudioPlayer::pause()
{
	std::string cmd;
	cmd = "pause " + m_currentPlay.alias;
	if (mci.sendStr(cmd) == false)
		return false;
	return true;
}
bool AudioPlayer::unpause()
{
	std::string cmd;
	cmd = "resume " + m_currentPlay.alias;
	if (mci.sendStr(cmd) == false)
		return false;
	return true;
}

bool AudioPlayer::test()
{
	//mci.mciSendStr("play C:\\1.mp3", NULL, 0, NULL);
	//mci.mciSendStr("play \"C:\\Users\\admin\\Desktop\\demo-conf\\tds\\4gMusicBox\\audio\\1 1.mp3\"", NULL, 0, NULL);
	mci.mciSendStr("play \"C:\\Users\\admin\\Desktop\\tds\\out\\..\\..\\demo-conf\\tds\\4gMusicBox\\audio\\1.mp3\"", NULL, 0, NULL);
	//mci.mciSendStr("play C:\\Users\\admin\\Desktop\\demo-conf\\tds\\4gMusicBox\\audio\\1.mp3", NULL, 0, NULL);
	return false;
}
