//! What Claude Code writes about its sessions and its account, read without
//! its plugin or its OpenTelemetry export.
//!
//! - **Transcripts.** Claude Code writes each session to
//!   `<config>/projects/<project>/<session id>.jsonl`, and each of its
//!   subagents to `<session id>/subagents/agent-<id>.jsonl` beside it. Every
//!   reply of the model is an `assistant` line per content block, each with
//!   the reply's `usage` and the request's `requestId`: the id Claude Code's
//!   OpenTelemetry `api_request` event carries, so a request counts once
//!   whichever source brings it. Requests whose reply is not part of the
//!   conversation (compaction, the small model's web search and fetch,
//!   titles) are in no transcript: only the OpenTelemetry export has them.
//!   [`Transcripts`] reads what each file gained, as [`crate::codex`] reads
//!   rollouts, and keeps in the store how far each one is counted.
//! - **Usage limits.** Claude Code's global config keeps the account's limits
//!   as it last fetched them (`cachedUsageUtilization`). It fetches them for
//!   `/usage`, not as it works, so while Claude Code is in use and no fresher
//!   reading has come (the plugin reports one after every turn), [`Limits`]
//!   runs `claude -p /usage`, which asks no model, to fetch them again.
//!
//! Neither format is public: everything here is read-only and best effort,
//! and what does not read as expected is skipped.

use std::collections::{HashMap, HashSet, VecDeque};
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant, SystemTime};

use agentsmon_protocol::{RateWindow, UsageRecord, agents};
use serde::Deserialize;
use serde_json::Value;

use crate::hub::Hub;
use crate::store::{self, Op};
use crate::tail::{self, Dirs, Head, Position};
use crate::util::{format_utc_ms, now_ms, parse_utc_ms};

/// At most this much of one file is read per pass.
const CHUNK: u64 = 8 << 20;
/// Files written to this recently are looked at on every pass; the rest on
/// the full walk.
const HOT: Duration = Duration::from_secs(24 * 3_600);
const FULL_WALK_EVERY: u32 = 30;
/// Lines worth parsing name it; everything else is skipped unread.
const MARKER: &str = "\"type\":\"assistant\"";
/// Request ids remembered per file: one reply spans a line per content block,
/// and those lines follow each other. The ledger catches anything older.
const RECENT: usize = 8;
/// What transcripts' cursors are named under, beside Codex's rollouts.
const CURSOR_PREFIX: &str = "claude/";

/// Claude Code's folder: `CLAUDE_CONFIG_DIR`, else `~/.claude`.
pub fn home() -> PathBuf {
    std::env::var_os("CLAUDE_CONFIG_DIR").map(PathBuf::from).unwrap_or_else(|| user_home().join(".claude"))
}

/// Its global config: in `CLAUDE_CONFIG_DIR`, else `~/.claude.json`.
fn config_file() -> PathBuf {
    match std::env::var_os("CLAUDE_CONFIG_DIR") {
        Some(dir) => PathBuf::from(dir).join(".claude.json"),
        None => user_home().join(".claude.json"),
    }
}

fn user_home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default()
}

// ---------------------------------------------------------------------------
// Transcripts
// ---------------------------------------------------------------------------

#[derive(Default)]
struct Transcript {
    /// How far it is read in this process.
    pos: Position,
    /// Bytes whose requests are counted, in this run or an earlier one.
    counted: u64,
    /// How the file counted is known again.
    head: Option<Head>,
    first_read: bool,
    len: u64,
    modified: Option<SystemTime>,
    /// The request ids of the latest replies.
    recent: VecDeque<String>,
}

/// What one pass read.
#[derive(Debug, Default)]
pub struct Harvest {
    pub usage: Vec<UsageRecord>,
    /// How far each file read is now counted, and its head.
    pub cursors: Vec<(String, u64, Option<String>)>,
    /// A transcript read before grew: Claude Code is at work.
    pub grew: bool,
}

pub struct Transcripts {
    root: PathBuf,
    dirs: Dirs,
    files: HashMap<PathBuf, Transcript>,
    /// From the store, at the first pass.
    counted: Option<HashMap<String, (u64, Option<Head>)>>,
    passes: u32,
    /// When a transcript last grew while the daemon watched.
    pub active_at: Option<i64>,
}

