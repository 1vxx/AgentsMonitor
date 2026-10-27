import { useEffect, useMemo, useState } from 'react'
import { motion } from 'motion/react'
import { agentInfo, windowLabel } from '../agents.ts'
import { api } from '../api.ts'
import { dateTime, pct, until } from '../format.ts'
import { useT, type Key } from '../i18n/index.ts'
import { useLive } from '../store.ts'
import type { QuotaSample } from '../types.ts'
import { AgentBadge, Empty, Seg } from '../components/bits.tsx'
import { Dial } from '../components/Dial.tsx'
import { rise } from '../components/motion.tsx'
import { QuotaChart } from '../components/QuotaChart.tsx'

const RANGES: readonly { id: number; name: Key }[] = [
  { id: 24, name: 'range.24h' },
  { id: 168, name: 'range.7d' },
  { id: 720, name: 'range.30d' },
]
const VIEWS = [
  { id: 'chart', name: 'qv.chart' },
  { id: 'table', name: 'qv.table' },
] as const

export const QuotaView = ({ now }: { now: number }) => {
  const quotas = useLive(s => s.quotas)
  const [hours, setHours] = useState(168)
  const [samples, setSamples] = useState<QuotaSample[]>([])
  const [view, setView] = useState<'chart' | 'table'>('chart')
  const asTable = view === 'table'
  const [error, setError] = useState<string>()
  const t = useT()

  // Reload when the range changes or a new reading arrives
  const latest = useMemo(() => Math.max(0, ...[...quotas.values()].map(q => q.updated_at)), [quotas])
  useEffect(() => {
    api
      .quotaHistory(hours)
      .then(list => {
        setSamples(list)
        setError(undefined)
      })
      .catch(e => setError(String(e)))
  }, [hours, latest])

  const agents = useMemo(() => [...new Set([...quotas.keys(), ...samples.map(s => s.agent)])].sort(), [quotas, samples])
  const to = now
  const from = now - hours * 3_600_000

  return (
    <div className="quota-view">
      <motion.div className="section-head" {...rise(1)}>
        <div className="section-lead">
          <span className="kicker">
            <b>02</b> Rate limits
          </span>
          <h2 className="section-title">{t('qv.title')}</h2>
        </div>
        <div className="toolbar">
          <Seg label={t('tok.range')} value={hours} onChange={setHours} options={RANGES.map(r => ({ id: r.id, label: t(r.name) }))} />
          <Seg label={t('qv.view')} value={view} onChange={setView} options={VIEWS.map(v => ({ id: v.id, label: t(v.name) }))} />
        </div>
      </motion.div>
      {error && <div className="error">{error}</div>}
      {agents.length === 0 && <Empty title={t('qv.empty')}>{t('qv.emptyBody')}</Empty>}
      {agents.map((agent, i) => {
        const q = quotas.get(agent)
        const mine = samples.filter(s => s.agent === agent)
        return (
          <motion.section className="panel" key={agent} {...rise(i + 2)}>
            <div className="panel-head">
              <AgentBadge agent={agent} full />
              {q && <span className="muted small">{t('qv.updated', { t: dateTime(q.updated_at) })}</span>}
            </div>
            {q && (
              <div className="dials dials-wide">
                {q.windows.map(w => (
                  <Dial key={w.kind} value={w.percent_used} label={windowLabel(w.kind)} detail={until(w.resets_at, now)} />
                ))}
              </div>
            )}
            {mine.length === 0 ? (
              <div className="footnote">{t('qv.noReadings')}</div>
            ) : asTable ? (
              <div className="table-wrap">
                <table className="table table-compact">
                  <thead>
                    <tr>
                      <th>{t('col.time')}</th>
                      <th>{t('col.window')}</th>
                      <th className="num">{t('col.used')}</th>
                      <th>{t('col.resets')}</th>
                    </tr>
                  </thead>
                  <tbody>
                    {[...mine].reverse().map((s, i) => (
                      <tr key={i}>
                        <td className="nowrap">{dateTime(s.ts)}</td>
                        <td>{windowLabel(s.kind)}</td>
                        <td className="num">{pct(s.percent_used)}</td>
                        <td className="nowrap">{s.resets_at ? dateTime(Date.parse(s.resets_at)) : '—'}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            ) : (
              <QuotaChart samples={mine} from={from} to={to} />
            )}
            <p className="footnote">{t('qv.footnote', { agent: agentInfo(agent).label })}</p>
          </motion.section>
        )
      })}
    </div>
  )
}
