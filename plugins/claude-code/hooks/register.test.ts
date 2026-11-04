import type { On } from 'claude-code'
import { expect, mock, test } from 'claude-code/testing'

type Sent = { type: string; sid?: string; [field: string]: unknown }

// What a compaction runs over
const messages = [{ role: 'user' as const, text: 'fix the bug', toolUses: [] }]

// Stands in for the engine beneath the plugin and for the daemon: answers
// what the plugin asks of the session and records every batch it posts
type Options = {
  down?: boolean
  tty?: string
  summarize?: () => Promise<void>
  // What the engine does with a prompt that enters: its hooks, then its turn
  enter?: (text: string) => Promise<unknown>
  drop?: boolean
}

const engine = (on: On, options: Options = {}) => {
  const posts: { socketPath?: string; events: Sent[] }[] = []
  const state = { down: options.down === true }
  on('session.id', () => ({ value: 'sess-1' }))
  on('session.cwd', () => ({ value: '/work/demo' }))
  on('session.model', () => ({ value: 'claude-opus-5-5' }))
  on('session.surfaces', () => ({ value: ['terminal'] }))
  on('session.version', () => ({ value: { version: '2.1.308', base: '2.1.308', builtAt: '' } }))
  on('session.repo', () => ({ value: { root: '/work/demo-repo', remote: null, internal: false, name: null } }))
  on('session.usage', () => ({
    value: {
      startedAt: 0,
      context: { window: 200_000, tokens: 1_000, percent: 0.5 },
      rateLimits: [{ kind: 'five_hour', percentUsed: 12.5, resetsAt: '2026-10-27T12:00:00Z' }],
      cost: { usd: 0.25 },
    },
  }))
  on('http.fetch', (_$, e) => {
    if (state.down) throw new Error('connect ENOENT')
    posts.push({ socketPath: e.init?.socketPath, events: JSON.parse(String(e.init?.body)) as Sent[] })
    return { value: { status: 200, ok: true, headers: {}, text: '{"accepted":1}' } }
  })
  on('session.start', (_$, e) => ({ cwd: e.cwd }))
  on('turn.start', (_$, e) => ({ turnId: e.turnId }))
  on('turn.complete', (_$, e) => ({ text: e.answer }))
  on('tool.call', () => ({ result: {} }))
  on('session.end', (_$, e) => ({ sessionId: e.sessionId }))
  on('session.measure', (_$, e) => ({ changed: e.changed }))
  // No settings hooks: each classic event settles with nothing to say
  on('classic.SessionStart', () => ({}))
  on('session.attach', (_$, e) => ({ clientId: e.clientId }))
  on('session.detach', (_$, e) => ({ clientId: e.clientId }))
  on('prompt.submit', async (_$, e) => {
    if (options.drop) return { drop: 'blocked by a hook' }
    await options.enter?.(e.text)
    return { text: e.text }
  })
  on('process.run', () => ({ value: { exitCode: 0, stdout: `${options.tty ?? 'ttys012'}\n`, stderr: '' } }))
  on('classic.PermissionRequest', () => ({}))
  on('session.compact', async (_$, e) => {
    // The summarizer takes a while
    await options.summarize?.()
    return { messages: e.messages }
  })
  const sent = () => posts.flatMap(p => p.events)
  return { posts, sent, state }
}

test('a turn reaches the daemon in order, after the hooks returned', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000 })
  mock.env(on, { HOME: '/Users/me' })
  const { posts, sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await $.turn.start({ text: 'fix   the bug', turnId: 't1' })
  await $.tool.call({ tool: 'Bash', command: 'cargo test', tool_use_id: 'u1' })
  await $.turn.complete({ answer: 'Done.', durationMs: 4_200, isAborted: false, turnId: 't1', reason: 'answer' })

  // Nothing was sent inside the hooks themselves
  expect(posts.length).toBe(0)
  await clock.settle()

  const types = sent().map(e => e.type)
  expect(types).toEqual(['session.start', 'usage', 'attend', 'prompt', 'turn.start', 'tool.start', 'tool.end', 'turn.end'])
  expect(posts[0]?.socketPath).toBe('/Users/me/.agentsmon/agentsmon.sock')
  expect(sent().every(e => e.sid === 'sess-1' && e.agent === 'claude-code')).toBe(true)

  const [start, usage, , prompt, , tool] = sent()
  expect(start).toMatchObject({ source: 'startup', project: 'demo-repo', model: 'claude-opus-5-5', interactive: true })
  expect(usage).toMatchObject({ rate_limits: [{ kind: 'five_hour', percent_used: 12.5 }], cost_usd: 0.25 })
  expect(prompt?.preview).toBe('fix the bug')
  expect(tool).toMatchObject({ tool: 'Bash', id: 'u1', subject: 'cargo test' })
})

test('the two start events at launch announce the session once', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await clock.settle()
  await $.classic.SessionStart({ source: 'startup', session_id: 'sess-1', permission_mode: 'acceptEdits' })
  await clock.settle()

  expect(sent().filter(e => e.type === 'session.start').length).toBe(1)
  expect(sent().at(-1)).toMatchObject({ type: 'heartbeat', permission_mode: 'acceptEdits' })
})