impl Transcripts {
    pub fn new() -> Transcripts {
        Transcripts::at(home().join("projects"))
    }

    pub fn at(root: PathBuf) -> Transcripts {
        Transcripts { root, dirs: Dirs::default(), files: HashMap::new(), counted: None, passes: 0, active_at: None }
    }

    /// Reads what the transcripts gained and hands the requests to the hub.
    pub fn pass(&mut self, hub: &Hub) {
        if self.counted.is_none() {
            match hub.store().read(store::log_cursors) {
                Ok(rows) => {
                    self.counted = Some(
                        rows.into_iter()
                            .filter(|(name, _, _)| name.starts_with(CURSOR_PREFIX))
                            .map(|(name, offset, head)| (name, (offset, head.as_deref().and_then(Head::decode))))
                            .collect(),
                    )
                }
                Err(e) => {
                    tracing::warn!("claude: could not read log cursors: {e}");
                    return;
                }
            }
        }
        let mut harvest = self.scan();
        if harvest.grew {
            self.active_at = Some(now_ms());
        }
        if !harvest.usage.is_empty() {
            // What the ledger holds already: the first lines of a reply read
            // before a restart, the history a forked session copies, and what
            // the OpenTelemetry export brought.
            let ids: Vec<String> = harvest.usage.iter().map(|u| u.request_id.clone()).collect();
            match hub.store().read(|c| store::known_requests(c, &ids)) {
                Ok(known) => harvest.usage.retain(|u| !known.contains(&u.request_id)),
                Err(e) => tracing::debug!("claude: could not look up counted requests: {e}"),
            }
            hub.ingest_usage(harvest.usage);
        }
        // After the requests, so a cursor never runs ahead of what it counted.
        let now = now_ms();
        for (name, offset, head) in harvest.cursors {
            hub.store().send(Op::Cursor { name, offset, head, ts: now });
        }
    }

    /// Reads what every transcript gained since the last pass.
    pub fn scan(&mut self) -> Harvest {
        let full = self.passes.is_multiple_of(FULL_WALK_EVERY);
        self.passes = self.passes.wrapping_add(1);
        let now = SystemTime::now();
        let hot = |t: &Transcript| t.modified.and_then(|m| now.duration_since(m).ok()).is_none_or(|age| age < HOT);
        let mut paths: HashSet<PathBuf> = HashSet::new();
        // One folder per project, a transcript per session in it.
        let projects: Vec<PathBuf> = self.dirs.list(&self.root).iter().filter(|e| e.is_dir).map(|e| e.path.clone()).collect();
        for project in projects {
            // A session new to this process, or at work: its subagents too.
            let files = &self.files;
            let mains: Vec<PathBuf> = self
                .dirs
                .list(&project)
                .iter()
                .filter(|e| !e.is_dir && is_jsonl(&e.path) && (full || files.get(&e.path).is_none_or(hot)))
                .map(|e| e.path.clone())
                .collect();
            for main in mains {
                let sub = main.with_extension("").join("subagents");
                paths.extend(self.dirs.list(&sub).iter().filter(|e| is_jsonl(&e.path)).map(|e| e.path.clone()));
                paths.insert(main);
            }
        }
        if full {
            self.dirs.forget_untouched();
        }
        paths.extend(self.files.iter().filter(|(_, t)| full || hot(t)).map(|(p, _)| p.clone()));

        let mut harvest = Harvest::default();
        for path in paths {
            let Ok(md) = std::fs::metadata(&path) else {
                // Deleted, or moved to another project's folder.
                self.files.remove(&path);
                continue;
            };
            let Some(name) = path.strip_prefix(&self.root).ok().map(|rel| format!("{CURSOR_PREFIX}{}", rel.to_string_lossy())) else {
                continue;
            };
            let (counted, head) = self.counted.as_ref().and_then(|c| c.get(&name)).copied().unwrap_or_default();
            let t = self.files.entry(path.clone()).or_insert_with(|| Transcript { counted, head, ..Default::default() });
            t.modified = md.modified().ok();
            if t.first_read && md.len() == t.pos.read {
                continue;
            }
            t.len = md.len();
            if let Err(e) = t.read_new(&path, &name, &mut harvest) {
                tracing::debug!("claude: {}: {e}", path.display());
            }
        }
        harvest
    }
}

