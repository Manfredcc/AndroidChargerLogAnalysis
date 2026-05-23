<script setup lang="ts">
import { ref, watch } from 'vue'
import { parseAbsTime, fmtMs } from '../utils/time'
import { fmtDuration } from '../utils/threshold'

const props = defineProps<{
  dataMinT: number
  dataMaxT: number
  modelMin: number
  modelMax: number
}>()

const emit = defineEmits<{
  'update:modelMin': [value: number]
  'update:modelMax': [value: number]
  'reset': []
}>()

const minInput = ref(fmtMs(props.modelMin))
const maxInput = ref(fmtMs(props.modelMax))

watch(() => props.modelMin, (v) => { minInput.value = fmtMs(v) })
watch(() => props.modelMax, (v) => { maxInput.value = fmtMs(v) })

function onSliderMinInput(e: Event) {
  let v = Number((e.target as HTMLInputElement).value)
  if (v >= props.modelMax) v = props.modelMax - 1000
  emit('update:modelMin', v)
}

function onSliderMaxInput(e: Event) {
  let v = Number((e.target as HTMLInputElement).value)
  if (v <= props.modelMin) v = props.modelMin + 1000
  emit('update:modelMax', v)
}

function onMinInputChange() {
  const ms = parseAbsTime(minInput.value)
  if (ms >= 0 && ms < props.modelMax && ms >= props.dataMinT) {
    emit('update:modelMin', ms)
  } else {
    minInput.value = fmtMs(props.modelMin)
  }
}

function onMaxInputChange() {
  const ms = parseAbsTime(maxInput.value)
  if (ms > props.modelMin && ms <= props.dataMaxT) {
    emit('update:modelMax', ms)
  } else {
    maxInput.value = fmtMs(props.modelMax)
  }
}
</script>

<template>
  <div class="time-range-bar" v-if="dataMaxT > dataMinT">
    <div class="time-range-header">
      <span class="time-label">时间范围</span>
      <span class="time-value">{{ fmtMs(modelMin) }} ~ {{ fmtMs(modelMax) }}</span>
      <span class="time-dur">({{ fmtDuration(modelMax - modelMin) }})</span>
      <button class="btn-reset" @click="emit('reset')">重置全部参数</button>
    </div>
    <div class="time-range-controls">
      <input
        class="time-input time-input-left"
        :value="minInput"
        placeholder="MM-DD HH:MM:SS"
        @change="onMinInputChange"
        @input="minInput = ($event.target as HTMLInputElement).value"
      />
      <div class="range-slider">
        <input
          type="range"
          class="range-thumb range-thumb-min"
          :min="dataMinT"
          :max="dataMaxT"
          step="1000"
          :value="modelMin"
          @input="onSliderMinInput"
        />
        <input
          type="range"
          class="range-thumb range-thumb-max"
          :min="dataMinT"
          :max="dataMaxT"
          step="1000"
          :value="modelMax"
          @input="onSliderMaxInput"
        />
      </div>
      <input
        class="time-input time-input-right"
        :value="maxInput"
        placeholder="MM-DD HH:MM:SS"
        @change="onMaxInputChange"
        @input="maxInput = ($event.target as HTMLInputElement).value"
      />
    </div>
    <div class="time-range-labels">
      <span>{{ fmtMs(dataMinT) }}</span>
      <span>{{ fmtMs(dataMaxT) }}</span>
    </div>
  </div>
</template>

<style scoped>
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
</style>
