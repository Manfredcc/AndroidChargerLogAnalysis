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
  3. 解压备份中的归档文件到该目录
  4. 递归查找子目录中的 .gz 文件，逐个解压并删掉原始 .gz
  5. 顶层备份归档保留不动
"""

import os
import shutil
import gzip
import zipfile
import tarfile
from pathlib import Path


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


def decompress_archive(archive_path: str) -> str:
    """
    解压日志归档，递归处理嵌套 .gz 文件。

    Args:
        archive_path: 归档文件路径 (.zip / .tar.gz / .tar.bz2 / .tar.xz)

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

    # 提前检查格式
    fname_lower = archive.name.lower()
    SUPPORTED_ZIP = ".zip"
    SUPPORTED_TAR = (".tar.gz", ".tar.bz2", ".tar.xz", ".tgz", ".tar")
    if not (fname_lower.endswith(SUPPORTED_ZIP) or fname_lower.endswith(SUPPORTED_TAR)):
        raise ValueError(f"不支持的归档格式: {archive.name} (支持: .zip, .tar.gz, .tar.bz2, .tar.xz)")

    # ── 1. 创建同名目录 ────────────────────────────────────
    out_dir = archive.parent / strip_archive_ext(archive.name)
    out_dir.mkdir(exist_ok=True)

    # ── 2. 移动归档到目录中作为备份 ──────────────────────────
    backup_path = out_dir / archive.name
    shutil.move(str(archive), str(backup_path))

    # ── 3. 解压归档内容 (从备份路径读取) ────────────────────
    try:
        if fname_lower.endswith(".zip"):
            _extract_zip(backup_path, out_dir)
        else:
            _extract_tar(backup_path, out_dir)
    except Exception as e:
        if out_dir.exists():
            shutil.rmtree(str(out_dir))
        raise RuntimeError(f"解压失败: {e}") from e

    # ── 4. 递归解压嵌套 .gz (排除顶层备份) ─────────────────
    for gz_path in out_dir.rglob("*.gz"):
        if gz_path == backup_path:
            continue  # 保留顶层备份
        _decompress_gz_in_place(gz_path)

    return str(out_dir)


def _extract_zip(archive: Path, out_dir: Path) -> None:
    """安全解压 .zip 文件。"""
    with zipfile.ZipFile(str(archive), "r") as zf:
        for name in zf.namelist():
            if not _is_safe_path(out_dir, name):
                raise RuntimeError(f"路径穿越检测: {name}")
        zf.extractall(str(out_dir))


def _extract_tar(archive: Path, out_dir: Path) -> None:
    """安全解压 .tar.* 文件。"""
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
        for member in tf.getmembers():
            if not _is_safe_path(out_dir, member.name):
                raise RuntimeError(f"路径穿越检测: {member.name}")
        tf.extractall(str(out_dir))


def _decompress_gz_in_place(gz_path: Path) -> None:
    """解压单个 .gz 文件输出到同路径 (去掉 .gz 后缀)，然后删除原 .gz。"""
    out_path = gz_path.with_suffix("")  # xx1.gz → xx1

    # 如果目标文件已存在，在后面加序号避免覆盖
    if out_path.exists():
        stem = gz_path.stem  # 去掉 .gz 后的文件名
        counter = 1
        while True:
            candidate = gz_path.with_name(f"{stem}.{counter}")
            if not candidate.exists():
                out_path = candidate
                break
            counter += 1

    try:
        with gzip.open(str(gz_path), "rb") as f_in:
            with open(str(out_path), "wb") as f_out:
                shutil.copyfileobj(f_in, f_out)
        gz_path.unlink()  # 删除原始 .gz
    except Exception as e:
        # 解压失败时保持原样，不删除
        raise RuntimeError(f"解压失败 {gz_path}: {e}") from e


__all__ = ["decompress_archive", "strip_archive_ext"]


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 2:
        print("用法: python decompressor.py <归档文件路径>")
        print("示例: python decompressor.py D:\\Logs\\chargerLog设备1.zip")
        sys.exit(1)

    archive_path = sys.argv[1]
    try:
        out_dir = decompress_archive(archive_path)
        print(f"解压完成: {out_dir}")
    except Exception as e:
        print(f"解压失败: {e}", file=sys.stderr)
        sys.exit(1)
