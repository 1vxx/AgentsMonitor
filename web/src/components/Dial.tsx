import type { ReactNode } from 'react'
import { motion } from 'motion/react'
import { pct } from '../format.ts'
import { fillTone } from './bits.tsx'
import { EASE, Numeral } from './motion.tsx'

// A quota window as an instrument dial: 270° of arc with a tick every 2.5%,
// lit up to the share used. Faint marks sit at the 70% and 90% thresholds; the
// arc turns amber past the first and red past the second.

const R = 38
const START = 135
const SWEEP = 270
const polar = (deg: number, r = R) => {
  const a = (deg * Math.PI) / 180
  return [Math.cos(a) * r, Math.sin(a) * r] as const
}
const [sx, sy] = polar(START)
const [ex, ey] = polar(START + SWEEP)
const ARC = `M${sx.toFixed(2)},${sy.toFixed(2)}A${R},${R} 0 1 1 ${ex.toFixed(2)},${ey.toFixed(2)}`

const TICKS = Array.from({ length: 41 }, (_, i) => {
  const deg = START + (SWEEP * i) / 40
  const len = i % 10 === 0 ? 5 : 2.5
  const [x1, y1] = polar(deg, 43)
  const [x2, y2] = polar(deg, 43 + len)
  return { x1, y1, x2, y2, at: i * 2.5, major: i % 10 === 0 }
})

export const Dial = ({ value, label, detail }: { value: number; label: string; detail?: ReactNode }) => {
  const v = Math.max(0, Math.min(100, value))
  const tone = fillTone(v)
  return (
    <div className={`dial tone-${tone}`}>
      <div className="dial-face">
        <svg viewBox="-50 -50 100 100" role="meter" aria-valuemin={0} aria-valuemax={100} aria-valuenow={Math.round(v)} aria-label={label}>
          <path className="dial-track" d={ARC} />
          {TICKS.map((t, i) => (
            <line
              key={i}
              className={`dial-tick${t.at <= v ? ' on' : ''}${t.major ? ' major' : ''}${t.at === 70 ? ' warn' : t.at === 90 ? ' crit' : ''}`}
              style={{ ['--i' as string]: i }}
              x1={t.x1}
              y1={t.y1}
              x2={t.x2}
              y2={t.y2}
            />
          ))}
          <motion.path
            className="dial-value"
            d={ARC}
            initial={{ pathLength: 0 }}
            animate={{ pathLength: Math.max(0.004, v / 100) }}
            transition={{ duration: 1.4, ease: EASE }}
          />
        </svg>
        <div className="dial-read">
          <Numeral className="dial-number" value={value} format={pct} />
          <span className="dial-label">{label}</span>
        </div>
      </div>
      {detail && <div className="dial-detail">{detail}</div>}
    </div>
  )
}
