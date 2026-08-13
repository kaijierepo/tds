#!/bin/bash
set -e

# ============================================================
# TDS 统一构建脚本（x86_64 / arm64 / armv7 三架构合一）
#
# 用法:
#   bash build.sh <x86_64|arm64|armv7> [选项...]
#
# 选项:
#   --debug / --release   构建模式，默认 release（release 会 strip）
#   --clean / -c          全量重编（删除全部 .o/.d 后编译）
#   --rebuild / -r        同 --clean
#   --help / -h           显示帮助
#
# 环境变量:
#   JOBS       并行编译任务数，默认取 nproc（本机核数）
#   NO_CCACHE=1  禁用 ccache 加速
#
# 输出:
#   x86_64 → ../out/tds/tds_x86_64_<mode>
#   arm64  → ../out/tds_arm64
#   armv7  → ../out/tds_armv7
#
# 注意: 切勿同时启动多个 build.sh 实例（并行写 .o 会冲突）。
# ============================================================

# ===================== 1. 参数解析 =====================
ARCH="${1:-}"
if [ -z "$ARCH" ]; then
    echo "用法: bash build.sh <x86_64|arm64|armv7> [--debug|--release] [--clean]"
    echo "示例: bash build.sh x86_64          # 本机 x86_64 release"
    echo "      bash build.sh armv7 --debug   # armv7 debug"
    echo "      bash build.sh arm64 --clean   # arm64 全量重编"
    exit 1
fi
shift

MODE="release"
CLEAN="no"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

for arg in "$@"; do
    case "$arg" in
        --debug|debug)   MODE="debug" ;;
        --release|release) MODE="release" ;;
        --clean|-c|clean|rebuild|--rebuild|-r) CLEAN="yes" ;;
        --help|-h) echo "用法: bash build.sh <x86_64|arm64|armv7> [--debug|--release] [--clean]"; exit 0 ;;
        *) echo "忽略未知参数: $arg" ;;
    esac
done

echo "目标架构: $ARCH    模式: $MODE    并行数: $JOBS"

# ===================== 1.5 生成版本信息 =====================
# 从 git 提交次数生成 SVN_VERSION（兼容原 tds_imp.cpp 的 USE_SVN_REV）
if command -v git >/dev/null 2>&1; then
    pushd .. >/dev/null
    # 浅克隆需要解除限制才能统计完整提交数
    if git rev-parse --is-shallow-repository 2>/dev/null | grep -q '^true$'; then
        echo "检测到浅克隆，拉取完整历史以统计提交数..."
        git fetch --unshallow 2>/dev/null || true
    fi
    REV_COUNT="$(git rev-list --count HEAD 2>/dev/null || echo 0)"
    popd >/dev/null
    echo "源码版本(rev): $REV_COUNT"
    cat > ../src/version.h <<EOF
#ifndef VERSION_H_
#define VERSION_H_

#define SVN_VERSION "$REV_COUNT"

#if 0
#pragma message("warning: local modification found ,please make sure source is updated,when bulid release package")
#endif

#endif
EOF
else
    echo "警告: 未找到 git，SVN_VERSION 保持原样"
fi

# ===================== 2. 架构配置 =====================
case "$ARCH" in
x86_64)
    CC="gcc"
    CXX="g++"
    arch_flags="-march=x86-64 -mtune=generic"
    SYSROOT=""
    strip_tool="strip"
    # 本机编译：链接系统动态库
    linkerflags="-lpthread -lcrypto -lkrb5 -lssl -lutil -lrt -ldl -static-libgcc -static-libstdc++"
    output_file="../out/tds/tds_x86_64_${MODE}"
    ;;
arm64)
    TOOLCHAIN_PATH="/opt/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu"
    CC="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-gcc"
    CXX="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-g++"
    arch_flags="-march=armv8-a"
    SYSROOT="${TOOLCHAIN_PATH}/aarch64-none-linux-gnu/libc"
    strip_tool="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-strip"
    # 交叉编译：业务库静态、系统库动态的混合链接（MG_TLS_BUILTIN 不需要 openssl/krb5）
    linkerflags="\
-Wl,--start-group \
-Wl,-Bstatic \
-Wl,-Bdynamic \
-lpthread -lutil -lrt -latomic -ldl -lc \
-Wl,--end-group \
-static-libgcc -static-libstdc++ \
"
    output_file="../out/tds_arm64"
    ;;
