// The review page: runs a theme (build/<theme>.js, see build.sh) on a canvas,
// turns the mouse into touches, plays its chimes, and feeds it summaries from
// a scripted world or from the daemon itself. page.js keeps the page around it.

import { apply, lang, pick, setLang, t } from './i18n.js'
import { $, cycleMode, drawDial, keysOpen, onKey, save, saved, segmented, showKeys, showMode, themeMenu, toast } from './page.js'
import { SCENARIOS } from './scenarios.js'
import { World } from './world.js'

const SIZE = 466
const LINK = { connecting: 0, online: 1, lost: 2 }

apply()
showMode()
drawDial()

// ---------- The theme ----------

// Every build holds one theme, so another one is another module: the page
// loads the one asked for (?theme=, else the last one shown), and switching
// reloads the page with it
const THEMES = await fetch('build/themes.json').then(r => r.json())
const asked = new URLSearchParams(location.search).get('theme') || saved('am.theme')
const theme = THEMES.find(x => x.id === asked) ?? THEMES[0]
save('am.theme', theme.id)
const menu = themeMenu(THEMES, theme, pick, next => {
  const url = new URL(location.href)
  url.searchParams.set('theme', next.id)
  location.href = url.href
})
const describeTheme = () => {
  menu.describe()
  $('theme-title').textContent = pick(theme.name)
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

// The stage glows in the display's mood
const showMood = mood => {
  if (document.documentElement.dataset.mood !== mood) document.documentElement.dataset.mood = mood
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

// Each chime's name and tone, as the cue buttons show them
const CUE_TONES = ['attn', 'attn', 'good', 'bad', 'legend']
const cueName = c => t(`cue.${c}`)

const log = text => {
  const li = document.createElement('li')
  const time = document.createElement('time')
  time.textContent = new Date().toLocaleTimeString([], { hour12: false })
  li.append(time, text)
  $('log').prepend(li)
  while ($('log').children.length > 30) $('log').lastChild.remove()
}

const showSound = () => {
  $('sound').setAttribute('aria-checked', String(!!audio))
  $('sound').title = t(audio ? 'sound.off' : 'sound.on')
}
$('sound').onclick = () => {
  if (audio) {
    audio.close()
    audio = null
  } else {
    audio = new AudioContext()
    gain = audio.createGain()
    gain.gain.value = +$('volume').value
    gain.connect(audio.destination)
  }
  showSound()
}
$('volume').oninput = () => gain && (gain.gain.value = +$('volume').value)
document.querySelectorAll('[data-cue]').forEach(b => (b.onclick = () => play(+b.dataset.cue)))

// ---------- Sources ----------

let source = 'script'
let scenario = SCENARIOS.find(s => s.id === saved('am.scenario')) ?? SCENARIOS[0]
let world = new World()
let step = 0
let virtual = 0 // seconds into the script
let playing = true
let speed = 1
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
  if (playing) virtual += dt * speed
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
  if (world.link !== 'online') return showMood('offline')
  const sum = world.summary(now())
  showMood(sum.mood)
  const json = JSON.stringify({ ...sum, ts: 0 })
  if (json !== lastSummary) {
    ui.summary(JSON.stringify(sum))
    lastSummary = json
  }
}

// The script's progress, with a mark at each moment it changes something
const clock = s => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}`
const drawMarks = () => {
  const at = [...new Set(scenario.script.map(([s]) => s))].filter(s => s > 0 && s < scenario.length)
  $('tl-marks').replaceChildren(
    ...at.map(s => {
      const i = document.createElement('i')
      i.style.left = `${(s / scenario.length) * 100}%`
      i.dataset.at = s
      return i
    }),
  )
}
const showProgress = () => {
  const done = Math.min(1, virtual / scenario.length)
  $('tl-fill').style.width = `${done * 100}%`
  for (const i of $('tl-marks').children) i.classList.toggle('past', +i.dataset.at <= virtual)
  $('clock').innerHTML = `<b>${clock(Math.min(virtual, scenario.length))}</b> / ${clock(scenario.length)}`
}

for (const s of SCENARIOS) $('scenario').append(new Option(pick(s.name), s.id, false, s === scenario))
const describeScenario = () => {
  for (const o of $('scenario').options) o.textContent = pick(SCENARIOS.find(s => s.id === o.value).name)
  $('scenario-note').textContent = pick(scenario.note)
}
const pickScenario = id => {
  scenario = SCENARIOS.find(s => s.id === id)
  save('am.scenario', id)
  describeScenario()
  drawMarks()
  resetScript()
}
$('scenario').onchange = e => pickScenario(e.target.value)
pickScenario(scenario.id)

const showPlaying = () => {
  $('play').dataset.state = playing ? 'playing' : 'paused'
  $('play').title = t(playing ? 'pause' : 'resume')
  $('play').setAttribute('aria-label', t(playing ? 'pause' : 'resume'))
}
const togglePlay = () => {
  playing = !playing
  showPlaying()
}
$('play').onclick = togglePlay
$('restart').onclick = () => resetScript()
$('restart').title = t('restart')

const setSpeed = segmented('speed', saved('am.speed') ?? '1', v => {
  speed = +v
  save('am.speed', v)
})

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

// What the link is doing, kept as a key so it can be said again in another
// language, with the tone of its dot
const WS_TONES = { 'ws.idle': 'rest', 'ws.connecting': 'attn', 'ws.open': 'good', 'ws.closed': 'bad', 'ws.bad': 'bad' }
let wsKey = 'ws.idle'
const wsSay = key => {
  wsKey = key
  $('ws-state').className = `status t-${WS_TONES[key]}`
  $('ws-state').lastElementChild.textContent = t(key)
}

const connectLive = () => {
  clearTimeout(retry)
  ws?.close()
  ui.link('connecting')
  showMood('offline')
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
      showMood(m.summary.mood)
    }
  }
  sock.onclose = () => {
    if (ws !== sock) return
    ui.link('lost')
    showMood('offline')
    wsSay('ws.closed')
    if (source === 'live') retry = setTimeout(connectLive, 3000)
  }
}
let liveSessions = []

segmented('source', 'script', v => {
  if (v === source) return
  source = v
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
    wsSay('ws.idle')
    resetScript()
  }
})
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
document.querySelectorAll('[data-ev]').forEach(
  b =>
    (b.onclick = () => {
      EVENTS[b.dataset.ev]()
      b.classList.remove('hit')
      void b.offsetWidth // so the ring plays again on a quick second click
      b.classList.add('hit')
    }),
)

// ---------- Touch and buttons ----------

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

// The other button: held 3 s, it summons, once per hold
let holding
$('boot').onpointerdown = e => {
  $('boot').setPointerCapture(e.pointerId)
  holding = setTimeout(() => ui.summon(performance.now() + skew), 3000)
}
$('boot').onpointerup = $('boot').onpointercancel = () => clearTimeout(holding)

// ---------- Display ----------

let fps = 60
segmented('zoom', saved('am.zoom') ?? '1', v => {
  save('am.zoom', v)
  document.documentElement.style.setProperty('--zoom', v)
  // Past life size the stage takes the whole width
  document.body.classList.toggle('big', +v > 1)
  canvas.classList.toggle('pixelated', +v > 1)
})
segmented('fps', saved('am.fps') ?? '60', v => {
  fps = +v
  save('am.fps', v)
})

const SESSION_TONES = { waiting_permission: 'attn', waiting_input: 'attn', thinking: 'work', tool: 'work', compacting: 'work' }
const toneOf = s => {
  if (s.stale) return 'rest'
  if (SESSION_TONES[s.status]) return SESSION_TONES[s.status]
  const end = s.last_turn?.outcome
  return end === 'answer' ? 'good' : end === 'error' || end === 'refusal' ? 'bad' : 'rest'
}
const showSessions = list => {
  $('session-count').textContent = list.length ? String(list.length) : ''
  if (!list.length) {
    const li = document.createElement('li')
    li.className = 'empty'
    li.textContent = t('status.none')
    return $('sessions').replaceChildren(li)
  }
  $('sessions').replaceChildren(
    ...list.map(s => {
      const li = document.createElement('li')
      li.className = `t-${toneOf(s)}${s.stale ? ' stale' : ''}`
      const name = document.createElement('span')
      name.className = 'name'
      name.textContent = s.title || s.project
      const proj = document.createElement('small')
      proj.textContent = `${s.project} · ${s.agent === 'codex' ? 'codex' : 'claude'}`
      name.append(proj)
      const state = document.createElement('span')
      state.className = 'state'
      const what = s.stale ? t('status.stale') : `${t(`status.${s.status}`)}${s.status === 'tool' && s.tool ? ` ${s.tool}` : ''}`
      const end = s.status === 'idle' && s.last_turn ? ` · ${s.last_turn.outcome}` : ''
      state.textContent = what + end
      li.append(document.createElement('i'), name, state)
      return li
    }),
  )
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
  if (!forced && at - lastFrame < 1000 / fps - 2) return
  lastFrame = at
  const dt = Math.min(1, (at - lastTick) / 1000)
  lastTick = at

  if (source === 'script') {
    advanceScript(dt)
    showProgress()
  }
  const wall = source === 'script' ? now() : Date.now()
  const ptr = ui.frame(at, wall)
  ctx.putImageData(new ImageData(new Uint8ClampedArray(mod.HEAPU8.buffer, ptr, SIZE * SIZE * 4).slice(), SIZE, SIZE), 0, 0)
  drawMs += mod._am_draw_ms()
  frames++

  for (let c; (c = ui.cue()) >= 0; ) {
    log(t('log.cue', { name: cueName(c) }) + (audio ? '' : t('log.muted')))
    toast(cueName(c), CUE_TONES[c])
    play(c)
  }

  if (at - statAt > 500) {
    $('r-screen').textContent = ui.screen()
    $('r-fps').textContent = ((frames * 1000) / (at - statAt)).toFixed(0)
    $('r-ms').textContent = (drawMs / frames).toFixed(1)
    frames = 0
    drawMs = 0
    statAt = at
    showSessions(source === 'script' ? world.summary(now()).sessions : liveSessions)
  }
}

// ---------- Language and keys ----------

// The words that say how things stand are set here, not by data-i18n, so a
// change of language keeps what they say
const showLang = () => {
  describeTheme()
  describeScenario()
  showPlaying()
  showSound()
  showMode()
  wsSay(wsKey)
  $('restart').title = t('restart')
}
segmented('lang', lang, v => {
  if (v !== lang) setLang(v)
  showLang()
})

const SPEEDS = ['1', '3', '10', '30']
onKey(e => {
  if (menu.key(e)) return e.preventDefault()
  if (keysOpen()) {
    if (e.key === 'Escape' || e.key === '?') showKeys(false)
    return
  }
  const k = e.key.toLowerCase()
  if (k === ' ' && source === 'script') togglePlay()
  else if (k === 'r' && source === 'script') resetScript()
  else if (k === 'b' && !e.repeat) side()
  else if (k === 't') cycleMode()
  else if (k === 'l') $('lang').querySelector(`[data-v="${lang === 'en' ? 'zh' : 'en'}"]`).click()
  else if (k === '?') showKeys(true)
  else if (SPEEDS[+k - 1] && source === 'script') setSpeed(SPEEDS[+k - 1])
  else return
  e.preventDefault()
})

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
