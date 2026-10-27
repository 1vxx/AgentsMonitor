import { AnimatePresence, motion } from 'motion/react'
import type { ReactNode } from 'react'
import { agentInfo, windowLabel } from '../agents.ts'
import { compact, until } from '../format.ts'
import { useLive } from '../store.ts'
import { localMidnight, useTokenReport } from '../tokens.ts'
import { tokenTotal, type Summary } from '../types.ts'
import { AgentBadge, fillTone, type Tone } from './bits.tsx'
import { Dial } from './Dial.tsx'
import { Halo } from './Halo.tsx'
import { EASE, Numeral, rise } from './motion.tsx'

/** The cover line: what is happening, in a sentence; the key words take the mood's colours. */
const headline = (summary: Summary | undefined, conn: string): { title: ReactNode; key: string; sub: string } => {
  if (!summary) return { key: 'connecting', title: conn === 'closed' ? <>守护进程<em>未连接</em></> : <>正在<em>连接</em></>, sub: '守护进程就绪后页面会自动重连' }
  const c = summary.counts
  const working = c.thinking + c.tool + c.compacting
  const top = summary.sessions[0]
  switch (summary.mood) {
    case 'attention':
      return {
        key: `attention-${c.waiting}`,
        title: c.waiting > 1 ? <>{c.waiting} 个会话<em>在等你</em></> : <>有会话<em>在等你</em></>,
        sub: top ? `${top.project} · ${agentInfo(top.agent).label}` : '',
      }
    case 'working':
      return { key: `working-${working}`, title: <>{working} 个会话<em>正在工作</em></>, sub: `共 ${c.live} 个活动会话${c.idle ? `，${c.idle} 个空闲` : ''}` }
    case 'error':
      return { key: 'error', title: <>有会话<em>出错了</em></>, sub: top ? `${top.project} · ${agentInfo(top.agent).label}` : '' }
    case 'idle':
      return { key: 'idle', title: <>一切<em>就绪</em></>, sub: `${c.live} 个会话空闲，等待新的指令` }
    default:
      return { key: 'offline', title: <>暂无<em>活动会话</em></>, sub: '启动一个 Claude Code 会话，它会出现在这里' }
  }
}

/** A row of pips, one lit per unit up to twelve: a count you can read at a glance. */
const Pips = ({ n }: { n: number }) => (
  <span className="pips" aria-hidden>
    {Array.from({ length: 12 }, (_, i) => (
      <i key={i} className={i < n ? 'on' : undefined} style={{ ['--i' as string]: i }} />
    ))}
    {n > 12 && <b>+{n - 12}</b>}
  </span>
)

const Stat = ({ label, code, value, hint, tone, title, pips }: { label: string; code: string; value: ReactNode; hint?: string; tone?: Tone; title?: string; pips?: number }) => (
  <div className={`stat${tone ? ` tone-${tone}` : ''}`} title={title}>
    <dt className="stat-label">
      {label}
      <span className="stat-code">{code}</span>
    </dt>
    <dd className="stat-value">{value}</dd>
    <dd className="stat-hint">{pips !== undefined ? <Pips n={pips} /> : (hint ?? ' ')}</dd>
  </div>
)

