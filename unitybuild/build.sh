#!/bin/bash
set -e

# 切换到源码目录
cd ../src
echo "当前编译目录: $(pwd)"

# ===================== 1. 定义编译参数（保留原有）=====================
common_flags="\
-g \
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
"

c_flags="\
-std=gnu99 \
"

cpp_flags="\
-std=gnu++17 \
-fpermissive \
"

linkerflags="-lpthread -lcrypto -lkrb5 -lssl -lutil -lrt -latomic"

# ===================== 2. 定义增量编译函数（核心新增）=====================
# 函数：判断是否需要编译 C 文件（源文件比目标文件新，或目标文件不存在）
compile_c_if_needed() {
    local src_file=$1
    local obj_file=$2
    # 如果目标文件不存在，或源文件更新时间更晚 → 编译
    if [ ! -f "$obj_file" ] || [ "$src_file" -nt "$obj_file" ]; then
        echo "编译 C 文件: $src_file → $obj_file"
        gcc $common_flags $c_flags -c "$src_file" -o "$obj_file"
    else
        echo "跳过 C 文件（未修改）: $src_file"
    fi
}

# 函数：判断是否需要编译 C++ 文件
compile_cpp_if_needed() {
    local src_file=$1
    local obj_file=$2
    if [ ! -f "$obj_file" ] || [ "$src_file" -nt "$obj_file" ]; then
        echo "编译 C++ 文件: $src_file → $obj_file"
        g++ $common_flags $cpp_flags -c "$src_file" -o "$obj_file"
    else
        echo "跳过 C++ 文件（未修改）: $src_file"
    fi
}

# ===================== 3. 增量编译所有文件（替换原有逐条编译）=====================
# --- 编译 C 文件 ---
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

