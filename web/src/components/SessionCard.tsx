import { memo } from 'react'
import { motion } from 'motion/react'
import { ago, compact, duration } from '../format.ts'
import { has, t, useT } from '../i18n/index.ts'
import { tokenTotal, type Session } from '../types.ts'
import { AgentBadge, Icon, Meter, StatusPill, sessionPlace, sessionTitle, shortModel, shortTool, statusView } from './bits.tsx'
import { EASE } from './motion.tsx'

export const sessionTokens = (s: Session) =>
  tokenTotal({
    input: s.stats.input_tokens,
    output: s.stats.output_tokens,
    cache_read: s.stats.cache_read_tokens,
    cache_write: s.stats.cache_write_tokens,
  })

const outcomeText = (outcome: string) => {
  const key = `card.outcome.${outcome}`
  return has(key) ? t(key) : outcome
}

const Activity = ({ s, now }: { s: Session; now: number }) => {
  if (s.attention && s.attention.kind !== 'idle') {
    return (
      <div className="activity activity-ask">
        <span className="tone-attention" style={{ display: 'inline-flex' }}>
          <Icon name={s.attention.kind === 'permission' ? 'alert' : 'ask'} />
        </span>
        <span className="activity-label">{t(s.attention.kind === 'permission' ? 'card.askPermission' : 'card.askInput')}</span>
        {s.attention.tool && <code className="tool">{shortTool(s.attention.tool)}</code>}
        <span className="mono subject">{s.attention.subject ?? s.attention.message}</span>
      </div>
    )
  }
  const tool = s.tools?.at(-1)
  if (tool) {
    return (
      <div className="activity">
        <code className="tool">{shortTool(tool.tool)}</code>
        <span className="mono subject">{tool.subject}</span>
        <span className="activity-time">{duration(now - tool.started_at)}</span>
      </div>
    )
  }
  if (s.turn) {
    return (
      <div className="activity">
        <span className="activity-label">{t('card.turnRunning')}</span>
        <span className="activity-dots" aria-hidden>
          <i />
          <i />
          <i />
        </span>
        <span className="activity-time">{duration(now - s.turn.started_at)}</span>
      </div>
    )
  }
  if (s.last_turn) {
    return (
      <div className="activity activity-done">
        <span className="activity-label">{outcomeText(s.last_turn.outcome)}</span>
        {s.last_turn.duration_ms !== undefined && <span>{t('card.took', { d: duration(s.last_turn.duration_ms) })}</span>}
        <span className="activity-time">{ago(s.last_turn.ended_at, now)}</span>
      </div>
    )
  }
  return (
    <div className="activity activity-done">
      <span className="activity-label">{t('card.firstPrompt')}</span>
    </div>
  )
}

export const SessionCard = memo(function SessionCard({ s, now, onOpen, i = 0 }: { s: Session; now: number; onOpen: (key: string) => void; i?: number }) {
  useT()
  const view = statusView(s)
  const title = sessionTitle(s)
  const agents = s.subagents?.length ?? 0
  return (
    <motion.article
      layout="position"
      className={`card tone-${view.tone}${s.stale ? ' is-stale' : ''}`}
      tabIndex={0}
      onClick={() => onOpen(s.key)}
      onKeyDown={e => (e.key === 'Enter' || e.key === ' ') && (e.preventDefault(), onOpen(s.key))}
      aria-label={`${title}: ${view.label}`}
      initial={{ opacity: 0, y: 18 }}
      animate={{ opacity: 1, y: 0 }}
      exit={{ opacity: 0, scale: 0.96, transition: { duration: 0.25 } }}
      transition={{ duration: 0.7, ease: EASE, delay: Math.min(i, 10) * 0.06, layout: { duration: 0.6, ease: EASE, delay: 0 } }}
    >
      <header className="card-head">
        <AgentBadge agent={s.agent} />
        <div className="card-title">
          <h3 title={title}>{title}</h3>
          <span className="card-path" title={s.meta.cwd}>
            {sessionPlace(s)}
          </span>
        </div>
        <StatusPill view={view} since={s.status_since} now={now} />
      </header>
      <Activity s={s} now={now} />
      {s.last_prompt && <p className="card-prompt">{s.last_prompt}</p>}
      <footer className="card-foot">
        <div className="card-ctx">
          <Meter size="sm" value={s.context?.percent} label={t('card.context')} />
        </div>
        <span className="card-fact" title={t(s.stats.exact_tokens ? 'card.tokensExact' : 'card.tokensEstimate')}>
          {s.stats.exact_tokens ? '' : '≈'}
          {compact(sessionTokens(s))} <span className="muted">tokens</span>
        </span>
        {agents > 0 && <span className="chip">{t('card.subagents', { n: agents })}</span>}
        {s.meta.permission_mode && s.meta.permission_mode !== 'default' && (
          <span className={`chip${s.meta.permission_mode === 'bypassPermissions' ? ' chip-warn' : ''}`}>{s.meta.permission_mode}</span>
        )}
        <span className="card-fact model">{shortModel(s.meta.model)}</span>
      </footer>
    </motion.article>
  )
})
