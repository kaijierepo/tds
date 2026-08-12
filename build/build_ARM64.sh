#!/bin/bash
set -e

# ===================== 1. 清理逻辑 =====================
# 支持命令行参数: clean / rebuild 执行全量编译, 默认增量编译
ENABLE_CLEAN="no"
if [ "$1" = "clean" ] || [ "$1" = "rebuild" ]; then
    ENABLE_CLEAN="yes"
fi

if [ "$ENABLE_CLEAN" = "yes" ]; then
    echo "🧹 开始清理原有 .o 目标文件..."
    cd ../src || { echo "❌ 错误：无法进入源码目录 ../src"; exit 1; }
    find . -name "*.o" -type f -delete
    find . -name "*.d" -type f -delete
    echo "✅ 清理完成！已删除所有 .o 和 .d 文件"
else
    echo "ℹ️  增量编译模式（保留原有 .o 文件），如需全量编译请执行: $0 clean"
    cd ../src || { echo "❌ 错误：无法进入源码目录 ../src"; exit 1; }
fi

# ===================== 2. 固定 aarch64 架构配置（整合静态链接）=====================
# 直接配置 aarch64 编译环境，删除其他架构分支
TOOLCHAIN_PATH="/opt/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu"
CC="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-gcc"
CXX="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-g++"
arch_flags="-march=armv8-a"
strip_tool="${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-strip"
SYSROOT="${TOOLCHAIN_PATH}/aarch64-none-linux-gnu/libc"
# aarch64 固定使用混合链接（业务库静态，系统库动态）
# 注：MG_TLS_BUILTIN 模式不需要 -lcrypto -lssl -lkrb5 -lk5crypto -lcom_err
linkerflags="\
-Wl,--start-group \
-Wl,-Bstatic \
-Wl,-Bdynamic \
-lpthread -lutil -lrt -latomic -ldl -lc \
-Wl,--end-group \
-static-libgcc -static-libstdc++ \
"
echo "✅ 配置 ARM 64位 (aarch64) 编译环境（GLIBC，混合链接）"

# 检查编译器是否安装
if ! command -v $CC &> /dev/null; then
    echo "❌ 错误：未找到 $CC 编译器，请先安装！"
    exit 1
fi

echo "📌 当前编译目录: $(pwd)"
echo "🎯 目标架构: aarch64（固定）"
echo "🔧 编译工具链: $CC / $CXX"

# ===================== 3. 编译参数（模块化重构）=====================
# --------------------------
# 3.1 基础架构参数（必选）
# --------------------------
common_flags="$arch_flags"

# --------------------------
# 3.2 功能宏定义（业务开关）
# --------------------------
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

# --------------------------
# 3.3 系统兼容宏定义（POSIX/GNU）
# --------------------------
common_flags+=" \
-D_POSIX_C_SOURCE=200809L \
-D_GNU_SOURCE \
"

# --------------------------
# 3.4 头文件包含路径（按模块分类）
# --------------------------
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
-I ./common/crypto/psa/drivers/builtin/include \
-I ./common/crypto/psa/drivers/builtin/src \
-I ./common/crypto/psa/core \
-I ./common/crypto/library \
-I ./common/crypto/psa/utilities \
-I ./common/crypto/psa/drivers/everest/include/tf-psa-crypto/private/everest \
"

# --------------------------
# 3.5 编译特性参数（通用）
# --------------------------
common_flags+=" \
-fPIC \
-pthread \
"

# --------------------------
# 3.6 跨编译专用配置（aarch64 固定启用）
# --------------------------
common_flags+=" --sysroot=${SYSROOT} "
echo "ℹ️  已为 aarch64 添加 sysroot 路径: ${SYSROOT}"

# --------------------------
# 3.7 语言标准参数（分离C/C++）
# --------------------------
c_flags="\
-std=gnu99 \
"

cpp_flags="\
-std=gnu++17 \
-fpermissive \
-Wno-psabi \
"

