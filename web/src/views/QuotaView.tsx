import { useEffect, useMemo, useState } from 'react'
import { motion } from 'motion/react'
import { agentInfo, windowLabel } from '../agents.ts'
import { api } from '../api.ts'
import { dateTime, pct, until } from '../format.ts'
import { useLive } from '../store.ts'
import type { QuotaSample } from '../types.ts'
import { AgentBadge, Empty, Seg } from '../components/bits.tsx'
import { Dial } from '../components/Dial.tsx'
import { rise } from '../components/motion.tsx'
import { QuotaChart } from '../components/QuotaChart.tsx'

const RANGES = [
  { id: 24, label: '24 小时' },
  { id: 168, label: '7 天' },
  { id: 720, label: '30 天' },
]
const VIEWS = [
  { id: 'chart', label: '图表' },
  { id: 'table', label: '表格' },
] as const

export const QuotaView = ({ now }: { now: number }) => {
  const quotas = useLive(s => s.quotas)
  const [hours, setHours] = useState(168)
  const [samples, setSamples] = useState<QuotaSample[]>([])
  const [view, setView] = useState<'chart' | 'table'>('chart')
  const asTable = view === 'table'
  const [error, setError] = useState<string>()

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
          <h2 className="section-title">额度</h2>
        </div>
        <div className="toolbar">
          <Seg label="时间范围" value={hours} onChange={setHours} options={RANGES} />
          <Seg label="显示方式" value={view} onChange={setView} options={VIEWS} />
        </div>
      </motion.div>
      {error && <div className="error">{error}</div>}
      {agents.length === 0 && <Empty title="暂无额度数据">订阅账号的 5 小时 / 7 天额度会在会话收到回复后上报。</Empty>}
      {agents.map((agent, i) => {
        const q = quotas.get(agent)
        const mine = samples.filter(s => s.agent === agent)
        return (
          <motion.section className="panel" key={agent} {...rise(i + 2)}>
            <div className="panel-head">
              <AgentBadge agent={agent} full />
              {q && <span className="muted small">更新于 {dateTime(q.updated_at)}</span>}
            </div>
            {q && (
              <div className="dials dials-wide">
                {q.windows.map(w => (
                  <Dial key={w.kind} value={w.percent_used} label={windowLabel(w.kind)} detail={until(w.resets_at, now)} />
                ))}
              </div>
            )}
            {mine.length === 0 ? (
              <div className="footnote">这个时间范围内没有读数。</div>
            ) : asTable ? (
              <div className="table-wrap">
                <table className="table table-compact">
                  <thead>
                    <tr>
                      <th>时间</th>
                      <th>窗口</th>
                      <th className="num">已用</th>
                      <th>重置于</th>
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
            <p className="footnote">{agentInfo(agent).label} 账号级额度：同一账号下所有会话共享。</p>
          </motion.section>
        )
      })}
    </div>
  )
}
