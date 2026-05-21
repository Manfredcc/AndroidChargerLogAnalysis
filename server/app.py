"""
ChargerLogAnalysis — Flask Web API 入口。

提供 REST API 用于充电日志分析：
  POST   /api/upload          — 上传并分析日志目录
  GET    /api/analysis/:id    — 获取分析结果
  GET    /api/history          — 历史分析列表
  GET    /api/history/:id      — 历史分析详情
  DELETE /api/history/:id      — 删除分析记录

启动方式:
  python app.py
  flask run --host=0.0.0.0 --port=5000
"""

import sys
from pathlib import Path

from flask import Flask, jsonify
from flask_cors import CORS

# 确保 server/ 目录在 sys.path 中，使 utils/ 和 routes/ 可导入
_server_dir = Path(__file__).resolve().parent
if str(_server_dir) not in sys.path:
    sys.path.insert(0, str(_server_dir))


def create_app() -> Flask:
    app = Flask(__name__)

    # 允许跨域请求（前端独立部署时必需）
    CORS(app, resources={r"/api/*": {"origins": "*"}})

    # ── 注册路由蓝图 ──────────────────────────────────────
    from routes.upload import upload_bp
    from routes.analysis import analysis_bp
    from routes.history import history_bp

    app.register_blueprint(upload_bp)
    app.register_blueprint(analysis_bp)
    app.register_blueprint(history_bp)

    # ── 全局错误处理 ──────────────────────────────────────
    @app.errorhandler(404)
    def not_found(e):
        return jsonify({"error": "接口不存在"}), 404

    @app.errorhandler(500)
    def internal_error(e):
        return jsonify({"error": "服务器内部错误"}), 500

    # ── 根路径 ──────────────────────────────────────────
    @app.route("/")
    def index():
        return r"""<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<title>ChargerLogAnalysis</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: -apple-system, system-ui, sans-serif; max-width: 800px; margin: 20px auto; padding: 20px; background: #f5f5f5; }
  h1 { color: #1a1a1a; margin-bottom: 8px; }
  .sub { color: #888; font-size: 14px; margin-bottom: 20px; }
  .card { background: #fff; border-radius: 8px; padding: 20px; margin-bottom: 16px; box-shadow: 0 1px 3px rgba(0,0,0,.1); }
  .card h2 { font-size: 16px; margin-bottom: 12px; color: #333; }
  label { display: block; font-size: 13px; color: #555; margin-bottom: 4px; }
  input { width: 100%; padding: 8px 10px; border: 1px solid #ddd; border-radius: 4px; font-size: 14px; margin-bottom: 10px; }
  .row { display: flex; gap: 10px; }
  .row > div { flex: 1; }
  button { padding: 8px 20px; background: #2563eb; color: #fff; border: none; border-radius: 4px; cursor: pointer; font-size: 14px; }
  button:hover { background: #1d4ed8; }
  .err { color: #d32f2f; background: #ffeaea; padding: 10px; border-radius: 4px; margin-top: 10px; white-space: pre-wrap; }
  table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 14px; }
  th, td { padding: 6px 10px; border-bottom: 1px solid #eee; text-align: left; }
  th { color: #888; font-weight: 500; font-size: 12px; }
  .badge { display: inline-block; padding: 2px 8px; border-radius: 10px; font-size: 11px; }
  .badge-ok { background: #e6f4ea; color: #137333; }
  .badge-miss { background: #fef3c7; color: #92400e; }
  .loading { color: #888; animation: pulse 1s infinite; }
  @keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.5; } }
  .history-item { padding: 8px 0; border-bottom: 1px solid #eee; cursor: pointer; display: flex; justify-content: space-between; align-items: center; }
  .history-item:hover { background: #f0f4ff; }
  .history-item .del { color: #ccc; cursor: pointer; padding: 2px 6px; }
  .history-item .del:hover { color: #d32f2f; }
</style>
</head>
<body>
<h1>ChargerLogAnalysis</h1>
<p class="sub">充电日志分析 — 输入目录路径开始分析</p>

<div class="card">
  <h2>新建分析</h2>
  <label>日志目录路径</label>
  <input id="log_dir" placeholder="例如: D:\Logs\chargerLog设备1" value="">
  <div class="row">
    <div><label>起始时间 (可选)</label><input id="start" placeholder="HH:MM:SS"></div>
    <div><label>结束时间 (可选)</label><input id="end" placeholder="HH:MM:SS"></div>
  </div>
  <div style="margin:10px 0">
    <label><input type="checkbox" id="no_cache"> 跳过缓存，强制重新解析</label>
  </div>
  <button onclick="doUpload()">开始分析</button>
  <div id="result"></div>
</div>

<div class="card">
  <h2>历史记录</h2>
  <div id="history">加载中...</div>
</div>

<script>
async function loadHistory() {
  try {
    let r = await fetch('/api/history');
    let d = await r.json();
    let h = document.getElementById('history');
    if (!d.items || d.items.length === 0) { h.innerHTML = '<p style="color:#888">暂无记录</p>'; return; }
    h.innerHTML = d.items.map(item =>
      '<div class="history-item" onclick="loadAnalysis(\'' + item.id + '\')">' +
      '<span><b>' + item.log_dir + '</b><br><small style="color:#888">' + item.created_at + ' · ' + item.points_count + ' 点</small></span>' +
      '<span style="font-size:12px;color:#888">' + (item.cached ? '<span class="badge badge-ok">缓存</span>' : '<span class="badge badge-miss">扫描</span>') +
      ' <span class="del" onclick="event.stopPropagation();delHistory(\'' + item.id + '\')">x</span></span>' +
      '</div>'
    ).join('');
  } catch(e) { document.getElementById('history').innerHTML = '<p class="err">' + e + '</p>'; }
}

async function doUpload() {
  let res = document.getElementById('result');
  res.innerHTML = '<p class="loading">分析中...</p>';
  let body = { log_dir: document.getElementById('log_dir').value };
  let start = document.getElementById('start').value;
  let end = document.getElementById('end').value;
  if (start) body.start = start;
  if (end) body.end = end;
  if (document.getElementById('no_cache').checked) body.no_cache = true;
  try {
    let r = await fetch('/api/upload', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) });
    let d = await r.json();
    if (d.error) { res.innerHTML = '<div class="err">' + d.error + '</div>'; return; }
    let html = '<p style="margin-top:10px">共 <b>' + d.points_count + '</b> 个数据点 '
      + (d.cached ? '<span class="badge badge-ok">来自缓存</span>' : '<span class="badge badge-miss">重新扫描</span>')
      + ' &nbsp; <small>ID: ' + d.id + '</small></p>';
    if (d.fields && d.fields.length > 0) {
      html += '<table><tr><th>指标</th><th>最高</th><th>最低</th><th>平均</th><th>中位数</th><th>数据量</th></tr>';
      d.fields.forEach(f => {
        html += '<tr><td>' + f.label + '</td><td>' + f.max + ' ' + f.unit + '</td><td>' + f.min + ' ' + f.unit + '</td><td>' + f.avg + ' ' + f.unit + '</td><td>' + f.median + ' ' + f.unit + '</td><td>' + f.count + '</td></tr>';
      });
      html += '</table>';
    }
    res.innerHTML = html;
    loadHistory();
  } catch(e) { res.innerHTML = '<div class="err">' + e + '</div>'; }
}

async function loadAnalysis(id) {
  let res = document.getElementById('result');
  res.innerHTML = '<p class="loading">加载中...</p>';
  try {
    let r = await fetch('/api/analysis/' + id);
    let d = await r.json();
    if (d.error) { res.innerHTML = '<div class="err">' + d.error + '</div>'; return; }
    document.getElementById('log_dir').value = d.log_dir || '';
    document.getElementById('start').value = d.start || '';
    document.getElementById('end').value = d.end || '';
    let html = '<p style="margin-top:10px">共 <b>' + d.points_count + '</b> 个数据点 '
      + (d.cached ? '<span class="badge badge-ok">来自缓存</span>' : '<span class="badge badge-miss">重新扫描</span>')
      + ' &nbsp; <small>ID: ' + d.id + '</small></p>';
    if (d.fields && d.fields.length > 0) {
      html += '<table><tr><th>指标</th><th>最高</th><th>最低</th><th>平均</th><th>中位数</th><th>数据量</th></tr>';
      d.fields.forEach(f => {
        html += '<tr><td>' + f.label + '</td><td>' + f.max + ' ' + f.unit + '</td><td>' + f.min + ' ' + f.unit + '</td><td>' + f.avg + ' ' + f.unit + '</td><td>' + f.median + ' ' + f.unit + '</td><td>' + f.count + '</td></tr>';
      });
      html += '</table>';
    }
    res.innerHTML = html;
  } catch(e) { res.innerHTML = '<div class="err">' + e + '</div>'; }
}

async function delHistory(id) {
  if (!confirm('删除此记录？')) return;
  await fetch('/api/history/' + id, { method: 'DELETE' });
  loadHistory();
}

loadHistory();
</script>
</body>
</html>"""

    # ── 健康检查 ──────────────────────────────────────────
    @app.route("/api/health")
    def health():
        return jsonify({"status": "ok"}), 200

    return app


app = create_app()

if __name__ == "__main__":
    print("ChargerLogAnalysis API Server")
    print(f"  访问: http://127.0.0.1:5000")
    print(f"  健康检查: http://127.0.0.1:5000/api/health")
    app.run(host="0.0.0.0", port=5000, debug=True)
