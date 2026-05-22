"""POST /api/upload — 上传并分析日志目录。"""

import json
import os
import uuid
import tkinter as tk
from datetime import datetime, timezone
from pathlib import Path
from tkinter import filedialog

from flask import Blueprint, request, jsonify, current_app

from config import get_history_dir
from utils.cpp_bridge import run_chargerlog, ChargerLogError

upload_bp = Blueprint("upload", __name__)

_ARCHIVE_EXTS = (".zip", ".tar.gz", ".tar.bz2", ".tar.xz", ".tgz", ".tar")


def _get_history_dir() -> Path:
    """每次调用时动态获取 history 目录，确保 frozen/dev 模式路径正确。"""
    return get_history_dir()


def _ensure_history_dir() -> None:
    _get_history_dir().mkdir(parents=True, exist_ok=True)


def _save_analysis(analysis_id: str, data: dict) -> None:
    _ensure_history_dir()
    filepath = _get_history_dir() / f"{analysis_id}.json"
    with open(filepath, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def _load_analysis(analysis_id: str) -> dict | None:
    filepath = _get_history_dir() / f"{analysis_id}.json"
    if not filepath.exists():
        return None
    with open(filepath, "r", encoding="utf-8") as f:
        return json.load(f)


def _list_analyses(page: int = 1, limit: int = 20) -> list[dict]:
    _ensure_history_dir()
    files = sorted(_get_history_dir().glob("*.json"), key=os.path.getmtime, reverse=True)
    total = len(files)
    start = (page - 1) * limit
    end = start + limit

    results = []
    for fp in files[start:end]:
        with open(fp, "r", encoding="utf-8") as f:
            record = json.load(f)
        results.append({
            "id": record.get("id"),
            "log_dir": record.get("log_dir"),
            "created_at": record.get("created_at"),
            "points_count": record.get("points_count"),
            "cached": record.get("cached"),
        })
    return results, total


@upload_bp.route("/api/select-path", methods=["POST"])
def select_path():
    """打开原生文件/目录选择对话框，返回选中路径。

    Request body (JSON):
        { "type": "file" | "directory" }   // 默认 "file"
    Response:
        { "path": "/chosen/path" }          // 取消时为空字符串
    """
    data = request.get_json(silent=True) or {}
    mode = data.get("type", "file")

    try:
        root = tk.Tk()
        root.withdraw()
        root.attributes("-topmost", True)

        if mode == "directory":
            path = filedialog.askdirectory(title="选择日志目录")
        else:
            path = filedialog.askopenfilename(
                title="选择日志文件或压缩包",
                filetypes=[
                    ("日志归档", "*.zip;*.tar.gz;*.tar.bz2;*.tar.xz;*.tgz;*.tar"),
                    ("所有文件", "*.*"),
                ],
            )
        root.destroy()
        return jsonify({"path": path or ""})
    except Exception as e:
        return jsonify({"error": f"无法打开文件对话框: {e}\n请确认服务器在有桌面会话的环境中运行"}), 500


def _is_archive_path(path: str) -> bool:
    """检查路径是否是可识别的压缩文件扩展名。"""
    return path.lower().endswith(_ARCHIVE_EXTS)


@upload_bp.route("/api/upload", methods=["POST"])
def upload():
    """分析日志目录或压缩包。

    Request body (JSON):
        {
            "log_dir": "/path/to/log/directory 或 /path/to/archive.zip",
            "no_cache": false           // optional
        }

    Response:
        {
            "id": "uuid",
            "log_dir": "/original/path",
            "points_count": 1234,
            "cached": false,
            "fields": [...],
            "points": [...]
        }
    """
    data = request.get_json(silent=True)
    if not data or "log_dir" not in data:
        return jsonify({"error": "缺少 log_dir 参数"}), 400

    original_path = data["log_dir"].strip()
    if not original_path:
        return jsonify({"error": "log_dir 不能为空"}), 400

    # 判断是目录还是压缩文件
    log_path = Path(original_path)
    if not log_path.exists():
        return jsonify({"error": f"路径不存在: {original_path}"}), 400

    if log_path.is_dir():
        actual_log_dir = str(log_path)
    elif log_path.is_file() and _is_archive_path(original_path):
        from utils.decompressor import decompress_archive
        try:
            actual_log_dir = decompress_archive(original_path)
        except (ValueError, RuntimeError) as e:
            return jsonify({"error": f"解压失败: {e}"}), 400
    else:
        return jsonify({
            "error": f"路径不是有效目录或支持的压缩包: {original_path}\n"
                     f"支持的压缩格式: .zip, .tar.gz, .tar.bz2, .tar.xz, .tgz, .tar"
        }), 400

    analysis_id = str(uuid.uuid4())[:8]
    no_cache = data.get("no_cache", False)

    try:
        result = run_chargerlog(
            log_dir=actual_log_dir,
            start=None,
            end=None,
            no_cache=no_cache,
            points=True,
            downsample=500,
        )
    except FileNotFoundError as e:
        return jsonify({"error": str(e)}), 503
    except ChargerLogError as e:
        return jsonify({"error": str(e), "stderr": e.stderr}), 500

    record = {
        "id": analysis_id,
        "log_dir": actual_log_dir,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "start": None,
        "end": None,
        "points_count": result.get("points_count", 0),
        "cached": result.get("cached", False),
        "fields": result.get("fields", []),
        "points": result.get("points", []),
    }
    _save_analysis(analysis_id, record)

    return jsonify(record), 200
