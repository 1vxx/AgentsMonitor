import { useEffect, useState } from 'react'
import { motion } from 'motion/react'
import { api } from '../api.ts'
import { compact, dateTime } from '../format.ts'
import { dateLocale, has, rich, t, useT } from '../i18n/index.ts'
import { RANGES, type RangeId, useTokenReport, useTokenSeries } from '../tokens.ts'
import { tokenTotal, type TokenReport } from '../types.ts'
import { Seg, shortModel } from '../components/bits.tsx'
import { Numeral, rise } from '../components/motion.tsx'
import { Orb } from '../components/Orb.tsx'
import { MEASURES, type Measure, TokenBars } from '../components/TokenBars.tsx'

const sourceLabel = (key: string) => {
  if (!key) return t('src.none')
  const known = `src.${key}`
  if (has(known)) return t(known)
  return key.startsWith('agent') ? t('src.agent', { name: key.replace(/^agent[:_]?/, '') }).trim() : key
}

const Stat = ({ label, value, hint, lead }: { label: string; value: number; hint?: string; lead?: boolean }) => (
  <div className={lead ? 'figure figure-lead' : 'figure'} title={value.toLocaleString(dateLocale())}>
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
            <th className="num">{t('measure.input')}</th>
            <th className="num">{t('measure.output')}</th>
            <th className="num">{t('measure.cache_read')}</th>
            <th className="num">{t('measure.cache_write')}</th>
            <th className="num">{t('measure.total')}</th>
            <th className="num">{t('chart.requests')}</th>
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
              <td className="num">{g.requests.toLocaleString(dateLocale())}</td>
            </tr>
          ))}
          {!report?.groups?.length && (
            <tr>
              <td colSpan={7} className="muted">
                {t('tok.noRequests')}
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
  useT()
  useEffect(() => {
    api.stats().then(s => setOtlp(s.otlp)).catch(() => {})
  }, [])
  if (otlp && otlp.exports > 0) {
    return (
      <div className="empty">
        <Orb mood="idle" size={56} />
        <div className="empty-title">{t('tok.gotExports')}</div>
        <div className="empty-body">{rich(t('tok.gotExportsBody', { n: otlp.exports, services: otlp.services.join(t('list.sep')) || t('tok.unnamed') }))}</div>
      </div>
    )
  }
  return (
    <div className="empty">
      <Orb mood="idle" size={56} />
      <div className="empty-title">{t('tok.noExports')}</div>
      <div className="empty-body">{rich(t('tok.noExportsBody'))}</div>
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
  useT()

  return (
    <motion.section className="usage" {...rise(0)}>
      <div className="section-head">
        <div className="section-lead">
          <span className="kicker">
            <b>01</b> Σ usage
          </span>
          <h2 className="section-title">Tokens</h2>
        </div>
        <Seg label={t('tok.range')} value={rangeId} onChange={setRangeId} options={RANGES.map(r => ({ id: r.id, label: t(`range.${r.id}`) }))} />
      </div>
      {error && <div className="error">{error}</div>}

      {report && report.last_ts === undefined ? (
        <NoCounts />
      ) : (
        <>
          <div className="figures">
            <Stat lead label={t('measure.total')} value={report ? tokenTotal(report) : 0} />
            <Stat label={t('measure.output')} value={report?.output ?? 0} />
            <Stat label={t('measure.input')} value={report?.input ?? 0} hint={t('tok.inputHint')} />
            <Stat label={t('measure.cache_read')} value={report?.cache_read ?? 0} />
            <Stat label={t('measure.cache_write')} value={report?.cache_write ?? 0} />
            <Stat label={t('fact.requests')} value={report?.requests ?? 0} />
          </div>

          <div className="panel">
            <div className="chart-head">
              <Seg size="sm" label={t('tok.measure')} value={measure} onChange={setMeasure} options={MEASURES.map(m => ({ id: m, label: t(`measure.${m}`) }))} />
              <span className="section-meta">
                {t(range.bucket < 86_400_000 ? 'tok.hourly' : 'tok.daily')}
                {range.chartNote ? ` · ${t(range.chartNote)}` : ''}
              </span>
            </div>
            {series ? <TokenBars series={series} until={now} measure={measure} /> : <span className="skeleton" style={{ height: 220 }} />}
          </div>

          <div className="breakdowns">
            <Breakdown title={t('tok.byModel')} report={byModel} label={k => shortModel(k) ?? (k || t('tok.unknown'))} />
            <Breakdown title={t('tok.bySource')} report={bySource} label={sourceLabel} />
          </div>
          <p className="footnote">
            {t('tok.footnote')}
            {report?.last_ts !== undefined && t('tok.lastRequest', { t: dateTime(report.last_ts) })}
          </p>
        </>
      )}
    </motion.section>
  )
}
