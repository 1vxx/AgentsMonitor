//! Claude Code's classic (settings) hook payloads.
//!
//! The Claude Code plugin sends canonical events from its hooks module and
//! never comes through here. This is the fallback for sessions that can only
//! run command hooks (an older Claude Code, a machine without the plugin):
//! point every hook at `agentsmon hook --agent claude-code`.

use agentsmon_protocol::{AttentionKind, Event, SessionMeta, TurnOutcome, UsageRecord};
use serde_json::Value;

use super::{AdapterError, Normalized, subject_for};
use crate::otlp::{AttrValue, Attrs};
use crate::util::preview;

const PROMPT_CHARS: usize = 200;

pub fn normalize(p: &Value) -> Result<Normalized, AdapterError> {
    let str_of = |name: &str| p.get(name).and_then(Value::as_str);
    let owned = |name: &str| str_of(name).map(str::to_string);
    let sid = str_of("session_id").ok_or(AdapterError::Malformed("no session_id"))?.to_string();
    let event_name = str_of("hook_event_name").ok_or(AdapterError::Malformed("no hook_event_name"))?;
    let agent_id = owned("agent_id");
    let tool = || owned("tool_name").unwrap_or_default();
    let tool_input = p.get("tool_input").unwrap_or(&Value::Null);
    let tool_id = owned("tool_use_id");

    let meta = SessionMeta {
        cwd: owned("cwd"),
        model: owned("model"),
        permission_mode: owned("permission_mode"),
        transcript_path: owned("transcript_path"),
        ..Default::default()
    };

    let events = match event_name {
        "SessionStart" => vec![Event::SessionStart { source: owned("source"), meta: SessionMeta::default() }],
        "SessionEnd" => vec![Event::SessionEnd { reason: owned("reason") }],
        "UserPromptSubmit" => {
            let text = str_of("prompt").and_then(|t| preview(t, PROMPT_CHARS));
            vec![
                Event::Prompt { preview: text.clone() },
                Event::TurnStart { turn_id: owned("prompt_id"), agent_id: None, preview: text },
            ]
        }
        "PreToolUse" => {
            let name = tool();
            let subject = subject_for(&name, tool_input);
            let mut out = vec![Event::ToolStart { id: tool_id, tool: name.clone(), agent_id, subject: subject.clone() }];
            if name == "AskUserQuestion" {
                out.push(Event::Attention { kind: AttentionKind::Input, tool: Some(name), subject, message: None });
            }
            out
        }
        "PostToolUse" | "PostToolUseFailure" => vec![Event::ToolEnd {
            id: tool_id,
            tool: tool(),
            agent_id,
            ok: Some(event_name == "PostToolUse"),
            duration_ms: None,
        }],
        "PermissionRequest" => {
            let name = tool();
            vec![Event::Attention {
                kind: AttentionKind::Permission,
                subject: subject_for(&name, tool_input),
                tool: Some(name),
                message: None,
            }]
        }
        "Notification" => match str_of("notification_type") {
            Some("idle_prompt") => vec![Event::Attention {
                kind: AttentionKind::Idle,
                tool: None,
                subject: None,
                message: owned("message"),
            }],
            // PermissionRequest already said so, with the tool.
            Some("permission_prompt") => vec![],
            kind => vec![Event::Notice { kind: kind.unwrap_or("notification").to_string(), message: owned("message") }],
        },
        "Stop" | "StopFailure" => vec![Event::TurnEnd {
            turn_id: None,
            agent_id: None,
            outcome: if event_name == "Stop" { TurnOutcome::Answer } else { TurnOutcome::Error },
            duration_ms: None,
            usage: None,
            preview: str_of("last_assistant_message").and_then(|t| preview(t, PROMPT_CHARS)),
        }],
        "SubagentStart" => vec![Event::TurnStart { turn_id: None, agent_id, preview: owned("agent_type") }],
        "SubagentStop" => vec![Event::TurnEnd {
            turn_id: None,
            agent_id,
            outcome: TurnOutcome::Answer,
            duration_ms: None,
            usage: None,
            preview: None,
        }],
        "PreCompact" => vec![Event::CompactStart { trigger: owned("trigger") }],
        "PostCompact" => vec![Event::CompactEnd {}],
        _ => vec![],
    };
    Ok(Normalized { sid, meta, events })
}

/// Claude Code's `api_request` event: one per model request it makes, the
/// main loop's, subagents', compaction's and background ones alike.
pub fn usage_from_otel(body: Option<&str>, attrs: &Attrs, ts: i64) -> Option<UsageRecord> {
    let is_request = attrs.get("event.name").and_then(AttrValue::as_str) == Some("api_request")
        || body == Some("claude_code.api_request");
    if !is_request {
        return None;
    }
    let text = |k: &str| attrs.get(k).and_then(AttrValue::as_str).map(str::to_string);
    let count = |k: &str| attrs.get(k).and_then(AttrValue::as_u64).unwrap_or(0);
    // Without an id a repeated delivery could not be told apart.
    let request_id = text("request_id").or_else(|| text("client_request_id"))?;
    Some(UsageRecord {
        request_id,
        ts,
        agent: agentsmon_protocol::agents::CLAUDE_CODE.to_string(),
        session_id: text("session.id"),
        model: text("model"),
        source: text("query_source"),
        input: count("input_tokens"),
        output: count("output_tokens"),
        cache_read: count("cache_read_tokens"),
        cache_write: count("cache_creation_tokens"),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn prompt_starts_a_turn() {
        let out = normalize(&json!({
            "session_id": "s", "hook_event_name": "UserPromptSubmit", "prompt": "fix   the bug", "cwd": "/w"
        }))
        .unwrap();
        assert_eq!(out.meta.cwd.as_deref(), Some("/w"));
        assert_eq!(out.events.len(), 2);
        assert!(matches!(&out.events[0], Event::Prompt { preview: Some(p) } if p == "fix the bug"));
        assert!(matches!(&out.events[1], Event::TurnStart { agent_id: None, .. }));
    }

    #[test]
    fn a_question_asks_for_input() {
        let out = normalize(&json!({
            "session_id": "s", "hook_event_name": "PreToolUse", "tool_name": "AskUserQuestion",
            "tool_use_id": "t1", "tool_input": {"questions": [{"question": "Which one?"}]}
        }))
        .unwrap();
        assert!(matches!(&out.events[1], Event::Attention { kind: AttentionKind::Input, subject: Some(s), .. } if s == "Which one?"));
    }

    #[test]
    fn missing_session_is_malformed() {
        assert!(normalize(&json!({"hook_event_name": "Stop"})).is_err());
    }

    #[test]
    fn unknown_events_map_to_nothing() {
        assert!(normalize(&json!({"session_id": "s", "hook_event_name": "FileChanged"})).unwrap().events.is_empty());
    }
}
