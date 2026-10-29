// The review page: runs a theme (build/<theme>.js, see build.sh) on a canvas,
// turns the mouse into touches, plays its chimes, and feeds it summaries from
// a scripted world or from the daemon itself.

import { apply, lang, pick, setLang, t } from './i18n.js'
import { SCENARIOS } from './scenarios.js'
import { World } from './world.js'

const SIZE = 466
const LINK = { connecting: 0, online: 1, lost: 2 }
const $ = id => document.getElementById(id)

const saved = key => {
  try {
    return localStorage.getItem(key)
  } catch {
    return null
  }
}
const save = (key, value) => {
  try {
    localStorage.setItem(key, value)
  } catch {}
}

apply()
$('readout').textContent = t('loading')

// ---------- The theme ----------

// Every build holds one theme, so another one is another module: the page
// loads the one asked for (?theme=, else the last one shown), and switching
// reloads the page with it
const THEMES = await fetch('build/themes.json').then(r => r.json())
const asked = new URLSearchParams(location.search).get('theme') || saved('am.theme')
const theme = THEMES.find(x => x.id === asked) ?? THEMES[0]
save('am.theme', theme.id)
for (const x of THEMES) $('theme').append(new Option(pick(x.name), x.id, false, x === theme))
$('theme').onchange = e => {
  const url = new URL(location.href)
  url.searchParams.set('theme', e.target.value)
  location.href = url.href
}
const describeTheme = () => {
  for (const o of $('theme').options) o.textContent = pick(THEMES.find(x => x.id === o.value).name)
  $('theme-about').textContent = `${pick(theme.about)} ${t('by', { author: theme.author })}`
}

const createUi = (await import(`./build/${theme.id}.js`)).default
const mod = await createUi()
const encoder = new TextEncoder()

const copyIn = bytes => {
  const p = mod._malloc(bytes.length + 1)
  mod.HEAPU8.set(bytes, p)
  mod.HEAPU8[p + bytes.length] = 0
  return p
}

const cjk = new Uint8Array(await fetch('../fonts/cjk10.bin').then(r => r.arrayBuffer()))
// The font stays in the module's memory for good: text is drawn straight from it
if (!mod._am_init(copyIn(cjk), cjk.length)) throw new Error('UI init failed')

const ui = {
  link: state => mod._am_link(LINK[state]),
  summary: json => {
    const p = copyIn(encoder.encode(json))
    const ok = mod._am_summary(p)
    mod._free(p)
    return ok
  },
  touch: (kind, x, y, ms) => mod._am_touch(kind, x, y, ms),
  press: ms => mod._am_press(ms),
  summon: ms => mod._am_summon(ms),
  frame: (ms, wall) => mod._am_frame(ms, wall, 0),
  cue: () => mod._am_cue(),
  screen: () => {
    const p = mod._am_screen()
    let s = ''
    for (let i = p; mod.HEAPU8[i]; i++) s += String.fromCharCode(mod.HEAPU8[i])
    return s
  },
}

// ---------- Sound ----------

let audio = null
let gain = null

const play = cue => {
  if (!audio) return
  const n = mod._am_synth(cue)
  const samples = new Int16Array(mod.HEAP16.buffer, mod._am_pcm(), n)
  const buf = audio.createBuffer(1, n, 24000)
  const ch = buf.getChannelData(0)
  for (let i = 0; i < n; i++) ch[i] = samples[i] / 32768
  const src = audio.createBufferSource()
  src.buffer = buf
  src.connect(gain)
  src.start()
}

const cueName = c => t(`cue.${c}`)

const log = text => {
  const li = document.createElement('li')
  li.textContent = `${new Date().toLocaleTimeString()}  ${text}`
  $('log').prepend(li)
  while ($('log').children.length > 30) $('log').lastChild.remove()
}

