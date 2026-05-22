<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'

const router = useRouter()
const route = useRoute()

function isActive(path: string) {
  return route.path.startsWith(path)
}

let heartbeatTimer: ReturnType<typeof setInterval> | null = null

function sendHeartbeat() {
  fetch('/api/heartbeat', { method: 'POST' }).catch(() => {})
}

onMounted(() => {
  sendHeartbeat()
  heartbeatTimer = setInterval(sendHeartbeat, 10_000)
  window.addEventListener('beforeunload', () => {
    navigator.sendBeacon('/api/shutdown')
  })
})

onUnmounted(() => {
  if (heartbeatTimer) clearInterval(heartbeatTimer)
})
</script>

<template>
  <div class="app-shell">
    <header class="nav-bar">
      <span class="logo" @click="router.push('/')">ChargerLogAnalysis</span>
      <nav>
        <button :class="{ active: isActive('/upload') }" @click="router.push('/upload')">
          分析
        </button>
        <button :class="{ active: isActive('/history') }" @click="router.push('/history')">
          历史
        </button>
      </nav>
    </header>
    <main>
      <router-view />
    </main>
  </div>
</template>

<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: -apple-system, system-ui, sans-serif; background: #f0f2f5; color: #1a1a1a; }
.app-shell { min-height: 100vh; }
.nav-bar {
  display: flex; align-items: center; justify-content: space-between;
  padding: 0 24px; height: 48px; background: #fff;
  border-bottom: 1px solid #e5e7eb; position: sticky; top: 0; z-index: 100;
}
.logo { font-weight: 700; font-size: 16px; color: #2563eb; cursor: pointer; user-select: none; }
nav { display: flex; gap: 4px; }
nav button {
  padding: 6px 16px; border: none; border-radius: 6px;
  background: transparent; color: #555; font-size: 14px; cursor: pointer;
}
nav button:hover { background: #f0f4ff; color: #2563eb; }
nav button.active { background: #e0e7ff; color: #2563eb; font-weight: 600; }
main { max-width: 1200px; margin: 0 auto; padding: 24px; }
</style>
