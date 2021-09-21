#include "ioDev_genicam.h"

#define PFNC_INCLUDE_HELPERS
#include "GenTL/PFNC.h"

ioDev_genicam* singleCamera = NULL;

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

    rcg::System::clearSystems();
	return info;
}

std::shared_ptr<rcg::Device> ioDev_genicam::getSingleGenicam()
{
    std::shared_ptr<rcg::Device> p = NULL;
    try
    {
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
                    break;
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

    rcg::System::clearSystems();

    return p;
}

void ioDev_genicam::startStream()
{
    std::shared_ptr<rcg::Device> dev = m_genicamDev;
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

            int errorCount = 0;
            while(errorCount<3)
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
                            size_t w = image.getWidth();
                            size_t h = image.getHeight();

                            if (w == 0 || h == 0) {errorCount++; continue;}

                            PfncFormat iPixelFmt = (PfncFormat)image.getPixelFormat();

                            //mono8ToBmp(p, width, height, "test.bmp");

                            STREAM_INFO si;
                            si.h = h;
                            si.w = w;
                            si.genicamPixelFmt = GetPixelFormatName(iPixelFmt);
                            m_videoSrvNode.AsynPushStream((char*)buffer->getBase(part),buffer->getSize(part), si);
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

            // report received and incomplete buffers

            std::cout << std::endl;
            std::cout << "Received buffers:   " << buffers_received << std::endl;
            std::cout << "Incomplete buffers: " << buffers_incomplete << std::endl;

        }

        dev->close();
    }
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

void thread_singleHostMode()
{
    singleCamera = new ioDev_genicam();
    while (1)
    {
        if (singleCamera->m_bOnline)
        {
            Sleep(1000);
            continue;
        }

        try
        {
            singleCamera->m_genicamDev = ioDev_genicam::getSingleGenicam();
            singleCamera->startStream();
        }
        catch (std::exception& e)
        {
            Sleep(1000);
        } 
    }
    
}

void ioDev_genicam::runSingleHostMode()
{
    thread t(thread_singleHostMode);
    t.detach();
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