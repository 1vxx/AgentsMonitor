import { useMemo, useState } from 'react'
import { agentInfo } from '../agents.ts'
import { clock } from '../format.ts'
import { useLive } from '../store.ts'
import type { Session, Status } from '../types.ts'
import { AgentBadge, Empty } from '../components/bits.tsx'
import { describe } from '../components/events.tsx'
import { SessionCard } from '../components/SessionCard.tsx'

const URGENCY: Record<Status, number> = {
  waiting_permission: 6,
  waiting_input: 5,
  tool: 4,
  thinking: 3,
  compacting: 2,
  idle: 1,
  ended: 0,
}

// Most urgent first; among equals, the one in its state longest
const byUrgency = (a: Session, b: Session) =>
  Number(a.stale) - Number(b.stale) || URGENCY[b.status] - URGENCY[a.status] || a.status_since - b.status_since

export const AgentFilter = ({ agents, value, onChange }: { agents: string[]; value: string; onChange: (a: string) => void }) =>
  agents.length > 1 ? (
    <div className="seg" role="group" aria-label="按代理筛选">
      <button className={value === '' ? 'on' : ''} onClick={() => onChange('')}>
        全部
      </button>
      {agents.map(a => (
        <button key={a} className={value === a ? 'on' : ''} onClick={() => onChange(a)}>
          {agentInfo(a).label}
        </button>
      ))}
    </div>
  ) : null

export const LiveView = ({ now, onOpen }: { now: number; onOpen: (key: string) => void }) => {
  const sessions = useLive(s => s.sessions)
  const feed = useLive(s => s.feed)
  const conn = useLive(s => s.conn)
  const [agent, setAgent] = useState('')

  const { live, ended, agents } = useMemo(() => {
    const all = [...sessions.values()]
    const agents = [...new Set(all.map(s => s.agent))].sort()
    const shown = all.filter(s => !agent || s.agent === agent)
    return {
      agents,
      live: shown.filter(s => s.status !== 'ended').sort(byUrgency),
      ended: shown.filter(s => s.status === 'ended').sort((a, b) => (b.ended_at ?? 0) - (a.ended_at ?? 0)),
    }
  }, [sessions, agent])

  return (
    <div className="live">
      <div className="live-main">
        <div className="section-head">
          <h2>
            活动会话 <span className="count">{live.length}</span>
          </h2>
          <AgentFilter agents={agents} value={agent} onChange={setAgent} />
        </div>
        {live.length === 0 ? (
          <Empty title={conn === 'open' ? '现在没有活动会话' : '守护进程未连接'}>
            {conn === 'open' ? (
              <>启动一个装有 agentsmon 插件的 Claude Code 会话，它会立刻出现在这里。</>
            ) : (
              <>
                先运行 <code>agentsmon serve</code>（或 <code>agentsmon service install</code>），页面会自动重连。
              </>
            )}
          </Empty>
        ) : (
          <div className="cards">
            {live.map(s => (
              <SessionCard key={s.key} s={s} now={now} onOpen={onOpen} />
            ))}
          </div>
        )}

        {ended.length > 0 && (
          <details className="ended">
            <summary>
              最近结束 <span className="count">{ended.length}</span>
            </summary>
            <div className="cards cards-dim">
              {ended.map(s => (
                <SessionCard key={s.key} s={s} now={now} onOpen={onOpen} />
              ))}
            </div>
          </details>
        )}
      </div>

      <aside className="live-feed" aria-label="实时动态">
        <div className="section-head">
          <h2>实时动态</h2>
        </div>
        <ol className="feed">
          {feed.slice(0, 80).map(r => {
            const d = describe(r)
            const s = sessions.get(r.key)
            return (
              <li key={r.seq} className={`feed-row tone-${d.tone}`} onClick={() => onOpen(r.key)}>
                <time>{clock(r.ts)}</time>
                <AgentBadge agent={r.agent} />
                <span className="feed-project">{s?.meta.project ?? r.key.split(':')[1]?.slice(0, 8)}</span>
                <span className="feed-text">
                  <span className="feed-kind">{d.kind}</span> {d.text}
                  {d.detail && <span className="feed-detail mono">{d.detail}</span>}
                </span>
              </li>
            )
          })}
          {feed.length === 0 && <li className="tl-empty">等待事件…</li>}
        </ol>
      </aside>
    </div>
  )
}
