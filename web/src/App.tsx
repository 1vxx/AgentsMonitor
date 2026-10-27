import { useCallback, useEffect, useState } from 'react'
import { duration, useNow } from './format.ts'
import { useLive } from './store.ts'
import { Overview, QuotaStrip } from './components/Overview.tsx'
import { SessionDrawer } from './components/SessionDrawer.tsx'
import { ActivityView } from './views/ActivityView.tsx'
import { DaemonView } from './views/DaemonView.tsx'
import { HistoryView } from './views/HistoryView.tsx'
import { LiveView } from './views/LiveView.tsx'
import { QuotaView } from './views/QuotaView.tsx'
import { TokensPanel } from './views/TokensPanel.tsx'

const TABS = [
  { id: 'live', label: '实时' },
  { id: 'history', label: '历史' },
  { id: 'activity', label: '事件' },
  { id: 'usage', label: '用量' },
  { id: 'daemon', label: '守护进程' },
] as const
type Tab = (typeof TABS)[number]['id']

const tabFromHash = (): Tab => {
  const raw = location.hash.replace(/^#\/?/, '').split('?')[0]
  // The tab was called quota before it showed tokens too.
  const id = raw === 'quota' ? 'usage' : raw
  return TABS.some(t => t.id === id) ? (id as Tab) : 'live'
}

type Theme = 'system' | 'dark' | 'light'
const THEME_KEY = 'agentsmon.theme'
const readTheme = (): Theme => {
  try {
    const v = localStorage.getItem(THEME_KEY)
    return v === 'dark' || v === 'light' ? v : 'system'
  } catch {
    return 'system'
  }
}

export const App = () => {
  const [tab, setTab] = useState<Tab>(tabFromHash)
  const [open, setOpen] = useState<string>()
  const [theme, setTheme] = useState<Theme>(readTheme)
  const now = useNow()
  const conn = useLive(s => s.conn)
  const server = useLive(s => s.server)
  const waiting = useLive(s => s.summary?.counts.waiting ?? 0)

  useEffect(() => {
    const onHash = () => setTab(tabFromHash())
    window.addEventListener('hashchange', onHash)
    return () => window.removeEventListener('hashchange', onHash)
  }, [])

  useEffect(() => {
    const root = document.documentElement
    if (theme === 'system') delete root.dataset.theme
    else root.dataset.theme = theme
    try {
      localStorage.setItem(THEME_KEY, theme)
    } catch {
      // Private windows may refuse storage; the choice lasts this visit
    }
  }, [theme])

  useEffect(() => {
    document.title = waiting > 0 ? `(${waiting}) 等待你 · AgentsMonitor` : 'AgentsMonitor'
  }, [waiting])

  const onOpen = useCallback((key: string) => setOpen(key), [])
  const onClose = useCallback(() => setOpen(undefined), [])

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <span className="brand-mark" aria-hidden />
          AgentsMonitor
        </div>
        <nav className="tabs" aria-label="视图">
          {TABS.map(t => (
            <a key={t.id} href={`#${t.id}`} className={tab === t.id ? 'on' : ''} aria-current={tab === t.id ? 'page' : undefined}>
              {t.label}
              {t.id === 'live' && waiting > 0 && <span className="badge">{waiting}</span>}
            </a>
          ))}
        </nav>
        <div className="topbar-right">
          <span className={`conn conn-${conn}`} title={conn === 'open' ? '实时连接中' : '正在重连守护进程'}>
            <span className="conn-dot" />
            {conn === 'open' ? (server ? `v${server.version} · 运行 ${duration(now - server.started_at).split(' ')[0]}` : '已连接') : conn === 'connecting' ? '连接中' : '已断开'}
          </span>
          <button
            className="icon-btn"
            onClick={() => setTheme(t => (t === 'system' ? 'dark' : t === 'dark' ? 'light' : 'system'))}
            title={`主题：${theme === 'system' ? '跟随系统' : theme === 'dark' ? '深色' : '浅色'}`}
            aria-label="切换主题"
          >
            {theme === 'system' ? '◐' : theme === 'dark' ? '●' : '○'}
          </button>
        </div>
      </header>

      <main className="content">
        {tab === 'live' && <Overview now={now} />}
        {tab === 'live' && <QuotaStrip now={now} />}
        {tab === 'live' && <LiveView now={now} onOpen={onOpen} />}
        {tab === 'history' && <HistoryView onOpen={onOpen} />}
        {tab === 'activity' && <ActivityView onOpen={onOpen} />}
        {tab === 'usage' && <TokensPanel now={now} />}
        {tab === 'usage' && <QuotaView now={now} />}
        {tab === 'daemon' && <DaemonView />}
      </main>

      {open && <SessionDrawer sessionKey={open} onClose={onClose} />}
    </div>
  )
}
