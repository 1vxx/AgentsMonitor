import { useEffect, useRef } from 'react'
import { onEvent } from '../store.ts'
import type { Mood } from '../types.ts'
import type { Tone } from './bits.tsx'
import { describe } from './events.tsx'

// The sky behind every page, on one canvas. The stars turn slowly round a pole
// in the upper right, where the rings of an astrolabe keep time and a sweep
// goes round; a Lissajous figure retraces itself in the lower left and now and
// then a meteor crosses. Each event from the daemon sends a ring out from the
// pole in the colour of its state. The stars shift a little with the pointer
// and the scroll, and each view looks at its own sector of the sky: changing
// views turns it there. With reduced motion the sky is drawn once and holds
// still.

const TAU = Math.PI * 2
const SKY_TURN = TAU / 1500 // the stars go round once every 25 minutes
const RINGS = [1, 1.7, 2.6, 3.8, 5.3, 7.2]
const DEPTH = [0.25, 0.55, 1]
const PING_LIFE = 4
const SECTOR = TAU / 24

interface Star {
  x: number
  y: number
  size: number
  alpha: number
  tw: number
  ph: number
}
interface Comet {
  t0: number
  x: number
  y: number
  dx: number
  dy: number
  len: number
  dur: number
}
interface Sky {
  dark: boolean
  star: string
  line: string
  starA: number
  lineA: number
  tones: Record<string, string>
}

/** Seeded, so the sky is the same one on every visit. */
const rng = (seed: number) => () => ((seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) / 4294967296)

const readSky = (): Sky => {
  const cs = getComputedStyle(document.documentElement)
  const v = (n: string) => cs.getPropertyValue(n).trim()
  return {
    dark: cs.colorScheme.includes('dark'),
    star: v('--sky-star') || '#fff',
    line: v('--sky-line') || '#8ea2ff',
    starA: Number(v('--sky-star-a')) || 1,
    lineA: Number(v('--sky-line-a')) || 1,
    tones: {
      active: v('--tone-active'),
      attention: v('--tone-attention'),
      good: v('--tone-good'),
      critical: v('--tone-critical'),
      warning: v('--tone-warning'),
    },
  }
}

/** A soft star, drawn once per theme and stamped for every bright one. */
const starSprite = (sky: Sky) => {
  const c = document.createElement('canvas')
  c.width = c.height = 32
  const g = c.getContext('2d')!
  const grad = g.createRadialGradient(16, 16, 0, 16, 16, 16)
  grad.addColorStop(0, sky.star)
  if (sky.dark) {
    grad.addColorStop(0.12, sky.star)
    grad.addColorStop(0.3, 'rgba(255,255,255,0.18)')
    grad.addColorStop(1, 'rgba(255,255,255,0)')
  } else {
    // On paper a star is a crisp inked dot
    grad.addColorStop(0.3, sky.star)
    grad.addColorStop(0.38, 'rgba(0,0,0,0)')
  }
  g.fillStyle = grad
  g.fillRect(0, 0, 32, 32)
  return c
}

