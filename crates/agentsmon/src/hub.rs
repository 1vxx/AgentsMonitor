//! The daemon's state: every session, the quotas, the event sequence, and the
//! broadcast that pushes their changes to WebSocket clients.
//!
//! One lock guards it all. Ingest holds it for a reducer step per event and
//! serializes each change once, while holding it; clients share the bytes.

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet, VecDeque};
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};

use agentsmon_protocol::{
    AttentionKind, ContextUsage, Envelope, Event, EventRecord, PROTOCOL_VERSION, Quota, QuotaSample, RateWindow,
    ServerInfo, Session, SessionMeta, Snapshot, Status, Summary, TokenTotals, UsageRecord,
};
use axum::extract::ws::Utf8Bytes;
use parking_lot::Mutex;
use serde::Serialize;
use tokio::sync::broadcast;

use crate::adapters;
use crate::config::{LiveView, Retention};
use crate::janitor;
use crate::reducer;
use crate::store::{self, Op, Store};
use crate::summary;
use crate::util::{now_ms, project_name};

/// One message for WebSocket clients, serialized once.
#[derive(Clone)]
pub struct Outbound {
    /// Part of the event feed, which only clients that asked for it get.
    pub feed: bool,
    pub text: Utf8Bytes,
}

/// The wire form of `ServerMsg`, borrowing what it sends.
#[derive(Serialize)]
#[serde(tag = "type")]
enum Msg<'a> {
    #[serde(rename = "snapshot")]
    Snapshot(&'a Snapshot),
    #[serde(rename = "session")]
    Session { session: &'a Session },
    #[serde(rename = "session.removed")]
    SessionRemoved { key: &'a str },
    #[serde(rename = "quota")]
    Quota { quota: &'a Quota },
    #[serde(rename = "event")]
    Event { record: &'a EventRecord },
    #[serde(rename = "summary")]
    Summary { summary: &'a Summary },
    #[serde(rename = "tokens")]
    Tokens { ts: i64, added: &'a TokenTotals },
}

impl Msg<'_> {
    fn out(&self) -> Outbound {
        Outbound {
            feed: matches!(self, Msg::Event { .. }),
            text: serde_json::to_string(self).unwrap_or_default().into(),
        }
    }
}

#[derive(Default, Serialize)]
pub struct Metrics {
    pub events_in: AtomicU64,
    pub batches_in: AtomicU64,
    pub rejected: AtomicU64,
    pub usage_in: AtomicU64,
    /// OTLP logs exports received, counted or not.
    pub otlp_exports: AtomicU64,
    pub usage_duplicates: AtomicU64,
    pub ws_clients: AtomicUsize,
}

pub struct Presence {
    pub key: String,
    pub agent: String,
    pub session_id: String,
    pub ended: bool,
    pub transcript: Option<String>,
}

/// What a collector read about a session, for [`Hub::observe`].
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Observed {
    pub meta: Option<SessionMeta>,
    pub context: Option<ContextUsage>,
    pub rate_limits: Option<Vec<RateWindow>>,
}

#[derive(Debug, Default, Serialize)]
pub struct IngestReport {
    pub accepted: usize,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub rejected: Vec<String>,
}

struct State {
    sessions: HashMap<String, Session>,
    quotas: BTreeMap<String, Quota>,
    /// When each session last sent a heartbeat; only those go stale.
    heartbeats: HashMap<String, i64>,
    seq: u64,
    summary: Summary,
    last_sweep: i64,
    /// No session is judged by its silence before this: the machine slept.
    grace_until: i64,
    /// Request ids counted lately, with when, oldest first, so a repeated
    /// delivery of one counts once in its session. (The ledger's own key
    /// keeps it once there whenever it comes.)
    seen: HashSet<String>,
    seen_order: VecDeque<(i64, String)>,
    /// Exact token counts for sessions the daemon has not heard of yet.
    pending_usage: HashMap<String, TokenTotals>,
    /// Sessions forgotten lately, and when: their stragglers (a last
    /// heartbeat, an end) must not bring them back. A new start may.
    forgotten: HashMap<String, i64>,
    /// Running sessions out of the live view (idle too long, or archived):
    /// only work brings one back. Heartbeats only keep it from going lost.
    hidden: HashMap<String, Hidden>,
}

struct Hidden {
    agent: String,
    session_id: String,
    last_seen: i64,
}

/// Whether an event is the session at work again, which brings a hidden one
/// back. Heartbeats, readings, notices and the idle reminder are not; an end
/// is recorded without bringing it back.
fn wakes(event: &Event) -> bool {
    !event.is_ephemeral()
        && !matches!(
            event,
            Event::Notice { .. } | Event::SessionEnd { .. } | Event::Attention { kind: AttentionKind::Idle, .. }
        )
}

/// A running session at rest for longer than the live view keeps one.
fn idle_too_long(s: &Session, now: i64, hide_idle_ms: i64) -> bool {
    hide_idle_ms > 0 && !s.is_ended() && s.status == Status::Idle && now - s.updated_at > hide_idle_ms
}

/// How long a forgotten session's stragglers are turned away.
const TOMBSTONE_MS: i64 = 24 * 3_600_000;

/// Exporters retry for minutes: hours of ids cover them, and the cap keeps
/// the memory flat however busy the machine is.
const SEEN_WINDOW_MS: i64 = 6 * 3_600_000;
const SEEN_MAX: usize = 50_000;
const PENDING_MAX: usize = 1_000;
/// Only requests this recent wait for their session: older ones (a log read
/// back from long ago) belong to sessions that will not be reported now.
const PENDING_WINDOW_MS: i64 = 3_600_000;

impl State {
    /// The session, created on first word, picking up any token counts that
    /// arrived before it.
    fn session_entry(&mut self, store: &Store, agent: &str, sid: &str, ts: i64) -> &mut Session {
        let key = Session::key_for(agent, sid);
        if !self.sessions.contains_key(&key) {
            // A session back after leaving memory (resumed days later, or
            // unarchived) picks up what the store kept of it.
            let mut s = store
                .read(|c| store::session(c, &key))
                .ok()
                .flatten()
                .unwrap_or_else(|| reducer::new_session(agent, sid, ts));
            if let Some(t) = self.pending_usage.remove(&key) {
                add_exact(&mut s, &t);
            }
            self.sessions.insert(key.clone(), s);
        }
        self.sessions.get_mut(&key).expect("just inserted")
    }

    /// Takes a session out of memory, and so out of the live view. A running
    /// one is remembered as hidden, so its heartbeats do not bring it back.
    fn hide(&mut self, key: &str, out: &mut Vec<Outbound>) -> bool {
        let Some(s) = self.sessions.remove(key) else { return false };
        self.heartbeats.remove(key);
        if !s.is_ended() {
            self.hidden.insert(key.to_string(), Hidden { agent: s.agent, session_id: s.session_id, last_seen: s.last_seen });
        }
        out.push(Msg::SessionRemoved { key }.out());
        true
    }

    fn remember(&mut self, request_id: &str, now: i64) -> bool {
        if !self.seen.insert(request_id.to_string()) {
            return false;
        }
        self.seen_order.push_back((now, request_id.to_string()));
        while let Some((at, _)) = self.seen_order.front() {
            if self.seen_order.len() <= SEEN_MAX && now - at <= SEEN_WINDOW_MS {
                break;
            }
            if let Some((_, old)) = self.seen_order.pop_front() {
                self.seen.remove(&old);
            }
        }
        true
    }
}

/// Adds exact per-request counts to a session; the first ones replace the
/// estimate summed from turn ends.
fn add_exact(s: &mut Session, t: &TokenTotals) {
    let st = &mut s.stats;
    if !st.exact_tokens {
        st.exact_tokens = true;
        st.input_tokens = 0;
        st.output_tokens = 0;
        st.cache_read_tokens = 0;
        st.cache_write_tokens = 0;
    }
    st.input_tokens += t.input;
    st.output_tokens += t.output;
    st.cache_read_tokens += t.cache_read;
    st.cache_write_tokens += t.cache_write;
    st.api_requests += t.requests as u32;
}

/// A gap between sweeps this long means the machine (and the daemon) slept.
const SLEEP_GAP_MS: i64 = 30_000;
/// After a sleep, how long sessions get to beat again before silence counts.
const WAKE_GRACE_MS: i64 = 75_000;

pub struct Hub {
    state: Mutex<State>,
    /// Every `service.name` an OTLP export has carried, to tell "nothing
    /// exports here" from "something exports what we do not read".
    otlp_services: Mutex<BTreeSet<String>>,
    tx: broadcast::Sender<Outbound>,
    store: Store,
    pub info: ServerInfo,
    pub metrics: Metrics,
    keep_ended_ms: i64,
    hide_idle_ms: i64,
    pub live: LiveView,
    pub retention: Retention,
    /// Held while a cleanup pass runs, so two never overlap.
    pub janitor: Mutex<()>,
    /// What the quick pass remembers between runs.
    pub watch: Mutex<janitor::Watch>,
}

impl Hub {
    pub fn new(store: Store, live: LiveView, retention: Retention) -> Hub {
        let now = now_ms();
        let keep_ended_ms = live.keep_ended_minutes as i64 * 60_000;
        let hide_idle_ms = live.hide_idle_minutes as i64 * 60_000;
        let mut sessions: HashMap<String, Session> = HashMap::new();
        let mut hidden: HashMap<String, Hidden> = HashMap::new();
        for mut s in store.read(|c| store::live_sessions(c, now - keep_ended_ms)).unwrap_or_default() {
            if idle_too_long(&s, now, hide_idle_ms) {
                let h = Hidden { agent: s.agent, session_id: s.session_id, last_seen: s.last_seen };
                hidden.insert(s.key, h);
                continue;
            }
            // Nothing is known about them until they speak again.
            if !s.is_ended() {
                s.stale = true;
            }
            sessions.insert(s.key.clone(), s);
        }
        let quotas: BTreeMap<String, Quota> =
            store.read(store::quotas).unwrap_or_default().into_iter().map(|q| (q.agent.clone(), q)).collect();
        let seq = store.read(store::max_seq).unwrap_or(0);
        // An exporter retries for minutes at most.
        let mut seen_order: VecDeque<(i64, String)> = store
            .read(|c| store::recent_request_ids(c, now - 2 * 3_600_000))
            .unwrap_or_default()
            .into_iter()
            .map(|id| (now, id))
            .collect();
        seen_order.truncate(SEEN_MAX);
        let seen: HashSet<String> = seen_order.iter().map(|(_, id)| id.clone()).collect();
        let summary = summary::build(sessions.values(), &quotas.values().cloned().collect::<Vec<_>>(), now);
        let (tx, _) = broadcast::channel(4096);
        Hub {
            state: Mutex::new(State {
                sessions,
                quotas,
                heartbeats: HashMap::new(),
                seq,
                summary,
                last_sweep: now,
                grace_until: now + WAKE_GRACE_MS,
                seen,
                seen_order,
                pending_usage: HashMap::new(),
                forgotten: HashMap::new(),
                hidden,
            }),
            otlp_services: Mutex::new(BTreeSet::new()),
            tx,
            store,
            info: ServerInfo {
                name: "agentsmon".into(),
                version: env!("CARGO_PKG_VERSION").into(),
                protocol: PROTOCOL_VERSION,
                started_at: now,
            },
            metrics: Metrics::default(),
            keep_ended_ms,
            hide_idle_ms,
            live,
            retention,
            janitor: Mutex::new(()),
            watch: Mutex::new(janitor::Watch::default()),
        }
    }

    pub fn store(&self) -> &Store {
        &self.store
    }

    pub fn subscribe(&self) -> broadcast::Receiver<Outbound> {
        self.tx.subscribe()
    }

    fn publish(&self, out: Vec<Outbound>) {
        for o in out {
            // No receivers is fine.
            let _ = self.tx.send(o);
        }
    }

    pub fn ingest(&self, batch: Vec<Envelope>) -> IngestReport {
        let now = now_ms();
        let mut report = IngestReport::default();
        let mut out = Vec::new();
        let mut changed: Vec<String> = Vec::new();
        let mut st = self.state.lock();

        for env in batch {
            if env.agent.is_empty() || env.agent.len() > 64 {
                report.rejected.push("agent must be 1-64 chars".into());
                continue;
            }
            // A clock ahead of ours is taken as ours.
            let ts = env.ts.map_or(now, |t| t.min(now));
            let (sid, events) = match env.event {
                Event::Raw { payload } => match adapters::normalize(&env.agent, &payload) {
                    Ok(n) => {
                        // Metadata rides along as a heartbeat-free merge.
                        if !n.meta.is_empty() && !n.sid.is_empty() && n.sid.len() <= 128 {
                            let key = Session::key_for(&env.agent, &n.sid);
                            // A hidden session takes it only when it comes back.
                            if n.events.iter().any(wakes) {
                                st.hidden.remove(&key);
                            }
                            if !st.forgotten.contains_key(&key) && !st.hidden.contains_key(&key) {
                                let s = st.session_entry(&self.store, &env.agent, &n.sid, ts);
                                if s.meta.merge(&n.meta) && !changed.contains(&key) {
                                    changed.push(key);
                                }
                            }
                        }
                        (n.sid, n.events)
                    }
                    Err(e) => {
                        report.rejected.push(e.to_string());
                        continue;
                    }
                },
                event => match env.sid {
                    Some(sid) => (sid, vec![event]),
                    None => {
                        report.rejected.push(format!("{}: sid missing", event.name()));
                        continue;
                    }
                },
            };
            if sid.is_empty() || sid.len() > 128 {
                report.rejected.push("sid must be 1-128 chars".into());
                continue;
            }
            for event in events {
                self.apply_one(&mut st, &env.agent, &sid, ts, event, &mut changed, &mut out);
                report.accepted += 1;
            }
        }

        for key in &changed {
            if let Some(s) = st.sessions.get(key) {
                out.push(Msg::Session { session: s }.out());
                self.store.send(Op::Session(Box::new(s.clone())));
            }
        }
        self.refresh_summary(&mut st, now, &mut out);
        drop(st);

        self.metrics.batches_in.fetch_add(1, Ordering::Relaxed);
        self.metrics.events_in.fetch_add(report.accepted as u64, Ordering::Relaxed);
        self.metrics.rejected.fetch_add(report.rejected.len() as u64, Ordering::Relaxed);
        self.publish(out);
        report
    }

    #[allow(clippy::too_many_arguments)]
    fn apply_one(
        &self,
        st: &mut State,
        agent: &str,
        sid: &str,
        ts: i64,
        event: Event,
        changed: &mut Vec<String>,
        out: &mut Vec<Outbound>,
    ) {
        let key = Session::key_for(agent, sid);
        if st.forgotten.contains_key(&key) {
            if !matches!(event, Event::SessionStart { .. }) {
                return;
            }
            st.forgotten.remove(&key);
        }
        // Out of the live view: work brings it back, its end goes to the
        // history alone, and anything else only says it still runs.
        let mut ends_hidden = false;
        if st.hidden.contains_key(&key) {
            if matches!(event, Event::SessionEnd { .. }) {
                ends_hidden = true;
            } else if !wakes(&event) {
                if let Some(h) = st.hidden.get_mut(&key) {
                    h.last_seen = h.last_seen.max(ts);
                }
                if let Event::Usage { rate_limits: Some(windows), .. } = &event {
                    self.note_quota(st, agent, &key, ts, windows, out);
                }
                if !event.is_ephemeral() {
                    self.record(st, ts, key, agent, event, out);
                }
                return;
            }
            st.hidden.remove(&key);
        }
        if matches!(event, Event::Heartbeat { .. }) {
            st.heartbeats.insert(key.clone(), ts);
        }
        let s = st.session_entry(&self.store, agent, sid, ts);
        let was_stale = s.stale;
        let mut moved = reducer::apply(s, ts, &event) || was_stale;
        if s.meta.project.is_none()
            && let Some(cwd) = &s.meta.cwd
        {
            s.meta.project = Some(project_name(cwd));
            moved = true;
        }
        if ends_hidden {
            // Stored as ended; the live view had let it go already.
            if let Some(s) = st.sessions.remove(&key) {
                self.store.send(Op::Session(Box::new(s)));
            }
            changed.retain(|k| k != &key);
        } else if moved && !changed.contains(&key) {
            changed.push(key.clone());
        }

        if let Event::Usage { rate_limits: Some(windows), .. } = &event {
            self.note_quota(st, agent, &key, ts, windows, out);
        }

        if !event.is_ephemeral() {
            self.record(st, ts, key, agent, event, out);
        }
    }

    /// Adds an event to the history and the event feed.
    fn record(&self, st: &mut State, ts: i64, key: String, agent: &str, event: Event, out: &mut Vec<Outbound>) {
        st.seq += 1;
        let record = EventRecord { seq: st.seq, ts, key, agent: agent.to_string(), event };
        out.push(Msg::Event { record: &record }.out());
        self.store.send(Op::Event(record));
    }

    /// Files an account's rate-limit reading, as `source` (a session key) saw it.
    fn note_quota(&self, st: &mut State, agent: &str, source: &str, ts: i64, windows: &[RateWindow], out: &mut Vec<Outbound>) {
        if windows.is_empty() {
            return;
        }
        let sample = |w: &RateWindow| {
            Op::Sample(QuotaSample {
                agent: agent.to_string(),
                kind: w.kind.clone(),
                ts,
                percent_used: w.percent_used,
                resets_at: w.resets_at.clone(),
            })
        };
        let previous = st.quotas.get(agent);
        // A reading older than the one held (a replayed queue) only goes to the history.
        if previous.is_some_and(|q| ts < q.updated_at) {
            for w in windows {
                self.store.send(sample(w));
            }
        } else if previous.is_none_or(|q| q.windows != windows) {
            for w in windows {
                let was = previous.and_then(|q| q.windows.iter().find(|p| p.kind == w.kind));
                if was.is_none_or(|p| p.percent_used != w.percent_used || p.resets_at != w.resets_at) {
                    self.store.send(sample(w));
                }
            }
            let quota = Quota { agent: agent.to_string(), windows: windows.to_vec(), updated_at: ts, source: Some(source.to_string()) };
            out.push(Msg::Quota { quota: &quota }.out());
            self.store.send(Op::Quota(quota.clone()));
            st.quotas.insert(agent.to_string(), quota);
        }
    }

    /// What a collector read about a session in the agent's own files. It
    /// fills in a session the agent's integration reported and never creates
    /// one; an ended session keeps its last context. The rate limits count
    /// for the account either way. Returns whether the session is known.
    pub fn observe(&self, agent: &str, sid: &str, ts: i64, seen: &Observed) -> bool {
        let now = now_ms();
        let key = Session::key_for(agent, sid);
        let mut out = Vec::new();
        let mut st = self.state.lock();
        if let Some(windows) = &seen.rate_limits {
            self.note_quota(&mut st, agent, &key, ts.min(now), windows, &mut out);
        }
        let known = !st.forgotten.contains_key(&key) && st.sessions.contains_key(&key);
        if let Some(s) = st.sessions.get_mut(&key).filter(|_| known) {
            let mut moved = seen.meta.as_ref().is_some_and(|m| s.meta.merge(m));
            if let Some(context) = seen.context.as_ref().filter(|c| !s.is_ended() && s.context.as_ref() != Some(*c)) {
                s.context = Some(context.clone());
                moved = true;
            }
            if moved {
                out.push(Msg::Session { session: s }.out());
                self.store.send(Op::Session(Box::new(s.clone())));
            }
        }
        self.refresh_summary(&mut st, now, &mut out);
        drop(st);
        self.publish(out);
        known
    }

    /// Files a reading of an account's rate limits that no session brought:
    /// the agent's own record of them, read as `source` says.
    pub fn note_rate_limits(&self, agent: &str, source: &str, ts: i64, windows: &[RateWindow]) {
        let now = now_ms();
        let mut out = Vec::new();
        let mut st = self.state.lock();
        self.note_quota(&mut st, agent, source, ts.min(now), windows, &mut out);
        self.refresh_summary(&mut st, now, &mut out);
        drop(st);
        self.publish(out);
    }

    /// When the reading of the agent's rate limits held now was taken.
    pub fn quota_at(&self, agent: &str) -> Option<i64> {
        self.state.lock().quotas.get(agent).map(|q| q.updated_at)
    }

    fn refresh_summary(&self, st: &mut State, now: i64, out: &mut Vec<Outbound>) {
        let quotas: Vec<Quota> = st.quotas.values().cloned().collect();
        let next = summary::build(st.sessions.values(), &quotas, now);
        if !summary::same(&next, &st.summary) {
            out.push(Msg::Summary { summary: &next }.out());
        }
        st.summary = next;
    }

    pub fn note_otlp(&self, services: &[String]) {
        self.metrics.otlp_exports.fetch_add(1, Ordering::Relaxed);
        let mut known = self.otlp_services.lock();
        for s in services {
            if known.len() < 32 {
                known.insert(s.clone());
            }
        }
    }

    pub fn otlp_services(&self) -> Vec<String> {
        self.otlp_services.lock().iter().cloned().collect()
    }

    /// Counts model requests exactly once each, by request id, into the
    /// ledger and into their sessions. Returns how many were new.
    pub fn ingest_usage(&self, records: Vec<UsageRecord>) -> usize {
        let now = now_ms();
        let mut out = Vec::new();
        let mut changed: Vec<String> = Vec::new();
        let mut added = TokenTotals::default();
        let mut fresh = 0;
        let mut st = self.state.lock();

        for u in records {
            if !st.remember(&u.request_id, now) {
                self.metrics.usage_duplicates.fetch_add(1, Ordering::Relaxed);
                continue;
            }
            fresh += 1;
            let t = TokenTotals { input: u.input, output: u.output, cache_read: u.cache_read, cache_write: u.cache_write, requests: 1 };
            added.add(&t);
            if let Some(sid) = &u.session_id {
                let key = Session::key_for(&u.agent, sid);
                // A session's last requests may land after its end: they still count.
                if let Some(s) = st.sessions.get_mut(&key) {
                    add_exact(s, &t);
                    if !changed.contains(&key) {
                        changed.push(key);
                    }
                } else if now - u.ts < PENDING_WINDOW_MS
                    && (st.pending_usage.len() < PENDING_MAX || st.pending_usage.contains_key(&key))
                {
                    st.pending_usage.entry(key).or_default().add(&t);
                }
            }
            self.store.send(Op::Usage(u));
        }

        for key in &changed {
            if let Some(s) = st.sessions.get(key) {
                out.push(Msg::Session { session: s }.out());
                self.store.send(Op::Session(Box::new(s.clone())));
            }
        }
        drop(st);
        if fresh > 0 {
            out.push(Msg::Tokens { ts: now, added: &added }.out());
        }
        self.metrics.usage_in.fetch_add(fresh as u64, Ordering::Relaxed);
        self.publish(out);
        fresh
    }

    /// Marks silent sessions stale, ends the ones long gone (hidden ones
    /// too), hides the ones idle too long, and forgets sessions that ended
    /// long enough ago. Runs every few seconds.
    pub fn sweep(&self) {
        self.sweep_at(now_ms());
    }

    fn sweep_at(&self, now: i64) {
        let mut out = Vec::new();
        let mut st = self.state.lock();
        if now - st.last_sweep > SLEEP_GAP_MS {
            st.grace_until = now + WAKE_GRACE_MS;
        }
        st.last_sweep = now;
        let judge = now >= st.grace_until;
        let mut lost = Vec::new();
        let State { sessions, heartbeats, hidden, .. } = &mut *st;

        for h in hidden.values().filter(|h| judge && now - h.last_seen > adapters::profile(&h.agent).lost_after_ms) {
            lost.push((h.agent.clone(), h.session_id.clone()));
        }
        for s in sessions.values_mut().filter(|s| judge && !s.is_ended()) {
            let profile = adapters::profile(&s.agent);
            let silent = now - s.last_seen;
            if silent > profile.lost_after_ms {
                lost.push((s.agent.clone(), s.session_id.clone()));
            } else if !s.stale && heartbeats.contains_key(&s.key) && silent > profile.stale_after_ms {
                s.stale = true;
                out.push(Msg::Session { session: s }.out());
                self.store.send(Op::Session(Box::new(s.clone())));
            }
        }
        let mut changed = Vec::new();
        for (agent, sid) in lost {
            let end = Event::SessionEnd { reason: Some("lost".into()) };
            self.apply_one(&mut st, &agent, &sid, now, end, &mut changed, &mut out);
        }
        for key in &changed {
            if let Some(s) = st.sessions.get(key) {
                out.push(Msg::Session { session: s }.out());
                self.store.send(Op::Session(Box::new(s.clone())));
            }
        }

        let idle: Vec<String> =
            st.sessions.values().filter(|s| idle_too_long(s, now, self.hide_idle_ms)).map(|s| s.key.clone()).collect();
        for key in idle {
            st.hide(&key, &mut out);
        }

        st.forgotten.retain(|_, at| now - *at < TOMBSTONE_MS);
        let cutoff = now - self.keep_ended_ms;
        let gone: Vec<String> =
            st.sessions.values().filter(|s| s.ended_at.is_some_and(|t| t < cutoff)).map(|s| s.key.clone()).collect();
        for key in gone {
            st.sessions.remove(&key);
            st.heartbeats.remove(&key);
            out.push(Msg::SessionRemoved { key: &key }.out());
        }

        self.refresh_summary(&mut st, now, &mut out);
        drop(st);
        self.publish(out);
    }

    /// Forgets a session everywhere, its history included.
    pub fn forget(&self, key: &str) -> bool {
        self.forget_many(&[key.to_string()]) > 0
    }

    /// Forgets sessions everywhere, their history included; returns how many
    /// were in memory.
    pub fn forget_many(&self, keys: &[String]) -> usize {
        let now = now_ms();
        let mut st = self.state.lock();
        let mut out = Vec::new();
        let mut known = 0;
        for key in keys {
            if st.sessions.remove(key).is_some() {
                known += 1;
            }
            st.heartbeats.remove(key);
            st.hidden.remove(key);
            st.forgotten.insert(key.clone(), now);
            out.push(Msg::SessionRemoved { key }.out());
        }
        self.refresh_summary(&mut st, now_ms(), &mut out);
        drop(st);
        for key in keys {
            self.store.send(Op::Forget(key.clone()));
        }
        self.publish(out);
        known
    }

    /// Takes sessions out of the live view (memory) and keeps them in the
    /// store: the history still has them, and they come back if they go on
    /// (heartbeats alone do not).
    pub fn hide_many(&self, keys: &[String]) -> usize {
        let mut st = self.state.lock();
        let mut out = Vec::new();
        let mut hidden = 0;
        for key in keys {
            if st.hide(key, &mut out) {
                hidden += 1;
            }
        }
        self.refresh_summary(&mut st, now_ms(), &mut out);
        drop(st);
        self.publish(out);
        hidden
    }

    /// Gives sessions in memory their titles; returns how many changed.
    pub fn set_titles(&self, titles: &[(String, String)]) -> usize {
        let mut st = self.state.lock();
        let mut out = Vec::new();
        let mut changed = 0;
        for (key, title) in titles {
            let Some(s) = st.sessions.get_mut(key) else { continue };
            if s.meta.title.as_deref() == Some(title.as_str()) {
                continue;
            }
            s.meta.title = Some(title.clone());
            out.push(Msg::Session { session: s }.out());
            self.store.send(Op::Session(Box::new(s.clone())));
            changed += 1;
        }
        if changed > 0 {
            self.refresh_summary(&mut st, now_ms(), &mut out);
        }
        drop(st);
        self.publish(out);
        changed
    }

    /// What the janitor checks of each session in memory.
    pub fn presence(&self) -> Vec<Presence> {
        let st = self.state.lock();
        st.sessions
            .values()
            .map(|s| Presence {
                key: s.key.clone(),
                agent: s.agent.clone(),
                session_id: s.session_id.clone(),
                ended: s.is_ended(),
                transcript: s.meta.transcript_path.clone(),
            })
            .collect()
    }

    /// Live sessions and those ended recently, most recently active first.
    pub fn sessions(&self) -> Vec<Session> {
        let st = self.state.lock();
        let mut list: Vec<Session> = st.sessions.values().cloned().collect();
        list.sort_by_key(|s| std::cmp::Reverse(s.updated_at));
        list
    }

    pub fn session(&self, key: &str) -> Option<Session> {
        self.state.lock().sessions.get(key).cloned()
    }

    pub fn quotas(&self) -> Vec<Quota> {
        self.state.lock().quotas.values().cloned().collect()
    }

    pub fn summary(&self) -> Summary {
        self.state.lock().summary.clone()
    }

    pub fn snapshot(&self) -> Snapshot {
        let st = self.state.lock();
        let mut sessions: Vec<Session> = st.sessions.values().cloned().collect();
        sessions.sort_by_key(|s| std::cmp::Reverse(s.updated_at));
        Snapshot {
            server: self.info.clone(),
            sessions,
            quotas: st.quotas.values().cloned().collect(),
            summary: st.summary.clone(),
        }
    }

    pub fn snapshot_message(&self) -> Utf8Bytes {
        Msg::Snapshot(&self.snapshot()).out().text
    }

    pub fn live_count(&self) -> usize {
        self.state.lock().sessions.values().filter(|s| !s.is_ended()).count()
    }

    /// Running sessions out of the live view, idle or archived.
    pub fn hidden_count(&self) -> usize {
        self.state.lock().hidden.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use agentsmon_protocol::{RateWindow, Status};

    fn hub(name: &str) -> (Hub, std::path::PathBuf) {
        let dir = std::env::temp_dir().join(format!("agentsmon-hub-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        (Hub::new(Store::open(&dir.join("t.db")).unwrap(), LiveView::default(), Retention::default()), dir)
    }

    fn env(json: &str) -> Envelope {
        serde_json::from_str(json).unwrap()
    }

    #[test]
    fn ingest_drives_sessions_and_broadcasts() {
        let (hub, dir) = hub("ingest");
        let mut rx = hub.subscribe();
        let report = hub.ingest(vec![
            env(r#"{"agent":"claude-code","sid":"s1","type":"session.start","cwd":"/w/proj"}"#),
            env(r#"{"agent":"claude-code","sid":"s1","type":"turn.start"}"#),
            env(r#"{"agent":"claude-code","sid":"s1","type":"tool.start","tool":"Bash","id":"t"}"#),
            env(r#"{"agent":"claude-code","type":"prompt"}"#),
        ]);
        assert_eq!(report.accepted, 3);
        assert_eq!(report.rejected.len(), 1);
        let s = hub.session("claude-code:s1").unwrap();
        assert_eq!(s.status, Status::Tool);
        assert_eq!(s.meta.project.as_deref(), Some("proj"));

        let mut kinds = Vec::new();
        while let Ok(o) = rx.try_recv() {
            let v: serde_json::Value = serde_json::from_str(o.text.as_str()).unwrap();
            kinds.push(v["type"].as_str().unwrap().to_string());
        }
        assert_eq!(kinds.iter().filter(|k| *k == "event").count(), 3);
        assert_eq!(kinds.iter().filter(|k| *k == "session").count(), 1, "one session message per batch");
        assert!(kinds.contains(&"summary".to_string()));
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn raw_payloads_go_through_the_adapter() {
        let (hub, dir) = hub("raw");
        let report = hub.ingest(vec![
            env(r#"{"agent":"claude-code","type":"raw","payload":{"session_id":"r","hook_event_name":"UserPromptSubmit","prompt":"go"}}"#),
            env(r#"{"agent":"nobody","type":"raw","payload":{}}"#),
        ]);
        assert_eq!((report.accepted, report.rejected.len()), (2, 1));
        assert_eq!(hub.session("claude-code:r").unwrap().status, Status::Thinking);
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn rate_limits_become_the_agents_quota() {
        let (hub, dir) = hub("quota");
        let usage = |pct: f64| Envelope {
            agent: "claude-code".into(),
            sid: Some("s".into()),
            ts: None,
            event: Event::Usage {
                context: None,
                cost_usd: Some(1.5),
                rate_limits: Some(vec![RateWindow { kind: "five_hour".into(), percent_used: pct, resets_at: None }]),
            },
        };
        hub.ingest(vec![usage(10.0)]);
        hub.ingest(vec![usage(12.5)]);
        let q = hub.quotas();
        assert_eq!(q.len(), 1);
        assert_eq!(q[0].windows[0].percent_used, 12.5);
        assert_eq!(hub.session("claude-code:s").unwrap().cost_usd, Some(1.5));
        assert_eq!(hub.summary().quotas[0].windows[0], ("five_hour".to_string(), 12.5));
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn silent_heartbeating_sessions_go_stale() {
        let (hub, dir) = hub("stale");
        let now = now_ms();
        let old = now - 120_000;
        hub.ingest(vec![env(&format!(r#"{{"agent":"claude-code","sid":"h","ts":{old},"type":"heartbeat"}}"#))]);
        hub.ingest(vec![env(&format!(r#"{{"agent":"claude-code","sid":"q","ts":{old},"type":"prompt"}}"#))]);
        // Fresh from start the daemon gives sessions a grace period first.
        hub.sweep_at(now);
        assert!(!hub.session("claude-code:h").unwrap().stale);
        let mut t = now;
        while t < now + WAKE_GRACE_MS {
            t += 5_000;
            hub.sweep_at(t);
        }
        assert!(hub.session("claude-code:h").unwrap().stale);
        assert!(!hub.session("claude-code:q").unwrap().stale, "no heartbeat, no staleness");
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn waking_from_sleep_ends_nothing_at_once() {
        let (hub, dir) = hub("sleep");
        let now = now_ms();
        hub.ingest(vec![env(r#"{"agent":"claude-code","sid":"z","type":"heartbeat"}"#)]);
        // An hour asleep: far past the lost window, yet the session stays...
        let woke = now + 3_600_000;
        hub.sweep_at(woke);
        let s = hub.session("claude-code:z").unwrap();
        assert!(!s.is_ended() && !s.stale);
        // ...until it has had its chance to beat again.
        let mut t = woke;
        while t <= woke + WAKE_GRACE_MS {
            t += 5_000;
            hub.sweep_at(t);
        }
        assert_eq!(hub.session("claude-code:z").unwrap().end_reason.as_deref(), Some("lost"));
        std::fs::remove_dir_all(dir).ok();
    }

    fn request(id: &str, sid: &str, output: u64) -> UsageRecord {
        UsageRecord {
            request_id: id.into(),
            ts: now_ms(),
            agent: "claude-code".into(),
            session_id: Some(sid.into()),
            model: Some("claude-opus-5-5".into()),
            source: Some("main".into()),
            input: 3,
            output,
            cache_read: 1000,
            cache_write: 50,
        }
    }

    #[test]
    fn exact_counts_replace_the_turn_estimate_and_count_once() {
        let (hub, dir) = hub("usage");
        hub.ingest(vec![
            env(r#"{"agent":"claude-code","sid":"u","type":"turn.start"}"#),
            env(r#"{"agent":"claude-code","sid":"u","type":"turn.end","outcome":"answer","usage":{"input":9,"output":9}}"#),
        ]);
        assert_eq!(hub.session("claude-code:u").unwrap().stats.output_tokens, 9);

        let mut rx = hub.subscribe();
        assert_eq!(hub.ingest_usage(vec![request("r1", "u", 5), request("r2", "u", 7), request("r1", "u", 5)]), 2);
        let s = hub.session("claude-code:u").unwrap();
        assert!(s.stats.exact_tokens);
        assert_eq!((s.stats.output_tokens, s.stats.input_tokens, s.stats.api_requests), (12, 6, 2));

        // Later turn ends no longer add their own estimate.
        hub.ingest(vec![env(r#"{"agent":"claude-code","sid":"u","type":"turn.end","outcome":"answer","usage":{"input":9,"output":9}}"#)]);
        assert_eq!(hub.session("claude-code:u").unwrap().stats.output_tokens, 12);

        let mut kinds = Vec::new();
        while let Ok(o) = rx.try_recv() {
            let v: serde_json::Value = serde_json::from_str(o.text.as_str()).unwrap();
            kinds.push(v["type"].as_str().unwrap().to_string());
        }
        assert!(kinds.contains(&"tokens".to_string()));
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn counts_before_the_session_is_known_wait_for_it() {
        let (hub, dir) = hub("pending");
        hub.ingest_usage(vec![request("p1", "late", 4)]);
        assert!(hub.session("claude-code:late").is_none());
        hub.ingest(vec![env(r#"{"agent":"claude-code","sid":"late","type":"session.start"}"#)]);
        let s = hub.session("claude-code:late").unwrap();
        assert_eq!((s.stats.output_tokens, s.stats.api_requests), (4, 1));

        // A request from long ago (a log read back) is counted in the ledger
        // only: its session is not about to be reported.
        let old = UsageRecord { ts: now_ms() - 2 * PENDING_WINDOW_MS, ..request("p2", "gone", 4) };
        assert_eq!(hub.ingest_usage(vec![old]), 1);
        hub.ingest(vec![env(r#"{"agent":"claude-code","sid":"gone","type":"session.start"}"#)]);
        assert_eq!(hub.session("claude-code:gone").unwrap().stats.api_requests, 0);
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn observations_fill_in_known_sessions_only() {
        let (hub, dir) = hub("observe");
        let seen = Observed {
            meta: Some(SessionMeta { surface: Some("desktop".into()), ..Default::default() }),
            context: Some(ContextUsage { tokens: Some(10), window: Some(100), percent: Some(10.0) }),
            rate_limits: Some(vec![RateWindow { kind: "five_hour".into(), percent_used: 3.0, resets_at: None }]),
        };
        // Unknown: no session appears, the account's limits still count.
        assert!(!hub.observe("codex", "c", now_ms(), &seen));
        assert!(hub.session("codex:c").is_none());
        assert_eq!(hub.quotas()[0].windows[0].percent_used, 3.0);

        hub.ingest(vec![env(r#"{"agent":"codex","sid":"c","type":"session.start"}"#)]);
        assert!(hub.observe("codex", "c", now_ms(), &seen));
        let s = hub.session("codex:c").unwrap();
        assert_eq!((s.meta.surface.as_deref(), s.context.and_then(|c| c.percent)), (Some("desktop"), Some(10.0)));

        // An ended session keeps its last context and stays ended.
        hub.ingest(vec![env(r#"{"agent":"codex","sid":"c","type":"session.end"}"#)]);
        let later = Observed { context: Some(ContextUsage { tokens: Some(90), ..Default::default() }), ..seen };
        assert!(hub.observe("codex", "c", now_ms(), &later));
        let s = hub.session("codex:c").unwrap();
        assert!(s.is_ended());
        assert_eq!(s.context.and_then(|c| c.tokens), Some(10));
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn the_dedupe_window_is_bounded_in_time_and_size() {
        let (hub, dir) = hub("seen");
        let mut st = hub.state.lock();
        assert!(st.remember("a", 0));
        assert!(!st.remember("a", 1), "a repeat inside the window counts once");
        // Past the window the old id is let go.
        assert!(st.remember("b", SEEN_WINDOW_MS + 10));
        assert!(st.remember("a", SEEN_WINDOW_MS + 11));
        for i in 0..SEEN_MAX + 10 {
            st.remember(&format!("x{i}"), SEEN_WINDOW_MS + 20);
        }
        assert!(st.seen.len() <= SEEN_MAX && st.seen_order.len() <= SEEN_MAX);
        drop(st);
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn forgetting_many_removes_them_from_memory_and_the_store() {
        let (hub, dir) = hub("forget");
        hub.ingest(vec![
            env(r#"{"agent":"claude-code","sid":"f1","type":"session.end"}"#),
            env(r#"{"agent":"claude-code","sid":"f2","type":"session.end"}"#),
            env(r#"{"agent":"claude-code","sid":"f3","type":"prompt"}"#),
        ]);
        assert_eq!(hub.forget_many(&["claude-code:f1".into(), "claude-code:f2".into()]), 2);
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        let left: Vec<String> = hub.sessions().into_iter().map(|s| s.key).collect();
        assert_eq!(left, vec!["claude-code:f3".to_string()]);
        let stored = hub.store().read(|c| store::list_sessions(c, None, 10, 0)).unwrap();
        assert_eq!(stored.len(), 1);
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn a_forgotten_sessions_stragglers_do_not_bring_it_back_but_a_new_start_does() {
        let (hub, dir) = hub("tombstone");
        hub.ingest(vec![env(r#"{"agent":"claude-code","sid":"t","type":"prompt"}"#)]);
        hub.forget("claude-code:t");
        hub.ingest(vec![
            env(r#"{"agent":"claude-code","sid":"t","type":"heartbeat"}"#),
            env(r#"{"agent":"claude-code","sid":"t","type":"session.end"}"#),
        ]);
        assert!(hub.session("claude-code:t").is_none());
        hub.ingest(vec![env(r#"{"agent":"claude-code","sid":"t","type":"session.start","source":"resume"}"#)]);
        assert!(hub.session("claude-code:t").is_some());
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn a_hidden_session_that_goes_on_comes_back_with_its_history() {
        let (hub, dir) = hub("revive");
        hub.ingest(vec![
            env(r#"{"agent":"claude-code","sid":"r","type":"prompt"}"#),
            env(r#"{"agent":"claude-code","sid":"r","type":"prompt"}"#),
            env(r#"{"agent":"claude-code","sid":"r","type":"session.end"}"#),
        ]);
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        assert_eq!(hub.hide_many(&["claude-code:r".into()]), 1);
        assert!(hub.session("claude-code:r").is_none());
        // Unarchived and resumed: the two earlier prompts are still counted.
        hub.ingest(vec![env(r#"{"agent":"claude-code","sid":"r","type":"prompt"}"#)]);
        assert_eq!(hub.session("claude-code:r").unwrap().stats.prompts, 3);
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn idle_sessions_leave_the_live_view_until_they_work_again() {
        let (hub, dir) = hub("idle");
        let now = now_ms();
        let idle = now - 3 * 3_600_000;
        let at = |sid: &str, ts: i64, rest: &str| env(&format!(r#"{{"agent":"claude-code","sid":"{sid}","ts":{ts},{rest}}}"#));
        hub.ingest(vec![
            at("old", idle, r#""type":"prompt""#),
            at("asking", idle, r#""type":"attention","kind":"permission","tool":"Bash""#),
            at("recent", now - 60_000, r#""type":"prompt""#),
        ]);
        hub.ingest(vec![
            at("old", now, r#""type":"heartbeat","busy":false"#),
            at("asking", now, r#""type":"heartbeat","busy":true"#),
        ]);
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        hub.sweep_at(now);
        // Waiting on a person, or used lately: still in view.
        assert!(hub.session("claude-code:asking").is_some());
        assert!(hub.session("claude-code:recent").is_some());
        assert!(hub.session("claude-code:old").is_none());
        assert_eq!(hub.hidden_count(), 1);

        // Its heartbeats and the idle reminder do not bring it back...
        hub.ingest(vec![
            at("old", now, r#""type":"heartbeat","busy":false"#),
            at("old", now, r#""type":"attention","kind":"idle""#),
        ]);
        assert!(hub.session("claude-code:old").is_none());
        // ...a prompt does, with what it had.
        hub.ingest(vec![at("old", now, r#""type":"prompt""#)]);
        assert_eq!(hub.session("claude-code:old").unwrap().stats.prompts, 2);
        assert_eq!(hub.hidden_count(), 0);
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn a_hidden_session_ends_in_the_history_only() {
        let (hub, dir) = hub("hidden-end");
        let now = now_ms();
        let idle = now - 3 * 3_600_000;
        for sid in ["closed", "crashed"] {
            hub.ingest(vec![env(&format!(r#"{{"agent":"claude-code","sid":"{sid}","ts":{idle},"type":"prompt"}}"#))]);
        }
        hub.ingest(vec![env(r#"{"agent":"claude-code","sid":"closed","type":"heartbeat"}"#)]);
        hub.sweep_at(now);
        assert_eq!(hub.hidden_count(), 2);

        hub.ingest(vec![env(r#"{"agent":"claude-code","sid":"closed","type":"session.end","reason":"other"}"#)]);
        // No beat from the other since it went quiet: once the start's grace
        // is over, it went lost, still out of view.
        let mut t = now;
        while t <= now + WAKE_GRACE_MS {
            t += 5_000;
            hub.sweep_at(t);
        }
        assert!(hub.sessions().is_empty());
        assert_eq!(hub.hidden_count(), 0);
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        let stored = |key: &str| hub.store().read(|c| store::session(c, key)).unwrap().unwrap();
        assert_eq!(stored("claude-code:closed").end_reason.as_deref(), Some("other"));
        assert_eq!(stored("claude-code:crashed").end_reason.as_deref(), Some("lost"));
        std::fs::remove_dir_all(dir).ok();
    }

    #[test]
    fn a_restart_keeps_idle_sessions_out_of_view() {
        let (hub, dir) = hub("idle-restart");
        let idle = now_ms() - 3 * 3_600_000;
        hub.ingest(vec![
            env(&format!(r#"{{"agent":"claude-code","sid":"old","ts":{idle},"type":"prompt"}}"#)),
            env(r#"{"agent":"claude-code","sid":"new","type":"prompt"}"#),
        ]);
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        drop(hub);
        let again = Hub::new(Store::open(&dir.join("t.db")).unwrap(), LiveView::default(), Retention::default());
        let keys: Vec<String> = again.sessions().into_iter().map(|s| s.key).collect();
        assert_eq!(keys, vec!["claude-code:new".to_string()]);
        assert_eq!(again.hidden_count(), 1);
        std::fs::remove_dir_all(dir).ok();
    }
}
