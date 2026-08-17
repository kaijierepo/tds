#!/bin/bash
set -e

# ============================================================
# TDS 发布打包脚本
#
# 把 build.sh 构建出的可执行文件打包为:
#   out/dist/tds-v{提交次数}-linux-{架构}/tds
#   out/dist/tds-v{提交次数}-linux-{架构}.tar.gz
#
# 用法:
#   bash package.sh <x86_64|arm64|armv7> [release|debug]
#
# 需先运行 build.sh 完成构建。版本号取自 git 提交次数
# （与 build.sh 生成 version.h 的方法一致）。
#
# 输出:
#   out/dist/tds-v{提交次数}-linux-{架构}.tar.gz
# ============================================================

ARCH="${1:-}"
MODE="${2:-release}"
if [ -z "$ARCH" ]; then
    echo "用法: bash package.sh <x86_64|arm64|armv7> [release|debug]"
    exit 1
fi

# ===================== 1. 获取提交次数（与 build.sh 相同方法） =====================
REV_COUNT="unknown"
if command -v git >/dev/null 2>&1; then
    pushd .. >/dev/null
    # 浅克隆需要解除限制才能统计完整提交数
    if git rev-parse --is-shallow-repository 2>/dev/null | grep -q '^true$'; then
        echo "检测到浅克隆，拉取完整历史以统计提交数..."
        git fetch --unshallow 2>/dev/null || true
    fi
    REV_COUNT="$(git rev-list --count HEAD 2>/dev/null || echo unknown)"
    popd >/dev/null
else
    echo "警告: 未找到 git，版本号记为 unknown"
fi
echo "源码版本(rev): $REV_COUNT"

# ===================== 2. 定位构建产物 =====================
case "$ARCH" in
x86_64)
    bin_file="../out/tds/tds_x86_64_${MODE}"
    ;;
arm64)
    bin_file="../out/tds_arm64"
    ;;
armv7)
    bin_file="../out/tds_armv7"
    ;;
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
VER="tds-v${REV_COUNT}-linux-${ARCH}"
DIST_DIR="../out/dist"
PKG_DIR="${DIST_DIR}/${VER}"

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"
cp -f "$bin_file" "${PKG_DIR}/tds"

tar czf "${DIST_DIR}/${VER}.tar.gz" -C "$DIST_DIR" "$VER"

echo "打包完成: ${DIST_DIR}/${VER}.tar.gz"
echo "发布目录: ${VER}"
ls -lh "${DIST_DIR}/${VER}.tar.gz"

# ===================== 4. 输出版本号供流水线 Release 步骤使用 =====================
# Gitee Go 流水线级变量：build 步骤内写入，同流水线后续步骤可引用
if [ -n "${GITEE_PARAMS:-}" ]; then
    echo "TDS_VERSION=${VER}" >> "$GITEE_PARAMS"
    echo "已写入流水线变量 TDS_VERSION=${VER}"
fi
