#!/bin/bash
set -e

# ===================== 使用说明 =====================
usage() {
    echo "用法: $0 <架构> [选项]"
    echo ""
    echo "支持的架构:"
    echo "  x86_64    - x86 64位架构"
    echo "  arm64     - ARM 64位架构"
    echo ""
    echo "选项:"
    echo "  --debug           调试模式（保留符号信息）"
    echo "  --release         发布模式（剥离符号，优化体积）[默认]"
    echo "  --rebuild, -r     强制重新编译所有源文件"
    echo "  --clean           删除所有 .o 目标文件并退出"
    echo "  --help, -h        显示帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 x86_64          # x86_64 release 构建"
    echo "  $0 arm64 --debug   # ARM64 debug 构建"
    echo "  $0 x86_64 -r       # x86_64 强制重编译"
    echo "  $0 arm64 --clean   # 清理 ARM64 目标文件"
    exit 0
}

# ===================== 参数解析 =====================
if [ $# -eq 0 ]; then
    echo "错误: 请指定目标架构 (x86_64 或 arm64)"
    echo ""
    usage
fi

TARGET_ARCH=""
BUILD_MODE="release"
REBUILD_MODE=false
CLEAN_MODE=false

# 第一个参数是架构
case "$1" in
    x86_64)
        TARGET_ARCH="x86_64"
        ;;
    arm64|aarch64)
        TARGET_ARCH="arm64"
        ;;
    --help|-h)
        usage
        ;;
    *)
        echo "错误: 未知架构 '$1'"
        echo "支持的架构: x86_64, arm64"
        exit 1
        ;;
esac

# 解析后续参数
shift
for arg in "$@"; do
    case "$arg" in
        --debug)
            BUILD_MODE="debug"
            ;;
        --release)
            BUILD_MODE="release"
            ;;
        --rebuild|-r)
            REBUILD_MODE=true
            ;;
        --clean)
            CLEAN_MODE=true
            ;;
        --help|-h)
            usage
            ;;
        *)
            echo "未知参数: $arg"
            usage
            ;;
    esac
done

# 切换到源码目录
cd ../src
echo "========================================"
echo "当前编译目录: $(pwd)"
echo "目标架构: $TARGET_ARCH"
echo "编译模式: $BUILD_MODE"
echo "========================================"

# ===================== 清理逻辑 =====================
if [ "$CLEAN_MODE" = true ]; then
    echo "🧹 清理模式: 删除所有 .o 目标文件..."
    find . -name "*.o" -type f -delete
    echo "✅ 清理完成!"
    exit 0
fi

# ===================== 1. 定义编译参数 =====================
# 基础通用参数
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
-I ./common/crypto/include \
-I ./common/crypto/include/psa \
-I ./common/crypto/include/tf-psa-crypto \
-I ./common/crypto/include/tf-psa-crypto/private \
-I ./common/crypto/psa/drivers/builtin/include \
-I ./common/crypto/psa/drivers/builtin/include/mbedtls/private \
-I ./common/crypto/psa/drivers/builtin/src \
-I ./common/crypto/psa/core \
-I ./common/crypto/psa/drivers/everest/include \
-I ./common/crypto/psa/drivers/everest/include/tf-psa-crypto/private/everest \
-I ./common/crypto/psa/drivers/everest/include/tf-psa-crypto/private/everest/kremlib \
-I ./common/crypto/psa/drivers/everest/include/tf-psa-crypto/private/everest \
-I ./common/crypto/psa/include \
-I ./common/crypto/psa/utilities \
-I ./common/crypto/library \
-fPIC \
-pthread \
"

# C 编译标志
c_flags="-std=gnu99"

# C++ 编译标志
cpp_flags="-std=gnu++17 -fpermissive"

# ===================== 2. 架构特定参数 =====================
case "$TARGET_ARCH" in
    x86_64)
        # x86_64 专用: 使用 generic 调度优化
        common_flags+="-march=x86-64 -mtune=generic "
        # 链接标志: 静态链接 libgcc/libstdc++
        linkerflags="-lpthread -lcrypto -lkrb5 -lssl -lutil -lrt -ldl -static-libgcc -static-libstdc++"
        # 输出文件名
        output_file="../out/tds/tds_x86_64_${BUILD_MODE}"
        ;;
    arm64)
        # ARM64: 使用原生架构优化
        common_flags+="-march=armv8-a "
        # 链接标志: 需要 -latomic (ARM atomic指令)
        linkerflags="-lpthread -lcrypto -lkrb5 -lssl -lutil -lrt -latomic -ldl"
        # 输出文件名
        output_file="../out/tds/tds_arm64_${BUILD_MODE}"
        ;;