export const Cosmos = ({ mood, sector }: { mood: Mood; sector: number }) => {
  const ref = useRef<HTMLCanvasElement>(null)
  const aim = useRef(sector * SECTOR)
  const redraw = useRef<() => void>(undefined)
  useEffect(() => {
    aim.current = sector * SECTOR
    redraw.current?.()
  }, [sector])

  useEffect(() => {
    const canvas = ref.current
    const ctx = canvas?.getContext('2d')
    if (!canvas || !ctx) return
    const still = matchMedia('(prefers-reduced-motion: reduce)')
    const darkQuery = matchMedia('(prefers-color-scheme: dark)')

    let w = 0
    let h = 0
    let dpr = 1
    let unit = 100
    let layers: Star[][] = [[], [], []]
    let figures: { x: number; y: number }[][] = []
    let sky = readSky()
    let sprite = starSprite(sky)
    const pointer = { x: 0, y: 0, tx: 0, ty: 0 }
    const pings: { t0: number; color: string }[] = []
    const comets: Comet[] = []
    let nextComet = 4
    let clock = 0
    let lastPing = -1
    let turned = aim.current

    // Just under the masthead, clear of every page's header
    const poleAt = () => ({ x: w * (w < 760 ? 0.88 : 0.84), y: w < 760 ? 150 : 112 })

    const seed = () => {
      const rand = rng(7788)
      const pole = poleAt()
      const reach = Math.hypot(Math.max(pole.x, w - pole.x), Math.max(pole.y, h - pole.y)) + 80
      const count = Math.min(2400, Math.round((Math.PI * reach * reach) / 5200))
      layers = [[], [], []]
      for (let i = 0; i < count; i++) {
        const r = reach * Math.sqrt(rand())
        const a = rand() * TAU
        const z = rand() ** 2
        const layer = z < 0.6 ? 0 : z < 0.88 ? 1 : 2
        layers[layer]!.push({
          x: Math.cos(a) * r,
          y: Math.sin(a) * r,
          size: [0.7, 1.05, 1.5][layer]! + rand() * [0.35, 0.4, 1.1][layer]!,
          alpha: [0.35, 0.55, 0.8][layer]! + rand() * 0.2,
          tw: 0.4 + rand() * 1.6,
          ph: rand() * TAU,
        })
      }
      // A few constellations: short chains of bright stars, joined by hairlines
      figures = Array.from({ length: 9 }, () => {
        const r = reach * (0.25 + 0.7 * Math.sqrt(rand()))
        const a = rand() * TAU
        let x = Math.cos(a) * r
        let y = Math.sin(a) * r
        let dir = rand() * TAU
        return Array.from({ length: 4 + Math.floor(rand() * 3) }, () => {
          const p = { x, y }
          dir += (rand() - 0.5) * 2.2
          const step = 38 + rand() * 60
          x += Math.cos(dir) * step
          y += Math.sin(dir) * step
          layers[2]!.push({ ...p, size: 1.8 + rand() * 1.2, alpha: 0.95, tw: 0.3 + rand() * 0.6, ph: rand() * TAU })
          return p
        })
      })
    }

    const resize = () => {
      dpr = Math.min(window.devicePixelRatio || 1, 2)
      w = canvas.clientWidth || window.innerWidth
      h = canvas.clientHeight || window.innerHeight
      canvas.width = Math.round(w * dpr)
      canvas.height = Math.round(h * dpr)
      unit = Math.max(64, Math.min(130, Math.min(w, h) * 0.1))
      seed()
    }

    const drawStars = (t: number, pole: { x: number; y: number }, scroll: number) => {
      const turn = t * SKY_TURN + turned
      for (let l = 0; l < 3; l++) {
        const d = DEPTH[l]!
        ctx.save()
        ctx.translate(pole.x + pointer.x * d, pole.y + pointer.y * d - scroll * 0.05 * d)
        ctx.rotate(turn)
        ctx.fillStyle = sky.star
        for (const s of layers[l]!) {
          const a = s.alpha * sky.starA * (0.62 + 0.38 * Math.sin(t * s.tw + s.ph))
          if (a <= 0.02) continue
          ctx.globalAlpha = a
          if (s.size > 1.5) {
            const k = s.size * (sky.dark ? 3.2 : 2.6)
            ctx.drawImage(sprite, s.x - k, s.y - k, k * 2, k * 2)
          } else {
            ctx.fillRect(s.x, s.y, s.size, s.size)
          }
        }
        if (l === 2) {
          ctx.strokeStyle = sky.line
          ctx.lineWidth = 0.75
          ctx.globalAlpha = 0.2 * sky.lineA
          ctx.beginPath()
          for (const f of figures) {
            f.forEach((p, i) => (i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y)))
          }
          ctx.stroke()
        }
        ctx.restore()
      }
    }

    const drawAstrolabe = (t: number, cx: number, cy: number) => {
      const la = sky.lineA
      ctx.save()
      ctx.translate(cx, cy)
      ctx.strokeStyle = sky.line
      ctx.fillStyle = sky.line
      ctx.lineWidth = 1

      // The sweep, a soft wedge going round every 14 seconds
      const sweep = (t * TAU) / 14
      const outer = RINGS.at(-1)! * unit
      if (ctx.createConicGradient) {
        const g = ctx.createConicGradient(sweep - 0.9, 0, 0)
        g.addColorStop(0, 'rgba(0,0,0,0)')
        g.addColorStop(0.143, sky.line)
        g.addColorStop(0.1431, 'rgba(0,0,0,0)')
        ctx.globalAlpha = 0.05 * la
        ctx.fillStyle = g
        ctx.beginPath()
        ctx.arc(0, 0, RINGS[4]! * unit, 0, TAU)
        ctx.fill()
        ctx.fillStyle = sky.line
      }

      ctx.globalAlpha = 0.12 * la
      ctx.beginPath()
      for (const k of RINGS) {
        ctx.moveTo(k * unit, 0)
        ctx.arc(0, 0, k * unit, 0, TAU)
      }
      ctx.stroke()

      ctx.globalAlpha = 0.055 * la
      ctx.beginPath()
      for (let i = 0; i < 24; i++) {
        const a = (i * TAU) / 24
        ctx.moveTo(Math.cos(a) * unit, Math.sin(a) * unit)
        ctx.lineTo(Math.cos(a) * outer, Math.sin(a) * outer)
      }
      ctx.stroke()

      // The rete: an eccentric dashed ring that turns with the stars
      ctx.save()
      ctx.rotate(t * SKY_TURN + turned)
      ctx.setLineDash([2, 7])
      ctx.globalAlpha = 0.16 * la
      ctx.beginPath()
      ctx.arc(unit * 0.95, 0, unit * 3.3, 0, TAU)
      ctx.stroke()
      ctx.restore()

      // A graduated ring turning the other way, a degree mark every 2°
      const gr = RINGS[2]! * unit
      ctx.save()
      ctx.rotate((-t * TAU) / 420 - turned * 0.5)
      ctx.globalAlpha = 0.2 * la
      ctx.beginPath()
      for (let deg = 0; deg < 360; deg += 2) {
        const len = deg % 30 === 0 ? 11 : deg % 10 === 0 ? 6 : 3
        const a = (deg * Math.PI) / 180
        ctx.moveTo(Math.cos(a) * gr, Math.sin(a) * gr)
        ctx.lineTo(Math.cos(a) * (gr + len), Math.sin(a) * (gr + len))
      }
      ctx.stroke()
      ctx.globalAlpha = 0.34 * la
      ctx.font = '500 9px ui-monospace, "SF Mono", Menlo, monospace'
      ctx.textAlign = 'center'
      ctx.textBaseline = 'middle'
      for (let deg = 0; deg < 360; deg += 30) {
        ctx.save()
        ctx.rotate((deg * Math.PI) / 180)
        ctx.translate(gr + 22, 0)
        ctx.rotate(Math.PI / 2)
        ctx.fillText(`${deg}°`, 0, 0)
        ctx.restore()
      }
      ctx.restore()

      // The pole itself, with its coordinates
      ctx.globalAlpha = 0.4 * la
      ctx.beginPath()
      ctx.moveTo(-7, 0)
      ctx.lineTo(7, 0)
      ctx.moveTo(0, -7)
      ctx.lineTo(0, 7)
      ctx.stroke()
      ctx.beginPath()
      ctx.arc(0, 0, 3, 0, TAU)
      ctx.stroke()
      ctx.globalAlpha = 0.32 * la
      ctx.textAlign = 'right'
      ctx.font = '500 9.5px ui-monospace, "SF Mono", Menlo, monospace'
      ctx.fillText('α 00ʰ00ᵐ  δ +90°', -12, -11)

      // Signals from the daemon
      for (let i = pings.length - 1; i >= 0; i--) {
        const p = pings[i]!
        const age = t - p.t0
        if (age > PING_LIFE || age < 0) {
          pings.splice(i, 1)
          continue
        }
        const k = age / PING_LIFE
        ctx.strokeStyle = p.color
        ctx.globalAlpha = 0.55 * (1 - k) ** 2
        ctx.lineWidth = 1.5 - k
        ctx.beginPath()
        ctx.arc(0, 0, unit * 0.4 + k * unit * 6.2, 0, TAU)
        ctx.stroke()
      }
      ctx.restore()
    }

    const drawFigure = (t: number) => {
      // x = sin(3s + δ), y = sin(2s): a 3:2 Lissajous whose phase drifts
      const r = Math.max(56, Math.min(140, Math.min(w, h) * 0.12))
      const cx = Math.max(r + 24, w * 0.09)
      const cy = h - Math.max(r + 40, h * 0.18)
      const delta = t * 0.06
      const at = (s: number) => ({ x: cx + Math.sin(3 * s + delta) * r, y: cy + Math.sin(2 * s) * r * 0.82 })
      const la = sky.lineA
      ctx.save()
      ctx.strokeStyle = sky.line
      ctx.fillStyle = sky.line
      ctx.lineWidth = 1

      // Its frame: axes with ticks, as on an oscilloscope
      ctx.globalAlpha = 0.07 * la
      ctx.beginPath()
      ctx.moveTo(cx - r * 1.25, cy)
      ctx.lineTo(cx + r * 1.25, cy)
      ctx.moveTo(cx, cy - r * 1.1)
      ctx.lineTo(cx, cy + r * 1.1)
      for (let i = -4; i <= 4; i++) {
        ctx.moveTo(cx + (i * r) / 4, cy - 3)
        ctx.lineTo(cx + (i * r) / 4, cy + 3)
        ctx.moveTo(cx - 3, cy + (i * r * 0.82) / 4)
        ctx.lineTo(cx + 3, cy + (i * r * 0.82) / 4)
      }
      ctx.stroke()

      ctx.globalAlpha = 0.13 * la
      ctx.beginPath()
      for (let i = 0; i <= 360; i++) {
        const p = at((i / 360) * TAU)
        if (i) ctx.lineTo(p.x, p.y)
        else ctx.moveTo(p.x, p.y)
      }
      ctx.stroke()

      // The pen, with a brighter stretch of the curve behind it
      const pen = t * 0.4
      ctx.globalAlpha = 0.4 * la
      ctx.lineWidth = 1.25
      ctx.beginPath()
      for (let i = 0; i <= 40; i++) {
        const p = at(pen - 0.9 + (i / 40) * 0.9)
        if (i) ctx.lineTo(p.x, p.y)
        else ctx.moveTo(p.x, p.y)
      }
      ctx.stroke()
      const head = at(pen)
      ctx.globalAlpha = 0.85 * la
      ctx.beginPath()
      ctx.arc(head.x, head.y, 2, 0, TAU)
      ctx.fill()

      ctx.globalAlpha = 0.3 * la
      ctx.font = 'italic 500 10px ui-monospace, "SF Mono", Menlo, monospace'
      ctx.fillText(`x = sin(3t + ${(delta % TAU).toFixed(2)})   y = sin(2t)`, cx - r * 1.2, cy + r * 1.1 + 18)
      ctx.restore()
    }

    const drawComets = (t: number) => {
      if (t > nextComet) {
        const a = 2.35 + Math.random() * 0.45
        const speed = 650 + Math.random() * 500
        comets.push({
          t0: t,
          x: w * (0.25 + Math.random() * 0.8),
          y: -20 + Math.random() * h * 0.35,
          dx: Math.cos(a) * speed,
          dy: Math.sin(a) * speed,
          len: 80 + Math.random() * 120,
          dur: 0.8 + Math.random() * 0.7,
        })
        nextComet = t + 6 + Math.random() * 9
      }
      for (let i = comets.length - 1; i >= 0; i--) {
        const c = comets[i]!
        const age = t - c.t0
        if (age > c.dur) {
          comets.splice(i, 1)
          continue
        }
        const hx = c.x + c.dx * age
        const hy = c.y + c.dy * age
        const n = Math.hypot(c.dx, c.dy)
        const tx = hx - (c.dx / n) * c.len
        const ty = hy - (c.dy / n) * c.len
        const g = ctx.createLinearGradient(hx, hy, tx, ty)
        g.addColorStop(0, sky.star)
        g.addColorStop(1, 'rgba(0,0,0,0)')
        ctx.globalAlpha = Math.sin((Math.PI * age) / c.dur) * 0.85 * sky.starA
        ctx.strokeStyle = g
        ctx.lineWidth = 1.4
        ctx.lineCap = 'round'
        ctx.beginPath()
        ctx.moveTo(hx, hy)
        ctx.lineTo(tx, ty)
        ctx.stroke()
      }
    }

    const draw = (t: number, dt = 0) => {
      pointer.x += (pointer.tx - pointer.x) * 0.045
      pointer.y += (pointer.ty - pointer.y) * 0.045
      turned = dt ? turned + (aim.current - turned) * (1 - Math.exp(-dt * 2.6)) : aim.current
      const scroll = window.scrollY
      const pole = poleAt()
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.clearRect(0, 0, w, h)
      drawStars(t, pole, scroll)
      drawFigure(t)
      drawAstrolabe(t, pole.x + pointer.x * 0.3, pole.y + pointer.y * 0.3 - scroll * 0.14)
      drawComets(t)
      ctx.globalAlpha = 1
    }

    let raf = 0
    let last = performance.now()
    const frame = (ms: number) => {
      const dt = Math.min(0.1, (ms - last) / 1000)
      clock += dt
      last = ms
      draw(clock, dt)
      raf = requestAnimationFrame(frame)
    }
    const run = () => {
      cancelAnimationFrame(raf)
      if (still.matches) {
        pointer.tx = pointer.ty = pointer.x = pointer.y = 0
        draw(clock || 30)
      } else {
        last = performance.now()
        raf = requestAnimationFrame(frame)
      }
    }

    const onResize = () => {
      resize()
      if (still.matches) draw(clock || 30)
    }
    const onTheme = () => {
      sky = readSky()
      sprite = starSprite(sky)
      if (still.matches) draw(clock || 30)
    }
    const onPointer = (e: PointerEvent) => {
      pointer.tx = (e.clientX / w - 0.5) * -36
      pointer.ty = (e.clientY / h - 0.5) * -28
    }
    const themeObserver = new MutationObserver(onTheme)
    themeObserver.observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] })
    darkQuery.addEventListener('change', onTheme)
    still.addEventListener('change', run)
    const sizer = new ResizeObserver(onResize)
    sizer.observe(canvas)
    window.addEventListener('pointermove', onPointer, { passive: true })
    const off = onEvent(r => {
      if (still.matches || clock - lastPing < 0.2) return
      lastPing = clock
      const tone: Tone = describe(r).tone
      pings.push({ t0: clock, color: sky.tones[tone] || sky.line })
      if (pings.length > 10) pings.shift()
    })

    redraw.current = () => still.matches && draw(clock || 30)
    resize()
    run()
    return () => {
      redraw.current = undefined
      cancelAnimationFrame(raf)
      off()
      themeObserver.disconnect()
      darkQuery.removeEventListener('change', onTheme)
      still.removeEventListener('change', run)
      sizer.disconnect()
      window.removeEventListener('pointermove', onPointer)
    }
  }, [])

  return (
    <div className={`cosmos mood-${mood}`} aria-hidden>
      <div className="cosmos-grid" />
      <div className="nebula nebula-1" />
      <div className="nebula nebula-2" />
      <div className="nebula nebula-3" />
      <canvas ref={ref} className="cosmos-sky" />
      <div className="cosmos-grain" />
    </div>
  )
}
