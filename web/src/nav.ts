// Where the dashboard can go and how it looks: shared by the masthead, the
// keyboard shortcuts and the command palette.

export const TABS = [
  { id: 'live', label: '实时', en: 'Live' },
  { id: 'history', label: '历史', en: 'History' },
  { id: 'activity', label: '事件', en: 'Events' },
  { id: 'usage', label: '用量', en: 'Usage' },
  { id: 'daemon', label: '守护进程', en: 'Daemon' },
] as const
export type Tab = (typeof TABS)[number]['id']

export const tabFromHash = (): Tab => {
  const raw = location.hash.replace(/^#\/?/, '').split('?')[0]
  // The tab was called quota before it showed tokens too.
  const id = raw === 'quota' ? 'usage' : raw
  return TABS.some(t => t.id === id) ? (id as Tab) : 'live'
}

export type Theme = 'system' | 'dark' | 'light'
export const THEMES: { id: Theme; label: string }[] = [
  { id: 'system', label: '跟随系统' },
  { id: 'light', label: '浅色' },
  { id: 'dark', label: '深色' },
]
const THEME_KEY = 'agentsmon.theme'

export const readTheme = (): Theme => {
  try {
    const v = localStorage.getItem(THEME_KEY)
    return v === 'dark' || v === 'light' ? v : 'system'
  } catch {
    return 'system'
  }
}

export const applyTheme = (theme: Theme) => {
  const root = document.documentElement
  if (theme === 'system') delete root.dataset.theme
  else root.dataset.theme = theme
  try {
    localStorage.setItem(THEME_KEY, theme)
  } catch {
    // Private windows may refuse storage; the choice lasts this visit
  }
}

export const nextTheme = (t: Theme): Theme => (t === 'system' ? 'dark' : t === 'dark' ? 'light' : 'system')

export const isMac = /Mac|iPhone|iPad/.test(navigator.userAgent)
