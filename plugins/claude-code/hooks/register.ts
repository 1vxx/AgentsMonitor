import type { EngineInterface, Register, SessionUsage } from 'claude-code'
import { PROMPT_CHARS, basename, preview, subjectOf } from './text.ts'

// Streams this session to the agentsmon daemon over its Unix socket.
//
// Every hook only pushes onto a queue and returns: sending happens on a timer
// once the hook's dispatch has resolved, so the session never waits on the
// monitor. Events go out in order, in batches; while the daemon is away they
// wait in a bounded queue and go out when it is back.

const AGENT = 'claude-code'
// The daemon counts a session stale after 65 s without one
const HEARTBEAT_MS = 20_000
const MAX_QUEUE = 1_000
const MAX_BATCH = 256
const RETRY_MIN_MS = 1_000
const RETRY_MAX_MS = 30_000
// Question tools put the session in "waiting for input" while they run
const QUESTION_TOOLS = new Set(['AskUserQuestion'])

type Wire = { type: string; [field: string]: unknown }
type Queued = Wire & { agent: string; sid?: string; ts: number }

type Meta = {
  cwd?: string
  project?: string
  model?: string
  permission_mode?: string
  agent_version?: string
  surface?: string
  interactive?: boolean
  transcript_path?: string
}

// This load's view of the session. A reload starts it over and announces the
// session again; the daemon keeps what it knew.
let withText = true
let configuredSocket = ''
let sid: string | undefined
let socketPath: string | undefined
let meta: Meta = {}
// The session.start source still to announce, if any, and the id last announced
let announce: string | undefined
let announced: string | undefined
// A session nobody drives yet (Claude Desktop keeps one warm for the next new
// session, the SDK before its first prompt) stays unannounced until it is
// used: then this is the source it announces with
let waiting: string | undefined
let ended = false
let heartbeat: { cancel: () => void } | undefined
// What is in flight, so a heartbeat can say whether anything is: the main
// turn, subagents (background ones outlive the main turn) and tool calls
let mainTurn = false
const agents = new Set<string>()
let tools = 0

const queue: Queued[] = []
let sending: Promise<void> | undefined
let scheduled = false
let retryMs = 0

const busy = () => mainTurn || agents.size > 0 || tools > 0
const text = (value: unknown, max: number) => (withText ? preview(value, max) : undefined)

const setMeta = (next: Meta) => {
  let changed = false
  for (const [k, v] of Object.entries(next) as [keyof Meta, Meta[keyof Meta]][]) {
    if (v !== undefined && v !== null && meta[k] !== v) {
      meta = { ...meta, [k]: v }
      changed = true
    }
  }
  return changed
}

const usageEvent = (u: Pick<SessionUsage, 'context' | 'rateLimits' | 'cost'>): Wire => ({
  type: 'usage',
  context: { tokens: u.context.tokens, window: u.context.window, percent: u.context.percent },
  cost_usd: u.cost?.usd,
  rate_limits: u.rateLimits.map(r => ({ kind: r.kind, percent_used: r.percentUsed, resets_at: r.resetsAt })),
})

const heartbeatEvent = (): Wire => ({ type: 'heartbeat', busy: busy(), ...meta })

// Starts reporting a session: at launch for one a person sits at, at first
// use for any other
const activate = ($: EngineInterface) => {
  if (heartbeat !== undefined || ended) return
  // Loaded into a session already running: no start event came
  start($, waiting ?? 'attach')
  waiting = undefined
}

const emit = ($: EngineInterface, event: Wire) => {
  activate($)
  queue.push({ agent: AGENT, sid, ts: Date.now(), ...event })
  if (queue.length > MAX_QUEUE) queue.splice(0, queue.length - MAX_QUEUE)
  schedule($, 0)
}

const schedule = ($: EngineInterface, ms: number) => {
  if (scheduled) return
  scheduled = true
  $.clock.after(ms, () => {
    scheduled = false
    void flush($)
  })
}

const start = ($: EngineInterface, source: string) => {
  heartbeat ??= $.clock.every(HEARTBEAT_MS, () => emit($, heartbeatEvent()))
  // Both start events fire at launch: the second only brings metadata
  if (sid !== undefined && sid === announced) {
    emit($, heartbeatEvent())
    return
  }
  announce = source
  schedule($, 0)
}

