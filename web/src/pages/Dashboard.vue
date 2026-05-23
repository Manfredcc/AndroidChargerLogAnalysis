<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { useRoute } from 'vue-router'
import * as echarts from 'echarts/core'
import { LineChart } from 'echarts/charts'
import { CanvasRenderer } from 'echarts/renderers'
import { TitleComponent, TooltipComponent, GridComponent, LegendComponent } from 'echarts/components'
import { getAnalysis, type AnalysisResult, type DataPoint } from '../api'
import { computeThreshold, fmtDuration, type ThresholdComputed } from '../utils/threshold'
import { fmtMs, MS_PER_DAY } from '../utils/time'
import TimeRangeSlider from '../components/TimeRangeSlider.vue'
import StatsTable from '../components/StatsTable.vue'
import ChartCard from '../components/ChartCard.vue'

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
  voltageThreshold.value = null
  tempThreshold.value = null
  currentThreshold.value = null
  levelThreshold.value = null
  cycleThreshold.value = null
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

/** 应用阈值样式：主系列半透明，上方覆盖加粗段 */
function applyThreshold(
  mainSeries: any,
  pairs: [number, number | null][],
  threshold: number,
  color: string,
  yAxisIndex?: number,
) {
  mainSeries.lineStyle = { color: hexToRgba(color, 0.25), width: 2 }
  mainSeries.itemStyle = { color: hexToRgba(color, 0.25) }
  mainSeries.areaStyle = {
    color: { type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
      colorStops: [{ offset: 0, color: hexToRgba(color, 0.08) }, { offset: 1, color: hexToRgba(color, 0.02) }] },
  }
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
function onDragStart(_e: DragEvent, idx: number) { dragIdx = idx }
function onDragOver(e: DragEvent, idx: number) {
  e.preventDefault()
  if (idx !== dragIdx) (e.dataTransfer!).dropEffect = 'move'
}
function onDrop(_e: DragEvent, idx: number) {
  if (idx === dragIdx || dragIdx < 0) return
  const items = [...chartOrder.value]
  const [moved] = items.splice(dragIdx, 1)
  items.splice(idx, 0, moved)
  chartOrder.value = items
}
function onDragEnd() { dragIdx = -1 }

// ── 统计 ──────────────────────────────
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

// ── ECharts options ──────────────────────────────

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

      <TimeRangeSlider
        v-if="data.points.length"
        v-model:model-min="timeRangeMin"
        v-model:model-max="timeRangeMax"
        :data-min-t="dataMinT"
        :data-max-t="dataMaxT"
        @reset="resetAll"
      />

      <div class="charts" v-if="data.points.length">
        <template v-for="(def, idx) in chartOrder" :key="def.id">
          <!-- stats: 汇总表 -->
          <ChartCard
            v-if="def.type === 'stats'"
            no-header
            @dragstart="onDragStart($event, idx)"
            @dragover="onDragOver($event, idx)"
            @drop="onDrop($event, idx)"
            @dragend="onDragEnd"
          >
            <StatsTable :rows="statRows" />
          </ChartCard>

          <!-- combined: 双 Y 轴图 (每指标独立一行: 勾选 + 统计 + 阈值) -->
          <ChartCard
            v-else-if="def.type === 'combined'"
            no-header
            @dragstart="onDragStart($event, idx)"
            @dragover="onDragOver($event, idx)"
            @drop="onDrop($event, idx)"
            @dragend="onDragEnd"
          >
            <div class="combined-header">
              <h3>电池电压 · 温度</h3>
              <div class="combined-rows">
                <!-- 电压行 -->
                <div class="combined-row">
                  <span class="stat-item" v-if="voltageStats">电压 {{ fmtStat(voltageStats.max, 0) }}<span class="sl">max</span> {{ fmtStat(voltageStats.min, 0) }}<span class="sl">min</span> {{ fmtStat(voltageStats.avg, 0) }}<span class="sl">avg</span> <span class="su">mV</span></span>
                  <label class="th-label">
                    阈值
                    <input type="number" class="th-input" :value="voltageThreshold ?? ''" @input="voltageThreshold = setTh($event)" placeholder="--" step="any" />
                    <span class="th-unit">mV</span>
                    <span v-if="voltageThResult" class="th-stat">{{ voltageThResult.abovePct.toFixed(1) }}% ({{ fmtDuration(voltageThResult.aboveTimeMs) }})</span>
                  </label>
                  <label class="toggle-label"><input type="checkbox" v-model="showVoltage" /><span>电压</span></label>
                </div>
                <!-- 温度行 -->
                <div class="combined-row">
                  <span class="stat-item" v-if="tempStats">温度 {{ fmtStat(tempStats.max, 1) }}<span class="sl">max</span> {{ fmtStat(tempStats.min, 1) }}<span class="sl">min</span> {{ fmtStat(tempStats.avg, 1) }}<span class="sl">avg</span> <span class="su">°C</span></span>
                  <label class="th-label">
                    阈值
                    <input type="number" class="th-input" :value="tempThreshold ?? ''" @input="tempThreshold = setTh($event)" placeholder="--" step="any" />
                    <span class="th-unit">°C</span>
                    <span v-if="tempThResult" class="th-stat">{{ tempThResult.abovePct.toFixed(1) }}% ({{ fmtDuration(tempThResult.aboveTimeMs) }})</span>
                  </label>
                  <label class="toggle-label"><input type="checkbox" v-model="showTemp" /><span>温度</span></label>
                </div>
              </div>
            </div>
            <div :ref="el => { if (el) chartRefs.set('combined', el as HTMLElement) }" :style="{ height: '300px' }" />
          </ChartCard>

          <!-- current: 电流图 -->
          <ChartCard
            v-else-if="def.type === 'current'"
            @dragstart="onDragStart($event, idx)"
            @dragover="onDragOver($event, idx)"
            @drop="onDrop($event, idx)"
            @dragend="onDragEnd"
          >
            <template #header-center>
              <span class="stat-item" v-if="currentStats">电池电流 {{ fmtStat(currentStats.max, 0) }}<span class="sl">max</span> {{ fmtStat(currentStats.min, 0) }}<span class="sl">min</span> {{ fmtStat(currentStats.avg, 0) }}<span class="sl">avg</span> <span class="su">mA</span></span>
            </template>
            <template #header-right>
              <label class="th-label">
                阈值
                <input type="number" class="th-input" :value="currentThreshold ?? ''" @input="currentThreshold = setTh($event)" placeholder="--" step="any" />
                <span class="th-unit">mA</span>
                <span v-if="currentThResult" class="th-stat">{{ currentThResult.abovePct.toFixed(1) }}% ({{ fmtDuration(currentThResult.aboveTimeMs) }})</span>
              </label>
            </template>
            <template #title>电池电流 <span class="unit">(mA)</span></template>
            <div :ref="el => { if (el) chartRefs.set('current', el as HTMLElement) }" :style="{ height: '240px' }" />
          </ChartCard>

          <!-- level: 电量图 -->
          <ChartCard
            v-else-if="def.type === 'level'"
            @dragstart="onDragStart($event, idx)"
            @dragover="onDragOver($event, idx)"
            @drop="onDrop($event, idx)"
            @dragend="onDragEnd"
          >
            <template #header-center>
              <span class="stat-item" v-if="levelStats">电量 {{ fmtStat(levelStats.max, 0) }}<span class="sl">max</span> {{ fmtStat(levelStats.min, 0) }}<span class="sl">min</span> {{ fmtStat(levelStats.avg, 0) }}<span class="sl">avg</span> <span class="su">%</span></span>
            </template>
            <template #header-right>
              <label class="th-label">
                阈值
                <input type="number" class="th-input" :value="levelThreshold ?? ''" @input="levelThreshold = setTh($event)" placeholder="--" step="any" />
                <span class="th-unit">%</span>
                <span v-if="levelThResult" class="th-stat">{{ levelThResult.abovePct.toFixed(1) }}% ({{ fmtDuration(levelThResult.aboveTimeMs) }})</span>
              </label>
            </template>
            <template #title>电量 <span class="unit">(%)</span></template>
            <div :ref="el => { if (el) chartRefs.set('level', el as HTMLElement) }" :style="{ height: '240px' }" />
          </ChartCard>

          <!-- cycle: 剩余循环图 + 额定输入 -->
          <ChartCard
            v-else-if="def.type === 'cycle'"
            @dragstart="onDragStart($event, idx)"
            @dragover="onDragOver($event, idx)"
            @drop="onDrop($event, idx)"
            @dragend="onDragEnd"
          >
            <template #header-center>
              <span class="stat-item" v-if="cycleStats">剩余循环 {{ fmtStat(cycleStats.max, 0) }}<span class="sl">max</span> {{ fmtStat(cycleStats.min, 0) }}<span class="sl">min</span> {{ fmtStat(cycleStats.avg, 0) }}<span class="sl">avg</span> <span class="su">次</span></span>
            </template>
            <template #header-right>
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
                <span v-if="cycleThResult" class="th-stat">{{ cycleThResult.abovePct.toFixed(1) }}% ({{ fmtDuration(cycleThResult.aboveTimeMs) }})</span>
              </label>
            </template>
            <template #title>剩余循环次数</template>
            <div :ref="el => { if (el) chartRefs.set('cycle', el as HTMLElement) }" :style="{ height: '240px' }" />
          </ChartCard>
        </template>
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

.charts { display: flex; flex-direction: column; gap: 16px; }

.unit { color: #999; font-weight: 400; font-size: 12px; }

/* ── stats inline ── */
.stat-item { font-size: 12px; color: #555; white-space: nowrap; }
.sl {
  display: inline; font-size: 8px; color: #bbb; vertical-align: super;
}
.su { color: #aaa; font-size: 11px; margin-left: 2px; }

/* ── combined chart two-row header ── */
.combined-header {
  display: grid;
  grid-template-columns: auto 1fr;
  align-items: center;
  gap: 4px 12px;
  margin-bottom: 4px;
}
.combined-header h3 {
  font-size: 14px; margin: 0; color: #333;
  grid-row: 1 / 3;
}
.combined-rows {
  display: contents;
}
.combined-row {
  display: grid;
  grid-template-columns: 1fr auto auto;
  align-items: center;
  gap: 8px;
  padding: 2px 0;
}
.combined-row .stat-item {
  text-align: center;
}

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

.empty { text-align: center; color: #999; padding: 60px 0; }
</style>
