import { useId, type ReactNode } from 'react'
import { motion } from 'motion/react'
import { agentInfo } from '../agents.ts'
import { duration, pct } from '../format.ts'
import type { Session } from '../types.ts'
import { EASE, Numeral } from './motion.tsx'
import { Orb } from './Orb.tsx'

export type Tone = 'attention' | 'warning' | 'critical' | 'active' | 'idle' | 'muted' | 'good'
export type IconName = 'alert' | 'ask' | 'spin' | 'pulse' | 'dot' | 'x' | 'stop' | 'stale' | 'pause'

export interface StatusView {
  label: string
  tone: Tone
  icon: IconName
}

type StatusFacts = Pick<Session, 'status' | 'stale' | 'last_turn' | 'tools' | 'end_reason'>

/** How a session's state reads on screen: a label, a tone, an icon; never color alone. */
export const statusView = (s: StatusFacts): StatusView => {
  if (s.status === 'ended') return { label: s.end_reason === 'lost' ? '已失联结束' : '已结束', tone: 'muted', icon: 'stop' }
  if (s.stale) return { label: '无信号', tone: 'muted', icon: 'stale' }
  switch (s.status) {
    case 'waiting_permission':
      return { label: '等待授权', tone: 'attention', icon: 'alert' }
    case 'waiting_input':
      return { label: '等待回答', tone: 'attention', icon: 'ask' }
    case 'tool': {
      const tool = s.tools?.at(-1)?.tool
      return { label: tool ? `执行 ${shortTool(tool)}` : '执行工具', tone: 'active', icon: 'spin' }
    }
    case 'thinking':
      return { label: '思考中', tone: 'active', icon: 'pulse' }
    case 'compacting':
      return { label: '压缩上下文', tone: 'active', icon: 'pulse' }
    default: {
      const outcome = s.last_turn?.outcome
      if (outcome === 'error' || outcome === 'refusal') return { label: outcome === 'error' ? '上轮出错' : '上轮被拒', tone: 'critical', icon: 'x' }
      if (outcome === 'aborted') return { label: '已中断', tone: 'idle', icon: 'pause' }
      return { label: '空闲', tone: 'idle', icon: 'dot' }
    }
  }
}

export const shortTool = (tool: string) => (tool.startsWith('mcp__') ? tool.split('__').slice(1).join(' · ') : tool)

export const shortModel = (model: string | undefined) => {
  if (!model) return undefined
  const m = /^claude-([a-z]+)-(\d+)-(\d+)(.*)$/.exec(model)
  return m ? `${m[1]} ${m[2]}.${m[3]}${m[4] ?? ''}` : model
}

export const Icon = ({ name }: { name: IconName }) => {
  switch (name) {
    case 'spin':
      return <span className="ic ic-spin" aria-hidden />
    case 'pulse':
      return <span className="ic ic-pulse" aria-hidden />
    case 'dot':
      return <span className="ic ic-dot" aria-hidden />
    default:
      return (
        <svg className="ic" viewBox="0 0 16 16" aria-hidden>
          {name === 'alert' && (
            <>
              <path d="M8 1.8 15 14H1z" fill="currentColor" />
              <path d="M8 6v3.6M8 11.4v.2" stroke="var(--icon-ink)" strokeWidth="1.8" strokeLinecap="round" />
            </>
          )}
          {name === 'ask' && (
            <>
              <circle cx="8" cy="8" r="7" fill="currentColor" />
              <path d="M6 6.2a2 2 0 1 1 2.8 1.8c-.6.3-.8.6-.8 1.3M8 11.6v.2" stroke="var(--icon-ink)" strokeWidth="1.6" fill="none" strokeLinecap="round" />
            </>
          )}
          {name === 'x' && (
            <>
              <circle cx="8" cy="8" r="7" fill="currentColor" />
              <path d="m5.5 5.5 5 5m0-5-5 5" stroke="var(--icon-ink)" strokeWidth="1.7" strokeLinecap="round" />
            </>
          )}
          {name === 'stop' && <rect x="3.5" y="3.5" width="9" height="9" rx="2" fill="currentColor" />}
          {name === 'pause' && <path d="M5 3.5v9M11 3.5v9" stroke="currentColor" strokeWidth="2.4" strokeLinecap="round" />}
          {name === 'stale' && <circle cx="8" cy="8" r="5.5" fill="none" stroke="currentColor" strokeWidth="1.6" strokeDasharray="2.4 2" />}
        </svg>
      )
  }
}

