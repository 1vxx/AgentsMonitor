// Plays the review page's scenarios through a theme in Node, with no browser:
// for pictures of it, and for checking that a change leaves its frames alone.
//
//   node devices/sim/snap.mjs png <theme> <scenario> <seconds> <out.png>
//       one frame, `seconds` into the scenario, as a round PNG
//   node devices/sim/snap.mjs record <theme> <out.json>
//       every frame of every scenario, plus scripted touches and button
//       presses, as a digest per frame and the chimes it asked for
//   node devices/sim/snap.mjs compare <before.json> <after.json>
//       the first frames that differ; exits 1 if any do
//   node devices/sim/snap.mjs check <theme>
//       plays every scenario twice, drawing whole frames and then 16-row
//       bands as a board does, and exits 1 if any frame differs: drawing a
//       band must not depend on another
//
// <theme> is a name under devices/themes, built by `devices/sim/build.sh`, or
// a path to a module built from it.

import { createHash } from 'node:crypto'
import { readFileSync, writeFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { deflateSync } from 'node:zlib'
import { SCENARIOS } from './scenarios.js'
import { World } from './world.js'

const HERE = dirname(fileURLToPath(import.meta.url))
const SIZE = 466
const LINK = { connecting: 0, online: 1, lost: 2 }
const BASE = 1793000000000 // the epoch ms every run starts at, so runs repeat exactly
const DOWN = 0, MOVE = 1, UP = 2

// ---------- The theme ----------

const load = async (theme, rows = 0) => {
  const path = theme.endsWith('.js') ? resolve(theme) : join(HERE, 'build', `${theme}.js`)
  const createUi = (await import(pathToFileURL(path).href)).default
  const mod = await createUi()
  const copyIn = bytes => {
    const p = mod._malloc(bytes.length + 1)
    mod.HEAPU8.set(bytes, p)
    mod.HEAPU8[p + bytes.length] = 0
    return p
  }
  const cjk = readFileSync(join(HERE, '..', 'fonts', 'cjk10.bin'))
  if (!mod._am_init(copyIn(cjk), cjk.length)) throw new Error(`${theme}: init failed`)
  const encoder = new TextEncoder()
  return {
    link: state => mod._am_link(LINK[state]),
    summary: json => {
      const p = copyIn(encoder.encode(json))
      mod._am_summary(p)
      mod._free(p)
    },
    touch: (kind, x, y, ms) => mod._am_touch(kind, x, y, ms),
    press: ms => mod._am_press(ms),
    summon: ms => mod._am_summon(ms),
    pixels: (ms, wall) => {
      const p = mod._am_frame(ms, wall, rows)
      return mod.HEAPU8.subarray(p, p + SIZE * SIZE * 4)
    },
    cues: () => {
      const out = []
      for (let c; (c = mod._am_cue()) >= 0; ) out.push(c)
      return out
    },
    screen: () => {
      let s = ''
      for (let i = mod._am_screen(); mod.HEAPU8[i]; i++) s += String.fromCharCode(mod.HEAPU8[i])
      return s
    },
  }
}

// ---------- Scripted hands ----------

// A finger moving from one point to another in `ms`, in a few steps
const drag = (at, [x0, y0], [x1, y1], ms = 160, steps = 5) => [
  [at, 'touch', DOWN, x0, y0],
  ...Array.from({ length: steps }, (_, i) => {
    const k = (i + 1) / steps
    return [at + (ms * k) / 1000, 'touch', MOVE, Math.round(x0 + (x1 - x0) * k), Math.round(y0 + (y1 - y0) * k)]
  }),
  [at + ms / 1000 + 0.01, 'touch', UP, x1, y1],
]
const tap = (at, x, y) => [
  [at, 'touch', DOWN, x, y],
  [at + 0.08, 'touch', UP, x, y],
]

// What a person does during a run, in seconds from its start: both themes
// answer every one of these, each in its own way
const HANDS = [
  ...drag(6, [330, 330], [120, 330]), // sideways: the next session
  ...drag(9, [120, 330], [340, 330]), // and back
  ...drag(12, [233, 380], [233, 200]), // up: every session
  ...drag(14, [233, 350], [233, 120], 90), // a flick through the list
  ...tap(16, 233, 245), // open one
  ...drag(18, [233, 140], [233, 380]), // down: back
  [21, 'press'],
  [21.2, 'press'], // twice
  [24, 'press'],
  [26, 'summon'],
  ...drag(29, [233, 380], [233, 330], 120), // up, but not far enough: it springs back
]

// ---------- A run ----------

// Plays one scenario: `fps` frames a second for its first `dense` seconds,
// then one a second to its end. Calls see(t, pixels) after every frame.
const play = (ui, scenario, { fps = 20, dense = 60, hands = true, until = Infinity }, see) => {
  const world = new World()
  const end = Math.min(until, scenario.length)
  const queue = hands ? HANDS.filter(h => h[0] < end).slice() : []
  let step = 0
  let lastLink = null
  let lastSummary = ''
  let t = 0
  ui.cues()
  while (t <= end) {
    const now = BASE + t * 1000
    while (step < scenario.script.length && scenario.script[step][0] <= t) scenario.script[step++][1](world, now)
    if (world.link !== lastLink) {
      ui.link(world.link)
      lastLink = world.link
      lastSummary = ''
    }
    if (world.link === 'online') {
      const sum = world.summary(now)
      const key = JSON.stringify({ ...sum, ts: 0 })
      if (key !== lastSummary) {
        ui.summary(JSON.stringify(sum))
        lastSummary = key
      }
    }
    while (queue.length && queue[0][0] <= t) {
      const [at, what, ...args] = queue.shift()
      const ms = 1000 + at * 1000
      if (what === 'touch') ui.touch(args[0], args[1], args[2], ms)
      else ui[what](ms)
    }
    see(t, ui.pixels(1000 + t * 1000, now))
    t = t < dense ? Math.round((t + 1 / fps) * 1000) / 1000 : t + 1
  }
}

// ---------- PNG ----------

const crcTable = Array.from({ length: 256 }, (_, n) => {
  let c = n
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
  return c >>> 0
})
const crc = buf => {
  let c = 0xffffffff
  for (const b of buf) c = crcTable[(c ^ b) & 255] ^ (c >>> 8)
  return (c ^ 0xffffffff) >>> 0
}
const chunk = (type, data) => {
  const out = Buffer.alloc(12 + data.length)
  out.writeUInt32BE(data.length, 0)
  out.write(type, 4, 'ascii')
  data.copy(out, 8)
  out.writeUInt32BE(crc(out.subarray(4, 8 + data.length)), 8 + data.length)
  return out
}
// RGBA, its corners already transparent
const png = rgba => {
  const raw = Buffer.alloc((SIZE * 4 + 1) * SIZE)
  for (let y = 0; y < SIZE; y++) Buffer.from(rgba.buffer, rgba.byteOffset + y * SIZE * 4, SIZE * 4).copy(raw, y * (SIZE * 4 + 1) + 1)
  const head = Buffer.alloc(13)
  head.writeUInt32BE(SIZE, 0)
  head.writeUInt32BE(SIZE, 4)
  head.set([8, 6, 0, 0, 0], 8)
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', head), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))])
}

// ---------- Records ----------

// Every frame of every scenario: its time, a digest of its pixels, the
// screen showing and the chimes asked for
const record = ui => {
  const runs = {}
  for (const scenario of SCENARIOS) {
    const frames = []
    play(ui, scenario, {}, (t, px) => {
      frames.push([t, createHash('sha1').update(px).digest('hex').slice(0, 16), ui.screen(), ui.cues().join(',')])
    })
    runs[scenario.id] = frames
    console.error(`${scenario.id}: ${frames.length} frames`)
  }
  return runs
}

// Prints how two records compare, scenario by scenario; returns how many differ
const compare = (a, b) => {
  let differ = 0
  for (const id of Object.keys(a)) {
    const fa = a[id], fb = b[id] ?? []
    const at = fa.findIndex((f, i) => !fb[i] || f.join() !== fb[i].join())
    if (at < 0 && fa.length === fb.length) {
      console.log(`${id}: ${fa.length} frames, all the same`)
      continue
    }
    differ++
    const i = at < 0 ? fa.length : at
    console.log(`${id}: differs from ${fa[i]?.[0] ?? fb[i]?.[0]} s`, { before: fa[i], after: fb[i] })
  }
  return differ
}

// ---------- Commands ----------

const scenarioOf = id => SCENARIOS.find(s => s.id === id) ?? fail(`no scenario ${id}; there are ${SCENARIOS.map(s => s.id).join(', ')}`)
const fail = message => {
  console.error(message)
  process.exit(2)
}

const [command, ...args] = process.argv.slice(2)
if (command === 'png') {
  const [theme, id, seconds, out] = args
  if (!out) fail('usage: snap.mjs png <theme> <scenario> <seconds> <out.png>')
  const ui = await load(theme)
  let last = null
  play(ui, scenarioOf(id), { hands: false, dense: Infinity, until: +seconds }, (t, px) => (last = px))
  writeFileSync(out, png(last))
} else if (command === 'record') {
  const [theme, out] = args
  if (!out) fail('usage: snap.mjs record <theme> <out.json>')
  writeFileSync(out, JSON.stringify(record(await load(theme))))
} else if (command === 'compare') {
  const [a, b] = args.map(f => JSON.parse(readFileSync(f, 'utf8')))
  process.exit(compare(a, b) ? 1 : 0)
} else if (command === 'check') {
  const [theme] = args
  if (!theme) fail('usage: snap.mjs check <theme>')
  const whole = record(await load(theme))
  const bands = record(await load(theme, 16))
  process.exit(compare(whole, bands) ? 1 : 0)
} else {
  fail('usage: snap.mjs png | record | compare | check (see the top of this file)')
}
