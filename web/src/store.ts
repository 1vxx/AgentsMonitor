import { useSyncExternalStore } from 'react'
import type { EventRecord, Quota, ServerInfo, ServerMsg, Session, Summary } from './types.ts'

// The live view of the daemon: one WebSocket, a snapshot first, deltas after.
// Every change makes a new state object, so React re-renders only readers of
// what changed.

export type Conn = 'connecting' | 'open' | 'closed'

export interface LiveState {
  conn: Conn
  server?: ServerInfo
  sessions: ReadonlyMap<string, Session>
  quotas: ReadonlyMap<string, Quota>
  summary?: Summary
  /** Newest first. */
  feed: readonly EventRecord[]
  /** Bumped whenever model requests were counted: token views refetch on it. */
  tokensTick: number
}

const FEED_MAX = 600

let state: LiveState = { conn: 'connecting', sessions: new Map(), quotas: new Map(), feed: [], tokensTick: 0 }
const listeners = new Set<() => void>()
// Events per session key, so an open drawer can append them
const eventListeners = new Set<(r: EventRecord) => void>()

const set = (next: Partial<LiveState>) => {
  state = { ...state, ...next }
  for (const l of listeners) l()
}

const apply = (msg: ServerMsg) => {
  switch (msg.type) {
    case 'snapshot':
      set({
        server: msg.server,
        sessions: new Map(msg.sessions.map(s => [s.key, s])),
        quotas: new Map(msg.quotas.map(q => [q.agent, q])),
        summary: msg.summary,
        // A (re)connect may have missed counts: token views read again.
        tokensTick: state.tokensTick + 1,
      })
      break
    case 'session': {
      const sessions = new Map(state.sessions)
      sessions.set(msg.session.key, msg.session)
      set({ sessions })
      break
    }
    case 'session.removed': {
      const sessions = new Map(state.sessions)
      sessions.delete(msg.key)
      set({ sessions })
      break
    }
    case 'quota': {
      const quotas = new Map(state.quotas)
      quotas.set(msg.quota.agent, msg.quota)
      set({ quotas })
      break
    }
    case 'summary':
      set({ summary: msg.summary })
      break
    case 'tokens':
      set({ tokensTick: state.tokensTick + 1 })
      break
    case 'event':
      set({ feed: [msg.record, ...state.feed].slice(0, FEED_MAX) })
      for (const l of eventListeners) l(msg.record)
      break
  }
}

let socket: WebSocket | undefined
let retry = 0
let started = false

const connect = () => {
  const url = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/v1/ws?feed=1`
  const ws = new WebSocket(url)
  socket = ws
  set({ conn: 'connecting' })
  ws.onopen = () => {
    retry = 0
    set({ conn: 'open' })
    // What happened before this page connected (or while it was away)
    fetch('/v1/events?limit=200')
      .then(res => (res.ok ? (res.json() as Promise<EventRecord[]>) : []))
      .then(seedFeed)
      .catch(() => {})
  }
  ws.onmessage = ev => {
    try {
      apply(JSON.parse(String(ev.data)) as ServerMsg)
    } catch (err) {
      console.warn('agentsmon: bad message', err)
    }
  }
  ws.onclose = () => {
    if (socket !== ws) return
    set({ conn: 'closed' })
    retry = Math.min(retry + 1, 6)
    setTimeout(connect, 500 * 2 ** (retry - 1))
  }
}

export const startLive = () => {
  if (started) return
  started = true
  connect()
}

/** Seeds the feed from the history, keeping anything already live. */
export const seedFeed = (records: EventRecord[]) => {
  const seen = new Set(state.feed.map(r => r.seq))
  const merged = [...state.feed, ...records.filter(r => !seen.has(r.seq))].sort((a, b) => b.seq - a.seq)
  set({ feed: merged.slice(0, FEED_MAX) })
}

const subscribe = (l: () => void) => {
  listeners.add(l)
  return () => listeners.delete(l)
}

export const useLive = <T,>(select: (s: LiveState) => T): T => useSyncExternalStore(subscribe, () => select(state))

export const onEvent = (l: (r: EventRecord) => void) => {
  eventListeners.add(l)
  return () => {
    eventListeners.delete(l)
  }
}
