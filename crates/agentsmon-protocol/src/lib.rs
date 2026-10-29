//! Wire types shared by the daemon, the agent plugins, the web dashboard and
//! (later) the display device.
//!
//! Three layers:
//! - [`Envelope`] / [`Event`]: what an agent integration sends in. First-party
//!   plugins send canonical events; anything that can only forward an agent's
//!   own hook payload sends [`Event::Raw`] and a daemon-side adapter maps it.
//! - [`Session`], [`Quota`], [`Summary`]: the state the daemon derives.
//! - [`ServerMsg`]: what the WebSocket pushes to clients.
//!
//! Agents are named by plain strings ([`agents`]) so adding one never needs a
//! protocol change.

use serde::{Deserialize, Serialize};
use serde_json::Value;

pub const PROTOCOL_VERSION: u32 = 1;

/// Well-known agent names. Any other string is accepted as-is.
pub mod agents {
    pub const CLAUDE_CODE: &str = "claude-code";
    pub const CODEX: &str = "codex";
}

// ---------------------------------------------------------------------------
// Ingest
// ---------------------------------------------------------------------------

/// One event as an integration sends it. `POST /v1/ingest` takes a JSON array
/// of these (or a single object).
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Envelope {
    /// Which agent product: `claude-code`, `codex`, ...
    pub agent: String,
    /// The agent's own session id. Optional for raw payloads, whose adapter
    /// reads it from the payload.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub sid: Option<String>,
    /// Milliseconds since the epoch, as the sender saw it. The daemon stamps
    /// its own clock when absent.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ts: Option<i64>,
    #[serde(flatten)]
    pub event: Event,
}

/// Facts about a session that may arrive with several events and only ever
/// fill in or replace what the daemon already knows.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct SessionMeta {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cwd: Option<String>,
    /// Display name of the project, usually the repository or folder name.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub project: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub permission_mode: Option<String>,
    /// Version of the agent product.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub agent_version: Option<String>,
    /// Where it is drawn: `terminal`, `desktop`, `vscode`, `headless`, ...
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub surface: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub interactive: Option<bool>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub transcript_path: Option<String>,
    /// The short title the agent gave the session (Claude Code: a rename,
    /// Claude Desktop's title or its own generated one), when it has one.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
}

impl SessionMeta {
    /// Overlays every field `other` has onto `self`; returns whether anything
    /// changed.
    pub fn merge(&mut self, other: &SessionMeta) -> bool {
        let before = self.clone();
        macro_rules! take {
            ($($f:ident),*) => { $( if other.$f.is_some() { self.$f = other.$f.clone(); } )* };
        }
        take!(cwd, project, model, permission_mode, agent_version, surface, interactive, transcript_path, title);
        *self != before
    }

    pub fn is_empty(&self) -> bool {
        *self == SessionMeta::default()
    }
}

