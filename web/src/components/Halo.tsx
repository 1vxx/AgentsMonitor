import { useEffect, useId, useMemo, useRef } from 'react'
import type { Mood, Session } from '../types.ts'
import { sessionTitle, statusView, type Tone } from './bits.tsx'
import { Orb } from './Orb.tsx'

// The mood orb at the centre of its own small system: graduated rings like an
// astrolabe's, a legend running round the rim, and one satellite per live
// session. A working session orbits quickly, one at rest drifts, one waiting
// for you stops where it is and sends out rings. A satellite opens its session.

const R_IN = 71
const R_OUT = 87
/** Degrees per second for each state. */
const SPEED: Record<Tone, number> = { active: 52, attention: 0, critical: 3, warning: 5, good: 6, idle: 7, muted: 2 }

const hash = (s: string) => {
  let h = 2166136261
  for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619)
  return h >>> 0
}

const TAIL = (r: number) => {
  const a = (-32 * Math.PI) / 180
  return `M${(Math.cos(a) * r).toFixed(2)},${(Math.sin(a) * r).toFixed(2)}A${r},${r} 0 0 1 ${r},0`
}

const TICKS = Array.from({ length: 120 }, (_, i) => {
  const a = (i * 3 * Math.PI) / 180
  const len = i % 10 === 0 ? 6 : i % 5 === 0 ? 3.5 : 2
  return { x1: Math.cos(a) * 94, y1: Math.sin(a) * 94, x2: Math.cos(a) * (94 + len), y2: Math.sin(a) * (94 + len), major: i % 10 === 0 }
})

interface Sat {
  key: string
  tone: Tone
  ring: number
  label: string
}

export const Halo = ({ mood, sessions, onOpen, size = 224 }: { mood: Mood; sessions: ReadonlyMap<string, Session>; onOpen: (key: string) => void; size?: number }) => {
  const path = `halo${useId().replace(/:/g, '')}`
  const sats = useMemo<Sat[]>(
    () =>
      [...sessions.values()]
        .filter(s => s.status !== 'ended')
        .sort((a, b) => (a.key < b.key ? -1 : 1))
        .slice(0, 16)
        .map((s, i) => ({ key: s.key, tone: statusView(s).tone, ring: i % 2 ? R_OUT : R_IN, label: `${sessionTitle(s)} · ${statusView(s).label}` })),
    [sessions],
  )
  const nodes = useRef(new Map<string, SVGGElement>())
  const motion = useRef(new Map<string, { a: number; v: number }>())
  const latest = useRef(sats)
  latest.current = sats

  // One frame loop moves every satellite; speeds ease from one state to the next
  useEffect(() => {
    const still = matchMedia('(prefers-reduced-motion: reduce)').matches
    let raf = 0
    let last = performance.now()
    const tick = (ms: number) => {
      const dt = Math.min(0.1, (ms - last) / 1000)
      last = ms
      for (const s of latest.current) {
        let m = motion.current.get(s.key)
        if (!m) {
          m = { a: hash(s.key) % 360, v: SPEED[s.tone] }
          motion.current.set(s.key, m)
        }
        m.v += (SPEED[s.tone] - m.v) * Math.min(1, dt * 1.6)
        if (!still) m.a = (m.a + m.v * dt) % 360
        nodes.current.get(s.key)?.setAttribute('transform', `rotate(${m.a.toFixed(2)})`)
      }
      if (!still) raf = requestAnimationFrame(tick)
    }
    raf = requestAnimationFrame(tick)
    return () => cancelAnimationFrame(raf)
  }, [sats])

  const legend = `AGENTSMONITOR  ✦  ${String(sats.length).padStart(2, '0')} IN ORBIT  ✦  T² ∝ a³  ✦  MOOD ${mood.toUpperCase()}  ✦  `

  return (
    <div className={`halo mood-${mood}`} style={{ ['--s' as string]: `${size}px` }}>
      <svg className="halo-svg" viewBox="-112 -112 224 224">
        <defs>
          <path id={path} d="M0,-104A104,104 0 1 1 0,104A104,104 0 1 1 0,-104" />
        </defs>
        <g className="halo-scale" aria-hidden>
          {TICKS.map((t, i) => (
            <line key={i} className={t.major ? 'major' : undefined} x1={t.x1} y1={t.y1} x2={t.x2} y2={t.y2} />
          ))}
        </g>
        <g className="halo-legend" aria-hidden>
          <text>
            <textPath href={`#${path}`} textLength={2 * Math.PI * 104 - 4} lengthAdjust="spacing">
              {legend}
            </textPath>
          </text>
        </g>
        <circle className="halo-ring" r={R_IN} />
        <circle className="halo-ring halo-ring-dash" r={R_OUT} />
        {sats.map(s => (
          <g
            key={s.key}
            ref={el => void (el ? nodes.current.set(s.key, el) : nodes.current.delete(s.key))}
            className={`sat tone-${s.tone}`}
            onClick={() => onOpen(s.key)}
          >
            <title>{s.label}</title>
            {s.tone === 'active' && <path className="sat-tail" d={TAIL(s.ring)} />}
            {s.tone === 'attention' && <circle className="sat-ping" cx={s.ring} r="4" />}
            <circle className="sat-hit" cx={s.ring} r="10" />
            <circle className="sat-dot" cx={s.ring} r="3.4" />
          </g>
        ))}
      </svg>
      <span className="halo-core">
        <Orb mood={mood} size={Math.round(size * 0.48)} />
      </span>
    </div>
  )
}
