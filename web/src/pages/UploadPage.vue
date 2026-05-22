<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { upload, getHistory, deleteHistory, selectPath, type HistoryItem } from '../api'

const router = useRouter()

const logDir = ref('')
const noCache = ref(false)
const loading = ref(false)
const error = ref('')
const history = ref<HistoryItem[]>([])
const showBrowseMenu = ref(false)
const browseWrapper = ref<HTMLElement | null>(null)

async function loadHistory() {
  try {
    const data = await getHistory(1, 50)
    const seen = new Set<string>()
    history.value = data.items.filter(item => {
      if (seen.has(item.log_dir)) return false
      seen.add(item.log_dir)
      return true
    }).slice(0, 10)
  } catch { /* ignore */ }
}

async function doBrowseDir() {
  showBrowseMenu.value = false
  try {
    const result = await selectPath('directory')
    if (result.path) logDir.value = result.path
  } catch (e: any) {
    error.value = e.message || String(e)
  }
}

async function doBrowseFile() {
  showBrowseMenu.value = false
  try {
    const result = await selectPath('file')
    if (result.path) logDir.value = result.path
  } catch (e: any) {
    error.value = e.message || String(e)
  }
}

function onClickOutside(e: MouseEvent) {
  if (browseWrapper.value && !browseWrapper.value.contains(e.target as Node)) {
    showBrowseMenu.value = false
  }
}

onMounted(() => document.addEventListener('click', onClickOutside))
onUnmounted(() => document.removeEventListener('click', onClickOutside))

async function doUpload() {
  if (!logDir.value.trim()) return
  loading.value = true
  error.value = ''
  try {
    const result = await upload({
      log_dir: logDir.value.trim(),
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

function timeAgo(iso: string): string {
  const diff = Date.now() - new Date(iso).getTime()
  const mins = Math.floor(diff / 60000)
  if (mins < 1) return '刚刚'
  if (mins < 60) return `${mins}分钟前`
  const hrs = Math.floor(mins / 60)
  if (hrs < 24) return `${hrs}小时前`
  const days = Math.floor(hrs / 24)
  if (days < 30) return `${days}天前`
  return new Date(iso).toLocaleDateString()
}

loadHistory()
</script>

<template>
  <div class="upload-page">
    <div class="card">
      <h2>新建分析</h2>
      <label>日志路径</label>
      <div class="path-row">
        <input v-model="logDir" placeholder="日志目录路径或压缩包路径 (支持 .zip, .tar.gz 等)"
               @keyup.enter="doUpload" />
        <div class="browse-wrapper" ref="browseWrapper">
          <button class="btn-browse" @click="showBrowseMenu = !showBrowseMenu" :disabled="loading">
            浏览 ▾
          </button>
          <div class="browse-menu" v-if="showBrowseMenu">
            <div class="browse-menu-item" @click="doBrowseDir">选择日志目录</div>
            <div class="browse-menu-item" @click="doBrowseFile">选择压缩文件</div>
          </div>
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
          <br /><small>{{ new Date(item.created_at).toLocaleString() }} · {{ item.points_count }} 点</small>
        </span>
        <span class="actions">
          <span class="badge badge-time">{{ timeAgo(item.created_at) }}</span>
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
.path-row { display: flex; gap: 8px; }
.path-row input { flex: 1; }
.btn-browse {
  padding: 8px 16px; border: 1px solid #2563eb; border-radius: 4px;
  background: #fff; color: #2563eb; font-size: 14px; cursor: pointer; white-space: nowrap;
}
.btn-browse:hover { background: #eff6ff; }
.btn-browse:disabled { opacity: 0.5; cursor: not-allowed; }
.browse-wrapper { position: relative; }
.browse-menu {
  position: absolute; top: 100%; right: 0; margin-top: 4px;
  background: #fff; border: 1px solid #e0e0e0; border-radius: 6px;
  box-shadow: 0 4px 12px rgba(0,0,0,.1); z-index: 100; min-width: 140px; overflow: hidden;
}
.browse-menu-item {
  padding: 8px 14px; font-size: 13px; color: #333; cursor: pointer; white-space: nowrap;
}
.browse-menu-item:hover { background: #f0f5ff; color: #2563eb; }
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
.badge-time { background: #f0f0f0; color: #888; }
.del { color: #ccc; padding: 4px 8px; }
.del:hover { color: #d32f2f; }
</style>
