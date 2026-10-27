import { memo } from 'react'
import { motion } from 'motion/react'
import { ago, compact, duration, tildify } from '../format.ts'
import { tokenTotal, type Session } from '../types.ts'
import { AgentBadge, Icon, Meter, StatusPill, shortModel, shortTool, statusView } from './bits.tsx'
import { EASE } from './motion.tsx'

export const sessionTokens = (s: Session) =>
  tokenTotal({
    input: s.stats.input_tokens,
    output: s.stats.output_tokens,
    cache_read: s.stats.cache_read_tokens,
    cache_write: s.stats.cache_write_tokens,
  })

const OUTCOME_TEXT: Record<string, string> = { answer: '已回复', aborted: '被中断', refusal: '被拒绝', error: '出错' }

const Activity = ({ s, now }: { s: Session; now: number }) => {
  if (s.attention && s.attention.kind !== 'idle') {
    return (
      <div className="activity activity-ask">
        <span className="tone-attention" style={{ display: 'inline-flex' }}>
          <Icon name={s.attention.kind === 'permission' ? 'alert' : 'ask'} />
        </span>
        <span className="activity-label">{s.attention.kind === 'permission' ? '请求批准' : '向你提问'}</span>
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
        <span className="activity-label">本轮已进行</span>
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
        <span className="activity-label">{OUTCOME_TEXT[s.last_turn.outcome] ?? s.last_turn.outcome}</span>
        {s.last_turn.duration_ms !== undefined && <span>用时 {duration(s.last_turn.duration_ms)}</span>}
        <span className="activity-time">{ago(s.last_turn.ended_at, now)}</span>
      </div>
    )
  }
  return (
    <div className="activity activity-done">
      <span className="activity-label">等待第一个提示</span>
    </div>
  )
}

export const SessionCard = memo(function SessionCard({ s, now, onOpen, i = 0 }: { s: Session; now: number; onOpen: (key: string) => void; i?: number }) {
  const view = statusView(s)
  const project = s.meta.project ?? s.session_id.slice(0, 8)
  const agents = s.subagents?.length ?? 0
  return (
    <motion.article
      layout="position"
      className={`card tone-${view.tone}${s.stale ? ' is-stale' : ''}`}
      tabIndex={0}
      onClick={() => onOpen(s.key)}
      onKeyDown={e => (e.key === 'Enter' || e.key === ' ') && (e.preventDefault(), onOpen(s.key))}
      aria-label={`${project}: ${view.label}`}
      initial={{ opacity: 0, y: 18 }}
      animate={{ opacity: 1, y: 0 }}
      exit={{ opacity: 0, scale: 0.96, transition: { duration: 0.25 } }}
      transition={{ duration: 0.7, ease: EASE, delay: Math.min(i, 10) * 0.06, layout: { duration: 0.6, ease: EASE, delay: 0 } }}
    >
      <header className="card-head">
        <AgentBadge agent={s.agent} />
        <div className="card-title">
          <h3>{project}</h3>
          <span className="card-path" title={s.meta.cwd}>
            {tildify(s.meta.cwd) ?? s.session_id}
          </span>
        </div>
        <StatusPill view={view} since={s.status_since} now={now} />
      </header>
      <Activity s={s} now={now} />
      {s.last_prompt && <p className="card-prompt">{s.last_prompt}</p>}
      <footer className="card-foot">
        <div className="card-ctx">
          <Meter size="sm" value={s.context?.percent} label="上下文" />
        </div>
        <span className="card-fact" title={s.stats.exact_tokens ? '本会话 tokens（逐请求精确计数）' : '本会话 tokens（按回合估算）'}>
          {s.stats.exact_tokens ? '' : '≈'}
          {compact(sessionTokens(s))} <span className="muted">tokens</span>
        </span>
        {agents > 0 && <span className="chip">子代理 ×{agents}</span>}
        {s.meta.permission_mode && s.meta.permission_mode !== 'default' && (
          <span className={`chip${s.meta.permission_mode === 'bypassPermissions' ? ' chip-warn' : ''}`}>{s.meta.permission_mode}</span>
        )}
        <span className="card-fact model">{shortModel(s.meta.model)}</span>
      </footer>
    </motion.article>
  )
})
