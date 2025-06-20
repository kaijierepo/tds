REM 根据模板头文件生成 带svn版本信息的头文件
echo on
subwcrev.exe ../ "../src/version.temp.h" "../src/version.h"