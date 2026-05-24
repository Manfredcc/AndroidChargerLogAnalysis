# -*- mode: python ; coding: utf-8 -*-

from pathlib import Path

_block_cipher = None

ROOT = Path(SPECPATH)

# ── 前端静态文件 ────────────────────────────────────────
datas = []
web_dist = ROOT / "web" / "dist"
if web_dist.exists():
    datas.append((str(web_dist), "web"))

# ── C++ 可执行文件 ──────────────────────────────────────
binaries = []
for sub in ["", "Release", "Debug"]:
    chargerlog_exe = ROOT / "core" / "build" / sub / "chargerlog.exe" if sub else ROOT / "core" / "build" / "chargerlog.exe"
    if chargerlog_exe.exists():
        binaries.append((str(chargerlog_exe), "."))
        break

a = Analysis(
    [str(ROOT / "launcher.py")],
    pathex=[str(ROOT / "server")],
    binaries=binaries,
    datas=datas,
    hiddenimports=[
        "flask",
        "flask_cors",
        "tkinter",
        "tkinter.filedialog",
        "app",
        "config",
        "routes",
        "routes.upload",
        "routes.analysis",
        "routes.history",
        "utils",
        "utils.cpp_bridge",
        "utils.decompressor",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="ChargerLogApp",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="ChargerLogApp",
)
