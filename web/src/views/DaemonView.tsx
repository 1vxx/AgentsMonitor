import { useCallback, useEffect, useState, type ReactNode } from 'react'
import { AnimatePresence, motion } from 'motion/react'
import { api } from '../api.ts'
import { bytes, compact, dateTime, duration } from '../format.ts'
import { useLive } from '../store.ts'
import type { DaemonStats, MaintenanceReport } from '../types.ts'
import { Numeral, rise } from '../components/motion.tsx'

const Stat = ({ label, value, i }: { label: string; value: ReactNode; i: number }) => (
  <motion.div className="fact" {...rise(i, 0.03)}>
    <dt>{label}</dt>
    <dd>{value}</dd>
  </motion.div>
)

const Count = ({ v }: { v: number }) => <Numeral value={v} format={n => compact(Math.round(n))} />

const summarize = (r: MaintenanceReport) => {
  const parts: string[] = []
  if (r.forgotten > 0) {
    const why = [
      r.too_old && `${r.too_old} 个超过保留期`,
      r.transcript_gone && `${r.transcript_gone} 个已在 Claude 中删除`,
      r.ended && `${r.ended} 个已结束`,
    ].filter(Boolean)
    parts.push(`删除了 ${r.forgotten} 个会话（${why.join('，')}）`)
  }
  if (r.archived > 0) parts.push(`${r.archived} 个已归档的会话移出了实时视图`)
  return parts.length ? parts.join('；') : '没有需要清理的会话'
}

/** Runs the cleanup now, or forgets every ended session (archived ones included). */
const Cleanup = ({ ended, onDone }: { ended: number; onDone: () => void }) => {
  const [busy, setBusy] = useState<'retention' | 'all'>()
  const [report, setReport] = useState<MaintenanceReport>()
  const [error, setError] = useState<string>()

  const run = async (all: boolean) => {
    if (all && !confirm(`删除全部 ${ended} 个已结束的会话及其事件？进行中的会话不受影响，token 用量统计保留。此操作不能撤销。`)) return
    setBusy(all ? 'all' : 'retention')
    setError(undefined)
    try {
      setReport(await api.maintenance(all))
      onDone()
    } catch (e) {
      setError(String(e))
    } finally {
      setBusy(undefined)
    }
  }

  return (
    <div className="cleanup">
      <div className="cleanup-actions">
        <button className="btn btn-primary" disabled={busy !== undefined} onClick={() => void run(false)}>
          {busy === 'retention' ? '清理中…' : '立即清理'}
        </button>
        <button className="btn btn-danger" disabled={busy !== undefined || ended === 0} onClick={() => void run(true)}>
          {busy === 'all' ? '删除中…' : `删除全部已结束会话（${ended}）`}
        </button>
      </div>
      <div className="footnote">
        立即清理：马上按上面的规则执行一次（同步删除和归档、清理过期数据），并整理数据库。删除全部已结束会话：把所有已结束的会话连同历史一起删掉，只保留进行中的会话。
      </div>
      {error && <div className="error">{error}</div>}
      <AnimatePresence>
        {report && (
          <motion.div
            key={report.duration_ms + report.db_bytes_after}
            className="cleanup-result"
            role="status"
            initial={{ opacity: 0, y: -6 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0 }}
          >
            {summarize(report)}；数据库 {bytes(report.db_bytes_before)} → {bytes(report.db_bytes_after)}，用时 {report.duration_ms} ms
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  )
}

export const DaemonView = () => {
  const server = useLive(s => s.server)
  const conn = useLive(s => s.conn)
  const [stats, setStats] = useState<DaemonStats>()
  const [error, setError] = useState<string>()

  const refresh = useCallback(
    () =>
      api
        .stats()
        .then(s => (setStats(s), setError(undefined)))
        .catch(e => setError(String(e))),
    [],
  )

  useEffect(() => {
    void refresh()
    const id = setInterval(refresh, 3000)
    return () => clearInterval(id)
  }, [refresh])

  return (
    <div className="daemon-view">
      <motion.div className="section-head" {...rise(0)}>
        <div className="section-lead">
          <span className="kicker">
            <b>01</b> Daemon
          </span>
          <h2 className="section-title">守护进程</h2>
        </div>
        <span className="section-meta">
          {server ? `${server.name} ${server.version} · 协议 v${server.protocol} · 启动于 ${dateTime(server.started_at)}` : conn}
        </span>
      </motion.div>
      {error && <div className="error">{error}</div>}
      {stats ? (
        <dl className="grid-stats grid-stats-lg">
          <Stat i={0} label="运行时长" value={duration(stats.uptime_ms)} />
          <Stat i={1} label="常驻内存" value={bytes(stats.rss_bytes)} />
          <Stat i={2} label="累计 CPU" value={stats.cpu_ms !== undefined ? `${(stats.cpu_ms / 1000).toFixed(2)}s` : '—'} />
          <Stat i={3} label="接收事件" value={<Count v={stats.events_in} />} />
          <Stat i={4} label="接收批次" value={<Count v={stats.batches_in} />} />
          <Stat i={5} label="拒收" value={<Count v={stats.rejected} />} />
          <Stat i={6} label="WebSocket 客户端" value={<Count v={stats.ws_clients} />} />
          <Stat i={7} label="活动 / 内存中会话" value={`${stats.sessions_live} / ${stats.sessions_in_memory}`} />
          <Stat i={8} label="库中会话" value={<Count v={stats.db?.sessions ?? 0} />} />
          <Stat i={9} label="库中事件" value={<Count v={stats.db?.events ?? 0} />} />
          <Stat i={10} label="额度读数" value={<Count v={stats.db?.quota_samples ?? 0} />} />
          <Stat i={11} label="已计数模型请求" value={<Count v={stats.db?.token_requests ?? 0} />} />
          <Stat i={12} label="重复投递（已去重）" value={<Count v={stats.usage_duplicates ?? 0} />} />
          <Stat i={13} label="数据库大小" value={bytes(stats.db_bytes)} />
        </dl>
      ) : (
        !error && <span className="skeleton" style={{ height: 260 }} />
      )}
      {stats?.retention && (
        <motion.section className="panel retention" {...rise(6)}>
          <div className="section-lead">
            <span className="kicker">
              <b>02</b> Retention
            </span>
            <h3 className="section-title section-title-sm">数据保留</h3>
          </div>
          <ul>
            <li>
              已结束的会话及其事件保留 {stats.retention.sessions_days} 天
              {stats.retention.follow_transcripts && '；在 Claude Code 里删除的会话（App 里删除，或自动清理）15 秒内同步删除'}
            </li>
            <li>在 Claude 桌面 App 里归档的会话 15 秒内移出实时视图（含"最近结束"），历史页仍可查看；取消归档后继续使用会带着原来的统计回来</li>
            <li>事件历史 {stats.retention.events_days} 天，额度读数 {stats.retention.quota_days} 天</li>
            <li>
              Token：逐请求保留 {stats.retention.token_detail_days} 天，之后按小时汇总保留到 {stats.retention.token_hourly_days} 天，再按天汇总保留到{' '}
              {Math.round(stats.retention.token_daily_days / 365)} 年；每一步合计都不变
            </li>
            <li>每 10 分钟自动清理一次，删除后空间立即归还磁盘</li>
          </ul>
          <Cleanup ended={Math.max(0, (stats.db?.sessions ?? 0) - stats.sessions_live)} onDone={() => void refresh()} />
        </motion.section>
      )}
    </div>
  )
}
