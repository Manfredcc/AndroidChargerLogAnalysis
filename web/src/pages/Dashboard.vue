<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import VChart from 'vue-echarts'
import { use } from 'echarts/core'
import { GaugeChart, BarChart } from 'echarts/charts'
import { CanvasRenderer } from 'echarts/renderers'
import { TitleComponent, TooltipComponent, GridComponent } from 'echarts/components'
import { getAnalysis, type AnalysisResult, type FieldResult } from '../api'

use([GaugeChart, BarChart, CanvasRenderer, TitleComponent, TooltipComponent, GridComponent])

const route = useRoute()
const router = useRouter()

const data = ref<AnalysisResult | null>(null)
const error = ref('')

onMounted(async () => {
  try {
    data.value = await getAnalysis(route.params.analysisId as string)
  } catch (e: any) {
    error.value = e.message || String(e)
  }
})

function gaugeOption(field: FieldResult) {
  const max = field.max
  const min = field.min
  return {
    series: [{
      type: 'gauge',
      startAngle: 210,
      endAngle: -30,
      center: ['50%', '60%'],
      radius: '90%',
      min: Math.floor(min * 0.9),
      max: Math.ceil(max * 1.1),
      splitNumber: 5,
      axisLine: {
        lineStyle: {
          width: 8,
          color: [
            [0.3, '#67e0e3'],
            [0.7, '#37a2da'],
            [1, '#2563eb'],
          ],
        },
      },
      pointer: { length: '60%', width: 4, itemStyle: { color: 'auto' } },
      detail: {
        valueAnimation: true,
        formatter: `{value} ${field.unit}`,
        fontSize: 14,
        offsetCenter: [0, '70%'],
      },
      title: { offsetCenter: [0, '90%'], fontSize: 12 },
      data: [{ value: max, name: field.label }],
    }],
  }
}

const fieldByName = computed(() => {
  if (!data.value) return {}
  const m: Record<string, FieldResult> = {}
  data.value.fields.forEach(f => { m[f.name] = f })
  return m
})

function barOption(field: FieldResult) {
  return {
    tooltip: { trigger: 'axis' },
    xAxis: { type: 'category', data: ['最高', '最低', '平均', '中位数'] },
    yAxis: {
      type: 'value',
      name: field.unit,
      min: Math.floor(field.min * 0.95),
      max: Math.ceil(field.max * 1.05),
    },
    series: [{
      type: 'bar',
      data: [
        { value: field.max, itemStyle: { color: '#ef4444' } },
        { value: field.min, itemStyle: { color: '#3b82f6' } },
        { value: field.avg, itemStyle: { color: '#22c55e' } },
        { value: field.median, itemStyle: { color: '#f59e0b' } },
      ],
      barWidth: '50%',
    }],
  }
}
</script>

<template>
  <div class="dashboard">
    <div v-if="error" class="error">{{ error }}</div>

    <template v-if="data">
      <div class="header">
        <div>
          <h2>{{ data.log_dir }}</h2>
          <p class="meta">
            {{ data.points_count }} 个数据点 ·
            <span v-if="data.start || data.end">
              范围 {{ data.start || '...' }} ~ {{ data.end || '...' }} ·
            </span>
            {{ data.cached ? '从缓存加载' : '重新扫描' }} ·
            {{ new Date(data.created_at).toLocaleString() }}
          </p>
        </div>
        <button class="btn-back" @click="router.push('/history')">返回历史</button>
      </div>

      <!-- Gauge gauges -->
      <div class="gauges" v-if="data.fields.length">
        <div class="gauge-card" v-for="f in data.fields" :key="f.name">
          <v-chart :option="gaugeOption(f)" autoresize style="height:220px" />
        </div>
      </div>

      <!-- Bar charts -->
      <div class="bars" v-if="data.fields.length">
        <div class="bar-card" v-for="f in data.fields" :key="f.name">
          <h3>{{ f.label }} <span class="unit">({{ f.unit }})</span></h3>
          <v-chart :option="barOption(f)" autoresize style="height:200px" />
          <div class="stats">
            <div class="stat">
              <span class="stat-label">最高</span>
              <span class="stat-val hi">{{ f.max }} {{ f.unit }}</span>
            </div>
            <div class="stat">
              <span class="stat-label">最低</span>
              <span class="stat-val lo">{{ f.min }} {{ f.unit }}</span>
            </div>
            <div class="stat">
              <span class="stat-label">平均</span>
              <span class="stat-val">{{ f.avg }} {{ f.unit }}</span>
            </div>
            <div class="stat">
              <span class="stat-label">中位数</span>
              <span class="stat-val">{{ f.median }} {{ f.unit }}</span>
            </div>
            <div class="stat">
              <span class="stat-label">数据量</span>
              <span class="stat-val">{{ f.count }}</span>
            </div>
          </div>
        </div>
      </div>
    </template>
  </div>
</template>

<style scoped>
.dashboard { max-width: 1200px; margin: 0 auto; }
.error { color: #d32f2f; background: #ffeaea; padding: 16px; border-radius: 8px; }
.header { display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 24px; }
h2 { font-size: 20px; color: #1a1a1a; word-break: break-all; }
.meta { color: #888; font-size: 13px; margin-top: 4px; }
.btn-back { padding: 6px 16px; border: 1px solid #ddd; border-radius: 6px; background: #fff; cursor: pointer; font-size: 13px; }
.btn-back:hover { border-color: #2563eb; color: #2563eb; }

.gauges { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 16px; margin-bottom: 24px; }
.gauge-card { background: #fff; border-radius: 8px; padding: 12px; box-shadow: 0 1px 3px rgba(0,0,0,.08); }

.bars { display: grid; grid-template-columns: repeat(auto-fit, minmax(500px, 1fr)); gap: 16px; }
.bar-card { background: #fff; border-radius: 8px; padding: 20px; box-shadow: 0 1px 3px rgba(0,0,0,.08); }
.bar-card h3 { font-size: 15px; margin-bottom: 4px; }
.unit { color: #999; font-weight: 400; font-size: 12px; }
.stats { display: flex; gap: 12px; margin-top: 8px; padding-top: 8px; border-top: 1px solid #f0f0f0; }
.stat { flex: 1; text-align: center; }
.stat-label { display: block; font-size: 11px; color: #999; }
.stat-val { display: block; font-size: 15px; font-weight: 600; }
.stat-val.hi { color: #ef4444; }
.stat-val.lo { color: #3b82f6; }
</style>