$('sound').onclick = () => {
  if (audio) {
    audio.close()
    audio = null
    $('sound').textContent = t('sound.on')
    return
  }
  audio = new AudioContext()
  gain = audio.createGain()
  gain.gain.value = +$('volume').value
  gain.connect(audio.destination)
  $('sound').textContent = t('sound.off')
}
$('volume').oninput = () => gain && (gain.gain.value = +$('volume').value)
document.querySelectorAll('[data-cue]').forEach(b => (b.onclick = () => play(+b.dataset.cue)))

// ---------- Sources ----------

let source = 'script'
let scenario = SCENARIOS[0]
let world = new World()
let step = 0
let virtual = 0 // seconds into the script
let playing = true
let base = Date.now()
let lastLink = null
let lastSummary = ''

const now = () => base + virtual * 1000

const resetScript = () => {
  world = new World()
  step = 0
  virtual = 0
  base = Date.now()
  lastSummary = ''
  lastLink = null
}

const advanceScript = dt => {
  if (playing) virtual += dt * +$('speed').value
  if (scenario.loop && virtual >= scenario.length) resetScript()
  while (step < scenario.script.length && scenario.script[step][0] <= virtual) scenario.script[step++][1](world, now())
  pushWorld()
}

const pushWorld = () => {
  if (world.link !== lastLink) {
    ui.link(world.link)
    lastLink = world.link
    lastSummary = ''
  }
  if (world.link !== 'online') return
  const sum = world.summary(now())
  const json = JSON.stringify({ ...sum, ts: 0 })
  if (json !== lastSummary) {
    ui.summary(JSON.stringify(sum))
    lastSummary = json
  }
}

for (const s of SCENARIOS) $('scenario').append(new Option(pick(s.name), s.id))
const describeScenario = () => {
  for (const o of $('scenario').options) o.textContent = pick(SCENARIOS.find(s => s.id === o.value).name)
  $('scenario-note').textContent = pick(scenario.note)
}
const pickScenario = id => {
  scenario = SCENARIOS.find(s => s.id === id)
  describeScenario()
  resetScript()
}
$('scenario').onchange = e => pickScenario(e.target.value)
pickScenario(scenario.id)

$('play').onclick = () => {
  playing = !playing
  $('play').textContent = t(playing ? 'pause' : 'resume')
}
$('restart').onclick = () => resetScript()

// The live daemon
let ws = null
let retry = null

// The address and token are kept in this browser between visits. A browser
// WebSocket can't send an Authorization header, so the token rides in the URL.
$('ws-url').value = saved('am.ws-url') || $('ws-url').value
$('ws-token').value = saved('am.ws-token') || ''

const liveUrl = () => {
  const token = $('ws-token').value.trim()
  save('am.ws-url', $('ws-url').value.trim())
  save('am.ws-token', token)
  try {
    const url = new URL($('ws-url').value.trim())
    if (token) url.searchParams.set('token', token)
    return url.href
  } catch {
    return null
  }
}

// What the link is doing, kept as a key so it can be said again in another language
let wsKey = 'ws.idle'
const wsSay = key => {
  wsKey = key
  $('ws-state').textContent = t(key)
}

const connectLive = () => {
  clearTimeout(retry)
  ws?.close()
  ui.link('connecting')
  const url = liveUrl()
  if (!url) {
    ws = null
    ui.link('lost')
    wsSay('ws.bad')
    return
  }
  wsSay('ws.connecting')
  const sock = new WebSocket(url)
  ws = sock
  sock.onopen = () => {
    ui.link('online')
    wsSay('ws.open')
  }
  sock.onmessage = e => {
    const m = JSON.parse(e.data)
    if ((m.type === 'snapshot' || m.type === 'summary') && m.summary) {
      ui.summary(JSON.stringify(m.summary))
      liveSessions = m.summary.sessions
    }
  }
  sock.onclose = () => {
    if (ws !== sock) return
    ui.link('lost')
    wsSay('ws.closed')
    if (source === 'live') retry = setTimeout(connectLive, 3000)
  }
}
let liveSessions = []