const readMeta = async ($: EngineInterface) => {
  const [cwd, model, surfaces, version, repo] = await Promise.all([
    $.session.cwd(),
    $.session.model(),
    $.session.surfaces(),
    $.session.version(),
    $.session.repo().catch(() => null),
  ])
  setMeta({
    cwd,
    model,
    surface: surfaces[0] ?? 'headless',
    agent_version: version.version,
    project: basename(repo?.root ?? cwd),
  })
}

const resolveSocket = async ($: EngineInterface) =>
  configuredSocket ||
  (await $.env.get('AGENTSMON_SOCKET')) ||
  `${(await $.env.get('HOME')) ?? ''}/.agentsmon/agentsmon.sock`

const send = async ($: EngineInterface) => {
  if (sid === undefined) sid = await $.session.id()
  if (announce !== undefined) {
    const source = announce
    announce = undefined
    announced = sid
    await readMeta($).catch(() => {})
    const at = Date.now()
    const opening: Queued[] = [{ agent: AGENT, sid, ts: at, type: 'session.start', source, ...meta }]
    // Rate limits and context as they stand, before a turn reports them
    const usage = await $.session.usage().catch(() => undefined)
    if (usage) opening.push({ agent: AGENT, sid, ts: at, ...usageEvent(usage) })
    queue.unshift(...opening)
  }
  socketPath ??= await resolveSocket($)
  while (queue.length > 0) {
    const batch = queue.slice(0, MAX_BATCH)
    for (const e of batch) e.sid ??= sid
    const res = await $.http.fetch('http://agentsmon/v1/ingest', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(batch),
      socketPath,
    })
    // A 4xx is about the events themselves: sending them again cannot help
    if (!res.ok && res.status >= 500) throw new Error(`agentsmon answered ${res.status}`)
    queue.splice(0, batch.length)
  }
}

// One send at a time, so events arrive in order
const flush = ($: EngineInterface): Promise<void> => {
  if (sending) return sending
  sending = send($)
    .then(() => {
      retryMs = 0
    })
    .catch(() => {
      retryMs = Math.min(Math.max(retryMs * 2, RETRY_MIN_MS), RETRY_MAX_MS)
      schedule($, retryMs)
    })
    .finally(() => {
      sending = undefined
    })
  return sending
}

const endSession = async ($: EngineInterface, endedId: string, reason: string) => {
  // Never used, never announced: nothing to end
  if (heartbeat === undefined) {
    if (reason !== 'clear') ended = true
    return
  }
  queue.push({ agent: AGENT, sid: endedId, ts: Date.now(), type: 'session.end', reason })
  mainTurn = false
  agents.clear()
  if (reason === 'clear') {
    // The process goes on under a new id, which SessionStart brings
    sid = undefined
  } else {
    ended = true
    heartbeat?.cancel()
  }
  // The process may be about to exit: send now, within the end's bound
  await (sending ?? Promise.resolve())
  await flush($)
}

