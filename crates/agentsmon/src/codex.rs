//! What Codex writes about its sessions that its hooks do not say.
//!
//! - **Rollouts.** Codex logs each thread to
//!   `$CODEX_HOME/sessions/YYYY/MM/DD/rollout-<time>-<thread id>.jsonl`. A
//!   `token_usage_record` line follows every model request, with its
//!   `response_id`; a `token_count` event carries the context fill and the
//!   account's rate-limit windows. [`Rollouts`] reads what each file gained
//!   and keeps in the store how far it has counted, so a request counts once
//!   across restarts. A subagent (a guardian review, a spawned agent) logs to
//!   a file of its own under its root session's id.
//! - **Threads.** `$CODEX_HOME/state_<n>.sqlite` has a row per thread, with the
//!   name Codex shows and whether it is archived; a deleted thread leaves it.
//!
//! Neither format is public: everything here is read-only and best effort,
//! and what does not read as expected is skipped.

use std::collections::{HashMap, HashSet};
use std::io::{BufRead, BufReader, Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use agentsmon_protocol::{ContextUsage, RateWindow, SessionMeta, UsageRecord, agents};
use rusqlite::{Connection, OpenFlags};
use serde_json::Value;

use crate::hub::{Hub, Observed};
use crate::store::{self, Op};
use crate::tail::{self, Dirs, Head, Position};
use crate::util::{format_utc, now_ms, parse_utc_ms};

/// At most this much of one file is read per pass.
const CHUNK: u64 = 8 << 20;
/// A session_meta line holds the base instructions: tens of kilobytes.
const FIRST_LINE_MAX: u64 = 4 << 20;
/// Files written to this recently are looked at on every pass; the rest on
/// the full walk.
const HOT: Duration = Duration::from_secs(24 * 3_600);
const FULL_WALK_EVERY: u32 = 30;
/// How long what was read waits for its session to be reported.
const UNSENT_MS: i64 = 10 * 60_000;

/// Lines worth parsing name one of these; everything else is skipped unread.
const MARKERS: [&str; 4] = [
    "\"type\":\"session_meta\"",
    "\"type\":\"turn_context\"",
    "\"type\":\"token_usage_record\"",
    "\"type\":\"token_count\"",
];

pub fn home() -> PathBuf {
    std::env::var_os("CODEX_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default().join(".codex"))
}

// ---------------------------------------------------------------------------
// Rollouts
// ---------------------------------------------------------------------------

/// Who a rollout belongs to, from its first line.
#[derive(Clone, Debug, PartialEq)]
struct Thread {
    session_id: String,
    /// `main`, or what kind of subagent wrote it.
    source: String,
    meta: SessionMeta,
}

#[derive(Default)]
struct Rollout {
    /// How far it is read in this process.
    pos: Position,
    /// Bytes whose requests an earlier run counted.
    counted: u64,
    /// How the file counted is known again.
    head: Option<Head>,
    first_read: bool,
    len: u64,
    modified: Option<SystemTime>,
    thread: Option<Thread>,
    model: Option<String>,
}

/// What one pass read.
#[derive(Debug, Default)]
pub struct Harvest {
    pub usage: Vec<UsageRecord>,
    /// Per session: when, and what was read, oldest first.
    pub seen: Vec<(String, i64, Observed)>,
    /// How far each file read is now counted, and its head.
    pub cursors: Vec<(String, u64, Option<String>)>,
}

pub struct Rollouts {
    root: PathBuf,
    dirs: Dirs,
    files: HashMap<PathBuf, Rollout>,
    /// From the store, at the first pass.
    counted: Option<HashMap<String, (u64, Option<Head>)>>,
    /// Read for a session not reported yet: since when, when, and what.
    unsent: HashMap<String, (i64, i64, Observed)>,
    passes: u32,
}

impl Rollouts {
    pub fn new() -> Rollouts {
        Rollouts::at(home().join("sessions"))
    }

    pub fn at(root: PathBuf) -> Rollouts {
        Rollouts { root, dirs: Dirs::default(), files: HashMap::new(), counted: None, unsent: HashMap::new(), passes: 0 }
    }

    /// Reads what the rollouts gained and hands it to the hub.
    pub fn pass(&mut self, hub: &Hub) {
        if self.counted.is_none() {
            match hub.store().read(store::log_cursors) {
                Ok(rows) => {
                    self.counted = Some(rows.into_iter().map(|(name, offset, head)| (name, (offset, head.as_deref().and_then(Head::decode)))).collect())
                }
                Err(e) => {
                    tracing::warn!("codex: could not read log cursors: {e}");
                    return;
                }
            }
        }
        let now = now_ms();
        let harvest = self.scan();
        if !harvest.usage.is_empty() {
            hub.ingest_usage(harvest.usage);
        }
        // After the requests, so a cursor never runs ahead of what it counted.
        for (name, offset, head) in harvest.cursors {
            hub.store().send(Op::Cursor { name, offset, head, ts: now });
        }
        // The rollout is written as soon as the thread starts, the hook that
        // reports the session a moment later.
        self.unsent.retain(|sid, (since, ts, seen)| now - *since < UNSENT_MS && !hub.observe(agents::CODEX, sid, *ts, seen));
        for (sid, ts, seen) in harvest.seen {
            if !hub.observe(agents::CODEX, &sid, ts, &seen) {
                // Its rate limits are filed; the rest waits for the session.
                let rest = Observed { rate_limits: None, ..seen };
                if rest != Observed::default() {
                    let since = self.unsent.get(&sid).map_or(now, |(since, _, _)| *since);
                    self.unsent.insert(sid, (since, ts, rest));
                }
            }
        }
    }

    /// Reads what every rollout gained since the last pass.
    pub fn scan(&mut self) -> Harvest {
        let full = self.passes.is_multiple_of(FULL_WALK_EVERY);
        self.passes = self.passes.wrapping_add(1);
        let mut paths: HashSet<PathBuf> = rollout_files(&mut self.dirs, &self.root, if full { usize::MAX } else { 2 }).into_iter().collect();
        if full {
            self.dirs.forget_untouched();
        }
        let now = SystemTime::now();
        let hot = |r: &Rollout| r.modified.and_then(|m| now.duration_since(m).ok()).is_none_or(|age| age < HOT);
        paths.extend(self.files.iter().filter(|(_, r)| full || hot(r)).map(|(p, _)| p.clone()));

        let mut harvest = Harvest::default();
        for path in paths {
            let Ok(md) = std::fs::metadata(&path) else {
                // Archived (moved) or deleted.
                self.files.remove(&path);
                continue;
            };
            let name = path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            let (counted, head) = self.counted.as_ref().and_then(|c| c.get(&name)).copied().unwrap_or_default();
            let r = self.files.entry(path.clone()).or_insert_with(|| Rollout { counted, head, ..Default::default() });
            r.modified = md.modified().ok();
            if r.first_read && md.len() == r.pos.read {
                continue;
            }
            r.len = md.len();
            if let Err(e) = r.read_new(&path, &name, &mut harvest) {
                tracing::debug!("codex: {}: {e}", path.display());
            }
        }
        harvest.seen.sort_by_key(|(_, ts, _)| *ts);
        harvest
    }
}

impl Rollout {
    fn read_new(&mut self, path: &Path, name: &str, out: &mut Harvest) -> std::io::Result<()> {
        let mut file = std::fs::File::open(path)?;
        if !self.first_read {
            self.first_read = true;
            if tail::replaced(&mut file, self.len, self.counted, self.head.as_ref())? {
                // Another file under the name: it is a new one.
                self.counted = 0;
                self.head = None;
            }
            if self.len == self.counted {
                // Nothing new since the last run: who it belongs to is enough.
                file.seek(SeekFrom::Start(0))?;
                let mut first = String::new();
                BufReader::new((&mut file).take(FIRST_LINE_MAX)).read_line(&mut first)?;
                let mut ignored = Harvest::default();
                self.take(&first, 0, &mut ignored, &mut Seen::default());
                self.pos.read = self.len;
                return Ok(());
            }
        } else if tail::replaced(&mut file, self.len, self.pos.read, self.head.as_ref())? {
            self.pos = Position::default();
            self.counted = 0;
            self.head = None;
        }
        if self.head.is_none_or(|h| h.short_of(self.len)) {
            self.head = Some(Head::of(&mut file, self.len)?);
        }

        let mut seen = Seen::default();
        let mut lines = Vec::new();
        self.pos.read_lines(&mut file, self.len, CHUNK, |line, end| {
            if let Ok(text) = std::str::from_utf8(line)
                && MARKERS.iter().any(|m| text.contains(m))
            {
                lines.push((text.to_string(), end));
            }
        })?;
        for (text, end) in lines {
            self.take(&text, end, out, &mut seen);
        }
        if self.pos.read > self.counted {
            self.counted = self.pos.read;
            out.cursors.push((name.to_string(), self.pos.read, self.head.map(|h| h.encode())));
        }
        if let Some(t) = &self.thread
            && let Some((ts, observed)) = seen.into_observed(t)
        {
            out.seen.push((t.session_id.clone(), ts, observed));
        }
        Ok(())
    }

    /// Takes one line ending at byte `end`. Requests and rate limits up to
    /// what an earlier run counted are not taken again.
    fn take(&mut self, line: &str, end: u64, out: &mut Harvest, seen: &mut Seen) {
        let Ok(v) = serde_json::from_str::<Value>(line) else { return };
        let fresh = end > self.counted;
        let ts = v.get("timestamp").and_then(Value::as_str).and_then(parse_utc_ms).unwrap_or_else(now_ms);
        let p = v.get("payload").unwrap_or(&Value::Null);
        match v.get("type").and_then(Value::as_str) {
            Some("session_meta") => self.thread = thread_of(p).or(self.thread.take()),
            Some("turn_context") => {
                if let Some(m) = p.get("model").and_then(Value::as_str) {
                    self.model = Some(m.to_string());
                }
            }
            Some("token_usage_record") if fresh => {
                if let Some(u) = self.usage_of(p, ts) {
                    out.usage.push(u);
                }
            }
            Some("event_msg") if p.get("type").and_then(Value::as_str) == Some("token_count") => {
                seen.ts = ts;
                if let Some(c) = context_of(p.get("info").unwrap_or(&Value::Null)) {
                    seen.context = Some(c);
                }
                if fresh && let Some(w) = p.get("rate_limits").and_then(windows_of) {
                    seen.rate_limits = Some(w);
                }
            }
            _ => {}
        }
    }

    fn usage_of(&self, p: &Value, ts: i64) -> Option<UsageRecord> {
        let text = |k: &str| p.get(k).and_then(Value::as_str).map(str::to_string);
        let request_id = text("response_id")?;
        let session_id = text("session_id").or_else(|| self.thread.as_ref().map(|t| t.session_id.clone()));
        let u = p.get("usage")?;
        let count = |k: &str| u.get(k).and_then(Value::as_u64).unwrap_or(0);
        // OpenAI counts cached input (read and written) inside the input, and
        // reasoning inside the output.
        let (cache_read, cache_write) = (count("cached_input_tokens"), count("cache_write_input_tokens"));
        let source = match (&self.thread, text("thread_id")) {
            (Some(t), _) => t.source.clone(),
            (None, Some(thread)) if Some(&thread) != session_id.as_ref() => "subagent".to_string(),
            _ => "main".to_string(),
        };
        Some(UsageRecord {
            request_id,
            ts,
            agent: agents::CODEX.to_string(),
            session_id,
            model: self.model.clone(),
            source: Some(source),
            input: count("input_tokens").saturating_sub(cache_read + cache_write),
            output: count("output_tokens"),
            cache_read,
            cache_write,
        })
    }
}

/// The latest of what one read saw.
#[derive(Default)]
struct Seen {
    ts: i64,
    context: Option<ContextUsage>,
    rate_limits: Option<Vec<RateWindow>>,
}

impl Seen {
    /// What a session learns from it: a subagent's file only tells the
    /// account's rate limits.
    fn into_observed(self, t: &Thread) -> Option<(i64, Observed)> {
        let main = t.source == "main";
        let observed = Observed {
            meta: main.then(|| t.meta.clone()),
            context: self.context.filter(|_| main),
            rate_limits: self.rate_limits,
        };
        (observed.context.is_some() || observed.rate_limits.is_some() || observed.meta.is_some())
            .then_some((if self.ts > 0 { self.ts } else { now_ms() }, observed))
    }
}

fn thread_of(p: &Value) -> Option<Thread> {
    let text = |k: &str| p.get(k).and_then(Value::as_str);
    let id = text("id")?;
    let session_id = text("session_id").unwrap_or(id).to_string();
    let source = p.get("source");
    let kind = match source.and_then(|s| s.get("subagent")) {
        Some(sub) => sub.get("other").and_then(Value::as_str).unwrap_or("subagent").to_string(),
        None if session_id != id => "subagent".to_string(),
        None => "main".to_string(),
    };
    let origin = source.and_then(Value::as_str);
    let surface = match (text("originator"), origin) {
        (Some("Codex Desktop"), _) => Some("desktop"),
        (_, Some("cli")) => Some("terminal"),
        (_, Some("exec")) => Some("headless"),
        (_, other) => other,
    };
    Some(Thread {
        session_id,
        source: kind,
        meta: SessionMeta {
            agent_version: text("cli_version").map(str::to_string),
            surface: surface.map(str::to_string),
            interactive: origin.map(|o| o != "exec"),
            ..Default::default()
        },
    })
}

fn context_of(info: &Value) -> Option<ContextUsage> {
    let window = info.get("model_context_window").and_then(Value::as_u64)?;
    let tokens = info.get("last_token_usage")?.get("total_tokens").and_then(Value::as_u64)?;
    let percent = (window > 0).then(|| (tokens as f64 / window as f64 * 1000.0).round() / 10.0);
    Some(ContextUsage { tokens: Some(tokens), window: Some(window), percent })
}

/// Codex's own usage limits: the five-hour and weekly windows, under their
/// usual names when they are those.
fn windows_of(rl: &Value) -> Option<Vec<RateWindow>> {
    if rl.get("limit_id").and_then(Value::as_str).is_some_and(|id| id != "codex") {
        return None;
    }
    let windows: Vec<RateWindow> = ["primary", "secondary"]
        .into_iter()
        .filter_map(|slot| {
            let w = rl.get(slot).filter(|w| w.is_object())?;
            let kind = match w.get("window_minutes").and_then(Value::as_u64) {
                Some(300) => "five_hour",
                Some(10_080) => "seven_day",
                _ => slot,
            };
            Some(RateWindow {
                kind: kind.to_string(),
                percent_used: w.get("used_percent").and_then(Value::as_f64)?,
                resets_at: w.get("resets_at").and_then(Value::as_i64).map(format_utc),
            })
        })
        .collect();
    (!windows.is_empty()).then_some(windows)
}

/// The rollout files of the newest `days` day folders, newest last.
fn rollout_files(dirs: &mut Dirs, root: &Path, days: usize) -> Vec<PathBuf> {
    let folders = |dirs: &mut Dirs, p: &Path| -> Vec<PathBuf> { dirs.list(p).iter().filter(|e| e.is_dir).map(|e| e.path.clone()).collect() };
    let mut day_dirs = Vec::new();
    'walk: for year in folders(dirs, root).into_iter().rev() {
        for month in folders(dirs, &year).into_iter().rev() {
            for day in folders(dirs, &month).into_iter().rev() {
                day_dirs.push(day);
                if day_dirs.len() >= days {
                    break 'walk;
                }
            }
        }
    }
    let mut files = Vec::new();
    for day in day_dirs.iter().rev() {
        files.extend(
            dirs.list(day)
                .iter()
                .filter(|e| !e.is_dir && e.path.file_name().and_then(|n| n.to_str()).is_some_and(|n| n.starts_with("rollout-") && n.ends_with(".jsonl")))
                .map(|e| e.path.clone()),
        );
    }
    files
}

// ---------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------

/// What became of some Codex sessions, by thread id.
#[derive(Debug, Default, PartialEq)]
pub struct Followed {
    pub titles: Vec<(String, String)>,
    pub archived: Vec<String>,
    /// In Codex's records once, and gone from them now.
    pub deleted: Vec<String>,
}

pub struct Threads {
    home: PathBuf,
    /// Threads found in the records once.
    seen: HashSet<String>,
}

impl Threads {
    pub fn new() -> Threads {
        Threads::at(home())
    }

    pub fn at(home: PathBuf) -> Threads {
        Threads { home, seen: HashSet::new() }
    }

    /// The newest `state_<n>.sqlite`.
    fn database(&self) -> Option<PathBuf> {
        std::fs::read_dir(&self.home)
            .ok()?
            .filter_map(Result::ok)
            .filter_map(|e| {
                let name = e.file_name().into_string().ok()?;
                let n: u32 = name.strip_prefix("state_")?.strip_suffix(".sqlite")?.parse().ok()?;
                Some((n, e.path()))
            })
            .max_by_key(|(n, _)| *n)
            .map(|(_, p)| p)
    }

    /// Looks up the threads of the sessions in memory. Nothing at all when
    /// the records cannot be read: a session is never taken for deleted then.
    pub fn follow(&mut self, ids: &[String]) -> Followed {
        let mut out = Followed::default();
        if ids.is_empty() {
            self.seen.clear();
            return out;
        }
        let Some(rows) = self.database().and_then(|db| read_threads(&db, ids).ok()) else { return out };
        for id in ids {
            match rows.get(id) {
                Some((name, archived)) => {
                    self.seen.insert(id.clone());
                    if let Some(name) = name {
                        out.titles.push((id.clone(), name.clone()));
                    }
                    if *archived {
                        out.archived.push(id.clone());
                    }
                }
                None if self.seen.contains(id) => out.deleted.push(id.clone()),
                None => {}
            }
        }
        self.seen.retain(|id| ids.contains(id));
        out
    }
}

fn read_threads(db: &Path, ids: &[String]) -> rusqlite::Result<HashMap<String, (Option<String>, bool)>> {
    let c = Connection::open_with_flags(db, OpenFlags::SQLITE_OPEN_READ_ONLY | OpenFlags::SQLITE_OPEN_NO_MUTEX)?;
    c.busy_timeout(Duration::from_millis(500))?;
    let marks = vec!["?"; ids.len()].join(",");
    let mut stmt = c.prepare(&format!("SELECT id, NULLIF(TRIM(name), ''), archived FROM threads WHERE id IN ({marks})"))?;
    let rows = stmt.query_map(rusqlite::params_from_iter(ids), |r| Ok((r.get(0)?, (r.get(1)?, r.get::<_, i64>(2)? != 0))))?;
    rows.collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("agentsmon-codex-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn line(ts: &str, kind: &str, payload: Value) -> String {
        format!("{}\n", json!({"timestamp": ts, "type": kind, "payload": payload}))
    }

    fn meta(id: &str, session: &str, source: Value) -> String {
        line("2026-10-27T07:48:07.000Z", "session_meta", json!({"id": id, "session_id": session, "source": source,
            "originator": "Codex Desktop", "cli_version": "0.164.0", "base_instructions": {"text": "…"}}))
    }

    fn request(id: &str, session: &str, thread: &str, input: u64, cached: u64) -> String {
        line("2026-10-27T07:48:17.923Z", "token_usage_record", json!({"thread_id": thread, "session_id": session,
            "response_id": id, "usage": {"input_tokens": input, "cached_input_tokens": cached, "cache_write_input_tokens": 0,
            "output_tokens": 181, "reasoning_output_tokens": 20, "total_tokens": input + 181}}))
    }

    fn count(used: f64) -> String {
        line("2026-10-27T07:48:18.000Z", "event_msg", json!({"type": "token_count",
            "info": {"last_token_usage": {"total_tokens": 47_500}, "model_context_window": 475_000},
            "rate_limits": {"limit_id": "codex", "primary": {"used_percent": used, "window_minutes": 300, "resets_at": 1_793_104_669},
                "secondary": {"used_percent": 4.0, "window_minutes": 10_080, "resets_at": 1_793_607_550}}}))
    }

    fn append(path: &Path, text: &str) {
        use std::io::Write;
        std::fs::OpenOptions::new().create(true).append(true).open(path).unwrap().write_all(text.as_bytes()).unwrap();
    }

    #[test]
    fn counts_each_request_once_and_reads_limits_and_context() {
        let root = temp("rollouts");
        let day = root.join("2026/10/27");
        std::fs::create_dir_all(&day).unwrap();
        let main = day.join("rollout-2026-10-27T15-48-07-s1.jsonl");
        append(&main, &meta("s1", "s1", json!("vscode")));
        append(&main, &line("2026-10-27T07:48:08.000Z", "turn_context", json!({"model": "gpt-6.1-sol"})));
        append(&main, &request("resp_1", "s1", "s1", 30_617, 23_296));
        append(&main, &count(1.0));
        // A message that only mentions a marker is parsed and left alone.
        append(&main, &line("2026-10-27T07:48:19.000Z", "response_item", json!({"text": "\"type\":\"token_count\""})));
        // Not finished being written.
        append(&main, "{\"timestamp\":\"2026-10-27T07:48:20.000Z\",\"type\":\"token_usage");

        let mut r = Rollouts::at(root.clone());
        r.counted = Some(HashMap::new());
        let h = r.scan();
        assert_eq!(h.usage.len(), 1);
        let u = &h.usage[0];
        assert_eq!((u.request_id.as_str(), u.session_id.as_deref(), u.model.as_deref()), ("resp_1", Some("s1"), Some("gpt-6.1-sol")));
        assert_eq!((u.input, u.cache_read, u.output, u.source.as_deref()), (7_321, 23_296, 181, Some("main")));
        assert_eq!(u.ts, 1_793_087_297_923);
        let (sid, _, seen) = &h.seen[0];
        assert_eq!(sid, "s1");
        assert_eq!(seen.context.as_ref().and_then(|c| c.percent), Some(10.0));
        let windows = seen.rate_limits.as_ref().unwrap();
        assert_eq!((windows[0].kind.as_str(), windows[0].resets_at.as_deref()), ("five_hour", Some("2026-10-27T12:37:49Z")));
        assert_eq!(windows[1].kind, "seven_day");
        assert_eq!(seen.meta.as_ref().and_then(|m| m.surface.as_deref()), Some("desktop"));
        let (name, offset, head) = h.cursors[0].clone();
        let head = head.as_deref().and_then(Head::decode);
        assert!(head.is_some());
        assert_eq!(name, "rollout-2026-10-27T15-48-07-s1.jsonl");

        // The rest of the line, a subagent's request in a file of its own.
        append(&main, "_record\",\"payload\":{\"session_id\":\"s1\",\"response_id\":\"resp_2\",\"usage\":{\"input_tokens\":10}}}\n");
        let guardian = day.join("rollout-2026-10-27T15-55-02-g1.jsonl");
        append(&guardian, &meta("g1", "s1", json!({"subagent": {"other": "guardian"}})));
        append(&guardian, &request("resp_3", "s1", "g1", 100, 0));
        append(&guardian, &count(2.0));
        let h = r.scan();
        let mut ids: Vec<(&str, &str)> = h.usage.iter().map(|u| (u.request_id.as_str(), u.source.as_deref().unwrap())).collect();
        ids.sort();
        assert_eq!(ids, vec![("resp_2", "main"), ("resp_3", "guardian")]);
        // The subagent tells the account's limits, not the session's context.
        let (_, _, seen) = h.seen.iter().find(|(_, _, s)| s.rate_limits.is_some()).unwrap();
        assert!(seen.context.is_none() && seen.meta.is_none());
        assert!(r.scan().usage.is_empty());

        // A later run starts from what was counted: it knows the session and
        // the model again, and counts only what is new.
        let mut again = Rollouts::at(root.clone());
        let len = std::fs::metadata(&main).unwrap().len();
        again.counted = Some(HashMap::from([(name.clone(), (offset, head))]));
        append(&main, &request("resp_4", "s1", "s1", 50, 0));
        let h = again.scan();
        let mut ids: Vec<&str> = h.usage.iter().map(|u| u.request_id.as_str()).collect();
        ids.sort();
        assert_eq!(ids, vec!["resp_2", "resp_3", "resp_4"]);
        assert!(h.usage.iter().all(|u| u.request_id != "resp_4" || u.model.as_deref() == Some("gpt-6.1-sol")));
        assert!(h.cursors.iter().any(|(n, o, _)| n == &name && *o > len));

        // Nothing new at all: only who the file belongs to is read.
        let mut idle = Rollouts::at(root.clone());
        let main_len = std::fs::metadata(&main).unwrap().len();
        let guardian_len = std::fs::metadata(&guardian).unwrap().len();
        idle.counted = Some(HashMap::from([
            (name.clone(), (main_len, head)),
            ("rollout-2026-10-27T15-55-02-g1.jsonl".to_string(), (guardian_len, None)),
        ]));
        let h = idle.scan();
        assert!(h.usage.is_empty() && h.cursors.is_empty() && h.seen.is_empty());
        assert!(idle.files.values().all(|f| f.thread.as_ref().is_some_and(|t| t.session_id == "s1")));
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_line_longer_than_a_chunk_is_skipped() {
        let root = temp("long");
        let day = root.join("2026/10/26");
        std::fs::create_dir_all(&day).unwrap();
        let file = day.join("rollout-2026-10-26T23-19-39-s2.jsonl");
        append(&file, &meta("s2", "s2", json!("cli")));
        append(&file, &request("resp_a", "s2", "s2", 10, 0));
        let image = "x".repeat(CHUNK as usize + 100);
        append(&file, &line("2026-10-26T15:20:00.000Z", "response_item", json!({"type": "message", "image": image})));
        append(&file, &request("resp_b", "s2", "s2", 20, 0));

        let mut r = Rollouts::at(root.clone());
        r.counted = Some(HashMap::new());
        let mut ids: Vec<String> = (0..4).flat_map(|_| r.scan().usage).map(|u| u.request_id).collect();
        ids.sort();
        assert_eq!(ids, vec!["resp_a", "resp_b"]);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn a_file_replaced_by_a_longer_one_is_read_from_the_top() {
        let root = temp("replaced");
        let day = root.join("2026/10/27");
        std::fs::create_dir_all(&day).unwrap();
        let file = day.join("rollout-2026-10-27T10-00-00-s3.jsonl");
        append(&file, &meta("s3", "s3", json!("cli")));
        append(&file, &request("resp_x", "s3", "s3", 10, 0));
        let mut r = Rollouts::at(root.clone());
        r.counted = Some(HashMap::new());
        let h = r.scan();
        assert_eq!(h.usage.len(), 1);
        let (name, offset, head) = h.cursors[0].clone();

        // Another thread written under the same name, longer than what was read.
        std::fs::write(&file, meta("s4", "s4", json!("cli"))).unwrap();
        append(&file, &request("resp_y", "s4", "s4", 10, 0));
        append(&file, &request("resp_z", "s4", "s4", 10, 0));
        assert!(std::fs::metadata(&file).unwrap().len() > offset);
        let ids = |h: Harvest| h.usage.into_iter().map(|u| u.request_id).collect::<Vec<_>>();
        assert_eq!(ids(r.scan()), vec!["resp_y", "resp_z"]);

        // So does a later run that kept the first file's cursor.
        let mut again = Rollouts::at(root.clone());
        again.counted = Some(HashMap::from([(name, (offset, head.as_deref().and_then(Head::decode)))]));
        assert_eq!(ids(again.scan()), vec!["resp_y", "resp_z"]);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn other_limits_are_left_alone() {
        assert!(windows_of(&json!({"limit_id": "codex_other", "primary": {"used_percent": 1.0}})).is_none());
        let w = windows_of(&json!({"primary": {"used_percent": 3.5, "window_minutes": 60}, "secondary": null})).unwrap();
        assert_eq!((w.len(), w[0].kind.as_str(), w[0].resets_at.as_deref()), (1, "primary", None));
    }

    #[test]
    fn walks_the_newest_day_folders() {
        let root = temp("walk");
        for (day, file) in [("2026/09/30", "rollout-a.jsonl"), ("2026/10/01", "rollout-b.jsonl"), ("2026/10/02", "rollout-c.jsonl")] {
            std::fs::create_dir_all(root.join(day)).unwrap();
            std::fs::write(root.join(day).join(file), "").unwrap();
            std::fs::write(root.join(day).join("notes.txt"), "").unwrap();
        }
        let names = |days| -> Vec<String> {
            rollout_files(&mut Dirs::default(), &root, days).iter().map(|p| p.file_name().unwrap().to_string_lossy().into_owned()).collect()
        };
        assert_eq!(names(2), vec!["rollout-b.jsonl", "rollout-c.jsonl"]);
        assert_eq!(names(usize::MAX), vec!["rollout-a.jsonl", "rollout-b.jsonl", "rollout-c.jsonl"]);
        assert!(rollout_files(&mut Dirs::default(), &root.join("nowhere"), 2).is_empty());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn follows_names_archiving_and_deletion() {
        let home = temp("threads");
        let db = Connection::open(home.join("state_5.sqlite")).unwrap();
        db.execute_batch(
            "CREATE TABLE threads (id TEXT PRIMARY KEY, title TEXT NOT NULL, name TEXT, archived INTEGER NOT NULL DEFAULT 0);
             INSERT INTO threads VALUES ('a', 'first prompt', 'Fix the bug', 0), ('b', 'hi', NULL, 1), ('c', 'x', ' ', 0);",
        )
        .unwrap();
        // An older database is not the one Codex uses.
        std::fs::write(home.join("state_4.sqlite"), "").unwrap();
        let mut t = Threads::at(home.clone());
        let ids = |v: &[&str]| v.iter().map(|s| s.to_string()).collect::<Vec<_>>();
        let f = t.follow(&ids(&["a", "b", "c", "new"]));
        assert_eq!(f, Followed { titles: vec![("a".into(), "Fix the bug".into())], archived: vec!["b".into()], deleted: vec![] });
        db.execute("DELETE FROM threads WHERE id IN ('a', 'new')", []).unwrap();
        assert_eq!(t.follow(&ids(&["a", "b", "new"])).deleted, vec!["a".to_string()]);
        assert_eq!(Threads::at(home.join("nowhere")).follow(&ids(&["a"])), Followed::default());
        std::fs::remove_dir_all(&home).ok();
    }
}
