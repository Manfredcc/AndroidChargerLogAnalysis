<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { useRoute } from 'vue-router'
import * as echarts from 'echarts/core'
import { LineChart } from 'echarts/charts'
import { CanvasRenderer } from 'echarts/renderers'
import { TitleComponent, TooltipComponent, GridComponent, LegendComponent } from 'echarts/components'
import { getAnalysis, type AnalysisResult, type DataPoint } from '../api'
import { computeThreshold, fmtDuration, type ThresholdComputed } from '../utils/threshold'

echarts.use([LineChart, CanvasRenderer, TitleComponent, TooltipComponent, GridComponent, LegendComponent])

const route = useRoute()

const data = ref<AnalysisResult | null>(null)
const loading = ref(false)
const reanalyzing = ref(false)
const error = ref('')
const ratedCycles = ref(300)
const showVoltage = ref(true)
const showTemp = ref(true)

// ── 时间范围 ──────────────────────────────
const timeRangeMin = ref(0)
const timeRangeMax = ref(0)
const timeRangeMinInput = ref('')
const timeRangeMaxInput = ref('')

// 每月 1 日前的累计天数 (非闰年)
const DAYS_BEFORE = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365]
const MS_PER_DAY = 86400000

/** 将 MM-DD HH:MM:SS 解析为年内毫秒数 */
function parseAbsTime(s: string): number {
  const match = s.match(/^(\d{2})-(\d{2})\s+(\d{2}):(\d{2}):(\d{2})$/)
  if (!match) return -1
  const month = Number(match[1])
  const day = Number(match[2])
  if (month < 1 || month > 12) return -1
  const maxDay = DAYS_BEFORE[month] - DAYS_BEFORE[month - 1]
  if (day < 1 || day > maxDay) return -1
  const doy = DAYS_BEFORE[month - 1] + (day - 1)
  const tod = Number(match[3]) * 3600000 + Number(match[4]) * 60000 + Number(match[5]) * 1000
  return doy * MS_PER_DAY + tod
}

const validPoints = computed(() => {
  if (!data.value) return []
  return data.value.points.filter(p => p.t >= 0 && p.t <= MS_PER_DAY * 366)
})

const dataMinT = computed(() =>
  validPoints.value.length > 0 ? validPoints.value[0].t : 0
)
const dataMaxT = computed(() =>
  validPoints.value.length > 0 ? validPoints.value[validPoints.value.length - 1].t : 0
)

const filteredPoints = computed(() => {
  if (!data.value || !data.value.points.length) return [] as DataPoint[]
  return data.value.points.filter(p =>
    p.t >= timeRangeMin.value && p.t <= timeRangeMax.value
  )
})

function resetAll() {
  timeRangeMin.value = dataMinT.value
  timeRangeMax.value = dataMaxT.value
  timeRangeMinInput.value = fmtMs(dataMinT.value)
  timeRangeMaxInput.value = fmtMs(dataMaxT.value)
  voltageThreshold.value = null
  tempThreshold.value = null
  currentThreshold.value = null
  levelThreshold.value = null
  cycleThreshold.value = null
}

function onSliderMinInput() {
  if (timeRangeMin.value >= timeRangeMax.value) {
    timeRangeMin.value = timeRangeMax.value - 1000
  }
  timeRangeMinInput.value = fmtMs(timeRangeMin.value)
}

function onSliderMaxInput() {
  if (timeRangeMax.value <= timeRangeMin.value) {
    timeRangeMax.value = timeRangeMin.value + 1000
  }
  timeRangeMaxInput.value = fmtMs(timeRangeMax.value)
}

function onMinInputChange() {
  const ms = parseAbsTime(timeRangeMinInput.value)
  if (ms >= 0 && ms < timeRangeMax.value && ms >= dataMinT.value) {
    timeRangeMin.value = ms
  } else {
    timeRangeMinInput.value = fmtMs(timeRangeMin.value)
  }
}

function onMaxInputChange() {
  const ms = parseAbsTime(timeRangeMaxInput.value)
  if (ms > timeRangeMin.value && ms <= dataMaxT.value) {
    timeRangeMax.value = ms
  } else {
    timeRangeMaxInput.value = fmtMs(timeRangeMax.value)
  }
}

