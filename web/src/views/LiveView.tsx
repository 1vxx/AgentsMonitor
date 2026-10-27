import { useEffect, useMemo, useRef, useState } from 'react'
import { AnimatePresence, motion } from 'motion/react'
import { agentInfo } from '../agents.ts'
import { clock } from '../format.ts'
import { rich, t, useT } from '../i18n/index.ts'
import { useLive } from '../store.ts'
import type { Session, Status } from '../types.ts'
import { AgentBadge, Empty, Seg } from '../components/bits.tsx'
import { describe } from '../components/events.tsx'
import { EASE, Numeral, rise } from '../components/motion.tsx'
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

export const AgentFilter = ({ agents, value, onChange }: { agents: string[]; value: string; onChange: (a: string) => void }) => {
  useT()
  return agents.length > 1 ? (
    <Seg label={t('live.filter')} value={value} onChange={onChange} options={[{ id: '', label: t('live.all') }, ...agents.map(a => ({ id: a, label: agentInfo(a).label }))]} />
  ) : null
}

/** The margin column: newest events slide in at the top and push the rest down. */
const Feed = ({ onOpen }: { onOpen: (key: string) => void }) => {
  const feed = useLive(s => s.feed)
  const sessions = useLive(s => s.sessions)
  const conn = useLive(s => s.conn)
  useT()
  const first = useRef(true)
  const list = useRef<HTMLOListElement>(null)
  // Which ends have more above or below them, so they can fade out
  const [edge, setEdge] = useState({ top: false, end: false })
  const measure = () => {
    const el = list.current
    if (!el) return
    const top = el.scrollTop > 4
    const end = el.scrollTop + el.clientHeight < el.scrollHeight - 4
    setEdge(e => (e.top === top && e.end === end ? e : { top, end }))
  }
  useEffect(() => {
    first.current = false
  }, [])
  useEffect(measure, [feed])

  // Headed like the sessions beside it, so both columns start on one line
  return (
    <motion.aside className="live-side" aria-label={t('live.feed')} {...rise(3)}>
      <div className="section-head">
        <div className="section-lead">
          <span className="kicker">
            <b>03</b> Telemetry
          </span>
          <h2 className="section-title">{t('live.feed')}</h2>
        </div>
        <span className="feed-live">
          <span className={`live-dot conn-${conn}`} aria-hidden />
          {t(conn === 'open' ? 'live.live' : 'live.reconnecting')}
        </span>
      </div>
      <div className={`live-feed${edge.top ? ' more-above' : ''}${edge.end ? ' more-below' : ''}`}>
        <motion.ol ref={list} className="feed" layoutScroll onScroll={measure}>
          {feed.slice(0, 80).map((r, i) => {
            const d = describe(r)
            const s = sessions.get(r.key)
            return (
              <motion.li
                key={r.seq}
                layout="position"
                className={`feed-row tone-${d.tone}`}
                onClick={() => onOpen(r.key)}
                initial={first.current ? { opacity: 0, y: 8 } : { opacity: 0, x: -14 }}
                animate={{ opacity: 1, x: 0, y: 0 }}
                transition={{ duration: 0.55, ease: EASE, delay: first.current ? Math.min(i, 14) * 0.035 : 0, layout: { duration: 0.45, ease: EASE, delay: 0 } }}
              >
                <time>{clock(r.ts)}</time>
                <span className="feed-dot" aria-hidden />
                <span className="feed-body">
                  <span className="feed-line">
                    <span className="feed-project">{s?.meta.project ?? r.key.split(':')[1]?.slice(0, 8)}</span>
                    <AgentBadge agent={r.agent} />
                  </span>
                  <span className="feed-text">
                    <span className="feed-kind">{d.kind}</span> {d.text}
                  </span>
                  {d.detail && <span className="feed-detail mono">{d.detail}</span>}
                </span>
              </motion.li>
            )
          })}
          {feed.length === 0 && <li className="tl-empty">{t('live.waiting')}</li>}
        </motion.ol>
      </div>
    </motion.aside>
  )
}

export const LiveView = ({ now, onOpen }: { now: number; onOpen: (key: string) => void }) => {
  const sessions = useLive(s => s.sessions)
  useT()
  const conn = useLive(s => s.conn)
  const [agent, setAgent] = useState('')
  const [showEnded, setShowEnded] = useState(false)

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
        <motion.div className="section-head" {...rise(2)}>
          <div className="section-lead">
            <span className="kicker">
              <b>02</b> Sessions in orbit
            </span>
            <h2 className="section-title">
              {t('live.sessions')} <Numeral className="count" value={live.length} />
            </h2>
          </div>
          <AgentFilter agents={agents} value={agent} onChange={setAgent} />
        </motion.div>
        {live.length === 0 ? (
          <Empty title={t(conn === 'open' ? 'live.none' : 'live.noDaemon')}>
            {conn === 'open' ? (
              t('live.noneBody')
            ) : (
              rich(t('live.noDaemonBody'))
            )}
          </Empty>
        ) : (
          <div className="cards">
            <AnimatePresence>
              {live.map((s, i) => (
                <SessionCard key={s.key} s={s} now={now} onOpen={onOpen} i={i} />
              ))}
            </AnimatePresence>
          </div>
        )}

        {ended.length > 0 && (
          <section className="ended">
            <button className="ended-toggle" onClick={() => setShowEnded(v => !v)} aria-expanded={showEnded}>
              <motion.svg viewBox="0 0 12 12" animate={{ rotate: showEnded ? 90 : 0 }} transition={{ duration: 0.3, ease: EASE }} aria-hidden>
                <path d="M4 2.5 7.5 6 4 9.5" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round" />
              </motion.svg>
              {t('live.ended')} <span className="count">{ended.length}</span>
            </button>
            <AnimatePresence initial={false}>
              {showEnded && (
                <motion.div
                  className="ended-body"
                  initial={{ height: 0, opacity: 0 }}
                  animate={{ height: 'auto', opacity: 1 }}
                  exit={{ height: 0, opacity: 0 }}
                  transition={{ duration: 0.5, ease: EASE }}
                >
                  <div className="cards cards-dim">
                    {ended.map((s, i) => (
                      <SessionCard key={s.key} s={s} now={now} onOpen={onOpen} i={i} />
                    ))}
                  </div>
                </motion.div>
              )}
            </AnimatePresence>
          </section>
        )}
      </div>

      <Feed onOpen={onOpen} />
    </div>
  )
}