armv7)
    TOOLCHAIN_PATH="/opt/armv7-eabihf--glibc--stable-2020.08-1"
    CC="${TOOLCHAIN_PATH}/bin/arm-buildroot-linux-gnueabihf-gcc"
    CXX="${TOOLCHAIN_PATH}/bin/arm-buildroot-linux-gnueabihf-g++"
    arch_flags="-march=armv7-a -mtune=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4"
    SYSROOT="${TOOLCHAIN_PATH}/arm-buildroot-linux-gnueabihf/sysroot"
    strip_tool="${TOOLCHAIN_PATH}/bin/arm-buildroot-linux-gnueabihf-strip"
    linkerflags="\
-Wl,--start-group \
-Wl,-Bstatic \
-Wl,-Bdynamic \
-lpthread -lutil -lrt -latomic -ldl -lc \
-Wl,--end-group \
-static-libgcc -static-libstdc++ \
"
    output_file="../out/tds_armv7"
    ;;
*)
    echo "错误: 未知架构 '$ARCH'，支持 x86_64 / arm64 / armv7"
    exit 1
    ;;
esac

# ===================== 3. 编译参数（模块化，三架构共用） =====================
# 3.1 基础架构参数
common_flags="$arch_flags"

# 3.2 功能宏定义（业务开关）
common_flags+=" \
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
"

# 3.3 系统兼容宏定义（POSIX/GNU）
common_flags+=" \
-D_POSIX_C_SOURCE=200809L \
-D_GNU_SOURCE \
"

# 3.4 头文件包含路径（按模块分类）
common_flags+=" \
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
-I ./common/crypto/psa/include \
-I ./common/crypto/psa/utilities \
-I ./common/crypto/library \
"

# 3.5 编译特性参数（通用）
common_flags+=" \
-fPIC \
-pthread \
"

# 3.6 跨编译专用配置（仅交叉编译启用）
if [ -n "$SYSROOT" ]; then
    common_flags+=" --sysroot=${SYSROOT} "
fi

# 3.7 语言标准参数（分离C/C++）
c_flags="-std=gnu99"
cpp_flags="-std=gnu++17 -fpermissive -Wno-psabi"

# 3.8 ccache 加速（自动检测，可 NO_CCACHE=1 关闭）
if [ "${NO_CCACHE:-0}" != "1" ] && command -v ccache >/dev/null 2>&1; then
    CC="ccache $CC"
    CXX="ccache $CXX"
    echo "已启用 ccache 加速"
fi

# 3.9 优化/调试参数
if [ "$MODE" = "debug" ]; then
    common_flags+=" -g -O0 "
elif [ "$MODE" = "release" ]; then
    common_flags+=" -O2 -fno-math-errno -fno-trapping-math "
fi

# ===================== 4. 检查编译器 =====================
if ! command -v $CC >/dev/null 2>&1; then
    echo "错误: 未找到编译器 $CC，请先安装工具链！"
    exit 1
fi