export const StatusPill = ({ view, since, now }: { view: StatusView; since?: number; now?: number }) => (
  <span className={`pill tone-${view.tone}`}>
    <Icon name={view.icon} />
    <span className="pill-label" title={view.label}>
      {view.label}
    </span>
    {since !== undefined && now !== undefined && <span className="pill-time">{duration(now - since)}</span>}
  </span>
)

export const AgentBadge = ({ agent, full }: { agent: string; full?: boolean }) => {
  const info = agentInfo(agent)
  return (
    <span className="agent" title={info.label} style={{ ['--agent' as string]: info.color }}>
      <span className="agent-mark">{info.short}</span>
      {full && <span className="agent-name">{info.label}</span>}
    </span>
  )
}

/** Thresholds for "how full": over 70 a warning, over 90 critical, labelled in text too. */
export const fillTone = (percent: number): Tone => (percent >= 90 ? 'critical' : percent >= 70 ? 'warning' : 'active')

export const Meter = ({ value, label, detail, size = 'md' }: { value?: number; label?: ReactNode; detail?: ReactNode; size?: 'sm' | 'md' }) => {
  const v = Math.max(0, Math.min(100, value ?? 0))
  const tone = value === undefined ? 'muted' : fillTone(v)
  return (
    <div className={`meter meter-${size}`}>
      {(label || detail) && (
        <div className="meter-head">
          <span className="meter-label">{label}</span>
          <span className="meter-value">{value === undefined ? pct(value) : <Numeral value={value} format={pct} />}</span>
        </div>
      )}
      <div
        className="meter-track"
        role="meter"
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={value === undefined ? undefined : Math.round(v)}
        aria-label={typeof label === 'string' ? label : undefined}
      >
        <motion.div
          className={`meter-fill tone-${tone}`}
          initial={{ width: 0 }}
          animate={{ width: `${v}%` }}
          transition={{ duration: 1.1, ease: EASE }}
        />
      </div>
      {detail && <div className="meter-detail">{detail}</div>}
    </div>
  )
}

/** Choices with a thumb that glides to the chosen one. */
export const Seg = <T extends string | number>({
  options,
  value,
  onChange,
  label,
  size,
}: {
  options: readonly { id: T; label: ReactNode }[]
  value: T
  onChange: (v: T) => void
  label: string
  size?: 'sm'
}) => {
  const id = useId()
  return (
    <div className={`seg${size ? ` seg-${size}` : ''}`} role="group" aria-label={label}>
      {options.map(o => (
        <button key={o.id} className={value === o.id ? 'on' : ''} aria-pressed={value === o.id} onClick={() => onChange(o.id)}>
          {value === o.id && <motion.span layoutId={`seg${id}`} className="seg-ink" transition={{ type: 'spring', stiffness: 520, damping: 40 }} />}
          <span className="seg-label">{o.label}</span>
        </button>
      ))}
    </div>
  )
}

export const Empty = ({ title, children }: { title: string; children?: ReactNode }) => (
  <motion.div className="empty" initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} transition={{ duration: 0.7, ease: EASE }}>
    <Orb mood="idle" size={56} />
    <div className="empty-title">{title}</div>
    {children && <div className="empty-body">{children}</div>}
  </motion.div>
)

export const SearchIcon = () => (
  <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" aria-hidden>
    <circle cx="7" cy="7" r="4.6" />
    <path d="m10.5 10.5 3.5 3.5" />
  </svg>
)
