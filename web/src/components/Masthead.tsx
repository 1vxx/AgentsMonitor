import { AnimatePresence, motion } from 'motion/react'
import { duration } from '../format.ts'
import { LOCALES, nextLocale, setLocale, useLocale, useT } from '../i18n/index.ts'
import { TABS, isMac, type Tab, type Theme } from '../nav.ts'
import { useLive } from '../store.ts'
import { SearchIcon } from './bits.tsx'
import { Logo } from './Logo.tsx'
import { EASE } from './motion.tsx'

const ThemeIcon = ({ theme }: { theme: Theme }) => (
  <svg viewBox="0 0 18 18" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" aria-hidden>
    {theme === 'system' && (
      <>
        <circle cx="9" cy="9" r="6.2" />
        <path d="M9 2.8a6.2 6.2 0 0 1 0 12.4z" fill="currentColor" />
      </>
    )}
    {theme === 'dark' && <path d="M14.6 11.2A6.3 6.3 0 0 1 6.8 3.4a6.3 6.3 0 1 0 7.8 7.8z" />}
    {theme === 'light' && (
      <>
        <circle cx="9" cy="9" r="3.3" />
        <path d="M9 1.6v1.6M9 14.8v1.6M1.6 9h1.6M14.8 9h1.6M3.8 3.8l1.1 1.1M13.1 13.1l1.1 1.1M3.8 14.2l1.1-1.1M13.1 4.9l1.1-1.1" />
      </>
    )}
  </svg>
)

export const Masthead = ({
  tab,
  waiting,
  now,
  theme,
  onTheme,
  onPalette,
}: {
  tab: Tab
  waiting: number
  now: number
  theme: Theme
  onTheme: (at: { x: number; y: number }) => void
  onPalette: () => void
}) => {
  const conn = useLive(s => s.conn)
  const server = useLive(s => s.server)
  const mood = useLive(s => s.summary?.mood)
  const t = useT()
  const locale = useLocale()
  const lang = LOCALES.find(l => l.id === locale)!

  return (
    <header className="masthead">
      <div className="masthead-inner">
        <a className="brand" href="#live" aria-label="AgentsMonitor">
          <motion.span
            className="brand-logo"
            initial={{ scale: 0.6, opacity: 0, rotate: -12 }}
            animate={{ scale: 1, opacity: 1, rotate: 0 }}
            transition={{ type: 'spring', stiffness: 420, damping: 22, delay: 0.1 }}
          >
            <Logo live={mood === 'working' || mood === 'attention'} />
          </motion.span>
          <span className="brand-name">AgentsMonitor</span>
        </a>

        <nav className="tabs" aria-label={t('mast.views')}>
          {TABS.map((v, i) => (
            <a key={v.id} href={`#${v.id}`} className={tab === v.id ? 'tab on' : 'tab'} aria-current={tab === v.id ? 'page' : undefined}>
              {tab === v.id && <motion.span layoutId="tab-thumb" className="tab-thumb" transition={{ type: 'spring', stiffness: 460, damping: 38 }} />}
              <span className="tab-idx" aria-hidden>
                {String(i + 1).padStart(2, '0')}
              </span>
              <span className="tab-label">{t(`tab.${v.id}`)}</span>
              {v.id === 'live' && waiting > 0 && (
                <motion.span className="badge" key={waiting} initial={{ scale: 0.4 }} animate={{ scale: 1 }} transition={{ type: 'spring', stiffness: 600, damping: 16 }}>
                  {waiting}
                </motion.span>
              )}
            </a>
          ))}
        </nav>

        <div className="masthead-right">
          <span className={`conn conn-${conn}`} title={t(conn === 'open' ? 'conn.liveTitle' : 'conn.retryTitle')}>
            <span className="conn-dot" />
            <AnimatePresence mode="wait" initial={false}>
              <motion.span
                key={`${conn}${locale}`}
                initial={{ opacity: 0, y: 4 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: -4 }}
                transition={{ duration: 0.25, ease: EASE }}
              >
                {conn === 'open'
                  ? server
                    ? t('conn.uptime', { v: server.version, d: duration(now - server.started_at).split(' ')[0]! })
                    : t('conn.open')
                  : t(conn === 'connecting' ? 'conn.connecting' : 'conn.closed')}
              </motion.span>
            </AnimatePresence>
          </span>
          <button className="cmdk" onClick={onPalette} aria-label={t('mast.palette')}>
            <SearchIcon />
            <span className="cmdk-label">{t('common.search')}</span>
            <kbd>{isMac ? '⌘K' : 'Ctrl K'}</kbd>
          </button>
          <button
            className="icon-btn lang-btn"
            onClick={() => setLocale(nextLocale(locale))}
            title={t('mast.language', { name: lang.label })}
            aria-label={t('mast.toggleLanguage')}
          >
            <AnimatePresence mode="wait" initial={false}>
              <motion.span
                key={locale}
                initial={{ y: 8, opacity: 0, filter: 'blur(4px)' }}
                animate={{ y: 0, opacity: 1, filter: 'blur(0px)' }}
                exit={{ y: -8, opacity: 0, filter: 'blur(4px)' }}
                transition={{ duration: 0.22, ease: EASE }}
              >
                {lang.short}
              </motion.span>
            </AnimatePresence>
          </button>
          <button
            className="icon-btn"
            onClick={e => {
              const r = e.currentTarget.getBoundingClientRect()
              onTheme({ x: r.left + r.width / 2, y: r.top + r.height / 2 })
            }}
            title={t('mast.theme', { name: t(`theme.${theme}`) })}
            aria-label={t('mast.toggleTheme')}
          >
            <AnimatePresence mode="wait" initial={false}>
              <motion.span
                key={theme}
                style={{ display: 'grid' }}
                initial={{ rotate: -90, scale: 0.6, opacity: 0 }}
                animate={{ rotate: 0, scale: 1, opacity: 1 }}
                exit={{ rotate: 90, scale: 0.6, opacity: 0 }}
                transition={{ duration: 0.28, ease: EASE }}
              >
                <ThemeIcon theme={theme} />
              </motion.span>
            </AnimatePresence>
          </button>
        </div>
      </div>
    </header>
  )
}
