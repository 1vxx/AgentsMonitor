//! Per-agent knowledge the daemon needs: how to read an agent's own hook
//! payloads ([`Event::Raw`]) and how long a silent session stays alive.
//!
//! Adding an agent: write a module with a `normalize` function and a
//! [`Profile`], and list it in [`normalize`] and [`profile`]. Its integration
//! either sends canonical events (a first-party plugin, like Claude Code's) or
//! forwards its hook payloads untouched with `agentsmon hook --agent <name>`.

use agentsmon_protocol::{Event, SessionMeta, UsageRecord, agents};
use serde_json::Value;

pub mod claude;
pub mod codex;
mod tool;

pub use tool::{is_question, subject_for};

/// What an adapter read out of one raw payload.
#[derive(Debug)]
pub struct Normalized {
    pub sid: String,
    /// Facts every payload carries (cwd, mode, ...), merged before the events.
    pub meta: SessionMeta,
    pub events: Vec<Event>,
}

#[derive(Debug)]
pub enum AdapterError {
    /// No adapter reads this agent's payloads.
    Unsupported(String),
    /// The payload lacks what every event needs (a session id).
    Malformed(&'static str),
}

impl std::fmt::Display for AdapterError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            AdapterError::Unsupported(agent) => write!(f, "no adapter reads raw payloads from {agent}"),
            AdapterError::Malformed(why) => write!(f, "malformed payload: {why}"),
        }
    }
}

/// Maps one raw hook payload of `agent` onto canonical events.
pub fn normalize(agent: &str, payload: &Value) -> Result<Normalized, AdapterError> {
    match agent {
        agents::CLAUDE_CODE => claude::normalize(payload),
        agents::CODEX => codex::normalize(payload),
        other => Err(AdapterError::Unsupported(other.to_string())),
    }
}

/// Where an OpenTelemetry log record came from.
pub struct OtelSource<'a> {
    /// The resource's `service.name`.
    pub service: &'a str,
    /// The instrumentation scope's name.
    pub scope: &'a str,
    /// The record's body, when it is a string.
    pub body: Option<&'a str>,
}

/// Maps one OpenTelemetry log record to a model request's token counts, when
/// it is one.
pub fn usage_from_otel(source: &OtelSource<'_>, attrs: &crate::otlp::Attrs, ts: i64) -> Option<UsageRecord> {
    // The host may rename the service (Claude Desktop says `claude-code-desktop`);
    // the events' own scope stays Claude Code's.
    if source.service.starts_with("claude-code") || source.scope.starts_with("com.anthropic.claude_code") {
        return claude::usage_from_otel(source.body, attrs, ts);
    }
    // Codex's token counts come from its rollout logs (`crate::codex`): its
    // OpenTelemetry events carry no request id to count them once by.
    None
}

/// How the daemon judges a session it has not heard from.
#[derive(Clone, Copy, Debug)]
pub struct Profile {
    /// Silence after which a session that sends heartbeats is shown stale.
    pub stale_after_ms: i64,
    /// Silence after which a session is taken to be gone: ended as `lost`.
    /// Sessions that never sent a heartbeat only fall under this one.
    pub lost_after_ms: i64,
}

pub fn profile(agent: &str) -> Profile {
    match agent {
        // The plugin beats every 20 s.
        agents::CLAUDE_CODE => Profile { stale_after_ms: 65_000, lost_after_ms: 30 * 60_000 },
        // Command hooks cannot beat. Codex ends a session left idle for 30 min
        // and not open in a client itself; one that crashed waits for this.
        _ => Profile { stale_after_ms: 120_000, lost_after_ms: 12 * 3_600_000 },
    }
}
