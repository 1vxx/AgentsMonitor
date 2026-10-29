// The page around the display: light or dark, the theme menu, segmented
// controls, the dial round the device, the toasts chimes leave and the
// shortcuts sheet. Nothing here knows about a theme or a summary; main.js
// wires these to them.

import { t } from './i18n.js'

export const $ = id => document.getElementById(id)

export const saved = key => {
  try {
    return localStorage.getItem(key)
  } catch {
    return null
  }
}
export const save = (key, value) => {
  try {
    localStorage.setItem(key, value)
  } catch {}
}

// ---------- Light and dark ----------

// The choice is system, light or dark; the page itself is always one of the
// two, set on <html> (index.html does it once before the first paint)
const MODES = ['system', 'light', 'dark']
const systemDark = matchMedia('(prefers-color-scheme: dark)')
let mode = MODES.includes(saved('am.mode')) ? saved('am.mode') : 'system'

export const showMode = () => {
  const dark = mode === 'dark' || (mode === 'system' && systemDark.matches)
  document.documentElement.dataset.mode = dark ? 'dark' : 'light'
  $('mode').dataset.pref = mode
  $('mode').title = t(`mode.${mode}`)
  $('mode').setAttribute('aria-label', t(`mode.${mode}`))
}
export const cycleMode = () => {
  mode = MODES[(MODES.indexOf(mode) + 1) % MODES.length]
  save('am.mode', mode)
  showMode()
}
systemDark.addEventListener('change', showMode)
$('mode').onclick = cycleMode

// ---------- Segmented controls ----------

// The buttons in #id, each with its value in data-v: marks the one chosen and
// tells onChange, now for `value` and on every click; returns a setter
export const segmented = (id, value, onChange) => {
  const buttons = [...$(id).querySelectorAll('button')]
  const set = v => {
    buttons.forEach(b => b.classList.toggle('on', b.dataset.v === String(v)))
    onChange(String(v))
  }
  buttons.forEach(b => (b.onclick = () => set(b.dataset.v)))
  set(value)
  return set
}

// ---------- The theme menu ----------

// A list of every theme with its description; choosing another calls onPick
export const themeMenu = (themes, current, pick, onPick) => {
  const button = $('theme-btn')
  const menu = $('theme-menu')
  let active = 0

  const render = () => {
    menu.replaceChildren(
      ...themes.map((theme, i) => {
        const li = document.createElement('li')
        li.setAttribute('role', 'option')
        li.setAttribute('aria-selected', String(theme === current))
        li.classList.toggle('active', i === active)
        for (const [cls, text] of [['n', pick(theme.name)], ['id', theme.id], ['d', pick(theme.about)]]) {
          const span = document.createElement('span')
          span.className = cls
          span.textContent = text
          li.append(span)
        }
        li.onclick = () => choose(theme)
        return li
      }),
    )
  }
  const open = () => {
    active = themes.indexOf(current)
    render()
    menu.hidden = false
    button.setAttribute('aria-expanded', 'true')
  }
  const close = () => {
    menu.hidden = true
    button.setAttribute('aria-expanded', 'false')
  }
  const choose = theme => {
    close()
    if (theme !== current) onPick(theme)
  }

  button.onclick = e => {
    e.stopPropagation()
    menu.hidden ? open() : close()
  }
  document.addEventListener('click', e => !menu.hidden && !menu.contains(e.target) && close())

  return {
    // Says the current theme again, in the current language
    describe: () => {
      $('theme-name').textContent = pick(current.name)
      if (!menu.hidden) render()
    },
    // Arrow keys, Enter and Escape while it is open; true if the key was its
    key: e => {
      if (menu.hidden) return false
      if (e.key === 'Escape') close()
      else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        active = (active + (e.key === 'ArrowDown' ? 1 : themes.length - 1)) % themes.length
        render()
      } else if (e.key === 'Enter') choose(themes[active])
      else return false
      return true
    },
  }
}

// ---------- The dial round the device ----------

// A bearing ring: a tick every 3°, a longer one with its bearing every 30°
export const drawDial = () => {
  const svg = $('dial')
  const add = (name, attrs, text) => {
    const el = document.createElementNS('http://www.w3.org/2000/svg', name)
    for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v)
    if (text) el.textContent = text
    svg.append(el)
  }
  add('circle', { r: 97, class: 'ring' })
  add('circle', { r: 90.5, class: 'ring-faint' })
  for (let k = 0; k < 120; k++) {
    const deg = k * 3
    const a = (deg * Math.PI) / 180
    const major = deg % 30 === 0
    const at = r => [(r * Math.sin(a)).toFixed(2), (-r * Math.cos(a)).toFixed(2)]
    const [x1, y1] = at(major ? 92 : 94.6)
    const [x2, y2] = at(97)
    add('line', { x1, y1, x2, y2, class: major ? 'tick major' : 'tick' })
    if (major) {
      const [x, y] = at(87.6)
      add('text', { x, y, transform: `rotate(${deg} ${x} ${y})` }, String(deg).padStart(3, '0'))
    }
  }
}

// ---------- Toasts ----------

// A chip that rises from the display and fades, in a tone of the page's
export const toast = (text, tone) => {
  const box = $('toasts')
  const el = document.createElement('span')
  el.className = `toast t-${tone}`
  el.textContent = text
  box.append(el)
  setTimeout(() => el.remove(), 2700)
  while (box.children.length > 4) box.firstChild.remove()
}

// ---------- The shortcuts sheet ----------

const sheet = $('keys')
export const showKeys = on => {
  sheet.hidden = !on
  if (on) $('keys-close').focus()
}
export const keysOpen = () => !sheet.hidden
$('keys-btn').onclick = () => showKeys(true)
$('keys-close').onclick = () => showKeys(false)
sheet.onclick = e => e.target === sheet && showKeys(false)

// Keys for the page, except while typing into a field
export const onKey = handler =>
  addEventListener('keydown', e => {
    if (e.metaKey || e.ctrlKey || e.altKey) return
    if (e.target instanceof HTMLInputElement || e.target instanceof HTMLSelectElement || e.target instanceof HTMLTextAreaElement) return
    handler(e)
  })
