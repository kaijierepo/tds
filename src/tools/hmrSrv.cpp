#include "pch.h"
#include "hmrSrv.h"
#include "logger.h"
#include "common.hpp"
#include "data_server/ds.h"

string hmrCodeStr = R"(
<!--code injected by TDS for hot module replacement-->
<script>
if ('WebSocket' in window) {
    (function () {
        function refreshCSS() {
            var sheets = [].slice.call(document.getElementsByTagName("link"));
            var head = document.getElementsByTagName("head")[0];
            for (var i = 0; i < sheets.length; ++i) {
                var elem = sheets[i];
                var parent = elem.parentElement || head;
                parent.removeChild(elem);
                var rel = elem.rel;
                if (elem.href && typeof rel != "string" || rel.length == 0 || rel.toLowerCase() == "stylesheet") {
                    var url = elem.href.replace(/(&|\?)_cacheOverride=\d+/, '');
                    elem.href = url + (url.indexOf('?') >= 0 ? '&' : '?') + '_cacheOverride=' + (new Date().valueOf());
                }
                parent.appendChild(elem);
            }
        }
        var wsSock = connectHMRSrv();
        function connectHMRSrv()
        {
            var protocol = window.location.protocol === 'http:' ? 'ws://' : 'wss://';
            var address = protocol + window.location.hostname + ":668" + window.location.pathname;
            var socket = new WebSocket(address);
            socket.onmessage = function (msg) {
                if (msg.data == 'reload') window.location.reload();
                else if (msg.data == 'refreshcss') refreshCSS();
            };
			socket.onopen = (event)=>{
				console.log("tds hot module replacement on 668 connected!");
			};
            if (sessionStorage && !sessionStorage.getItem('IsThisFirstTime_Log_From_LiveServer')) {
                console.log('Live reload enabled.');
                sessionStorage.setItem('IsThisFirstTime_Log_From_LiveServer', true);
            }
            return socket;
        }

        setInterval(() => {
            if(wsSock != null && wsSock.readyState == wsSock.CLOSED)
            {
                wsSock = connectHMRSrv();
            }
        }, 500);
    })();
}
else {
    console.error('Upgrade your browser. This Browser is NOT supported WebSocket for Live-Reloading.');
}
</script>
)";


string wstring2string(wstring wstr) {
    string result;
    //获取缓冲区大小，并申请空间，缓冲区大小事按字节计算的  
    int len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), NULL, 0, NULL, NULL);
    char* buffer = new char[len + 1];
    //宽字节编码转换成多字节编码  
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), buffer, len, NULL, NULL);
    buffer[len] = '\0';
    //删除缓冲区并返回值  
    result.append(buffer);
    delete[] buffer;
    return result;
}

HMRServer hmrServer;

HMRServer::HMRServer()
{
}

