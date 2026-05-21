"""POST /api/upload — 上传并分析日志目录。"""

import json
import os
import uuid
from datetime import datetime, timezone
from pathlib import Path

from flask import Blueprint, request, jsonify, current_app

from utils.cpp_bridge import run_chargerlog, ChargerLogError

upload_bp = Blueprint("upload", __name__)

HISTORY_DIR = Path(__file__).resolve().parent.parent / "history"


def _ensure_history_dir() -> None:
    HISTORY_DIR.mkdir(parents=True, exist_ok=True)


def _save_analysis(analysis_id: str, data: dict) -> None:
    _ensure_history_dir()
    filepath = HISTORY_DIR / f"{analysis_id}.json"
    with open(filepath, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def _load_analysis(analysis_id: str) -> dict | None:
    filepath = HISTORY_DIR / f"{analysis_id}.json"
    if not filepath.exists():
        return None
    with open(filepath, "r", encoding="utf-8") as f:
        return json.load(f)


def _list_analyses(page: int = 1, limit: int = 20) -> list[dict]:
    _ensure_history_dir()
    files = sorted(HISTORY_DIR.glob("*.json"), key=os.path.getmtime, reverse=True)
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


@upload_bp.route("/api/upload", methods=["POST"])
def upload():
    """分析日志目录。

    Request body (JSON):
        {
            "log_dir": "/path/to/log/directory",
            "start": "HH:MM:SS",        // optional
            "end": "HH:MM:SS",          // optional
            "no_cache": false           // optional
        }

    Response:
        {
            "analysis_id": "uuid",
            "points_count": 1234,
            "cached": false,
            "fields": [...]
        }
    """
    data = request.get_json(silent=True)
    if not data or "log_dir" not in data:
        return jsonify({"error": "缺少 log_dir 参数"}), 400

    log_dir = data["log_dir"]
    if not os.path.isdir(log_dir):
        return jsonify({"error": f"目录不存在: {log_dir}"}), 400

    analysis_id = str(uuid.uuid4())[:8]
    start = data.get("start")
    end = data.get("end")
    no_cache = data.get("no_cache", False)

    try:
        result = run_chargerlog(
            log_dir=log_dir,
            start=start,
            end=end,
            no_cache=no_cache,
        )
    except FileNotFoundError as e:
        return jsonify({"error": str(e)}), 503
    except ChargerLogError as e:
        return jsonify({"error": str(e), "stderr": e.stderr}), 500

    record = {
        "id": analysis_id,
        "log_dir": log_dir,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "start": start,
        "end": end,
        "points_count": result.get("points_count", 0),
        "cached": result.get("cached", False),
        "fields": result.get("fields", []),
    }
    _save_analysis(analysis_id, record)

    return jsonify(record), 200
