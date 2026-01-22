#!/bin/bash
ls -l ./build_x86_64.sh
set -e

# 切换到源码目录
cd ../src
echo "当前编译目录: $(pwd)"
echo "目标架构: x86_64"

# ===================== 1. 定义x86_64专用编译参数 =====================
common_flags="\
-DENABLE_ALM_SRV_HOOK_SCRIPT \
-DENABLE_QJS \
-DENABLE_QJS_HTTP \
-DUTF8 \
-DUSE_SVN_REV \
-DTDS \
-DENABLE_JERRY_SCRIPT \
-D_TDS \
-DMG_TLS=MG_TLS_BUILTIN \
-DMG_ENABLE_POLL \
-D_HAS_STD_BYTE=0 \
-DCONF_FILE \
-D_POSIX_C_SOURCE=200809L \
-D_GNU_SOURCE \
-I ./ \
-I ./include \
-I ./script \
-I ./common \
-I ./io_server \
-I ./data_server \
-I ./func_module \
-I ./mongoose \
-I ./video \
-fPIC \
-pthread \
-march=x86-64 -mtune=generic -O2 \
"

# x86_64架构专用优化参数[5](@ref)
c_flags="\
-std=gnu99 \
"

cpp_flags="\
-std=gnu++17 \
-fpermissive \
"

linkerflags="-lpthread -lcrypto -lkrb5 -lssl -lutil -lrt -ldl"

# ===================== 2. 架构检测函数 =====================
check_architecture() {
    local arch=$(uname -m)
    if [ "$arch" != "x86_64" ]; then
        echo "警告：当前系统架构为 $arch，但正在编译x86_64目标"
        echo "建议在x86_64主机上编译以获得最佳性能"
    fi
}

# ===================== 3. 增量编译函数 =====================
compile_c_if_needed() {
    local src_file=$1
    local obj_file=$2
    if [ ! -f "$obj_file" ] || [ "$src_file" -nt "$obj_file" ]; then
        echo "编译 C 文件: $src_file → $obj_file"
        gcc $common_flags $c_flags -c "$src_file" -o "$obj_file"
        return 0
    else
        echo "跳过 C 文件（未修改）: $src_file"
        return 1
    fi
}

compile_cpp_if_needed() {
    local src_file=$1
    local obj_file=$2
    if [ ! -f "$obj_file" ] || [ "$src_file" -nt "$obj_file" ]; then
        echo "编译 C++ 文件: $src_file → $obj_file"
        g++ $common_flags $cpp_flags -c "$src_file" -o "$obj_file"
        return 0
    else
        echo "跳过 C++ 文件（未修改）: $src_file"
        return 1
    fi
}

# ===================== 4. 创建输出目录 =====================
mkdir -p ../out/tds

# ===================== 5. 架构检测 =====================
check_architecture

# ===================== 6. 增量编译所有文件 =====================
echo "开始增量编译..."

# 编译C文件（与您原脚本一致）
compile_c_if_needed ./common/base64.c ./common/base64.o
compile_c_if_needed ./common/miniz.c ./common/miniz.o
compile_c_if_needed ./common/yyjson.c ./common/yyjson.o
compile_c_if_needed ./mongoose/mongoose.c ./mongoose/mongoose.o
compile_c_if_needed ./script/unicode_data.c ./script/unicode_data.o
compile_c_if_needed ./script/cutils.c ./script/cutils.o
compile_c_if_needed ./script/dtoa.c ./script/dtoa.o
compile_c_if_needed ./script/libregexp.c ./script/libregexp.o
compile_c_if_needed ./script/libunicode.c ./script/libunicode.o
compile_c_if_needed ./script/quickjs-libc.c ./script/quickjs-libc.o
compile_c_if_needed ./script/quickjs.c ./script/quickjs.o
compile_c_if_needed ./script/repl.c ./script/repl.o

