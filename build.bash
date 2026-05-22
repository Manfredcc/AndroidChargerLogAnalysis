#!/usr/bin/env bash
# Build script for ChargerLogAnalysis Windows 打包
# 用法:
#   ./build.bash              # 完整构建
#   ./build.bash --clean      # 清理后重新构建
#   ./build.bash --skip-web   # 跳过前端构建
#   ./build.bash --skip-cpp   # 跳过 C++ 编译

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DIST="$ROOT/dist/ChargerLogApp"

CLEAN=false
SKIP_WEB=false
SKIP_CPP=false

for arg in "$@"; do
    case $arg in
        --clean)   CLEAN=true ;;
        --skip-web) SKIP_WEB=true ;;
        --skip-cpp) SKIP_CPP=true ;;
        *) echo "未知参数: $arg"; exit 1 ;;
    esac
done

# ── 查找缺失的工具 ────────────────────────────────────
_find_tool() {
    # 在 PATH 和常见位置查找工具
    local name="$1"
    # 常见 Windows 工具路径（Git Bash 下 E: 等盘符映射为 /e/）
    local extra_paths=(
        "/c/Program Files/CMake/bin"
        "/e/02Resource/App/softwares/cmake/bin"
        "/c/Program Files/nodejs"
        "/c/Users/$USER/AppData/Local/Programs/Python/Python312/Scripts"
        "/c/Users/$USER/AppData/Local/Programs/Python/Python312"
    )
    # 先搜 PATH
    local found
    found="$(command -v "$name" 2>/dev/null || true)"
    if [ -n "$found" ]; then
        echo "$found"
        return 0
    fi
    # 再搜常见路径
    for d in "${extra_paths[@]}"; do
        if [ -x "$d/$name" ] || [ -x "$d/$name.exe" ]; then
            echo "$d/$name"
            return 0
        fi
    done
    return 1
}

_ensure_tool() {
    local name="$1" install_hint="$2"
    local path
    path="$(_find_tool "$name" || true)"
    if [ -z "$path" ]; then
        echo "  ERROR: 找不到 $name，请安装后重试。"
        echo "         $install_hint"
        exit 1
    fi
    # 如果工具所在目录尚未添加到 PATH，临时加入
    local dir
    dir="$(dirname "$path")"
    case ":$PATH:" in
        *":$dir:"*) ;;
        *) export PATH="$dir:$PATH" ;;
    esac
}

echo "============================================"
echo "  ChargerLogAnalysis 打包脚本"
echo "============================================"

# 预检关键工具
if ! $SKIP_CPP; then
    _ensure_tool cmake "https://cmake.org/download/"
fi
if ! $SKIP_WEB; then
    _ensure_tool npm "https://nodejs.org/"
fi
_ensure_tool pyinstaller "pip install pyinstaller"

# ── 清理 ────────────────────────────────────────────
if $CLEAN; then
    echo ""
    echo "[1/3] 清理旧构建产物..."
    rm -rf "$DIST" "$ROOT/build" "$ROOT/web/dist"
fi

# ── C++ 编译 ────────────────────────────────────────
if $SKIP_CPP; then
    echo ""
    echo "[跳过] C++ 编译"
else
    echo ""
    echo "[1/3] 编译 C++ chargerlog..."

    BUILD_DIR="$ROOT/core/build"
    mkdir -p "$BUILD_DIR"

    cd "$BUILD_DIR"
    if [ ! -f CMakeCache.txt ]; then
        cmake -G "Unix Makefiles" ..
    fi
    cmake --build . --config Release
    echo "  -> chargerlog.exe 编译完成"
fi

# ── 前端构建 ────────────────────────────────────────
if $SKIP_WEB; then
    echo ""
    echo "[跳过] 前端构建"
else
    echo ""
    echo "[2/3] 构建 Vue 前端..."

    cd "$ROOT/web"
    if [ ! -d node_modules ]; then
        echo "  安装前端依赖..."
        npm install
    fi
    npm run build
    echo "  -> web/dist/ 构建完成"
fi

# ── PyInstaller 打包 ────────────────────────────────
echo ""
echo "[3/3] PyInstaller 打包..."

cd "$ROOT"

# 检查前置文件
WEB_DIST="$ROOT/web/dist"
CHARGERLOG_EXE="$ROOT/core/build/chargerlog.exe"

if [ ! -d "$WEB_DIST" ]; then
    echo "  ERROR: web/dist/ 不存在，请先构建前端或去掉 --skip-web"
    exit 1
fi
if [ ! -f "$CHARGERLOG_EXE" ]; then
    echo "  ERROR: chargerlog.exe 不存在，请先编译 C++ 或去掉 --skip-cpp"
    exit 1
fi

pyinstaller chargerlog.spec

# 清理 COLLECT 残留的中间 EXE（与目录同名但无依赖）
rm -f "$ROOT/dist/ChargerLogApp.exe"

echo ""
echo "============================================"
echo "  打包完成"
echo "  输出: $DIST"
echo "  可执行文件: $DIST/ChargerLogAnalysis.exe"
echo "============================================"

# 显示目录结构概览
echo ""
echo "目录结构:"
find "$DIST" -maxdepth 2 -not -path "*/_internal/*" -not -path "*/tcl*" \
    | sed "s|$DIST| ChargerLogAnalysis|" | sort
