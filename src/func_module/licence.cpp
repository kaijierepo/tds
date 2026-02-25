#include "common.h"
#include "ioSrv.h"
#include <string>
#include <ctime>
#include "webSrv.h"
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif


string get_hid_v1(){
    if(fs::fileExist("/etc/tds/mid.txt")){
        string mid;
        fs::readFile("/etc/tds/mid.txt", mid);
        return mid;
     }
     else {
        auto now = std::chrono::high_resolution_clock::now();
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        std::stringstream thread_id_ss;
        thread_id_ss << std::this_thread::get_id();
        size_t thread_id_num = 0;
        thread_id_ss >> thread_id_num;  
        
        string mid = "mid_v1_" + std::to_string(nanoseconds) + std::to_string(thread_id_num);
         fs::writeFile("/etc/tds/mid.txt", mid);
         return mid;
    }
}

bool licence_handler(mg_http_message* hm, struct mg_connection* c){
    if (mg_http_match_uri(hm, "/licence/mid/v1")) //hardware id v1
    {
        string mid = get_hid_v1();
        string contentLen = to_string(mid.size());

        string resHeader =
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin:*\r\n"  //允许所有源，也可以指定请求中的源
            "Access-Control-Allow-Private-Network: true\r\n" //CORS-RFC1918 允许私有网络请求
            "Content-Length:" + contentLen + "\r\n\r\n";

        mg_send(c, resHeader.c_str(), resHeader.size());
        mg_send(c, mid.c_str(), mid.size());
        c->is_resp = 0;
        return true;
    }
    else if (mg_http_match_uri(hm, "/licence/mid/v2")) {
        return true;
    }
    return false;
}

time_t getFileCreationTime(const std::string& filepath) {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(
        filepath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return 0; 
    }

    FILETIME creationTime, lastAccessTime, lastWriteTime;
    time_t result = 0;

    if (GetFileTime(hFile, &creationTime, &lastAccessTime, &lastWriteTime)) {
        ULARGE_INTEGER ull;
        ull.LowPart = creationTime.dwLowDateTime;
        ull.HighPart = creationTime.dwHighDateTime;
        const ULONGLONG EPOCH_DIFFERENCE = 116444736000000000ULL;
        ull.QuadPart -= EPOCH_DIFFERENCE;
        result = static_cast<time_t>(ull.QuadPart / 10000000ULL);
    }

    CloseHandle(hFile);
    return result;

#else
    struct stat fileStat;

    if (stat(filepath.c_str(), &fileStat) != 0) {
        return 0; 
    }

#ifdef st_birthtime
    return fileStat.st_birthtime;
#else
    return fileStat.st_mtime;
#endif
#endif
}

std::string encrypt(const std::string& input) {
    std::string result = input;
    char key = 0x66;
    for (size_t i = 0; i < result.length(); i++) {
        result[i] = result[i] ^ key;
        if (result[i] < 32) {
            result[i] += 32;
        }
        else if (result[i] > 126) {
            result[i] -= 32;
        }
    }
    return result;
}

struct LicenceInfo {
    string info;
    string id;
    string key;
    LicenceInfo() {
    }

    string toStr() {
        return "info=" + info  + "\nmid=" + id + "\nkey=" + key;
    }

    string generateCode() {
        string src = info + id + "tds666";
        return encrypt(src);
    }
};

string encodeID(time_t t) {
    string s = str::fromInt(t);
    for (int i = 0; i < s.size(); i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            s[i] = 'A' + (s[i] - '0');
        }
    }
    return s;
}

LicenceInfo readLicence() {
    LicenceInfo li;
    string p = fs::appPath() + "/license.txt";
    if (!fs::fileExist(p)) {
        LicenceInfo info;
        string license = info.toStr();
        fs::writeFile(p, license);
        time_t t = getFileCreationTime(p);
        string id = encodeID(t);
        info.id = id;
        license = info.toStr();
        fs::writeFile(p, license);
    }
    string data;
    fs::readFile(p, data);
    vector<string> list;
    str::split(list,data, "\n");
    for (auto& s : list) {
        vector<string> kv;
        str::split(kv, s, "=");
        if (kv.size() == 2) {
            if (kv[0] == "info") {
                li.info = kv[1];
            }
            else if (kv[0] == "mid") {
                li.id = kv[1];
            }
            else if (kv[0] == "key") {
                li.key = kv[1];
            }
        }
    }

    return li;
}


class Licence {
public:
	Licence() {
        m_info = readLicence();
        bool valid = isValid();
        printf(valid ? "licence valid\n" : "licence invalid,io server stopped\n");
        if (!valid) {
            ioSrv.m_bEnableAcq = false;
        }

        g_mapHttpHandler["/licence/*"] = licence_handler;
    }

    bool isValid() { 
        string validCode = m_info.generateCode();
        string code = m_info.key;
        return validCode == code;
    }

    LicenceInfo m_info;
};


Licence l;