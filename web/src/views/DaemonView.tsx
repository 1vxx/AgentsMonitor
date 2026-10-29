import { useCallback, useEffect, useState, type ReactNode } from 'react'
import { AnimatePresence, motion } from 'motion/react'
import { api } from '../api.ts'
import { bytes, compact, dateTime, duration } from '../format.ts'
import { t, useT } from '../i18n/index.ts'
import { useLive } from '../store.ts'
import type { DaemonStats, MaintenanceReport } from '../types.ts'
import { Numeral, rise } from '../components/motion.tsx'

const Stat = ({ label, value, i }: { label: string; value: ReactNode; i: number }) => (
  <motion.div className="fact" {...rise(i, 0.03)}>
    <dt>{label}</dt>
    <dd>{value}</dd>
  </motion.div>
)

const Count = ({ v }: { v: number }) => <Numeral value={v} format={n => compact(Math.round(n))} />

/** A setting in minutes, as `30m` or `2h`. */
const span = (minutes: number) => duration(minutes * 60_000).replace(/ 0+[sm]$/, '')

const summarize = (r: MaintenanceReport) => {
  const parts: string[] = []
  if (r.forgotten > 0) {
    const why = [
      r.too_old && t('dm.tooOld', { n: r.too_old }),
      r.transcript_gone && t('dm.gone', { n: r.transcript_gone }),
      r.ended && t('dm.ended', { n: r.ended }),
    ].filter(Boolean)
    parts.push(t('dm.forgot', { n: r.forgotten, why: why.join(t('list.sep')) }))
  }
  if (r.archived > 0) parts.push(t('dm.archived', { n: r.archived }))
  return parts.length ? parts.join(t('dm.partSep')) : t('dm.nothing')
}

