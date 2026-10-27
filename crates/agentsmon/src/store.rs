//! SQLite persistence: session snapshots, the event history, quota readings
//! and the token ledger. Writes go to one thread that batches them into transactions, so
//! the ingest path only ever pushes onto a channel; reads use their own
//! connection (WAL lets them run beside the writer).

use std::collections::HashMap;
use std::path::Path;
use std::sync::Arc;
use std::sync::mpsc::{self, Receiver, Sender};

use agentsmon_protocol::{Event, EventRecord, Quota, QuotaSample, Session, TokenGroup, TokenPoint, TokenTotals, UsageRecord};
use parking_lot::Mutex;
use rusqlite::{Connection, OptionalExtension, params};

const SCHEMA: &str = "
CREATE TABLE IF NOT EXISTS sessions (
    key TEXT PRIMARY KEY,
    agent TEXT NOT NULL,
    status TEXT NOT NULL,
    started_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    ended_at INTEGER,
    data TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS sessions_updated ON sessions(updated_at DESC);
CREATE TABLE IF NOT EXISTS events (
    seq INTEGER PRIMARY KEY,
    ts INTEGER NOT NULL,
    key TEXT NOT NULL,
    agent TEXT NOT NULL,
    type TEXT NOT NULL,
    data TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS events_key ON events(key, seq);
CREATE INDEX IF NOT EXISTS events_ts ON events(ts);
CREATE TABLE IF NOT EXISTS quotas (
    agent TEXT PRIMARY KEY,
    data TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS quota_samples (
    agent TEXT NOT NULL,
    kind TEXT NOT NULL,
    ts INTEGER NOT NULL,
    percent REAL NOT NULL,
    resets_at TEXT
);
CREATE INDEX IF NOT EXISTS quota_samples_agent_ts ON quota_samples(agent, ts);
-- One row per model request, counted once by its request id.
CREATE TABLE IF NOT EXISTS token_usage (
    request_id TEXT PRIMARY KEY,
    ts INTEGER NOT NULL,
    agent TEXT NOT NULL,
    session_key TEXT,
    model TEXT NOT NULL,
    source TEXT NOT NULL,
    input INTEGER NOT NULL,
    output INTEGER NOT NULL,
    cache_read INTEGER NOT NULL,
    cache_write INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS token_usage_ts ON token_usage(ts);
-- Requests older than the detail window, summed per hour, then per day:
-- totals stay exact, windows reaching that far back are exact to the hour or
-- the day.
CREATE TABLE IF NOT EXISTS token_hourly (
    hour INTEGER NOT NULL,
    agent TEXT NOT NULL,
    model TEXT NOT NULL,
    source TEXT NOT NULL,
    input INTEGER NOT NULL,
    output INTEGER NOT NULL,
    cache_read INTEGER NOT NULL,
    cache_write INTEGER NOT NULL,
    requests INTEGER NOT NULL,
    PRIMARY KEY (hour, agent, model, source)
);
CREATE TABLE IF NOT EXISTS token_daily (
    day INTEGER NOT NULL,
    agent TEXT NOT NULL,
    model TEXT NOT NULL,
    source TEXT NOT NULL,
    input INTEGER NOT NULL,
    output INTEGER NOT NULL,
    cache_read INTEGER NOT NULL,
    cache_write INTEGER NOT NULL,
    requests INTEGER NOT NULL,
    PRIMARY KEY (day, agent, model, source)
);
";

/// The token tables, finest first: the table, its time column, and what
/// counts its requests.
const TOKEN_TIERS: [(&str, &str, &str); 3] = [
    ("token_usage", "ts", "COUNT(*)"),
    ("token_hourly", "hour", "SUM(requests)"),
    ("token_daily", "day", "SUM(requests)"),
];

const HOUR_MS: i64 = 3_600_000;
const DAY_MS: i64 = 86_400_000;

/// What one maintenance pass removes or folds: everything older than each
/// cut-off, in ms since the epoch.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Cutoffs {
    pub events: i64,
    pub quota_samples: i64,
    /// Requests older than this are summed per hour.
    pub token_detail: i64,
    /// Hours older than this are summed per day.
    pub token_hourly: i64,
    /// Days older than this are dropped: the end of the token history.
    pub token_daily: i64,
}

/// Writes the store thread takes.
pub enum Op {
    Event(EventRecord),
    Session(Box<Session>),
    Quota(Quota),
    Sample(QuotaSample),
    Forget(String),
    /// Sets the title of a stored session that is not in memory.
    Title { key: String, title: String },
    Usage(UsageRecord),
    /// Prunes and folds by the cut-offs, then gives the space back to the
    /// file system.
    Maintain(Cutoffs),
    /// Answered once everything sent before it is committed.
    Flush(Sender<()>),
}

#[derive(Clone)]
pub struct Store {
    tx: Sender<Op>,
    reader: Arc<Mutex<Connection>>,
    path: std::path::PathBuf,
}

impl Store {
    pub fn open(path: &Path) -> rusqlite::Result<Store> {
        if let Some(dir) = path.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        let writer = Connection::open(path)?;
        // Freed pages go back to the file system (VACUUM once to switch an
        // older file over), and the write-ahead log is cut back after use.
        let auto_vacuum: i64 = writer.pragma_query_value(None, "auto_vacuum", |r| r.get(0))?;
        if auto_vacuum != 2 {
            writer.pragma_update(None, "auto_vacuum", "INCREMENTAL")?;
            writer.execute_batch("VACUUM")?;
        }
        writer.pragma_update(None, "journal_mode", "WAL")?;
        writer.pragma_update(None, "synchronous", "NORMAL")?;
        writer.pragma_update(None, "journal_size_limit", 4 * 1024 * 1024)?;
        writer.execute_batch(SCHEMA)?;
        let reader = Connection::open(path)?;
        reader.pragma_update(None, "query_only", true)?;

        let (tx, rx) = mpsc::channel();
        std::thread::Builder::new()
            .name("agentsmon-store".into())
            .spawn(move || write_loop(writer, rx))
            .expect("store thread");
        Ok(Store { tx, reader: Arc::new(Mutex::new(reader)), path: path.to_path_buf() })
    }

    /// What the database takes on disk, its write-ahead log included.
    pub fn disk_bytes(&self) -> u64 {
        ["", "-wal", "-shm"]
            .iter()
            .filter_map(|suffix| {
                let mut p = self.path.clone().into_os_string();
                p.push(suffix);
                std::fs::metadata(p).ok().map(|m| m.len())
            })
            .sum()
    }

    pub fn send(&self, op: Op) {
        // The thread only stops with the process.
        let _ = self.tx.send(op);
    }

    /// Waits until every write sent so far is committed, at most `timeout`.
    /// Returns whether it got there.
    pub fn flush(&self, timeout: std::time::Duration) -> bool {
        let (done, wait) = mpsc::channel();
        self.send(Op::Flush(done));
        wait.recv_timeout(timeout).is_ok()
    }

    /// Runs a read on the reader connection.
    pub fn read<T>(&self, f: impl FnOnce(&Connection) -> rusqlite::Result<T>) -> rusqlite::Result<T> {
        f(&self.reader.lock())
    }
}

fn write_loop(mut conn: Connection, rx: Receiver<Op>) {
    while let Ok(first) = rx.recv() {
        let mut ops = vec![first];
        while ops.len() < 1024 {
            match rx.try_recv() {
                Ok(op) => ops.push(op),
                Err(_) => break,
            }
        }
        let mut flushes = Vec::new();
        ops.retain_mut(|op| match op {
            Op::Flush(done) => {
                flushes.push(done.clone());
                false
            }
            _ => true,
        });
        let maintained = ops.iter().any(|op| matches!(op, Op::Maintain(_)));
        if !ops.is_empty()
            && let Err(e) = write_batch(&mut conn, ops)
        {
            tracing::error!("store write failed: {e}");
        }
        if maintained && let Err(e) = compact(&conn) {
            tracing::warn!("store compaction failed: {e}");
        }
        for done in flushes {
            let _ = done.send(());
        }
    }
}

fn write_batch(conn: &mut Connection, ops: Vec<Op>) -> rusqlite::Result<()> {
    // A session changed many times in one batch is written once.
    let mut sessions: HashMap<String, Box<Session>> = HashMap::new();
    let mut titles: Vec<(String, String)> = Vec::new();
    let tx = conn.transaction()?;
    for op in ops {
        match op {
            Op::Event(r) => {
                let data = serde_json::to_string(&r.event).unwrap_or_default();
                tx.prepare_cached("INSERT OR REPLACE INTO events (seq, ts, key, agent, type, data) VALUES (?1, ?2, ?3, ?4, ?5, ?6)")?
                    .execute(params![r.seq as i64, r.ts, r.key, r.agent, r.event.name(), data])?;
            }
            Op::Session(s) => {
                sessions.insert(s.key.clone(), s);
            }
            Op::Quota(q) => {
                let data = serde_json::to_string(&q).unwrap_or_default();
                tx.prepare_cached("INSERT OR REPLACE INTO quotas (agent, data) VALUES (?1, ?2)")?.execute(params![q.agent, data])?;
            }
            Op::Sample(s) => {
                tx.prepare_cached("INSERT INTO quota_samples (agent, kind, ts, percent, resets_at) VALUES (?1, ?2, ?3, ?4, ?5)")?
                    .execute(params![s.agent, s.kind, s.ts, s.percent_used, s.resets_at])?;
            }
            Op::Forget(key) => {
                sessions.remove(&key);
                tx.execute("DELETE FROM sessions WHERE key = ?1", [&key])?;
                tx.execute("DELETE FROM events WHERE key = ?1", [&key])?;
            }
            Op::Usage(u) => {
                tx.prepare_cached(
                    "INSERT OR IGNORE INTO token_usage (request_id, ts, agent, session_key, model, source, input, output, cache_read, cache_write)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
                )?
                .execute(params![
                    u.request_id,
                    u.ts,
                    u.agent,
                    u.session_id.as_ref().map(|sid| Session::key_for(&u.agent, sid)),
                    u.model.as_deref().unwrap_or(""),
                    u.source.as_deref().unwrap_or(""),
                    u.input as i64,
                    u.output as i64,
                    u.cache_read as i64,
                    u.cache_write as i64,
                ])?;
            }
            Op::Title { key, title } => titles.push((key, title)),
            Op::Flush(_) => {}
            Op::Maintain(c) => {
                tx.execute("DELETE FROM events WHERE ts < ?1", [c.events])?;
                tx.execute("DELETE FROM quota_samples WHERE ts < ?1", [c.quota_samples])?;
                // Whole hours and whole days only, so a bucket is never split
                // between two tiers.
                fold(&tx, "token_usage", "ts", "COUNT(*)", "token_hourly", "hour", HOUR_MS, c.token_detail / HOUR_MS * HOUR_MS)?;
                fold(&tx, "token_hourly", "hour", "SUM(requests)", "token_daily", "day", DAY_MS, c.token_hourly / DAY_MS * DAY_MS)?;
                tx.execute("DELETE FROM token_daily WHERE day < ?1", [c.token_daily])?;
            }
        }
    }
    for s in sessions.into_values() {
        let data = serde_json::to_string(&s).unwrap_or_default();
        tx.prepare_cached(
            "INSERT OR REPLACE INTO sessions (key, agent, status, started_at, updated_at, ended_at, data) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
        )?
        .execute(params![s.key, s.agent, s.status.as_str(), s.started_at, s.updated_at, s.ended_at, data])?;
    }
    for (key, title) in titles {
        tx.prepare_cached("UPDATE sessions SET data = json_set(data, '$.meta.title', ?2) WHERE key = ?1")?.execute(params![key, title])?;
    }
    tx.commit()
}

/// Moves the rows of `from` older than `before` into `to`, summed per
/// `bucket`; the totals do not change.
#[allow(clippy::too_many_arguments)]
fn fold(
    tx: &rusqlite::Transaction<'_>,
    from: &str,
    from_time: &str,
    requests: &str,
    to: &str,
    to_time: &str,
    bucket: i64,
    before: i64,
) -> rusqlite::Result<()> {
    tx.execute(
        &format!(
            "INSERT INTO {to} ({to_time}, agent, model, source, input, output, cache_read, cache_write, requests)
             SELECT {from_time} / ?2 * ?2, agent, model, source, SUM(input), SUM(output), SUM(cache_read), SUM(cache_write), {requests}
             FROM {from} WHERE {from_time} < ?1 GROUP BY 1, 2, 3, 4
             ON CONFLICT ({to_time}, agent, model, source) DO UPDATE SET
                input = input + excluded.input, output = output + excluded.output,
                cache_read = cache_read + excluded.cache_read, cache_write = cache_write + excluded.cache_write,
                requests = requests + excluded.requests"
        ),
        [before, bucket],
    )?;
    tx.execute(&format!("DELETE FROM {from} WHERE {from_time} < ?1"), [before])?;
    Ok(())
}

/// Gives freed pages back and cuts the write-ahead log down.
fn compact(conn: &Connection) -> rusqlite::Result<()> {
    conn.execute_batch("PRAGMA incremental_vacuum;")?;
    conn.query_row("PRAGMA wal_checkpoint(TRUNCATE)", [], |_| Ok(()))
}

// ---------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------

fn parse_sessions(rows: impl Iterator<Item = rusqlite::Result<String>>) -> rusqlite::Result<Vec<Session>> {
    let mut out = Vec::new();
    for data in rows {
        if let Ok(s) = serde_json::from_str(&data?) {
            out.push(s);
        }
    }
    Ok(out)
}

/// Sessions still running, or ended at or after `ended_since`.
pub fn live_sessions(c: &Connection, ended_since: i64) -> rusqlite::Result<Vec<Session>> {
    let mut stmt = c.prepare("SELECT data FROM sessions WHERE ended_at IS NULL OR ended_at >= ?1 ORDER BY updated_at DESC LIMIT 2000")?;
    parse_sessions(stmt.query_map([ended_since], |r| r.get(0))?)
}

pub fn list_sessions(c: &Connection, agent: Option<&str>, limit: u32, offset: u32) -> rusqlite::Result<Vec<Session>> {
    let mut stmt = c.prepare_cached(
        "SELECT data FROM sessions WHERE (?1 IS NULL OR agent = ?1) ORDER BY updated_at DESC LIMIT ?2 OFFSET ?3",
    )?;
    parse_sessions(stmt.query_map(params![agent, limit, offset], |r| r.get(0))?)
}

pub fn session(c: &Connection, key: &str) -> rusqlite::Result<Option<Session>> {
    let data: Option<String> = c.query_row("SELECT data FROM sessions WHERE key = ?1", [key], |r| r.get(0)).optional()?;
    Ok(data.and_then(|d| serde_json::from_str(&d).ok()))
}

pub fn events(c: &Connection, key: Option<&str>, before: Option<u64>, limit: u32) -> rusqlite::Result<Vec<EventRecord>> {
    let mut stmt = c.prepare_cached(
        "SELECT seq, ts, key, agent, data FROM events
         WHERE (?1 IS NULL OR key = ?1) AND (?2 IS NULL OR seq < ?2)
         ORDER BY seq DESC LIMIT ?3",
    )?;
    let rows = stmt.query_map(params![key, before.map(|b| b as i64), limit], |r| {
        Ok((r.get::<_, i64>(0)?, r.get::<_, i64>(1)?, r.get::<_, String>(2)?, r.get::<_, String>(3)?, r.get::<_, String>(4)?))
    })?;
    let mut out = Vec::new();
    for row in rows {
        let (seq, ts, key, agent, data) = row?;
        if let Ok(event) = serde_json::from_str::<Event>(&data) {
            out.push(EventRecord { seq: seq as u64, ts, key, agent, event });
        }
    }
    Ok(out)
}

pub fn max_seq(c: &Connection) -> rusqlite::Result<u64> {
    c.query_row("SELECT COALESCE(MAX(seq), 0) FROM events", [], |r| r.get::<_, i64>(0)).map(|n| n as u64)
}

pub fn quotas(c: &Connection) -> rusqlite::Result<Vec<Quota>> {
    let mut stmt = c.prepare("SELECT data FROM quotas")?;
    let rows = stmt.query_map([], |r| r.get::<_, String>(0))?;
    Ok(rows.filter_map(|d| d.ok().and_then(|d| serde_json::from_str(&d).ok())).collect())
}

pub fn quota_history(c: &Connection, agent: Option<&str>, since: i64) -> rusqlite::Result<Vec<QuotaSample>> {
    let mut stmt = c.prepare_cached(
        "SELECT agent, kind, ts, percent, resets_at FROM quota_samples
         WHERE (?1 IS NULL OR agent = ?1) AND ts >= ?2 ORDER BY ts ASC LIMIT 20000",
    )?;
    let rows = stmt.query_map(params![agent, since], |r| {
        Ok(QuotaSample { agent: r.get(0)?, kind: r.get(1)?, ts: r.get(2)?, percent_used: r.get(3)?, resets_at: r.get(4)? })
    })?;
    rows.collect()
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TokenGroupBy {
    Agent,
    Model,
    Source,
    Session,
}

impl TokenGroupBy {
    pub fn parse(s: &str) -> Option<TokenGroupBy> {
        match s {
            "agent" => Some(TokenGroupBy::Agent),
            "model" => Some(TokenGroupBy::Model),
            "source" => Some(TokenGroupBy::Source),
            "session" => Some(TokenGroupBy::Session),
            _ => None,
        }
    }

    /// The grouping column in the detail table and in the hourly one, which
    /// keeps no sessions.
    fn columns(group: Option<TokenGroupBy>) -> (&'static str, &'static str) {
        match group {
            None => ("''", "''"),
            Some(TokenGroupBy::Agent) => ("agent", "agent"),
            Some(TokenGroupBy::Model) => ("model", "model"),
            Some(TokenGroupBy::Source) => ("source", "source"),
            Some(TokenGroupBy::Session) => ("COALESCE(session_key, '')", "''"),
        }
    }
}

fn totals_row(r: &rusqlite::Row<'_>) -> rusqlite::Result<(String, TokenTotals)> {
    let n = |i: usize| r.get::<_, Option<i64>>(i).map(|v| v.unwrap_or(0).max(0) as u64);
    Ok((
        r.get(0)?,
        TokenTotals { input: n(1)?, output: n(2)?, cache_read: n(3)?, cache_write: n(4)?, requests: n(5)? },
    ))
}

/// Token totals over `[since, until)`, grouped when asked; groups sorted by
/// their total, largest first.
pub fn token_report(
    c: &Connection,
    since: i64,
    until: i64,
    group: Option<TokenGroupBy>,
) -> rusqlite::Result<(TokenTotals, Vec<TokenGroup>)> {
    let (detail, summed) = TokenGroupBy::columns(group);
    let sums = "SUM(input), SUM(output), SUM(cache_read), SUM(cache_write)";
    let mut by: HashMap<String, TokenTotals> = HashMap::new();
    for (i, (table, time, requests)) in TOKEN_TIERS.iter().enumerate() {
        let key = if i == 0 { detail } else { summed };
        let sql = format!("SELECT {key}, {sums}, {requests} FROM {table} WHERE {time} >= ?1 AND {time} < ?2 GROUP BY 1");
        let mut stmt = c.prepare_cached(&sql)?;
        for row in stmt.query_map([since, until], totals_row)? {
            let (key, t) = row?;
            by.entry(key).or_default().add(&t);
        }
    }
    let mut totals = TokenTotals::default();
    for t in by.values() {
        totals.add(t);
    }
    let mut groups: Vec<TokenGroup> = match group {
        None => Vec::new(),
        Some(_) => by.into_iter().map(|(key, totals)| TokenGroup { key, totals }).collect(),
    };
    groups.sort_by(|a, b| b.totals.total().cmp(&a.totals.total()).then_with(|| a.key.cmp(&b.key)));
    Ok((totals, groups))
}

/// Token totals per `bucket` ms from `since`, buckets with nothing left out.
pub fn token_series(c: &Connection, since: i64, until: i64, bucket: i64) -> rusqlite::Result<Vec<TokenPoint>> {
    let sums = "SUM(input), SUM(output), SUM(cache_read), SUM(cache_write)";
    let mut by: std::collections::BTreeMap<i64, TokenTotals> = std::collections::BTreeMap::new();
    for (table, time, requests) in TOKEN_TIERS {
        let sql =
            format!("SELECT ({time} - ?1) / ?3 * ?3 + ?1, {sums}, {requests} FROM {table} WHERE {time} >= ?1 AND {time} < ?2 GROUP BY 1");
        let mut stmt = c.prepare_cached(&sql)?;
        let rows = stmt.query_map([since, until, bucket], |r| {
            let n = |i: usize| r.get::<_, Option<i64>>(i).map(|v| v.unwrap_or(0).max(0) as u64);
            Ok((r.get::<_, i64>(0)?, TokenTotals { input: n(1)?, output: n(2)?, cache_read: n(3)?, cache_write: n(4)?, requests: n(5)? }))
        })?;
        for row in rows {
            let (t, totals) = row?;
            by.entry(t).or_default().add(&totals);
        }
    }
    Ok(by.into_iter().map(|(t, totals)| TokenPoint { t, totals }).collect())
}

pub fn last_usage_ts(c: &Connection) -> rusqlite::Result<Option<i64>> {
    c.query_row("SELECT MAX(ts) FROM token_usage", [], |r| r.get(0))
}

/// Ended sessions: key, when it ended, and the transcript Claude Code keeps
/// for it, if the daemon was told where.
pub fn ended_sessions(c: &Connection) -> rusqlite::Result<Vec<(String, i64, Option<String>)>> {
    let mut stmt =
        c.prepare("SELECT key, ended_at, json_extract(data, '$.meta.transcript_path') FROM sessions WHERE ended_at IS NOT NULL")?;
    stmt.query_map([], |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?)))?.collect()
}

/// Stored sessions with a transcript but no title yet, most recent first.
pub fn untitled_sessions(c: &Connection) -> rusqlite::Result<Vec<(String, String)>> {
    let mut stmt = c.prepare(
        "SELECT key, json_extract(data, '$.meta.transcript_path') AS path FROM sessions
         WHERE path IS NOT NULL AND json_extract(data, '$.meta.title') IS NULL ORDER BY updated_at DESC LIMIT 500",
    )?;
    stmt.query_map([], |r| Ok((r.get(0)?, r.get(1)?)))?.collect()
}

/// Request ids counted since `since`, so a delivery repeated across a
/// restart still counts once.
pub fn recent_request_ids(c: &Connection, since: i64) -> rusqlite::Result<Vec<String>> {
    let mut stmt = c.prepare("SELECT request_id FROM token_usage WHERE ts >= ?1")?;
    stmt.query_map([since], |r| r.get(0))?.collect()
}

#[derive(serde::Serialize)]
pub struct Counts {
    pub sessions: u64,
    pub events: u64,
    pub quota_samples: u64,
    pub token_requests: u64,
}

pub fn counts(c: &Connection) -> rusqlite::Result<Counts> {
    let n = |sql: &str| c.query_row(sql, [], |r| r.get::<_, i64>(0)).map(|n| n as u64);
    Ok(Counts {
        sessions: n("SELECT COUNT(*) FROM sessions")?,
        events: n("SELECT COUNT(*) FROM events")?,
        quota_samples: n("SELECT COUNT(*) FROM quota_samples")?,
        token_requests: n("SELECT COUNT(*) FROM token_usage")?
            + n("SELECT COALESCE(SUM(requests), 0) FROM token_hourly")?
            + n("SELECT COALESCE(SUM(requests), 0) FROM token_daily")?,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::reducer::new_session;

    #[test]
    fn writes_land_and_read_back() {
        let dir = std::env::temp_dir().join(format!("agentsmon-store-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let store = Store::open(&dir.join("t.db")).unwrap();
        let s = new_session("claude-code", "s1", 1);
        store.send(Op::Session(Box::new(s.clone())));
        store.send(Op::Event(EventRecord {
            seq: 1,
            ts: 1,
            key: s.key.clone(),
            agent: s.agent.clone(),
            event: Event::Prompt { preview: Some("hi".into()) },
        }));
        // The writer is asynchronous: wait for it.
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        loop {
            let n = store.read(counts).unwrap();
            if n.events == 1 && n.sessions == 1 {
                break;
            }
            assert!(std::time::Instant::now() < deadline, "store never wrote");
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
        let back = store.read(|c| session(c, &s.key)).unwrap().unwrap();
        assert_eq!(back, s);
        let evs = store.read(|c| events(c, Some(&s.key), None, 10)).unwrap();
        assert_eq!(evs[0].event.name(), "prompt");
        assert_eq!(store.read(max_seq).unwrap(), 1);
        std::fs::remove_dir_all(&dir).ok();
    }

    fn usage(id: &str, ts: i64, model: &str, output: u64) -> UsageRecord {
        UsageRecord {
            request_id: id.into(),
            ts,
            agent: "claude-code".into(),
            session_id: Some("s1".into()),
            model: Some(model.into()),
            source: Some("main".into()),
            input: 1,
            output,
            cache_read: 100,
            cache_write: 10,
        }
    }

    fn wait_for(store: &Store, requests: u64) {
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        while store.read(counts).unwrap().token_requests != requests {
            assert!(std::time::Instant::now() < deadline, "store never wrote");
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
    }

    #[test]
    fn the_ledger_counts_each_request_once_and_keeps_totals_through_rollup() {
        let dir = std::env::temp_dir().join(format!("agentsmon-ledger-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let store = Store::open(&dir.join("t.db")).unwrap();
        let hour = 3_600_000;
        store.send(Op::Usage(usage("a", 10 * hour + 5, "opus", 7)));
        store.send(Op::Usage(usage("a", 10 * hour + 5, "opus", 7)));
        store.send(Op::Usage(usage("b", 10 * hour + 9, "haiku", 3)));
        store.send(Op::Usage(usage("c", 12 * hour, "opus", 5)));
        wait_for(&store, 3);

        let (all, by_model) = store.read(|c| token_report(c, 0, i64::MAX, Some(TokenGroupBy::Model))).unwrap();
        assert_eq!((all.output, all.requests, all.total()), (15, 3, 15 + 3 + 300 + 30));
        assert_eq!(by_model[0].key, "opus");
        assert_eq!(by_model[0].totals.output, 12);

        // Old requests move into the hourly table; the totals do not change.
        store.send(Op::Maintain(Cutoffs { events: 0, quota_samples: 0, token_detail: 11 * hour, token_hourly: 0, token_daily: 0 }));
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        while store.read(|c| c.query_row("SELECT COUNT(*) FROM token_usage", [], |r| r.get::<_, i64>(0))).unwrap() != 1 {
            assert!(std::time::Instant::now() < deadline);
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
        let (after, _) = store.read(|c| token_report(c, 0, i64::MAX, None)).unwrap();
        assert_eq!(after, all);
        let (window, _) = store.read(|c| token_report(c, 12 * hour, 13 * hour, None)).unwrap();
        assert_eq!(window.output, 5);
        let series = store.read(|c| token_series(c, 0, i64::MAX, hour)).unwrap();
        assert_eq!(series.iter().map(|p| (p.t / hour, p.totals.output)).collect::<Vec<_>>(), vec![(10, 10), (12, 5)]);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn flush_waits_for_everything_sent_before_it() {
        let dir = std::env::temp_dir().join(format!("agentsmon-flush-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let store = Store::open(&dir.join("t.db")).unwrap();
        for i in 0..500 {
            store.send(Op::Usage(usage(&format!("f{i}"), i, "opus", 1)));
        }
        assert!(store.flush(std::time::Duration::from_secs(5)));
        // No waiting loop: the flush answered only after the commit.
        assert_eq!(store.read(counts).unwrap().token_requests, 500);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn tokens_fold_hour_then_day_then_end_and_the_file_shrinks() {
        let dir = std::env::temp_dir().join(format!("agentsmon-tiers-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let store = Store::open(&dir.join("t.db")).unwrap();
        let day = DAY_MS;
        // Three days, a few requests each, plus padding so there is something to give back.
        for d in 0..3 {
            for i in 0..400 {
                store.send(Op::Usage(usage(&format!("d{d}-{i}"), d * day + i * 60_000, "opus", 1)));
            }
        }
        assert!(store.flush(std::time::Duration::from_secs(5)));
        let before = store.read(|c| token_report(c, 0, i64::MAX, None)).unwrap().0;
        let size_before = store.disk_bytes();

        // Day 0 and 1 leave the detail tier; day 0 also leaves the hourly tier.
        let cut = Cutoffs { events: 0, quota_samples: 0, token_detail: 2 * day, token_hourly: day, token_daily: 0 };
        store.send(Op::Maintain(cut));
        assert!(store.flush(std::time::Duration::from_secs(5)));
        let tiers = store
            .read(|c| {
                let n = |t: &str| c.query_row(&format!("SELECT COUNT(*) FROM {t}"), [], |r| r.get::<_, i64>(0));
                Ok((n("token_usage")?, n("token_hourly")?, n("token_daily")?))
            })
            .unwrap();
        assert_eq!(tiers.0, 400, "day 2 stays per request");
        assert!(tiers.1 > 0 && tiers.2 == 1, "day 1 per hour, day 0 per day: {tiers:?}");
        assert_eq!(store.read(|c| token_report(c, 0, i64::MAX, None)).unwrap().0, before);
        let (one_day, _) = store.read(|c| token_report(c, 0, day, None)).unwrap();
        assert_eq!(one_day.requests, 400);

        // The end of the history drops whole days.
        store.send(Op::Maintain(Cutoffs { token_daily: day, ..cut }));
        assert!(store.flush(std::time::Duration::from_secs(5)));
        assert_eq!(store.read(|c| token_report(c, 0, i64::MAX, None)).unwrap().0.requests, 800);
        assert!(store.disk_bytes() <= size_before, "compaction must not grow the file");
        std::fs::remove_dir_all(&dir).ok();
    }
}
