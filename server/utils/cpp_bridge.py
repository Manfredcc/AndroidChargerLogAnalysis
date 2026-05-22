"""
C++ chargerlog 可执行文件的 Python 封装。

通过 subprocess 调用 chargerlog CLI，传递 JSON 参数并解析返回结果。
"""

import json
import subprocess
import sys
from typing import Optional, Any

from config import get_chargerlog_bin

_CHARGERLOG_BIN = get_chargerlog_bin()


class ChargerLogError(RuntimeError):
    """chargerlog 执行失败异常。"""
    def __init__(self, message: str, stderr: str = ""):
        super().__init__(message)
        self.stderr = stderr


def run_chargerlog(
    log_dir: str,
    start: Optional[str] = None,
    end: Optional[str] = None,
    no_cache: bool = False,
    points: bool = False,
    downsample: int = 0,
    timeout: int = 120,
) -> dict[str, Any]:
    """调用 chargerlog 可执行文件解析日志目录。

    Args:
        log_dir: 日志目录路径
        start: 起始时间 HH:MM:SS (可选)
        end: 结束时间 HH:MM:SS (可选)
        no_cache: 是否跳过缓存强制重新解析
        points: 是否输出原始数据点 (用于时序图表)
        downsample: 降采样目标点数 (0=不降采样)
        timeout: 子进程超时秒数

    Returns:
        解析后的 JSON 结果

    Raises:
        FileNotFoundError: chargerlog 可执行文件不存在
        ChargerLogError: chargerlog 执行失败
    """
    if not _CHARGERLOG_BIN.exists():
        raise FileNotFoundError(
            f"chargerlog 可执行文件不存在: {_CHARGERLOG_BIN}\n"
            f"请先编译 C++ 项目: cmake --build build/"
        )

    args = [str(_CHARGERLOG_BIN), "--json"]
    if points:
        args.append("--points")
        if downsample > 0:
            args.extend(["--downsample", str(downsample)])
    if no_cache:
        args.append("--no-cache")
    if start:
        args.extend(["--start", start])
    if end:
        args.extend(["--end", end])
    args.append(log_dir)

    try:
        result = subprocess.run(
            args,
            capture_output=True,
            text=True,
            timeout=timeout,
            encoding="utf-8",
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
        )
    except subprocess.TimeoutExpired:
        raise ChargerLogError(f"chargerlog 执行超时 ({timeout}s)")

    if result.returncode != 0:
        raise ChargerLogError(
            f"chargerlog 返回错误码 {result.returncode}",
            stderr=result.stderr,
        )

    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as e:
        raise ChargerLogError(
            f"chargerlog 输出解析失败: {e}",
            stderr=result.stdout[:500],
        )
