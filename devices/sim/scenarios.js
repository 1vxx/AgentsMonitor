// Scripts for the review page: each is a list of [seconds, step(world, now)]
// played against virtual time, so they can be sped up.

const AM = { project: 'AgentsMonitor' }
const t = (title, extra = {}) => ({ ...AM, title, ...extra })

export const SCENARIOS = [
  {
    id: 'day',
    name: '日常节奏',
    note: '四个会话交替工作：中途有人等你授权、有人提问、有一轮出错。约 2 分钟一轮，循环播放。',
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
    name: '等你授权，一直不处理',
    note: '3 秒时有会话请求授权，之后没人处理。把速度调到 10× 或 30×，可以听到 2 分钟、5 分钟、之后每 10 分钟的轻提醒；轻点屏幕后不再提醒。',
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
    id: 'many-waits',
    name: '三个会话同时等你',
    note: '2、4、6 秒各有一个会话开始等你：提醒卡会每 4 秒轮换一个，底部圆点表示第几个。20 秒时其中一个被处理。',
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
    name: '满载 8 个会话',
    note: '8 个会话、长标题：看标题滚动、列表滚动和外圈卫星的拥挤程度。第一个会话用中文标题，展示中文像素字体。',
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
    name: '完成与出错',
    note: '依次：完成一轮（轻响，光球变绿并荡出涟漪和火花）、出错（低沉下行音，光球变红并摇头抖动）、被拒、中断（不响）。听一下每种提示音，看光球和卫星的变化。',
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
    name: '完成了但还没看',
    note: '3 秒时一个会话做完、8 秒时一个出错，你都没在看：它们一直醒着等你，单只视图优先显示，全部视图顶部写 2 DONE。30 秒时你在电脑上打开第一个，它就回去打盹。调到 30× 可以听到 5 分钟后的再次提醒。也可以随时点“在电脑上打开所有会话”。',
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
    name: '传说：五个一起干活',
    note: '四个会话在工作，3 秒时第五个开始：隐藏的飞龙登场，变成最新那个会话的宠物。上滑看全部，它的名字是金色的。',
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
    name: '传说：十连胜',
    note: '一个会话每 2 秒顺利完成一轮，连续 10 轮（约 20 秒）后飞龙登场。',
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
    name: '没有会话',
    note: '连接正常但没有任何会话：灰色光球缓慢呼吸。',
    length: 30,
    script: [],
  },
  {
    id: 'link',
    name: '断线与重连',
    note: '3 秒时连接断开，8 秒时重连中，11 秒时恢复。',
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
