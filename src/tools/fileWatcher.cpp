#include "pch.h"
#include "FileWatcher.h"
#include "logger.h"
#include "common.hpp"
#include "data_server/ds.h"

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

FileWatcher fileWatcher;

FileWatcher::FileWatcher()
{
}

void watchFile_thread(const std::string dir_path)
{
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
                file_name = str::replace( file_name,"\\","/");
                
                
                if (file_name!= lastFileModify ||  timeopt::CalcTimePassMilliSecond(lastFileModifyTime) > 50)
                {
                    lastFileModify = file_name;
                    GetLocalTime(&lastFileModifyTime);
                    //LOG("[keyinfo]检测到文件改变:" + dir_path + "/" + file_name);

                    ds.m_mutexTdsSessionList_webHMR.lock();
                    for (int i = 0; i < ds.m_vecTdsSession_webHMR.size(); i++)
                    {
                        shared_ptr<TDS_SESSION> p = ds.m_vecTdsSession_webHMR[i];
                        if (file_name.find(".css") != string::npos)
                        {
                            p->sendStr("refreshcss");
                        }
                        else if (file_name.find(".html") != string::npos) //html只有当前路径下面的才触发更新
                        {
                            file_name = "/" + file_name;
                            if(file_name.find(p->webHMRPath) != string::npos)
                                p->sendStr("reload");
                        }
                        else
                        {
                            p->sendStr("reload");
                        }
                    }
                    ds.m_mutexTdsSessionList_webHMR.unlock();
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

void FileWatcher::run(const std::string dir_path)
{
    thread t(watchFile_thread, dir_path);
    t.detach();
}