/// The canonical event vocabulary every agent is mapped onto.
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum Event {
    /// A session began, or an integration (re)connected to a running one: the
    /// daemon never resets a known session on this.
    #[serde(rename = "session.start")]
    SessionStart {
        /// `startup`, `resume`, `clear`, `reload`, ...
        #[serde(default, skip_serializing_if = "Option::is_none")]
        source: Option<String>,
        #[serde(flatten)]
        meta: SessionMeta,
    },
    #[serde(rename = "session.end")]
    SessionEnd {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        reason: Option<String>,
    },
    /// Liveness ping; carries metadata so a restarted daemon can rebuild the
    /// session from it.
    #[serde(rename = "heartbeat")]
    Heartbeat {
        /// Whether the integration sees a turn running. `false` lets the
        /// daemon drop work whose end event was lost.
        #[serde(default, skip_serializing_if = "Option::is_none")]
        busy: Option<bool>,
        #[serde(flatten)]
        meta: SessionMeta,
    },
    /// The person submitted a prompt.
    #[serde(rename = "prompt")]
    Prompt {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        preview: Option<String>,
    },
    /// A model turn began: the main loop's, or a subagent's (`agent_id`).
    #[serde(rename = "turn.start")]
    TurnStart {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        turn_id: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        agent_id: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        preview: Option<String>,
    },
    #[serde(rename = "turn.end")]
    TurnEnd {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        turn_id: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        agent_id: Option<String>,
        outcome: TurnOutcome,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        duration_ms: Option<u64>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        usage: Option<TokenUsage>,
        /// The start of the answer.
        #[serde(default, skip_serializing_if = "Option::is_none")]
        preview: Option<String>,
    },
    #[serde(rename = "tool.start")]
    ToolStart {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        id: Option<String>,
        tool: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        agent_id: Option<String>,
        /// What the call is about: a path, a command, a pattern, a URL.
        #[serde(default, skip_serializing_if = "Option::is_none")]
        subject: Option<String>,
    },
    #[serde(rename = "tool.end")]
    ToolEnd {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        id: Option<String>,
        tool: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        agent_id: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        ok: Option<bool>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        duration_ms: Option<u64>,
    },
    /// The agent stopped to wait for the person.
    #[serde(rename = "attention")]
    Attention {
        kind: AttentionKind,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        tool: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        subject: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        message: Option<String>,
    },
    #[serde(rename = "attention.clear")]
    AttentionClear {},
    #[serde(rename = "compact.start")]
    CompactStart {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        trigger: Option<String>,
    },
    #[serde(rename = "compact.end")]
    CompactEnd {},
    /// Context fill, cost and the account's rate-limit windows.
    #[serde(rename = "usage")]
    Usage {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        context: Option<ContextUsage>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        cost_usd: Option<f64>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        rate_limits: Option<Vec<RateWindow>>,
    },
    /// Anything worth showing that changes no state.
    #[serde(rename = "notice")]
    Notice {
        kind: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        message: Option<String>,
    },
    /// An agent's own hook payload, mapped by the daemon's adapter for
    /// `Envelope::agent`. Never stored or broadcast as such.
    #[serde(rename = "raw")]
    Raw { payload: Value },
}

