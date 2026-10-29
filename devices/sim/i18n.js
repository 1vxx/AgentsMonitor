// The review page's words, in English and Chinese. Elements name theirs with
// data-i18n (text), data-i18n-html (text with markup), data-i18n-title and
// data-i18n-placeholder; scripts use t(). The choice is kept in this browser,
// and starts from the browser's own language.

const STRINGS = {
  en: {
    title: 'AgentsMonitor display preview',
    'title.short': 'Display preview',
    lead: 'The round screen runs the same C++ display code as the firmware, compiled to WebAssembly: 466 × 466, pixel for pixel.',
    'foot.themes': 'Make a theme of your own',
    theme: 'Theme',
    'theme.pick': 'Choose a theme',
    by: '· by {author}',
    'mode.system': 'Appearance: as the system',
    'mode.light': 'Appearance: light',
    'mode.dark': 'Appearance: dark',
    'side.title': 'Side button (B): once for the session that needs you most, twice for every session',
    'boot.title': 'Bottom button: hold for 3 s to summon what the theme keeps for rare moments',
    'r.screen': 'Screen',
    'r.ms': 'ms a frame',
    'hint.short': 'Drag to swipe · click to tap · B for the side button · ? for every shortcut',
    source: 'Source',
    'source.script': 'Demo scripts',
    'source.live': 'Live daemon',
    scenario: 'Script',
    pause: 'Pause (Space)',
    resume: 'Resume (Space)',
    restart: 'Restart (R)',
    speed: 'Speed',
    'ws.url': 'WebSocket address',
    'ws.token': 'Token',
    'ws.tokenHint': 'Leave empty if the daemon asks for none',
    'ws.connect': 'Connect',
    'ws.idle': 'Not connected',
    'ws.connecting': 'Connecting…',
    'ws.open': 'Connected',
    'ws.closed': 'Disconnected; retrying in 3 s',
    'ws.bad': 'That address looks wrong: it should look like ws://127.0.0.1:7788/v1/ws',
    'ws.note':
      'Use 127.0.0.1 when the daemon runs on this computer. When it listens on your network, enter <code>http.token</code> from <code>~/.agentsmon/config.toml</code>. No agents at hand? <code>python3 scripts/demo.py</code> runs one on made-up sessions, at <code>ws://127.0.0.1:7790/v1/ws</code>.',
    events: 'Make things happen',
    'ev.permission': 'A new session asks for approval',
    'ev.input': 'A session asks you a question',
    'ev.answer': 'Answer every wait',
    'ev.done': 'Finish a turn',
    'ev.error': 'Fail a turn',
    'ev.open': 'Open all sessions on the computer',
    'ev.start': 'Start a working session',
    'ev.stale': 'A session goes quiet',
    'ev.end': 'End a session',
    'ev.link': 'Drop or restore the link',
    sound: 'Sound',
    'sound.on': 'Turn sound on',
    'sound.off': 'Turn sound off',
    volume: 'Volume',
    'cue.0': 'Waiting',
    'cue.1': 'Reminder',
    'cue.2': 'Done',
    'cue.3': 'Error',
    'cue.4': 'Legend',
    'log.cue': 'Chime: {name}',
    'log.muted': ' (sound is off)',
    display: 'Display',
    zoom: 'Zoom',
    'zoom.half': 'About life size',
    'zoom.one': 'Pixel for pixel',
    'zoom.two': 'See the pixels',
    fps: 'Frame rate',
    'fps.device': 'As on the device',
    sessions: 'Sessions now',
    'status.thinking': 'Thinking',
    'status.tool': 'Running',
    'status.compacting': 'Compacting',
    'status.waiting_input': 'Has a question',
    'status.waiting_permission': 'Needs approval',
    'status.idle': 'Idle',
    'status.stale': 'No signal',
    'status.none': 'No sessions',
    keys: 'Shortcuts and gestures',
    'k.play': 'Pause or resume the script',
    'k.restart': 'Restart the script',
    'k.side': 'The side button; twice quickly for every session',
    'k.speed': 'Script speed: 1×, 3×, 10×, 30×',
    'k.mode': 'Light, dark, or as the system',
    'k.lang': 'English or Chinese',
    'k.keys': 'This list',
    'g.click': 'Click',
    'g.tap': 'A tap on the screen',
    'g.drag': 'Drag',
    'g.swipe': 'A swipe: sideways for the next session, up for the list, down to go back',
    'g.boot': 'Bottom button',
    'g.hold': 'Hold 3 s for a rare moment',
    close: 'Close',
  },
  zh: {
    title: 'AgentsMonitor 显示设备预览',
    'title.short': '显示设备预览',
    lead: '圆屏运行的是和固件同一份 C++ 显示代码，编译成 WebAssembly：466 × 466，与真机像素一一对应。',
    'foot.themes': '做一个自己的主题',
    theme: '主题',
    'theme.pick': '选择主题',
    by: '· 作者：{author}',
    'mode.system': '外观：跟随系统',
    'mode.light': '外观：浅色',
    'mode.dark': '外观：深色',
    'side.title': '侧键（B）：按一下回到最需要你的会话，连按两下打开全部会话',
    'boot.title': '下方按键：按住 3 秒，召唤主题为稀有时刻准备的东西',
    'r.screen': '画面',
    'r.ms': '毫秒/帧',
    'hint.short': '拖动＝滑动 · 点击＝轻点 · B＝侧键 · ? 查看全部快捷键',
    source: '数据来源',
    'source.script': '演示剧本',
    'source.live': '实时 daemon',
    scenario: '剧本',
    pause: '暂停（空格）',
    resume: '继续（空格）',
    restart: '重新开始（R）',
    speed: '速度',
    'ws.url': 'WebSocket 地址',
    'ws.token': '令牌',
    'ws.tokenHint': 'daemon 不要求时留空',
    'ws.connect': '连接',
    'ws.idle': '未连接',
    'ws.connecting': '连接中…',
    'ws.open': '已连接',
    'ws.closed': '已断开，3 秒后重连',
    'ws.bad': '地址不对，应形如 ws://127.0.0.1:7788/v1/ws',
    'ws.note':
      'daemon 在本机时用 127.0.0.1 就行；它监听局域网时，要填 <code>~/.agentsmon/config.toml</code> 里的 <code>http.token</code>。手边没有在跑的代理？<code>python3 scripts/demo.py</code> 会在 <code>ws://127.0.0.1:7790/v1/ws</code> 跑一个装满虚构会话的守护进程。',
    events: '手动触发',
    'ev.permission': '新会话请求授权',
    'ev.input': '一个会话向你提问',
    'ev.answer': '处理所有等待',
    'ev.done': '完成一轮',
    'ev.error': '一轮出错',
    'ev.open': '在电脑上打开所有会话',
    'ev.start': '新增工作中会话',
    'ev.stale': '一个会话失联',
    'ev.end': '结束一个会话',
    'ev.link': '断开 / 恢复连接',
    sound: '声音',
    'sound.on': '启用声音',
    'sound.off': '关闭声音',
    volume: '音量',
    'cue.0': '等你',
    'cue.1': '轻提醒',
    'cue.2': '完成',
    'cue.3': '出错',
    'cue.4': '传说',
    'log.cue': '提示音：{name}',
    'log.muted': '（声音未启用）',
    display: '显示',
    zoom: '缩放',
    'zoom.half': '接近实物大小',
    'zoom.one': '像素对应',
    'zoom.two': '看像素细节',
    fps: '帧率',
    'fps.device': '与真机相同',
    sessions: '当前会话',
    'status.thinking': '思考中',
    'status.tool': '执行',
    'status.compacting': '压缩上下文',
    'status.waiting_input': '等待回答',
    'status.waiting_permission': '等待授权',
    'status.idle': '空闲',
    'status.stale': '无信号',
    'status.none': '没有会话',
    keys: '快捷键与手势',
    'k.play': '暂停或继续剧本',
    'k.restart': '重新开始剧本',
    'k.side': '侧键；快速按两下看全部会话',
    'k.speed': '剧本速度：1×、3×、10×、30×',
    'k.mode': '浅色、深色或跟随系统',
    'k.lang': '中文或英文',
    'k.keys': '这份列表',
    'g.click': '点击',
    'g.tap': '在屏幕上轻点',
    'g.drag': '拖动',
    'g.swipe': '滑动：左右切换会话，上滑打开列表，下滑返回',
    'g.boot': '下方按键',
    'g.hold': '按住 3 秒，召唤稀有时刻',
    close: '关闭',
  },
}