# ===================== 5. 源文件列表（三架构共用） =====================
# --- C 文件 ---
c_srcs="$(cat <<'EOF'
./common/base64.c
./common/miniz.c
./common/yyjson.c
./mongoose/mongoose.c
./script/unicode_data.c
./script/cutils.c
./script/dtoa.c
./script/libregexp.c
./script/libunicode.c
./script/quickjs-libc.c
./script/quickjs.c
./script/repl.c
./common/rsa.c
./common/bignum.c
./common/crypto/library/ssl_tls.c
./common/crypto/library/ssl_tls13_server.c
./common/crypto/library/ssl_tls13_keys.c
./common/crypto/library/ssl_tls13_generic.c
./common/crypto/library/ssl_tls13_client.c
./common/crypto/library/ssl_tls12_server.c
./common/crypto/library/ssl_tls12_client.c
./common/crypto/library/ssl_ticket.c
./common/crypto/library/ssl_msg.c
./common/crypto/library/ssl_debug_helpers_generated.c
./common/crypto/library/ssl_cookie.c
./common/crypto/library/ssl_client.c
./common/crypto/library/ssl_ciphersuites.c
./common/crypto/library/ssl_cache.c
./common/crypto/library/pkcs7.c
./common/crypto/library/net_sockets.c
./common/crypto/library/mps_trace.c
./common/crypto/library/mps_reader.c
./common/crypto/library/mbedtls_config.c
./common/crypto/library/error.c
./common/crypto/library/debug.c
./common/crypto/library/version.c
./common/crypto/library/version_features.c
./common/crypto/library/timing.c
./common/crypto/library/x509.c
./common/crypto/library/x509_create.c
./common/crypto/library/x509_crl.c
./common/crypto/library/x509_crt.c
./common/crypto/library/x509_csr.c
./common/crypto/library/x509_oid.c
./common/crypto/library/x509write.c
./common/crypto/library/x509write_crt.c
./common/crypto/library/x509write_csr.c
./common/crypto/psa/core/psa_crypto.c
./common/crypto/psa/core/psa_crypto_client.c
./common/crypto/psa/core/psa_crypto_driver_wrappers_no_static.c
./common/crypto/psa/core/psa_crypto_random.c
./common/crypto/psa/core/psa_crypto_slot_management.c
./common/crypto/psa/core/psa_crypto_storage.c
./common/crypto/psa/core/psa_its_file.c
./common/crypto/psa/core/psa_util.c
./common/crypto/psa/core/tf_psa_crypto_config.c
./common/crypto/psa/core/tf_psa_crypto_version.c
./common/crypto/psa/drivers/builtin/src/aes.c
./common/crypto/psa/drivers/builtin/src/aesce.c
./common/crypto/psa/drivers/builtin/src/aria.c
./common/crypto/psa/drivers/builtin/src/bignum.c
./common/crypto/psa/drivers/builtin/src/bignum_core.c
./common/crypto/psa/drivers/builtin/src/bignum_mod.c
./common/crypto/psa/drivers/builtin/src/bignum_mod_raw.c
./common/crypto/psa/drivers/builtin/src/block_cipher.c
./common/crypto/psa/drivers/builtin/src/camellia.c
./common/crypto/psa/drivers/builtin/src/ccm.c
./common/crypto/psa/drivers/builtin/src/chacha20.c
./common/crypto/psa/drivers/builtin/src/chacha20_neon.c
./common/crypto/psa/drivers/builtin/src/chachapoly.c
./common/crypto/psa/drivers/builtin/src/cipher.c
./common/crypto/psa/drivers/builtin/src/cipher_wrap.c
./common/crypto/psa/drivers/builtin/src/cmac.c
./common/crypto/psa/drivers/builtin/src/ctr_drbg.c
./common/crypto/psa/drivers/builtin/src/ecdsa.c
./common/crypto/psa/drivers/builtin/src/ecjpake.c
./common/crypto/psa/drivers/builtin/src/ecp.c
./common/crypto/psa/drivers/builtin/src/ecp_curves.c
./common/crypto/psa/drivers/builtin/src/ecp_curves_new.c
./common/crypto/psa/drivers/builtin/src/entropy.c
./common/crypto/psa/drivers/builtin/src/entropy_poll.c
./common/crypto/psa/drivers/builtin/src/gcm.c
./common/crypto/psa/drivers/builtin/src/hmac_drbg.c
./common/crypto/psa/drivers/builtin/src/md5.c
./common/crypto/psa/drivers/builtin/src/poly1305.c
./common/crypto/psa/drivers/builtin/src/psa_crypto_aead.c
./common/crypto/psa/drivers/builtin/src/psa_crypto_cipher.c
./common/crypto/psa/drivers/builtin/src/psa_crypto_ecp.c
./common/crypto/psa/drivers/builtin/src/psa_crypto_ffdh.c
./common/crypto/psa/drivers/builtin/src/psa_crypto_hash.c
./common/crypto/psa/drivers/builtin/src/psa_crypto_mac.c
./common/crypto/psa/drivers/builtin/src/psa_crypto_pake.c
./common/crypto/psa/drivers/builtin/src/psa_crypto_rsa.c
./common/crypto/psa/drivers/builtin/src/psa_crypto_xof.c
./common/crypto/psa/drivers/builtin/src/psa_util_internal.c
./common/crypto/psa/drivers/builtin/src/ripemd160.c
./common/crypto/psa/drivers/builtin/src/rsa.c
./common/crypto/psa/drivers/builtin/src/rsa_alt_helpers.c
./common/crypto/psa/drivers/builtin/src/sha1.c
./common/crypto/psa/drivers/builtin/src/sha256.c
./common/crypto/psa/drivers/builtin/src/sha3.c
./common/crypto/psa/drivers/builtin/src/sha512.c
./common/crypto/psa/drivers/everest/library/x25519.c
./common/crypto/psa/drivers/everest/library/Hacl_Curve25519_joined.c
./common/crypto/psa/extras/md.c
./common/crypto/psa/extras/nist_kw.c
./common/crypto/psa/extras/pk.c
./common/crypto/psa/extras/pk_ecc.c
./common/crypto/psa/extras/pk_rsa.c
./common/crypto/psa/extras/pk_wrap.c
./common/crypto/psa/extras/pkparse.c
./common/crypto/psa/extras/pkwrite.c
./common/crypto/psa/platform/platform.c
./common/crypto/psa/platform/platform_util.c
./common/crypto/psa/utilities/asn1parse.c
./common/crypto/psa/utilities/asn1write.c
./common/crypto/psa/utilities/base64.c
./common/crypto/psa/utilities/constant_time.c
./common/crypto/psa/utilities/oid.c
./common/crypto/psa/utilities/pem.c
./common/crypto/psa/utilities/pkcs5.c
EOF
)"

