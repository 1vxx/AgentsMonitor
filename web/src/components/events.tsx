import { compact, duration } from '../format.ts'
import { has, t } from '../i18n/index.ts'
import type { EventRecord } from '../types.ts'
import { shortTool, type Tone } from './bits.tsx'

const str = (v: unknown) => (typeof v === 'string' ? v : undefined)
const num = (v: unknown) => (typeof v === 'number' ? v : undefined)

/** A known outcome or end reason in words; an unknown one as the daemon sent it. */
const word = (prefix: string, v: string) => {
  const key = `${prefix}.${v}`
  return has(key) ? t(key) : v
}

export interface Described {
  kind: string
  text: string
  detail?: string
  tone: Tone
}

/** One line for an event of the history or the live feed. */
export const describe = (r: EventRecord): Described => {
  const agentId = str(r.agent_id)
  switch (r.type) {
    case 'session.start':
      return { kind: t('ev.kind.session'), text: `${t('ev.started')}${str(r.source) ? ` · ${str(r.source)}` : ''}`, detail: str(r.model), tone: 'good' }
    case 'session.end': {
      const reason = str(r.reason)
      return { kind: t('ev.kind.session'), text: `${t('ev.ended')}${reason ? ` · ${word('ev.end', reason)}` : ''}`, tone: 'muted' }
    }
    case 'prompt':
      return { kind: t('ev.kind.prompt'), text: str(r.preview) ?? t('ev.newPrompt'), tone: 'idle' }
    case 'turn.start':
      return {
        kind: t(agentId ? 'ev.kind.subTurn' : 'ev.kind.turn'),
        text: agentId ? (str(r.preview) ?? t('ev.started')) : t('ev.working'),
        tone: 'active',
      }
    case 'turn.end': {
      const outcome = str(r.outcome) ?? 'answer'
      const ms = num(r.duration_ms)
      const usage = r.usage as { input?: number; output?: number } | undefined
      const bits = [word('ev.outcome', outcome), ms !== undefined ? duration(ms) : undefined, usage?.output ? t('ev.outputTokens', { n: compact(usage.output) }) : undefined]
      return {
        kind: t(agentId ? 'ev.kind.subTurn' : 'ev.kind.turn'),
        text: bits.filter(Boolean).join(' · '),
        detail: str(r.preview),
        tone: outcome === 'error' || outcome === 'refusal' ? 'critical' : outcome === 'aborted' ? 'muted' : 'good',
      }
    }
    case 'tool.start':
      return { kind: t(agentId ? 'ev.kind.subTool' : 'ev.kind.tool'), text: shortTool(str(r.tool) ?? '?'), detail: str(r.subject), tone: 'active' }
    case 'tool.end': {
      const ok = r.ok !== false
      const ms = num(r.duration_ms)
      const tool = shortTool(str(r.tool) ?? '?')
      return {
        kind: t(agentId ? 'ev.kind.subTool' : 'ev.kind.tool'),
        text: `${t(ok ? 'ev.toolDone' : 'ev.toolFailed', { tool })}${ms !== undefined ? ` · ${duration(ms)}` : ''}`,
        tone: ok ? 'idle' : 'critical',
      }
    }
    case 'attention': {
      const kind = str(r.kind)
      const what = t(kind === 'permission' ? 'status.permission' : kind === 'input' ? 'status.input' : 'ev.waitIdle')
      const tool = str(r.tool)
      return { kind: t('ev.kind.attention'), text: tool ? `${what} · ${shortTool(tool)}` : what, detail: str(r.subject) ?? str(r.message), tone: 'attention' }
    }
    case 'attention.clear':
      return { kind: t('ev.kind.attention'), text: t('ev.handled'), tone: 'idle' }
    case 'compact.start':
      return { kind: t('ev.kind.context'), text: `${t('ev.compactStart')}${str(r.trigger) ? ` · ${str(r.trigger)}` : ''}`, tone: 'active' }
    case 'compact.end':
      return { kind: t('ev.kind.context'), text: t('ev.compactEnd'), tone: 'idle' }
    case 'notice':
      return { kind: t('ev.kind.notice'), text: str(r.kind) ?? 'notice', detail: str(r.message), tone: 'idle' }
    default:
      return { kind: r.type, text: '', tone: 'idle' }
  }
}
