<script setup lang="ts">
interface StatRow {
  label: string
  color: string
  max: string
  min: string
  avg: string
  unit: string
}

defineProps<{
  rows: StatRow[]
}>()
</script>

<template>
  <div class="stats-card-inner" v-if="rows.length">
    <table class="stats-table">
      <thead>
        <tr>
          <th>指标</th>
          <th>最大值</th>
          <th>最小值</th>
          <th>平均值</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="row in rows" :key="row.label">
          <td><span class="stat-dot" :style="{ background: row.color }"></span>{{ row.label }}</td>
          <td class="val">{{ row.max }}</td>
          <td class="val">{{ row.min }}</td>
          <td class="val">{{ row.avg }}</td>
        </tr>
      </tbody>
    </table>
  </div>
  <div v-else class="stats-empty">暂无统计数据</div>
</template>

<style scoped>
.stats-card-inner { padding: 0; }
.stats-table { width: 100%; border-collapse: collapse; font-size: 13px; }
.stats-table th {
  color: #888; font-weight: 500; font-size: 11px;
  padding: 4px 8px; border-bottom: 1px solid #eee; text-align: center;
}
.stats-table th:first-child { text-align: left; }
.stats-table td {
  padding: 5px 8px; border-bottom: 1px solid #f5f5f5; text-align: center; color: #333;
}
.stats-table td:first-child { text-align: left; color: #555; }
.stats-table .val { font-family: monospace; font-weight: 500; }
.stats-table tbody tr:last-child td { border-bottom: none; }
.stat-dot {
  display: inline-block; width: 8px; height: 8px; border-radius: 50%;
  margin-right: 6px; vertical-align: middle;
}
.stats-empty { text-align: center; color: #bbb; padding: 16px 0; font-size: 13px; }
</style>
