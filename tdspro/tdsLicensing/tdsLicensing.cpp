// tdsLicensing.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <string>
#include "licence.h"
using namespace std;
using namespace Licence;

int main(int argc, char** argv)
{
    string cmd;
    if (argc > 1)
    {
        cmd = argv[1];
    }

    if (cmd == "-c")
    {
        createLicence();
    }
    else if (cmd == "-h")
    {
        printf("-c 创建licence\r\n");
        printf("-a 激活licence\r\n");
        printf("-v 验证licence\r\n");
    }
    else if (cmd == "-a")
    {
        writeCodeToLicence();
        printf("激活licence\r\n");
    }
    else if (cmd == "-v")
    {
        //licenceMng.checkLicence();
    }
}
