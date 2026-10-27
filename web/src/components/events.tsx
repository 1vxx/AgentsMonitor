import { compact, duration } from '../format.ts'
import type { EventRecord } from '../types.ts'
import { shortTool, type Tone } from './bits.tsx'

const str = (v: unknown) => (typeof v === 'string' ? v : undefined)
const num = (v: unknown) => (typeof v === 'number' ? v : undefined)

const OUTCOMES: Record<string, string> = { answer: '完成', aborted: '被中断', refusal: '被拒绝', error: '出错' }
const END_REASONS: Record<string, string> = {
  prompt_input_exit: '用户退出',
  clear: '/clear',
  resume: '被恢复替代',
  logout: '登出',
  other: '进程结束',
  lost: '失联',
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
  const sub = agentId ? '子代理 ' : ''
  switch (r.type) {
    case 'session.start':
      return { kind: '会话', text: `开始${str(r.source) ? ` · ${str(r.source)}` : ''}`, detail: str(r.model), tone: 'good' }
    case 'session.end': {
      const reason = str(r.reason)
      return { kind: '会话', text: `结束${reason ? ` · ${END_REASONS[reason] ?? reason}` : ''}`, tone: 'muted' }
    }
    case 'prompt':
      return { kind: '提问', text: str(r.preview) ?? '（新提示）', tone: 'idle' }
    case 'turn.start':
      return { kind: `${sub}回合`, text: agentId ? (str(r.preview) ?? '开始') : '开始处理', tone: 'active' }
    case 'turn.end': {
      const outcome = str(r.outcome) ?? 'answer'
      const ms = num(r.duration_ms)
      const usage = r.usage as { input?: number; output?: number } | undefined
      const bits = [OUTCOMES[outcome] ?? outcome, ms !== undefined ? duration(ms) : undefined, usage?.output ? `${compact(usage.output)} 输出 tokens` : undefined]
      return {
        kind: `${sub}回合`,
        text: bits.filter(Boolean).join(' · '),
        detail: str(r.preview),
        tone: outcome === 'error' || outcome === 'refusal' ? 'critical' : outcome === 'aborted' ? 'muted' : 'good',
      }
    }
    case 'tool.start':
      return { kind: `${sub}工具`, text: shortTool(str(r.tool) ?? '?'), detail: str(r.subject), tone: 'active' }
    case 'tool.end': {
      const ok = r.ok !== false
      const ms = num(r.duration_ms)
      return {
        kind: `${sub}工具`,
        text: `${shortTool(str(r.tool) ?? '?')} ${ok ? '完成' : '失败'}${ms !== undefined ? ` · ${duration(ms)}` : ''}`,
        tone: ok ? 'idle' : 'critical',
      }
    }
    case 'attention': {
      const kind = str(r.kind)
      const what = kind === 'permission' ? '等待授权' : kind === 'input' ? '等待回答' : '等待输入'
      const tool = str(r.tool)
      return { kind: '需要你', text: tool ? `${what} · ${shortTool(tool)}` : what, detail: str(r.subject) ?? str(r.message), tone: 'attention' }
    }
    case 'attention.clear':
      return { kind: '需要你', text: '已处理', tone: 'idle' }
    case 'compact.start':
      return { kind: '上下文', text: `开始压缩${str(r.trigger) ? ` · ${str(r.trigger)}` : ''}`, tone: 'active' }
    case 'compact.end':
      return { kind: '上下文', text: '压缩完成', tone: 'idle' }
    case 'notice':
      return { kind: '通知', text: str(r.kind) ?? 'notice', detail: str(r.message), tone: 'idle' }
    default:
      return { kind: r.type, text: '', tone: 'idle' }
  }
}