# --- C++ 文件 ---
cpp_srcs="$(cat <<'EOF'
./CDataSimu.cpp
./main.cpp
./pch.cpp
./tds_imp.cpp
./test.cpp
./common/common.cpp
./common/dtwrecoge.cpp
./common/kvIni.cpp
./common/logger.cpp
./common/md5.cpp
./common/memDiag.cpp
./common/secure.cpp
./common/sha1.cpp
./common/sha256.cpp
./common/stream2pkt.cpp
./common/tcpClt.cpp
./common/tcpSrv.cpp
./common/udpSrv.cpp
./data_server/as_interface.cpp
./data_server/mp.cpp
./data_server/uplink_mqtt.cpp
./data_server/uplinkManager.cpp
./data_server/obj.cpp
./data_server/prj.cpp
./data_server/rpcHandler.cpp
./data_server/rpcHandler_common.cpp
./data_server/scriptEngine.cpp
./data_server/scriptFunc.cpp
./data_server/scriptManager.cpp
./data_server/tAlmSrv.cpp
./data_server/tdb.cpp
./data_server/tdsSession.cpp
./data_server/tSockSrv.cpp
./data_server/webSrv.cpp
./func_module/csvTable.cpp
./func_module/dumpCatch.cpp
./func_module/fileUploadSrv.cpp
./func_module/logServer.cpp
./func_module/statusServer.cpp
./func_module/taskServer.cpp
./func_module/tdsConf.cpp
./func_module/userMng.cpp
./func_module/diskCleaner.cpp
./func_module/gzhServer.cpp
./func_module/licence.cpp
./func_module/xiaot.cpp
./include/tds.cpp
./io_server/ioChan.cpp
./io_server/ioDev.cpp
./io_server/ioDev_bacnet.cpp
./io_server/ioDev_custom.cpp
./io_server/ioDev_dcqk.cpp
./io_server/ioDev_dlt645_2007.cpp
./io_server/ioDev_eip.cpp
./io_server/ioDev_iq60.cpp
./io_server/ioDev_modbusRtu.cpp
./io_server/ioDev_modbusSlave.cpp
./io_server/ioDev_modbusTcp.cpp
./io_server/ioDev_mqtt.cpp
./io_server/ioDev_onvif.cpp
./io_server/ioDev_srvStatus.cpp
./io_server/ioDev_tdsp.cpp
./io_server/ioDev_visca.cpp
./io_server/ioGW_localSerial.cpp
./io_server/ioGW_rs485ToNet.cpp
./io_server/ioSrv.cpp
./io_server/proto_common.cpp
./io_server/proto_eip.cpp
./io_server/proto_tb3386.cpp
./io_server/proto_ws.cpp
./video/streamServer.cpp
./video/streamServer_file.cpp
./video/streamServer_rtsp.cpp
./video/streamServer_rpc.cpp
./video/streamSession.cpp
./video/streamNode.cpp
./video/streamSession_rtsp.cpp
./video/streamNode_rtp.cpp
./video/mp4Writer.cpp
./video/streamSession_socket.cpp
./video/streamSession_webrtc.cpp
./video/dtls_transport.cpp
./video/srtp_protect.cpp
./common/rsa_verify.cpp
EOF
)"

