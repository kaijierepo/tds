#!/bin/bash
set -e

# ============================================================
# TDS 发布打包脚本
#
# 把 build.sh 构建出的可执行文件打包为:
#   out/tds/tds-{提交次数}-linux-{架构}-{yyMMddHHmmss}.tar.gz
# 直接在 out/tds 中打包：不拷贝、不创建 out/dist，
# 压缩包为扁平结构（包内仅 tds 文件，无目录）。
#
# 用法:
#   bash package.sh <x86_64|arm64|armv7> [release|debug]
#
# 需先运行 build.sh 完成构建。版本号取自 Gitee API 的提交次数
# （commits 接口响应头 commit_count，匿名可访问）。
#
# 输出:
#   out/tds/tds-{提交次数}-linux-{架构}-{yyMMddHHmmss}.tar.gz
# ============================================================

ARCH="${1:-}"
MODE="${2:-release}"
if [ -z "$ARCH" ]; then
    echo "用法: bash package.sh <x86_64|arm64|armv7> [release|debug]"
    exit 1
fi

# 切换到脚本所在目录（build/），保证 ../out/tds 相对路径在任何调用方式下都正确
# （流水线 build@gcc 的 commands 每行是独立 shell，不能依赖调用者先 cd build）
cd "$(dirname "$0")" || { echo "错误: 无法进入脚本目录 $(dirname "$0")"; exit 1; }

# ===================== 1. 获取提交次数（Gitee API） =====================
REV_COUNT="unknown"
if command -v curl >/dev/null 2>&1; then
    # Gitee 公开仓库匿名 API：commits 接口响应头携带 commit_count/total_count
    API_COUNT="$(curl -s -D - -o /dev/null "https://gitee.com/api/v5/repos/liangtuSoft/tds/commits?per_page=1" 2>/dev/null | tr -d '\r' | awk -F': ' '/^[Cc]ommit_count|^[Tt]otal_count/{print $2}' | tail -1)"
    if [ -n "$API_COUNT" ] && echo "$API_COUNT" | grep -qE '^[0-9]+$'; then
        REV_COUNT="$API_COUNT"
        echo "源码版本(rev, Gitee API): $REV_COUNT"
    else
        echo "警告: Gitee API 未返回有效提交数，版本号记为 unknown"
    fi
else
    echo "警告: 未找到 curl，版本号记为 unknown"
fi
echo "源码版本(rev): $REV_COUNT"

# ===================== 2. 定位构建产物 =====================
# 所有架构统一输出到 out/tds/tds（build.sh 已改为统一目录+文件名）
bin_file="../out/tds/tds"
case "$ARCH" in
x86_64|arm64|armv7) ;;
*)
    echo "错误: 未知架构 '$ARCH'，支持 x86_64 / arm64 / armv7"
    exit 1
    ;;
esac

if [ ! -f "$bin_file" ]; then
    echo "错误: 找不到构建产物 $bin_file，请先运行: bash build.sh $ARCH"
    exit 1
fi

# ===================== 3. 打包 =====================
VER="tds-${REV_COUNT}-linux-${ARCH}-$(date +%y%m%d%H%M%S)"
# 支持流水线用构建号覆盖版本号（TDS_VERSION_OVERRIDE），
# 保证产物包名与 release 版本号一致（release@gitee 插件可用 ${GITEE_PIPELINE_BUILD_NUMBER}）
if [ -n "${TDS_VERSION_OVERRIDE:-}" ]; then
    VER="${TDS_VERSION_OVERRIDE}"
    echo "使用覆盖版本号: ${VER}"
fi
TDS_DIR="../out/tds"

# 直接在构建产物目录中打包：不拷贝、不创建 out/dist
tar czf "${TDS_DIR}/${VER}.tar.gz" -C "$TDS_DIR" tds

echo "打包完成: ${TDS_DIR}/${VER}.tar.gz"
ls -lh "${TDS_DIR}/${VER}.tar.gz"

# ===================== 4. 输出版本号供流水线 Release 步骤使用 =====================
# Gitee Go 流水线级变量：build 步骤内写入，同流水线后续步骤可引用
if [ -n "${GITEE_PARAMS:-}" ]; then
    echo "TDS_VERSION=${VER}" >> "$GITEE_PARAMS"
    echo "已写入流水线变量 TDS_VERSION=${VER}"
fi