# ===================== 4. 增量编译函数 =====================
compile_c_if_needed() {
    local src_file=$1
    local obj_file=$2
    local dep_file="${obj_file%.o}.d"
    local need_compile=0

    if [ ! -f "$obj_file" ]; then
        need_compile=1
    elif [ ! -f "$dep_file" ]; then
        need_compile=1
    elif [ "$src_file" -nt "$obj_file" ]; then
        need_compile=1
    else
        # 检查头文件依赖是否有变化
        if [ -f "$dep_file" ]; then
            while IFS= read -r header; do
                if [ -f "$header" ] && [ "$header" -nt "$obj_file" ]; then
                    need_compile=1
                    break
                fi
            done < <(sed -n 's/^ *//; s/ *\\*$//; /^$/d' "$dep_file" | tr ' ' '\n' | grep '\.h$')
        fi
    fi

    if [ "$need_compile" -eq 1 ]; then
        echo "🔨 编译 C 文件: $src_file → $obj_file"
        $CC $common_flags $c_flags -MMD -MP -c "$src_file" -o "$obj_file"
    else
        echo "⏩ 跳过 C 文件（未修改）: $src_file"
    fi
}

compile_cpp_if_needed() {
    local src_file=$1
    local obj_file=$2
    local dep_file="${obj_file%.o}.d"
    local need_compile=0

    if [ ! -f "$obj_file" ]; then
        need_compile=1
    elif [ ! -f "$dep_file" ]; then
        need_compile=1
    elif [ "$src_file" -nt "$obj_file" ]; then
        need_compile=1
    else
        # 检查头文件依赖是否有变化
        if [ -f "$dep_file" ]; then
            while IFS= read -r header; do
                if [ -f "$header" ] && [ "$header" -nt "$obj_file" ]; then
                    need_compile=1
                    break
                fi
            done < <(sed -n 's/^ *//; s/ *\\*$//; /^$/d' "$dep_file" | tr ' ' '\n' | grep '\.h$')
        fi
    fi

    if [ "$need_compile" -eq 1 ]; then
        echo "🔨 编译 C++ 文件: $src_file → $obj_file"
        $CXX $common_flags $cpp_flags -MMD -MP -c "$src_file" -o "$obj_file"
    else
        echo "⏩ 跳过 C++ 文件（未修改）: $src_file"
    fi
}

# ===================== 5. 编译所有文件 =====================
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
compile_c_if_needed ./common/rsa.c ./common/rsa.o
compile_c_if_needed ./common/bignum.c ./common/bignum.o

