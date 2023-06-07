#include "pch.h"
#include "tds_imp.h"


//https://learn.microsoft.com/en-us/cpp/preprocessor/init-seg?view=msvc-170
//init_seg 表示优选初始化全局变量，优先级别为lib分组
#pragma init_seg(lib)
i_tds* tds = new TDS_imp();