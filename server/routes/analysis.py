"""GET /api/analysis/:id — 获取分析详情（每次重新运行 chargerlog，利用 .chargerlog_cache 加速）。"""

import os
from datetime import datetime, timezone

from flask import Blueprint, jsonify

from routes.upload import _load_analysis, _save_analysis
from utils.cpp_bridge import run_chargerlog, ChargerLogError

analysis_bp = Blueprint("analysis", __name__)


@analysis_bp.route("/api/analysis/<analysis_id>", methods=["GET"])
def get_analysis(analysis_id: str):
    """获取指定分析 ID 的结果。

    每次都重新调用 chargerlog，利用日志目录下的 .chargerlog_cache
    判断是否需要重新解析。指纹不变时秒级返回，文件变化时自动重新扫描。
    """
    record = _load_analysis(analysis_id)
    if record is None:
        return jsonify({"error": f"分析记录不存在: {analysis_id}"}), 404

    log_dir = record.get("log_dir", "")
    if not log_dir or not os.path.isdir(log_dir):
        # 目录已不存在，返回历史快照
        return jsonify(record), 200

    try:
        result = run_chargerlog(
            log_dir=log_dir,
            no_cache=False,
            points=True,
            downsample=500,
        )
    except FileNotFoundError as e:
        return jsonify({"error": str(e)}), 503
    except ChargerLogError as e:
        return jsonify({"error": str(e), "stderr": e.stderr}), 500

    # 更新记录
    record["points_count"] = result.get("points_count", 0)
    record["cached"] = result.get("cached", False)
    record["fields"] = result.get("fields", [])
    record["points"] = result.get("points", [])
    record["updated_at"] = datetime.now(timezone.utc).isoformat()
    _save_analysis(analysis_id, record)

    return jsonify(record), 200
