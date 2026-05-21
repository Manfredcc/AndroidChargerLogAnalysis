import { createRouter, createWebHashHistory } from 'vue-router'

const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    {
      path: '/',
      redirect: '/upload',
    },
    {
      path: '/upload',
      name: 'upload',
      component: () => import('../pages/UploadPage.vue'),
    },
    {
      path: '/dashboard/:analysisId',
      name: 'dashboard',
      component: () => import('../pages/Dashboard.vue'),
    },
    {
      path: '/history',
      name: 'history',
      component: () => import('../pages/HistoryPage.vue'),
    },
  ],
})

export default router
