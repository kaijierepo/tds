#pragma once
#include "ioDev.h"


class ioDev_camera : public ioDev {
public:
	virtual void ptz_startMove(string dir, float panSpeed = 0, float tiltSpeed = 0) {};
	virtual void ptz_stopMove() {};
	virtual void ptz_startZoom(string dir, float zoomSpeed = 0) {};
	virtual void ptz_stopZoom() {};
	virtual void ptz_startFocus(string dir, float focusSpeed = 0) {};
	virtual void ptz_stopFocus() {};
	virtual void ptz_gotoPreset(int presetIdx) {};
	virtual void ptz_addPreset() {};
	virtual void ptz_deletePreset(int presetIdx) {};
	bool isCamera() override { return true; };
};