# mbedtls library
compile_c_if_needed ./common/crypto/library/ssl_tls.c ./common/crypto/library/ssl_tls.o
compile_c_if_needed ./common/crypto/library/ssl_tls13_server.c ./common/crypto/library/ssl_tls13_server.o
compile_c_if_needed ./common/crypto/library/ssl_tls13_keys.c ./common/crypto/library/ssl_tls13_keys.o
compile_c_if_needed ./common/crypto/library/ssl_tls13_generic.c ./common/crypto/library/ssl_tls13_generic.o
compile_c_if_needed ./common/crypto/library/ssl_tls13_client.c ./common/crypto/library/ssl_tls13_client.o
compile_c_if_needed ./common/crypto/library/ssl_tls12_server.c ./common/crypto/library/ssl_tls12_server.o
compile_c_if_needed ./common/crypto/library/ssl_tls12_client.c ./common/crypto/library/ssl_tls12_client.o
compile_c_if_needed ./common/crypto/library/ssl_ticket.c ./common/crypto/library/ssl_ticket.o
compile_c_if_needed ./common/crypto/library/ssl_msg.c ./common/crypto/library/ssl_msg.o
compile_c_if_needed ./common/crypto/library/ssl_debug_helpers_generated.c ./common/crypto/library/ssl_debug_helpers_generated.o
compile_c_if_needed ./common/crypto/library/ssl_cookie.c ./common/crypto/library/ssl_cookie.o
compile_c_if_needed ./common/crypto/library/ssl_client.c ./common/crypto/library/ssl_client.o
compile_c_if_needed ./common/crypto/library/ssl_ciphersuites.c ./common/crypto/library/ssl_ciphersuites.o
compile_c_if_needed ./common/crypto/library/ssl_cache.c ./common/crypto/library/ssl_cache.o
compile_c_if_needed ./common/crypto/library/pkcs7.c ./common/crypto/library/pkcs7.o
compile_c_if_needed ./common/crypto/library/net_sockets.c ./common/crypto/library/net_sockets.o
compile_c_if_needed ./common/crypto/library/mps_trace.c ./common/crypto/library/mps_trace.o
compile_c_if_needed ./common/crypto/library/mps_reader.c ./common/crypto/library/mps_reader.o
compile_c_if_needed ./common/crypto/library/mbedtls_config.c ./common/crypto/library/mbedtls_config.o
compile_c_if_needed ./common/crypto/library/error.c ./common/crypto/library/error.o
compile_c_if_needed ./common/crypto/library/debug.c ./common/crypto/library/debug.o
compile_c_if_needed ./common/crypto/library/version.c ./common/crypto/library/version.o
compile_c_if_needed ./common/crypto/library/version_features.c ./common/crypto/library/version_features.o
compile_c_if_needed ./common/crypto/library/timing.c ./common/crypto/library/timing.o
compile_c_if_needed ./common/crypto/library/x509.c ./common/crypto/library/x509.o
compile_c_if_needed ./common/crypto/library/x509_create.c ./common/crypto/library/x509_create.o
compile_c_if_needed ./common/crypto/library/x509_crl.c ./common/crypto/library/x509_crl.o
compile_c_if_needed ./common/crypto/library/x509_crt.c ./common/crypto/library/x509_crt.o
compile_c_if_needed ./common/crypto/library/x509_csr.c ./common/crypto/library/x509_csr.o
compile_c_if_needed ./common/crypto/library/x509_oid.c ./common/crypto/library/x509_oid.o
compile_c_if_needed ./common/crypto/library/x509write.c ./common/crypto/library/x509write.o
compile_c_if_needed ./common/crypto/library/x509write_crt.c ./common/crypto/library/x509write_crt.o
compile_c_if_needed ./common/crypto/library/x509write_csr.c ./common/crypto/library/x509write_csr.o

# psa/core
compile_c_if_needed ./common/crypto/psa/core/psa_crypto.c ./common/crypto/psa/core/psa_crypto.o
compile_c_if_needed ./common/crypto/psa/core/psa_crypto_client.c ./common/crypto/psa/core/psa_crypto_client.o
compile_c_if_needed ./common/crypto/psa/core/psa_crypto_driver_wrappers_no_static.c ./common/crypto/psa/core/psa_crypto_driver_wrappers_no_static.o
compile_c_if_needed ./common/crypto/psa/core/psa_crypto_random.c ./common/crypto/psa/core/psa_crypto_random.o
compile_c_if_needed ./common/crypto/psa/core/psa_crypto_slot_management.c ./common/crypto/psa/core/psa_crypto_slot_management.o
compile_c_if_needed ./common/crypto/psa/core/psa_crypto_storage.c ./common/crypto/psa/core/psa_crypto_storage.o
compile_c_if_needed ./common/crypto/psa/core/psa_its_file.c ./common/crypto/psa/core/psa_its_file.o
compile_c_if_needed ./common/crypto/psa/core/psa_util.c ./common/crypto/psa/core/psa_util.o
compile_c_if_needed ./common/crypto/psa/core/tf_psa_crypto_config.c ./common/crypto/psa/core/tf_psa_crypto_config.o
compile_c_if_needed ./common/crypto/psa/core/tf_psa_crypto_version.c ./common/crypto/psa/core/tf_psa_crypto_version.o

