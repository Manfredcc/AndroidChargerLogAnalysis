<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { getHistory, deleteHistory, type HistoryItem } from '../api'

const router = useRouter()

const items = ref<HistoryItem[]>([])
const total = ref(0)
const page = ref(1)
const limit = 20

async function load() {
  const data = await getHistory(page.value, limit)
  items.value = data.items
  total.value = data.total
}

async function doDelete(id: string) {
  await deleteHistory(id)
  load()
}

onMounted(load)
</script>

<template>
  <div class="history-page">
    <h2>历史记录 <span class="count">({{ total }})</span></h2>

    <div v-if="!items.length" class="empty">暂无记录</div>

    <div class="card" v-if="items.length">
      <table>
        <thead>
          <tr>
            <th>目录</th>
            <th>时间</th>
            <th>数据点</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="item in items" :key="item.id" @click="router.push(`/dashboard/${item.id}`)">
            <td class="dir">{{ item.log_dir }}</td>
            <td>{{ new Date(item.created_at).toLocaleString() }}</td>
            <td>{{ item.points_count }}</td>
            <td @click.stop>
              <button class="del" @click="doDelete(item.id)">删除</button>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<style scoped>
.history-page { max-width: 900px; margin: 0 auto; }
h2 { font-size: 20px; margin-bottom: 16px; }
.count { color: #999; font-weight: 400; }
.card { background: #fff; border-radius: 8px; overflow: hidden; box-shadow: 0 1px 3px rgba(0,0,0,.08); }
table { width: 100%; border-collapse: collapse; }
th, td { padding: 10px 16px; text-align: left; font-size: 14px; }
th { color: #888; font-weight: 500; font-size: 12px; background: #fafafa; border-bottom: 1px solid #eee; }
td { border-bottom: 1px solid #f0f0f0; }
tr { cursor: pointer; }
tr:hover td { background: #f8faff; }
.dir { max-width: 400px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.del { padding: 2px 10px; border: 1px solid #ddd; border-radius: 4px; background: #fff; color: #999; cursor: pointer; font-size: 12px; }
.del:hover { border-color: #d32f2f; color: #d32f2f; }
.empty { text-align: center; color: #999; padding: 60px 0; }
</style>
