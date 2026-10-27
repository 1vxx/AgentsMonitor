import { useLayoutEffect, useMemo, useRef, useState } from 'react'
import { motion } from 'motion/react'
import { windowLabel } from '../agents.ts'
import { dateTime, pct } from '../format.ts'
import type { QuotaSample } from '../types.ts'
import { EASE_IN_OUT } from './motion.tsx'

// A step line per rate-limit window: a reading holds until the next one.
// One y-axis (percent used, 0-100), a crosshair with every window's value at
// that moment, a legend plus a direct label at each line's end.

const SLOT: Record<string, number> = { five_hour: 1, seven_day: 2, seven_day_opus: 3, seven_day_sonnet: 4, spend_limit: 5, primary: 1, secondary: 2 }
const color = (kind: string, i: number) => `var(--series-${SLOT[kind] ?? Math.min(i + 3, 8)})`

const HEIGHT = 260
const PAD = { top: 16, right: 92, bottom: 28, left: 40 }

interface Series {
  kind: string
  color: string
  points: { ts: number; v: number }[]
}

const valueAt = (s: Series, t: number) => {
  let v: number | undefined
  for (const p of s.points) {
    if (p.ts > t) break
    v = p.v
  }
  return v
}

export const QuotaChart = ({ samples, from, to }: { samples: QuotaSample[]; from: number; to: number }) => {
  const box = useRef<HTMLDivElement>(null)
  const [width, setWidth] = useState(720)
  const [hover, setHover] = useState<number>()

  useLayoutEffect(() => {
    const el = box.current
    if (!el) return
    const ro = new ResizeObserver(([entry]) => entry && setWidth(Math.max(320, entry.contentRect.width)))
    ro.observe(el)
    return () => ro.disconnect()
  }, [])

  const series = useMemo<Series[]>(() => {
    const byKind = new Map<string, Series>()
    for (const s of samples) {
      let entry = byKind.get(s.kind)
      if (!entry) {
        entry = { kind: s.kind, color: color(s.kind, byKind.size), points: [] }
        byKind.set(s.kind, entry)
      }
      entry.points.push({ ts: s.ts, v: s.percent_used })
    }
    return [...byKind.values()].sort((a, b) => (SLOT[a.kind] ?? 9) - (SLOT[b.kind] ?? 9))
  }, [samples])

  const innerW = width - PAD.left - PAD.right
  const innerH = HEIGHT - PAD.top - PAD.bottom
  const yMax = Math.max(100, ...samples.map(s => s.percent_used))
  const x = (t: number) => PAD.left + ((t - from) / (to - from)) * innerW
  const y = (v: number) => PAD.top + innerH - (v / yMax) * innerH

  const path = (s: Series) => {
    const pts = s.points.filter(p => p.ts <= to)
    if (pts.length === 0) return ''
    // Start from the last reading before the window, if any
    const before = pts.filter(p => p.ts < from).at(-1)
    const inside = pts.filter(p => p.ts >= from)
    const seq = before ? [{ ts: from, v: before.v }, ...inside] : inside
    if (seq.length === 0) return ''
    let d = `M${x(seq[0]!.ts).toFixed(1)},${y(seq[0]!.v).toFixed(1)}`
    for (let i = 1; i < seq.length; i++) {
      d += `H${x(seq[i]!.ts).toFixed(1)}V${y(seq[i]!.v).toFixed(1)}`
    }
    return d + `H${x(to).toFixed(1)}`
  }

  const ticks = useMemo(() => {
    const n = Math.max(2, Math.floor(innerW / 140))
    return Array.from({ length: n + 1 }, (_, i) => from + ((to - from) * i) / n)
  }, [from, to, innerW])

  const onMove = (e: React.PointerEvent<SVGRectElement>) => {
    const rect = e.currentTarget.getBoundingClientRect()
    const t = from + ((e.clientX - rect.left) / rect.width) * (to - from)
    setHover(Math.max(from, Math.min(to, t)))
  }

  const tip = hover !== undefined ? series.map(s => ({ s, v: valueAt(s, hover) })).filter(r => r.v !== undefined) : []
  const tipLeft = hover !== undefined && x(hover) > width - 220

  // Direct labels at the right end, nudged apart so they never collide
  const ends = series
    .map(s => ({ s, v: valueAt(s, to) }))
    .filter((r): r is { s: Series; v: number } => r.v !== undefined)
    .sort((a, b) => b.v - a.v)
  let lastY = -Infinity
  const labels = ends.map(r => {
    const yy = Math.max(y(r.v), lastY + 14)
    lastY = yy
    return { ...r, yy }
  })

  return (
    <div className="chart" ref={box}>
      {series.length > 1 && (
        <div className="legend">
          {series.map(s => (
            <span key={s.kind} className="legend-item">
              <span className="swatch" style={{ background: s.color }} />
              {windowLabel(s.kind)}
            </span>
          ))}
        </div>
      )}
      <svg width={width} height={HEIGHT} role="img" aria-label="额度使用随时间变化">
        {[0, 25, 50, 75, 100].map(v => (
          <g key={v}>
            <line className={v === 0 ? 'axis' : 'grid'} x1={PAD.left} x2={PAD.left + innerW} y1={y(v)} y2={y(v)} />
            <text className="tick" x={PAD.left - 8} y={y(v)} dy="0.32em" textAnchor="end">
              {v}%
            </text>
          </g>
        ))}
        {ticks.map(t => (
          <text key={t} className="tick" x={x(t)} y={HEIGHT - 8} textAnchor="middle">
            {dateTime(t)}
          </text>
        ))}
        {/* Each line is drawn like a stroke of the brush when the range changes */}
        {series.map((s, i) => (
          <motion.path
            key={`${s.kind}:${to - from}`}
            d={path(s)}
            fill="none"
            stroke={s.color}
            strokeWidth={1.75}
            strokeLinejoin="round"
            initial={{ pathLength: 0 }}
            animate={{ pathLength: 1 }}
            transition={{ duration: 1.4, ease: EASE_IN_OUT, delay: i * 0.12 }}
          />
        ))}
        {labels.map(({ s, v, yy }) => (
          <text key={s.kind} className="end-label" x={PAD.left + innerW + 8} y={yy} dy="0.32em">
            {windowLabel(s.kind)} {pct(v)}
          </text>
        ))}
        {hover !== undefined && (
          <g pointerEvents="none">
            <line className="crosshair" x1={x(hover)} x2={x(hover)} y1={PAD.top} y2={PAD.top + innerH} />
            {tip.map(({ s, v }) => (
              <circle key={s.kind} cx={x(hover)} cy={y(v!)} r={4} fill={s.color} stroke="var(--surface-1)" strokeWidth={2} />
            ))}
          </g>
        )}
        <rect
          x={PAD.left}
          y={PAD.top}
          width={innerW}
          height={innerH}
          fill="transparent"
          onPointerMove={onMove}
          onPointerLeave={() => setHover(undefined)}
        />
      </svg>
      {hover !== undefined && tip.length > 0 && (
        <div className="tooltip" style={{ left: tipLeft ? x(hover) - 12 : x(hover) + 12, top: PAD.top, transform: tipLeft ? 'translateX(-100%)' : undefined }}>
          <div className="tooltip-title">{dateTime(hover)}</div>
          {tip.map(({ s, v }) => (
            <div key={s.kind} className="tooltip-row">
              <span className="swatch" style={{ background: s.color }} />
              <span>{windowLabel(s.kind)}</span>
              <strong>{pct(v)}</strong>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