test('a warm spare session nobody uses never shows up', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { posts } = engine(on)

  // Claude Desktop starts one ahead of time, headless, and may drop it unused
  await $.session.start({ cwd: '/work/demo', surface: null, isInteractive: false })
  await $.classic.SessionStart({ source: 'startup', session_id: 'spare-1' })
  await $.session.measure({ context: { window: 200_000 }, rateLimits: [], changed: ['context'] })
  await clock.advance(60_000)
  await $.session.end({ reason: 'other', sessionId: 'spare-1', resume: { id: 'spare-1' } })
  await clock.settle()

  expect(posts.length).toBe(0)
})

test('a non-interactive session announces itself once a window attaches', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: null, isInteractive: false })
  await $.classic.SessionStart({ source: 'startup', session_id: 'sess-1' })
  await clock.advance(5_000)
  expect(sent().length).toBe(0)

  await $.session.attach({ surface: 'desktop', clientId: 'c1' })
  await clock.settle()
  expect(sent()[0]).toMatchObject({ type: 'session.start', source: 'startup', sid: 'sess-1', project: 'demo-repo' })
})

test('opening a session and leaving it tell the daemon the person was there', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: null, isInteractive: false })
  await $.session.attach({ surface: 'desktop', clientId: 'c1' })
  await clock.settle()
  await $.session.detach({ surface: 'desktop', clientId: 'c1', reason: 'detach' })
  await clock.settle()
  await $.session.attach({ surface: 'desktop', clientId: 'c1' })
  await $.session.detach({ surface: 'desktop', clientId: 'c1', reason: 'end' })
  await clock.settle()

  expect(sent().filter(e => e.type === 'attend').map(e => e.leaving ?? false)).toEqual([false, true, false])
})

test('a warm spare left unused says nothing of anyone leaving', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { posts } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: null, isInteractive: false })
  await $.session.detach({ surface: 'desktop', clientId: 'c1', reason: 'detach' })
  await clock.settle()
  expect(posts.length).toBe(0)
})

const submit = (kind: string) => ({ text: 'go', wait: false, origin: { kind } }) as never

test('only what a person sent counts as turning to the session', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, { CLAUDE_CODE_ENTRYPOINT: 'claude-desktop' })
  const { sent } = engine(on)
  const attends = () => sent().filter(e => e.type === 'attend').length

  await $.session.start({ cwd: '/work/demo', surface: null, isInteractive: false })
  for (const kind of ['task-notification', 'scheduled-trigger', 'auto-continuation', 'peer']) await $.prompt.submit(submit(kind))
  await clock.settle()
  expect(attends()).toBe(0)

  for (const kind of ['composer', 'bridge', 'sdk']) await $.prompt.submit(submit(kind))
  await clock.settle()
  expect(attends()).toBe(3)
})

test('starting a session at a terminal is someone turning to it', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await clock.settle()
  expect(sent().filter(e => e.type === 'attend').length).toBe(1)
})

test('a script is nobody turning to the session', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, { CLAUDE_CODE_ENTRYPOINT: 'sdk-cli' })
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: null, isInteractive: false })
  await $.prompt.submit(submit('sdk'))
  await $.turn.start({ text: 'go', turnId: 't1' })
  await clock.settle()
  expect(sent().some(e => e.type === 'attend')).toBe(false)
})

test('a turn shows from the moment its prompt is sent, not once its hooks are done', async ($, on) => {
  const clock = mock.clock(on, { now: 1_000 })
  mock.env(on, {})
  // A UserPromptSubmit hook that takes 30 s, then the turn
  const enter = async (text: string) => {
    await clock.sleep(30_000)
    await $.turn.start({ text, turnId: 't1' })
  }
  const { sent } = engine(on, { enter })

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  const sending = $.prompt.submit(submit('composer'))
  await clock.settle()
  expect(sent().filter(e => e.type === 'prompt' || e.type === 'turn.start')).toMatchObject([
    { type: 'prompt', preview: 'go' },
    { type: 'turn.start', preview: 'go' },
  ])

  await clock.advance(30_000)
  await sending
  await $.turn.complete({ answer: '', durationMs: 1, isAborted: false, turnId: 't1', reason: 'answer' })
  await clock.settle()
  expect(sent().filter(e => e.type === 'turn.start').length).toBe(1)
  expect(sent().filter(e => e.type === 'heartbeat').map(b => b.busy)).toEqual([true])
  expect(sent().at(-1)?.type).toBe('turn.end')
})

test('a prompt a hook drops takes back the turn it showed', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on, { drop: true })

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await $.prompt.submit(submit('composer'))
  await clock.settle()
  const after = sent().slice(sent().findIndex(e => e.type === 'turn.start'))
  expect(after).toMatchObject([{ type: 'turn.start' }, { type: 'heartbeat', busy: false }])
})

test('a prompt typed over a running turn joins it', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await $.turn.start({ text: 'one', turnId: 't1' })
  await $.prompt.submit({ ...submit('composer'), turnId: 't1' })
  await clock.settle()
  expect(sent().filter(e => e.type === 'turn.start').length).toBe(1)
  expect(sent().some(e => e.type === 'heartbeat')).toBe(false)
})

