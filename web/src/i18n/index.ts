import { createElement, Fragment, useSyncExternalStore, type ReactNode } from 'react'
import { en } from './en.ts'
import { zh } from './zh.ts'

// The dashboard's languages. English is the reference dictionary: every key
// exists there, and another language falls back to it for a missing one. A
// message is a string with {name} placeholders, or a function of its
// parameters where the grammar needs one (plurals). The choice is kept in
// localStorage; the first visit follows the browser.

export type Locale = 'en' | 'zh'
export type Params = Record<string, string | number>
export type Message = string | ((p: Params) => string)
export type Key = keyof typeof en
export type Dict = Partial<Record<Key, Message>>

/** Each language under its own name, so it can be found whatever the current one is. */
export const LOCALES: readonly { id: Locale; label: string; short: string }[] = [
  { id: 'en', label: 'English', short: 'EN' },
  { id: 'zh', label: '中文', short: '中' },
]

const DICTS: Record<Locale, Dict> = { en, zh }
const STORE_KEY = 'agentsmon.locale'

const detect = (): Locale => {
  try {
    const v = localStorage.getItem(STORE_KEY)
    if (v === 'en' || v === 'zh') return v
  } catch {
    // Private windows may refuse storage; follow the browser
  }
  return navigator.language.toLowerCase().startsWith('zh') ? 'zh' : 'en'
}

let locale: Locale = detect()
const listeners = new Set<() => void>()
const tag = (l: Locale) => (l === 'zh' ? 'zh-CN' : 'en')
document.documentElement.lang = tag(locale)

export const getLocale = () => locale

export const setLocale = (next: Locale) => {
  if (next === locale) return
  locale = next
  document.documentElement.lang = tag(next)
  try {
    localStorage.setItem(STORE_KEY, next)
  } catch {
    // The choice lasts this visit
  }
  for (const l of listeners) l()
}

export const nextLocale = (l: Locale): Locale => (l === 'en' ? 'zh' : 'en')

const subscribe = (l: () => void) => {
  listeners.add(l)
  return () => listeners.delete(l)
}

const fill = (m: Message, p?: Params) => {
  const s = typeof m === 'function' ? m(p ?? {}) : m
  return p ? s.replace(/\{(\w+)\}/g, (all, k: string) => (k in p ? String(p[k]) : all)) : s
}

/** The message for `key` in the current language. */
export const t = (key: Key, p?: Params): string => fill(DICTS[locale][key] ?? en[key], p)

/** Whether a key built at run time exists, e.g. a window kind the daemon sends. */
export const has = (key: string): key is Key => key in en

/** A message in every language at once: what search matches against. */
export const inAll = (key: Key) => LOCALES.map(l => fill(DICTS[l.id][key] ?? en[key])).join(' ')

/** The current language; a component that renders text calls this (or useT) to follow changes. */
export const useLocale = () => useSyncExternalStore(subscribe, getLocale)

export const useT = () => {
  useLocale()
  return t
}

/** The language tag for dates and numbers. */
export const dateLocale = () => (locale === 'zh' ? 'zh-CN' : 'en-US')

/** Turns <em>…</em> and <code>…</code> inside a message into elements. */
export const rich = (s: string): ReactNode =>
  createElement(
    Fragment,
    null,
    ...s.split(/(<em>.*?<\/em>|<code>.*?<\/code>)/).map((part, i) => {
      const m = /^<(em|code)>(.*)<\/\1>$/.exec(part)
      return m ? createElement(m[1]!, { key: i }, m[2]) : part
    }),
  )
