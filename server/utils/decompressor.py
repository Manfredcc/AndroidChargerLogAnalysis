"""
日志归档解压工具。

处理典型的安卓日志归档结构：
  log.tar.gz (或 .zip)
  ├── aplog/
  │   ├── xx1.gz        ← 自动解压为 xx1，删除原始 .gz
  │   └── xx2.gz
  └── klog/
      ├── xx1.gz
      └── xx2.gz

流程：
  1. 创建同名目录 (log.tar.gz → log/)
  2. 将归档文件移动到目录中作为备份
  3. 解压备份中的归档文件到该目录 (单次遍历 + 路径穿越检测)
  4. 并行解压子目录中的 .gz 文件，删掉原始 .gz
  5. 顶层备份归档保留不动
"""

import concurrent.futures
import os
import shutil
import gzip
import time
import zipfile
import tarfile
from pathlib import Path

# gzip 解压缓冲区: 2MB (SSD 上比默认 1MB 更高效)
_GZ_BUFFER_SIZE = 2 * 1024 * 1024

_ARCHIVE_EXTENSIONS = [
    ".tar.gz",
    ".tar.bz2",
    ".tar.xz",
    ".tgz",
    ".zip",
    ".tar",
]


def strip_archive_ext(filename: str) -> str:
    """去除常见归档扩展名，返回目录名。"""
    name = filename
    # .tar.gz 必须在 .gz 之前匹配
    for ext in _ARCHIVE_EXTENSIONS:
        if name.lower().endswith(ext):
            return name[: -len(ext)]
    if name.lower().endswith(".gz"):
        return name[:-3]
    return name


def _is_safe_path(dest: Path, member_path: str) -> bool:
    """防止 zip/tar 路径穿越 (Zip Slip 漏洞)。"""
    resolved = (dest / member_path).resolve()
    return str(resolved).startswith(str(dest.resolve()))


def decompress_archive(archive_path: str, verbose: bool = False) -> str:
    """
    解压日志归档，递归处理嵌套 .gz 文件。

    Args:
        archive_path: 归档文件路径 (.zip / .tar.gz / .tar.bz2 / .tar.xz)
        verbose: 是否打印耗时信息，默认关闭

    Returns:
        解压后目录的路径

    Raises:
        FileNotFoundError: 归档文件不存在
        ValueError: 不支持的归档格式
        RuntimeError: 解压失败
    """
    archive = Path(archive_path).resolve()
    if not archive.exists():
        raise FileNotFoundError(f"归档文件不存在: {archive_path}")

    fname_lower = archive.name.lower()
    SUPPORTED_ZIP = ".zip"
    SUPPORTED_TAR = (".tar.gz", ".tar.bz2", ".tar.xz", ".tgz", ".tar")
    if not (fname_lower.endswith(SUPPORTED_ZIP) or fname_lower.endswith(SUPPORTED_TAR)):
        raise ValueError(f"不支持的归档格式: {archive.name} (支持: .zip, .tar.gz, .tar.bz2, .tar.xz)")

    t0 = time.perf_counter() if verbose else 0

    out_dir = archive.parent / strip_archive_ext(archive.name)
    out_dir.mkdir(exist_ok=True)

    backup_path = out_dir / archive.name
    shutil.move(str(archive), str(backup_path))

    try:
        if fname_lower.endswith(".zip"):
            _extract_zip(backup_path, out_dir)
        else:
            _extract_tar(backup_path, out_dir)
    except Exception as e:
        if out_dir.exists():
            shutil.rmtree(str(out_dir))
        raise RuntimeError(f"解压失败: {e}") from e

    t1 = time.perf_counter() if verbose else 0
    if verbose:
        print(f"  [解压] 顶层归档: {t1 - t0:.2f}s")

    gz_list = [
        p for p in out_dir.rglob("*.gz")
        if p != backup_path
    ]

    if not gz_list:
        if verbose:
            print(f"  [解压] 总耗时: {time.perf_counter() - t0:.2f}s")
        return str(out_dir)

    max_workers = min(os.cpu_count() or 4, len(gz_list), 8)
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as pool:
        futures = {pool.submit(_decompress_gz_in_place, p): p for p in gz_list}
        for fut in concurrent.futures.as_completed(futures):
            exc = fut.exception()
            if exc is not None:
                for remaining in futures:
                    remaining.cancel()
                raise RuntimeError(f"解压失败 {futures[fut]}: {exc}") from exc

    if verbose:
        t2 = time.perf_counter()
        print(f"  [解压] 嵌套 .gz ({len(gz_list)} 个, {max_workers} 线程): {t2 - t1:.2f}s")
        print(f"  [解压] 总耗时: {t2 - t0:.2f}s")

    return str(out_dir)


def _extract_zip(archive: Path, out_dir: Path) -> None:
    """单次遍历解压 .zip，同时做路径穿越检测。"""
    with zipfile.ZipFile(str(archive), "r") as zf:
        for name in zf.namelist():
            if not _is_safe_path(out_dir, name):
                raise RuntimeError(f"路径穿越检测: {name}")
            zf.extract(name, str(out_dir))


def _extract_tar(archive: Path, out_dir: Path) -> None:
    """单次遍历解压 .tar.*，同时做路径穿越检测。"""
    mode_map = {
        ".tar.gz": "r:gz",
        ".tgz": "r:gz",
        ".tar.bz2": "r:bz2",
        ".tar.xz": "r:xz",
        ".tar": "r:",
    }
    mode_key = next((ext for ext in mode_map if archive.name.lower().endswith(ext)), "")
    mode = mode_map.get(mode_key, "r:*")

    with tarfile.open(str(archive), mode) as tf:
        for member in tf:
            if not _is_safe_path(out_dir, member.name):
                raise RuntimeError(f"路径穿越检测: {member.name}")
            tf.extract(member, str(out_dir))


def _decompress_gz_in_place(gz_path: Path) -> None:
    """解压单个 .gz 文件输出到同路径 (去掉 .gz 后缀)，然后删除原 .gz。"""
    out_path = gz_path.with_suffix("")

    if out_path.exists():
        stem = gz_path.stem
        counter = 1
        while True:
            candidate = gz_path.with_name(f"{stem}.{counter}")
            if not candidate.exists():
                out_path = candidate
                break
            counter += 1

    with gzip.open(str(gz_path), "rb") as f_in:
        with open(str(out_path), "wb") as f_out:
            shutil.copyfileobj(f_in, f_out, length=_GZ_BUFFER_SIZE)
    gz_path.unlink()


__all__ = ["decompress_archive", "strip_archive_ext"]


if __name__ == "__main__":
    import sys

    args = sys.argv[1:]
    verbose = "--verbose" in args or "-v" in args
    paths = [a for a in args if a not in ("--verbose", "-v")]

    if len(paths) != 1:
        print("用法: python decompressor.py [--verbose] <归档文件路径>")
        print("示例: python decompressor.py D:\\Logs\\chargerLog设备1.zip")
        sys.exit(1)

    archive_path = paths[0]
    try:
        out_dir = decompress_archive(archive_path, verbose=verbose)
        print(f"解压完成: {out_dir}")
    except Exception as e:
        print(f"解压失败: {e}", file=sys.stderr)
        sys.exit(1)