// ── 阈值 refs ──────────────────────────────
const voltageThreshold = ref<number | null>(null)
const tempThreshold = ref<number | null>(null)
const currentThreshold = ref<number | null>(null)
const levelThreshold = ref<number | null>(null)
const cycleThreshold = ref<number | null>(null)

const voltageThResult = computed<ThresholdComputed | null>(() => {
  if (voltageThreshold.value == null || !data.value) return null
  return computeThreshold(filteredPoints.value, 'v', voltageThreshold.value)
})
const tempThResult = computed<ThresholdComputed | null>(() => {
  if (tempThreshold.value == null || !data.value) return null
  return computeThreshold(filteredPoints.value, 'tmp', tempThreshold.value)
})
const currentThResult = computed<ThresholdComputed | null>(() => {
  if (currentThreshold.value == null || !data.value) return null
  return computeThreshold(filteredPoints.value, 'cur', currentThreshold.value)
})
const levelThResult = computed<ThresholdComputed | null>(() => {
  if (levelThreshold.value == null || !data.value) return null
  return computeThreshold(filteredPoints.value, 'lvl', levelThreshold.value)
})
const cycleThResult = computed<ThresholdComputed | null>(() => {
  if (cycleThreshold.value == null || !data.value) return null
  return computeThreshold(filteredPoints.value, 'cc', cycleThreshold.value)
})

interface ChartDef {
  id: string
  title: string
  type: 'stats' | 'combined' | 'current' | 'level' | 'cycle'
  height: number
}
const chartOrder = ref<ChartDef[]>([
  { id: 'stats',    title: '',              type: 'stats',    height: 0 },
  { id: 'combined', title: '电池电压 · 温度', type: 'combined', height: 300 },
  { id: 'current',  title: '电池电流',       type: 'current',  height: 240 },
  { id: 'level',    title: '电量',           type: 'level',    height: 240 },
  { id: 'cycle',    title: '剩余循环次数',   type: 'cycle',    height: 240 },
])

const chartRefs = ref<Map<string, HTMLElement>>(new Map())
const chartMap = new Map<string, echarts.ECharts>()

// ── 阈值辅助 ──────────────────────────────
function setTh(e: Event): number | null {
  const raw = (e.target as HTMLInputElement).value
  return raw === '' ? null : Number(raw)
}
function hexToRgba(hex: string, a: number): string {
  const r = parseInt(hex.slice(1, 3), 16)
  const g = parseInt(hex.slice(3, 5), 16)
  const b = parseInt(hex.slice(5, 7), 16)
  return `rgba(${r},${g},${b},${a})`
}

/** 应用阈值样式：把主系列改成半透明背景，叠加上方全不透明段 */
function applyThreshold(
  mainSeries: any,
  pairs: [number, number | null][],
  threshold: number,
  color: string,
  yAxisIndex?: number,
) {
  // 主系列 → 半透明
  mainSeries.lineStyle = { color: hexToRgba(color, 0.25), width: 2 }
  mainSeries.itemStyle = { color: hexToRgba(color, 0.25) }
  mainSeries.areaStyle = {
    color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
      colorStops: [{ offset: 0, color: hexToRgba(color, 0.08) }, { offset: 1, color: hexToRgba(color, 0.02) }] },
  }
  // 上方覆盖层 → 加粗
  return {
    type: 'line' as const,
    data: pairs.map(([t, v]) => v != null && v >= threshold ? [t, v] : [t, null]),
    yAxisIndex,
    smooth: false,
    symbol: 'none',
    connectNulls: false,
    lineStyle: { color, width: 4 },
    itemStyle: { color },
    showSymbol: false,
    z: 10,
  }
}

// ── 拖拽排序 ──────────────────────────────
let dragIdx = -1
function onDragStart(e: DragEvent, idx: number) {
  dragIdx = idx
  const el = e.target as HTMLElement
  el.classList.add('dragging')
  e.dataTransfer!.effectAllowed = 'move'
}
function onDragOver(e: DragEvent, idx: number) {
  e.preventDefault()
  if (idx === dragIdx) return
  e.dataTransfer!.dropEffect = 'move'
}
function onDrop(_e: DragEvent, idx: number) {
  if (idx === dragIdx || dragIdx < 0) return
  const items = [...chartOrder.value]
  const [moved] = items.splice(dragIdx, 1)
  items.splice(idx, 0, moved)
  chartOrder.value = items
}
function onDragEnd(e: DragEvent) {
  (e.target as HTMLElement).classList.remove('dragging')
  dragIdx = -1
}