test('a session at a terminal says which one', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await clock.settle()
  expect(sent()[0]).toMatchObject({ type: 'session.start', tty: 'ttys012' })
})

test('a session with no terminal says none', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on, { tty: '??' })

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await clock.settle()
  expect(sent()[0]?.tty).toBeUndefined()
})

test('a heartbeat says whether work is in flight', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: null, isInteractive: false })
  await $.turn.start({ text: 'go', turnId: 't1' })
  await clock.advance(20_000)
  await $.turn.complete({ answer: '', durationMs: 1, isAborted: false, turnId: 't1', reason: 'answer' })
  await clock.advance(20_000)

  const beats = sent().filter(e => e.type === 'heartbeat')
  expect(beats.map(b => b.busy)).toEqual([true, false])
})

test('a process that does no work says so once, then nothing about work', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  // Once for a process that died mid-turn; after that the same session, open
  // in another window, may be at work there
  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await clock.advance(60_000)
  expect(sent().filter(e => e.type === 'heartbeat').map(b => b.busy)).toEqual([false, undefined, undefined])
})

test('a turn between two heartbeats is still said to be over, once', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await $.turn.start({ text: 'go', turnId: 't1' })
  await $.turn.complete({ answer: '', durationMs: 1, isAborted: false, turnId: 't1', reason: 'answer' })
  await clock.advance(40_000)
  expect(sent().filter(e => e.type === 'heartbeat').map(b => b.busy)).toEqual([false, undefined])
})

test('a /compact between turns keeps the session busy until it is done', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on, { summarize: () => clock.sleep(30_000) })

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await $.turn.start({ text: 'go', turnId: 't1' })
  await $.turn.complete({ answer: '', durationMs: 1, isAborted: false, turnId: 't1', reason: 'answer' })
  const compacting = $.session.compact({ trigger: 'manual', messages })
  await clock.advance(20_000)
  expect(sent().filter(e => e.type.startsWith('compact')).map(e => e.type)).toEqual(['compact.start'])
  expect(sent().filter(e => e.type === 'heartbeat').map(b => b.busy)).toEqual([true])

  await clock.advance(10_000)
  await compacting
  await clock.advance(10_000)
  expect(sent().filter(e => e.type.startsWith('compact'))).toMatchObject([
    { type: 'compact.start', trigger: 'manual' },
    { type: 'compact.end' },
  ])
  expect(sent().filter(e => e.type === 'heartbeat').map(b => b.busy)).toEqual([true, false])
})

test('a compaction worked out ahead of time is no compaction', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  // Claude Code prepares one while the session rests, and installs nothing
  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await $.session.compact({ trigger: 'precompute', messages })
  await clock.settle()
  expect(sent().some(e => e.type.startsWith('compact'))).toBe(false)
})

test('events wait while the daemon is away and go out once it is back', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent, state } = engine(on, { down: true })

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await $.turn.start({ text: 'one', turnId: 't1' })
  await clock.settle()
  expect(sent().length).toBe(0)

  state.down = false
  await clock.advance(1_000)
  expect(sent().map(e => e.type)).toEqual(['session.start', 'usage', 'attend', 'prompt', 'turn.start'])
})

test('a question and a permission prompt ask for the person', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await $.tool.call({ tool: 'AskUserQuestion', questions: [{ question: 'Which DB?', header: 'DB', options: [], multiSelect: false }] })
  await $.classic.PermissionRequest({ tool_name: 'Bash', tool_input: { command: 'rm -rf build' } })
  await clock.settle()

  const asks = sent().filter(e => e.type === 'attention')
  expect(asks).toMatchObject([
    { kind: 'input', tool: 'AskUserQuestion', subject: 'Which DB?' },
    { kind: 'permission', tool: 'Bash', subject: 'rm -rf build' },
  ])
})

test('with detail none no text leaves the session', { options: { detail: 'none' } }, async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await $.turn.start({ text: 'secret plan', turnId: 't1' })
  await $.tool.call({ tool: 'Bash', command: 'cat secrets.txt' })
  await clock.settle()

  const leaked = JSON.stringify(sent())
  expect(leaked.includes('secret')).toBe(false)
  expect(sent().some(e => e.type === 'prompt')).toBe(true)
})

test('the end goes out at once, and a /clear carries on under the new id', async ($, on) => {
  const clock = mock.clock(on)
  mock.env(on, {})
  const { sent } = engine(on)

  await $.session.start({ cwd: '/work/demo', surface: 'terminal', isInteractive: true })
  await clock.settle()
  await $.session.end({ reason: 'clear', sessionId: 'sess-1', resume: { id: 'sess-1' } })
  const end = sent().find(e => e.type === 'session.end')
  expect(end).toMatchObject({ sid: 'sess-1', reason: 'clear' })

  await $.classic.SessionStart({ source: 'clear', session_id: 'sess-2' })
  await clock.settle()
  const restart = sent().filter(e => e.type === 'session.start').at(-1)
  expect(restart).toMatchObject({ sid: 'sess-2', source: 'clear' })
})