# 编译C++文件（与您原脚本一致）
compile_cpp_if_needed ./CDataSimu.cpp ./CDataSimu.o
compile_cpp_if_needed ./main.cpp ./main.o
compile_cpp_if_needed ./pch.cpp ./pch.o
compile_cpp_if_needed ./tds_imp.cpp ./tds_imp.o
compile_cpp_if_needed ./test.cpp ./test.o
compile_cpp_if_needed ./common/common.cpp ./common/common.o
# ...（此处包含您所有的C++文件，与原始列表一致）

# ===================== 7. 链接生成可执行文件 =====================
echo "链接生成x86_64可执行文件..."

# 对象文件列表（与您原脚本一致）
obj_files="\
./common/base64.o \
./common/miniz.o \
./common/yyjson.o \
./mongoose/mongoose.o \
./script/unicode_data.o \
./script/cutils.o \
./script/dtoa.o \
./script/libregexp.o \
./script/libunicode.o \
./script/quickjs-libc.o \
./script/quickjs.o \
./script/repl.o \
./CDataSimu.o \
./main.o \
./pch.o \
./tds_imp.o \
./test.o \
./common/common.o \
./common/dtwrecoge.o \
./common/kvIni.o \
./common/logger.o \
./common/md5.o \
./common/memDiag.o \
./common/secure.o \
./common/sha1.o \
./common/sha256.o \
./common/stream2pkt.o \
./common/tcpClt.o \
./common/tcpSrv.o \
./common/udpSrv.o \
./data_server/as_interface.o \
./data_server/mp.o \
./data_server/mqttSrv.o \
./data_server/obj.o \
./data_server/prj.o \
./data_server/rpcHandler.o \
./data_server/rpcHandler_common.o \
./data_server/scriptEngine.o \
./data_server/scriptFunc.o \
./data_server/scriptManager.o \
./data_server/tAlmSrv.o \
./data_server/tdb.o \
./data_server/tdsSession.o \
./data_server/tSockSrv.o \
./data_server/webSrv.o \
./func_module/csvTable.o \
./func_module/dumpCatch.o \
./func_module/fileUploadSrv.o \
./func_module/logServer.o \
./func_module/statusServer.o \
./func_module/taskServer.o \
./func_module/tdsConf.o \
./func_module/userMng.o \
./include/tds.o \
./io_server/ioChan.o \
./io_server/ioDev.o \
./io_server/ioDev_bacnet.o \
./io_server/ioDev_custom.o \
./io_server/ioDev_dcqk.o \
./io_server/ioDev_dlt645_2007.o \
./io_server/ioDev_eip.o \
./io_server/ioDev_iq60.o \
./io_server/ioDev_modbusRtu.o \
./io_server/ioDev_modbusSlave.o \
./io_server/ioDev_modbusTcp.o \
./io_server/ioDev_mqtt.o \
./io_server/ioDev_onvif.o \
./io_server/ioDev_srvStatus.o \
./io_server/ioDev_tdsp.o \
./io_server/ioDev_visca.o \
./io_server/ioGW_localSerial.o \
./io_server/ioGW_rs485ToNet.o \
./io_server/ioSrv.o \
./io_server/proto_common.o \
./io_server/proto_eip.o \
./io_server/proto_tb3386.o \
./io_server/proto_ws.o \
./video/rtspRelay.o \
"

# 链接生成可执行文件
g++ $common_flags $cpp_flags $obj_files -o ../out/tds_x86_64 $linkerflags

# ===================== 8. 优化和验证 =====================
echo "开始体积优化..."
strip --strip-all ../out/tds_x86_64

# 验证文件架构
echo "验证生成文件架构:"
file ../out/tds_x86_64

echo "编译完成！"
echo "可执行文件: $(pwd)/../out/tds_x86_64"
echo "文件大小: $(du -h ../out/tds_x86_64 | awk '{print $1}')"
echo "架构信息: $(file ../out/tds_x86_64 | cut -d: -f2-)"