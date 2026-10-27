import { useEffect, useMemo, useState } from 'react'
import { motion } from 'motion/react'
import { api } from '../api.ts'
import { clock } from '../format.ts'
import { t, useT, type Key } from '../i18n/index.ts'
import { seedFeed, useLive } from '../store.ts'
import { AgentBadge, Empty, Seg, sessionTitle } from '../components/bits.tsx'
import { describe } from '../components/events.tsx'
import { rise } from '../components/motion.tsx'

const GROUPS = [
  { id: '', name: 'grp.all', types: [] },
  { id: 'ask', name: 'grp.ask', types: ['attention'] },
  { id: 'turn', name: 'grp.turn', types: ['prompt', 'turn.start', 'turn.end'] },
  { id: 'tool', name: 'grp.tool', types: ['tool.start', 'tool.end'] },
  { id: 'session', name: 'grp.session', types: ['session.start', 'session.end', 'compact.start', 'compact.end', 'notice'] },
] as const satisfies readonly { id: string; name: Key; types: readonly string[] }[]
type Group = (typeof GROUPS)[number]['id']

/** "14:32" for the first event of each minute, so the list reads in paragraphs. */
const minute = (ts: number) => clock(ts).slice(0, 5)

export const ActivityView = ({ onOpen }: { onOpen: (key: string) => void }) => {
  const feed = useLive(s => s.feed)
  const sessions = useLive(s => s.sessions)
  const [group, setGroup] = useState<Group>('')
  const [error, setError] = useState<string>()
  useT()

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
          <span className="kicker">
            <b>01</b> Event stream
          </span>
          <h2 className="section-title">{t('act.title')}</h2>
        </div>
        <Seg label={t('act.filter')} value={group} onChange={setGroup} options={GROUPS.map(g => ({ id: g.id, label: t(g.name) }))} />
      </motion.div>
      {error && <div className="error">{error}</div>}
      {shown.length === 0 ? (
        <Empty title={t('drawer.noEvents')} />
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
                <span className="feed-project">{s ? sessionTitle(s) : r.key.split(':')[1]?.slice(0, 8)}</span>
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
