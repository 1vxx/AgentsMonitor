import { useEffect, useState } from 'react'
import { api } from './api.ts'
import { useLive } from './store.ts'
import type { TokenReport, TokenSeries } from './types.ts'

const HOUR = 3_600_000
const DAY = 86_400_000

/** Local midnight `daysBack` days ago. */
export const localMidnight = (now: number, daysBack = 0) => {
  const d = new Date(now)
  d.setHours(0, 0, 0, 0)
  d.setDate(d.getDate() - daysBack)
  return d.getTime()
}

export type RangeId = 'today' | '24h' | '7d' | '30d' | 'all'

export interface Range {
  id: RangeId
  label: string
  /** Start of the totals' window. */
  since: (now: number) => number
  /** Start and bucket of the chart. */
  seriesSince: (now: number) => number
  bucket: number
  chartNote?: string
}

export const RANGES: Range[] = [
  { id: 'today', label: '今天', since: n => localMidnight(n), seriesSince: n => localMidnight(n), bucket: HOUR },
  { id: '24h', label: '24 小时', since: n => n - DAY, seriesSince: n => Math.floor((n - DAY) / HOUR) * HOUR + HOUR, bucket: HOUR },
  { id: '7d', label: '7 天', since: n => localMidnight(n, 6), seriesSince: n => localMidnight(n, 6), bucket: DAY },
  { id: '30d', label: '30 天', since: n => localMidnight(n, 29), seriesSince: n => localMidnight(n, 29), bucket: DAY },
  { id: 'all', label: '全部', since: () => 0, seriesSince: n => localMidnight(n, 89), bucket: DAY, chartNote: '图表显示近 90 天' },
]

/** Refetches when the window moves or new requests are counted. */
export const useTokenReport = (since: number, group?: 'model' | 'source') => {
  const tick = useLive(s => s.tokensTick)
  const [data, setData] = useState<TokenReport>()
  const [error, setError] = useState<string>()
  useEffect(() => {
    let alive = true
    // Exports arrive every couple of seconds: coalesce a burst into one read.
    const t = setTimeout(
      () =>
        api
          .tokens(since, group)
          .then(r => alive && (setData(r), setError(undefined)))
          .catch(e => alive && setError(String(e))),
      tick ? 300 : 0,
    )
    return () => {
      alive = false
      clearTimeout(t)
    }
  }, [since, group, tick])
  return { data, error }
}

export const useTokenSeries = (since: number, bucket: number) => {
  const tick = useLive(s => s.tokensTick)
  const [data, setData] = useState<TokenSeries>()
  useEffect(() => {
    let alive = true
    const t = setTimeout(() => api.tokenSeries(since, bucket).then(r => alive && setData(r)).catch(() => {}), tick ? 300 : 0)
    return () => {
      alive = false
      clearTimeout(t)
    }
  }, [since, bucket, tick])
  return data
}
