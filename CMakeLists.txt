cmake_minimum_required(VERSION 3.10)
project(tds CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -DMG_TLS=0 -DTDS -fpermissive")
set(CMAKE_BUILD_TYPE Debug)

include_directories(
    ./src 
    ./src/include
    ./src/common 
    ./src/func_module 
    ./src/data_server 
    ./src/io_server 
    ./tdspro 
    ./tdspro/func_module 
    ./tdspro/io_server 
    ./src/3rdparty/jerryscript/include 
    ./src/3rdparty/openssl/include
)

set(SOURCE_FILES
    ./tools/unityBuild.cpp
)

add_executable(tds ${SOURCE_FILES})

target_link_libraries(tds pthread stdc++fs)

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/out/tds)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/out/tds)