interface Stats {
  max: number
  min: number
  avg: number
}

function computeStats(points: DataPoint[], field: 'v' | 'tmp' | 'cur' | 'lvl' | 'cc'): Stats | null {
  const values = points.map(p => p[field]).filter(v => v != null) as number[]
  if (values.length === 0) return null
  let max = values[0], min = values[0], sum = 0
  for (const v of values) {
    if (v > max) max = v
    if (v < min) min = v
    sum += v
  }
  return { max, min, avg: sum / values.length }
}

function fmtStat(val: number, decimals = 1): string {
  return val.toFixed(decimals)
}

interface StatRow {
  label: string
  color: string
  max: string
  min: string
  avg: string
  unit: string
}

const statRows = computed<StatRow[]>(() => {
  const r: StatRow[] = []
  if (voltageStats.value) r.push({ label: '电压 (mV)', color: '#2563eb', max: fmtStat(voltageStats.value.max, 0), min: fmtStat(voltageStats.value.min, 0), avg: fmtStat(voltageStats.value.avg, 0), unit: 'mV' })
  if (tempStats.value)    r.push({ label: '温度 (°C)', color: '#ef4444', max: fmtStat(tempStats.value.max, 1), min: fmtStat(tempStats.value.min, 1), avg: fmtStat(tempStats.value.avg, 1), unit: '°C' })
  if (currentStats.value) r.push({ label: '电流 (mA)', color: '#22c55e', max: fmtStat(currentStats.value.max, 0), min: fmtStat(currentStats.value.min, 0), avg: fmtStat(currentStats.value.avg, 0), unit: 'mA' })
  if (levelStats.value)   r.push({ label: '电量 (%)',  color: '#f59e0b', max: fmtStat(levelStats.value.max, 0), min: fmtStat(levelStats.value.min, 0), avg: fmtStat(levelStats.value.avg, 0), unit: '%' })
  if (cycleStats.value)   r.push({ label: '剩余循环',  color: '#8b5cf6', max: fmtStat(cycleStats.value.max, 0), min: fmtStat(cycleStats.value.min, 0), avg: fmtStat(cycleStats.value.avg, 0), unit: '次' })
  return r
})

const voltageStats  = computed(() => computeStats(filteredPoints.value, 'v'))
const tempStats     = computed(() => computeStats(filteredPoints.value, 'tmp'))
const currentStats  = computed(() => computeStats(filteredPoints.value, 'cur'))
const levelStats    = computed(() => computeStats(filteredPoints.value, 'lvl'))
const cycleStats    = computed(() => {
  const pts = filteredPoints.value
  if (!pts.length) return null
  const total = ratedCycles.value
  const values = pts.map(p => p.cc != null ? total - p.cc : null).filter(v => v != null) as number[]
  if (values.length === 0) return null
  let max = values[0], min = values[0], sum = 0
  for (const v of values) {
    if (v > max) max = v
    if (v < min) min = v
    sum += v
  }
  return { max, min, avg: sum / values.length }
})

