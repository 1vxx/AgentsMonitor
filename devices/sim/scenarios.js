// Scripts for the review page and snap.mjs: each is a list of
// [seconds, step(world, now)] played against virtual time, so they can be
// sped up. Names and notes come in each of the page's languages.

const AM = { project: 'AgentsMonitor' }
const t = (title, extra = {}) => ({ ...AM, title, ...extra })

export const SCENARIOS = [
  {
    id: 'day',
    name: { en: 'A day\'s rhythm', zh: '日常节奏' },
    note: {
      en: 'Four sessions take turns working; along the way one waits for approval, one asks a question and one turn fails. About two minutes, on a loop.',
      zh: '四个会话交替工作：中途有人等你授权、有人提问、有一轮出错。约 2 分钟一轮，循环播放。',
    },
    length: 120,
    loop: true,
    script: [
      [0, (w, n) => {
        w.start(n, 'claude-code:a', t('Daemon DB growth and event retention'))
        w.start(n, 'claude-code:b', t('Theme switch polish'))
        w.tool(n, 'claude-code:b', 'Edit')
        w.finished(n, 'claude-code:c', t('Live session ordering'), 'answer', 120_000)
        w.finished(n, 'codex:d', { project: 'agentsmon-device', title: 'Device UI prototype', agent: 'codex' }, 'aborted', 600_000)
      }],
      [3, (w, n) => w.tool(n, 'claude-code:a', 'Read')],
      [6, (w, n) => w.tool(n, 'claude-code:a', 'Bash')],
      [9, (w, n) => w.think(n, 'claude-code:b')],
      [12, (w, n) => w.think(n, 'claude-code:a')],
      [15, (w, n) => w.prompt(n, 'claude-code:c')],
      [18, (w, n) => w.tool(n, 'claude-code:c', 'Grep')],
      [22, (w, n) => w.ask(n, 'claude-code:b', { tool: 'Bash', subject: 'pnpm --dir web build' })],
      [40, (w, n) => w.answer(n, 'claude-code:b')],
      [44, (w, n) => w.finish(n, 'claude-code:b')],
      [50, (w, n) => w.tool(n, 'claude-code:a', 'Edit')],
      [58, (w, n) => w.finish(n, 'claude-code:a', 'error')],
      [64, (w, n) => w.ask(n, 'claude-code:c', { kind: 'input', tool: 'AskUserQuestion', message: 'Sort live sessions by start time, or by last activity?' })],
      [80, (w, n) => w.answer(n, 'claude-code:c')],
      [84, (w, n) => w.tool(n, 'claude-code:c', 'Edit')],
      [92, (w, n) => w.finish(n, 'claude-code:c')],
      [96, (w, n) => w.prompt(n, 'claude-code:a')],
      [100, (w, n) => w.tool(n, 'claude-code:a', 'mcp__github__create_pull_request')],
      [112, (w, n) => w.finish(n, 'claude-code:a')],
    ],
  },
  {
    id: 'wait',
    name: { en: 'An approval nobody answers', zh: '等你授权，一直不处理' },
    note: {
      en: 'At 3 s a session asks for approval, and nobody answers. Speed up to 10× or 30× to hear the gentle reminders at 2 and 5 minutes, then every 10; a tap on the screen stops them.',
      zh: '3 秒时有会话请求授权，之后没人处理。把速度调到 10× 或 30×，可以听到 2 分钟、5 分钟、之后每 10 分钟的轻提醒；轻点屏幕后不再提醒。',
    },
    length: 3600,
    script: [
      [0, (w, n) => {
        w.start(n, 'claude-code:a', t('Investigate UI stutter'))
        w.tool(n, 'claude-code:a', 'Read')
        w.finished(n, 'claude-code:b', t('Translate README to Chinese'), 'answer', 300_000)
      }],
      [3, (w, n) => w.ask(n, 'claude-code:a', { tool: 'Bash', subject: 'rm -rf target && cargo build --release' })],
    ],
  },
  {
    id: 'long-day',
    name: { en: 'A long day, answered late', zh: '漫长的一天，很晚才回' },
    note: {
      en: 'A session reads, thinks, compacts and edits, then at 24 s asks for approval and waits 26 minutes before it is answered; it finishes, nobody looks for 6½ minutes, it is opened, and then all goes quiet until bored and, 10 minutes after it finished, asleep. Play it at 30× (80 s).',
      zh: '一个会话读文件、思考、压缩、改代码，24 秒时请求授权，26 分钟后才被处理；接着做完，6 分半没人看，然后被打开，之后一直没动静，直到无聊、做完 10 分钟后睡着。用 30× 播放（80 秒）。',
    },
    length: 2400,
    script: [
      [0, (w, n) => {
        w.start(n, 'claude-code:a', t('Refactor the sync engine'))
        w.tool(n, 'claude-code:a', 'Read')
        w.finished(n, 'claude-code:b', t('Docs pass'), 'answer', 2 * 3_600_000)
      }],
      [4, (w, n) => w.think(n, 'claude-code:a')],
      [8, (w, n) => w.touch(n, 'claude-code:a', { status: 'compacting', since: n })],
      [16, (w, n) => w.tool(n, 'claude-code:a', 'Edit')],
      [24, (w, n) => w.ask(n, 'claude-code:a', { tool: 'Bash', subject: 'git push --force-with-lease' })],
      [1584, (w, n) => w.answer(n, 'claude-code:a')],
      [1620, (w, n) => w.finish(n, 'claude-code:a')],
      [2010, (w, n) => w.open(n, 'claude-code:a')],
    ],
  },
  {
    id: 'many-waits',
    name: { en: 'Three waiting at once', zh: '三个会话同时等你' },
    note: {
      en: 'At 2, 4 and 6 s a session starts waiting for you: the alert takes each in turn every 4 s. At 20 s one of them is answered.',
      zh: '2、4、6 秒各有一个会话开始等你：提醒每 4 秒轮换一个。20 秒时其中一个被处理。',
    },
    length: 60,
    script: [
      [0, (w, n) => {
        w.start(n, 'claude-code:a', t('Count quotas from logs'))
        w.start(n, 'claude-code:b', t('Codex plugin icon', { project: 'agentsmon-plugins' }))
        w.start(n, 'codex:c', { project: 'agentsmon-device', title: 'Firmware Wi-Fi setup', agent: 'codex' })
      }],
      [2, (w, n) => w.ask(n, 'claude-code:a', { tool: 'Bash', subject: 'sqlite3 ~/.agentsmon/agentsmon.db "VACUUM"' })],
      [4, (w, n) => w.ask(n, 'claude-code:b', { tool: 'Write', subject: 'plugins/codex/assets/icon.svg' })],
      [6, (w, n) => w.ask(n, 'codex:c', { kind: 'input', message: 'SoftAP or BLE for Wi-Fi setup?' })],
      [20, (w, n) => w.answer(n, 'claude-code:b')],
    ],
  },
  {
    id: 'full',
    name: { en: 'Eight sessions, all busy', zh: '满载 8 个会话' },
    note: {
      en: 'Eight sessions with long titles: watch titles scroll, lists fill and the screen get crowded. The first has a Chinese title, in the hanzi pixel font.',
      zh: '8 个会话、长标题：看标题滚动、列表填满和屏幕变拥挤。第一个会话用中文标题，展示中文像素字体。',
    },
    length: 60,
    script: [
      [0, (w, n) => {
        w.start(n, 'claude-code:1', t('守护线程数据库增长和事件保留'))
        w.tool(n, 'claude-code:1', 'Bash')
        w.start(n, 'claude-code:2', t('Theme switch polish'))
        w.start(n, 'claude-code:3', t('Live session ordering'))
        w.tool(n, 'claude-code:3', 'Grep')
        w.start(n, 'codex:4', { project: 'agentsmon-device', title: 'Device UI prototype and WASM build', agent: 'codex' })
        w.finished(n, 'claude-code:5', t('Translate README to Chinese'), 'answer', 30_000)
        w.finished(n, 'claude-code:6', t('Codex plugin icon'), 'error', 200_000)
        w.finished(n, 'claude-code:7', t('Count quotas from logs'), 'answer', 3_600_000)
        w.start(n, 'claude-code:8', { project: 'scratch' })
        w.setStale(n, 'claude-code:8', true)
      }],
      [5, (w, n) => w.ask(n, 'claude-code:2', { tool: 'Edit', subject: 'web/src/styles/tokens.css' })],
    ],
  },
  {
    id: 'endings',
    name: { en: 'Finished and failed', zh: '完成与出错' },
    note: {
      en: 'In turn: a turn finishes (a soft chime), one fails (a low falling tone), one is refused and one is interrupted (no sound). Listen to each chime and watch how the theme reacts.',
      zh: '依次：完成一轮（轻响）、出错（低沉下行音）、被拒、中断（不响）。听一下每种提示音，看主题如何反应。',
    },
    length: 30,
    script: [
      [0, (w, n) => {
        w.start(n, 'claude-code:a', t('Investigate UI stutter'))
        w.start(n, 'claude-code:b', t('Theme switch polish'))
        w.start(n, 'claude-code:c', t('Live session ordering'))
        w.start(n, 'claude-code:d', t('Translate README to Chinese'))
      }],
      [3, (w, n) => w.finish(n, 'claude-code:a', 'answer')],
      [7, (w, n) => w.finish(n, 'claude-code:b', 'error')],
      [11, (w, n) => w.finish(n, 'claude-code:c', 'refusal')],
      [15, (w, n) => w.finish(n, 'claude-code:d', 'aborted')],
    ],
  },
  {
    id: 'unseen',
    name: { en: 'Finished, not yet seen', zh: '完成了但还没看' },
    note: {
      en: 'At 3 s a session finishes and at 8 s one fails while you are away: both stay up, waiting for you. At 30 s you open the first on the computer and it settles. At 30× you can hear the second reminder after 5 minutes. "Open all sessions on the computer" does the same at any time.',
      zh: '3 秒时一个会话做完、8 秒时一个出错，你都没在看：它们一直醒着等你。30 秒时你在电脑上打开第一个，它就安静下来。调到 30× 可以听到 5 分钟后的再次提醒。也可以随时点“在电脑上打开所有会话”。',
    },
    length: 600,
    script: [
      [0, (w, n) => {
        w.start(n, 'claude-code:a', t('Investigate UI stutter'))
        w.start(n, 'claude-code:b', t('Theme switch polish'))
        w.tool(n, 'claude-code:b', 'Edit')
        w.start(n, 'codex:c', { project: 'agentsmon-device', title: 'Firmware Wi-Fi setup', agent: 'codex' })
        w.tool(n, 'codex:c', 'Bash')
      }],
      [3, (w, n) => w.finish(n, 'claude-code:a', 'answer')],
      [8, (w, n) => w.finish(n, 'claude-code:b', 'error')],
      [30, (w, n) => w.open(n, 'claude-code:a')],
    ],
  },
  {
    id: 'legend-crowd',
    name: { en: 'Legend: five at work', zh: '传说：五个一起干活' },
    note: {
      en: 'Four sessions are working, and at 3 s a fifth starts: a rare moment. In pets, the hidden dragon arrives as the newest session\'s pet; swipe up to see its golden name.',
      zh: '四个会话在工作，3 秒时第五个开始：一个稀有时刻。在 pets 里，隐藏的飞龙登场，变成最新那个会话的宠物；上滑看全部，它的名字是金色的。',
    },
    length: 60,
    script: [
      [0, (w, n) => {
        w.start(n, 'claude-code:a', t('Investigate UI stutter'))
        w.start(n, 'claude-code:b', t('Theme switch polish'))
        w.tool(n, 'claude-code:b', 'Edit')
        w.start(n, 'claude-code:c', t('Live session ordering'))
        w.start(n, 'codex:d', { project: 'agentsmon-device', title: 'Firmware Wi-Fi setup', agent: 'codex' })
      }],
      [3, (w, n) => w.start(n, 'claude-code:e', t('Daemon DB growth and event retention'))],
      [12, (w, n) => w.tool(n, 'claude-code:e', 'Bash')],
      [20, (w, n) => w.finish(n, 'claude-code:e')],
      [40, (w, n) => w.end('claude-code:e')],
    ],
  },
  {
    id: 'legend-streak',
    name: { en: 'Legend: ten in a row', zh: '传说：十连胜' },
    note: {
      en: 'One session finishes a turn well every 2 s; after 10 in a row (about 20 s) comes a rare moment. In pets, the dragon arrives.',
      zh: '一个会话每 2 秒顺利完成一轮，连续 10 轮（约 20 秒）后迎来稀有时刻。在 pets 里，飞龙登场。',
    },
    length: 40,
    script: [
      [0, (w, n) => {
        w.start(n, 'claude-code:a', t('Count quotas from logs'))
        w.finished(n, 'claude-code:b', t('Translate README to Chinese'), 'answer', 300_000)
      }],
      ...Array.from({ length: 10 }, (_, i) => [
        [1 + 2 * i, (w, n) => w.prompt(n, 'claude-code:a')],
        [2 + 2 * i, (w, n) => w.finish(n, 'claude-code:a')],
      ]).flat(),
    ],
  },
  {
    id: 'empty',
    name: { en: 'No sessions', zh: '没有会话' },
    note: {
      en: 'Connected, but there are no sessions at all.',
      zh: '连接正常，但没有任何会话。',
    },
    length: 30,
    script: [],
  },
  {
    id: 'link',
    name: { en: 'Link lost and back', zh: '断线与重连' },
    note: {
      en: 'At 3 s the link drops, at 8 s it is reconnecting, and at 11 s it is back.',
      zh: '3 秒时连接断开，8 秒时重连中，11 秒时恢复。',
    },
    length: 30,
    script: [
      [0, (w, n) => {
        w.start(n, 'claude-code:a', t('Investigate UI stutter'))
        w.tool(n, 'claude-code:a', 'Bash')
      }],
      [3, w => (w.link = 'lost')],
      [8, w => (w.link = 'connecting')],
      [11, w => (w.link = 'online')],
    ],
  },
]
