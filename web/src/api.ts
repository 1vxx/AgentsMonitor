import type { DaemonStats, EventRecord, MaintenanceReport, QuotaSample, Session, TokenReport, TokenSeries } from './types.ts'

const get = async <T,>(path: string): Promise<T> => {
  const res = await fetch(path)
  if (!res.ok) throw new Error(`${path}: ${res.status} ${await res.text()}`)
  return (await res.json()) as T
}

const query = (params: Record<string, string | number | undefined>) => {
  const q = new URLSearchParams()
  for (const [k, v] of Object.entries(params)) if (v !== undefined && v !== '') q.set(k, String(v))
  const s = q.toString()
  return s ? `?${s}` : ''
}

export const api = {
  history: (p: { agent?: string; limit?: number; offset?: number }) =>
    get<Session[]>(`/v1/sessions${query({ scope: 'all', ...p })}`),
  session: (key: string) => get<Session>(`/v1/sessions/${encodeURIComponent(key)}`),
  sessionEvents: (key: string, before?: number) =>
    get<EventRecord[]>(`/v1/sessions/${encodeURIComponent(key)}/events${query({ limit: 300, before })}`),
  events: (before?: number) => get<EventRecord[]>(`/v1/events${query({ limit: 300, before })}`),
  quotaHistory: (hours: number, agent?: string) =>
    get<QuotaSample[]>(`/v1/quotas/history${query({ hours, agent })}`),
  tokens: (since: number, group?: 'model' | 'source' | 'agent' | 'session', agent?: string) =>
    get<TokenReport>(`/v1/tokens${query({ since, group, agent })}`),
  tokenSeries: (since: number, bucketMs: number, agent?: string) =>
    get<TokenSeries>(`/v1/tokens/series${query({ since, bucket_ms: bucketMs, agent })}`),
  stats: () => get<DaemonStats>('/v1/stats'),
  /** Runs a cleanup pass now; `allEnded` also forgets every ended session. */
  maintenance: async (allEnded: boolean) => {
    const res = await fetch(`/v1/maintenance${allEnded ? '?ended=all' : ''}`, { method: 'POST' })
    if (!res.ok) throw new Error(`cleanup: ${res.status} ${await res.text()}`)
    return (await res.json()) as MaintenanceReport
  },
  forget: async (key: string) => {
    const res = await fetch(`/v1/sessions/${encodeURIComponent(key)}`, { method: 'DELETE' })
    if (!res.ok) throw new Error(`forget: ${res.status}`)
  },
}
