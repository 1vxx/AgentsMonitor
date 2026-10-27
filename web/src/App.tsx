import { useCallback, useEffect, useState } from 'react'
import { flushSync } from 'react-dom'
import { AnimatePresence, MotionConfig, motion } from 'motion/react'
import { useNow } from './format.ts'
import { TABS, applyTheme, isMac, nextTheme, readTheme, tabFromHash, type Tab, type Theme } from './nav.ts'
import { useLive } from './store.ts'
import { CommandPalette } from './components/CommandPalette.tsx'
import { Masthead } from './components/Masthead.tsx'
import { EASE } from './components/motion.tsx'
import { Overview, QuotaStrip } from './components/Overview.tsx'
import { SessionDrawer } from './components/SessionDrawer.tsx'
import { ActivityView } from './views/ActivityView.tsx'
import { DaemonView } from './views/DaemonView.tsx'
import { HistoryView } from './views/HistoryView.tsx'
import { LiveView } from './views/LiveView.tsx'
import { QuotaView } from './views/QuotaView.tsx'
import { TokensPanel } from './views/TokensPanel.tsx'

const reducedMotion = () => matchMedia('(prefers-reduced-motion: reduce)').matches

export const App = () => {
  const [tab, setTab] = useState<Tab>(tabFromHash)
  const [open, setOpen] = useState<string>()
  const [palette, setPalette] = useState(false)
  const [theme, setTheme] = useState<Theme>(readTheme)
  const now = useNow()
  const waiting = useLive(s => s.summary?.counts.waiting ?? 0)

  useEffect(() => {
    const onHash = () => setTab(tabFromHash())
    window.addEventListener('hashchange', onHash)
    return () => window.removeEventListener('hashchange', onHash)
  }, [])

  useEffect(() => applyTheme(theme), [theme])

  useEffect(() => {
    document.title = waiting > 0 ? `(${waiting}) 等待你 · AgentsMonitor` : 'AgentsMonitor'
  }, [waiting])

  /** Switches the theme; from the toggle, the new one spreads out from it like ink. */
  const changeTheme = useCallback((next: Theme, at?: { x: number; y: number }) => {
    const swap = () => {
      applyTheme(next)
      flushSync(() => setTheme(next))
    }
    if (!at || !document.startViewTransition || reducedMotion()) return swap()
    const transition = document.startViewTransition(swap)
    const r = Math.hypot(Math.max(at.x, innerWidth - at.x), Math.max(at.y, innerHeight - at.y))
    void transition.ready.then(() =>
      document.documentElement.animate(
        { clipPath: [`circle(0px at ${at.x}px ${at.y}px)`, `circle(${r}px at ${at.x}px ${at.y}px)`] },
        { duration: 720, easing: 'cubic-bezier(0.65, 0, 0.35, 1)', pseudoElement: '::view-transition-new(root)' },
      ),
    )
  }, [])

  // ⌘K or / for the palette, 1–5 for the views; never while typing
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
        e.preventDefault()
        setPalette(p => !p)
        return
      }
      if (palette || open || e.metaKey || e.ctrlKey || e.altKey) return
      if ((e.target as HTMLElement | null)?.closest?.('input, textarea, select, [contenteditable]')) return
      const t = TABS[Number(e.key) - 1]
      if (t) location.hash = t.id
      else if (e.key === '/') {
        e.preventDefault()
        setPalette(true)
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [palette, open])

  const onOpen = useCallback((key: string) => setOpen(key), [])
  const onClose = useCallback(() => setOpen(undefined), [])
  const closePalette = useCallback(() => setPalette(false), [])
  const pickTheme = useCallback((t: Theme) => changeTheme(t), [changeTheme])

  return (
    <MotionConfig reducedMotion="user">
      <div className="app">
        <Masthead
          tab={tab}
          waiting={waiting}
          now={now}
          theme={theme}
          onTheme={at => changeTheme(nextTheme(theme), at)}
          onPalette={() => setPalette(true)}
        />

        <main className="content">
          <AnimatePresence mode="wait" initial={false} onExitComplete={() => window.scrollTo({ top: 0 })}>
            <motion.div
              key={tab}
              className="page"
              initial={{ opacity: 0, y: 12 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -6, transition: { duration: 0.16, ease: 'easeIn' } }}
              transition={{ duration: 0.5, ease: EASE }}
            >
              {tab === 'live' && (
                <>
                  <Overview now={now} />
                  <QuotaStrip now={now} />
                  <LiveView now={now} onOpen={onOpen} />
                </>
              )}
              {tab === 'history' && <HistoryView onOpen={onOpen} />}
              {tab === 'activity' && <ActivityView onOpen={onOpen} />}
              {tab === 'usage' && (
                <>
                  <TokensPanel now={now} />
                  <QuotaView now={now} />
                </>
              )}
              {tab === 'daemon' && <DaemonView />}
            </motion.div>
          </AnimatePresence>
        </main>

        <footer className="footer">
          <span>AgentsMonitor · 数据只保存在这台电脑上</span>
          <span className="footer-keys">
            <kbd>{isMac ? '⌘K' : 'Ctrl K'}</kbd> 搜索 <kbd>1</kbd>–<kbd>5</kbd> 切换视图 <kbd>esc</kbd> 关闭
          </span>
        </footer>

        <AnimatePresence>{open && <SessionDrawer key={open} sessionKey={open} onClose={onClose} />}</AnimatePresence>
        <AnimatePresence>
          {palette && <CommandPalette now={now} theme={theme} onClose={closePalette} onOpen={onOpen} onTheme={pickTheme} />}
        </AnimatePresence>
      </div>
    </MotionConfig>
  )
}