/** Runs the cleanup now, or forgets every ended session (archived ones included). */
const Cleanup = ({ ended, onDone }: { ended: number; onDone: () => void }) => {
  const [busy, setBusy] = useState<'retention' | 'all'>()
  const [report, setReport] = useState<MaintenanceReport>()
  const [error, setError] = useState<string>()
  useT()

  const run = async (all: boolean) => {
    if (all && !confirm(t('dm.confirmAll', { n: ended }))) return
    setBusy(all ? 'all' : 'retention')
    setError(undefined)
    try {
      setReport(await api.maintenance(all))
      onDone()
    } catch (e) {
      setError(String(e))
    } finally {
      setBusy(undefined)
    }
  }

  return (
    <div className="cleanup">
      <div className="cleanup-actions">
        <button className="btn btn-primary" disabled={busy !== undefined} onClick={() => void run(false)}>
          {t(busy === 'retention' ? 'dm.cleaning' : 'dm.cleanNow')}
        </button>
        <button className="btn btn-danger" disabled={busy !== undefined || ended === 0} onClick={() => void run(true)}>
          {busy === 'all' ? t('dm.removing') : t('dm.removeAll', { n: ended })}
        </button>
      </div>
      <div className="footnote">
        {t('dm.cleanupNote')}
      </div>
      {error && <div className="error">{error}</div>}
      <AnimatePresence>
        {report && (
          <motion.div
            key={report.duration_ms + report.db_bytes_after}
            className="cleanup-result"
            role="status"
            initial={{ opacity: 0, y: -6 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0 }}
          >
            {t('dm.result', { summary: summarize(report), before: bytes(report.db_bytes_before), after: bytes(report.db_bytes_after), ms: report.duration_ms })}
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  )
}

export const DaemonView = () => {
  const server = useLive(s => s.server)
  useT()
  const conn = useLive(s => s.conn)
  const [stats, setStats] = useState<DaemonStats>()
  const [error, setError] = useState<string>()

  const refresh = useCallback(
    () =>
      api
        .stats()
        .then(s => (setStats(s), setError(undefined)))
        .catch(e => setError(String(e))),
    [],
  )

  useEffect(() => {
    void refresh()
    const id = setInterval(refresh, 3000)
    return () => clearInterval(id)
  }, [refresh])

  return (
    <div className="daemon-view">
      <motion.div className="section-head" {...rise(0)}>
        <div className="section-lead">
          <span className="kicker">
            <b>01</b> Daemon
          </span>
          <h2 className="section-title">{t('dm.title')}</h2>
        </div>
        <span className="section-meta">
          {server ? t('dm.meta', { name: server.name, version: server.version, protocol: server.protocol, t: dateTime(server.started_at) }) : conn}
        </span>
      </motion.div>
      {error && <div className="error">{error}</div>}
      {stats ? (
        <dl className="grid-stats grid-stats-lg">
          <Stat i={0} label={t('dm.uptime')} value={duration(stats.uptime_ms)} />
          <Stat i={1} label={t('dm.rss')} value={bytes(stats.rss_bytes)} />
          <Stat i={2} label={t('dm.cpu')} value={stats.cpu_ms !== undefined ? `${(stats.cpu_ms / 1000).toFixed(2)}s` : '—'} />
          <Stat i={3} label={t('dm.events')} value={<Count v={stats.events_in} />} />
          <Stat i={4} label={t('dm.batches')} value={<Count v={stats.batches_in} />} />
          <Stat i={5} label={t('dm.rejected')} value={<Count v={stats.rejected} />} />
          <Stat i={6} label={t('dm.ws')} value={<Count v={stats.ws_clients} />} />
          <Stat i={7} label={t('dm.sessions')} value={`${stats.sessions_live} / ${stats.sessions_in_memory}`} />
          <Stat i={8} label={t('dm.dbSessions')} value={<Count v={stats.db?.sessions ?? 0} />} />
          <Stat i={9} label={t('dm.dbEvents')} value={<Count v={stats.db?.events ?? 0} />} />
          <Stat i={10} label={t('dm.quota')} value={<Count v={stats.db?.quota_samples ?? 0} />} />
          <Stat i={11} label={t('dm.requests')} value={<Count v={stats.db?.token_requests ?? 0} />} />
          <Stat i={12} label={t('dm.dupes')} value={<Count v={stats.usage_duplicates ?? 0} />} />
          <Stat i={13} label={t('dm.dbSize')} value={bytes(stats.db_bytes)} />
        </dl>
      ) : (
        !error && <span className="skeleton" style={{ height: 260 }} />
      )}
      {stats?.retention && (
        <motion.section className="panel retention" {...rise(6)}>
          <div className="section-lead">
            <span className="kicker">
              <b>02</b> Retention
            </span>
            <h3 className="section-title section-title-sm">{t('dm.retention')}</h3>
          </div>
          <ul>
            {stats.live && (
              <li>
                {t(stats.live.hide_idle_minutes > 0 ? 'dm.ret.live' : 'dm.ret.liveEnded', {
                  i: span(stats.live.hide_idle_minutes),
                  e: span(stats.live.keep_ended_minutes),
                  h: stats.sessions_hidden ?? 0,
                })}
              </li>
            )}
            <li>
              {t('dm.ret.sessions', { n: stats.retention.sessions_days })}
              {stats.retention.follow_transcripts && t('dm.ret.follow')}
            </li>
            <li>{t('dm.ret.archive')}</li>
            <li>{t('dm.ret.events', { e: stats.retention.events_days, q: stats.retention.quota_days })}</li>
            <li>
              {t('dm.ret.tokens', {
                d: stats.retention.token_detail_days,
                h: stats.retention.token_hourly_days,
                y: Math.round(stats.retention.token_daily_days / 365),
              })}
            </li>
            <li>{t('dm.ret.sweep')}</li>
          </ul>
          <Cleanup
            ended={Math.max(0, (stats.db?.sessions ?? 0) - stats.sessions_live - (stats.sessions_hidden ?? 0))}
            onDone={() => void refresh()}
          />
        </motion.section>
      )}
    </div>
  )
}
