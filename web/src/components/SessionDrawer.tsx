import { useEffect, useRef, useState } from 'react'
import { motion } from 'motion/react'
import { agentInfo } from '../agents.ts'
import { api } from '../api.ts'
import { clock, compact, dateTime, duration, tildify, useNow } from '../format.ts'
import { useT } from '../i18n/index.ts'
import { onEvent, useLive } from '../store.ts'
import type { EventRecord, Session } from '../types.ts'
import { Icon, Meter, StatusPill, shortModel, shortTool, statusView } from './bits.tsx'
import { describe } from './events.tsx'
import { EASE, Numeral } from './motion.tsx'

const Fact = ({ label, children, mono }: { label: string; children: React.ReactNode; mono?: boolean }) => (
  <div className="fact">
    <dt>{label}</dt>
    <dd className={mono ? 'mono' : undefined}>{children ?? '—'}</dd>
  </div>
)

const Num = ({ v }: { v: number }) => <Numeral value={v} format={n => compact(Math.round(n))} />

// Sections arrive one after another once the sheet has slid in
const sheet = { hidden: {}, show: { transition: { staggerChildren: 0.05, delayChildren: 0.14 } } }
const part = { hidden: { opacity: 0, y: 14 }, show: { opacity: 1, y: 0, transition: { duration: 0.6, ease: EASE } } }

const Loading = ({ error }: { error?: string }) => (
  <div className="drawer-loading">
    {error ? (
      <div className="error">{error}</div>
    ) : (
      <>
        <span className="skeleton" style={{ width: 90, height: 10 }} />
        <span className="skeleton" style={{ width: '60%', height: 30 }} />
        <span className="skeleton" style={{ width: '40%', height: 12 }} />
        <span className="skeleton" style={{ width: '100%', height: 70, marginTop: 20 }} />
        <span className="skeleton" style={{ width: '100%', height: 120 }} />
      </>
    )}
  </div>
)

