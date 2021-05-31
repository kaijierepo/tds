call ./build/vcvarinit.bat

cd ./src

set compilerflags=/utf-8 /MP /Zi /FC /EHsc /std:c++17 /MD /D DEBUG /Fo"..\\build\\OBJ\\" /D WINDOWS /D _MBCS /I.\ /I.\common /I.\mo_model /I.\data_server /I.\io_server /I.\alarm_server /I.\3rdparty\ffmpeg\include 
set linkerflags=/SUBSYSTEM:CONSOLE /LIBPATH:".\3rdparty\ffmpeg\lib" /ignore:4099 ole32.lib Bcrypt.lib Secur32.lib Shlwapi.lib User32.lib shell32.lib gdi32.lib vfw32.lib OleAut32.lib
md "../build/OBJ"

cl.exe %compilerflags%  .\main.cpp ..\build\unityBuild.cpp .\common\cJson.c /Fe"..\\out\\tds.exe" /link %linkerflags%

cd ../



