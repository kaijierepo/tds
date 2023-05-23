#define STREAM_TYPE_ENUM string
namespace STREAM_TYPE {
	const string bmp = "bmp";
	const string h264 = "h264";
	const string rgba = "rgba";
	const string mono8 = "mono8";
	const string mono16 = "mono16";
}


struct STREAM_INFO {
	int w;
	int h;
	int pixelSize;
	string pixelFmt;
	float frameRate;
	STREAM_INFO()
	{
		w = 0;
		h = 0;
		pixelSize = 0;
		pixelFmt = "";
		frameRate = 0;
	}
};