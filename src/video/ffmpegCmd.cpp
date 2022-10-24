#include "pch.h"
#include "ffmpegCmd.h"
#include "logger.h"

DWORD runProcess(string exe, string cmdline) {
	STARTUPINFOW si;
	//si.lpTitle = (LPWSTR)title.c_str();
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	// Start the child process.
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	if (!CreateProcessW(
		NULL,   // No module name (use command line)
		(LPWSTR)charCodec::utf8toUtf16(exe + " " + cmdline).c_str(),        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory
		&si,            // Pointer to STARTUPINFO structure
		&pi)           // Pointer to PROCESS_INFORMATION structure
		)
	{
		LOG("runProcess失败:" + exe + " " + cmdline + "  " + sys::getLastError());
		return 0;
	}
	else
	{
		string s = "runProcess成功: " + exe + " " + cmdline;
		LOG(s);
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return pi.dwProcessId;
}

DWORD openRtspSrc(string tag,string rtspSrc)
{
	string tagPinyin;
	str::hanZi2Pinyin(tag, tagPinyin);
	transform(tagPinyin.begin(), tagPinyin.end(), tagPinyin.begin(), ::toupper);
	string cmdline = str::format("-i \"%s\" -vcodec h264 -codec copy -f rtsp -rtsp_transport tcp rtsp://localhost/tds/%s", rtspSrc.c_str(), tagPinyin.c_str());
	return runProcess(fs::appPath() + "/com/ffmpeg/ffmpeg.exe", cmdline);
}