impl Transcript {
    fn read_new(&mut self, path: &Path, name: &str, out: &mut Harvest) -> std::io::Result<()> {
        let mut file = std::fs::File::open(path)?;
        let watched = self.first_read;
        if !self.first_read {
            self.first_read = true;
            self.pos.read = self.counted;
        }
        if tail::replaced(&mut file, self.len, self.pos.read, self.head.as_ref())? {
            // Another file under the name: read from the top.
            *self = Transcript { first_read: true, len: self.len, modified: self.modified, ..Default::default() };
        }
        if self.head.is_none_or(|h| h.short_of(self.len)) {
            self.head = Some(Head::of(&mut file, self.len)?);
        }
        let recent = &mut self.recent;
        let moved = self.pos.read_lines(&mut file, self.len, CHUNK, |line, _| {
            if let Ok(text) = std::str::from_utf8(line)
                && text.contains(MARKER)
                && let Some(u) = usage_of(text, recent)
            {
                out.usage.push(u);
            }
        })?;
        out.grew |= watched && moved;
        if self.pos.read > self.counted {
            self.counted = self.pos.read;
            out.cursors.push((name.to_string(), self.counted, self.head.map(|h| h.encode())));
        }
        Ok(())
    }
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct Line {
    #[serde(rename = "type")]
    kind: String,
    session_id: Option<String>,
    request_id: Option<String>,
    timestamp: Option<String>,
    is_sidechain: Option<bool>,
    message: Option<Reply>,
}

#[derive(Deserialize)]
struct Reply {
    id: Option<String>,
    model: Option<String>,
    usage: Option<Usage>,
}

#[derive(Deserialize)]
struct Usage {
    input_tokens: Option<u64>,
    output_tokens: Option<u64>,
    cache_read_input_tokens: Option<u64>,
    cache_creation_input_tokens: Option<u64>,
}

/// The request an `assistant` line tells of, unless an earlier line of the
/// same reply told it already.
fn usage_of(text: &str, recent: &mut VecDeque<String>) -> Option<UsageRecord> {
    let line: Line = serde_json::from_str(text).ok()?;
    let reply = line.message.filter(|_| line.kind == "assistant")?;
    // Claude Code's own notes in the conversation (an API error, an
    // interruption) are no request.
    let model = reply.model.filter(|m| m != "<synthetic>")?;
    let request_id = line.request_id.or(reply.id)?;
    let u = reply.usage?;
    if recent.contains(&request_id) {
        return None;
    }
    if recent.len() == RECENT {
        recent.pop_front();
    }
    recent.push_back(request_id.clone());
    let n = |v: Option<u64>| v.unwrap_or(0);
    Some(UsageRecord {
        request_id,
        ts: line.timestamp.as_deref().and_then(parse_utc_ms).unwrap_or_else(now_ms),
        agent: agents::CLAUDE_CODE.to_string(),
        session_id: line.session_id,
        model: Some(model),
        source: Some(if line.is_sidechain == Some(true) { "subagent" } else { "main" }.to_string()),
        // Anthropic counts cached input apart from the input.
        input: n(u.input_tokens),
        output: n(u.output_tokens),
        cache_read: n(u.cache_read_input_tokens),
        cache_write: n(u.cache_creation_input_tokens),
    })
}

fn entries(dir: &Path) -> Vec<PathBuf> {
    std::fs::read_dir(dir).map(|it| it.filter_map(Result::ok).map(|e| e.path()).collect()).unwrap_or_default()
}

fn is_jsonl(p: &Path) -> bool {
    p.extension().is_some_and(|e| e == "jsonl")
}

// ---------------------------------------------------------------------------
// Usage limits
// ---------------------------------------------------------------------------

/// The windows Claude Code's plugin API names too, in its order.
const KINDS: [&str; 4] = ["five_hour", "seven_day", "seven_day_opus", "seven_day_sonnet"];

/// `/usage` without the user's settings, tools or MCP servers, and without
/// keeping a session: it starts no plugin, and nothing of it is left behind.
const USAGE_ARGS: [&str; 10] =
    ["-p", "/usage", "--output-format", "json", "--tools", "", "--strict-mcp-config", "--setting-sources", "", "--no-session-persistence"];
/// Longest a `/usage` run may take.
const RUN_TIMEOUT: Duration = Duration::from_secs(60);

/// How the limits are fetched again: how often at most, and with which
/// `claude` (`None`: found on `PATH` or where its installers put it).
#[derive(Clone, Debug)]
pub struct Refresh {
    pub every: Duration,
    pub binary: Option<PathBuf>,
}

/// The account's usage limits as Claude Code last fetched them.
pub struct Limits {
    file: PathBuf,
    modified: Option<SystemTime>,
    /// When the reading last filed was fetched.
    fetched: i64,
    refresh: Option<Refresh>,
    run: Option<(Child, Instant)>,
    last_run: Option<Instant>,
    /// The last run failed: the next failure is not logged again.
    failing: bool,
}

impl Limits {
    pub fn new(refresh: Option<Refresh>) -> Limits {
        Limits::at(config_file(), refresh)
    }

