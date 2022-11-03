#pragma once


DWORD openRtspSrc(string tag,string rtspSrc);


class StreamPusher {
public:
	void startPusher(string tag, string streamUrl);
	void stopPusher(string tag);
};