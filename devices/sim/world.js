// A stand-in for the daemon: sessions that scenarios move through their
// states, summarised the way crates/agentsmon/src/summary.rs does it.

const ERROR_MOOD_MS = 5 * 60_000
const MAX_ITEMS = 8

const isWaiting = s => s.status === 'waiting_input' || s.status === 'waiting_permission'
const isWorking = s => s.status === 'thinking' || s.status === 'tool' || s.status === 'compacting'

// While it works a session uses tokens (cache reads most of them) and fills
// its context; each starts from its own amount, so no two read alike
const TOKENS_PER_S = 9000
const CONTEXT_PER_S = 0.25
const seed = key => [...key].reduce((h, c) => (h * 31 + c.charCodeAt(0)) >>> 0, 7)
const workedMs = (s, now) => s.workedMs + (isWorking(s) ? now - s.markAt : 0)

export class World {
  constructor() {
    this.sessions = new Map()
    this.link = 'online'
  }

  get list() {
    return [...this.sessions.values()]
  }

  start(now, key, { title, project = 'AgentsMonitor', agent = 'claude-code' }) {
    const s = { key, agent, project, title, status: 'thinking', since: now, turnStart: now, lastSeen: now, stale: false }
    const h = seed(key)
    Object.assign(s, { tokensFrom: 300_000 + (h % 2_500_000), contextFrom: 12 + (h % 30), workedMs: 0, markAt: now })
    this.sessions.set(key, s)
    return s
  }

  // A session that finished a turn some time ago
  finished(now, key, info, outcome = 'answer', agoMs = 60_000) {
    const s = this.start(now - agoMs - 20_000, key, info)
    this.finish(now - agoMs, key, outcome)
    s.lastSeen = now
    return s
  }

  touch(now, key, change) {
    const s = this.sessions.get(key)
    if (!s) return
    s.workedMs = workedMs(s, now)
    s.markAt = now
    Object.assign(s, change, { lastSeen: now })
  }

  prompt(now, key) {
    this.touch(now, key, { status: 'thinking', since: now, turnStart: now, tool: undefined, attention: undefined })
  }

  think(now, key) {
    this.touch(now, key, { status: 'thinking', since: now, tool: undefined })
  }

  tool(now, key, tool) {
    this.touch(now, key, { status: 'tool', since: now, tool })
  }

  ask(now, key, { kind = 'permission', tool, subject, message } = {}) {
    this.touch(now, key, {
      status: kind === 'input' ? 'waiting_input' : 'waiting_permission',
      since: now,
      tool: tool ?? this.sessions.get(key)?.tool,
      attention: { kind, since: now, tool, subject, message },
    })
  }

  // The person answered: the session carries on
  answer(now, key) {
    const s = this.sessions.get(key)
    if (!s || !isWaiting(s)) return
    const tool = s.attention?.kind === 'permission' ? s.attention.tool : undefined
    this.touch(now, key, { attention: undefined, status: tool ? 'tool' : 'thinking', since: now, tool })
  }

  finish(now, key, outcome = 'answer') {
    this.touch(now, key, { status: 'idle', since: now, tool: undefined, attention: undefined, last_turn: { outcome, ended_at: now } })
  }

  setStale(now, key, stale) {
    const s = this.sessions.get(key)
    if (s) s.stale = stale
  }

  end(key) {
    this.sessions.delete(key)
  }

  summary(now) {
    const live = this.list
    const counts = { live: 0, idle: 0, thinking: 0, tool: 0, compacting: 0, waiting: 0, stale: 0 }
    let errored = false
    for (const s of live) {
      counts.live++
      if (s.stale) counts.stale++
      if (isWaiting(s)) counts.waiting++
      else if (s.status in counts) counts[s.status]++
      const out = s.last_turn?.outcome
      if (s.status === 'idle' && (out === 'error' || out === 'refusal') && now - s.last_turn.ended_at < ERROR_MOOD_MS) errored = true
    }
    // Waiting, longest first; working, newest turn first; idle, latest first; no signal last
    const key = s =>
      s.stale ? [3, -s.lastSeen] : isWaiting(s) ? [0, s.attention?.since ?? s.since] : isWorking(s) ? [1, -s.turnStart] : [2, -s.since]
    live.sort((a, b) => {
      const ka = key(a), kb = key(b)
      return ka[0] - kb[0] || ka[1] - kb[1] || a.key.localeCompare(b.key)
    })
    const fresh = pred => live.some(s => !s.stale && pred(s))
    const mood = fresh(isWaiting) ? 'attention' : fresh(isWorking) ? 'working' : errored ? 'error' : live.length ? 'idle' : 'offline'
    return {
      ts: now,
      mood,
      counts,
      sessions: live.slice(0, MAX_ITEMS).map(s => {
        const worked = workedMs(s, now) / 1000
        return {
          key: s.key,
          agent: s.agent,
          project: s.project,
          title: s.title,
          status: s.status,
          since: s.since,
          tool: isWorking(s) || isWaiting(s) ? s.tool : undefined,
          stale: s.stale || undefined,
          attention: s.attention,
          last_turn: s.last_turn,
          tokens: Math.round(s.tokensFrom + worked * TOKENS_PER_S),
          // Codex's counts read as an estimate here, to show how one looks
          tokens_exact: s.agent !== 'codex' || undefined,
          context: { percent: Math.min(97, s.contextFrom + worked * CONTEXT_PER_S) },
        }
      }),
      quotas: [],
    }
  }
}
