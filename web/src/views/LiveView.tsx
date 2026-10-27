import { useEffect, useMemo, useRef, useState } from 'react'
import type { FocusEvent } from 'react'
import { AnimatePresence, motion } from 'motion/react'
import { agentInfo } from '../agents.ts'
import { clock } from '../format.ts'
import { rich, t, useT } from '../i18n/index.ts'
import type { Key } from '../i18n/index.ts'
import { useLive } from '../store.ts'
import type { Session } from '../types.ts'
import { AgentBadge, Empty, Seg, sessionTitle } from '../components/bits.tsx'
import { describe } from '../components/events.tsx'
import { EASE, Numeral, rise } from '../components/motion.tsx'
import { SessionCard } from '../components/SessionCard.tsx'

// The board's bands, top to bottom: what waits on you, what works, what rests;
// a session without a signal goes last whatever its status says
const BANDS = ['waiting', 'working', 'idle', 'stale'] as const
type Band = (typeof BANDS)[number]
const BAND_LABEL: Record<Band, Key> = { waiting: 'stat.waiting', working: 'stat.working', idle: 'stat.idle', stale: 'status.stale' }

const band = (s: Session): Band =>
  s.stale ? 'stale' : s.status === 'waiting_permission' || s.status === 'waiting_input' ? 'waiting' : s.status === 'idle' ? 'idle' : 'working'
const rank = (b: Band) => BANDS.indexOf(b)

// Within a band, a moment that moves only on a transition, never per tool call:
// the longest wait first, then the newest turn, the latest to settle, the last heard from
const moment = (s: Session): number => {
  switch (band(s)) {
    case 'waiting':
      return s.attention?.since ?? s.status_since
    case 'working':
      return -(s.turn?.started_at ?? s.subagents?.[0]?.started_at ?? s.status_since)
    case 'idle':
      return -s.status_since
    default:
      return -s.last_seen
  }
}

// The same order as the daemon's summary (summary.rs)
const byBoard = (a: Session, b: Session) =>
  rank(band(a)) - rank(band(b)) || moment(a) - moment(b) || b.started_at - a.started_at || (a.key < b.key ? -1 : a.key > b.key ? 1 : 0)

/**
 * Holds the cards where they stand while the pointer or focus is among them, so
 * none slides out from under a click; the new order lands once both have left.
 * A session that arrives meanwhile joins the end of its band.
 */
const useHold = (sessions: Session[]) => {
  const [held, setHeld] = useState<Map<string, [band: Band, at: number]> | null>(null)
  const inside = useRef({ pointer: false, focus: false })
  const placed = useMemo(() => {
    const at = (s: Session) => held?.get(s.key)
    return sessions
      .map(s => ({ s, band: at(s)?.[0] ?? band(s) }))
      .sort((a, b) => rank(a.band) - rank(b.band) || (at(a.s)?.[1] ?? Infinity) - (at(b.s)?.[1] ?? Infinity))
  }, [sessions, held])
  // An emptied board takes the grid away before the pointer can leave it
  const empty = sessions.length === 0
  useEffect(() => {
    if (!empty) return
    inside.current = { pointer: false, focus: false }
    setHeld(null)
  }, [empty])
  const grip =(where: 'pointer' | 'focus', on: boolean) => {
    inside.current[where] = on
    const any = inside.current.pointer || inside.current.focus
    setHeld(h => (any ? (h ?? new Map(placed.map((p, i) => [p.s.key, [p.band, i]]))) : null))
  }
  const handlers = {
    onPointerEnter: () => grip('pointer', true),
    onPointerLeave: () => grip('pointer', false),
    onFocus: () => grip('focus', true),
    onBlur: (e: FocusEvent) => !e.currentTarget.contains(e.relatedTarget) && grip('focus', false),
  }
  return { placed, handlers }
}

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
                    <span className="feed-project">{s ? sessionTitle(s) : r.key.split(':')[1]?.slice(0, 8)}</span>
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
      live: shown.filter(s => s.status !== 'ended').sort(byBoard),
      ended: shown.filter(s => s.status === 'ended').sort((a, b) => (b.ended_at ?? 0) - (a.ended_at ?? 0)),
    }
  }, [sessions, agent])
  const { placed, handlers } = useHold(live)
  // Bands are named only when there is more than one to tell apart
  const sizes = placed.reduce((m, p) => m.set(p.band, (m.get(p.band) ?? 0) + 1), new Map<Band, number>())

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
          <div className="cards" {...handlers}>
            <AnimatePresence>
              {placed.flatMap(({ s, band }, i) => [
                ...(sizes.size > 1 && band !== placed[i - 1]?.band
                  ? [
                      <motion.div
                        key={`band-${band}`}
                        className={`band band-${band}`}
                        layout="position"
                        initial={{ opacity: 0 }}
                        animate={{ opacity: 1 }}
                        exit={{ opacity: 0, transition: { duration: 0.2 } }}
                        transition={{ duration: 0.5, ease: EASE, layout: { duration: 0.6, ease: EASE } }}
                      >
                        {t(BAND_LABEL[band])} <span className="count">{sizes.get(band)}</span>
                      </motion.div>,
                    ]
                  : []),
                <SessionCard key={s.key} s={s} now={now} onOpen={onOpen} i={i} />,
              ])}
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