# psa/drivers/builtin/src
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/aes.c ./common/crypto/psa/drivers/builtin/src/aes.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/aesce.c ./common/crypto/psa/drivers/builtin/src/aesce.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/aria.c ./common/crypto/psa/drivers/builtin/src/aria.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/bignum.c ./common/crypto/psa/drivers/builtin/src/bignum.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/bignum_core.c ./common/crypto/psa/drivers/builtin/src/bignum_core.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/bignum_mod.c ./common/crypto/psa/drivers/builtin/src/bignum_mod.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/bignum_mod_raw.c ./common/crypto/psa/drivers/builtin/src/bignum_mod_raw.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/block_cipher.c ./common/crypto/psa/drivers/builtin/src/block_cipher.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/camellia.c ./common/crypto/psa/drivers/builtin/src/camellia.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/ccm.c ./common/crypto/psa/drivers/builtin/src/ccm.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/chacha20.c ./common/crypto/psa/drivers/builtin/src/chacha20.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/chacha20_neon.c ./common/crypto/psa/drivers/builtin/src/chacha20_neon.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/chachapoly.c ./common/crypto/psa/drivers/builtin/src/chachapoly.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/cipher.c ./common/crypto/psa/drivers/builtin/src/cipher.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/cipher_wrap.c ./common/crypto/psa/drivers/builtin/src/cipher_wrap.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/cmac.c ./common/crypto/psa/drivers/builtin/src/cmac.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/ctr_drbg.c ./common/crypto/psa/drivers/builtin/src/ctr_drbg.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/ecdsa.c ./common/crypto/psa/drivers/builtin/src/ecdsa.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/ecjpake.c ./common/crypto/psa/drivers/builtin/src/ecjpake.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/ecp.c ./common/crypto/psa/drivers/builtin/src/ecp.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/ecp_curves.c ./common/crypto/psa/drivers/builtin/src/ecp_curves.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/ecp_curves_new.c ./common/crypto/psa/drivers/builtin/src/ecp_curves_new.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/entropy.c ./common/crypto/psa/drivers/builtin/src/entropy.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/entropy_poll.c ./common/crypto/psa/drivers/builtin/src/entropy_poll.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/gcm.c ./common/crypto/psa/drivers/builtin/src/gcm.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/hmac_drbg.c ./common/crypto/psa/drivers/builtin/src/hmac_drbg.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/md5.c ./common/crypto/psa/drivers/builtin/src/md5.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/poly1305.c ./common/crypto/psa/drivers/builtin/src/poly1305.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/psa_crypto_aead.c ./common/crypto/psa/drivers/builtin/src/psa_crypto_aead.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/psa_crypto_cipher.c ./common/crypto/psa/drivers/builtin/src/psa_crypto_cipher.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/psa_crypto_ecp.c ./common/crypto/psa/drivers/builtin/src/psa_crypto_ecp.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/psa_crypto_ffdh.c ./common/crypto/psa/drivers/builtin/src/psa_crypto_ffdh.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/psa_crypto_hash.c ./common/crypto/psa/drivers/builtin/src/psa_crypto_hash.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/psa_crypto_mac.c ./common/crypto/psa/drivers/builtin/src/psa_crypto_mac.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/psa_crypto_pake.c ./common/crypto/psa/drivers/builtin/src/psa_crypto_pake.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/psa_crypto_rsa.c ./common/crypto/psa/drivers/builtin/src/psa_crypto_rsa.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/psa_crypto_xof.c ./common/crypto/psa/drivers/builtin/src/psa_crypto_xof.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/psa_util_internal.c ./common/crypto/psa/drivers/builtin/src/psa_util_internal.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/ripemd160.c ./common/crypto/psa/drivers/builtin/src/ripemd160.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/rsa.c ./common/crypto/psa/drivers/builtin/src/rsa.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/rsa_alt_helpers.c ./common/crypto/psa/drivers/builtin/src/rsa_alt_helpers.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/sha1.c ./common/crypto/psa/drivers/builtin/src/sha1.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/sha256.c ./common/crypto/psa/drivers/builtin/src/sha256.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/sha3.c ./common/crypto/psa/drivers/builtin/src/sha3.o
compile_c_if_needed ./common/crypto/psa/drivers/builtin/src/sha512.c ./common/crypto/psa/drivers/builtin/src/sha512.o

