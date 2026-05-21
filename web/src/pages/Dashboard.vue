<script setup lang="ts">
import { ref, onMounted, onUnmounted, nextTick } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import * as echarts from 'echarts/core'
import { LineChart } from 'echarts/charts'
import { CanvasRenderer } from 'echarts/renderers'
import { TitleComponent, TooltipComponent, GridComponent } from 'echarts/components'
import { getAnalysis, type AnalysisResult } from '../api'

echarts.use([LineChart, CanvasRenderer, TitleComponent, TooltipComponent, GridComponent])

const route = useRoute()
const router = useRouter()

const data = ref<AnalysisResult | null>(null)
const error = ref('')
const chartRefs = ref<HTMLElement[]>([])
let charts: echarts.ECharts[] = []

function fmtMs(ms: number): string {
  const h = Math.floor(ms / 3600000)
  const m = Math.floor((ms % 3600000) / 60000)
  const s = Math.floor((ms % 60000) / 1000)
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
}

function lineOption(field: 'v' | 'tmp' | 'cur', unit: string, color: string) {
  if (!data.value || !data.value.points.length) return {}
  const pts = data.value.points
  const pairs = pts.map(p => [p.t, p[field]] as [number, number | null])

  return {
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const p = params[0]
        if (!p) return ''
        const time = fmtMs(p.data[0])
        return `${time}<br/>${p.seriesName}: ${p.data[1]} ${unit}`
      },
    },
    grid: { left: 60, right: 20, top: 10, bottom: 30 },
    xAxis: {
      type: 'value',
      scale: true,
      axisLabel: {
        fontSize: 10,
        formatter: (ms: number) => fmtMs(ms),
      },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      name: unit,
      nameTextStyle: { fontSize: 11 },
      axisLabel: { fontSize: 10 },
      splitLine: { lineStyle: { color: '#f0f0f0', type: 'dashed' as const } },
    },
    series: [{
      type: 'line',
      data: pairs,
      name: '',
      smooth: false,
      symbol: 'none',
      connectNulls: true,
      lineStyle: { color, width: 2 },
      areaStyle: {
        color: {
          type: 'linear',
          x: 0, y: 0, x2: 0, y2: 1,
          colorStops: [
            { offset: 0, color: color + '20' },
            { offset: 1, color: color + '04' },
          ],
        },
      },
    }],
  }
}

const fieldConfigs: { field: 'v' | 'tmp' | 'cur'; label: string; unit: string; color: string }[] = [
  { field: 'v', label: '电池电压', unit: 'mV', color: '#2563eb' },
  { field: 'tmp', label: '电池温度', unit: '°C', color: '#ef4444' },
  { field: 'cur', label: '电池电流', unit: 'mA', color: '#22c55e' },
]

function disposeCharts() {
  charts.forEach(c => c.dispose())
  charts = []
}

function initCharts() {
  disposeCharts()
  chartRefs.value.forEach((el, i) => {
    if (!el) return
    const cfg = fieldConfigs[i]
    const chart = echarts.init(el)
    chart.setOption(lineOption(cfg.field, cfg.unit, cfg.color))
    charts.push(chart)
  })
}

let resizeTimer: ReturnType<typeof setTimeout> | null = null
function onResize() {
  if (resizeTimer) clearTimeout(resizeTimer)
  resizeTimer = setTimeout(() => {
    const widths = chartRefs.value.map(el => el?.clientWidth || 0)
    if (widths.some(w => w <= 0)) return
    charts.forEach(c => {
      try { c.resize() } catch { /* ignore zero-size errors */ }
    })
  }, 100)
}

onMounted(async () => {
  try {
    data.value = await getAnalysis(route.params.analysisId as string)
    await nextTick()
    initCharts()
    window.addEventListener('resize', onResize)
  } catch (e: any) {
    error.value = e.message || String(e)
  }
})

onUnmounted(() => {
  window.removeEventListener('resize', onResize)
  if (resizeTimer) clearTimeout(resizeTimer)
  disposeCharts()
})
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

      <div class="charts" v-if="data.points.length">
        <div class="chart-card" v-for="(cfg, idx) in fieldConfigs" :key="cfg.field">
          <h3>{{ cfg.label }} <span class="unit">({{ cfg.unit }})</span></h3>
          <div :ref="el => { if (el) chartRefs[idx] = el as HTMLElement }" style="height:240px" />
        </div>
      </div>
      <div class="empty" v-else-if="!error">
        暂无数据点。请确认日志目录中包含 healthd 数据。
      </div>
    </template>
  </div>
</template>

<style scoped>
.dashboard { max-width: 960px; margin: 0 auto; }
.error { color: #d32f2f; background: #ffeaea; padding: 16px; border-radius: 8px; }
.header { display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 20px; }
h2 { font-size: 18px; color: #1a1a1a; word-break: break-all; }
.meta { color: #888; font-size: 13px; margin-top: 4px; }
.btn-back { padding: 6px 16px; border: 1px solid #ddd; border-radius: 6px; background: #fff; cursor: pointer; font-size: 13px; }
.btn-back:hover { border-color: #2563eb; color: #2563eb; }

.charts { display: flex; flex-direction: column; gap: 16px; }
.chart-card { background: #fff; border-radius: 8px; padding: 16px 20px 8px; box-shadow: 0 1px 3px rgba(0,0,0,.08); }
.chart-card h3 { font-size: 14px; margin: 0 0 4px; color: #333; }
.unit { color: #999; font-weight: 400; font-size: 12px; }
.empty { text-align: center; color: #999; padding: 60px 0; }
</style>