    pub fn at(file: PathBuf, refresh: Option<Refresh>) -> Limits {
        Limits { file, modified: None, fetched: 0, refresh, run: None, last_run: None, failing: false }
    }

    /// Files a reading fetched since the last pass; fetches one when it is
    /// due. `active_at` is when Claude Code was last seen at work.
    pub fn pass(&mut self, hub: &Hub, active_at: Option<i64>) {
        if let Some((fetched, windows)) = self.read() {
            hub.note_rate_limits(agents::CLAUDE_CODE, "usage", fetched, &windows);
        }
        self.reap();
        let Some(every) = self.refresh.as_ref().map(|r| r.every) else { return };
        if self.run.is_none()
            && self.last_run.is_none_or(|t| t.elapsed() >= every)
            && due(now_ms(), every, active_at, hub.quota_at(agents::CLAUDE_CODE))
        {
            self.last_run = Some(Instant::now());
            match self.spawn() {
                Ok(child) => self.run = Some((child, Instant::now())),
                Err(e) => self.failed(&format!("could not run `claude -p /usage`: {e}")),
            }
        }
    }

    /// A reading fetched after the last one returned, when the config changed.
    fn read(&mut self) -> Option<(i64, Vec<RateWindow>)> {
        let modified = std::fs::metadata(&self.file).and_then(|m| m.modified()).ok()?;
        if self.modified == Some(modified) {
            return None;
        }
        self.modified = Some(modified);
        let (fetched, windows) = reading(&std::fs::read(&self.file).ok()?)?;
        (fetched > self.fetched).then(|| {
            self.fetched = fetched;
            (fetched, windows)
        })
    }

    /// Ends a run that finished or took too long.
    fn reap(&mut self) {
        let Some((child, started)) = &mut self.run else { return };
        match child.try_wait() {
            Ok(Some(status)) if status.success() => {
                self.failing = false;
                self.run = None;
            }
            Ok(Some(status)) => {
                self.run = None;
                self.failed(&format!("`claude -p /usage` exited with {status}"));
            }
            Ok(None) if started.elapsed() > RUN_TIMEOUT => {
                let _ = child.kill();
                let _ = child.wait();
                self.run = None;
                self.failed("`claude -p /usage` took too long and was stopped");
            }
            Ok(None) => {}
            Err(e) => {
                self.run = None;
                self.failed(&format!("`claude -p /usage`: {e}"));
            }
        }
    }

    fn failed(&mut self, why: &str) {
        if !self.failing {
            tracing::warn!("claude: {why}");
        }
        self.failing = true;
    }

