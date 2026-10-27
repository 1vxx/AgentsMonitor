import type { On } from 'claude-code'
import { expect, mock, test } from 'claude-code/testing'

type Sent = { type: string; sid?: string; [field: string]: unknown }

// Stands in for the engine beneath the plugin and for the daemon: answers
// what the plugin asks of the session and records every batch it posts
const engine = (on: On, options: { down?: boolean } = {}) => {
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
  on('classic.PermissionRequest', () => ({}))
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
  expect(types).toEqual(['session.start', 'usage', 'prompt', 'turn.start', 'tool.start', 'tool.end', 'turn.end'])
  expect(posts[0]?.socketPath).toBe('/Users/me/.agentsmon/agentsmon.sock')
  expect(sent().every(e => e.sid === 'sess-1' && e.agent === 'claude-code')).toBe(true)

  const [start, usage, prompt, , tool] = sent()
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
  expect(sent().map(e => e.type)).toEqual(['session.start', 'usage', 'prompt', 'turn.start'])
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