export const SessionDrawer = ({ sessionKey, onClose }: { sessionKey: string; onClose: () => void }) => {
  const live = useLive(st => st.sessions.get(sessionKey))
  const [stored, setStored] = useState<Session>()
  const [events, setEvents] = useState<EventRecord[]>([])
  const [error, setError] = useState<string>()
  const panel = useRef<HTMLElement>(null)
  const now = useNow()
  const t = useT()
  const s = live ?? stored

  useEffect(() => {
    let alive = true
    setEvents([])
    setError(undefined)
    api
      .sessionEvents(sessionKey)
      .then(list => alive && setEvents(prev => mergeEvents(prev, list)))
      .catch(e => alive && setError(String(e)))
    if (!live) api.session(sessionKey).then(x => alive && setStored(x)).catch(() => {})
    const off = onEvent(r => r.key === sessionKey && setEvents(prev => mergeEvents(prev, [r])))
    return () => {
      alive = false
      off()
    }
    // The live copy is read on each render; loading depends on the key alone
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [sessionKey])

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && onClose()
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [onClose])

  // Focus moves into the sheet and comes back to where it was
  useEffect(() => {
    const before = document.activeElement as HTMLElement | null
    panel.current?.focus({ preventScroll: true })
    return () => before?.focus?.({ preventScroll: true })
  }, [])

  const forget = async () => {
    if (!confirm(t('drawer.forgetConfirm'))) return
    await api.forget(sessionKey).catch(e => setError(String(e)))
    onClose()
  }

  return (
    <motion.div
      className="drawer-backdrop"
      onClick={onClose}
      initial={{ opacity: 0 }}
      animate={{ opacity: 1 }}
      exit={{ opacity: 0, transition: { duration: 0.25, delay: 0.05 } }}
      transition={{ duration: 0.35 }}
    >
      <motion.aside
        ref={panel}
        tabIndex={-1}
        className="drawer"
        role="dialog"
        aria-modal="true"
        aria-label={t('drawer.label')}
        onClick={e => e.stopPropagation()}
        initial={{ x: '104%' }}
        animate={{ x: 0 }}
        exit={{ x: '104%', transition: { duration: 0.32, ease: [0.55, 0, 0.75, 0.2] } }}
        transition={{ type: 'spring', stiffness: 260, damping: 32, mass: 0.9 }}
      >
        {!s ? (
          <Loading error={error} />
        ) : (
          <>
            <header className="drawer-head">
              <div className="drawer-title">
                <div className="label">{t('drawer.kicker', { agent: agentInfo(s.agent).label })}</div>
                <h2>{s.meta.project ?? s.session_id.slice(0, 8)}</h2>
                <span className="drawer-path mono">{tildify(s.meta.cwd) ?? s.session_id}</span>
              </div>
              <button className="icon-btn drawer-close" onClick={onClose} aria-label={t('common.close')}>
                <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" aria-hidden>
                  <path d="m4 4 8 8M12 4l-8 8" />
                </svg>
              </button>
            </header>

            <motion.div className="drawer-body" variants={sheet} initial="hidden" animate="show">
              <motion.div className="drawer-status" variants={part}>
                <StatusPill view={statusView(s)} since={s.status_since} now={now} />
                {shortModel(s.meta.model) && <span className="chip">{shortModel(s.meta.model)}</span>}
                {s.meta.permission_mode && <span className={`chip${s.meta.permission_mode === 'bypassPermissions' ? ' chip-warn' : ''}`}>{s.meta.permission_mode}</span>}
              </motion.div>

              {s.attention && s.attention.kind !== 'idle' && (
                <motion.div className="callout tone-attention" variants={part}>
                  <span className="callout-icon" aria-hidden>
                    <Icon name={s.attention.kind === 'permission' ? 'alert' : 'ask'} />
                  </span>
                  <div className="callout-text">
                    <strong>{t(s.attention.kind === 'permission' ? 'drawer.waitApprove' : 'drawer.waitAnswer')}</strong>
                    <div className="callout-line">
                      {s.attention.tool && <code className="tool">{shortTool(s.attention.tool)}</code>}
                      <span className="mono">{s.attention.subject ?? s.attention.message}</span>
                    </div>
                  </div>
                </motion.div>
              )}

              <motion.section variants={part}>
                <h4 className="sec-title">{t('card.context')}</h4>
                <Meter
                  value={s.context?.percent}
                  label={s.context?.tokens !== undefined ? `${compact(s.context.tokens)} / ${compact(s.context.window ?? 0)} tokens` : t('drawer.noReading')}
                />
              </motion.section>

              {(s.tools?.length ?? 0) + (s.subagents?.length ?? 0) > 0 && (
                <motion.section variants={part}>
                  <h4 className="sec-title">{t('drawer.inflight')}</h4>
                  <ul className="inflight">
                    {s.tools?.map((tool, i) => (
                      <li key={tool.id ?? i}>
                        <span className="ic ic-spin tone-active" aria-hidden />
                        <code className="tool">{shortTool(tool.tool)}</code>
                        <span className="mono subject">{tool.subject}</span>
                        {tool.agent_id && <span className="chip">{t('drawer.subagent')}</span>}
                        <span className="activity-time">{duration(now - tool.started_at)}</span>
                      </li>
                    ))}
                    {s.subagents?.map(a => (
                      <li key={a.id}>
                        <span className="ic ic-pulse tone-active" aria-hidden />
                        <span className="chip">{t('drawer.subagent')}</span>
                        <span className="subject">{a.preview ?? a.id}</span>
                        <span className="activity-time">{duration(now - a.started_at)}</span>
                      </li>
                    ))}
                  </ul>
                </motion.section>
              )}

              {s.last_prompt && (
                <motion.section variants={part}>
                  <h4 className="sec-title">{t('drawer.lastPrompt')}</h4>
                  <blockquote className="quote">{s.last_prompt}</blockquote>
                </motion.section>
              )}
              {s.last_turn?.preview && (
                <motion.section variants={part}>
                  <h4 className="sec-title">{t('drawer.lastReply')}</h4>
                  <blockquote className="quote quote-answer">{s.last_turn.preview}</blockquote>
                </motion.section>
              )}

              <motion.section variants={part}>
                <h4 className="sec-title">
                  {t('drawer.stats')} <span className="h4-note">{t(s.stats.exact_tokens ? 'drawer.exact' : 'drawer.estimate')}</span>
                </h4>
                <dl className="grid-stats grid-stats-4">
                  <Fact label={t('fact.prompts')}><Num v={s.stats.prompts} /></Fact>
                  <Fact label={t('fact.turns')}><Num v={s.stats.turns} /></Fact>
                  <Fact label={t('fact.toolCalls')}><Num v={s.stats.tool_calls} /></Fact>
                  <Fact label={t('fact.toolErrors')}><Num v={s.stats.tool_errors} /></Fact>
                  <Fact label={t('fact.subagents')}><Num v={s.stats.subagent_runs} /></Fact>
                  <Fact label={t('fact.errors')}><Num v={s.stats.errors} /></Fact>
                  <Fact label={t('fact.input')}><Num v={s.stats.input_tokens} /></Fact>
                  <Fact label={t('fact.output')}><Num v={s.stats.output_tokens} /></Fact>
                  <Fact label={t('fact.cacheRead')}><Num v={s.stats.cache_read_tokens} /></Fact>
                  <Fact label={t('fact.cacheWrite')}><Num v={s.stats.cache_write_tokens} /></Fact>
                  <Fact label={t('fact.busy')}>{duration(s.stats.busy_ms)}</Fact>
                  <Fact label={t('fact.requests')}>{s.stats.exact_tokens ? <Num v={s.stats.api_requests ?? 0} /> : '—'}</Fact>
                </dl>
              </motion.section>

              <motion.section variants={part}>
                <h4 className="sec-title">{t('drawer.session')}</h4>
                <dl className="facts">
                  <Fact label={t('fact.dir')} mono>
                    {tildify(s.meta.cwd)}
                  </Fact>
                  <Fact label={t('fact.model')}>{shortModel(s.meta.model)}</Fact>
                  <Fact label={t('fact.permission')}>{s.meta.permission_mode}</Fact>
                  <Fact label={t('fact.surface')}>{s.meta.surface}</Fact>
                  <Fact label={t('fact.version')}>{s.meta.agent_version}</Fact>
                  <Fact label={t('fact.started')}>{dateTime(s.started_at)}</Fact>
                  <Fact label={t('fact.lastSeen')}>{dateTime(s.last_seen)}</Fact>
                  {s.ended_at && <Fact label={t('fact.ended')}>{`${dateTime(s.ended_at)} · ${s.end_reason ?? ''}`}</Fact>}
                  <Fact label={t('fact.sessionId')} mono>
                    {s.session_id}
                  </Fact>
                </dl>
              </motion.section>

              <motion.section variants={part}>
                <h4 className="sec-title">
                  {t('drawer.timeline')} <span className="h4-note">{events.length > 0 && t('drawer.count', { n: events.length })}</span>
                </h4>
                {error && <div className="error">{error}</div>}
                <ol className="timeline">
                  {events.map((r, i) => {
                    const d = describe(r)
                    return (
                      <li key={r.seq} className={`tl tone-${d.tone}`} style={{ ['--i' as string]: Math.min(i, 24) }}>
                        <time>{clock(r.ts)}</time>
                        <span className="tl-kind">{d.kind}</span>
                        <span className="tl-text">
                          {d.text}
                          {d.detail && <span className="tl-detail mono">{d.detail}</span>}
                        </span>
                      </li>
                    )
                  })}
                  {events.length === 0 && !error && <li className="tl-empty">{t('drawer.noEvents')}</li>}
                </ol>
              </motion.section>

              <motion.div variants={part}>
                <button className="btn btn-danger" onClick={forget}>
                  {t('drawer.forget')}
                </button>
              </motion.div>
            </motion.div>
          </>
        )}
      </motion.aside>
    </motion.div>
  )
}

const mergeEvents = (prev: EventRecord[], more: EventRecord[]) => {
  const seen = new Set(prev.map(r => r.seq))
  return [...prev, ...more.filter(r => !seen.has(r.seq))].sort((a, b) => b.seq - a.seq).slice(0, 1000)
}