    fn spawn(&self) -> std::io::Result<Child> {
        let configured = self.refresh.as_ref().and_then(|r| r.binary.clone());
        let binary = configured
            .or_else(find_claude)
            .ok_or_else(|| std::io::Error::new(std::io::ErrorKind::NotFound, "claude is not installed where the daemon looks"))?;
        let dir = std::env::temp_dir().join("agentsmon-claude");
        std::fs::create_dir_all(&dir)?;
        let mut cmd = Command::new(&binary);
        cmd.args(USAGE_ARGS).current_dir(&dir).stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null());
        // A session of its own, not one inside the Claude Code that may have
        // started the daemon.
        for (key, _) in std::env::vars_os() {
            let k = key.to_string_lossy();
            if k == "CLAUDECODE" || k.starts_with("CLAUDE_CODE_") {
                cmd.env_remove(&key);
            }
        }
        cmd.envs([("DISABLE_TELEMETRY", "1"), ("DISABLE_ERROR_REPORTING", "1"), ("DISABLE_AUTOUPDATER", "1")]);
        // An npm install's `claude` is a Node script: its folder holds the
        // `node` it runs on, which a login agent's PATH does not.
        if let Some(bin) = binary.parent() {
            let path = std::env::var_os("PATH").map(|p| std::env::split_paths(&p).collect::<Vec<_>>()).unwrap_or_default();
            if let Ok(joined) = std::env::join_paths(std::iter::once(bin.to_path_buf()).chain(path)) {
                cmd.env("PATH", joined);
            }
        }
        cmd.spawn()
    }
}

/// Whether to fetch the limits now: Claude Code was at work within `every`,
/// and the reading held (the plugin's, or an earlier fetch) is older.
fn due(now: i64, every: Duration, active_at: Option<i64>, held_at: Option<i64>) -> bool {
    let every = every.as_millis() as i64;
    active_at.is_some_and(|at| now - at < every) && held_at.is_none_or(|at| now - at >= every)
}

/// When the reading in Claude Code's config was fetched, and its windows.
fn reading(config: &[u8]) -> Option<(i64, Vec<RateWindow>)> {
    #[derive(Deserialize)]
    struct Config {
        #[serde(rename = "cachedUsageUtilization")]
        cached: Option<Cached>,
    }
    #[derive(Deserialize)]
    #[serde(rename_all = "camelCase")]
    struct Cached {
        fetched_at_ms: i64,
        utilization: Value,
    }
    let cached = serde_json::from_slice::<Config>(config).ok()?.cached?;
    let windows: Vec<RateWindow> = KINDS
        .iter()
        .filter_map(|&kind| {
            let w = cached.utilization.get(kind).filter(|w| w.is_object())?;
            Some(RateWindow {
                kind: kind.to_string(),
                percent_used: w.get("utilization")?.as_f64()?,
                // To the second, as the plugin reports it.
                resets_at: w.get("resets_at").and_then(Value::as_str).and_then(utc_ms).map(|ms| format_utc_ms(ms.div_euclid(1_000) * 1_000)),
            })
        })
        .collect();
    (!windows.is_empty()).then_some((cached.fetched_at_ms, windows))
}

/// Milliseconds of `2026-10-27T17:20:00.001876+00:00`, or of the `Z` form.
fn utc_ms(s: &str) -> Option<i64> {
    match s.strip_suffix("+00:00") {
        Some(rest) => parse_utc_ms(&format!("{rest}Z")),
        None => parse_utc_ms(s),
    }
}

/// Where `claude` is installed: on `PATH`, else where its installers put it.
fn find_claude() -> Option<PathBuf> {
    let home = user_home();
    let on_path: Vec<PathBuf> = std::env::var_os("PATH").map(|p| std::env::split_paths(&p).collect()).unwrap_or_default();
    let usual = [".local/bin", ".claude/local", ".npm-global/bin", ".volta/bin", ".bun/bin"].map(|d| home.join(d));
    let system = ["/opt/homebrew/bin", "/usr/local/bin"].map(PathBuf::from);
    // nvm keeps a folder per Node version: the newest first.
    let mut nvm = entries(&home.join(".nvm/versions/node"));
    nvm.sort_by_key(|p| std::cmp::Reverse(node_version(p)));
    on_path
        .into_iter()
        .chain(usual)
        .chain(system)
        .chain(nvm.into_iter().map(|v| v.join("bin")))
        .map(|dir| dir.join("claude"))
        .find(|p| std::fs::metadata(p).is_ok_and(|m| m.is_file() && m.permissions().mode() & 0o111 != 0))
}

