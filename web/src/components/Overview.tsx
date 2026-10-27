import { agentInfo, windowLabel } from '../agents.ts'
import { compact, until } from '../format.ts'
import { useLive } from '../store.ts'
import { localMidnight, useTokenReport } from '../tokens.ts'
import { tokenTotal, type Mood } from '../types.ts'
import { AgentBadge, Meter, fillTone } from './bits.tsx'

const MOOD: Record<Mood, { title: string; tone: string }> = {
  attention: { title: '有会话在等你', tone: 'warning' },
  working: { title: '工作中', tone: 'active' },
  error: { title: '有会话出错', tone: 'critical' },
  idle: { title: '全部空闲', tone: 'idle' },
  offline: { title: '没有活动会话', tone: 'muted' },
}

const Tile = ({ label, value, hint, tone, title }: { label: string; value: string | number; hint?: string; tone?: string; title?: string }) => (
  <div className={`tile${tone ? ` tone-${tone}` : ''}`} title={title}>
    <div className="tile-label">{label}</div>
    <div className="tile-value">{value}</div>
    {hint && <div className="tile-hint">{hint}</div>}
  </div>
)

export const Overview = ({ now }: { now: number }) => {
  const summary = useLive(s => s.summary)
  const counts = summary?.counts
  const mood = MOOD[summary?.mood ?? 'offline']
  const working = counts ? counts.thinking + counts.tool + counts.compacting : 0
  const { data: today } = useTokenReport(localMidnight(now))
  const top = summary?.sessions[0]

  return (
    <section className="overview" aria-label="总览">
      <div className={`hero tone-${mood.tone}`}>
        <div className={`orb orb-${summary?.mood ?? 'offline'}`} aria-hidden>
          <div className="orb-core" />
        </div>
        <div className="hero-text">
          <div className="hero-title">{mood.title}</div>
          <div className="hero-sub">
            {summary?.mood === 'attention' && top
              ? `${top.project} · ${agentInfo(top.agent).label}`
              : counts
                ? `${counts.live} 个活动会话`
                : '正在连接守护进程…'}
          </div>
        </div>
      </div>
      <Tile label="活动会话" value={counts?.live ?? 0} hint={counts?.stale ? `${counts.stale} 个无信号` : undefined} />
      <Tile label="工作中" value={working} tone={working ? 'active' : undefined} />
      <Tile label="等待你" value={counts?.waiting ?? 0} tone={counts?.waiting ? 'warning' : undefined} />
      <Tile label="空闲" value={counts?.idle ?? 0} />
      {today?.last_ts === undefined ? (
        <Tile label="今日 Tokens" value="—" hint={today ? '未接入精确计数' : undefined} />
      ) : (
        <Tile
          label="今日 Tokens"
          value={compact(tokenTotal(today))}
          hint={`输出 ${compact(today.output)} · 输入 ${compact(today.input)}`}
          title={`缓存读取 ${today.cache_read.toLocaleString()} · 缓存写入 ${today.cache_write.toLocaleString()} · ${today.requests} 次请求`}
        />
      )}
    </section>
  )
}

export const QuotaStrip = ({ now }: { now: number }) => {
  const quotas = useLive(s => s.quotas)
  if (quotas.size === 0) {
    return (
      <section className="quota-strip">
        <div className="quota-card quota-empty">
          <span className="muted">额度</span>
          <span>暂无读数：订阅账号在会话收到第一条回复后上报 5 小时 / 7 天额度。</span>
        </div>
      </section>
    )
  }
  return (
    <section className="quota-strip" aria-label="额度">
      {[...quotas.values()].map(q => (
        <div className="quota-card" key={q.agent}>
          <div className="quota-head">
            <AgentBadge agent={q.agent} full />
            <span className="muted">额度</span>
          </div>
          <div className="quota-windows">
            {q.windows.map(w => (
              <Meter
                key={w.kind}
                value={w.percent_used}
                label={windowLabel(w.kind)}
                detail={
                  <>
                    {fillTone(w.percent_used) !== 'active' && <span className={`tag tone-${fillTone(w.percent_used)}`}>{w.percent_used >= 90 ? '接近上限' : '偏高'}</span>}
                    {until(w.resets_at, now)}
                  </>
                }
              />
            ))}
          </div>
        </div>
      ))}
    </section>
  )
}
