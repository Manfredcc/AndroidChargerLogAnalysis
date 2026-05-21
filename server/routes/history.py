"""GET/DELETE /api/history — 历史分析记录管理。"""

import os
from pathlib import Path

from flask import Blueprint, jsonify, request

from routes.upload import _list_analyses, _load_analysis, HISTORY_DIR

history_bp = Blueprint("history", __name__)


@history_bp.route("/api/history", methods=["GET"])
def list_history():
    """列出历史分析记录。

    Query params:
        page: 页码 (默认 1)
        limit: 每页条数 (默认 20)
    """
    page = request.args.get("page", 1, type=int)
    limit = request.args.get("limit", 20, type=int)
    page = max(1, page)
    limit = min(max(1, limit), 100)

    results, total = _list_analyses(page=page, limit=limit)
    return jsonify({
        "items": results,
        "total": total,
        "page": page,
        "limit": limit,
    }), 200


@history_bp.route("/api/history/<analysis_id>", methods=["GET"])
def get_history_detail(analysis_id: str):
    """获取单条历史分析详情。"""
    record = _load_analysis(analysis_id)
    if record is None:
        return jsonify({"error": f"分析记录不存在: {analysis_id}"}), 404
    return jsonify(record), 200


@history_bp.route("/api/history/<analysis_id>", methods=["DELETE"])
def delete_history(analysis_id: str):
    """删除指定分析记录。"""
    filepath = HISTORY_DIR / f"{analysis_id}.json"
    if not filepath.exists():
        return jsonify({"error": f"分析记录不存在: {analysis_id}"}), 404
    os.remove(filepath)
    return jsonify({"deleted": analysis_id}), 200
