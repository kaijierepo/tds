#!/bin/bash
set -e

# ===================== 0. 核心配置项（仅保留清理开关）=====================
# 清理开关：yes=编译前清理 .o 文件，no=不清理（保留增量编译）
ENABLE_CLEAN="yes"               # 仅需修改这个值即可控制清理逻辑

# 架构配置（不变）
TARGET_ARCH="armv7l"             # 目标架构：x86_64/armv7l/aarch64
CC=""
CXX=""
arch_flags=""
strip_tool="strip"

# ===================== 1. 清理逻辑（仅依赖 ENABLE_CLEAN）=====================
if [ "$ENABLE_CLEAN" = "yes" ]; then
    echo "🧹 开始清理原有 .o 目标文件..."
    # 切换到源码目录并清理
    cd ../src || { echo "❌ 错误：无法进入源码目录 ../src"; exit 1; }
    # 递归删除所有 .o 文件（核心清理命令）
    find . -name "*.o" -type f -delete
    echo "✅ 清理完成！已删除所有 .o 文件"
else
    echo "ℹ️  清理功能已禁用（ENABLE_CLEAN=no），保留原有 .o 文件"
    # 切换到源码目录（不清理，仅保证目录正确）
    cd ../src || { echo "❌ 错误：无法进入源码目录 ../src"; exit 1; }
fi

# ===================== 2. 架构配置逻辑（完全不变）=====================
case "$TARGET_ARCH" in
    x86_64)
        CC="gcc"
        CXX="g++"
        arch_flags="-m64 -mtune=generic -O2"
        strip_tool="strip"
        echo "✅ 配置 x86_64 编译环境"
        ;;
    armv7l)
        CC="arm-linux-gnueabihf-gcc"
        CXX="arm-linux-gnueabihf-g++"
        arch_flags="-march=armv7-a -mtune=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4"
        strip_tool="arm-linux-gnueabihf-strip"
        echo "✅ 配置 ARM 32位 (armv7l) 编译环境"
        ;;
    aarch64)
        CC="aarch64-linux-gnu-gcc"
        CXX="aarch64-linux-gnu-g++"
        arch_flags="-march=armv8-a -mtune=cortex-a53"
        strip_tool="aarch64-linux-gnu-strip"
        echo "✅ 配置 ARM 64位 (aarch64) 编译环境"
        ;;
    *)
        echo "❌ 错误：不支持的架构 $TARGET_ARCH，仅支持 x86_64/armv7l/aarch64"
        exit 1
        ;;
esac

# 检查编译器是否安装（不变）
if ! command -v $CC &> /dev/null; then
    echo "❌ 错误：未找到 $CC 编译器，请先安装！"
    echo "📦 安装命令（Ubuntu/Debian）："
    case "$TARGET_ARCH" in
        x86_64)
            echo "  sudo apt install gcc g++"
            ;;
        armv7l)
            echo "  sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf"
            ;;
        aarch64)
            echo "  sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
            ;;
    esac
    exit 1
fi

echo "📌 当前编译目录: $(pwd)"
echo "🎯 目标架构: $TARGET_ARCH"
echo "🔧 编译工具链: $CC / $CXX"

# ===================== 3. 编译参数（不变）=====================
common_flags="\
$arch_flags \
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

linkerflags="-lpthread -lcrypto -lkrb5 -lssl -lutil -lrt -latomic -ldl"

# ===================== 4. 增量编译函数（不变）=====================
compile_c_if_needed() {
    local src_file=$1
    local obj_file=$2
    if [ ! -f "$obj_file" ] || [ "$src_file" -nt "$obj_file" ]; then
        echo "🔨 编译 C 文件: $src_file → $obj_file"
        $CC $common_flags $c_flags -c "$src_file" -o "$obj_file"
    else
        echo "⏩ 跳过 C 文件（未修改）: $src_file"
    fi
}

compile_cpp_if_needed() {
    local src_file=$1
    local obj_file=$2
    if [ ! -f "$obj_file" ] || [ "$src_file" -nt "$obj_file" ]; then
        echo "🔨 编译 C++ 文件: $src_file → $obj_file"
        $CXX $common_flags $cpp_flags -c "$src_file" -o "$obj_file"
    else
        echo "⏩ 跳过 C++ 文件（未修改）: $src_file"
    fi
}

# ===================== 5. 编译所有文件（不变）=====================
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

# ===================== 6. 链接生成可执行文件（不变）=====================
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

output_file="../out/tds/tds_$TARGET_ARCH"
echo "🔗 链接生成 $TARGET_ARCH 可执行文件: $output_file"
$CXX $common_flags $cpp_flags $obj_files -o $output_file $linkerflags

echo "⚡ 开始体积优化..."
$strip_tool --strip-all $output_file

echo "✅ 验证编译结果（文件架构）："
file $output_file

echo "🎉 编译完成！$TARGET_ARCH 版本可执行文件路径: $output_file"
echo "📦 优化后文件大小: $(du -h $output_file | awk '{print $1}')"