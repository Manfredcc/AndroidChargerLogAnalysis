<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import * as echarts from 'echarts/core'
import { LineChart } from 'echarts/charts'
import { CanvasRenderer } from 'echarts/renderers'
import { TitleComponent, TooltipComponent, GridComponent, LegendComponent } from 'echarts/components'
import { getAnalysis, type AnalysisResult } from '../api'

echarts.use([LineChart, CanvasRenderer, TitleComponent, TooltipComponent, GridComponent, LegendComponent])

const route = useRoute()
const router = useRouter()

const data = ref<AnalysisResult | null>(null)
const error = ref('')
const ratedCycles = ref(300)
const chartRefs = ref<HTMLElement[]>([])
let charts: echarts.ECharts[] = []
let cycleChart: echarts.ECharts | null = null

function fmtMs(ms: number): string {
  const h = Math.floor(ms / 3600000)
  const m = Math.floor((ms % 3600000) / 60000)
  const s = Math.floor((ms % 60000) / 1000)
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
}

function combinedOption() {
  if (!data.value || !data.value.points.length) return {}
  const pts = data.value.points
  const vPairs = pts.map(p => [p.t, p.v] as [number, number | null])
  const tPairs = pts.map(p => [p.t, p.tmp] as [number, number | null])

  return {
    legend: { data: ['电压', '温度'], bottom: 0 },
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const time = fmtMs(params[0]?.data[0] || 0)
        let s = time
        params.forEach((p: any) => {
          if (p && p.data[1] != null) s += `<br/>${p.seriesName}: ${p.data[1]} ${p.seriesName === '电压' ? 'mV' : '°C'}`
        })
        return s
      },
    },
    grid: { left: 60, right: 60, top: 10, bottom: 40 },
    xAxis: {
      type: 'value',
      scale: true,
      axisLabel: { fontSize: 10, formatter: (ms: number) => fmtMs(ms) },
      splitLine: { show: false },
    },
    yAxis: [
      {
        type: 'value',
        name: 'mV',
        nameTextStyle: { fontSize: 11 },
        axisLabel: { fontSize: 10 },
        splitLine: { lineStyle: { color: '#f0f0f0', type: 'dashed' as const } },
      },
      {
        type: 'value',
        name: '°C',
        nameTextStyle: { fontSize: 11 },
        axisLabel: { fontSize: 10 },
        splitLine: { show: false },
      },
    ],
    series: [
      {
        type: 'line',
        name: '电压',
        data: vPairs,
        yAxisIndex: 0,
        smooth: false,
        symbol: 'none',
        connectNulls: true,
        lineStyle: { color: '#2563eb', width: 2 },
        itemStyle: { color: '#2563eb' },
        areaStyle: {
          color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [{ offset: 0, color: '#2563eb20' }, { offset: 1, color: '#2563eb04' }] },
        },
      },
      {
        type: 'line',
        name: '温度',
        data: tPairs,
        yAxisIndex: 1,
        smooth: false,
        symbol: 'none',
        connectNulls: true,
        lineStyle: { color: '#ef4444', width: 2 },
        itemStyle: { color: '#ef4444' },
        areaStyle: {
          color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [{ offset: 0, color: '#ef444420' }, { offset: 1, color: '#ef444404' }] },
        },
      },
    ],
  }
}

function currentOption() {
  if (!data.value || !data.value.points.length) return {}
  const pts = data.value.points
  const pairs = pts.map(p => [p.t, p.cur] as [number, number | null])

  return {
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const p = params[0]
        if (!p || p.data[1] == null) return ''
        return `${fmtMs(p.data[0])}<br/>电流: ${p.data[1]} mA`
      },
    },
    grid: { left: 60, right: 20, top: 10, bottom: 30 },
    xAxis: {
      type: 'value',
      scale: true,
      axisLabel: { fontSize: 10, formatter: (ms: number) => fmtMs(ms) },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      name: 'mA',
      nameTextStyle: { fontSize: 11 },
      axisLabel: { fontSize: 10 },
      splitLine: { lineStyle: { color: '#f0f0f0', type: 'dashed' as const } },
    },
    series: [{
      type: 'line',
      data: pairs,
      name: '电流',
      smooth: false,
      symbol: 'none',
      connectNulls: true,
      lineStyle: { color: '#22c55e', width: 2 },
      itemStyle: { color: '#22c55e' },
      areaStyle: {
        color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
          colorStops: [{ offset: 0, color: '#22c55e20' }, { offset: 1, color: '#22c55e04' }] },
      },
    }],
  }
}

