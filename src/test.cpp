#include "3rdparty/rapidonvif/include/onvifclient.hpp"

#ifdef HAVE_ONVIF
#pragma comment(lib, "onvifcpplib.lib")

string strUrl;
string strUser;
string strPw;
OnvifClientDevice* m_pOnvifClient = new OnvifClientDevice(strUrl, strUser, strPw);
void* m_pOnvifPtz = new OnvifClientPTZ(*m_pOnvifClient);
#endif