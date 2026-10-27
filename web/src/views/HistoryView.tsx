import { useCallback, useEffect, useMemo, useState } from 'react'
import { api } from '../api.ts'
import { compact, dateTime, duration, tildify } from '../format.ts'
import { useLive } from '../store.ts'
import type { Session } from '../types.ts'
import { AgentBadge, Empty, StatusPill, statusView } from '../components/bits.tsx'
import { sessionTokens } from '../components/SessionCard.tsx'
import { AgentFilter } from './LiveView.tsx'

const PAGE = 50

export const HistoryView = ({ onOpen }: { onOpen: (key: string) => void }) => {
  const live = useLive(s => s.sessions)
  const [rows, setRows] = useState<Session[]>([])
  const [done, setDone] = useState(false)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string>()
  const [agent, setAgent] = useState('')
  const [q, setQ] = useState('')

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
      ? merged.filter(s => [s.meta.project, s.meta.cwd, s.session_id, s.last_prompt].some(v => v?.toLowerCase().includes(needle)))
      : merged
  }, [rows, live, q])

  return (
    <div className="history">
      <div className="section-head">
        <h2>全部会话</h2>
        <div className="toolbar">
          <AgentFilter agents={agents} value={agent} onChange={setAgent} />
          <input className="search" placeholder="搜索项目、目录、提示…" value={q} onChange={e => setQ(e.target.value)} aria-label="搜索" />
        </div>
      </div>
      {error && <div className="error">{error}</div>}
      {shown.length === 0 && !loading ? (
        <Empty title="没有记录" />
      ) : (
        <div className="table-wrap">
          <table className="table">
            <thead>
              <tr>
                <th>项目</th>
                <th>状态</th>
                <th>开始</th>
                <th>最近活动</th>
                <th className="num">回合</th>
                <th className="num">工具</th>
                <th className="num">输入 / 输出</th>
                <th className="num">Tokens 合计</th>
                <th className="num">工作时长</th>
              </tr>
            </thead>
            <tbody>
              {shown.map(s => (
                <tr key={s.key} onClick={() => onOpen(s.key)} tabIndex={0} onKeyDown={e => e.key === 'Enter' && onOpen(s.key)}>
                  <td>
                    <div className="cell-project">
                      <AgentBadge agent={s.agent} />
                      <div>
                        <div className="strong">{s.meta.project ?? s.session_id.slice(0, 8)}</div>
                        <div className="muted small mono">{tildify(s.meta.cwd)}</div>
                      </div>
                    </div>
                  </td>
                  <td>
                    <StatusPill view={statusView(s)} />
                  </td>
                  <td className="nowrap">{dateTime(s.started_at)}</td>
                  <td className="nowrap">{dateTime(s.updated_at)}</td>
                  <td className="num">{s.stats.turns}</td>
                  <td className="num">{s.stats.tool_calls}</td>
                  <td className="num nowrap">
                    {compact(s.stats.input_tokens)} / {compact(s.stats.output_tokens)}
                  </td>
                  <td className="num nowrap" title={s.stats.exact_tokens ? '逐请求精确计数' : '按回合估算'}>
                    {s.stats.exact_tokens ? '' : '≈'}
                    {compact(sessionTokens(s))}
                  </td>
                  <td className="num nowrap">{duration(s.stats.busy_ms)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
      {!done && (
        <button className="btn" disabled={loading} onClick={() => void load(rows.length)}>
          {loading ? '加载中…' : '加载更多'}
        </button>
      )}
    </div>
  )
}
