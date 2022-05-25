#include "pch.h"
#include "ioDiscoverer.h"
#include <WinUser.h>
//#include <guiddef.h>
//#include <winuser.h>
#include <Dbt.h>
#include <wtypes.h>
//#include <devguid.h>  
#include "logger.h"
#include "ioSrv.h"
#include "rpcHandler.h"

#include "ioDev_genicam.h"


#include "streamServer.h"
/*
by default, Windows OS will only post WM_DEVICECHANGE to
All applications with a top - level window, and
Only upon port and volume change.

top-level window is a window without WM_CHILD attribute
*/

// 消息处理函数的实现
LRESULT CALLBACK WindowProc_hwDetect(
    _In_  HWND hwnd,
    _In_  UINT uMsg,
    _In_  WPARAM wParam,
    _In_  LPARAM lParam
)
{
    if (DBT_DEVICEARRIVAL == wParam || DBT_DEVICEREMOVECOMPLETE == wParam)
    {
        string devEventType;
        if (DBT_DEVICEARRIVAL == wParam)
            devEventType = "online";
        else if (DBT_DEVICEREMOVECOMPLETE == wParam)
            devEventType = "offline";

        PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
        switch (pHdr->dbch_devicetype)
        {
        case DBT_DEVTYP_DEVICEINTERFACE:
        {
            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
            // do something...
            break;
        }
        case DBT_DEVTYP_HANDLE:
        {
            PDEV_BROADCAST_HANDLE pDevHnd = (PDEV_BROADCAST_HANDLE)pHdr;
            // do something...
            break;
        }
        case DBT_DEVTYP_OEM:
        {
            PDEV_BROADCAST_OEM pDevOem = (PDEV_BROADCAST_OEM)pHdr;
            // do something...
            break;
        }
        case DBT_DEVTYP_PORT:
        {
            PDEV_BROADCAST_PORT pDevPort = (PDEV_BROADCAST_PORT)pHdr;
            string name = pDevPort->dbcp_name;
            //插入拔出返回两次事件，一次name为  COM1 一次是 NULL_COM1 ，
            //有人虚拟串口打开一段时间后，删除时只会返回 NULL_COM一次，刚打开则是2次，原理不太清楚。
            //绿联rs485转usb线测试 都返回COM1不会返回NULL_COM,

            //如果要避免有人虚拟串口的两次消息,需如下处理.但会不兼容真实硬件,摒弃该处理.未来需进一步确定是否是有人虚拟串口软件的bug
            /*  if ( (name.find("COM") == 0 && devEventType == "online") ||
                (name.find("NULL_COM") == 0 && devEventType == "offline"))*/

            if (name.find("COM") == 0 ||name.find("NULL_COM") == 0)
            {
                if (name.find("NULL_COM") == 0)
                {
                    name = name.substr(5, name.length() - 5);
                }

                //string log = "port changes,name:" + name + ",event:" + devEventType;
                //LOG(log);
                ioSrv.refreshSerialIODev();

                MODULE_BUS_MSG msg;
                msg.eventName =  devEventType;
                msg.moduleName = "ioDiscoverer";
                json jMsg;
                jMsg["ioAddr"] = name;
                jMsg["devType"] = IO_DEV_TYPE::GW::local_serial;
                msg.content = jMsg.dump();
                tds->publishMsg(msg);
            }
            break;
        }
        case DBT_DEVTYP_VOLUME:
        {
            PDEV_BROADCAST_VOLUME pDevVolume = (PDEV_BROADCAST_VOLUME)pHdr;
            // do something...
            break;
        }
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}


void serialDetectThread()
{
    setThreadName("serial detection thread");
    /*未来如需要检测除串口外的其他设备， 使用
  * https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerdevicenotificationa
  https://www.codeproject.com/Articles/14500/Detecting-Hardware-Insertion-and-or-Removal
  https://www.codeproject.com/Articles/119168/Hardware-Change-Detection
  GUID guidForModemDevices = { 0x2c7089aa, 0x2e0e, 0x11d1,
  {0xb1, 0x14, 0x00, 0xc0, 0x4f, 0xc2, 0xaa, 0xe4} };
  DEV_BROADCAST_DEVICEINTERFACE notificationFilter;
  ZeroMemory(&notificationFilter, sizeof(notificationFilter));
  notificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
  notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
  notificationFilter.dbcc_classguid = GUID_DEVCLASS_PORTS;
  */

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
    hwDetect.lpfnWndProc = WindowProc_hwDetect;
    hwDetect.lpszClassName = _T("hwDetect");
    hwDetect.hInstance = hInstance;
    RegisterClass(&hwDetect);

    //创建窗口
    HWND hwnd = CreateWindow(
        "hwDetect",           //上面注册的类名，要完全一致  
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

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}


bool ioDiscoverer::runSerialDiscover()
{
    thread t(serialDetectThread);
    t.detach();
    return true;
}

void thread_genicamDiscover(ioDiscoverer* p)
{
    p->doGenicamDiscover();
}

bool ioDiscoverer::runGenicamDiscover()
{
    thread t(thread_genicamDiscover, this);
    t.detach();
    return true;
}

bool ioDiscoverer::doGenicamDiscover()
{
#ifdef ENABLE_GENICAM
    while (1)
    {
        //掉线重连
        if (firstDiscoverGenicam)
        {
            if (firstDiscoverGenicam->m_bConnected == false)
            {
                firstDiscoverGenicam->connect();
            }
        }
        else
        {
            std::shared_ptr<rcg::Device> p = NULL;
            try
            {
                //打开Common Transport Interface 一个system对应一个.cti文件
                std::vector<std::shared_ptr<rcg::System> > system = rcg::System::getSystems();
                for (size_t i = 0; i < system.size(); i++)
                {
                    system[i]->open();
                    std::vector<std::shared_ptr<rcg::Interface> > interf = system[i]->getInterfaces();
                    for (size_t k = 0; k < interf.size(); k++)
                    {
                        interf[k]->open();
                        std::vector<std::shared_ptr<rcg::Device> > device = interf[k]->getDevices();
                        for (size_t j = 0; j < device.size(); j++)
                        {
                            p = device[j];

                            ioDev* piod = ioSrv.getIODev(p->getID());
                            if (!piod)
                            {
                                //执行发现，加入到ioSrv
                                ioDev_genicam* pgen = (ioDev_genicam*)ioSrv.onChildDevDiscovered(p->getID(), IO_DEV_TYPE::DEV::genicam);
                                pgen->m_genicamDev = p;

                                //初始化推流id
                                pgen->registerToStreamServer(p->getID());
                                //第一个发现的genicam额外增加推流id到 genicam_0
                                if (firstDiscoverGenicam == NULL)
                                {
                                    firstDiscoverGenicam = pgen;
                                    firstDiscoverGenicam->registerToStreamServer("genicam_0");
                                }

                                //前两步完成后再发送通知，因为收到通知后的设备操作可能需要前两步完成后才能操作
                                MODULE_BUS_MSG msg;
                                msg.eventName = "online";
                                msg.moduleName = "ioDiscoverer";
                                json jMsg;
                                jMsg["ioAddr"] = p->getID();
                                jMsg["devType"] = "genicam";
                                msg.content = jMsg.dump();
                                tds->publishMsg(msg);
                               
       
                            }
                        }
                        interf[k]->close();
                        if (p)break;
                    }
                    system[i]->close();
                    if (p)break;
                }
            }
            catch (const std::exception& ex)
            {
                std::cerr << ex.what() << std::endl;
            }
        }
        Sleep(1000);
    }
#endif
    return true;
}
 

bool ioDiscoverer::run()
{
    runSerialDiscover();
    runGenicamDiscover();

    return true;
}




