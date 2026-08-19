#ifndef TDS_IO_SERVER_IODEV_CAMERA_H
#define TDS_IO_SERVER_IODEV_CAMERA_H

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
	virtual void ptz_gotoPreset(string preset) {};
	virtual void ptz_addPreset(int presetIdx) {};
	virtual void ptz_deletePreset(int presetIdx) {};
	virtual void ptz_gotoAbsolute(float pan, float tilt, float zoom) {};
	virtual void ptz_pausePresetPatrol(int parseTime) {};
	bool isCamera() override { return true; };
	bool isMoving;
	TIME startMoveTime;
};

#endif /* TDS_IO_SERVER_IODEV_CAMERA_H */