document.querySelectorAll('#source button').forEach(
  b =>
    (b.onclick = () => {
      source = b.dataset.v
      document.querySelectorAll('#source button').forEach(x => x.classList.toggle('on', x === b))
      $('script-box').hidden = source !== 'script'
      $('events-box').hidden = source !== 'script'
      $('live-box').hidden = source !== 'live'
      if (source === 'live') {
        connectLive()
      } else {
        clearTimeout(retry)
        const sock = ws
        ws = null
        sock?.close()
        resetScript()
      }
    }),
)
$('ws-connect').onclick = connectLive

// ---------- Hand-made events ----------

const TITLES = ['Investigate UI stutter', 'Daemon DB growth and event retention', 'Theme switch polish', 'Live session ordering', 'Device UI prototype', 'Translate README to Chinese', 'Count quotas from logs', 'Codex plugin icon']
const COMMANDS = ['pnpm --dir web build', 'cargo test -p agentsmon', 'git push origin main', 'rm -rf node_modules && pnpm i', 'curl -s localhost:7788/v1/summary']
const anyOf = list => list[Math.floor(Math.random() * list.length)]
let made = 0

const newSession = () => {
  const key = `claude-code:m${++made}`
  world.start(now(), key, { project: 'AgentsMonitor', title: anyOf(TITLES) })
  world.tool(now(), key, anyOf(['Read', 'Edit', 'Bash', 'Grep']))
  return key
}
const working = () => world.list.filter(s => ['thinking', 'tool', 'compacting'].includes(s.status) && !s.stale)
const anyWorking = () => anyOf(working())?.key ?? newSession()

const EVENTS = {
  permission: () => world.ask(now(), newSession(), { tool: 'Bash', subject: anyOf(COMMANDS) }),
  input: () => world.ask(now(), anyWorking(), { kind: 'input', tool: 'AskUserQuestion', message: 'Fix the failing tests first, or commit what is there?' }),
  answer: () => world.list.forEach(s => world.answer(now(), s.key)),
  done: () => world.finish(now(), anyWorking(), 'answer'),
  open: () => world.list.forEach(s => world.open(now(), s.key)),
  error: () => world.finish(now(), anyWorking(), 'error'),
  start: () => newSession(),
  stale: () => {
    const s = anyOf(world.list.filter(x => !x.stale))
    if (s) world.setStale(now(), s.key, true)
  },
  end: () => {
    const s = anyOf(world.list)
    if (s) world.end(s.key)
  },
  link: () => (world.link = world.link === 'online' ? 'lost' : 'online'),
}
document.querySelectorAll('[data-ev]').forEach(b => (b.onclick = () => EVENTS[b.dataset.ev]()))

// ---------- Touch ----------

const canvas = $('screen')
const ctx = canvas.getContext('2d')
const point = e => {
  const r = canvas.getBoundingClientRect()
  return [Math.round(((e.clientX - r.left) / r.width) * SIZE), Math.round(((e.clientY - r.top) / r.height) * SIZE)]
}
canvas.onpointerdown = e => {
  canvas.setPointerCapture(e.pointerId)
  ui.touch(0, ...point(e), performance.now())
}
canvas.onpointermove = e => e.buttons && ui.touch(1, ...point(e), performance.now())
canvas.onpointerup = e => ui.touch(2, ...point(e), performance.now())

// The side button: pressed once or twice, by mouse or the B key
// (on the frames' clock, which a scripted run moves ahead)
const side = () => ui.press(performance.now() + skew)
$('side').onclick = side
addEventListener('keydown', e => {
  if (e.key.toLowerCase() === 'b' && !e.repeat && !(e.target instanceof HTMLInputElement)) side()
})

// The other button: held 3 s, it summons, once per hold
let holding
$('boot').onpointerdown = e => {
  $('boot').setPointerCapture(e.pointerId)
  holding = setTimeout(() => ui.summon(performance.now() + skew), 3000)
}
$('boot').onpointerup = $('boot').onpointercancel = () => clearTimeout(holding)

