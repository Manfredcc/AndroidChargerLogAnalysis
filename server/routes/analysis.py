"""GET /api/analysis/:id — 获取分析详情。"""

from flask import Blueprint, jsonify

from routes.upload import _load_analysis

analysis_bp = Blueprint("analysis", __name__)


@analysis_bp.route("/api/analysis/<analysis_id>", methods=["GET"])
def get_analysis(analysis_id: str):
    """获取指定分析 ID 的结果。"""
    record = _load_analysis(analysis_id)
    if record is None:
        return jsonify({"error": f"分析记录不存在: {analysis_id}"}), 404
    return jsonify(record), 200
