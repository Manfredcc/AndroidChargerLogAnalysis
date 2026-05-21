<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { upload, getHistory, deleteHistory, type HistoryItem } from '../api'

const router = useRouter()

const logDir = ref('')
const start = ref('')
const end = ref('')
const noCache = ref(false)
const loading = ref(false)
const error = ref('')
const history = ref<HistoryItem[]>([])

async function loadHistory() {
  try {
    const data = await getHistory(1, 10)
    history.value = data.items
  } catch { /* ignore */ }
}

async function doUpload() {
  if (!logDir.value.trim()) return
  loading.value = true
  error.value = ''
  try {
    const result = await upload({
      log_dir: logDir.value.trim(),
      start: start.value || undefined,
      end: end.value || undefined,
      no_cache: noCache.value || undefined,
    })
    router.push(`/dashboard/${result.id}`)
  } catch (e: any) {
    error.value = e.message || String(e)
  } finally {
    loading.value = false
  }
}

async function doDelete(id: string) {
  await deleteHistory(id)
  loadHistory()
}

loadHistory()
</script>

<template>
  <div class="upload-page">
    <div class="card">
      <h2>新建分析</h2>
      <label>日志目录路径</label>
      <input v-model="logDir" placeholder="例如: D:\Logs\chargerLog设备1"
             @keyup.enter="doUpload" />

      <div class="row">
        <div>
          <label>起始时间 (可选)</label>
          <input v-model="start" placeholder="HH:MM:SS" />
        </div>
        <div>
          <label>结束时间 (可选)</label>
          <input v-model="end" placeholder="HH:MM:SS" />
        </div>
      </div>

      <label class="check">
        <input type="checkbox" v-model="noCache" />
        跳过缓存，强制重新解析
      </label>

      <button class="btn-primary" :disabled="loading || !logDir.trim()" @click="doUpload">
        {{ loading ? '分析中...' : '开始分析' }}
      </button>

      <div v-if="error" class="error">{{ error }}</div>
    </div>

    <div class="card" v-if="history.length">
      <h2>最近分析</h2>
      <div
        class="history-item"
        v-for="item in history"
        :key="item.id"
        @click="router.push(`/dashboard/${item.id}`)"
      >
        <span>
          <b>{{ item.log_dir }}</b>
          <br /><small>{{ item.created_at }} · {{ item.points_count }} 点</small>
        </span>
        <span class="actions">
          <span class="badge" :class="item.cached ? 'badge-ok' : 'badge-miss'">
            {{ item.cached ? '缓存' : '扫描' }}
          </span>
          <span class="del" @click.stop="doDelete(item.id)">x</span>
        </span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.upload-page { max-width: 640px; margin: 0 auto; }
.card { background: #fff; border-radius: 8px; padding: 24px; margin-bottom: 16px; box-shadow: 0 1px 3px rgba(0,0,0,.08); }
h2 { font-size: 16px; margin-bottom: 16px; color: #333; }
label { display: block; font-size: 13px; color: #666; margin-bottom: 4px; margin-top: 10px; }
input[type="text"] { width: 100%; padding: 8px 10px; border: 1px solid #ddd; border-radius: 4px; font-size: 14px; }
.row { display: flex; gap: 12px; }
.row > div { flex: 1; }
.check { display: flex; align-items: center; gap: 8px; margin: 14px 0; cursor: pointer; }
.check input { width: auto; }
.btn-primary {
  width: 100%; padding: 10px; background: #2563eb; color: #fff; border: none;
  border-radius: 6px; font-size: 15px; cursor: pointer; margin-top: 8px;
}
.btn-primary:disabled { background: #a5b4fc; cursor: not-allowed; }
.error { color: #d32f2f; background: #ffeaea; padding: 10px; border-radius: 4px; margin-top: 12px; }
.history-item {
  padding: 10px 0; border-bottom: 1px solid #f0f0f0; cursor: pointer;
  display: flex; justify-content: space-between; align-items: center;
}
.history-item:hover { background: #f8faff; margin: 0 -12px; padding-left: 12px; padding-right: 12px; border-radius: 4px; }
.history-item small { color: #999; }
.badge { display: inline-block; padding: 2px 8px; border-radius: 10px; font-size: 11px; }
.badge-ok { background: #e6f4ea; color: #137333; }
.badge-miss { background: #fef3c7; color: #92400e; }
.del { color: #ccc; padding: 4px 8px; }
.del:hover { color: #d32f2f; }
</style>
