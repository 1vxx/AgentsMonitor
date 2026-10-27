// What the dashboard knows about each agent product. An agent missing here
// still shows, under its own name.

export interface AgentInfo {
  label: string
  short: string
  /** Identity color: a fixed categorical slot per agent, never by rank. */
  color: string
}

const KNOWN: Record<string, AgentInfo> = {
  'claude-code': { label: 'Claude Code', short: 'CC', color: 'var(--series-2)' },
  codex: { label: 'Codex', short: 'CX', color: 'var(--series-1)' },
}

export const agentInfo = (agent: string): AgentInfo =>
  KNOWN[agent] ?? { label: agent, short: agent.slice(0, 2).toUpperCase(), color: 'var(--series-7)' }

const WINDOWS: Record<string, string> = {
  five_hour: '5 小时窗口',
  seven_day: '7 天窗口',
  seven_day_opus: '7 天 · Opus',
  seven_day_sonnet: '7 天 · Sonnet',
  spend_limit: '消费上限',
  primary: '主窗口',
  secondary: '次窗口',
}

export const windowLabel = (kind: string) => WINDOWS[kind] ?? kind.replace(/_/g, ' ')