# psa/drivers/everest/library
compile_c_if_needed ./common/crypto/psa/drivers/everest/library/x25519.c ./common/crypto/psa/drivers/everest/library/x25519.o
compile_c_if_needed ./common/crypto/psa/drivers/everest/library/Hacl_Curve25519_joined.c ./common/crypto/psa/drivers/everest/library/Hacl_Curve25519_joined.o

# psa/extras
compile_c_if_needed ./common/crypto/psa/extras/md.c ./common/crypto/psa/extras/md.o
compile_c_if_needed ./common/crypto/psa/extras/nist_kw.c ./common/crypto/psa/extras/nist_kw.o
compile_c_if_needed ./common/crypto/psa/extras/pk.c ./common/crypto/psa/extras/pk.o
compile_c_if_needed ./common/crypto/psa/extras/pk_ecc.c ./common/crypto/psa/extras/pk_ecc.o
compile_c_if_needed ./common/crypto/psa/extras/pk_rsa.c ./common/crypto/psa/extras/pk_rsa.o
compile_c_if_needed ./common/crypto/psa/extras/pk_wrap.c ./common/crypto/psa/extras/pk_wrap.o
compile_c_if_needed ./common/crypto/psa/extras/pkparse.c ./common/crypto/psa/extras/pkparse.o
compile_c_if_needed ./common/crypto/psa/extras/pkwrite.c ./common/crypto/psa/extras/pkwrite.o

# psa/platform
compile_c_if_needed ./common/crypto/psa/platform/platform.c ./common/crypto/psa/platform/platform.o
compile_c_if_needed ./common/crypto/psa/platform/platform_util.c ./common/crypto/psa/platform/platform_util.o

# psa/utilities
compile_c_if_needed ./common/crypto/psa/utilities/asn1parse.c ./common/crypto/psa/utilities/asn1parse.o
compile_c_if_needed ./common/crypto/psa/utilities/asn1write.c ./common/crypto/psa/utilities/asn1write.o
compile_c_if_needed ./common/crypto/psa/utilities/base64.c ./common/crypto/psa/utilities/base64.o
compile_c_if_needed ./common/crypto/psa/utilities/constant_time.c ./common/crypto/psa/utilities/constant_time.o
compile_c_if_needed ./common/crypto/psa/utilities/oid.c ./common/crypto/psa/utilities/oid.o
compile_c_if_needed ./common/crypto/psa/utilities/pem.c ./common/crypto/psa/utilities/pem.o
compile_c_if_needed ./common/crypto/psa/utilities/pkcs5.c ./common/crypto/psa/utilities/pkcs5.o

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
compile_cpp_if_needed ./data_server/uplink_mqtt.cpp ./data_server/uplink_mqtt.o
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
compile_cpp_if_needed ./func_module/dumpCatch.cpp ./func_module/dumpCatch.o
compile_cpp_if_needed ./func_module/fileUploadSrv.cpp ./func_module/fileUploadSrv.o
compile_cpp_if_needed ./func_module/logServer.cpp ./func_module/logServer.o
compile_cpp_if_needed ./func_module/statusServer.cpp ./func_module/statusServer.o
compile_cpp_if_needed ./func_module/taskServer.cpp ./func_module/taskServer.o
compile_cpp_if_needed ./func_module/tdsConf.cpp ./func_module/tdsConf.o
compile_cpp_if_needed ./func_module/userMng.cpp ./func_module/userMng.o
compile_cpp_if_needed ./func_module/diskCleaner.cpp ./func_module/diskCleaner.o
compile_cpp_if_needed ./func_module/gzhServer.cpp ./func_module/gzhServer.o
compile_cpp_if_needed ./func_module/licence.cpp ./func_module/licence.o
compile_cpp_if_needed ./func_module/xiaot.cpp ./func_module/xiaot.o
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
compile_cpp_if_needed ./video/streamSession.cpp ./video/streamSession.o
compile_cpp_if_needed ./video/streamNode.cpp ./video/streamNode.o
compile_cpp_if_needed ./video/streamSession_rtsp.cpp ./video/streamSession_rtsp.o
compile_cpp_if_needed ./video/streamNode_rtp.cpp ./video/streamNode_rtp.o
compile_cpp_if_needed ./video/mp4Writer.cpp ./video/mp4Writer.o
compile_cpp_if_needed ./video/streamSession_socket.cpp ./video/streamSession_socket.o
compile_cpp_if_needed ./video/streamSession_webrtc.cpp ./video/streamSession_webrtc.o
compile_cpp_if_needed ./video/dtls_transport.cpp ./video/dtls_transport.o
compile_cpp_if_needed ./video/srtp_protect.cpp ./video/srtp_protect.o
compile_cpp_if_needed ./common/rsa_verify.cpp ./common/rsa_verify.o

