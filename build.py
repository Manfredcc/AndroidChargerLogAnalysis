"""ChargerLogAnalysis 打包脚本 — 单文件，跨平台。

用法:
  python build.py                    # 完整构建
  python build.py --clean            # 清理后重新构建
  python build.py --skip-cpp         # 跳过 C++ 编译
  python build.py --skip-web         # 跳过前端构建
"""

import argparse
import shlex
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DIST = ROOT / "dist" / "ChargerLogApp"
CORE_BUILD = ROOT / "core" / "build"
WEB_DIR = ROOT / "web"


def run(cmd: list[str], cwd: Path | None = None, desc: str = "") -> None:
    print(f"  {desc or ' '.join(cmd)}")
    # shell=True 确保 Windows 上能找到 .cmd/.bat 文件（如 npm, cmake）
    # shlex.quote 保护含空格的参数（如 "MinGW Makefiles"）
    subprocess.run(" ".join(shlex.quote(a) for a in cmd), cwd=cwd, check=True, shell=True)


def build_cpp() -> None:
    print("\n[1/3] 编译 C++ chargerlog...")
    CORE_BUILD.mkdir(parents=True, exist_ok=True)

    # 优先 MinGW，不可用或失败时自动回退到平台默认生成器
    for attempt in range(2):
        gen, gen_desc = (["-G", "MinGW Makefiles"], "MinGW") if attempt == 0 else ([], "默认")
        try:
            run(["cmake"] + gen + [".."], cwd=CORE_BUILD, desc=f"cmake 配置 ({gen_desc})")
            run(["cmake", "--build", ".", "--config", "Release"], cwd=CORE_BUILD, desc="cmake 构建")
            print("  -> chargerlog.exe 编译完成")
            return
        except subprocess.CalledProcessError:
            # 清空整个 build 目录后重试下一个生成器
            print(f"  {gen_desc} 构建失败，清理重试...")
            for p in CORE_BUILD.iterdir():
                if p.is_dir():
                    shutil.rmtree(p, ignore_errors=True)
                else:
                    p.unlink()
            if attempt == 0:
                print(f"  回退到平台默认生成器...")
                continue
            sys.exit("ERROR: C++ 编译失败")


def build_web() -> None:
    print("\n[2/3] 构建 Vue 前端...")
    if not (WEB_DIR / "node_modules").exists():
        run(["npm", "install"], cwd=WEB_DIR, desc="npm install")
    run(["npm", "run", "build"], cwd=WEB_DIR, desc="npm run build")
    print("  -> web/dist/ 构建完成")


def _find_chargerlog_exe() -> Path | None:
    """查找 chargerlog.exe — MinGW 输出到 build/，MSVC 输出到 build/Release/。"""
    for sub in ["", "Release", "Debug"]:
        p = CORE_BUILD / sub / "chargerlog.exe" if sub else CORE_BUILD / "chargerlog.exe"
        if p.exists():
            return p
    return None


def build_pyinstaller() -> None:
    print("\n[3/3] PyInstaller 打包...")

    web_dist = ROOT / "web" / "dist"
    chargerlog_exe = _find_chargerlog_exe()

    if not web_dist.exists():
        sys.exit("ERROR: web/dist/ 不存在，请先构建前端或去掉 --skip-web")
    if chargerlog_exe is None:
        sys.exit("ERROR: chargerlog.exe 不存在，请先编译 C++ 或去掉 --skip-cpp")

    run(["pyinstaller", "-y", str(ROOT / "chargerlog.spec")], cwd=ROOT, desc="pyinstaller")

    # 清理中间 EXE
    stray = ROOT / "dist" / "ChargerLogApp.exe"
    if stray.exists():
        stray.unlink()

    print(f"\n{'=' * 44}")
    print(f"  打包完成")
    print(f"  输出: {DIST}")
    print(f"  可执行文件: {DIST / 'ChargerLogApp.exe'}")
    print(f"{'=' * 44}")


def zip_dist() -> None:
    print("\n[压缩] 生成 ChargerLogApp.zip...")
    zip_path = DIST.parent / "ChargerLogApp.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in DIST.rglob("*"):
            arcname = f.relative_to(DIST.parent)
            zf.write(f, arcname)
    size_mb = zip_path.stat().st_size / (1024 * 1024)
    print(f"  -> {zip_path}  ({size_mb:.1f} MB)")


def main() -> None:
    parser = argparse.ArgumentParser(description="ChargerLogAnalysis 打包脚本")
    parser.add_argument("--clean", action="store_true", help="清理旧构建产物")
    parser.add_argument("--skip-cpp", action="store_true", help="跳过 C++ 编译")
    parser.add_argument("--skip-web", action="store_true", help="跳过前端构建")
    args = parser.parse_args()

    if args.clean:
        print("[清理] 删除旧构建产物...")
        for p in [DIST, ROOT / "build", WEB_DIR / "dist"]:
            if p.exists():
                shutil.rmtree(p, ignore_errors=True)
        stray = ROOT / "dist" / "ChargerLogApp.exe"
        if stray.exists():
            stray.unlink()

    if not args.skip_cpp:
        build_cpp()
    else:
        print("\n[跳过] C++ 编译")

    if not args.skip_web:
        build_web()
    else:
        print("\n[跳过] 前端构建")

    build_pyinstaller()
    zip_dist()


if __name__ == "__main__":
    main()
