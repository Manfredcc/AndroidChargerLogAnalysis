"""
解压器测试。

创建模拟的安卓日志结构：
  test_logs.tar.gz
  ├── aplog/
  │   ├── events.gz
  │   └── main.gz
  └── klog/
      └── kernel.gz

然后验证 decompress_archive 能正确解压。
"""

import sys
import shutil
import gzip
import zipfile
import tarfile
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))

from utils.decompressor import decompress_archive, strip_archive_ext


def _make_gz_file(path: str, content: str = "") -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with gzip.open(path, "wt", encoding="utf-8") as f:
        f.write(content)


def test_basic_zip():
    """测试 .zip 归档解压。"""
    tmpdir = Path(tempfile.mkdtemp())
    try:
        zip_path = tmpdir / "logs.zip"
        gz_file = "aplog/events.gz"
        _make_gz_file(str(tmpdir / gz_file), "event log content")

        with zipfile.ZipFile(str(zip_path), "w") as zf:
            zf.write(str(tmpdir / gz_file), arcname=gz_file)

        out = decompress_archive(str(zip_path))
        out_dir = Path(out)

        assert out_dir.name == "logs"
        assert (out_dir / "logs.zip").exists(), "备份应存在"
        assert not (out_dir / "aplog/events.gz").exists(), ".gz 应被删除"
        assert (out_dir / "aplog/events").exists()
        assert (out_dir / "aplog/events").read_text(encoding="utf-8") == "event log content"

        print("[PASS] test_basic_zip")
    finally:
        shutil.rmtree(str(tmpdir))


def test_tar_gz_with_nested_gz():
    """测试 .tar.gz 中嵌套 .gz 的解压。"""
    tmpdir = Path(tempfile.mkdtemp())
    try:
        events_content = "events log line 1\nevents log line 2"
        main_content = "main log line 1"
        kernel_content = "kernel log line 1"

        _make_gz_file(str(tmpdir / "aplog/events.gz"), events_content)
        _make_gz_file(str(tmpdir / "aplog/main.gz"), main_content)
        _make_gz_file(str(tmpdir / "klog/kernel.gz"), kernel_content)

        archive_path = tmpdir / "test_logs.tar.gz"
        with tarfile.open(str(archive_path), "w:gz") as tar:
            tar.add(str(tmpdir / "aplog"), arcname="aplog")
            tar.add(str(tmpdir / "klog"), arcname="klog")

        out = decompress_archive(str(archive_path))
        out_dir = Path(out)

        assert out_dir.name == "test_logs"
        assert (out_dir / "test_logs.tar.gz").exists(), "备份应存在"
        assert not (out_dir / "aplog/events.gz").exists()
        assert (out_dir / "aplog/events").exists()
        assert (out_dir / "aplog/events").read_text(encoding="utf-8") == events_content
        assert (out_dir / "aplog/main").exists()
        assert (out_dir / "klog/kernel").exists()

        print("[PASS] test_tar_gz_with_nested_gz")
    finally:
        shutil.rmtree(str(tmpdir))


def test_strip_archive_ext():
    cases = [
        ("log.tar.gz", "log"),
        ("log.tgz", "log"),
        ("log.tar.bz2", "log"),
        ("log.tar.xz", "log"),
        ("log.zip", "log"),
        ("log.tar", "log"),
        ("file.txt.gz", "file.txt"),
        ("no_ext", "no_ext"),
        ("a.b.c.tar.gz", "a.b.c"),
    ]
    for input_name, expected in cases:
        result = strip_archive_ext(input_name)
        assert result == expected, f"strip_archive_ext({input_name}) = {result}, 期望 {expected}"
    print("[PASS] test_strip_archive_ext")


def test_nonexistent_archive():
    try:
        decompress_archive("/nonexistent/path/file.zip")
        assert False
    except FileNotFoundError:
        print("[PASS] test_nonexistent_archive")


def test_unsupported_format():
    tmpdir = Path(tempfile.mkdtemp())
    try:
        fake = tmpdir / "test.7z"
        fake.write_text("fake", encoding="utf-8")
        try:
            decompress_archive(str(fake))
            assert False
        except ValueError as e:
            assert "不支持的归档格式" in str(e)
        print("[PASS] test_unsupported_format")
    finally:
        shutil.rmtree(str(tmpdir))


def test_nested_zip_inside_tar_gz():
    tmpdir = Path(tempfile.mkdtemp())
    try:
        nested_zip = tmpdir / "sub/extra.zip"
        nested_zip.parent.mkdir(parents=True)
        with zipfile.ZipFile(str(nested_zip), "w") as zf:
            zf.writestr("readme.txt", "extra data")

        archive_path = tmpdir / "hybrid.tar.gz"
        with tarfile.open(str(archive_path), "w:gz") as tar:
            tar.add(str(tmpdir / "sub"), arcname="sub")

        out = decompress_archive(str(archive_path))
        out_dir = Path(out)

        assert (out_dir / "sub/extra.zip").exists(), ".zip 应保留"
        print("[PASS] test_nested_zip_inside_tar_gz")
    finally:
        shutil.rmtree(str(tmpdir))


if __name__ == "__main__":
    test_strip_archive_ext()
    test_nonexistent_archive()
    test_unsupported_format()
    test_basic_zip()
    test_tar_gz_with_nested_gz()
    test_nested_zip_inside_tar_gz()
    print("\n全部测试通过!")
