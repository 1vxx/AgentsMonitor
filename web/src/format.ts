import { useEffect, useState } from 'react'
// `t` is a timestamp in here, so the translator goes by another name
import { dateLocale, t as i18n } from './i18n/index.ts'

/** Re-renders every `ms` with the current time. */
export const useNow = (ms = 1000) => {
  const [now, setNow] = useState(() => Date.now())
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), ms)
    return () => clearInterval(id)
  }, [ms])
  return now
}

export const duration = (ms: number) => {
  const s = Math.max(0, Math.floor(ms / 1000))
  if (s < 60) return `${s}s`
  const m = Math.floor(s / 60)
  if (m < 60) return `${m}m ${String(s % 60).padStart(2, '0')}s`
  const h = Math.floor(m / 60)
  if (h < 24) return `${h}h ${String(m % 60).padStart(2, '0')}m`
  return `${Math.floor(h / 24)}d ${h % 24}h`
}

export const ago = (ts: number, now: number) => {
  const d = now - ts
  if (d < 5_000) return i18n('time.justNow')
  return i18n('time.ago', { d: duration(d).split(' ')[0]! })
}

export const clock = (ts: number) =>
  new Date(ts).toLocaleTimeString(dateLocale(), { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' })

export const dateTime = (ts: number) =>
  new Date(ts).toLocaleString(dateLocale(), { hour12: false, month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' })

export const compact = (n: number) => {
  if (n < 1000) return String(n)
  if (n < 1_000_000) return `${(n / 1000).toFixed(n < 10_000 ? 1 : 0)}k`
  if (n < 1_000_000_000) return `${(n / 1_000_000).toFixed(n < 10_000_000 ? 2 : 1)}M`
  return `${(n / 1_000_000_000).toFixed(2)}B`
}

export const bytes = (n: number | undefined) => {
  if (n === undefined) return '—'
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(0)} KB`
  return `${(n / 1024 / 1024).toFixed(1)} MB`
}

export const pct = (n: number | undefined) => (n === undefined ? '—' : `${n < 10 ? n.toFixed(1) : Math.round(n)}%`)

/** `~/x` for paths under the home folder. */
export const tildify = (path: string | undefined) => path?.replace(/^\/Users\/[^/]+|^\/home\/[^/]+/, '~')

export const until = (iso: string | undefined, now: number) => {
  if (!iso) return undefined
  const t = Date.parse(iso)
  if (Number.isNaN(t)) return undefined
  return t <= now ? i18n('time.resetSoon') : i18n('time.resetIn', { d: duration(t - now).replace(/ \d+s$/, '') })
}
