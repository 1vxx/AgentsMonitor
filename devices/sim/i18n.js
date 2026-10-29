// The review page's words, in English and Chinese. Elements name theirs with
// data-i18n (text), data-i18n-html (text with markup), data-i18n-title and
// data-i18n-placeholder; scripts use t(). The choice is kept in this browser,
// and starts from the browser's own language.

const STRINGS = {
  en: {
    title: 'AgentsMonitor display preview',
    lead: 'The round screen runs the same C++ display code as the firmware, compiled to WebAssembly: 466 × 466, pixel for pixel.',
    theme: 'Theme',
    by: 'by {author}',
    'side.title': 'Side button (B): once for the session that needs you most, twice for every session',
    'boot.title': 'Bottom button: hold for 3 s to summon what the theme keeps for rare moments',
    loading: 'Loading…',
    hint: 'Use the mouse on the round screen: click to tap, drag to swipe. Swipe sideways for the next session, up for the list and down to go back; the screen follows your finger and springs back if you let go halfway. The side button at the top right (or the B key): once for the session that needs you most, twice for every session. Hold the button at the bottom right for 3 s to summon a rare moment.',
    source: 'Data source',
    'source.script': 'Demo scripts',
    'source.live': 'Live daemon',
    scenario: 'Script',
    pause: 'Pause',
    resume: 'Resume',
    restart: 'Restart',
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
      'Use 127.0.0.1 when the daemon runs on this computer. When it listens on your network, enter <code>http.token</code> from <code>~/.agentsmon/config.toml</code>. The address and token are kept in this browser.',
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
    listen: 'Listen:',
    'cue.0': 'Waiting',
    'cue.1': 'Reminder',
    'cue.2': 'Done',
    'cue.3': 'Error',
    'cue.4': 'Legend',
    'log.cue': 'Chime: {name}',
    'log.muted': ' (sound is off)',
    display: 'Display',
    zoom: 'Zoom',
    'zoom.half': '0.5× (about life size)',
    'zoom.one': '1× (pixel for pixel)',
    'zoom.two': '2× (see the pixels)',
    fps: 'Frame rate',
    'fps.device': '60 (as on the device)',
    sessions: 'Sessions now',
    'status.thinking': 'Thinking',
    'status.tool': 'Running',
    'status.compacting': 'Compacting',
    'status.waiting_input': 'Has a question',
    'status.waiting_permission': 'Needs approval',
    'status.idle': 'Idle',
    'status.stale': 'No signal',
    'status.none': 'No sessions',
    readout: 'Screen {screen} · {fps} fps · drawing {ms} ms (in the browser)',
  },
  zh: {
    title: 'AgentsMonitor 显示设备预览',
    lead: '圆屏运行的是和固件同一份 C++ 显示代码，编译成 WebAssembly：466 × 466，与真机像素一一对应。',
    theme: '主题',
    by: '作者：{author}',
    'side.title': '侧键（B）：按一下回到最需要你的会话，连按两下打开全部会话',
    'boot.title': '下方按键：按住 3 秒，召唤主题为稀有时刻准备的东西',
    loading: '加载中…',
    hint: '在圆屏上用鼠标操作：点击＝轻点，按住拖动＝滑动。左右滑切换会话，上滑打开列表，下滑返回；画面跟着手指走，拖到一半松手会弹回。右上角的侧键（或键盘 B）：按一下回到最需要你的会话，连按两下打开全部会话。右下角的按键按住 3 秒：召唤稀有时刻。',
    source: '数据来源',
    'source.script': '演示剧本',
    'source.live': '实时 daemon',
    scenario: '剧本',
    pause: '暂停',
    resume: '继续',
    restart: '重新开始',
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
      'daemon 在本机时用 127.0.0.1 就行；它监听局域网时，要填 <code>~/.agentsmon/config.toml</code> 里的 <code>http.token</code>。地址和令牌记在这个浏览器里。',
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
    listen: '试听：',
    'cue.0': '等你',
    'cue.1': '轻提醒',
    'cue.2': '完成',
    'cue.3': '出错',
    'cue.4': '传说',
    'log.cue': '提示音：{name}',
    'log.muted': '（声音未启用）',
    display: '显示',
    zoom: '缩放',
    'zoom.half': '0.5×（接近实物大小）',
    'zoom.one': '1×（像素对应）',
    'zoom.two': '2×（看像素细节）',
    fps: '帧率',
    'fps.device': '60（与真机相同）',
    sessions: '当前会话',
    'status.thinking': '思考中',
    'status.tool': '执行',
    'status.compacting': '压缩上下文',
    'status.waiting_input': '等待回答',
    'status.waiting_permission': '等待授权',
    'status.idle': '空闲',
    'status.stale': '无信号',
    'status.none': '没有会话',
    readout: '屏幕 {screen} · {fps} fps · 绘制 {ms} ms（浏览器）',
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
