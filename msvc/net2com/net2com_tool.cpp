// net2com_tool.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "net2com.h"

int main()
{
    tcp2com  t2c;
    t2c.run();

    while (1) {
		Sleep(1000);
    }
}
