REM 根据模板头文件生成 带svn版本信息的头文件
echo on

REM 直接执行，将错误输出重定向到空设备
subwcrev.exe ../ "../src/version.temp.h" "../src/version.h" 2>nul
if errorlevel 1 (
    echo 警告: 执行 subwcrev.exe 失败，跳过版本信息生成
)