function levelOption() {
  if (!data.value || !data.value.points.length) return {}
  const pts = data.value.points
  const pairs = pts.map(p => [p.t, p.lvl] as [number, number | null])

  return {
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const p = params[0]
        if (!p || p.data[1] == null) return ''
        return `${fmtMs(p.data[0])}<br/>电量: ${p.data[1]}%`
      },
    },
    grid: { left: 50, right: 20, top: 10, bottom: 30 },
    xAxis: {
      type: 'value',
      scale: true,
      axisLabel: { fontSize: 10, formatter: (ms: number) => fmtMs(ms) },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      name: '%',
      min: 0,
      max: 100,
      nameTextStyle: { fontSize: 11 },
      axisLabel: { fontSize: 10 },
      splitLine: { lineStyle: { color: '#f0f0f0', type: 'dashed' as const } },
    },
    series: [{
      type: 'line',
      data: pairs,
      name: '电量',
      smooth: false,
      symbol: 'none',
      connectNulls: true,
      lineStyle: { color: '#f59e0b', width: 2 },
      itemStyle: { color: '#f59e0b' },
      areaStyle: {
        color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
          colorStops: [{ offset: 0, color: '#f59e0b20' }, { offset: 1, color: '#f59e0b04' }] },
      },
    }],
  }
}

function cycleOption() {
  if (!data.value || !data.value.points.length) return {}
  const pts = data.value.points
  const total = ratedCycles.value
  const pairs = pts.map(p => {
    const cc = p.cc
    if (cc == null) return [p.t, null] as [number, number | null]
    return [p.t, total - cc] as [number, number | null]
  })

  return {
    tooltip: {
      trigger: 'axis',
      formatter: (params: any) => {
        const p = params[0]
        if (!p || p.data[1] == null) return ''
        return `${fmtMs(p.data[0])}<br/>剩余循环: ${p.data[1]} / ${total}`
      },
    },
    grid: { left: 50, right: 20, top: 10, bottom: 30 },
    xAxis: {
      type: 'value',
      scale: true,
      axisLabel: { fontSize: 10, formatter: (ms: number) => fmtMs(ms) },
      splitLine: { show: false },
    },
    yAxis: {
      type: 'value',
      name: '次',
      max: total || undefined,
      minInterval: 1,
      nameTextStyle: { fontSize: 11 },
      axisLabel: { fontSize: 10 },
      splitLine: { lineStyle: { color: '#f0f0f0', type: 'dashed' as const } },
    },
    series: [{
      type: 'line',
      data: pairs,
      name: '剩余循环',
      step: 'end',
      smooth: false,
      symbol: 'none',
      connectNulls: true,
      lineStyle: { color: '#8b5cf6', width: 2 },
      itemStyle: { color: '#8b5cf6' },
      areaStyle: {
        color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
          colorStops: [{ offset: 0, color: '#8b5cf620' }, { offset: 1, color: '#8b5cf604' }] },
      },
    }],
  }
}

function disposeCharts() {
  charts.forEach(c => c.dispose())
  charts = []
  cycleChart = null
}

function initCharts() {
  disposeCharts()
  const refs = chartRefs.value
  if (refs[0]) {
    const c1 = echarts.init(refs[0])
    c1.setOption(combinedOption())
    charts.push(c1)
  }
  if (refs[1]) {
    const c2 = echarts.init(refs[1])
    c2.setOption(currentOption())
    charts.push(c2)
  }
  if (refs[2]) {
    const c3 = echarts.init(refs[2])
    c3.setOption(levelOption())
    charts.push(c3)
  }
  if (refs[3]) {
    cycleChart = echarts.init(refs[3])
    cycleChart.setOption(cycleOption())
    charts.push(cycleChart)
  }
}

watch(ratedCycles, () => {
  if (cycleChart) cycleChart.setOption(cycleOption())
})

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
        <div class="chart-card">
          <h3>电池电压 · 温度</h3>
          <div :ref="el => { if (el) chartRefs[0] = el as HTMLElement }" style="height:300px" />
        </div>
        <div class="chart-card">
          <h3>电池电流 <span class="unit">(mA)</span></h3>
          <div :ref="el => { if (el) chartRefs[1] = el as HTMLElement }" style="height:240px" />
        </div>
        <div class="chart-card">
          <h3>电量 <span class="unit">(%)</span></h3>
          <div :ref="el => { if (el) chartRefs[2] = el as HTMLElement }" style="height:240px" />
        </div>
        <div class="chart-card">
          <div class="chart-card-header">
            <h3>剩余循环次数</h3>
            <div class="rated-inline">
              额定：<input
                type="number"
                class="rated-input"
                :value="ratedCycles"
                @input="ratedCycles = Number(($event.target as HTMLInputElement).value) || 0"
                min="0"
              /> 次
            </div>
          </div>
          <div :ref="el => { if (el) chartRefs[3] = el as HTMLElement }" style="height:240px" />
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
.chart-card h3 { font-size: 14px; margin: 0; color: #333; }
.unit { color: #999; font-weight: 400; font-size: 12px; }
.chart-card-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px; }
.rated-inline { font-size: 12px; color: #999; display: flex; align-items: center; gap: 4px; }
.rated-input {
  width: 48px; padding: 1px 4px; border: 1px solid #ddd; border-radius: 4px;
  font-size: 12px; text-align: center; color: #333;
}
.rated-input:focus { outline: none; border-color: #8b5cf6; }
.empty { text-align: center; color: #999; padding: 60px 0; }
</style>
