import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import { motion } from 'motion/react'
import { api } from '../api.ts'
import { ago } from '../format.ts'
import { LOCALES, inAll, setLocale, useLocale, useT } from '../i18n/index.ts'
import { TABS, THEMES, isMac, type Theme } from '../nav.ts'
import { useLive } from '../store.ts'
import type { Session } from '../types.ts'
import { AgentBadge, SearchIcon, StatusPill, sessionPlace, sessionTitle, statusView } from './bits.tsx'
import { EASE } from './motion.tsx'

// ⌘K: find any session, live or past, jump to a view, change the theme or
// the language. Views, themes and languages match in every language.

interface Item {
  id: string
  group: string
  title: ReactNode
  sub?: ReactNode
  lead?: ReactNode
  aside?: ReactNode
  run: () => void
}

const haystack = (s: Session) => [s.meta.title, s.meta.project, s.meta.cwd, s.session_id, s.last_prompt].filter(Boolean).join(' ').toLowerCase()

export const CommandPalette = ({
  now,
  theme,
  onClose,
  onOpen,
  onTheme,
}: {
  now: number
  theme: Theme
  onClose: () => void
  onOpen: (key: string) => void
  onTheme: (t: Theme) => void
}) => {
  const live = useLive(s => s.sessions)
  const [past, setPast] = useState<Session[]>([])
  const [q, setQ] = useState('')
  const [active, setActive] = useState(0)
  const input = useRef<HTMLInputElement>(null)
  const list = useRef<HTMLDivElement>(null)
  const t = useT()
  const locale = useLocale()

  useEffect(() => {
    input.current?.focus()
    let alive = true
    api
      .history({ limit: 200 })
      .then(rows => alive && setPast(rows))
      .catch(() => {})
    return () => {
      alive = false
    }
  }, [])

  const items = useMemo(() => {
    const needle = q.trim().toLowerCase()
    const merged = new Map<string, Session>()
    for (const s of past) merged.set(s.key, s)
    for (const s of live.values()) merged.set(s.key, s)
    const sessions = [...merged.values()]
      .filter(s => !needle || haystack(s).includes(needle))
      .sort((a, b) => Number(a.status === 'ended') - Number(b.status === 'ended') || b.updated_at - a.updated_at)
      .slice(0, needle ? 14 : 6)

    const out: Item[] = sessions.map(s => ({
      id: s.key,
      group: t('pal.sessions'),
      lead: <AgentBadge agent={s.agent} />,
      title: sessionTitle(s),
      sub: <span className="mono">{sessionPlace(s)}</span>,
      aside: s.status === 'ended' ? <span className="muted small">{ago(s.updated_at, now)}</span> : <StatusPill view={statusView(s)} />,
      run: () => onOpen(s.key),
    }))
    const chosen = <span className="muted small">{t('pal.current')}</span>
    TABS.forEach((v, i) => {
      if (needle && !`${inAll(`tab.${v.id}`)} ${v.id} ${inAll('pal.jump')}`.toLowerCase().includes(needle)) return
      out.push({ id: `tab:${v.id}`, group: t('pal.jump'), title: t(`tab.${v.id}`), sub: `#${v.id}`, aside: <kbd>{i + 1}</kbd>, run: () => void (location.hash = v.id) })
    })
    for (const id of THEMES) {
      if (needle && !`${inAll(`theme.${id}`)} ${inAll('pal.theme')} ${id}`.toLowerCase().includes(needle)) continue
      out.push({ id: `theme:${id}`, group: t('pal.theme'), title: t(`theme.${id}`), aside: theme === id ? chosen : undefined, run: () => onTheme(id) })
    }
    for (const l of LOCALES) {
      if (needle && !`${l.label} ${l.id} ${inAll('pal.language')}`.toLowerCase().includes(needle)) continue
      out.push({ id: `lang:${l.id}`, group: t('pal.language'), title: l.label, aside: locale === l.id ? chosen : undefined, run: () => setLocale(l.id) })
    }
    return out
  }, [q, past, live, now, theme, locale, t, onOpen, onTheme])

  const current = Math.min(active, Math.max(0, items.length - 1))

  useEffect(() => {
    list.current?.querySelector('[data-active="true"]')?.scrollIntoView({ block: 'nearest' })
  }, [current])

  const run = (item: Item | undefined) => {
    if (!item) return
    onClose()
    item.run()
  }

  const onKey = (e: React.KeyboardEvent) => {
    const n = Math.max(1, items.length)
    if (e.key === 'ArrowDown') {
      e.preventDefault()
      setActive((current + 1) % n)
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      setActive((current - 1 + n) % n)
    } else if (e.key === 'Enter') {
      e.preventDefault()
      run(items[current])
    } else if (e.key === 'Escape') {
      // Only the palette closes; an open drawer stays
      e.preventDefault()
      e.stopPropagation()
      onClose()
    }
  }

  return (
    <motion.div
      className="palette-backdrop"
      onMouseDown={onClose}
      initial={{ opacity: 0 }}
      animate={{ opacity: 1 }}
      exit={{ opacity: 0, transition: { duration: 0.18 } }}
      transition={{ duration: 0.25 }}
    >
      <motion.div
        className="palette"
        role="dialog"
        aria-modal="true"
        aria-label={t('pal.label')}
        onMouseDown={e => e.stopPropagation()}
        initial={{ opacity: 0, y: -14, scale: 0.97 }}
        animate={{ opacity: 1, y: 0, scale: 1 }}
        exit={{ opacity: 0, y: -8, scale: 0.98, transition: { duration: 0.16 } }}
        transition={{ duration: 0.4, ease: EASE }}
      >
        <label className="palette-search">
          <SearchIcon />
          <input
            ref={input}
            value={q}
            onChange={e => {
              setQ(e.target.value)
              setActive(0)
            }}
            onKeyDown={onKey}
            placeholder={t('pal.placeholder')}
            aria-label={t('common.search')}
            role="combobox"
            aria-expanded="true"
            aria-controls="palette-list"
            aria-activedescendant={items[current] ? `pi-${current}` : undefined}
          />
          <kbd>esc</kbd>
        </label>
        <div className="palette-list" id="palette-list" role="listbox" ref={list}>
          {items.length === 0 && <div className="palette-empty">{t('pal.empty', { q })}</div>}
          {items.map((item, i) => (
            <div key={item.id} className="palette-group-wrap">
              {item.group !== items[i - 1]?.group && <div className="palette-group label">{item.group}</div>}
              <div
                id={`pi-${i}`}
                role="option"
                aria-selected={i === current}
                data-active={i === current}
                className="palette-item"
                onMouseMove={() => i !== current && setActive(i)}
                onClick={() => run(item)}
              >
                {i === current && <motion.span layoutId="palette-hl" className="palette-hl" transition={{ type: 'spring', stiffness: 700, damping: 46 }} />}
                {item.lead}
                <span className="palette-text">
                  <span className="palette-title">{item.title}</span>
                  {item.sub && <span className="palette-sub">{item.sub}</span>}
                </span>
                {item.aside && <span className="palette-aside">{item.aside}</span>}
              </div>
            </div>
          ))}
        </div>
        <div className="palette-foot">
          <span>
            <kbd>↑</kbd>
            <kbd>↓</kbd> {t('pal.select')}
          </span>
          <span>
            <kbd>↵</kbd> {t('pal.open')}
          </span>
          <span>
            <kbd>{isMac ? '⌘K' : 'Ctrl K'}</kbd> {t('pal.toggle')}
          </span>
        </div>
      </motion.div>
    </motion.div>
  )
}