# --- 编译 C++ 文件 ---
compile_cpp_if_needed ./CDataSimu.cpp ./CDataSimu.o
compile_cpp_if_needed ./main.cpp ./main.o
compile_cpp_if_needed ./pch.cpp ./pch.o
compile_cpp_if_needed ./tds_imp.cpp ./tds_imp.o
compile_cpp_if_needed ./test.cpp ./test.o
compile_cpp_if_needed ./common/common.cpp ./common/common.o
compile_cpp_if_needed ./common/dtwrecoge.cpp ./common/dtwrecoge.o
compile_cpp_if_needed ./common/kvIni.cpp ./common/kvIni.o
compile_cpp_if_needed ./common/logger.cpp ./common/logger.o
compile_cpp_if_needed ./common/md5.cpp ./common/md5.o
compile_cpp_if_needed ./common/memDiag.cpp ./common/memDiag.o
compile_cpp_if_needed ./common/secure.cpp ./common/secure.o
compile_cpp_if_needed ./common/sha1.cpp ./common/sha1.o
compile_cpp_if_needed ./common/sha256.cpp ./common/sha256.o
compile_cpp_if_needed ./common/stream2pkt.cpp ./common/stream2pkt.o
compile_cpp_if_needed ./common/tcpClt.cpp ./common/tcpClt.o
compile_cpp_if_needed ./common/tcpSrv.cpp ./common/tcpSrv.o
compile_cpp_if_needed ./common/udpSrv.cpp ./common/udpSrv.o
compile_cpp_if_needed ./data_server/as_interface.cpp ./data_server/as_interface.o
compile_cpp_if_needed ./data_server/mp.cpp ./data_server/mp.o
compile_cpp_if_needed ./data_server/mqttSrv.cpp ./data_server/mqttSrv.o
compile_cpp_if_needed ./data_server/obj.cpp ./data_server/obj.o
compile_cpp_if_needed ./data_server/prj.cpp ./data_server/prj.o
compile_cpp_if_needed ./data_server/rpcHandler.cpp ./data_server/rpcHandler.o
compile_cpp_if_needed ./data_server/rpcHandler_common.cpp ./data_server/rpcHandler_common.o
compile_cpp_if_needed ./data_server/scriptEngine.cpp ./data_server/scriptEngine.o
compile_cpp_if_needed ./data_server/scriptFunc.cpp ./data_server/scriptFunc.o
compile_cpp_if_needed ./data_server/scriptManager.cpp ./data_server/scriptManager.o
compile_cpp_if_needed ./data_server/tAlmSrv.cpp ./data_server/tAlmSrv.o
compile_cpp_if_needed ./data_server/tdb.cpp ./data_server/tdb.o
compile_cpp_if_needed ./data_server/tdsSession.cpp ./data_server/tdsSession.o
compile_cpp_if_needed ./data_server/tSockSrv.cpp ./data_server/tSockSrv.o
compile_cpp_if_needed ./data_server/webSrv.cpp ./data_server/webSrv.o
compile_cpp_if_needed ./func_module/csvTable.cpp ./func_module/csvTable.o
compile_cpp_if_needed ./func_module/dumpCatch.cpp ./func_module/dumpCatch.o
compile_cpp_if_needed ./func_module/fileUploadSrv.cpp ./func_module/fileUploadSrv.o
compile_cpp_if_needed ./func_module/logServer.cpp ./func_module/logServer.o
compile_cpp_if_needed ./func_module/statusServer.cpp ./func_module/statusServer.o
compile_cpp_if_needed ./func_module/taskServer.cpp ./func_module/taskServer.o
compile_cpp_if_needed ./func_module/tdsConf.cpp ./func_module/tdsConf.o
compile_cpp_if_needed ./func_module/userMng.cpp ./func_module/userMng.o
compile_cpp_if_needed ./include/tds.cpp ./include/tds.o
compile_cpp_if_needed ./io_server/ioChan.cpp ./io_server/ioChan.o
compile_cpp_if_needed ./io_server/ioDev.cpp ./io_server/ioDev.o
compile_cpp_if_needed ./io_server/ioDev_bacnet.cpp ./io_server/ioDev_bacnet.o
compile_cpp_if_needed ./io_server/ioDev_custom.cpp ./io_server/ioDev_custom.o
compile_cpp_if_needed ./io_server/ioDev_dcqk.cpp ./io_server/ioDev_dcqk.o
compile_cpp_if_needed ./io_server/ioDev_dlt645_2007.cpp ./io_server/ioDev_dlt645_2007.o
compile_cpp_if_needed ./io_server/ioDev_eip.cpp ./io_server/ioDev_eip.o
compile_cpp_if_needed ./io_server/ioDev_iq60.cpp ./io_server/ioDev_iq60.o
compile_cpp_if_needed ./io_server/ioDev_modbusRtu.cpp ./io_server/ioDev_modbusRtu.o
compile_cpp_if_needed ./io_server/ioDev_modbusSlave.cpp ./io_server/ioDev_modbusSlave.o
compile_cpp_if_needed ./io_server/ioDev_modbusTcp.cpp ./io_server/ioDev_modbusTcp.o
compile_cpp_if_needed ./io_server/ioDev_mqtt.cpp ./io_server/ioDev_mqtt.o
compile_cpp_if_needed ./io_server/ioDev_onvif.cpp ./io_server/ioDev_onvif.o
compile_cpp_if_needed ./io_server/ioDev_srvStatus.cpp ./io_server/ioDev_srvStatus.o
compile_cpp_if_needed ./io_server/ioDev_tdsp.cpp ./io_server/ioDev_tdsp.o
compile_cpp_if_needed ./io_server/ioDev_visca.cpp ./io_server/ioDev_visca.o
compile_cpp_if_needed ./io_server/ioGW_localSerial.cpp ./io_server/ioGW_localSerial.o
compile_cpp_if_needed ./io_server/ioGW_rs485ToNet.cpp ./io_server/ioGW_rs485ToNet.o
compile_cpp_if_needed ./io_server/ioSrv.cpp ./io_server/ioSrv.o
compile_cpp_if_needed ./io_server/proto_common.cpp ./io_server/proto_common.o
compile_cpp_if_needed ./io_server/proto_eip.cpp ./io_server/proto_eip.o
compile_cpp_if_needed ./io_server/proto_tb3386.cpp ./io_server/proto_tb3386.o
compile_cpp_if_needed ./io_server/proto_ws.cpp ./io_server/proto_ws.o
compile_cpp_if_needed ./video/rtspRelay.cpp ./video/rtspRelay.o

# ===================== 4. 链接生成可执行文件（每次都执行，确保最新）=====================
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

echo "链接生成可执行文件: ../out/tds/tds"
g++ $common_flags $cpp_flags $obj_files -o ../out/tds/tds $linkerflags

echo "编译完成！可执行文件路径: $(pwd)/../out/tds/tds"