const pad = (n: number) => String(n).padStart(2, '0')
const stamp = (now: number) => {
  const d = new Date(now)
  const week = d.toLocaleDateString('en-US', { weekday: 'short' }).toUpperCase()
  return `${d.getFullYear()}.${pad(d.getMonth() + 1)}.${pad(d.getDate())} ${week} · ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
}

/** Each account's rate-limit windows as dials, beside the cover line. */
const QuotaDials = ({ now }: { now: number }) => {
  const quotas = useLive(s => s.quotas)
  if (quotas.size === 0) {
    return (
      <motion.div className="cover-quota cover-quota-empty" {...rise(2)}>
        <span className="kicker">Quota</span>
        <span>暂无额度读数：订阅账号在会话收到第一条回复后上报 5 小时 / 7 天额度。</span>
      </motion.div>
    )
  }
  return (
    <div className="cover-quota" aria-label="额度">
      {[...quotas.values()].map((q, i) => (
        <motion.div className="dial-group" key={q.agent} {...rise(i + 2)}>
          <div className="dial-group-head">
            <span className="kicker">Quota</span>
            <AgentBadge agent={q.agent} full />
          </div>
          <div className="dials">
            {q.windows.map(w => {
              const tone = fillTone(w.percent_used)
              return (
                <Dial
                  key={w.kind}
                  value={w.percent_used}
                  label={windowLabel(w.kind)}
                  detail={
                    <>
                      {tone !== 'active' && <span className={`tag tone-${tone}`}>{w.percent_used >= 90 ? '接近上限' : '偏高'}</span>}
                      {until(w.resets_at, now)}
                    </>
                  }
                />
              )
            })}
          </div>
        </motion.div>
      ))}
    </div>
  )
}

export const Overview = ({ now, onOpen }: { now: number; onOpen: (key: string) => void }) => {
  const summary = useLive(s => s.summary)
  const sessions = useLive(s => s.sessions)
  const conn = useLive(s => s.conn)
  const counts = summary?.counts
  const mood = summary?.mood ?? 'offline'
  const working = counts ? counts.thinking + counts.tool + counts.compacting : 0
  const { data: today } = useTokenReport(localMidnight(now))
  const head = headline(summary, conn)

  return (
    <section className={`cover mood-${mood}`} aria-label="总览">
      <div className="cover-main">
        <motion.div
          className="cover-orb"
          initial={{ opacity: 0, scale: 0.7, rotate: -40, filter: 'blur(14px)' }}
          animate={{ opacity: 1, scale: 1, rotate: 0, filter: 'blur(0px)' }}
          transition={{ duration: 1.4, ease: EASE }}
        >
          <Halo mood={mood} sessions={sessions} onOpen={onOpen} />
        </motion.div>
        <div className="cover-text">
          <motion.div className="cover-date" {...rise(0)}>
            <span className={`live-dot conn-${conn}`} aria-hidden />
            {stamp(now)}
          </motion.div>
          <AnimatePresence mode="wait" initial={false}>
            <motion.div
              key={head.key}
              initial={{ opacity: 0, y: 14, filter: 'blur(8px)' }}
              animate={{ opacity: 1, y: 0, filter: 'blur(0px)' }}
              exit={{ opacity: 0, y: -8, filter: 'blur(6px)', transition: { duration: 0.2 } }}
              transition={{ duration: 0.7, ease: EASE }}
            >
              <h1 className="cover-title">{head.title}</h1>
              <p className="cover-sub">{head.sub}</p>
            </motion.div>
          </AnimatePresence>
        </div>
        <QuotaDials now={now} />
      </div>
      <motion.dl className="stats" {...rise(3)}>
        <Stat label="活动会话" code="LIVE" value={<Numeral value={counts?.live ?? 0} />} pips={counts?.stale ? undefined : (counts?.live ?? 0)} hint={counts?.stale ? `${counts.stale} 个无信号` : undefined} />
        <Stat label="工作中" code="ACTV" value={<Numeral value={working} />} tone={working ? 'active' : undefined} pips={working} />
        <Stat label="等待你" code="WAIT" value={<Numeral value={counts?.waiting ?? 0} />} tone={counts?.waiting ? 'attention' : undefined} pips={counts?.waiting ?? 0} />
        <Stat label="空闲" code="IDLE" value={<Numeral value={counts?.idle ?? 0} />} pips={counts?.idle ?? 0} />
        {today?.last_ts === undefined ? (
          <Stat label="今日 Tokens" code="Σ TODAY" value="—" hint={today ? '未接入精确计数' : undefined} />
        ) : (
          <Stat
            label="今日 Tokens"
            code="Σ TODAY"
            value={<Numeral value={tokenTotal(today)} format={n => compact(Math.round(n))} />}
            hint={`输出 ${compact(today.output)} · 输入 ${compact(today.input)}`}
            title={`缓存读取 ${today.cache_read.toLocaleString()} · 缓存写入 ${today.cache_write.toLocaleString()} · ${today.requests} 次请求`}
          />
        )}
      </motion.dl>
    </section>
  )
}