// ---------- Display ----------

const applyZoom = () => {
  const z = +$('zoom').value
  canvas.style.width = canvas.style.height = `${SIZE * z}px`
  canvas.classList.toggle('pixelated', z > 1)
}
$('zoom').onchange = applyZoom
applyZoom()

const escape = text => String(text).replace(/[&<>"]/g, c => `&#${c.charCodeAt(0)};`)
const showSessions = list => {
  const rows = list.map(s => {
    const state = s.stale ? t('status.stale') : `${t(`status.${s.status}`)}${s.status === 'tool' && s.tool ? ` ${s.tool}` : ''}`
    const end = s.status === 'idle' && s.last_turn ? ` · ${s.last_turn.outcome}` : ''
    return `<tr><td>${escape(s.title || s.project)}</td><td class="state">${escape(state + end)}</td></tr>`
  })
  $('sessions').innerHTML = rows.join('') || `<tr><td class="state">${t('status.none')}</td></tr>`
}

// ---------- The loop ----------

let lastFrame = 0
let lastTick = performance.now()
let frames = 0
let drawMs = 0
let statAt = performance.now()
// Time skipped by preview.run(), added to the page clock so it stays monotonic
let skew = 0

const tick = forced => {
  const at = performance.now() + skew
  if (!forced && at - lastFrame < 1000 / +$('fps').value - 2) return
  lastFrame = at
  const dt = Math.min(1, (at - lastTick) / 1000)
  lastTick = at

  if (source === 'script') advanceScript(dt)
  const wall = source === 'script' ? now() : Date.now()
  const ptr = ui.frame(at, wall)
  ctx.putImageData(new ImageData(new Uint8ClampedArray(mod.HEAPU8.buffer, ptr, SIZE * SIZE * 4).slice(), SIZE, SIZE), 0, 0)
  drawMs += mod._am_draw_ms()
  frames++

  for (let c; (c = ui.cue()) >= 0; ) {
    log(t('log.cue', { name: cueName(c) }) + (audio ? '' : t('log.muted')))
    play(c)
  }

  if (at - statAt > 500) {
    const fps = (frames * 1000) / (at - statAt)
    $('readout').textContent = t('readout', { screen: ui.screen(), fps: fps.toFixed(0), ms: (drawMs / frames).toFixed(1) })
    frames = 0
    drawMs = 0
    statAt = at
    if (source === 'script') {
      const m = Math.floor(virtual / 60)
      const s = Math.floor(virtual % 60)
      $('clock').textContent = `${m}:${String(s).padStart(2, '0')}`
      showSessions(world.summary(now()).sessions)
    } else {
      showSessions(liveSessions)
    }
  }
}

// ---------- Language ----------

// The words that say how things stand are set here, not by data-i18n, so a
// change of language keeps what they say
const showLang = () => {
  document.querySelectorAll('#lang button').forEach(b => b.classList.toggle('on', b.dataset.lang === lang))
  describeTheme()
  describeScenario()
  $('play').textContent = t(playing ? 'pause' : 'resume')
  $('sound').textContent = t(audio ? 'sound.off' : 'sound.on')
  $('ws-state').textContent = t(wsKey)
}
document.querySelectorAll('#lang button').forEach(
  b =>
    (b.onclick = () => {
      setLang(b.dataset.lang)
      showLang()
    }),
)
showLang()

const loop = () => {
  requestAnimationFrame(loop)
  tick(false)
}
requestAnimationFrame(loop)

// For checking the page from a script: run `seconds` of frames at once
window.preview = {
  run(seconds, fps = 25) {
    for (let i = 0; i < Math.round(seconds * fps); i++) {
      skew += 1000 / fps
      tick(true)
    }
    return ui.screen()
  },
  touch: (kind, x, y) => ui.touch(kind, x, y, performance.now() + skew),
  press: () => ui.press(performance.now() + skew),
  summon: () => ui.summon(performance.now() + skew),
}