export const LANGS = Object.keys(STRINGS)

const stored = () => {
  try {
    return localStorage.getItem('am.lang')
  } catch {
    return null
  }
}

export let lang = LANGS.includes(stored()) ? stored() : navigator.language?.toLowerCase().startsWith('zh') ? 'zh' : 'en'

// A string in the current language, with {name} filled in from vars
export const t = (key, vars = {}) => (STRINGS[lang][key] ?? STRINGS.en[key] ?? key).replace(/\{(\w+)\}/g, (_, k) => vars[k] ?? '')

// One of a {en, zh} pair, as scenarios and theme.json give them
export const pick = texts => texts?.[lang] ?? texts?.en ?? ''

// Puts the current language's words into the page
export const apply = (root = document) => {
  document.documentElement.lang = lang === 'zh' ? 'zh-CN' : 'en'
  document.title = t('title')
  root.querySelectorAll('[data-i18n]').forEach(el => (el.textContent = t(el.dataset.i18n)))
  root.querySelectorAll('[data-i18n-html]').forEach(el => (el.innerHTML = t(el.dataset.i18nHtml)))
  root.querySelectorAll('[data-i18n-title]').forEach(el => (el.title = t(el.dataset.i18nTitle)))
  root.querySelectorAll('[data-i18n-placeholder]').forEach(el => (el.placeholder = t(el.dataset.i18nPlaceholder)))
}

export const setLang = next => {
  lang = next
  try {
    localStorage.setItem('am.lang', next)
  } catch {}
  apply()
}
