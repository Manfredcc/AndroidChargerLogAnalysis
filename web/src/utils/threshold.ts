import type { DataPoint } from '../api'

export interface ThresholdSegment {
  start: number
  end: number
}

export interface ThresholdComputed {
  aboveTimeMs: number
  totalTimeMs: number
  abovePct: number
  aboveSegments: ThresholdSegment[]
}

/**
 * Compute time-above-threshold from existing DataPoint[] array.
 * Uses linear interpolation between consecutive points to locate
 * exact threshold crossing times.
 */
export function computeThreshold(
  points: DataPoint[],
  fieldKey: 'v' | 'tmp' | 'cur' | 'lvl' | 'cc',
  threshold: number,
): ThresholdComputed {
  const valid = points
    .filter(p => p[fieldKey] != null)
    .map(p => ({ t: p.t, v: p[fieldKey] as number }))

  if (valid.length < 2) {
    return { aboveTimeMs: 0, totalTimeMs: 0, abovePct: 0, aboveSegments: [] }
  }

  const totalTimeMs = valid[valid.length - 1].t - valid[0].t
  let aboveTimeMs = 0
  const aboveSegments: ThresholdSegment[] = []
  let segStart: number | null = null

  for (let i = 0; i < valid.length - 1; i++) {
    const p1 = valid[i]
    const p2 = valid[i + 1]
    const dur = p2.t - p1.t
    if (dur <= 0) continue

    const above1 = p1.v > threshold
    const above2 = p2.v > threshold

    if (above1 && above2) {
      aboveTimeMs += dur
      if (segStart === null) segStart = p1.t
    } else if (above1 && !above2) {
      const ratio = (threshold - p2.v) / (p1.v - p2.v)
      const crossT = p2.t - ratio * dur
      aboveTimeMs += Math.max(0, crossT - p1.t)
      if (segStart === null) segStart = p1.t
      aboveSegments.push({ start: segStart!, end: crossT })
      segStart = null
    } else if (!above1 && above2) {
      const ratio = (threshold - p1.v) / (p2.v - p1.v)
      const crossT = p1.t + ratio * dur
      aboveTimeMs += Math.max(0, p2.t - crossT)
      segStart = crossT
    }
  }

  if (segStart !== null) {
    aboveSegments.push({ start: segStart, end: valid[valid.length - 1].t })
  }

  const abovePct = totalTimeMs > 0 ? (aboveTimeMs / totalTimeMs) * 100 : 0
  return { aboveTimeMs, totalTimeMs, abovePct, aboveSegments }
}

/** Format milliseconds as compact duration string, e.g. "2h 15m" or "45m" */
export function fmtDuration(ms: number): string {
  const totalSec = Math.round(ms / 1000)
  const h = Math.floor(totalSec / 3600)
  const m = Math.floor((totalSec % 3600) / 60)
  if (h > 0) return `${h}h ${m}m`
  return `${m}m`
}
