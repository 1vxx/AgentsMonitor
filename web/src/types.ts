// Mirror of crates/agentsmon-protocol: what the daemon sends.

export type Status = 'ended' | 'idle' | 'compacting' | 'thinking' | 'tool' | 'waiting_input' | 'waiting_permission'
export type Mood = 'offline' | 'idle' | 'error' | 'working' | 'attention'
export type TurnOutcome = 'answer' | 'aborted' | 'refusal' | 'error'
export type AttentionKind = 'permission' | 'input' | 'idle'

export interface SessionMeta {
  cwd?: string
  project?: string
  model?: string
  permission_mode?: string
  agent_version?: string
  surface?: string
  interactive?: boolean
  transcript_path?: string
  /** The short title the agent gave the session, when it has one. */
  title?: string
}

export interface ContextUsage {
  tokens?: number
  window?: number
  percent?: number
}

export interface ActiveTool {
  id?: string
  tool: string
  subject?: string
  agent_id?: string
  started_at: number
}

export interface Session {
  key: string
  agent: string
  session_id: string
  meta: SessionMeta
  status: Status
  status_since: number
  started_at: number
  updated_at: number
  last_seen: number
  stale: boolean
  ended_at?: number
  end_reason?: string
  turn?: { id?: string; started_at: number; preview?: string }
  tools?: ActiveTool[]
  subagents?: { id: string; started_at: number; preview?: string }[]
  attention?: { kind: AttentionKind; since: number; tool?: string; subject?: string; message?: string }
  compacting?: boolean
  last_turn?: { outcome: TurnOutcome; ended_at: number; duration_ms?: number; preview?: string }
  last_prompt?: string
  context?: ContextUsage
  cost_usd?: number
  stats: {
    prompts: number
    turns: number
    tool_calls: number
    tool_errors: number
    subagent_runs: number
    errors: number
    input_tokens: number
    output_tokens: number
    cache_read_tokens: number
    cache_write_tokens: number
    /** Model requests counted in the token figures (exact counting only). */
    api_requests?: number
    /** Token figures are per-request counts from the agent itself, not turn estimates. */
    exact_tokens?: boolean
    busy_ms: number
  }
}

export interface TokenTotals {
  input: number
  output: number
  cache_read: number
  cache_write: number
  requests: number
}

export interface TokenReport extends TokenTotals {
  since: number
  until: number
  groups?: (TokenTotals & { key: string })[]
  last_ts?: number
}

export interface TokenSeries {
  since: number
  bucket_ms: number
  points: (TokenTotals & { t: number })[]
}

export const tokenTotal = (t: Pick<TokenTotals, 'input' | 'output' | 'cache_read' | 'cache_write'>) =>
  t.input + t.output + t.cache_read + t.cache_write

export interface RateWindow {
  kind: string
  percent_used: number
  resets_at?: string
}

export interface Quota {
  agent: string
  windows: RateWindow[]
  updated_at: number
  source?: string
}

export interface QuotaSample {
  agent: string
  kind: string
  ts: number
  percent_used: number
  resets_at?: string
}

export interface SummaryItem {
  key: string
  agent: string
  project: string
  title?: string
  status: Status
  since: number
  tool?: string
  stale?: boolean
  attention?: { kind: AttentionKind; since: number; tool?: string; subject?: string; message?: string }
  last_turn?: { outcome: TurnOutcome; ended_at: number }
}

export interface Summary {
  ts: number
  mood: Mood
  counts: { live: number; idle: number; thinking: number; tool: number; compacting: number; waiting: number; stale: number }
  sessions: SummaryItem[]
  quotas: { agent: string; windows: [string, number][] }[]
}

export interface ServerInfo {
  name: string
  version: string
  protocol: number
  started_at: number
}

/** A stored event: the canonical event's own fields sit beside these. */
export interface EventRecord {
  seq: number
  ts: number
  key: string
  agent: string
  type: string
  [field: string]: unknown
}

export interface Snapshot {
  server: ServerInfo
  sessions: Session[]
  quotas: Quota[]
  summary: Summary
}

export type ServerMsg =
  | ({ type: 'snapshot' } & Snapshot)
  | { type: 'session'; session: Session }
  | { type: 'session.removed'; key: string }
  | { type: 'quota'; quota: Quota }
  | { type: 'event'; record: EventRecord }
  | { type: 'summary'; summary: Summary }
  | { type: 'tokens'; ts: number; added: TokenTotals }

/** What `POST /v1/maintenance` did. */
export interface MaintenanceReport {
  forgotten: number
  /** Past the retention. */
  too_old: number
  /** Deleted in Claude Code (transcript gone). */
  transcript_gone: number
  /** Removed only because every ended session was asked for. */
  ended: number
  /** Archived in Claude Desktop: out of the live view, still in the history. */
  archived: number
  db_bytes_before: number
  db_bytes_after: number
  duration_ms: number
}

export interface DaemonStats {
  uptime_ms: number
  events_in: number
  batches_in: number
  rejected: number
  usage_in?: number
  usage_duplicates?: number
  /** OTLP logs exports received, and every service name they carried. */
  otlp?: { exports: number; services: string[] }
  /** Database file, write-ahead log included. */
  db_bytes?: number
  retention?: {
    sessions_days: number
    follow_transcripts: boolean
    events_days: number
    quota_days: number
    token_detail_days: number
    token_hourly_days: number
    token_daily_days: number
  }
  /** What the live view holds; 0 idle minutes keeps idle sessions in it. */
  live?: { keep_ended_minutes: number; hide_idle_minutes: number }
  ws_clients: number
  sessions_in_memory: number
  sessions_live: number
  /** Running sessions out of the live view: idle too long, or archived. */
  sessions_hidden?: number
  db?: { sessions: number; events: number; quota_samples: number; token_requests?: number }
  rss_bytes?: number
  cpu_ms?: number
}
