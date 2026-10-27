import { useCallback, useEffect, useMemo, useState } from 'react'
import { motion } from 'motion/react'
import { api } from '../api.ts'
import { compact, dateTime, duration } from '../format.ts'
import { useT } from '../i18n/index.ts'
import { useLive } from '../store.ts'
import type { Session } from '../types.ts'
import { AgentBadge, Empty, SearchIcon, StatusPill, sessionPlace, sessionTitle, statusView } from '../components/bits.tsx'
import { Numeral, rise } from '../components/motion.tsx'
import { sessionTokens } from '../components/SessionCard.tsx'
import { AgentFilter } from './LiveView.tsx'

const PAGE = 50

const SkeletonRows = () => (
  <>
    {Array.from({ length: 6 }, (_, i) => (
      <tr key={i} className="row-skeleton">
        <td>
          <span className="skeleton" style={{ width: 180, height: 14 }} />
        </td>
        {Array.from({ length: 8 }, (_, j) => (
          <td key={j}>
            <span className="skeleton" style={{ width: j === 0 ? 70 : 48, height: 12 }} />
          </td>
        ))}
      </tr>
    ))}
  </>
)

export const HistoryView = ({ onOpen }: { onOpen: (key: string) => void }) => {
  const live = useLive(s => s.sessions)
  const [rows, setRows] = useState<Session[]>([])
  const [done, setDone] = useState(false)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string>()
  const [agent, setAgent] = useState('')
  const [q, setQ] = useState('')
  const t = useT()

  const load = useCallback(
    async (offset: number) => {
      setLoading(true)
      try {
        const page = await api.history({ agent: agent || undefined, limit: PAGE, offset })
        setRows(prev => (offset === 0 ? page : [...prev, ...page]))
        setDone(page.length < PAGE)
        setError(undefined)
      } catch (e) {
        setError(String(e))
      } finally {
        setLoading(false)
      }
    },
    [agent],
  )

  useEffect(() => {
    void load(0)
  }, [load])

  const agents = useMemo(() => [...new Set([...live.values()].map(s => s.agent))].sort(), [live])
  const shown = useMemo(() => {
    const needle = q.trim().toLowerCase()
    // Live copies are fresher than the page
    const merged = rows.map(r => live.get(r.key) ?? r)
    return needle
      ? merged.filter(s => [s.meta.title, s.meta.project, s.meta.cwd, s.session_id, s.last_prompt].some(v => v?.toLowerCase().includes(needle)))
      : merged
  }, [rows, live, q])

  const first = loading && rows.length === 0

  return (
    <div className="history">
      <motion.div className="section-head" {...rise(0)}>
        <div className="section-lead">
          <span className="kicker">
            <b>01</b> Archive
          </span>
          <h2 className="section-title">
            {t('hist.title')} {!first && <Numeral className="count" value={shown.length} />}
          </h2>
        </div>
        <div className="toolbar">
          <AgentFilter agents={agents} value={agent} onChange={setAgent} />
          <label className="field">
            <SearchIcon />
            <input placeholder={t('hist.search')} value={q} onChange={e => setQ(e.target.value)} aria-label={t('common.search')} />
          </label>
        </div>
      </motion.div>
      {error && <div className="error">{error}</div>}
      {shown.length === 0 && !loading ? (
        <Empty title={q ? t('hist.noMatch', { q }) : t('hist.empty')} />
      ) : (
        <motion.div className="table-wrap" {...rise(1)}>
          <table className="table">
            <thead>
              <tr>
                <th>{t('col.project')}</th>
                <th>{t('col.status')}</th>
                <th>{t('col.started')}</th>
                <th>{t('col.updated')}</th>
                <th className="num">{t('col.turns')}</th>
                <th className="num">{t('col.tools')}</th>
                <th className="num">{t('col.io')}</th>
                <th className="num">{t('col.total')}</th>
                <th className="num">{t('col.busy')}</th>
              </tr>
            </thead>
            <tbody>
              {first ? (
                <SkeletonRows />
              ) : (
                shown.map((s, i) => (
                  <tr
                    key={s.key}
                    className="row-in"
                    style={{ ['--i' as string]: Math.min(i, 20) }}
                    onClick={() => onOpen(s.key)}
                    tabIndex={0}
                    onKeyDown={e => e.key === 'Enter' && onOpen(s.key)}
                  >
                    <td>
                      <div className="cell-project">
                        <AgentBadge agent={s.agent} />
                        <div className="cell-project-text">
                          <div className="cell-title">{sessionTitle(s)}</div>
                          <div className="cell-path mono" title={s.meta.cwd}>
                            {sessionPlace(s)}
                          </div>
                        </div>
                      </div>
                    </td>
                    <td>
                      <StatusPill view={statusView(s)} />
                    </td>
                    <td className="nowrap date">{dateTime(s.started_at)}</td>
                    <td className="nowrap date">{dateTime(s.updated_at)}</td>
                    <td className="num">{s.stats.turns}</td>
                    <td className="num">{s.stats.tool_calls}</td>
                    <td className="num nowrap">
                      {compact(s.stats.input_tokens)} <span className="muted">/</span> {compact(s.stats.output_tokens)}
                    </td>
                    <td className="num nowrap strong" title={t(s.stats.exact_tokens ? 'hist.exact' : 'hist.estimate')}>
                      {s.stats.exact_tokens ? '' : '≈'}
                      {compact(sessionTokens(s))}
                    </td>
                    <td className="num nowrap">{duration(s.stats.busy_ms)}</td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </motion.div>
      )}
      {!done && !first && (
        <button className="link-btn" disabled={loading} onClick={() => void load(rows.length)}>
          {t(loading ? 'hist.loading' : 'hist.more')}
        </button>
      )}
    </div>
  )
}