impl Event {
    pub fn name(&self) -> &'static str {
        match self {
            Event::SessionStart { .. } => "session.start",
            Event::SessionEnd { .. } => "session.end",
            Event::Heartbeat { .. } => "heartbeat",
            Event::Prompt { .. } => "prompt",
            Event::TurnStart { .. } => "turn.start",
            Event::TurnEnd { .. } => "turn.end",
            Event::ToolStart { .. } => "tool.start",
            Event::ToolEnd { .. } => "tool.end",
            Event::Attention { .. } => "attention",
            Event::AttentionClear {} => "attention.clear",
            Event::CompactStart { .. } => "compact.start",
            Event::CompactEnd {} => "compact.end",
            Event::Usage { .. } => "usage",
            Event::Notice { .. } => "notice",
            Event::Raw { .. } => "raw",
        }
    }

    /// Events too frequent or too empty to keep in the history. (Usage lives
    /// on in the session and the quota readings.)
    pub fn is_ephemeral(&self) -> bool {
        matches!(self, Event::Heartbeat { .. } | Event::Usage { .. } | Event::Raw { .. })
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TurnOutcome {
    Answer,
    Aborted,
    Refusal,
    Error,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AttentionKind {
    /// A tool call waits for approval.
    Permission,
    /// The agent asked the person a question.
    Input,
    /// The agent has been waiting at the prompt for a while.
    Idle,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct TokenUsage {
    #[serde(default)]
    pub input: u64,
    #[serde(default)]
    pub output: u64,
    #[serde(default)]
    pub cache_read: u64,
    #[serde(default)]
    pub cache_write: u64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct ContextUsage {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tokens: Option<u64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub window: Option<u64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub percent: Option<f64>,
}

/// One rate-limit window of an account: `five_hour`, `seven_day`, a
/// gateway's `spend_limit`, Codex's `primary`/`secondary` when they are
/// windows of other lengths.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct RateWindow {
    pub kind: String,
    /// 0..=100, past 100 on an exceeded spend limit.
    pub percent_used: f64,
    /// ISO 8601.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub resets_at: Option<String>,
}

// ---------------------------------------------------------------------------
// Derived state
// ---------------------------------------------------------------------------

/// What a session is doing, derived from what is in flight; ordered from the
/// least to the most urgent for the person.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Status {
    Ended,
    Idle,
    Compacting,
    Thinking,
    Tool,
    WaitingInput,
    WaitingPermission,
}

impl Status {
    pub fn is_waiting(self) -> bool {
        matches!(self, Status::WaitingInput | Status::WaitingPermission)
    }
    pub fn is_working(self) -> bool {
        matches!(self, Status::Thinking | Status::Tool | Status::Compacting)
    }
    pub fn as_str(self) -> &'static str {
        match self {
            Status::Ended => "ended",
            Status::Idle => "idle",
            Status::Compacting => "compacting",
            Status::Thinking => "thinking",
            Status::Tool => "tool",
            Status::WaitingInput => "waiting_input",
            Status::WaitingPermission => "waiting_permission",
        }
    }
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct ActiveTurn {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,
    pub started_at: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub preview: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct ActiveTool {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,
    pub tool: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub subject: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub agent_id: Option<String>,
    pub started_at: i64,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Subagent {
    pub id: String,
    pub started_at: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub preview: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Attention {
    pub kind: AttentionKind,
    pub since: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tool: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub subject: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct LastTurn {
    pub outcome: TurnOutcome,
    pub ended_at: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub duration_ms: Option<u64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub preview: Option<String>,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct SessionStats {
    pub prompts: u32,
    pub turns: u32,
    pub tool_calls: u32,
    pub tool_errors: u32,
    pub subagent_runs: u32,
    pub errors: u32,
    pub input_tokens: u64,
    pub output_tokens: u64,
    pub cache_read_tokens: u64,
    pub cache_write_tokens: u64,
    /// Model requests counted in the token figures (exact counting only).
    pub api_requests: u32,
    /// The token figures are the agent's own per-request counts (see
    /// [`UsageRecord`]); otherwise they are summed from turn ends, which miss
    /// requests outside a turn and interrupted ones.
    pub exact_tokens: bool,
    /// Sum of main-loop turn durations.
    pub busy_ms: u64,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Session {
    /// `<agent>:<session id>`, unique across agents.
    pub key: String,
    pub agent: String,
    pub session_id: String,
    pub meta: SessionMeta,
    pub status: Status,
    pub status_since: i64,
    pub started_at: i64,
    pub updated_at: i64,
    /// Last time anything, a heartbeat included, came from the session.
    pub last_seen: i64,
    /// Nothing heard for longer than the agent's liveness window.
    pub stale: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ended_at: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub end_reason: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub turn: Option<ActiveTurn>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub tools: Vec<ActiveTool>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub subagents: Vec<Subagent>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub attention: Option<Attention>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub compacting: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_turn: Option<LastTurn>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_prompt: Option<String>,
    /// When the person last opened the session in the app it runs in
    /// (Claude Desktop, the Codex app), as far as that app records it.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub seen_at: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub context: Option<ContextUsage>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cost_usd: Option<f64>,
    pub stats: SessionStats,
}

impl Session {
    pub fn key_for(agent: &str, session_id: &str) -> String {
        format!("{agent}:{session_id}")
    }

    pub fn is_ended(&self) -> bool {
        self.ended_at.is_some()
    }
}

/// An account's rate-limit windows for one agent product, as last reported.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Quota {
    pub agent: String,
    pub windows: Vec<RateWindow>,
    pub updated_at: i64,
    /// The session whose response reported it, or `usage` when read from the
    /// agent's own record of the account's limits (Claude Code's `/usage`).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub source: Option<String>,
}

// ---------------------------------------------------------------------------
// Token accounting
// ---------------------------------------------------------------------------

/// One model request's token counts, as the agent itself reported it (Claude
/// Code's `api_request` OpenTelemetry event). The request id makes each one
/// count once however often it is delivered.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct UsageRecord {
    pub request_id: String,
    pub ts: i64,
    pub agent: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub session_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
    /// What made the request: the main loop, a subagent, compaction, ...
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub source: Option<String>,
    pub input: u64,
    pub output: u64,
    pub cache_read: u64,
    pub cache_write: u64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct TokenTotals {
    pub input: u64,
    pub output: u64,
    pub cache_read: u64,
    pub cache_write: u64,
    pub requests: u64,
}

impl TokenTotals {
    pub fn total(&self) -> u64 {
        self.input + self.output + self.cache_read + self.cache_write
    }

    pub fn add(&mut self, other: &TokenTotals) {
        self.input += other.input;
        self.output += other.output;
        self.cache_read += other.cache_read;
        self.cache_write += other.cache_write;
        self.requests += other.requests;
    }
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct TokenGroup {
    pub key: String,
    #[serde(flatten)]
    pub totals: TokenTotals,
}

/// `GET /v1/tokens`: totals over `[since, until)`, optionally broken down.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct TokenReport {
    pub since: i64,
    pub until: i64,
    #[serde(flatten)]
    pub totals: TokenTotals,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub groups: Vec<TokenGroup>,
    /// When the last request anywhere was counted; absent before the first.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_ts: Option<i64>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct TokenPoint {
    /// Start of the bucket.
    pub t: i64,
    #[serde(flatten)]
    pub totals: TokenTotals,
}

/// `GET /v1/tokens/series`: totals per bucket, empty buckets left out.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct TokenSeries {
    pub since: i64,
    pub bucket_ms: i64,
    pub points: Vec<TokenPoint>,
}

/// One reading of one rate-limit window, as the quota history keeps it.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct QuotaSample {
    pub agent: String,
    pub kind: String,
    pub ts: i64,
    pub percent_used: f64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub resets_at: Option<String>,
}

/// One stored event, as the history and the live feed carry it.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct EventRecord {
    pub seq: u64,
    pub ts: i64,
    pub key: String,
    pub agent: String,
    #[serde(flatten)]
    pub event: Event,
}

// ---------------------------------------------------------------------------
// Summary: the small, glanceable form a display device draws from
// ---------------------------------------------------------------------------

/// The overall feeling of all live sessions, most urgent first.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Mood {
    /// No live session.
    Offline,
    Idle,
    /// The last turn somewhere ended in an error or refusal.
    Error,
    Working,
    /// Someone waits for the person.
    Attention,
}

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct StatusCounts {
    pub live: u32,
    pub idle: u32,
    pub thinking: u32,
    pub tool: u32,
    pub compacting: u32,
    pub waiting: u32,
    pub stale: u32,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct SummaryItem {
    pub key: String,
    pub agent: String,
    pub project: String,
    /// The session's own short title, when it has one.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    pub status: Status,
    pub since: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tool: Option<String>,
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub stale: bool,
    /// What the session waits for, while it waits.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub attention: Option<AttentionBrief>,
    /// How its last turn ended, so a display can tell a finish from a failure.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_turn: Option<TurnBrief>,
    /// Tokens it has used, cache reads and writes included, as the dashboard
    /// counts them; absent before the first is counted.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tokens: Option<u64>,
    /// `tokens` are the agent's own per-request counts, not summed from turn ends.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub tokens_exact: bool,
    /// How full its context window was at the last reading.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub context: Option<ContextUsage>,
}

/// [`Attention`] with its texts cut to fit a small screen.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct AttentionBrief {
    pub kind: AttentionKind,
    pub since: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tool: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub subject: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct TurnBrief {
    pub outcome: TurnOutcome,
    pub ended_at: i64,
    /// The person has opened the session since the turn ended.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub seen: bool,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct QuotaBrief {
    pub agent: String,
    /// `(kind, percent_used)` pairs.
    pub windows: Vec<(String, f64)>,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Summary {
    pub ts: i64,
    pub mood: Mood,
    pub counts: StatusCounts,
    /// Live sessions, most urgent first, at most eight.
    pub sessions: Vec<SummaryItem>,
    pub quotas: Vec<QuotaBrief>,
}

// ---------------------------------------------------------------------------
// Server -> client
// ---------------------------------------------------------------------------

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ServerInfo {
    pub name: String,
    pub version: String,
    pub protocol: u32,
    pub started_at: i64,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Snapshot {
    pub server: ServerInfo,
    pub sessions: Vec<Session>,
    pub quotas: Vec<Quota>,
    pub summary: Summary,
}

/// Messages on `GET /v1/ws`. The first is always a `snapshot`; deltas follow.
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "type")]
#[allow(clippy::large_enum_variant)]
pub enum ServerMsg {
    #[serde(rename = "snapshot")]
    Snapshot(Snapshot),
    /// A session was created or changed; carries the whole session.
    #[serde(rename = "session")]
    Session { session: Session },
    /// A session was forgotten (pruned or deleted).
    #[serde(rename = "session.removed")]
    SessionRemoved { key: String },
    #[serde(rename = "quota")]
    Quota { quota: Quota },
    /// A history event; only to clients that asked for the feed.
    #[serde(rename = "event")]
    Event { record: EventRecord },
    #[serde(rename = "summary")]
    Summary { summary: Summary },
    /// Model requests were counted; `added` is what they came to. Clients
    /// refresh their token windows on it.
    #[serde(rename = "tokens")]
    Tokens { ts: i64, added: TokenTotals },
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn canonical_envelope_round_trips() {
        let json = r#"{"agent":"claude-code","sid":"s1","ts":5,"type":"tool.start","tool":"Bash","subject":"ls"}"#;
        let env: Envelope = serde_json::from_str(json).unwrap();
        assert_eq!(env.sid.as_deref(), Some("s1"));
        match &env.event {
            Event::ToolStart { tool, subject, .. } => {
                assert_eq!(tool, "Bash");
                assert_eq!(subject.as_deref(), Some("ls"));
            }
            other => panic!("unexpected {other:?}"),
        }
        let back = serde_json::to_string(&env).unwrap();
        assert_eq!(back, json);
    }

    #[test]
    fn session_start_flattens_meta() {
        let json = r#"{"agent":"claude-code","sid":"s1","type":"session.start","source":"startup","cwd":"/x","model":"opus"}"#;
        let env: Envelope = serde_json::from_str(json).unwrap();
        let Event::SessionStart { source, meta } = env.event else { panic!() };
        assert_eq!(source.as_deref(), Some("startup"));
        assert_eq!(meta.cwd.as_deref(), Some("/x"));
        assert_eq!(meta.model.as_deref(), Some("opus"));
    }

    #[test]
    fn raw_payload_is_kept_whole() {
        let json = r#"{"agent":"codex","type":"raw","payload":{"hook_event_name":"Stop","session_id":"x"}}"#;
        let env: Envelope = serde_json::from_str(json).unwrap();
        let Event::Raw { payload } = env.event else { panic!() };
        assert_eq!(payload["hook_event_name"], "Stop");
    }

    #[test]
    fn unknown_fields_are_ignored() {
        let json = r#"{"agent":"claude-code","sid":"s","type":"prompt","preview":"hi","extra":1}"#;
        assert!(serde_json::from_str::<Envelope>(json).is_ok());
    }

    #[test]
    fn status_order_is_urgency() {
        assert!(Status::WaitingPermission > Status::Tool);
        assert!(Status::Tool > Status::Idle);
        assert!(Mood::Attention > Mood::Working);
    }
}
