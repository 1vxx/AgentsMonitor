//! Codex's lifecycle hook payloads, which its plugin forwards untouched with
//! `agentsmon hook --agent codex`.
//!
//! Token counts, context fill and rate limits are not in the hooks: the
//! `codex` collector reads them from the session's rollout log.
//!
//! The hooks carry `transcript_path`, but Codex calls its format unstable and
//! moves or removes the file (archiving, moving history into its database), so
//! it is left out: the janitor follows Codex's thread records instead.

use std::collections::HashMap;
use std::sync::LazyLock;

use agentsmon_protocol::{AttentionKind, Event, SessionMeta, TurnOutcome};
use parking_lot::Mutex;
use serde_json::Value;

use super::{AdapterError, Normalized, is_question, subject_for};
use crate::util::preview;

const PROMPT_CHARS: usize = 200;
const ASKED_MAX: usize = 1_000;

/// Per session: the turn a question was asked in, and the question.
type Asked = HashMap<String, (Option<String>, Option<String>)>;

/// Questions asked with `request_user_input_async`: the tool returns at once
/// and the turn ends with the question open, so the session waits for the
/// person from that turn's end until the next prompt.
static ASKED: LazyLock<Mutex<Asked>> = LazyLock::new(|| Mutex::new(HashMap::new()));