export const register: Register = (on, options) => {
  withText = options.detail !== 'none'
  configuredSocket = typeof options.socket === 'string' ? options.socket.trim() : ''

  on('session.start', ($, e, next) => {
    setMeta({ interactive: e.isInteractive, cwd: e.cwd })
    if (e.isInteractive) start($, 'startup')
    else waiting = 'startup'
    return next(e)
  })

  // Says how the session began (a resume, a /clear) and under which id
  on('classic.SessionStart', ($, e, next) => {
    sid = e.session_id
    setMeta({ model: e.model, permission_mode: e.permission_mode, transcript_path: e.transcript_path, cwd: e.cwd })
    if (heartbeat === undefined && waiting !== undefined) waiting = e.source
    else start($, e.source)
    return next(e)
  }).catch((_$, e, next) => next(e))

  // A window or a phone opened the session: someone is looking at it now
  on('session.attach', ($, e, next) => {
    activate($)
    return next(e)
  })

  on('session.end', async ($, e, next) => {
    await endSession($, e.sessionId, e.reason)
    return next(e)
  })

  // The permission mode can change any time (shift+tab): pick it up per prompt
  on('classic.UserPromptSubmit', ($, e, next) => {
    if (setMeta({ permission_mode: e.permission_mode })) emit($, heartbeatEvent())
    return next(e)
  }).catch((_$, e, next) => next(e))

  on('turn.start', ($, e, next) => {
    if (e.agentId === undefined) {
      mainTurn = true
      const prompt = text(e.text, PROMPT_CHARS)
      if (e.text !== '') emit($, { type: 'prompt', preview: prompt })
      emit($, { type: 'turn.start', turn_id: e.turnId, preview: prompt })
    } else {
      agents.add(e.agentId)
      emit($, { type: 'turn.start', turn_id: e.turnId, agent_id: e.agentId, preview: text(e.text, PROMPT_CHARS) })
    }
    return next(e)
  })

  on('turn.complete', ($, e, next) => {
    if (e.agentId === undefined) {
      mainTurn = false
      if (e.usage?.model && setMeta({ model: e.usage.model })) emit($, heartbeatEvent())
    } else {
      agents.delete(e.agentId)
    }
    emit($, {
      type: 'turn.end',
      turn_id: e.turnId,
      agent_id: e.agentId,
      outcome: e.reason,
      duration_ms: Math.round(e.durationMs),
      usage: e.usage && {
        input: e.usage.input_tokens,
        output: e.usage.output_tokens,
        cache_read: e.usage.cache_read_input_tokens,
        cache_write: e.usage.cache_creation_input_tokens,
        model: e.usage.model,
      },
      preview: text(e.answer, PROMPT_CHARS),
    })
    return next(e)
  })

  // Wraps the call, its permission prompt included, to see it start and end
  on('tool.call', async ($, e, next) => {
    const tool = String(e.tool)
    const id = e.tool_use_id
    const subject = withText ? subjectOf(tool, e) : undefined
    emit($, { type: 'tool.start', id, tool, agent_id: e.agentId, subject })
    if (QUESTION_TOOLS.has(tool)) emit($, { type: 'attention', kind: 'input', tool, subject })
    const started = Date.now()
    let ok = false
    tools++
    try {
      const result = await next(e)
      ok = result.deny === undefined && result.isError !== true
      return result
    } finally {
      tools--
      emit($, { type: 'tool.end', id, tool, agent_id: e.agentId, ok, duration_ms: Date.now() - started })
    }
    // Watching never stands in the way of a call
  }).catch((_$, e, next) => next(e))

  on('classic.PermissionRequest', ($, e, next) => {
    emit($, {
      type: 'attention',
      kind: 'permission',
      tool: e.tool_name,
      subject: withText ? subjectOf(e.tool_name, e.tool_input) : undefined,
    })
    return next(e)
  }).catch((_$, e, next) => next(e))

  on('classic.Notification', ($, e, next) => {
    switch (e.notification_type) {
      case 'idle_prompt':
        emit($, { type: 'attention', kind: 'idle', message: text(e.message, PROMPT_CHARS) })
        break
      case 'elicitation_dialog':
        emit($, { type: 'attention', kind: 'input', message: text(e.message, PROMPT_CHARS) })
        break
      case 'permission_prompt':
        // PermissionRequest said so already, with the tool
        break
      default:
        emit($, { type: 'notice', kind: e.notification_type, message: text(e.message, PROMPT_CHARS) })
    }
    return next(e)
  }).catch((_$, e, next) => next(e))

  on('classic.PreCompact', ($, e, next) => {
    emit($, { type: 'compact.start', trigger: e.trigger })
    return next(e)
  }).catch((_$, e, next) => next(e))

  on('classic.PostCompact', ($, e, next) => {
    emit($, { type: 'compact.end' })
    return next(e)
  }).catch((_$, e, next) => next(e))

  // Context fill, cost and the account's rate-limit windows, after each turn
  // and whenever a window moves a point
  on('session.measure', ($, e, next) => {
    // Measuring is no use of the session: an unannounced one reports its
    // figures when it announces
    if (heartbeat !== undefined) emit($, usageEvent(e))
    return next(e)
  })
}
