import { useEffect, useMemo, useState } from 'react'
import { agentInfo, windowLabel } from '../agents.ts'
import { api } from '../api.ts'
import { dateTime, pct, until } from '../format.ts'
import { useLive } from '../store.ts'
import type { QuotaSample } from '../types.ts'
import { AgentBadge, Empty, Meter } from '../components/bits.tsx'
import { QuotaChart } from '../components/QuotaChart.tsx'

const RANGES = [
  { hours: 24, label: '24 小时' },
  { hours: 168, label: '7 天' },
  { hours: 720, label: '30 天' },
]

export const QuotaView = ({ now }: { now: number }) => {
  const quotas = useLive(s => s.quotas)
  const [hours, setHours] = useState(168)
  const [samples, setSamples] = useState<QuotaSample[]>([])
  const [asTable, setAsTable] = useState(false)
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
      <div className="section-head">
        <h2>额度</h2>
        <div className="toolbar">
          <div className="seg" role="group" aria-label="时间范围">
            {RANGES.map(r => (
              <button key={r.hours} className={hours === r.hours ? 'on' : ''} onClick={() => setHours(r.hours)}>
                {r.label}
              </button>
            ))}
          </div>
          <button className="btn btn-ghost" onClick={() => setAsTable(v => !v)}>
            {asTable ? '图表' : '表格'}
          </button>
        </div>
      </div>
      {error && <div className="error">{error}</div>}
      {agents.length === 0 && <Empty title="暂无额度数据">订阅账号的 5 小时 / 7 天额度会在会话收到回复后上报。</Empty>}
      {agents.map(agent => {
        const q = quotas.get(agent)
        const mine = samples.filter(s => s.agent === agent)
        return (
          <section className="panel" key={agent}>
            <div className="panel-head">
              <AgentBadge agent={agent} full />
              {q && <span className="muted small">更新于 {dateTime(q.updated_at)}</span>}
            </div>
            {q && (
              <div className="quota-windows quota-windows-wide">
                {q.windows.map(w => (
                  <Meter key={w.kind} value={w.percent_used} label={windowLabel(w.kind)} detail={until(w.resets_at, now)} />
                ))}
              </div>
            )}
            {mine.length === 0 ? (
              <div className="muted small">这个时间范围内没有读数。</div>
            ) : asTable ? (
              <div className="table-wrap">
                <table className="table">
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
            <div className="muted small">{agentInfo(agent).label} 账号级额度：同一账号下所有会话共享。</div>
          </section>
        )
      })}
    </div>
  )
}