function fmtMs(ms: number): string {
  const doy = Math.floor(ms / MS_PER_DAY)
  const tod = ms % MS_PER_DAY
  let month = 12
  for (let m = 1; m <= 12; m++) {
    if (doy < DAYS_BEFORE[m]) { month = m; break }
  }
  const day = doy - DAYS_BEFORE[month - 1] + 1
  const h = Math.floor(tod / 3600000)
  const m = Math.floor((tod % 3600000) / 60000)
  const s = Math.floor((tod % 60000) / 1000)
  return `${String(month).padStart(2, '0')}-${String(day).padStart(2, '0')} ${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
}

function combinedOption() {
  if (!data.value || !filteredPoints.value.length) return {}
  const pts = filteredPoints.value
  const vPairs = pts.map(p => [p.t, p.v] as [number, number | null])
  const tPairs = pts.map(p => [p.t, p.tmp] as [number, number | null])

  const series: any[] = []
  if (showVoltage.value) {
    const s: any = {
      type: 'line', name: '电压', data: vPairs, yAxisIndex: 0,
      smooth: false, symbol: 'none', connectNulls: true,
      lineStyle: { color: '#2563eb', width: 2 },
      itemStyle: { color: '#2563eb' },
      areaStyle: { color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
        colorStops: [{ offset: 0, color: '#2563eb20' }, { offset: 1, color: '#2563eb04' }] } },
    }
    if (voltageThreshold.value != null) {
      series.push(s)
      series.push(applyThreshold(s, vPairs, voltageThreshold.value, '#2563eb', 0))
    } else {
      series.push(s)
    }
  }
  if (showTemp.value) {
    const s: any = {
      type: 'line', name: '温度', data: tPairs, yAxisIndex: showVoltage.value ? 1 : 0,
      smooth: false, symbol: 'none', connectNulls: true,
      lineStyle: { color: '#ef4444', width: 2 },
      itemStyle: { color: '#ef4444' },
      areaStyle: { color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
        colorStops: [{ offset: 0, color: '#ef444420' }, { offset: 1, color: '#ef444404' }] } },
    }
    if (tempThreshold.value != null) {
      series.push(s)
      series.push(applyThreshold(s, tPairs, tempThreshold.value, '#ef4444', showVoltage.value ? 1 : 0))
    } else {
      series.push(s)
    }
  }

  return {
    legend: { data: ['电压', '温度'], bottom: 0 },
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'none' },
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
        name: showVoltage.value || !showTemp.value ? 'mV' : '°C',
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
    series,
  }
}

function currentOption() {
  if (!data.value || !filteredPoints.value.length) return {}
  const pts = filteredPoints.value
  const pairs = pts.map(p => [p.t, p.cur] as [number, number | null])
  const main: any = {
    type: 'line', data: pairs, name: '电流',
    smooth: false, symbol: 'none', connectNulls: true,
    lineStyle: { color: '#22c55e', width: 2 },
    itemStyle: { color: '#22c55e' },
    areaStyle: { color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
      colorStops: [{ offset: 0, color: '#22c55e20' }, { offset: 1, color: '#22c55e04' }] } },
  }
  const series: any[] = currentThreshold.value != null
    ? [main, applyThreshold(main, pairs, currentThreshold.value, '#22c55e')]
    : [main]

  return {
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'none' },
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
    series,
  }
}

function levelOption() {
  if (!data.value || !filteredPoints.value.length) return {}
  const pts = filteredPoints.value
  const pairs = pts.map(p => [p.t, p.lvl] as [number, number | null])
  const main: any = {
    type: 'line', data: pairs, name: '电量',
    smooth: false, symbol: 'none', connectNulls: true,
    lineStyle: { color: '#f59e0b', width: 2 },
    itemStyle: { color: '#f59e0b' },
    areaStyle: { color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
      colorStops: [{ offset: 0, color: '#f59e0b20' }, { offset: 1, color: '#f59e0b04' }] } },
  }
  const series: any[] = levelThreshold.value != null
    ? [main, applyThreshold(main, pairs, levelThreshold.value, '#f59e0b')]
    : [main]

  return {
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'none' },
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
    series,
  }
}

function cycleOption() {
  if (!data.value || !filteredPoints.value.length) return {}
  const pts = filteredPoints.value
  const total = ratedCycles.value
  const pairs = pts.map(p => {
    const cc = p.cc
    if (cc == null) return [p.t, null] as [number, number | null]
    return [p.t, total - cc] as [number, number | null]
  })
  const main: any = {
    type: 'line', data: pairs, name: '剩余循环', step: 'end',
    smooth: false, symbol: 'none', connectNulls: true,
    lineStyle: { color: '#8b5cf6', width: 2 },
    itemStyle: { color: '#8b5cf6' },
    areaStyle: { color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
      colorStops: [{ offset: 0, color: '#8b5cf620' }, { offset: 1, color: '#8b5cf604' }] } },
  }
  const series: any[] = cycleThreshold.value != null
    ? [main, applyThreshold(main, pairs, cycleThreshold.value, '#8b5cf6')]
    : [main]

  return {
    tooltip: {
      trigger: 'axis',
      axisPointer: { type: 'none' },
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
    series,
  }
}

function disposeCharts() {
  chartMap.forEach(c => c.dispose())
  chartMap.clear()
}

const optionFns: Record<string, () => any> = {
  combined: combinedOption,
  current: currentOption,
  level: levelOption,
  cycle: cycleOption,
}

function initCharts() {
  disposeCharts()
  chartOrder.value.forEach(def => {
    if (def.type === 'stats') return
    const el = chartRefs.value.get(def.id)
    if (!el) return
    const chart = echarts.init(el)
    chart.setOption(optionFns[def.id]())
    chartMap.set(def.id, chart)
  })
}

function updateChart(id: string) {
  const chart = chartMap.get(id)
  if (chart) chart.setOption(optionFns[id](), { notMerge: true })
}

watch(ratedCycles, () => updateChart('cycle'))

watch([showVoltage, showTemp], () => updateChart('combined'))

watch(voltageThreshold, () => updateChart('combined'))
watch(tempThreshold, () => updateChart('combined'))
watch(currentThreshold, () => updateChart('current'))
watch(levelThreshold, () => updateChart('level'))
watch(cycleThreshold, () => updateChart('cycle'))

watch([timeRangeMin, timeRangeMax], () => {
  chartOrder.value.forEach(def => {
    if (def.type !== 'stats') updateChart(def.id)
  })
})

watch(chartOrder, async () => {
  await nextTick()
  initCharts()
})

let resizeTimer: ReturnType<typeof setTimeout> | null = null
function onResize() {
  if (resizeTimer) clearTimeout(resizeTimer)
  resizeTimer = setTimeout(() => {
    const widths = [...chartRefs.value.values()].map(el => el?.clientWidth || 0)
    if (widths.length === 0 || widths.some(w => w <= 0)) return
    chartMap.forEach(c => {
      try { c.resize() } catch { /* ignore zero-size errors */ }
    })
  }, 100)
}

onMounted(async () => {
  loading.value = true
  let timer: ReturnType<typeof setTimeout> | null = setTimeout(() => {
    reanalyzing.value = true
  }, 500)
  try {
    data.value = await getAnalysis(route.params.analysisId as string)
    if (data.value && validPoints.value.length > 0) {
      timeRangeMin.value = dataMinT.value
      timeRangeMax.value = dataMaxT.value
      timeRangeMinInput.value = fmtMs(dataMinT.value)
      timeRangeMaxInput.value = fmtMs(dataMaxT.value)
    }
    await nextTick()
    initCharts()
    window.addEventListener('resize', onResize)
  } catch (e: any) {
    error.value = e.message || String(e)
  } finally {
    if (timer) clearTimeout(timer)
    loading.value = false
    reanalyzing.value = false
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

    <div v-if="reanalyzing" class="loading-banner">缓存失效，正在重新分析</div>

    <template v-if="data">
      <div class="header">
        <div>
          <h2>{{ data.log_dir }}</h2>
          <p class="meta">
            {{ data.points_count }} 个数据点 ·
            数据时间 {{ fmtMs(dataMinT) }} ~ {{ fmtMs(dataMaxT) }} ·
            {{ new Date(data.created_at).toLocaleString() }}
          </p>
        </div>
      </div>

      <div class="time-range-bar" v-if="data.points.length && dataMaxT > dataMinT">
        <div class="time-range-header">
          <span class="time-label">时间范围</span>
          <span class="time-value">{{ fmtMs(timeRangeMin) }} ~ {{ fmtMs(timeRangeMax) }}</span>
          <span class="time-dur">({{ fmtDuration(timeRangeMax - timeRangeMin) }})</span>
          <button class="btn-reset" @click="resetAll">重置全部参数</button>
        </div>
        <div class="time-range-controls">
          <input
            class="time-input time-input-left"
            :value="timeRangeMinInput"
            placeholder="MM-DD HH:MM:SS"
            @change="onMinInputChange"
            @input="timeRangeMinInput = ($event.target as HTMLInputElement).value"
          />
          <div class="range-slider">
            <input
              type="range"
              class="range-thumb range-thumb-min"
              :min="dataMinT"
              :max="dataMaxT"
              step="1000"
              :value="timeRangeMin"
              @input="timeRangeMin = Number(($event.target as HTMLInputElement).value); onSliderMinInput()"
            />
            <input
              type="range"
              class="range-thumb range-thumb-max"
              :min="dataMinT"
              :max="dataMaxT"
              step="1000"
              :value="timeRangeMax"
              @input="timeRangeMax = Number(($event.target as HTMLInputElement).value); onSliderMaxInput()"
            />
          </div>
          <input
            class="time-input time-input-right"
            :value="timeRangeMaxInput"
            placeholder="MM-DD HH:MM:SS"
            @change="onMaxInputChange"
            @input="timeRangeMaxInput = ($event.target as HTMLInputElement).value"
          />
        </div>
        <div class="time-range-labels">
          <span>{{ fmtMs(dataMinT) }}</span>
          <span>{{ fmtMs(dataMaxT) }}</span>
        </div>
      </div>

      <div class="charts" v-if="data.points.length">
        <div
          class="chart-card"
          v-for="(def, idx) in chartOrder"
          :key="def.id"
          draggable="true"
          @dragstart="onDragStart($event, idx)"
          @dragover="onDragOver($event, idx)"
          @drop="onDrop($event, idx)"
          @dragend="onDragEnd"
        >
          <!-- stats: 汇总表 -->
          <template v-if="def.type === 'stats'">
            <div class="stats-card-inner" v-if="statRows.length">
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
                  <tr v-for="row in statRows" :key="row.label">
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

          <!-- combined: 双 Y 轴图 -->
          <template v-if="def.type === 'combined'">
            <div class="chart-card-header">
              <h3>{{ def.title }}</h3>
              <div class="stats-inline" v-if="(showVoltage && voltageStats) || (showTemp && tempStats)">
                <span class="stat-item" v-if="showVoltage && voltageStats">电压 {{ fmtStat(voltageStats.max, 0) }}<span class="sl">max</span> {{ fmtStat(voltageStats.min, 0) }}<span class="sl">min</span> {{ fmtStat(voltageStats.avg, 0) }}<span class="sl">avg</span> <span class="su">mV</span></span>
                <span class="stat-item" v-if="showTemp && tempStats">温度 {{ fmtStat(tempStats.max, 1) }}<span class="sl">max</span> {{ fmtStat(tempStats.min, 1) }}<span class="sl">min</span> {{ fmtStat(tempStats.avg, 1) }}<span class="sl">avg</span> <span class="su">°C</span></span>
              </div>
              <div class="header-right">
                <div class="thresholds-group">
                  <label class="th-label">
                    电压阈值
                    <input type="number" class="th-input" :value="voltageThreshold ?? ''" @input="voltageThreshold = setTh($event)" placeholder="--" step="any" />
                    <span class="th-unit">mV</span>
                    <span v-if="voltageThResult" class="th-stat">超 {{ voltageThResult.abovePct.toFixed(1) }}% ({{ fmtDuration(voltageThResult.aboveTimeMs) }})</span>
                  </label>
                  <label class="th-label">
                    温度阈值
                    <input type="number" class="th-input" :value="tempThreshold ?? ''" @input="tempThreshold = setTh($event)" placeholder="--" step="any" />
                    <span class="th-unit">°C</span>
                    <span v-if="tempThResult" class="th-stat">超 {{ tempThResult.abovePct.toFixed(1) }}% ({{ fmtDuration(tempThResult.aboveTimeMs) }})</span>
                  </label>
                </div>
                <div class="toggle-group">
                  <label class="toggle-label"><input type="checkbox" v-model="showVoltage" /><span>电压</span></label>
                  <label class="toggle-label"><input type="checkbox" v-model="showTemp" /><span>温度</span></label>
                </div>
              </div>
            </div>
            <div :ref="el => { if (el) chartRefs.set('combined', el as HTMLElement) }" :style="{ height: def.height + 'px' }" />
          </template>

          <!-- current: 电流图 -->
          <template v-else-if="def.type === 'current'">
            <div class="chart-card-header">
              <h3>{{ def.title }} <span class="unit">(mA)</span></h3>
              <div class="stats-inline" v-if="currentStats">
                <span class="stat-item">电池电流 {{ fmtStat(currentStats.max, 0) }}<span class="sl">max</span> {{ fmtStat(currentStats.min, 0) }}<span class="sl">min</span> {{ fmtStat(currentStats.avg, 0) }}<span class="sl">avg</span> <span class="su">mA</span></span>
              </div>
              <div class="header-right">
                <label class="th-label">
                  阈值
                  <input type="number" class="th-input" :value="currentThreshold ?? ''" @input="currentThreshold = setTh($event)" placeholder="--" step="any" />
                  <span class="th-unit">mA</span>
                  <span v-if="currentThResult" class="th-stat">超 {{ currentThResult.abovePct.toFixed(1) }}% ({{ fmtDuration(currentThResult.aboveTimeMs) }})</span>
                </label>
              </div>
            </div>
            <div :ref="el => { if (el) chartRefs.set('current', el as HTMLElement) }" :style="{ height: def.height + 'px' }" />
          </template>

          <!-- level: 电量图 -->
          <template v-else-if="def.type === 'level'">
            <div class="chart-card-header">
              <h3>{{ def.title }} <span class="unit">(%)</span></h3>
              <div class="stats-inline" v-if="levelStats">
                <span class="stat-item">电量 {{ fmtStat(levelStats.max, 0) }}<span class="sl">max</span> {{ fmtStat(levelStats.min, 0) }}<span class="sl">min</span> {{ fmtStat(levelStats.avg, 0) }}<span class="sl">avg</span> <span class="su">%</span></span>
              </div>
              <div class="header-right">
                <label class="th-label">
                  阈值
                  <input type="number" class="th-input" :value="levelThreshold ?? ''" @input="levelThreshold = setTh($event)" placeholder="--" step="any" />
                  <span class="th-unit">%</span>
                  <span v-if="levelThResult" class="th-stat">超 {{ levelThResult.abovePct.toFixed(1) }}% ({{ fmtDuration(levelThResult.aboveTimeMs) }})</span>
                </label>
              </div>
            </div>
            <div :ref="el => { if (el) chartRefs.set('level', el as HTMLElement) }" :style="{ height: def.height + 'px' }" />
          </template>

          <!-- cycle: 剩余循环图 + 额定输入 -->
          <template v-else-if="def.type === 'cycle'">
            <div class="chart-card-header">
              <h3>{{ def.title }}</h3>
              <div class="stats-inline" v-if="cycleStats">
                <span class="stat-item">剩余循环 {{ fmtStat(cycleStats.max, 0) }}<span class="sl">max</span> {{ fmtStat(cycleStats.min, 0) }}<span class="sl">min</span> {{ fmtStat(cycleStats.avg, 0) }}<span class="sl">avg</span> <span class="su">次</span></span>
              </div>
              <div class="header-right">
                <div class="rated-inline">
                  额定：<input
                    type="number"
                    class="rated-input"
                    :value="ratedCycles"
                    @input="ratedCycles = Number(($event.target as HTMLInputElement).value) || 0"
                    min="0"
                  /> 次
                </div>
                <label class="th-label">
                  阈值
                  <input type="number" class="th-input" :value="cycleThreshold ?? ''" @input="cycleThreshold = setTh($event)" placeholder="--" step="any" />
                  <span class="th-unit">次</span>
                  <span v-if="cycleThResult" class="th-stat">超 {{ cycleThResult.abovePct.toFixed(1) }}% ({{ fmtDuration(cycleThResult.aboveTimeMs) }})</span>
                </label>
              </div>
            </div>
            <div :ref="el => { if (el) chartRefs.set('cycle', el as HTMLElement) }" :style="{ height: def.height + 'px' }" />
          </template>
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
.loading-banner { text-align: center; padding: 40px 20px; color: #888; font-size: 15px; }
.header { display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 20px; }
h2 { font-size: 18px; color: #1a1a1a; word-break: break-all; }
.meta { color: #888; font-size: 13px; margin-top: 4px; }
/* ── Time Range Slider ── */
.time-range-bar {
  background: #fff; border-radius: 8px; padding: 16px 20px;
  margin-bottom: 16px; box-shadow: 0 1px 3px rgba(0,0,0,.08);
}
.time-range-header {
  display: flex; align-items: center; gap: 8px; margin-bottom: 10px;
}
.time-label { font-size: 13px; color: #666; font-weight: 500; }
.time-value { font-size: 14px; color: #1a1a1a; font-weight: 600; font-family: monospace; }
.time-dur { font-size: 12px; color: #999; }
.btn-reset {
  margin-left: auto; padding: 2px 12px; border: 1px solid #ddd;
  border-radius: 4px; background: #fff; font-size: 12px; color: #666; cursor: pointer;
}
.btn-reset:hover { border-color: #2563eb; color: #2563eb; }

.time-range-controls {
  display: flex; align-items: center; gap: 10px;
}
.time-input {
  width: 130px; padding: 4px 6px; border: 1px solid #ddd; border-radius: 4px;
  font-size: 12px; font-family: monospace; text-align: center; color: #333;
}
.time-input:focus { outline: none; border-color: #2563eb; }

.range-slider {
  position: relative; flex: 1; height: 28px;
}
.range-slider input[type="range"] {
  position: absolute; top: 0; left: 0; width: 100%; height: 28px;
  margin: 0; padding: 0;
  -webkit-appearance: none; appearance: none; background: transparent;
  pointer-events: none;
}
.range-thumb-min { z-index: 1; }
.range-thumb-max { z-index: 2; }
/* 只有底层 (min) 显示灰色轨道，上层 (max) 轨道透明 */
.range-thumb-min::-webkit-slider-runnable-track {
  height: 4px; background: #e0e0e0; border-radius: 2px;
}
.range-thumb-max::-webkit-slider-runnable-track {
  height: 4px; background: transparent;
}
.range-slider input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none; appearance: none;
  width: 16px; height: 16px; border-radius: 50%;
  background: #2563eb; border: 2px solid #fff;
  box-shadow: 0 1px 3px rgba(0,0,0,.2);
  cursor: pointer; pointer-events: auto; margin-top: -6px;
}
.range-thumb-min::-moz-range-track {
  height: 4px; background: #e0e0e0; border-radius: 2px;
}
.range-thumb-max::-moz-range-track {
  height: 4px; background: transparent;
}
.range-slider input[type="range"]::-moz-range-thumb {
  width: 16px; height: 16px; border-radius: 50%;
  background: #2563eb; border: 2px solid #fff;
  box-shadow: 0 1px 3px rgba(0,0,0,.2);
  cursor: pointer; pointer-events: auto;
}

.time-range-labels {
  display: flex; justify-content: space-between; margin-top: 4px;
  font-size: 11px; color: #bbb; font-family: monospace;
}

.charts { display: flex; flex-direction: column; gap: 16px; }
.chart-card { background: #fff; border-radius: 8px; padding: 16px 20px 8px; box-shadow: 0 1px 3px rgba(0,0,0,.08); }
.chart-card h3 { font-size: 14px; margin: 0; color: #333; }
.unit { color: #999; font-weight: 400; font-size: 12px; }

.chart-card-header {
  display: grid;
  grid-template-columns: auto 1fr auto;
  align-items: center;
  margin-bottom: 4px;
  gap: 4px;
}
.header-right { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; }

/* ── stats summary table ── */
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

/* ── stats inline ── */
.stats-inline {
  display: flex; align-items: center; justify-content: center; gap: 10px;
}
.stat-item { font-size: 12px; color: #555; white-space: nowrap; }
.sl {
  display: inline; font-size: 8px; color: #bbb; vertical-align: super;
}
.su { color: #aaa; font-size: 11px; margin-left: 2px; }

/* ── toggle (checkbox) ── */
.toggle-group { display: flex; align-items: center; gap: 10px; }
.toggle-label { display: flex; align-items: center; gap: 3px; font-size: 12px; color: #666; cursor: pointer; user-select: none; }
.toggle-label input[type="checkbox"] { cursor: pointer; margin: 0; }

/* ── threshold input & stat ── */
.th-label {
  display: inline-flex;
  align-items: center;
  gap: 3px;
  font-size: 12px;
  color: #999;
}
.th-input {
  width: 52px;
  padding: 1px 4px;
  border: 1px solid #ddd;
  border-radius: 4px;
  font-size: 12px;
  text-align: center;
  color: #333;
}
.th-input:focus { outline: none; border-color: #ef4444; }
.th-input::placeholder { color: #ccc; font-size: 10px; }
.th-unit { color: #bbb; }
.th-stat { color: #ef4444; font-size: 11px; white-space: nowrap; margin-left: 2px; }
.thresholds-group { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }

/* ── rated ── */
.rated-inline { font-size: 12px; color: #999; display: flex; align-items: center; gap: 4px; }
.rated-input {
  width: 48px; padding: 1px 4px; border: 1px solid #ddd; border-radius: 4px;
  font-size: 12px; text-align: center; color: #333;
}
.rated-input:focus { outline: none; border-color: #8b5cf6; }

/* ── drag ── */
.chart-card[draggable] { cursor: grab; }
.chart-card[draggable]:active { cursor: grabbing; }
.chart-card.dragging { opacity: 0.4; }
.empty { text-align: center; color: #999; padding: 60px 0; }
</style>
