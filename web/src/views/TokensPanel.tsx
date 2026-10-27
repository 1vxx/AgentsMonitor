import { useEffect, useState } from 'react'
import { motion } from 'motion/react'
import { api } from '../api.ts'
import { compact, dateTime } from '../format.ts'
import { RANGES, type RangeId, useTokenReport, useTokenSeries } from '../tokens.ts'
import { tokenTotal, type TokenReport } from '../types.ts'
import { Seg, shortModel } from '../components/bits.tsx'
import { Numeral, rise } from '../components/motion.tsx'
import { Orb } from '../components/Orb.tsx'
import { MEASURES, type Measure, TokenBars } from '../components/TokenBars.tsx'

const SOURCES: Record<string, string> = {
  repl_main_thread: '主会话',
  sdk: '非交互（-p / SDK）',
  compact: '上下文压缩',
  '': '未标注',
}

const sourceLabel = (key: string) =>
  SOURCES[key] ?? (key.startsWith('agent') ? `子代理 ${key.replace(/^agent[:_]?/, '')}`.trim() : key)

const Stat = ({ label, value, hint, lead }: { label: string; value: number; hint?: string; lead?: boolean }) => (
  <div className={lead ? 'figure figure-lead' : 'figure'} title={value.toLocaleString()}>
    <div className="stat-label">{label}</div>
    <Numeral className="figure-value" value={value} format={n => compact(Math.round(n))} />
    <div className="figure-hint">{hint ?? '\u00a0'}</div>
  </div>
)

const Breakdown = ({ title, report, label }: { title: string; report?: TokenReport; label: (key: string) => string }) => (
  <div className="breakdown">
    <h4 className="sec-title">{title}</h4>
    <div className="table-wrap">
      <table className="table table-compact">
        <thead>
          <tr>
            <th />
            <th className="num">输入</th>
            <th className="num">输出</th>
            <th className="num">缓存读取</th>
            <th className="num">缓存写入</th>
            <th className="num">合计</th>
            <th className="num">请求</th>
          </tr>
        </thead>
        <tbody>
          {report?.groups?.map(g => (
            <tr key={g.key}>
              <td className="cell-title">{label(g.key)}</td>
              <td className="num">{compact(g.input)}</td>
              <td className="num">{compact(g.output)}</td>
              <td className="num">{compact(g.cache_read)}</td>
              <td className="num">{compact(g.cache_write)}</td>
              <td className="num strong">{compact(tokenTotal(g))}</td>
              <td className="num">{g.requests.toLocaleString()}</td>
            </tr>
          ))}
          {!report?.groups?.length && (
            <tr>
              <td colSpan={7} className="muted">
                这段时间没有请求
              </td>
            </tr>
          )}
        </tbody>
      </table>
    </div>
  </div>
)

/** Says why nothing is counted yet: no export arrives, or one arrives that holds no requests. */
const NoCounts = () => {
  const [otlp, setOtlp] = useState<{ exports: number; services: string[] }>()
  useEffect(() => {
    api.stats().then(s => setOtlp(s.otlp)).catch(() => {})
  }, [])
  if (otlp && otlp.exports > 0) {
    return (
      <div className="empty">
        <Orb mood="idle" size={56} />
        <div className="empty-title">收到了用量上报，但还没有模型请求</div>
        <div className="empty-body">
          守护进程已收到 {otlp.exports} 次 OpenTelemetry 导出（服务：{otlp.services.join('、') || '未命名'}），其中还没有{' '}
          <code>api_request</code> 记录。在会话里发一条消息，几秒后这里就会出现计数。
        </div>
      </div>
    )
  }
  return (
    <div className="empty">
      <Orb mood="idle" size={56} />
      <div className="empty-title">还没收到 Claude Code 的用量上报</div>
      <div className="empty-body">
        在 <code>~/.claude/settings.json</code> 的 <code>env</code> 里设置 <code>CLAUDE_CODE_ENABLE_TELEMETRY=1</code>、
        <code>OTEL_LOGS_EXPORTER=otlp</code>、<code>OTEL_EXPORTER_OTLP_LOGS_PROTOCOL=http/json</code>、
        <code>OTEL_EXPORTER_OTLP_LOGS_ENDPOINT=http://127.0.0.1:7788/v1/logs</code>。配置只对之后启动的会话生效：终端里新开
        <code>claude</code>，桌面 App 需要完全退出后重新打开。
      </div>
    </div>
  )
}

export const TokensPanel = ({ now }: { now: number }) => {
  const [rangeId, setRangeId] = useState<RangeId>('today')
  const [measure, setMeasure] = useState<Measure>('total')
  const range = RANGES.find(r => r.id === rangeId) ?? RANGES[0]!
  // Windows move with the minute, not the second: fewer refetches.
  const minute = Math.floor(now / 60_000) * 60_000
  const since = range.since(minute)
  const { data: report, error } = useTokenReport(since)
  const { data: byModel } = useTokenReport(since, 'model')
  const { data: bySource } = useTokenReport(since, 'source')
  const series = useTokenSeries(range.seriesSince(minute), range.bucket)

  return (
    <motion.section className="usage" {...rise(0)}>
      <div className="section-head">
        <div className="section-lead">
          <h2 className="section-title">Tokens</h2>
        </div>
        <Seg label="时间范围" value={rangeId} onChange={setRangeId} options={RANGES} />
      </div>
      {error && <div className="error">{error}</div>}

      {report && report.last_ts === undefined ? (
        <NoCounts />
      ) : (
        <>
          <div className="figures">
            <Stat lead label="合计" value={report ? tokenTotal(report) : 0} />
            <Stat label="输出" value={report?.output ?? 0} />
            <Stat label="输入" value={report?.input ?? 0} hint="未命中缓存的部分" />
            <Stat label="缓存读取" value={report?.cache_read ?? 0} />
            <Stat label="缓存写入" value={report?.cache_write ?? 0} />
            <Stat label="模型请求" value={report?.requests ?? 0} />
          </div>

          <div className="panel">
            <div className="chart-head">
              <Seg size="sm" label="图表指标" value={measure} onChange={setMeasure} options={MEASURES} />
              <span className="section-meta">
                {range.bucket < 86_400_000 ? '每小时' : '每天'}
                {range.chartNote ? ` · ${range.chartNote}` : ''}
              </span>
            </div>
            {series ? <TokenBars series={series} until={now} measure={measure} /> : <span className="skeleton" style={{ height: 220 }} />}
          </div>

          <div className="breakdowns">
            <Breakdown title="按模型" report={byModel} label={k => shortModel(k) ?? (k || '未知')} />
            <Breakdown title="按来源" report={bySource} label={sourceLabel} />
          </div>
          <p className="footnote">
            每次模型请求按 Claude Code 自己上报的用量计数一次（按请求 ID 去重），主会话、子代理、上下文压缩和后台请求都包含在内。
            {report?.last_ts !== undefined && ` 最近一次请求：${dateTime(report.last_ts)}。`}
          </p>
        </>
      )}
    </motion.section>
  )
}