esac

# 根据编译模式追加参数
if [ "$BUILD_MODE" = "debug" ]; then
    common_flags+="-g -O0 "
elif [ "$BUILD_MODE" = "release" ]; then
    common_flags+="-O2 -fno-math-errno -fno-trapping-math "
else
    echo "错误: BUILD_MODE 只能是 debug 或 release"
    exit 1
fi

# ===================== 3. 增量编译函数 =====================
compile_c_if_needed() {
    local src_file=$1
    local obj_file=$2
    if [ "$REBUILD_MODE" = true ] || [ ! -f "$obj_file" ] || [ "$src_file" -nt "$obj_file" ]; then
        echo "编译 C 文件: $src_file → $obj_file"
        gcc $common_flags $c_flags -c "$src_file" -o "$obj_file"
    else
        echo "跳过 C 文件（未修改）: $src_file"
    fi
}

compile_cpp_if_needed() {
    local src_file=$1
    local obj_file=$2
    if [ "$REBUILD_MODE" = true ] || [ ! -f "$obj_file" ] || [ "$src_file" -nt "$obj_file" ]; then
        echo "编译 C++ 文件: $src_file → $obj_file"
        g++ $common_flags $cpp_flags -c "$src_file" -o "$obj_file"
    else
        echo "跳过 C++ 文件（未修改）: $src_file"
    fi
}

# ===================== 3.5 编译 mbedtls 静态库 =====================
MBEDTLS_LIB="../out/tds/libmbedtls.a"
compile_mbedtls() {
    local mbedtls_src_dirs=(
        "./common/crypto/library"
        "./common/crypto/psa/core"
        "./common/crypto/psa/drivers/builtin/src"
        "./common/crypto/psa/drivers/everest/library"
        "./common/crypto/psa/drivers/everest/library/kremlib"
        "./common/crypto/psa/drivers/everest/library/legacy"
        "./common/crypto/psa/extras"
        "./common/crypto/psa/platform"
        "./common/crypto/psa/utilities"
    )
    
    local all_obj_files=""
    local need_rebuild=false
    
    # 收集所有需要编译的 .c 文件
    local c_files=()
    for dir in "${mbedtls_src_dirs[@]}"; do
        if [ -d "$dir" ]; then
            while IFS= read -r -d '' f; do
                c_files+=("$f")
            done < <(find "$dir" -name "*.c" -print0 2>/dev/null)
        fi
    done
    
    # 检查是否需要重新编译
    if [ "$REBUILD_MODE" = true ] || [ ! -f "$MBEDTLS_LIB" ]; then
        need_rebuild=true
    else
        for src_file in "${c_files[@]}"; do
            if [ "$src_file" -nt "$MBEDTLS_LIB" ]; then
                need_rebuild=true
                break
            fi
        done
    fi
    
    if [ "$need_rebuild" = false ]; then
        echo "跳过 mbedtls 库（未修改）"
        return
    fi
    
    echo "编译 mbedtls 库..."
    
    local obj_list=""
    for src_file in "${c_files[@]}"; do
        local obj_file="${src_file%.c}.o"
        echo "  编译: $src_file"
        # FStar_UInt128_extracted.c 需要结构体版本的 uint128
        if [[ "$src_file" == *"FStar_UInt128_extracted"* ]]; then
            gcc $common_flags $c_flags -DKRML_VERIFIED_UINT128 -c "$src_file" -o "$obj_file" || exit 1
        else
            gcc $common_flags $c_flags -c "$src_file" -o "$obj_file" || exit 1
        fi
        obj_list="$obj_list $obj_file"
    done
    
    # 打包成静态库
    echo "  打包: $MBEDTLS_LIB"
    ar rcs "$MBEDTLS_LIB" $obj_list
    
    # 清理临时 .o 文件
    for obj_file in $obj_list; do
        rm -f "$obj_file"
    done
    
    echo "mbedtls 库编译完成"
}

# ===================== 4. 创建输出目录 =====================
mkdir -p ../out/tds

compile_mbedtls

# ===================== 5. 增量编译所有文件 =====================
echo ""
echo "开始编译..."

