import { useEffect, useLayoutEffect, useRef } from 'react'
import { animate, useReducedMotion } from 'motion/react'

// The house motion: one easing for arrivals, one for strokes being drawn.
export const EASE = [0.22, 1, 0.36, 1] as const
export const EASE_IN_OUT = [0.65, 0, 0.35, 1] as const

/** Rises into focus; `i` staggers siblings, capped so long lists never wait. */
export const rise = (i = 0, step = 0.045) => ({
  initial: { opacity: 0, y: 12, filter: 'blur(6px)' },
  animate: { opacity: 1, y: 0, filter: 'blur(0px)' },
  transition: { duration: 0.7, ease: EASE, delay: Math.min(i, 12) * step },
})

/**
 * A number that rolls to its new value instead of jumping. It counts up from
 * zero when it first appears; `format` turns the moving value into text.
 */
export const Numeral = ({ value, format = n => String(Math.round(n)), className }: { value: number; format?: (n: number) => string; className?: string }) => {
  const ref = useRef<HTMLSpanElement>(null)
  const shown = useRef(0)
  const fmt = useRef(format)
  fmt.current = format
  const reduce = useReducedMotion()

  useLayoutEffect(() => {
    if (ref.current && !ref.current.textContent) ref.current.textContent = fmt.current(reduce ? value : 0)
  })

  useEffect(() => {
    const el = ref.current
    if (!el) return
    const from = shown.current
    const write = (v: number) => {
      shown.current = v
      el.textContent = fmt.current(Number.isInteger(value) ? Math.round(v) : v)
    }
    if (reduce || from === value) {
      write(value)
      return
    }
    const controls = animate(from, value, { duration: from === 0 ? 1.3 : 0.8, ease: EASE, onUpdate: write })
    return () => controls.stop()
  }, [value, reduce])

  return <span ref={ref} className={className} />
}
