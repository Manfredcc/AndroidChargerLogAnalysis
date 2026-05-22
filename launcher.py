"""ChargerLogAnalysis 启动器 — dev & PyInstaller frozen 双模式入口。

用法:
  python launcher.py              # dev 模式
  ChargerLogAnalysis.exe          # 打包后双击运行
"""

import os
import sys
import time
import threading
import webbrowser
from pathlib import Path


def _is_frozen() -> bool:
    return getattr(sys, 'frozen', False)


def _resolve_paths() -> dict:
    if _is_frozen():
        meipass = Path(sys._MEIPASS)
        exe_dir = Path(sys.executable).parent
        return {
            'static': str(meipass / "web"),
            'chargerlog_bin': str(meipass / "chargerlog.exe"),
            'history_dir': str(exe_dir / "history"),
        }
    else:
        project_root = Path(__file__).resolve().parent
        return {
            'static': str(project_root / "web" / "dist"),
            'chargerlog_bin': str(project_root / "core" / "build" / "chargerlog.exe"),
            'history_dir': str(project_root / "server" / "history"),
        }


_HEARTBEAT_SEC = 0.0
_HEARTBEAT_LOCK = threading.Lock()
_HEARTBEAT_TIMEOUT = 120  # 秒，需大于 Chrome 后台标签页 setInterval 节流上限 (~60s)


def _update_heartbeat():
    global _HEARTBEAT_SEC
    with _HEARTBEAT_LOCK:
        _HEARTBEAT_SEC = time.time()


def _heartbeat_monitor():
    """监控前端心跳，超时后自动退出进程。"""
    while True:
        time.sleep(5)
        with _HEARTBEAT_LOCK:
            elapsed = time.time() - _HEARTBEAT_SEC
        if elapsed > _HEARTBEAT_TIMEOUT:
            os._exit(0)


def main():
    paths = _resolve_paths()

    # 通过环境变量注入路径，config.py 优先读取
    os.environ["CHARGERLOG_BIN_PATH"] = paths['chargerlog_bin']
    os.environ["CHARGERLOG_HISTORY_DIR"] = paths['history_dir']

    # 确保 server/ 目录在 sys.path 中 (仅 dev 模式需要，frozen 时模块已展平)
    if not _is_frozen():
        server_dir = str(Path(__file__).resolve().parent / "server")
        if server_dir not in sys.path:
            sys.path.insert(0, server_dir)

    from app import create_app

    static = paths['static']
    app = create_app(static_folder=static if Path(static).exists() else None)
    app.config['_heartbeat_update'] = _update_heartbeat

    # 初始化心跳时间戳
    _update_heartbeat()

    def open_browser():
        webbrowser.open("http://127.0.0.1:5000")

    threading.Timer(1.0, open_browser).start()
    threading.Thread(target=_heartbeat_monitor, daemon=True, name="heartbeat").start()

    if not _is_frozen():
        print("ChargerLogAnalysis")
        print(f"  地址: http://127.0.0.1:5000")
        print("  按 Ctrl+C 退出")

    try:
        app.run(host="127.0.0.1", port=5000, debug=False)
    except KeyboardInterrupt:
        if not _is_frozen():
            print("\n已退出。")


if __name__ == "__main__":
    main()
