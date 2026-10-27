import { useEffect, useMemo, useState } from 'react'
import { motion } from 'motion/react'
import { api } from '../api.ts'
import { clock } from '../format.ts'
import { seedFeed, useLive } from '../store.ts'
import { AgentBadge, Empty, Seg } from '../components/bits.tsx'
import { describe } from '../components/events.tsx'
import { rise } from '../components/motion.tsx'

const GROUPS = [
  { id: '', label: '全部', types: [] },
  { id: 'ask', label: '需要你', types: ['attention'] },
  { id: 'turn', label: '回合', types: ['prompt', 'turn.start', 'turn.end'] },
  { id: 'tool', label: '工具', types: ['tool.start', 'tool.end'] },
  { id: 'session', label: '会话', types: ['session.start', 'session.end', 'compact.start', 'compact.end', 'notice'] },
] as const satisfies readonly { id: string; label: string; types: readonly string[] }[]
type Group = (typeof GROUPS)[number]['id']

/** "14:32" for the first event of each minute, so the list reads in paragraphs. */
const minute = (ts: number) => clock(ts).slice(0, 5)

export const ActivityView = ({ onOpen }: { onOpen: (key: string) => void }) => {
  const feed = useLive(s => s.feed)
  const sessions = useLive(s => s.sessions)
  const [group, setGroup] = useState<Group>('')
  const [error, setError] = useState<string>()

  useEffect(() => {
    api.events().then(seedFeed).catch(e => setError(String(e)))
  }, [])

  const shown = useMemo(() => {
    const types: readonly string[] = GROUPS.find(g => g.id === group)?.types ?? []
    return types.length ? feed.filter(r => types.includes(r.type)) : feed
  }, [feed, group])

  return (
    <div className="activity-view">
      <motion.div className="section-head" {...rise(0)}>
        <div className="section-lead">
          <h2 className="section-title">事件流</h2>
        </div>
        <Seg label="按类型筛选" value={group} onChange={setGroup} options={GROUPS} />
      </motion.div>
      {error && <div className="error">{error}</div>}
      {shown.length === 0 ? (
        <Empty title="还没有事件" />
      ) : (
        <motion.ol className="chronicle" key={group} {...rise(1)}>
          {shown.map((r, i) => {
            const d = describe(r)
            const s = sessions.get(r.key)
            const m = minute(r.ts)
            const newMinute = m !== (shown[i - 1] && minute(shown[i - 1]!.ts))
            return (
              <li key={r.seq} className={`chron-row tone-${d.tone}${newMinute ? ' chron-minute' : ''}`} style={{ ['--i' as string]: Math.min(i, 24) }} onClick={() => onOpen(r.key)}>
                <time>{clock(r.ts)}</time>
                <span className="feed-dot" aria-hidden />
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
        </motion.ol>
      )}
    </div>
  )
}
