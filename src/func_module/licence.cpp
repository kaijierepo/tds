#include "common.h"
#include "ioSrv.h"
#include <string>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

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
    string io;
    string video;
    string id;
    string code;
    LicenceInfo() {
    }

    string toStr() {
        return "io=" + io + "\nvideo=" + video + "\nid=" + id + "\ncode=" + code;
    }

    string generateCode() {
        string src = io + video + id + "tds666";
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
            if (kv[0] == "io") {
                li.io = kv[1];
            }
            else if (kv[0] == "video") {
                li.video = kv[1];
            }
            else if (kv[0] == "id") {
                li.id = kv[1];
            }
            else if (kv[0] == "code") {
                li.code = kv[1];
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
    }

    bool isValid() { 
        string validCode = m_info.generateCode();
        string code = m_info.code;
        return validCode == code;
    }

    LicenceInfo m_info;
};

Licence l;