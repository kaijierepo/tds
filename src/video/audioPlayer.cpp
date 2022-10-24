#include "pch.h"
#include "video/audioPlayer.h"
#include<iostream>
#include<random>
#include<time.h>
#include "common.h"
#include "logger.h"

HWND hwndPlayer = NULL;

//同一个device的操作一定要在同一个线程当中进行。
LRESULT CALLBACK WindowProc_audioPlayer(
	_In_  HWND hwnd,
	_In_  UINT uMsg,
	_In_  WPARAM wParam,
	_In_  LPARAM lParam
)
{
	if (uMsg == MCI_OPEN)
	{
		audioPlayer.mci.sendCmd(audioPlayer.mci.m_mciID, uMsg, wParam, lParam);
	}
	else if (uMsg == MCI_CLOSE)
	{
		audioPlayer.mci.sendCmd(audioPlayer.mci.m_mciID, uMsg, wParam, lParam);
	}
	else if (uMsg == MCI_PAUSE)
	{
		audioPlayer.mci.sendCmd(audioPlayer.mci.m_mciID, uMsg, wParam, lParam);
	}
	else if (uMsg == MCI_PLAY)
	{
		audioPlayer.mci.sendCmd(audioPlayer.mci.m_mciID, uMsg, wParam, lParam);
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void audioPlayerThread()
{
	setThreadName("audio player thread");
  //注册窗口类
	HINSTANCE hInstance;
	hInstance = GetModuleHandle(NULL);
	WNDCLASS hwDetect;
	hwDetect.cbClsExtra = 0;
	hwDetect.cbWndExtra = 0;
	hwDetect.hCursor = LoadCursor(hInstance, IDC_ARROW);;
	hwDetect.hIcon = LoadIcon(hInstance, IDI_APPLICATION);;
	hwDetect.lpszMenuName = NULL;
	hwDetect.style = CS_HREDRAW | CS_VREDRAW;
	hwDetect.hbrBackground = (HBRUSH)COLOR_WINDOW;
	hwDetect.lpfnWndProc = WindowProc_audioPlayer;
	hwDetect.lpszClassName = _T("audioPlayer");
	hwDetect.hInstance = hInstance;
	RegisterClass(&hwDetect);

	//创建窗口
	HWND hwnd = CreateWindow(
		"audioPlayer",           //上面注册的类名，要完全一致  
		"",                     //窗口标题文字  
		WS_OVERLAPPEDWINDOW, //窗口外观样式  
		0,             //窗口相对于父级的X坐标  
		0,             //窗口相对于父级的Y坐标  
		100,                //窗口的宽度  
		100,                //窗口的高度  
		NULL,               //没有父窗口，为NULL  
		NULL,               //没有菜单，为NULL  
		hInstance,          //当前应用程序的实例句柄  
		NULL);              //没有附加数据，为NULL  

	ShowWindow(hwnd, SW_HIDE);
	hwndPlayer = hwnd;

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}


Mci::Mci()
{
	HINSTANCE hins = LoadLibraryA("winmm.dll");
	mciSendStr = (w32mciSendStr)GetProcAddress(hins, "mciSendStringW");
	mciSendCmd = (w32mciSendCmd)GetProcAddress(hins, "mciSendCommandW");
	wmcierror = (w32mcierror)GetProcAddress(hins, "mciGetErrorStringA");
}
Mci::~Mci()
{
	FreeLibrary(hins);
}

bool Mci::sendStr(std::string command)
{
	int errcode = mciSendStr(command.c_str(), errorInfo, 254, 0);
	if (errcode)
	{
		memset(errorInfo, 0, 254);
		wmcierror(errcode, errorInfo, 254);
		return errcode;
	}
	return errcode;
}

bool Mci::sendCmd(MCIDEVICEID mciId, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	int errcode = mciSendCmd(mciId, uMsg, dwParam1, dwParam2);
	if (errcode)
	{
		memset(errorInfo, 0, 254);
		wmcierror(errcode, errorInfo, 254);
		return false;
	}
	return true;
}

bool Mci::sendCmdMsg(MCIDEVICEID mciId,UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	m_mciID = mciId;
	DWORD ret = SendMessage(hwndPlayer, uMsg, dwParam1, dwParam2);
	if (ret == 0)
		return true;
	return false;
}


AudioPlayer audioPlayer;

AudioPlayer::AudioPlayer()
{
	m_currentDevId = 0;
	m_paused = false;
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
	thread t(audioPlayerThread);
	t.detach();
	return true;
}
bool AudioPlayer::loadPlayList()
{
	m_audioPath = tds->conf->confPath + "audio";
	vector<string> fileList;
	fs::getFileList(fileList, m_audioPath);

	for (int i = 0; i < fileList.size(); i++)
	{
		AUDIO_INFO ai;
		ai.filename = fileList[i];
		ai.filePath = m_audioPath + "/" + ai.filename;
		ai.filePath = str::replace(ai.filePath, "/", "\\");
		getAudioTimeLen(ai);
		m_audioList.push_back(ai);
	}

	if (m_audioList.size() > 0)
	{
		for (int i = 0; i < m_audioList.size(); i++)
		{
			LOG("[音频播放器] 加载音乐 " + m_audioList[i].filePath);
		}
	}
	return true;
}

bool AudioPlayer::getAudioTimeLen(AUDIO_INFO& ai)
{
	//打开设备
	MCI_OPEN_PARMSW mciOpen;
	memset(&mciOpen, 0, sizeof(mciOpen));
	wstring wpath = charCodec::utf8toUtf16(ai.filePath);
	mciOpen.lpstrElementName = wpath.c_str();
	mci.sendCmd(NULL, MCI_OPEN, MCI_OPEN_ELEMENT, (DWORD_PTR)&mciOpen); //发送打开相关设备的命令
	//检测播放总长度
	DWORD wDeviceID = mciOpen.wDeviceID; //得到打开的设备的ID
	MCI_STATUS_PARMS mciStatusParms;
	mciStatusParms.dwItem = MCI_STATUS_LENGTH;
	mci.sendCmd(wDeviceID, MCI_STATUS, MCI_WAIT | MCI_STATUS_ITEM, (DWORD_PTR)&mciStatusParms); //发送状态命令
	ai.length_ms = mciStatusParms.dwReturn;

	mci.sendCmd(wDeviceID, MCI_CLOSE, NULL, NULL);
	return true;
}
bool AudioPlayer::play(AUDIO_INFO ai,int start_ms, int end_ms)
{
	if (end_ms == -1) end_ms = ai.length_ms;


	if (ai.filePath != m_currentPlay.filePath)
	{
		if (!stop())
			return false;
	}

	if (m_currentDevId == 0)
	{
		//打开设备
		MCI_OPEN_PARMSW mciOpen;
		memset(&mciOpen, 0, sizeof(mciOpen));
		wstring wpath = charCodec::utf8toUtf16(ai.filePath);
		mciOpen.lpstrElementName = wpath.c_str();
		if (!mci.sendCmdMsg(NULL, MCI_OPEN, MCI_OPEN_ELEMENT, (DWORD_PTR)&mciOpen)) //发送打开相关设备的命令
		{
			return false;
		}
		//检测播放总长度
		DWORD wDeviceID = mciOpen.wDeviceID; //得到打开的设备的ID
		MCI_STATUS_PARMS mciStatusParms;
		mciStatusParms.dwItem = MCI_STATUS_LENGTH;
		if (!mci.sendCmdMsg(wDeviceID, MCI_STATUS, MCI_WAIT | MCI_STATUS_ITEM, (DWORD_PTR)&mciStatusParms))
		{
			return false;
		}//发送状态命令
		DWORD lLength = mciStatusParms.dwReturn;

		m_currentDevId = wDeviceID;
		m_currentPlay = ai;
	}
		
	
	//播放设备
	MCI_PLAY_PARMS mciPlay;
	if (!mci.sendCmdMsg(m_currentDevId, MCI_PLAY, NULL, (DWORD_PTR)&mciPlay))
	{
		return false;
	}
	m_paused = false;
	return true;
}

bool AudioPlayer::playListItem(int itemIdx, int start_ms, int end_ms)
{
	if (itemIdx > m_audioList.size() - 1)
	{
		return false;
	}
	AUDIO_INFO ai = m_audioList[itemIdx];
	return play(ai, start_ms, end_ms);
}

bool AudioPlayer::stop()
{
	if (m_currentDevId != 0)
	{
		if (!mci.sendCmdMsg(m_currentDevId, MCI_CLOSE, NULL, NULL))
		{
			LOG("[error]停止音频播放失败,devID=" + str::fromInt(m_currentDevId));
			return false;
		}
		else
		{
			m_currentDevId = 0;
		}
	}
	return true;
}
bool AudioPlayer::pause()
{
	if (!mci.sendCmdMsg(m_currentDevId, MCI_PAUSE, NULL,NULL))
	{
		return false;
	}
	m_paused = true;
	return true;
}
bool AudioPlayer::unpause()
{
	
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

AUDIO_INFO* AudioPlayer::getAudioInfo(string name)
{
	AUDIO_INFO* pai = NULL;
	for (int i = 0; i < m_audioList.size(); i++)
	{
		AUDIO_INFO& ai = m_audioList[i];
		if (ai.filename == name)
		{
			pai = &ai;
			break;
		}
	}
	return pai;
}

bool AudioPlayer::rpc_play(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	int start = 0;
	int end = -1;
	if (params.contains("start"))
	{
		start = params["start"].get<float>() * 1000;
	}
	if (params.contains("end"))
	{
		start = params["end"].get<float>() * 1000;
	}
	AUDIO_INFO ai;
	if (params.contains("index"))
	{
		int index = params["index"].get<int>();
		if (index > m_audioList.size() - 1)
		{
			json j = "错误,序号不合法,当前播放列表长度" + str::fromInt(m_audioList.size());
			rpcResp.error = j.dump();
			return true;
		}
		ai = m_audioList[index];
	}
	else if (params.contains("name"))
	{
		string name = params["name"].get<string>();
		AUDIO_INFO* pai = getAudioInfo(name);
		if (pai)
		{
			ai = *pai;
		}
		else
		{
			json j = "错误,未找到指定名称的音频," + name;
			rpcResp.error = j.dump();
			return true;
		}
	}
	else
	{
		if (m_audioList.size() > 0)
		{
			if (m_currentDevId)
			{
				mci.sendCmd(m_currentDevId, MCI_PLAY, NULL, NULL);
				return true;
			}
			else 
			   ai = m_audioList[0];
		}
		else
		{
			json j = "错误,当前播放列表为空,无法播放";
			rpcResp.error = j.dump();
			return true;
		}
	}
	
	if (!play(ai, start, end))
	{
		json j = mci.errorInfo;
		rpcResp.error = j.dump();
	}

	return true;
}

bool AudioPlayer::rpc_getPlayList(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	json list;
	for (int i = 0; i < m_audioList.size(); i++)
	{
		AUDIO_INFO& ai = m_audioList[i];
		json j;
		j["name"] = ai.filename;
		j["length"] = (float)ai.length_ms / 1000.0;
		if (ai.filename == m_currentPlay.filename)
		{
			j["playing"] = true;
			if (m_paused)
			{
				j["paused"] = true;
			}
		}
		list.push_back(j);
	}

	rpcResp.result = list.dump(2);
	return true;
}
