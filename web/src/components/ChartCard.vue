<script setup lang="ts">
import { ref } from 'vue'

defineSlots<{
  default?: (props: Record<string, never>) => any
  title?: (props: Record<string, never>) => any
  'header-center'?: (props: Record<string, never>) => any
  'header-right'?: (props: Record<string, never>) => any
}>()

defineProps<{
  title?: string
  height?: number
  noHeader?: boolean
}>()

const emit = defineEmits<{
  dragstart: [e: DragEvent]
  dragover: [e: DragEvent]
  drop: [e: DragEvent]
  dragend: [e: DragEvent]
}>()

const dragging = ref(false)

function onDragStart(e: DragEvent) {
  dragging.value = true
  e.dataTransfer!.effectAllowed = 'move'
  emit('dragstart', e)
}
function onDragEnd(e: DragEvent) {
  dragging.value = false
  emit('dragend', e)
}
</script>

<template>
  <div
    class="chart-card"
    :class="{ dragging }"
    draggable="true"
    @dragstart="onDragStart"
    @dragover.prevent="$emit('dragover', $event)"
    @drop="$emit('drop', $event)"
    @dragend="onDragEnd"
  >
    <div class="chart-card-header" v-if="!noHeader">
      <h3><slot name="title">{{ title }}</slot></h3>
      <div class="stats-inline">
        <slot name="header-center" />
      </div>
      <div class="header-right">
        <slot name="header-right" />
      </div>
    </div>
    <slot />
  </div>
</template>

<style scoped>
.chart-card { background: #fff; border-radius: 8px; padding: 16px 20px 8px; box-shadow: 0 1px 3px rgba(0,0,0,.08); }
.chart-card h3 { font-size: 14px; margin: 0; color: #333; }

.chart-card-header {
  display: grid;
  grid-template-columns: auto 1fr auto;
  align-items: center;
  margin-bottom: 4px;
  gap: 4px;
}
.header-right { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; }

.stats-inline {
  display: flex; align-items: center; justify-content: center; gap: 10px;
}

.chart-card[draggable] { cursor: grab; }
.chart-card[draggable]:active { cursor: grabbing; }
.chart-card.dragging { opacity: 0.4; }
</style>
