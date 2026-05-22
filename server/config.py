"""集中式路径解析：支持 dev 模式和 PyInstaller frozen 模式。

环境变量可覆盖自动检测的路径，优先级最高：
  CHARGERLOG_BIN_PATH   — chargerlog.exe 的路径
  CHARGERLOG_HISTORY_DIR — history 数据目录
"""

import os
import sys
from pathlib import Path


def _is_frozen() -> bool:
    return getattr(sys, 'frozen', False)


def get_chargerlog_bin() -> Path:
    env = os.environ.get("CHARGERLOG_BIN_PATH")
    if env:
        return Path(env)

    if _is_frozen():
        p = Path(sys._MEIPASS) / "chargerlog.exe"
        if p.exists():
            return p
        return p.with_suffix(".exe")

    # Dev mode
    project_root = Path(__file__).resolve().parent.parent
    p = project_root / "core" / "build" / "chargerlog"
    if not p.exists():
        p = p.with_suffix(".exe")
    return p


def get_history_dir() -> Path:
    env = os.environ.get("CHARGERLOG_HISTORY_DIR")
    if env:
        return Path(env)

    if _is_frozen():
        return Path(sys.executable).parent / "history"

    # Dev mode
    return Path(__file__).resolve().parent / "history"


def get_static_dir() -> Path | None:
    if _is_frozen():
        p = Path(sys._MEIPASS) / "web"
        return p if p.exists() else None

    # Dev mode
    p = Path(__file__).resolve().parent.parent / "web" / "dist"
    return p if p.exists() else None
