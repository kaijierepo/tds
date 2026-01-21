call ./build/vcvarinit.bat

cd ./src

set compilerflags=/utf-8 /MP /Zi /FC /EHsc /std:c++17 /MTd /D DEBUG /Fo"..\\build\\OBJ\\" /D WINDOWS /D _MBCS /I.\ /I.\common /I.\func_module /I.\data_server /I.\io_server /I..\tdspro /I..\tdspro\func_module /I..\tdspro\io_server /I.\3rdparty\jerryscript\include /I.\3rdparty\openssl\include /I.\3rdparty\jerryscript\include

set linkerflags=/SUBSYSTEM:CONSOLE /LIBPATH:".\3rdparty\jerryscript\lib\MTd" /ignore:4099 /ignore:4477 jerry-core.lib jerry-ext.lib jerry-port-default.lib ole32.lib Bcrypt.lib Secur32.lib Shlwapi.lib User32.lib shell32.lib gdi32.lib vfw32.lib OleAut32.lib
md "../build/OBJ"


cl.exe %compilerflags%  .\main.cpp ..\build\unityBuild.cpp /Fe"..\\out\\tds.exe" /link %linkerflags%

cd ../