void HMRServer::watchFile_process(string dir_path) {
    if (dir_path.empty()) {
        printf("path is null");
        return;
    }
    string lastFileModify;
    SYSTEMTIME lastFileModifyTime;
    GetLocalTime(&lastFileModifyTime);
#ifdef WIN32
    HANDLE h_dir = INVALID_HANDLE_VALUE;
    BYTE lp_buffer[1024];
    ZeroMemory(lp_buffer, 1024);
    DWORD bytes = NULL;
    BOOL isok = FALSE;
    FILE_NOTIFY_INFORMATION* pnotify = (FILE_NOTIFY_INFORMATION*)lp_buffer;
    FILE_NOTIFY_INFORMATION* tmp;
    ZeroMemory(&lp_buffer, sizeof(FILE_NOTIFY_INFORMATION));
    h_dir = CreateFile((LPCSTR)dir_path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ |
        FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (INVALID_HANDLE_VALUE == h_dir) {
        printf("error %s", GetLastError());
        return;
    }
    WCHAR* ws_file_name = new wchar_t[_MAX_FNAME];
    while (1) {//m_start 判断线程结束的标志
        //FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,可更改为其他需要检测到的文件的某些变化
        isok = ReadDirectoryChangesW(h_dir, &lp_buffer, sizeof(lp_buffer), TRUE,
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytes, NULL, NULL);
        if (isok) {
            tmp = pnotify;
            if (tmp->FileNameLength) {
                memcpy(ws_file_name, tmp->FileName, (tmp->FileNameLength + 1) * 2);
            }

            if (tmp->Action == FILE_ACTION_MODIFIED) {//判断文件发生变化具体的事件
                string file_name = wstring2string(ws_file_name);//得到发生变化的文件名
                file_name = str::replace(file_name, "\\", "/");


                if (file_name != lastFileModify || timeopt::CalcTimePassMilliSecond(lastFileModifyTime) > 50)
                {
                    lastFileModify = file_name;
                    GetLocalTime(&lastFileModifyTime);
                    //LOG("[keyinfo]检测到文件改变:" + dir_path + "/" + file_name);

                    m_mutexSessions.lock();
                    for (auto& i : m_mapSessions)
                    {
                        HMR_SESSION hs = i.second;
                        if (file_name.find(".css") != string::npos)
                        {
                            websocketSend("refreshcss",hs.sock);
                        }
                        else if (file_name.find(".html") != string::npos) //html只有当前路径下面的才触发更新
                        {
                            file_name = "/" + file_name;
                            if (file_name.find(hs.webHMRPath) != string::npos)
                                websocketSend("reload",hs.sock);
                        }
                        else
                        {
                            websocketSend("reload",hs.sock);
                        }
                    }
                    m_mutexSessions.unlock();
                }


            }
            ZeroMemory(tmp, 1024);
        }
        else {
            printf("ReadDirectoryChangesW error");
        }
    }
    if (ws_file_name) {
        delete[]ws_file_name;
    }
    CloseHandle(h_dir);
#else
    int inotify_fd, wd;
    char buf[BUF_LEN];
    ssize_t num_read;
    char* p;
    struct inotify_event* event;
    inotify_fd = inotify_init();
    if (inotify_fd == -1) {
        printf("inotifyFd 初始化失败");
    }
    wd = inotify_add_watch(inotify_fd, dir_path.c_str(), IN_CLOSE_WRITE);
    if (wd == -1) {
        printf("inotify_add_watch error\n");
    }
    while (m_start) {//判断线程结束标志
        num_read = read(inotify_fd, buf, BUF_LEN);
        if (num_read == -1) {
            printf("read error %s", strerror(errno));
        }
        for (p = buf; p < buf + num_read;) {
            event = (struct inotify_event*)p;
            string fn = event->name;//获取到发生变化的文件名
            //添加文件变化后要进行的操作
            //......
        }
        p += sizeof(struct inotify_event) + event->len;
    }
}
close(inotify_fd);
#endif
}

void watchFile_thread(HMRServer* p,const std::string dir_path)
{
    p->watchFile_process(dir_path);
}

void HMRServer::run(const std::string dir_path)
{
    thread t(watchFile_thread, this,dir_path);
    t.detach();

    //http热更新服务 668
    if (tds->conf->debugMode)
    {
        m_httpHotUpdateSrv = new tcpSrv();
        if (!m_httpHotUpdateSrv->run(this, 668))
        {
            if (m_httpHotUpdateSrv->m_lastError == WSAEADDRINUSE)//10048)
            {
                LOG("ERROR:10048,Only one usage of each socket address (protocol/network address/port) is normally permitted.");
            }
            else if (m_httpHotUpdateSrv->m_lastError == WSAEACCES)//10013)
            {
                LOG("ERROR:10013,An attempt was made to access a socket in a way forbidden by its access permissions.");
            }
            LOG("[error]HTTP热更新服务websocket服务端口668启动失败！");
        }
        LOG("[keyinfo][Web热更新 ] 端口:" + str::fromInt(668));
    }
}


void HMRServer::websocketSend(string s, int sock)
{
    CWSPPkt req;
    req.pack(s.c_str(), s.length(), WS_FrameType::WS_TEXT_FRAME);
    send(sock, req.data, req.len,0);
}


void HMRServer::statusChange_tcpSrv(tcpSession* pTcpSession, bool bIsConn)
{
    if (bIsConn)
    {
        HMR_SESSION s;
        s.sock = pTcpSession->sock;
        m_mutexSessions.lock();
        m_mapSessions[pTcpSession] = s;
        m_mutexSessions.unlock();
    }
    else
    {
        if (pTcpSession->pALSession)
        {
            m_mutexSessions.lock();
            delete pTcpSession->pALSession;
            m_mapSessions.erase(pTcpSession);
            m_mutexSessions.unlock();
        }
    }
}

void HMRServer::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess)
{
    m_mutexSessions.lock();
    HMR_SESSION hs = m_mapSessions[pTcpSess];
    m_mutexSessions.unlock();

    string strData = str::fromBuff(pData, iLen);
    if (CWSPPkt::isHandShake(strData)) {
        httplib::Request httpReq;
        httplib::Server srv;
        srv.parse_request_line(strData.c_str(), httpReq);
        string path = str::trimSuffix(httpReq.target, "index.html");
        path = str::trimSuffix(path, "/");
        hs.webHMRPath = path; //当前链接关联的web热更新目录

        //回复websocket握手
        CWSPPkt req;
        std::string handshakeString = req.GetHandshakeString(strData);
        //http协议回复
        ::send(pTcpSess->sock,handshakeString.c_str(), handshakeString.length(),0);
    }
}

