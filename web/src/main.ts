// Edge 浏览器兼容修复：页面不可见时跳过 history.replaceState，
// 避免 Edge Bug 导致窗口最小化后立即弹回
const isEdge = /Edg\//.test(navigator.userAgent)
if (isEdge) {
  const originalReplaceState = history.replaceState
  history.replaceState = function (...args) {
    if (document.visibilityState === 'hidden') return
    return (originalReplaceState as Function).apply(this, args)
  }
}

import { createApp } from 'vue'
import App from './App.vue'
import router from './router'

const app = createApp(App)
app.use(router)
app.mount('#app')
