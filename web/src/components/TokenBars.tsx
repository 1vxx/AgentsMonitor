import { useLayoutEffect, useMemo, useRef, useState } from 'react'
import { compact } from '../format.ts'
import { tokenTotal, type TokenSeries, type TokenTotals } from '../types.ts'

// One bar per bucket for the chosen measure; hovering a bucket shows all of
// its counts. Empty buckets stay as gaps on the axis, never interpolated.

export type Measure = 'total' | 'input' | 'output' | 'cache_read' | 'cache_write'

export const MEASURES: { id: Measure; label: string }[] = [
  { id: 'total', label: '合计' },
  { id: 'output', label: '输出' },
  { id: 'input', label: '输入' },
  { id: 'cache_read', label: '缓存读取' },
  { id: 'cache_write', label: '缓存写入' },
]

const value = (t: TokenTotals, m: Measure) => (m === 'total' ? tokenTotal(t) : t[m])

const HEIGHT = 220
const PAD = { top: 12, right: 12, bottom: 26, left: 52 }
const EMPTY: TokenTotals = { input: 0, output: 0, cache_read: 0, cache_write: 0, requests: 0 }

/** A round axis top: 1, 2 or 5 times a power of ten. */
const niceMax = (v: number) => {
  if (v <= 0) return 1
  const p = 10 ** Math.floor(Math.log10(v))
  return ([1, 2, 5, 10].find(m => m * p >= v) ?? 10) * p
}

const bucketLabel = (t: number, bucket: number) => {
  const d = new Date(t)
  return bucket < 86_400_000
    ? d.toLocaleTimeString('zh-CN', { hour12: false, hour: '2-digit', minute: '2-digit' })
    : d.toLocaleDateString('zh-CN', { month: '2-digit', day: '2-digit' })
}

export const TokenBars = ({ series, until, measure }: { series: TokenSeries; until: number; measure: Measure }) => {
  const box = useRef<HTMLDivElement>(null)
  const [width, setWidth] = useState(720)
  const [hover, setHover] = useState<number>()

  useLayoutEffect(() => {
    const el = box.current
    if (!el) return
    const ro = new ResizeObserver(([entry]) => entry && setWidth(Math.max(300, entry.contentRect.width)))
    ro.observe(el)
    return () => ro.disconnect()
  }, [])

  const buckets = useMemo(() => {
    const { since, bucket_ms: b } = series
    const by = new Map(series.points.map(p => [p.t, p]))
    const n = Math.min(400, Math.max(1, Math.ceil((until - since) / b)))
    return Array.from({ length: n }, (_, i) => {
      const t = since + i * b
      return { t, totals: by.get(t) ?? EMPTY }
    })
  }, [series, until])

  const innerW = width - PAD.left - PAD.right
  const innerH = HEIGHT - PAD.top - PAD.bottom
  const top = niceMax(Math.max(0, ...buckets.map(b => value(b.totals, measure))))
  const slot = innerW / buckets.length
  const barW = Math.max(1, slot - 2)
  const y = (v: number) => PAD.top + innerH - (v / top) * innerH
  const labelEvery = Math.max(1, Math.ceil(buckets.length / Math.max(2, Math.floor(innerW / 70))))
  const h = hover !== undefined ? buckets[hover] : undefined

  return (
    <div className="chart" ref={box}>
      <svg width={width} height={HEIGHT} role="img" aria-label="tokens 随时间分布">
        {[0, 0.5, 1].map(f => (
          <g key={f}>
            <line className={f === 0 ? 'axis' : 'grid'} x1={PAD.left} x2={PAD.left + innerW} y1={y(top * f)} y2={y(top * f)} />
            <text className="tick" x={PAD.left - 8} y={y(top * f)} dy="0.32em" textAnchor="end">
              {compact(top * f)}
            </text>
          </g>
        ))}
        {/* Keyed by what is shown, so the bars grow again when it changes */}
        <g key={`${measure}:${series.since}:${series.bucket_ms}`}>
          {buckets.map((b, i) => {
            const v = value(b.totals, measure)
            const x = PAD.left + i * slot + (slot - barW) / 2
            const bh = Math.max(0, PAD.top + innerH - y(v))
            const r = Math.min(3, barW / 2, bh)
            return (
              <g key={b.t}>
                {v > 0 && (
                  <path
                    className={`bar${hover === i ? ' bar-on' : ''}${hover !== undefined && hover !== i ? ' bar-dim' : ''}`}
                    style={{ ['--i' as string]: Math.min(i, 60) }}
                    d={`M${x},${PAD.top + innerH}V${PAD.top + innerH - bh + r}q0,-${r} ${r},-${r}H${x + barW - r}q${r},0 ${r},${r}V${PAD.top + innerH}Z`}
                  />
                )}
                {i % labelEvery === 0 && (
                  <text className="tick" x={x + barW / 2} y={HEIGHT - 8} textAnchor="middle">
                    {bucketLabel(b.t, series.bucket_ms)}
                  </text>
                )}
                <rect
                  x={PAD.left + i * slot}
                  y={PAD.top}
                  width={slot}
                  height={innerH}
                  fill="transparent"
                  onPointerEnter={() => setHover(i)}
                  onPointerLeave={() => setHover(undefined)}
                />
              </g>
            )
          })}
        </g>
      </svg>
      {h && hover !== undefined && (
        <div
          className="tooltip"
          style={{
            left: PAD.left + hover * slot + slot / 2 + (hover > buckets.length / 2 ? -12 : 12),
            top: PAD.top,
            transform: hover > buckets.length / 2 ? 'translateX(-100%)' : undefined,
          }}
        >
          <div className="tooltip-title">
            {new Date(h.t).toLocaleString('zh-CN', { hour12: false, month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' })}
          </div>
          {MEASURES.map(m => (
            <div key={m.id} className="tooltip-row">
              <span>{m.label}</span>
              <strong>{value(h.totals, m.id).toLocaleString()}</strong>
            </div>
          ))}
          <div className="tooltip-row">
            <span>请求</span>
            <strong>{h.totals.requests}</strong>
          </div>
        </div>
      )}
    </div>
  )
}