pub fn normalize(p: &Value) -> Result<Normalized, AdapterError> {
    let str_of = |name: &str| p.get(name).and_then(Value::as_str);
    let owned = |name: &str| str_of(name).map(str::to_string);
    let sid = str_of("session_id").ok_or(AdapterError::Malformed("no session_id"))?.to_string();
    let event_name = str_of("hook_event_name").ok_or(AdapterError::Malformed("no hook_event_name"))?;
    let turn_id = owned("turn_id");
    let tool = || owned("tool_name").unwrap_or_default();
    let tool_input = p.get("tool_input").unwrap_or(&Value::Null);
    let tool_id = owned("tool_use_id");
    let text = |name: &str| str_of(name).and_then(|t| preview(t, PROMPT_CHARS));

    let meta = SessionMeta {
        cwd: owned("cwd"),
        model: owned("model"),
        permission_mode: owned("permission_mode"),
        ..Default::default()
    };
    tracing::debug!(sid, event_name, tool = str_of("tool_name"), "codex hook");

    let events = match event_name {
        "SessionStart" => vec![Event::SessionStart { source: owned("source"), meta: SessionMeta::default() }],
        "SessionEnd" => {
            ASKED.lock().remove(&sid);
            vec![Event::SessionEnd { reason: owned("reason") }]
        }
        "UserPromptSubmit" => {
            ASKED.lock().remove(&sid);
            let prompt = text("prompt");
            vec![Event::Prompt { preview: prompt.clone() }, Event::TurnStart { turn_id, agent_id: None, preview: prompt }]
        }
        "PreToolUse" => {
            let name = tool();
            let subject = subject_for(&name, tool_input);
            let mut out = vec![Event::ToolStart { id: tool_id, tool: name.clone(), agent_id: None, subject: subject.clone() }];
            if name.ends_with("request_user_input_async") {
                let mut asked = ASKED.lock();
                if asked.len() >= ASKED_MAX {
                    asked.clear();
                }
                asked.insert(sid.clone(), (turn_id, subject));
            } else if is_question(&name) {
                out.push(Event::Attention { kind: AttentionKind::Input, tool: Some(name), subject, message: None });
            }
            out
        }
        "PostToolUse" => vec![Event::ToolEnd { id: tool_id, tool: tool(), agent_id: None, ok: None, duration_ms: None }],
        "PermissionRequest" => {
            let name = tool();
            let described = tool_input.get("description").and_then(Value::as_str).and_then(|d| preview(d, PROMPT_CHARS));
            vec![Event::Attention {
                kind: AttentionKind::Permission,
                subject: subject_for(&name, tool_input).or(described),
                tool: Some(name),
                message: None,
            }]
        }
        "Stop" => {
            let mut out = vec![Event::TurnEnd {
                turn_id: turn_id.clone(),
                agent_id: None,
                outcome: TurnOutcome::Answer,
                duration_ms: None,
                usage: None,
                preview: text("last_assistant_message"),
            }];
            let open = ASKED.lock().remove(&sid);
            if let Some((asked_in, subject)) = open
                && (asked_in.is_none() || turn_id.is_none() || asked_in == turn_id)
            {
                out.push(Event::Attention { kind: AttentionKind::Input, tool: None, subject, message: None });
            }
            out
        }
        "Interrupt" => {
            ASKED.lock().remove(&sid);
            vec![Event::TurnEnd {
                turn_id,
                agent_id: None,
                outcome: TurnOutcome::Aborted,
                duration_ms: None,
                usage: None,
                preview: None,
            }]
        }
        "SubagentStart" => vec![Event::TurnStart { turn_id: None, agent_id: owned("agent_id"), preview: owned("agent_type") }],
        "SubagentStop" => vec![Event::TurnEnd {
            turn_id: None,
            agent_id: owned("agent_id"),
            outcome: TurnOutcome::Answer,
            duration_ms: None,
            usage: None,
            preview: text("last_assistant_message"),
        }],
        "PreCompact" => vec![Event::CompactStart { trigger: owned("trigger") }],
        "PostCompact" => vec![Event::CompactEnd {}],
        _ => vec![],
    };
    Ok(Normalized { sid, meta, events })
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn hook(sid: &str, name: &str, extra: Value) -> Normalized {
        let mut p = json!({"session_id": sid, "hook_event_name": name, "cwd": "/w", "model": "gpt-6.1-sol",
            "transcript_path": "/r.jsonl", "permission_mode": "default"});
        p.as_object_mut().unwrap().extend(extra.as_object().unwrap().clone());
        normalize(&p).unwrap()
    }

    #[test]
    fn a_prompt_starts_a_turn_and_keeps_the_transcript_out() {
        let out = hook("p", "UserPromptSubmit", json!({"turn_id": "t1", "prompt": "fix   it"}));
        assert_eq!(out.meta.model.as_deref(), Some("gpt-6.1-sol"));
        assert_eq!(out.meta.transcript_path, None);
        assert!(matches!(&out.events[0], Event::Prompt { preview: Some(p) } if p == "fix it"));
        assert!(matches!(&out.events[1], Event::TurnStart { turn_id: Some(t), agent_id: None, .. } if t == "t1"));
    }

    #[test]
    fn tools_and_approvals() {
        let out = hook("t", "PreToolUse", json!({"tool_name": "Bash", "tool_use_id": "c1", "tool_input": {"command": "ls"}}));
        assert!(matches!(&out.events[..], [Event::ToolStart { id: Some(i), subject: Some(s), .. }] if i == "c1" && s == "ls"));
        let out = hook("t", "PermissionRequest", json!({"tool_name": "Bash", "tool_input": {"command": "rm -r x", "description": "Remove x"}}));
        assert!(matches!(&out.events[0], Event::Attention { kind: AttentionKind::Permission, subject: Some(s), .. } if s == "rm -r x"));
        let out = hook("t", "PostToolUse", json!({"tool_name": "Bash", "tool_use_id": "c1", "tool_response": "ok"}));
        assert!(matches!(&out.events[0], Event::ToolEnd { id: Some(i), .. } if i == "c1"));
    }

    #[test]
    fn a_question_waits_while_it_runs() {
        let input = json!({"questions": [{"question": "Which one?"}]});
        let out = hook("q", "PreToolUse", json!({"tool_name": "request_user_input", "tool_use_id": "c", "tool_input": input}));
        assert!(matches!(&out.events[1], Event::Attention { kind: AttentionKind::Input, subject: Some(s), .. } if s == "Which one?"));
    }

    #[test]
    fn an_async_question_waits_from_the_turns_end_until_the_next_prompt() {
        let input = json!({"questions": [{"title": "Ship it?"}]});
        let out = hook("a", "PreToolUse", json!({"turn_id": "t1", "tool_name": "request_user_input_async", "tool_input": input}));
        assert_eq!(out.events.len(), 1);
        let out = hook("a", "Stop", json!({"turn_id": "t1", "last_assistant_message": "Asked."}));
        assert!(matches!(&out.events[..], [Event::TurnEnd { .. }, Event::Attention { kind: AttentionKind::Input, tool: None, subject: Some(s), .. }] if s == "Ship it?"));
        // Asked once: the next turn's end is a plain one.
        assert_eq!(hook("a", "Stop", json!({"turn_id": "t2"})).events.len(), 1);

        hook("b", "PreToolUse", json!({"turn_id": "t1", "tool_name": "request_user_input_async", "tool_input": input}));
        hook("b", "Interrupt", json!({"turn_id": "t1"}));
        assert_eq!(hook("b", "Stop", json!({"turn_id": "t1"})).events.len(), 1);
    }

    #[test]
    fn interrupts_subagents_and_compaction() {
        let out = hook("i", "Interrupt", json!({"turn_id": "t1"}));
        assert!(matches!(&out.events[0], Event::TurnEnd { outcome: TurnOutcome::Aborted, agent_id: None, .. }));
        let out = hook("i", "SubagentStart", json!({"agent_id": "g1", "agent_type": "guardian"}));
        assert!(matches!(&out.events[0], Event::TurnStart { agent_id: Some(a), .. } if a == "g1"));
        let out = hook("i", "SubagentStop", json!({"agent_id": "g1", "agent_type": "guardian"}));
        assert!(matches!(&out.events[0], Event::TurnEnd { agent_id: Some(a), .. } if a == "g1"));
        assert!(matches!(&hook("i", "PreCompact", json!({"trigger": "auto"})).events[0], Event::CompactStart { .. }));
        assert!(matches!(&hook("i", "SessionStart", json!({"source": "resume"})).events[0], Event::SessionStart { source: Some(s), .. } if s == "resume"));
    }

    #[test]
    fn missing_session_is_malformed() {
        assert!(normalize(&json!({"hook_event_name": "Stop"})).is_err());
    }
}
