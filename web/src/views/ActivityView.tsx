import { useEffect, useMemo, useState } from 'react'
import { api } from '../api.ts'
import { clock } from '../format.ts'
import { seedFeed, useLive } from '../store.ts'
import { AgentBadge, Empty } from '../components/bits.tsx'
import { describe } from '../components/events.tsx'

const GROUPS: { id: string; label: string; types: string[] }[] = [
  { id: '', label: '全部', types: [] },
  { id: 'ask', label: '需要你', types: ['attention'] },
  { id: 'turn', label: '回合', types: ['prompt', 'turn.start', 'turn.end'] },
  { id: 'tool', label: '工具', types: ['tool.start', 'tool.end'] },
  { id: 'session', label: '会话', types: ['session.start', 'session.end', 'compact.start', 'compact.end', 'notice'] },
]

export const ActivityView = ({ onOpen }: { onOpen: (key: string) => void }) => {
  const feed = useLive(s => s.feed)
  const sessions = useLive(s => s.sessions)
  const [group, setGroup] = useState('')
  const [error, setError] = useState<string>()

  useEffect(() => {
    api.events().then(seedFeed).catch(e => setError(String(e)))
  }, [])

  const shown = useMemo(() => {
    const types = GROUPS.find(g => g.id === group)?.types ?? []
    return types.length ? feed.filter(r => types.includes(r.type)) : feed
  }, [feed, group])

  return (
    <div className="activity-view">
      <div className="section-head">
        <h2>事件流</h2>
        <div className="seg" role="group" aria-label="按类型筛选">
          {GROUPS.map(g => (
            <button key={g.id} className={group === g.id ? 'on' : ''} onClick={() => setGroup(g.id)}>
              {g.label}
            </button>
          ))}
        </div>
      </div>
      {error && <div className="error">{error}</div>}
      {shown.length === 0 ? (
        <Empty title="还没有事件" />
      ) : (
        <ol className="feed feed-wide">
          {shown.map(r => {
            const d = describe(r)
            const s = sessions.get(r.key)
            return (
              <li key={r.seq} className={`feed-row tone-${d.tone}`} onClick={() => onOpen(r.key)}>
                <time>{clock(r.ts)}</time>
                <AgentBadge agent={r.agent} />
                <span className="feed-project">{s?.meta.project ?? r.key.split(':')[1]?.slice(0, 8)}</span>
                <span className="feed-kind">{d.kind}</span>
                <span className="feed-text">
                  {d.text}
                  {d.detail && <span className="feed-detail mono">{d.detail}</span>}
                </span>
              </li>
            )
          })}
        </ol>
      )}
    </div>
  )
}
