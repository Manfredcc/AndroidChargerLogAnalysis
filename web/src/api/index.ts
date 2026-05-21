const BASE = '/api'

export interface FieldResult {
  name: string
  label: string
  unit: string
  count: number
  max: number
  min: number
  avg: number
  median: number
}

export interface DataPoint {
  t: number         // elapsed_ms
  v: number | null   // battery_voltage_mv
  tmp: number | null // battery_temperature_c
  cur: number | null // battery_current_ma
  lvl: number | null // battery_level_pct
}

export interface AnalysisResult {
  id: string
  log_dir: string
  created_at: string
  points_count: number
  cached: boolean
  start?: string
  end?: string
  fields: FieldResult[]
  points: DataPoint[]
}

export interface HistoryItem {
  id: string
  log_dir: string
  created_at: string
  points_count: number
  cached: boolean
}

export interface HistoryList {
  items: HistoryItem[]
  total: number
  page: number
  limit: number
}

export async function upload(params: {
  log_dir: string
  start?: string
  end?: string
  no_cache?: boolean
}): Promise<AnalysisResult> {
  const resp = await fetch(`${BASE}/upload`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  })
  const data = await resp.json()
  if (data.error) throw new Error(data.error)
  return data
}

export async function getAnalysis(id: string): Promise<AnalysisResult> {
  const resp = await fetch(`${BASE}/analysis/${id}`)
  const data = await resp.json()
  if (data.error) throw new Error(data.error)
  return data
}

export async function getHistory(page = 1, limit = 20): Promise<HistoryList> {
  const resp = await fetch(`${BASE}/history?page=${page}&limit=${limit}`)
  return resp.json()
}

export async function deleteHistory(id: string): Promise<void> {
  await fetch(`${BASE}/history/${id}`, { method: 'DELETE' })
}
