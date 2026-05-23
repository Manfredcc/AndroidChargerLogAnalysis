// 每月 1 日前的累计天数 (非闰年)
export const DAYS_BEFORE = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365]
export const MS_PER_DAY = 86400000

/** 将 MM-DD HH:MM:SS 解析为年内毫秒数，失败返回 -1 */
export function parseAbsTime(s: string): number {
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

/** 将年内毫秒数格式化为 MM-DD HH:MM:SS */
export function fmtMs(ms: number): string {
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