/// `v24.21.0` as numbers, to sort by.
fn node_version(dir: &Path) -> Vec<u64> {
    let name = dir.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    name.trim_start_matches('v').split('.').map(|part| part.parse().unwrap_or(0)).collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("agentsmon-claude-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn append(path: &Path, text: &str) {
        use std::io::Write;
        std::fs::OpenOptions::new().create(true).append(true).open(path).unwrap().write_all(text.as_bytes()).unwrap();
    }

    /// One line of a reply, as Claude Code 2.1 writes it, trimmed.
    fn reply(session: &str, request: &str, block: &str, output: u64, sidechain: bool) -> String {
        format!(
            "{}\n",
            json!({"parentUuid": "p", "isSidechain": sidechain, "type": "assistant", "sessionId": session,
                "requestId": request, "timestamp": "2026-10-27T09:40:51.133Z",
                "message": {"model": "claude-opus-5-5", "id": format!("msg_{request}"), "type": "message", "role": "assistant",
                    "content": [{"type": block, "text": "…"}],
                    "usage": {"input_tokens": 2, "cache_creation_input_tokens": 300, "cache_read_input_tokens": 4_000,
                        "output_tokens": output, "server_tool_use": {"web_search_requests": 0}}}})
        )
    }

    #[test]
    fn counts_each_reply_once_with_its_subagents() {
        let root = temp("transcripts");
        let project = root.join("-Users-me-proj");
        std::fs::create_dir_all(project.join("s1/subagents")).unwrap();
        let main = project.join("s1.jsonl");
        append(&main, "{\"type\":\"queue-operation\",\"sessionId\":\"s1\"}\n");
        append(&main, "{\"type\":\"user\",\"sessionId\":\"s1\",\"message\":{\"role\":\"user\",\"content\":\"\\\"type\\\":\\\"assistant\\\"\"}}\n");
        // One reply, three content blocks.
        append(&main, &reply("s1", "req_1", "thinking", 10, false));
        append(&main, &reply("s1", "req_1", "text", 10, false));
        append(&main, &reply("s1", "req_1", "tool_use", 10, false));
        // Claude Code's own note: no request.
        append(&main, &json!({"type": "assistant", "sessionId": "s1", "message": {"model": "<synthetic>", "id": "x",
            "usage": {"input_tokens": 0, "output_tokens": 0}}}).to_string());
        append(&main, "\n");
        let sub = project.join("s1/subagents/agent-a1.jsonl");
        append(&sub, &reply("s1", "req_2", "text", 7, true));
        // Not finished being written.
        let last = reply("s1", "req_3", "text", 5, false);
        append(&main, &last[..60]);

        let mut t = Transcripts::at(root.clone());
        t.counted = Some(HashMap::new());
        let h = t.scan();
        let mut got: Vec<(&str, &str, u64)> =
            h.usage.iter().map(|u| (u.request_id.as_str(), u.source.as_deref().unwrap(), u.output)).collect();
        got.sort();
        assert_eq!(got, vec![("req_1", "main", 10), ("req_2", "subagent", 7)]);
        let u = &h.usage[0];
        assert_eq!((u.session_id.as_deref(), u.model.as_deref(), u.input, u.cache_read, u.cache_write), (Some("s1"), Some("claude-opus-5-5"), 2, 4_000, 300));
        assert_eq!(u.ts, 1_793_094_051_133);
        // Read in a pass that saw the file first: no sign of work yet.
        assert!(!h.grew);
        let cursors: HashMap<String, (u64, Option<Head>)> =
            h.cursors.iter().map(|(n, o, head)| (n.clone(), (*o, head.as_deref().and_then(Head::decode)))).collect();
        assert!(cursors.contains_key("claude/-Users-me-proj/s1.jsonl"));
        assert!(cursors.contains_key("claude/-Users-me-proj/s1/subagents/agent-a1.jsonl"));

        // The rest of the line: the reply is counted, and Claude Code is at work.
        append(&main, &last[60..]);
        let h = t.scan();
        assert_eq!(h.usage.iter().map(|u| u.request_id.as_str()).collect::<Vec<_>>(), vec!["req_3"]);
        assert!(h.grew);
        assert!(t.scan().usage.is_empty());

        // A later run reads on from the cursors: only what is new.
        let mut cursors = cursors;
        for (n, o, head) in &h.cursors {
            cursors.insert(n.clone(), (*o, head.as_deref().and_then(Head::decode)));
        }
        append(&main, &reply("s1", "req_4", "text", 1, false));
        let mut again = Transcripts::at(root.clone());
        again.counted = Some(cursors.clone());
        assert_eq!(again.scan().usage.iter().map(|u| u.request_id.as_str()).collect::<Vec<_>>(), vec!["req_4"]);

        // A transcript replaced by another one, longer, is read from the top.
        std::fs::write(&main, format!("{{\"type\":\"summary\",\"leafUuid\":\"other\"}}\n{}{}", reply("s1", "req_5", "text", 1, false), "x".repeat(4_000) + "\n")).unwrap();
        let mut replaced = Transcripts::at(root.clone());
        replaced.counted = Some(cursors);
        assert_eq!(replaced.scan().usage.iter().map(|u| u.request_id.as_str()).collect::<Vec<_>>(), vec!["req_5"]);
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn reads_the_limits_claude_code_last_fetched() {
        let config = json!({"numStartups": 3, "cachedUsageUtilization": {"fetchedAtMs": 1_793_105_605_100_i64, "accountUuid": "a",
            "utilization": {
                "five_hour": {"utilization": 7, "resets_at": "2026-10-27T17:20:00.001876+00:00"},
                "seven_day": {"utilization": 20.5, "resets_at": "2026-10-28T10:00:00Z"},
                "seven_day_opus": null,
                "iguana_necktie": {"utilization": 0, "resets_at": "2026-11-05T07:59:00+00:00"},
                "limits": [{"kind": "session", "percent": 7}]}}});
        let (fetched, windows) = reading(config.to_string().as_bytes()).unwrap();
        assert_eq!(fetched, 1_793_105_605_100);
        assert_eq!(
            windows,
            vec![
                RateWindow { kind: "five_hour".into(), percent_used: 7.0, resets_at: Some("2026-10-27T17:20:00.000Z".into()) },
                RateWindow { kind: "seven_day".into(), percent_used: 20.5, resets_at: Some("2026-10-28T10:00:00.000Z".into()) },
            ]
        );
        assert!(reading(b"{\"numStartups\": 3}").is_none());
        assert!(reading(b"not json").is_none());

        // Only a reading fetched after the last one is filed.
        let dir = temp("limits");
        let file = dir.join(".claude.json");
        std::fs::write(&file, config.to_string()).unwrap();
        let mut limits = Limits::at(file.clone(), None);
        assert!(limits.read().is_some());
        assert!(limits.read().is_none());
        let mut later = config.clone();
        later["numStartups"] = json!(4);
        std::fs::write(&file, later.to_string()).unwrap();
        limits.modified = None;
        assert!(limits.read().is_none());
        later["cachedUsageUtilization"]["fetchedAtMs"] = json!(1_793_105_700_000_i64);
        std::fs::write(&file, later.to_string()).unwrap();
        limits.modified = None;
        assert_eq!(limits.read().map(|(at, _)| at), Some(1_793_105_700_000));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn fetches_only_while_claude_code_works_and_the_reading_is_old() {
        let every = Duration::from_secs(600);
        let now = 10_000_000;
        // Idle: the windows only run out, which their reset times tell.
        assert!(!due(now, every, None, None));
        assert!(!due(now, every, Some(now - 700_000), Some(now - 3_600_000)));
        // At work, and nothing newer than ten minutes.
        assert!(due(now, every, Some(now - 5_000), None));
        assert!(due(now, every, Some(now - 5_000), Some(now - 600_000)));
        // The plugin reported a moment ago.
        assert!(!due(now, every, Some(now - 5_000), Some(now - 30_000)));
    }

    #[test]
    fn sorts_node_versions_as_numbers() {
        let mut v = [PathBuf::from("v9.11.2"), PathBuf::from("v24.21.0"), PathBuf::from("v18.0.1")];
        v.sort_by_key(|p| std::cmp::Reverse(node_version(p)));
        assert_eq!(v[0], PathBuf::from("v24.21.0"));
    }
}