# --- 编译 C 文件 ---
compile_c_if_needed ./common/bignum.c ./common/bignum.o
compile_c_if_needed ./common/rsa.c ./common/rsa.o
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
compile_cpp_if_needed ./common/rsa_verify.cpp ./common/rsa_verify.o
compile_cpp_if_needed ./data_server/as_interface.cpp ./data_server/as_interface.o
compile_cpp_if_needed ./data_server/mp.cpp ./data_server/mp.o
compile_cpp_if_needed ./data_server/mqttSrv.cpp ./data_server/mqttSrv.o
compile_cpp_if_needed ./data_server/uplinkManager.cpp ./data_server/uplinkManager.o
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
compile_cpp_if_needed ./func_module/diskCleaner.cpp ./func_module/diskCleaner.o
compile_cpp_if_needed ./func_module/dumpCatch.cpp ./func_module/dumpCatch.o
compile_cpp_if_needed ./func_module/fileUploadSrv.cpp ./func_module/fileUploadSrv.o
compile_cpp_if_needed ./func_module/gzhServer.cpp ./func_module/gzhServer.o
compile_cpp_if_needed ./func_module/licence.cpp ./func_module/licence.o
compile_cpp_if_needed ./func_module/logServer.cpp ./func_module/logServer.o
compile_cpp_if_needed ./func_module/smsServer.cpp ./func_module/smsServer.o
compile_cpp_if_needed ./func_module/statusServer.cpp ./func_module/statusServer.o
compile_cpp_if_needed ./func_module/taskServer.cpp ./func_module/taskServer.o
compile_cpp_if_needed ./func_module/tdsConf.cpp ./func_module/tdsConf.o
compile_cpp_if_needed ./func_module/tdsWatchDog.cpp ./func_module/tdsWatchDog.o
compile_cpp_if_needed ./func_module/userMng.cpp ./func_module/userMng.o
compile_cpp_if_needed ./func_module/xiaot.cpp ./func_module/xiaot.o
compile_cpp_if_needed ./func_module/aliDDNS.cpp ./func_module/aliDDNS.o
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
compile_cpp_if_needed ./video/streamServer.cpp ./video/streamServer.o
compile_cpp_if_needed ./video/streamServer_file.cpp ./video/streamServer_file.o
compile_cpp_if_needed ./video/streamServer_rtsp.cpp ./video/streamServer_rtsp.o
compile_cpp_if_needed ./video/streamServer_rpc.cpp ./video/streamServer_rpc.o
compile_cpp_if_needed ./video/streamNode.cpp ./video/streamNode.o
compile_cpp_if_needed ./video/streamSession.cpp ./video/streamSession.o
compile_cpp_if_needed ./video/streamSession_rtsp.cpp ./video/streamSession_rtsp.o
compile_cpp_if_needed ./video/streamNode_rtp.cpp ./video/streamNode_rtp.o
compile_cpp_if_needed ./video/mp4Writer.cpp ./video/mp4Writer.o
compile_cpp_if_needed ./video/streamSession_socket.cpp ./video/streamSession_socket.o
compile_cpp_if_needed ./video/streamSession_webrtc.cpp ./video/streamSession_webrtc.o
compile_cpp_if_needed ./video/dtls_transport.cpp ./video/dtls_transport.o
compile_cpp_if_needed ./video/srtp_protect.cpp ./video/srtp_protect.o

# ===================== 6. 链接生成可执行文件 =====================
echo ""
echo "链接生成可执行文件: $output_file"

obj_files="\
./common/bignum.o \
./common/rsa.o \
./common/rsa_verify.o \
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
./func_module/diskCleaner.o \
./func_module/dumpCatch.o \
./func_module/fileUploadSrv.o \
./func_module/gzhServer.o \
./func_module/licence.o \
./func_module/logServer.o \
./func_module/smsServer.o \
./func_module/statusServer.o \
./func_module/taskServer.o \
./func_module/tdsConf.o \
./func_module/tdsWatchDog.o \
./func_module/userMng.o \
./func_module/xiaot.o \
./func_module/aliDDNS.o \
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
./video/streamServer.o \
./video/streamServer_file.o \
./video/streamServer_rtsp.o \
./video/streamServer_rpc.o \
./video/streamNode.o \
./video/streamSession.o \
./video/streamSession_rtsp.o \
./video/streamNode_rtp.o \
./video/mp4Writer.o \
./video/streamSession_socket.o \
./video/streamSession_webrtc.o \
./video/dtls_transport.o \
./video/srtp_protect.o \
"

g++ $common_flags $cpp_flags $obj_files -o $output_file $linkerflags $MBEDTLS_LIB

# ===================== 7. 优化和验证 =====================
echo ""
if [ "$BUILD_MODE" = "release" ]; then
    echo "开始体积优化（release模式）..."
    strip --strip-all $output_file
else
    echo "debug模式跳过strip，保留完整调试信息"
fi

echo ""
echo "========================================"
echo "✅ 编译完成!"
echo "========================================"
echo "可执行文件: $(pwd)/$output_file"
echo "文件大小: $(du -h $output_file | awk '{print $1}')"
echo "架构信息: $(file $output_file | cut -d: -f2-)"
echo "========================================"