# ===================== 6. 进入源码目录 =====================
cd ../src || { echo "错误: 无法进入源码目录 ../src"; exit 1; }

# ===================== 7. 清理 =====================
if [ "$CLEAN" = "yes" ]; then
    echo "清理原有 .o/.d 目标文件..."
    find . -name "*.o" -type f -delete
    find . -name "*.d" -type f -delete
    echo "清理完成"
else
    echo "增量编译模式（保留原有 .o），全量重编请加 --clean"
fi

# ===================== 8. 并行增量编译 =====================
# 生成需要编译的命令列表（跳过已编译且头文件未变化的文件）
BUILD_CMDS="$(mktemp)"
TRAP_LIST="$BUILD_CMDS"

# 判断是否需要编译：obj 缺失 / .d 缺失 / 源文件更新 / 依赖头文件更新
append_cmd_if_needed() {
    local src_file=$1
    local obj_file=$2
    local cc_cmd=$3
    local dep_file="${obj_file%.o}.d"

    if [ ! -f "$obj_file" ] || [ ! -f "$dep_file" ] || [ "$src_file" -nt "$obj_file" ]; then
        echo "$cc_cmd -MMD -MP -c '$src_file' -o '$obj_file'" >> "$BUILD_CMDS"
        return
    fi
    # 检查依赖头文件是否更新
    while IFS= read -r header; do
        if [ -f "$header" ] && [ "$header" -nt "$obj_file" ]; then
            echo "$cc_cmd -MMD -MP -c '$src_file' -o '$obj_file'" >> "$BUILD_CMDS"
            return
        fi
    done < <(sed -n 's/^ *//; s/ *\\*$//; /^$/d' "$dep_file" | tr ' ' '\n' | grep '\.h$')
}

C_CMD="$CC $common_flags $c_flags"
CPP_CMD="$CXX $common_flags $cpp_flags"

for src in $c_srcs; do
    append_cmd_if_needed "$src" "${src%.*}.o" "$C_CMD"
done
for src in $cpp_srcs; do
    append_cmd_if_needed "$src" "${src%.*}.o" "$CPP_CMD"
done

CMD_COUNT="$(wc -l < "$BUILD_CMDS")"
if [ "$CMD_COUNT" -gt 0 ]; then
    echo "待编译文件数: $CMD_COUNT，并行数: $JOBS"
    xargs -P "$JOBS" -a "$BUILD_CMDS" -I{} bash -c '{}'
else
    echo "无需编译，所有目标文件已是最新"
fi
rm -f "$BUILD_CMDS"

# ===================== 9. 链接 =====================
obj_files=""
for src in $c_srcs $cpp_srcs; do
    obj_files+=" ${src%.*}.o"
done

mkdir -p "$(dirname "$output_file")"
echo "链接生成可执行文件: $output_file"
$CXX $common_flags $cpp_flags $obj_files -o "$output_file" $linkerflags

# ===================== 10. 体积优化与验证 =====================
if [ "$MODE" = "release" ]; then
    echo "开始体积优化 (strip)..."
    "$strip_tool" --strip-all "$output_file"
fi

echo "验证编译结果（文件架构）:"
file "$output_file"
echo "编译完成！产物路径: $output_file"
echo "优化后文件大小: $(du -h "$output_file" | awk '{print $1}')"
exit 0
