

cd ../src

pwd

compilerflags="-g -DMG_TLS=0 -std=c++17 -fpermissive -I ./ -I ./common -I ./func_module -I ./data_server -I ./io_server -I ../tdspro -I ../tdspro/func_module -I ../tdspro/io_server -I ./3rdparty/jerryscript/include -I ./3rdparty/openssl/include -I./3rdparty/jerryscript/include"

linkerflags="-l iconv"

g++ -DTDS $compilerflags ../build/unityBuild.cpp -o ../out/tds $linkerflags