# ===================== 6. 链接生成可执行文件 =====================
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
./common/rsa.o \
./common/bignum.o \
./common/crypto/library/ssl_tls.o \
./common/crypto/library/ssl_tls13_server.o \
./common/crypto/library/ssl_tls13_keys.o \
./common/crypto/library/ssl_tls13_generic.o \
./common/crypto/library/ssl_tls13_client.o \
./common/crypto/library/ssl_tls12_server.o \
./common/crypto/library/ssl_tls12_client.o \
./common/crypto/library/ssl_ticket.o \
./common/crypto/library/ssl_msg.o \
./common/crypto/library/ssl_debug_helpers_generated.o \
./common/crypto/library/ssl_cookie.o \
./common/crypto/library/ssl_client.o \
./common/crypto/library/ssl_ciphersuites.o \
./common/crypto/library/ssl_cache.o \
./common/crypto/library/pkcs7.o \
./common/crypto/library/net_sockets.o \
./common/crypto/library/mps_trace.o \
./common/crypto/library/mps_reader.o \
./common/crypto/library/mbedtls_config.o \
./common/crypto/library/error.o \
./common/crypto/library/debug.o \
./common/crypto/library/version.o \
./common/crypto/library/version_features.o \
./common/crypto/library/timing.o \
./common/crypto/library/x509.o \
./common/crypto/library/x509_create.o \
./common/crypto/library/x509_crl.o \
./common/crypto/library/x509_crt.o \
./common/crypto/library/x509_csr.o \
./common/crypto/library/x509_oid.o \
./common/crypto/library/x509write.o \
./common/crypto/library/x509write_crt.o \
./common/crypto/library/x509write_csr.o \
./common/crypto/psa/core/psa_crypto.o \
./common/crypto/psa/core/psa_crypto_client.o \
./common/crypto/psa/core/psa_crypto_driver_wrappers_no_static.o \
./common/crypto/psa/core/psa_crypto_random.o \
./common/crypto/psa/core/psa_crypto_slot_management.o \
./common/crypto/psa/core/psa_crypto_storage.o \
./common/crypto/psa/core/psa_its_file.o \
./common/crypto/psa/core/psa_util.o \
./common/crypto/psa/core/tf_psa_crypto_config.o \
./common/crypto/psa/core/tf_psa_crypto_version.o \
./common/crypto/psa/drivers/builtin/src/aes.o \
./common/crypto/psa/drivers/builtin/src/aesce.o \
./common/crypto/psa/drivers/builtin/src/aria.o \
./common/crypto/psa/drivers/builtin/src/bignum.o \
./common/crypto/psa/drivers/builtin/src/bignum_core.o \
./common/crypto/psa/drivers/builtin/src/bignum_mod.o \
./common/crypto/psa/drivers/builtin/src/bignum_mod_raw.o \
./common/crypto/psa/drivers/builtin/src/block_cipher.o \
./common/crypto/psa/drivers/builtin/src/camellia.o \
./common/crypto/psa/drivers/builtin/src/ccm.o \
./common/crypto/psa/drivers/builtin/src/chacha20.o \
./common/crypto/psa/drivers/builtin/src/chacha20_neon.o \
./common/crypto/psa/drivers/builtin/src/chachapoly.o \
./common/crypto/psa/drivers/builtin/src/cipher.o \
./common/crypto/psa/drivers/builtin/src/cipher_wrap.o \
./common/crypto/psa/drivers/builtin/src/cmac.o \
./common/crypto/psa/drivers/builtin/src/ctr_drbg.o \
./common/crypto/psa/drivers/builtin/src/ecdsa.o \
./common/crypto/psa/drivers/builtin/src/ecjpake.o \
./common/crypto/psa/drivers/builtin/src/ecp.o \
./common/crypto/psa/drivers/builtin/src/ecp_curves.o \
./common/crypto/psa/drivers/builtin/src/ecp_curves_new.o \
./common/crypto/psa/drivers/builtin/src/entropy.o \
./common/crypto/psa/drivers/builtin/src/entropy_poll.o \
./common/crypto/psa/drivers/builtin/src/gcm.o \
./common/crypto/psa/drivers/builtin/src/hmac_drbg.o \
./common/crypto/psa/drivers/builtin/src/md5.o \
./common/crypto/psa/drivers/builtin/src/poly1305.o \
./common/crypto/psa/drivers/builtin/src/psa_crypto_aead.o \
./common/crypto/psa/drivers/builtin/src/psa_crypto_cipher.o \
./common/crypto/psa/drivers/builtin/src/psa_crypto_ecp.o \
./common/crypto/psa/drivers/builtin/src/psa_crypto_ffdh.o \
./common/crypto/psa/drivers/builtin/src/psa_crypto_hash.o \
./common/crypto/psa/drivers/builtin/src/psa_crypto_mac.o \
./common/crypto/psa/drivers/builtin/src/psa_crypto_pake.o \
./common/crypto/psa/drivers/builtin/src/psa_crypto_rsa.o \
./common/crypto/psa/drivers/builtin/src/psa_crypto_xof.o \
./common/crypto/psa/drivers/builtin/src/psa_util_internal.o \
./common/crypto/psa/drivers/builtin/src/ripemd160.o \
./common/crypto/psa/drivers/builtin/src/rsa.o \
./common/crypto/psa/drivers/builtin/src/rsa_alt_helpers.o \
./common/crypto/psa/drivers/builtin/src/sha1.o \
./common/crypto/psa/drivers/builtin/src/sha256.o \
./common/crypto/psa/drivers/builtin/src/sha3.o \
./common/crypto/psa/drivers/builtin/src/sha512.o \
./common/crypto/psa/drivers/everest/library/x25519.o \
./common/crypto/psa/drivers/everest/library/Hacl_Curve25519_joined.o \
./common/crypto/psa/extras/md.o \
./common/crypto/psa/extras/nist_kw.o \
./common/crypto/psa/extras/pk.o \
./common/crypto/psa/extras/pk_ecc.o \
./common/crypto/psa/extras/pk_rsa.o \
./common/crypto/psa/extras/pk_wrap.o \
./common/crypto/psa/extras/pkparse.o \
./common/crypto/psa/extras/pkwrite.o \
./common/crypto/psa/platform/platform.o \
./common/crypto/psa/platform/platform_util.o \
./common/crypto/psa/utilities/asn1parse.o \
./common/crypto/psa/utilities/asn1write.o \
./common/crypto/psa/utilities/base64.o \
./common/crypto/psa/utilities/constant_time.o \
./common/crypto/psa/utilities/oid.o \
./common/crypto/psa/utilities/pem.o \
./common/crypto/psa/utilities/pkcs5.o \
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
./data_server/uplink_mqtt.o \
./data_server/uplinkManager.o \
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
./func_module/diskCleaner.o \
./func_module/gzhServer.o \
./func_module/licence.o \
./func_module/xiaot.o \
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
./common/rsa_verify.o \
"

output_file="../out/tds/tds_aarch64"  # 固定输出文件名
echo "🔗 链接生成 aarch64 可执行文件: $output_file"
$CXX $common_flags $cpp_flags $obj_files -o $output_file $linkerflags

echo "⚡ 开始体积优化..."
$strip_tool --strip-all $output_file

echo "✅ 验证编译结果（文件架构）："
file $output_file

echo "🎉 编译完成！aarch64 版本可执行文件路径: $output_file"
echo "📦 优化后文件大小: $(du -h $output_file | awk '{print $1}')"
