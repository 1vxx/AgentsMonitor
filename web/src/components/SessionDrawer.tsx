import { useEffect, useState } from 'react'
import { api } from '../api.ts'
import { clock, compact, dateTime, duration, tildify, useNow } from '../format.ts'
import { onEvent, useLive } from '../store.ts'
import type { EventRecord, Session } from '../types.ts'
import { AgentBadge, Meter, StatusPill, shortModel, shortTool, statusView } from './bits.tsx'
import { describe } from './events.tsx'

const Fact = ({ label, children, mono }: { label: string; children: React.ReactNode; mono?: boolean }) => (
  <div className="fact">
    <dt>{label}</dt>
    <dd className={mono ? 'mono' : undefined}>{children ?? '—'}</dd>
  </div>
)

export const SessionDrawer = ({ sessionKey, onClose }: { sessionKey: string; onClose: () => void }) => {
  const live = useLive(st => st.sessions.get(sessionKey))
  const [stored, setStored] = useState<Session>()
  const [events, setEvents] = useState<EventRecord[]>([])
  const [error, setError] = useState<string>()
  const now = useNow()
  const s = live ?? stored

  useEffect(() => {
    let alive = true
    setEvents([])
    setError(undefined)
    api
      .sessionEvents(sessionKey)
      .then(list => alive && setEvents(prev => mergeEvents(prev, list)))
      .catch(e => alive && setError(String(e)))
    if (!live) api.session(sessionKey).then(x => alive && setStored(x)).catch(() => {})
    const off = onEvent(r => r.key === sessionKey && setEvents(prev => mergeEvents(prev, [r])))
    return () => {
      alive = false
      off()
    }
    // The live copy is read on each render; loading depends on the key alone
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [sessionKey])

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && onClose()
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [onClose])

  const forget = async () => {
    if (!confirm('从监控中心删除这个会话及其历史事件？')) return
    await api.forget(sessionKey).catch(e => setError(String(e)))
    onClose()
  }

  return (
    <div className="drawer-backdrop" onClick={onClose}>
      <aside className="drawer" role="dialog" aria-modal="true" aria-label="会话详情" onClick={e => e.stopPropagation()}>
        {!s ? (
          <div className="drawer-loading">{error ?? '加载中…'}</div>
        ) : (
          <>
            <header className="drawer-head">
              <AgentBadge agent={s.agent} full />
              <h2>{s.meta.project ?? s.session_id.slice(0, 8)}</h2>
              <StatusPill view={statusView(s)} since={s.status_since} now={now} />
              <button className="icon-btn" onClick={onClose} aria-label="关闭">
                ×
              </button>
            </header>

            <div className="drawer-body">
              {s.attention && s.attention.kind !== 'idle' && (
                <div className="callout tone-warning">
                  <strong>{s.attention.kind === 'permission' ? '等待你批准' : '等待你回答'}</strong>
                  {s.attention.tool && <code className="tool">{shortTool(s.attention.tool)}</code>}
                  <span className="mono">{s.attention.subject ?? s.attention.message}</span>
                </div>
              )}

              <section>
                <h4>上下文</h4>
                <Meter
                  value={s.context?.percent}
                  label={s.context?.tokens !== undefined ? `${compact(s.context.tokens)} / ${compact(s.context.window ?? 0)} tokens` : '尚无读数'}
                />
              </section>

              {(s.tools?.length ?? 0) + (s.subagents?.length ?? 0) > 0 && (
                <section>
                  <h4>进行中</h4>
                  <ul className="inflight">
                    {s.tools?.map((t, i) => (
                      <li key={t.id ?? i}>
                        <code className="tool">{shortTool(t.tool)}</code>
                        <span className="mono subject">{t.subject}</span>
                        {t.agent_id && <span className="chip">子代理</span>}
                        <span className="activity-time">{duration(now - t.started_at)}</span>
                      </li>
                    ))}
                    {s.subagents?.map(a => (
                      <li key={a.id}>
                        <span className="chip">子代理</span>
                        <span className="subject">{a.preview ?? a.id}</span>
                        <span className="activity-time">{duration(now - a.started_at)}</span>
                      </li>
                    ))}
                  </ul>
                </section>
              )}

              {s.last_prompt && (
                <section>
                  <h4>最近的提示</h4>
                  <p className="quote">{s.last_prompt}</p>
                </section>
              )}
              {s.last_turn?.preview && (
                <section>
                  <h4>最近的回复</h4>
                  <p className="quote">{s.last_turn.preview}</p>
                </section>
              )}

              <section>
                <h4>
                  统计 <span className="h4-note">{s.stats.exact_tokens ? 'tokens 为逐请求精确计数' : 'tokens 按回合估算（未接入精确计数）'}</span>
                </h4>
                <dl className="facts facts-stats">
                  <Fact label="提示">{s.stats.prompts}</Fact>
                  <Fact label="回合">{s.stats.turns}</Fact>
                  <Fact label="工具调用">{s.stats.tool_calls}</Fact>
                  <Fact label="工具失败">{s.stats.tool_errors}</Fact>
                  <Fact label="子代理">{s.stats.subagent_runs}</Fact>
                  <Fact label="出错回合">{s.stats.errors}</Fact>
                  <Fact label="输入 tokens">{compact(s.stats.input_tokens)}</Fact>
                  <Fact label="输出 tokens">{compact(s.stats.output_tokens)}</Fact>
                  <Fact label="缓存读">{compact(s.stats.cache_read_tokens)}</Fact>
                  <Fact label="缓存写">{compact(s.stats.cache_write_tokens)}</Fact>
                  <Fact label="工作时长">{duration(s.stats.busy_ms)}</Fact>
                  <Fact label="模型请求">{s.stats.exact_tokens ? (s.stats.api_requests ?? 0) : '—'}</Fact>
                </dl>
              </section>

              <section>
                <h4>会话</h4>
                <dl className="facts">
                  <Fact label="目录" mono>
                    {tildify(s.meta.cwd)}
                  </Fact>
                  <Fact label="模型">{shortModel(s.meta.model)}</Fact>
                  <Fact label="权限模式">{s.meta.permission_mode}</Fact>
                  <Fact label="界面">{s.meta.surface}</Fact>
                  <Fact label="版本">{s.meta.agent_version}</Fact>
                  <Fact label="开始">{dateTime(s.started_at)}</Fact>
                  <Fact label="最后信号">{dateTime(s.last_seen)}</Fact>
                  {s.ended_at && <Fact label="结束">{`${dateTime(s.ended_at)} · ${s.end_reason ?? ''}`}</Fact>}
                  <Fact label="会话 ID" mono>
                    {s.session_id}
                  </Fact>
                </dl>
              </section>

              <section>
                <h4>时间线</h4>
                {error && <div className="error">{error}</div>}
                <ol className="timeline">
                  {events.map(r => {
                    const d = describe(r)
                    return (
                      <li key={r.seq} className={`tl tone-${d.tone}`}>
                        <time>{clock(r.ts)}</time>
                        <span className="tl-kind">{d.kind}</span>
                        <span className="tl-text">
                          {d.text}
                          {d.detail && <span className="tl-detail mono">{d.detail}</span>}
                        </span>
                      </li>
                    )
                  })}
                  {events.length === 0 && !error && <li className="tl-empty">还没有事件</li>}
                </ol>
              </section>

              <button className="btn btn-danger" onClick={forget}>
                删除此会话
              </button>
            </div>
          </>
        )}
      </aside>
    </div>
  )
}

const mergeEvents = (prev: EventRecord[], more: EventRecord[]) => {
  const seen = new Set(prev.map(r => r.seq))
  return [...prev, ...more.filter(r => !seen.has(r.seq))].sort((a, b) => b.seq - a.seq).slice(0, 1000)
}
