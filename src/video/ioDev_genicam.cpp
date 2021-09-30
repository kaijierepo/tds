#include "pch.h"
#include "ioDev_genicam.h"
#ifdef ENABLE_GENICAM
#include "prj.h"
#define PFNC_INCLUDE_HELPERS
#include "GenTL/PFNC.h"
#include "mp.h"
#include "logger.h"
#include "streamServer.h"

ioDev_genicam* firstDiscoverGenicam = NULL;


void thread_doStream(ioDev_genicam* p)
{
    while (1)
    {
        if (p->m_bStopStream)
        {
            break;
        }
        if (!p->m_bConnected) //用户没有停止码流，但是设备连接断开了。等待重连后继续获取码流
        {
            Sleep(50);
            continue;
        }
        try
        {
            p->doStreaming();
            if(!p->m_bStopStream)//异常退出码流
                p->disconnect();
        }
        catch (std::exception& e)
        {
            p->disconnect();
            Sleep(1000);
        }
    }
}


json ioDev_genicam::listDevices()
{
    json info = json::array();

    try
    {
        // list all systems, interfaces and devices

        std::vector<std::shared_ptr<rcg::System> > system = rcg::System::getSystems();

        for (size_t i = 0; i < system.size(); i++)
        {
            system[i]->open();

            /*
            info["transportLayer"] = system[i]->getID();
            info["vendor"] = system[i]->getVendor();
            info["model"] = system[i]->getModel();
            info["vendorVersion"] = system[i]->getVersion();
            info["TLType"] = system[i]->getTLType();
            info["name"] = system[i]->getName();
            info["pathname"] = system[i]->getPathname();
            info["displayName"] = system[i]->getDisplayName();
            info["genTLVersion"] = system[i]->getMajorVersion() + "." + system[i]->getMinorVersion();
            */

            std::vector<std::shared_ptr<rcg::Interface> > interf = system[i]->getInterfaces();

            for (size_t k = 0; k < interf.size(); k++)
            {
                interf[k]->open();

                json jInterface;
                jInterface["interface"] = interf[k]->getID();
                jInterface["displayName"] = interf[k]->getDisplayName();
                jInterface["TLType"] = interf[k]->getTLType();

                json jDevices = json::array();
                std::vector<std::shared_ptr<rcg::Device> > device = interf[k]->getDevices();

                for (size_t j = 0; j < device.size(); j++)
                {
                    json jDev;
                    jDev["device"] = device[j]->getID();
                    jDev["vendor"] = device[j]->getVendor();
                    jDev["model"] = device[j]->getModel();
                    jDev["TLType"] = device[j]->getTLType();
                    jDev["displayName"] = device[j]->getDisplayName();
                    jDev["userDefinedName"] = device[j]->getUserDefinedName();
                    jDev["accessStatus"] = device[j]->getAccessStatus();
                    jDev["serialNumber"] = device[j]->getSerialNumber();
                    jDev["version"] = device[j]->getVersion();
                    jDev["TSFrequency"] = device[j]->getTimestampFrequency();
                    jDevices.push_back(jDev);
                }
                jInterface["children"] = jDevices;
                interf[k]->close();
                info.push_back(jInterface);
            }
            system[i]->close();
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
    }

	return info;
}

void ioDev_genicam::setGenicamDev(std::shared_ptr<rcg::Device> genDev)
{
    m_genicamDev = genDev;
}

void ioDev_genicam::doStreaming()
{
    m_bStreaming = true;
    unique_lock<mutex> lock(m_csStreamThread);
    m_bStopStream = false;
    std::shared_ptr<rcg::Device> dev = m_genicamDev;
    if (dev)
    {
        std::vector<std::shared_ptr<rcg::Stream> > stream = dev->getStreams();
        if (stream.size() > 0)
        {
            // opening first stream
            stream[0]->open();
            stream[0]->attachBuffers(true);
            stream[0]->startStreaming();

            std::cout << "Package size: " << rcg::getString(m_nodemap, "GevSCPSPacketSize") << std::endl;

            int buffers_received = 0;
            int buffers_incomplete = 0;
            auto time_start = std::chrono::steady_clock::now();
            double latency_ns = 0;

            int errorCount = 0;
            bool firstFrameRecv = false;
            STREAM_INFO si;
            while(errorCount<2)
            {
                if (m_bStopStream)
                    break;
                const rcg::Buffer* buffer = stream[0]->grab(2000);
                if (buffer != 0)
                {
                    buffers_received++;

                    if (!buffer->getIsIncomplete())
                    {
                        uint32_t npart = buffer->getNumberOfParts();
                        for (uint32_t part = 0; part < npart; part++)
                        {
                            rcg::Image image(buffer, part);
                            size_t w = image.getWidth();
                            size_t h = image.getHeight();

                            if (w == 0 || h == 0) {errorCount++; continue;}

                            PfncFormat iPixelFmt = (PfncFormat)image.getPixelFormat();

                            //mono8ToBmp(p, width, height, "test.bmp");

                            if (!firstFrameRecv)
                            {
                                si.h = h;
                                si.w = w;
                                si.pixelFmt = GetPixelFormatName(iPixelFmt);
                                si.pixelSize = PFNC_PIXEL_SIZE(iPixelFmt);
                                firstFrameRecv = true;
                                string log = str::format("开始接收码流,ioAddr:%s,width:%d,height:%d,像素格式:%s,像素大小:%d",
                                  m_genicamDev->getID(), si.w, si.h, si.pixelFmt, si.pixelSize);
                                LOG(log);
                            }

                            
                            STREAM_DATA sd;
                            sd.info = si;
                            sd.pData = (char*)buffer->getBase(part);
                            sd.len = buffer->getSize(part);
                            pushStream(sd);
                            sd.pData = NULL;
                        }
                    }
                    else
                    {
                        std::cerr << "Incomplete buffer received" << std::endl;
                        buffers_incomplete++;
                    }
                }
                else
                {
                    std::cerr << "Cannot grab images" << std::endl;
                    errorCount++;
                    continue;
                }
            }

            stream[0]->stopStreaming();
            stream[0]->close();
        }
    }
    m_bStreaming = false;
}

void ioDev_genicam::addStreamId(string streamId)
{
    m_streamId.push_back(streamId);
    streamSrvNode* pssn = streamSrv.getSrvNode(streamId);
    pssn->setPusher(this);
}

void ioDev_genicam::setParam(string name,json val,bool isEnum)
{
    bool bRet = false;
    if (val.is_number_integer())
    {
        bRet = rcg::setInteger(m_nodemap, name.c_str(), val.get<int>());
    }
    else if (val.is_number_float())
    {
        bRet = rcg::setFloat(m_nodemap, name.c_str(), val.get<float>());
    }
    else if (val.is_string() && isEnum)
    {
        bRet = rcg::setEnum(m_nodemap, name.c_str(), val.get<string>().c_str());
    }
   
    string sRlt = bRet ? "成功" : "失败";
    LOG("设置GenICam参数 " + name + " = " + val.dump() + " " + sRlt);
}

void ioDev_genicam::doCmd(string name)
{
    rcg::callCommand(m_nodemap, name.c_str());
}

bool ioDev_genicam::startStream(STREAM_INFO* si)
{
    if (m_bStreaming)
        return true;
    if(si)
        m_streamInfoConf = *si;
    thread t(thread_doStream, this);
    t.detach();
    return true;
}

bool ioDev_genicam::stopStream()
{
    m_bStopStream = true;
    unique_lock<mutex> lock(m_csStreamThread);
    return true;
}

void ioDev_genicam::mono8ToBmp(char* pData, int w, int h, string fileName)
{
    int iBmpLen = w * h * 3 + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    UCHAR* pBmp = new UCHAR[iBmpLen];
    UCHAR* pBmpData = pBmp + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPFILEHEADER* pFh = (BITMAPFILEHEADER*)pBmp;
    BITMAPINFOHEADER* pIh = (BITMAPINFOHEADER*)(pBmp + sizeof(BITMAPFILEHEADER));

    ZeroMemory(pFh, sizeof(*pFh));
    pFh->bfType = 0x4d42;
    pFh->bfSize = w * h * 3 + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    pFh->bfOffBits = sizeof(*pFh) + sizeof(*pIh);

    ZeroMemory(pIh, sizeof(*pIh));
    pIh->biSize = sizeof(*pIh);
    pIh->biWidth = w;
    pIh->biHeight = h;
    pIh->biBitCount = 24;
    pIh->biPlanes = 1;
    pIh->biCompression = BI_RGB;//DWORD;

    for (int i = 0; i < w * h; i++)
    {
        pBmpData[i * 3] = pData[i];
        pBmpData[i * 3 + 1] = pData[i];
        pBmpData[i * 3 + 2] = pData[i];
    }

    string path = fs::appPath() + "\\" + fileName + ".bmp";
    fs::writeFile(path.c_str(), (char*)pBmp, iBmpLen);
    delete pBmp;
}



ioDev_genicam::ioDev_genicam()
{
    m_bStopStream = false;
    m_bStreaming = false;
}

bool ioDev_genicam::disconnect()
{
    try {
        m_genicamDev->close();
        m_nodemap = NULL;
        m_bConnected = false;
        string log = "[IO服务] 设备连接断开," + getDesc();
        LOG(log);
    }
    catch (std::exception& e)
    {
        return false;
    }
    return true;
}

bool ioDev_genicam::connect()
{
    try {
        m_genicamDev->open(rcg::Device::CONTROL);
        m_nodemap = m_genicamDev->getRemoteNodeMap();
        m_bConnected = true;
        string log = "[IO服务] 连接设备成功," + getDesc();
        LOG(log);
    }
    catch (std::exception& e)
    {
        return false;
    }
    return true;
}

string ioDev_genicam::getDesc()
{
    string s = "类型:genicam,型号:" + m_genicamDev->getModel() + ",厂家:" + m_genicamDev->getVendor() + ",IO地址:" + m_genicamDev->getID();
    return s;
}

void thread_genicamKeepConnected(ioDev_genicam* p) {
    //while (1)
    //{
    //   Sleep(2000);
    //   
    //   if (p->m_bConnected == false)
    //   {
    //       p->connect();
    //   }
    //}
}

bool ioDev_genicam::run()
{
    connect();
    thread t(thread_genicamKeepConnected, this);
    t.detach();
    return true;
}


void ioDev_genicam::captureImageToBmp(string devId, string fileName)
{
    std::shared_ptr<rcg::Device> dev = rcg::getDevice("devicemodulc4_2f_90_f2_37_81");
    if (dev)
    {
        dev->open(rcg::Device::CONTROL);
        std::shared_ptr<GenApi::CNodeMapRef> nodemap = dev->getRemoteNodeMap();

     
        std::vector<std::shared_ptr<rcg::Stream> > stream = dev->getStreams();

        if (stream.size() > 0)
        {
            // opening first stream
            stream[0]->open();
            stream[0]->attachBuffers(true);
            stream[0]->startStreaming();

            std::cout << "Package size: " << rcg::getString(nodemap, "GevSCPSPacketSize") << std::endl;

            int buffers_received = 0;
            int buffers_incomplete = 0;
            auto time_start = std::chrono::steady_clock::now();
            double latency_ns = 0;

            for (int k = 0; k < 1; k++)
            {
                // grab next image with timeout of 3 seconds
                int retry = 5;
                while (retry > 0)
                {
                    const rcg::Buffer* buffer = stream[0]->grab(3000);

                    if (buffer != 0)
                    {
                        buffers_received++;

                        if (!buffer->getIsIncomplete())
                        {
                            uint32_t npart = buffer->getNumberOfParts();
                            for (uint32_t part = 0; part < npart; part++)
                            {
                                rcg::Image image(buffer, part);
                                size_t width = image.getWidth();
                                size_t height = image.getHeight();
                                 char* p = (char*)(image.getPixels());
                                mono8ToBmp(p, width, height, "test.bmp");         
                            }
                        }
                        else
                        {
                            std::cerr << "Incomplete buffer received" << std::endl;
                            buffers_incomplete++;
                        }
                    }
                    else
                    {
                        std::cerr << "Cannot grab images" << std::endl;
                        break;
                    }

                    retry--;
                }
            }

            stream[0]->stopStreaming();
            stream[0]->close();

            // report received and incomplete buffers

            std::cout << std::endl;
            std::cout << "Received buffers:   " << buffers_received << std::endl;
            std::cout << "Incomplete buffers: " << buffers_incomplete << std::endl;

        }

        dev->close();
    }
    else
    {
       
    }
}
#endif