//! Folds canonical events into a [`Session`]. Pure: no clock, no I/O.
//!
//! The status is never set directly. Events only record what is in flight (the
//! main turn, tools, subagents, a pending question, a compaction) and
//! [`derive_status`] reads the status off those facts, so a lost or repeated
//! event can only leave a fact behind, never a status that contradicts them.

use agentsmon_protocol::{
    ActiveTool, ActiveTurn, Attention, AttentionKind, Event, LastTurn, Session, SessionMeta,
    SessionStats, Status, TurnOutcome,
};

pub fn new_session(agent: &str, session_id: &str, ts: i64) -> Session {
    Session {
        key: Session::key_for(agent, session_id),
        agent: agent.to_string(),
        session_id: session_id.to_string(),
        meta: SessionMeta::default(),
        status: Status::Idle,
        status_since: ts,
        started_at: ts,
        updated_at: ts,
        last_seen: ts,
        stale: false,
        ended_at: None,
        end_reason: None,
        turn: None,
        tools: Vec::new(),
        subagents: Vec::new(),
        attention: None,
        compacting: false,
        last_turn: None,
        last_prompt: None,
        context: None,
        cost_usd: None,
        stats: SessionStats::default(),
    }
}

pub fn derive_status(s: &Session) -> Status {
    if s.ended_at.is_some() {
        return Status::Ended;
    }
    match s.attention.as_ref().map(|a| a.kind) {
        Some(AttentionKind::Permission) => return Status::WaitingPermission,
        Some(AttentionKind::Input) => return Status::WaitingInput,
        _ => {}
    }
    if s.compacting {
        Status::Compacting
    } else if !s.tools.is_empty() {
        Status::Tool
    } else if s.turn.is_some() || !s.subagents.is_empty() {
        Status::Thinking
    } else {
        Status::Idle
    }
}

/// Clears everything in flight: the session is at rest.
fn settle(s: &mut Session) {
    s.turn = None;
    s.tools.clear();
    s.subagents.clear();
    s.attention = None;
    s.compacting = false;
}

/// Applies one event at `ts`. Returns whether anything a client shows changed
/// (a bare heartbeat changes nothing).
pub fn apply(s: &mut Session, ts: i64, ev: &Event) -> bool {
    let before = s.clone();
    let mut touch = true;

    // Activity after an end means the session came back (a resume).
    if s.ended_at.is_some() && !matches!(ev, Event::SessionEnd { .. } | Event::Raw { .. }) {
        s.ended_at = None;
        s.end_reason = None;
    }

    match ev {
        Event::SessionStart { meta, .. } => {
            s.meta.merge(meta);
        }
        Event::SessionEnd { reason } => {
            settle(s);
            s.ended_at = Some(ts);
            s.end_reason = reason.clone();
        }
        Event::Heartbeat { meta, busy } => {
            s.meta.merge(meta);
            if *busy == Some(false) {
                // Whatever is still in flight here lost its end on the way.
                let idle = s.attention.take().filter(|a| a.kind == AttentionKind::Idle);
                settle(s);
                s.attention = idle;
            }
            touch = false;
        }
        Event::Prompt { preview } => {
            s.stats.prompts += 1;
            if preview.is_some() {
                s.last_prompt = preview.clone();
            }
            clear_attention(s, |_| true);
        }
        Event::TurnStart { turn_id, agent_id, preview } => match agent_id {
            None => {
                s.turn = Some(ActiveTurn { id: turn_id.clone(), started_at: ts, preview: preview.clone() });
                clear_attention(s, |_| true);
            }
            Some(id) => {
                if !s.subagents.iter().any(|a| &a.id == id) {
                    s.subagents.push(agentsmon_protocol::Subagent {
                        id: id.clone(),
                        started_at: ts,
                        preview: preview.clone(),
                    });
                    s.stats.subagent_runs += 1;
                }
            }
        },
        Event::TurnEnd { agent_id, outcome, duration_ms, usage, preview, .. } => {
            // Exact per-request counts, once they flow, replace these.
            if let Some(u) = usage.as_ref().filter(|_| !s.stats.exact_tokens) {
                s.stats.input_tokens += u.input;
                s.stats.output_tokens += u.output;
                s.stats.cache_read_tokens += u.cache_read;
                s.stats.cache_write_tokens += u.cache_write;
            }
            match agent_id {
                None => {
                    let started = s.turn.take().map(|t| t.started_at);
                    // Subagents may run on in the background: they end on
                    // their own, or with a heartbeat that says nothing runs.
                    s.tools.retain(|t| t.agent_id.is_some());
                    s.attention = None;
                    s.compacting = false;
                    let duration = duration_ms.or(started.map(|t| (ts - t).max(0) as u64));
                    s.stats.turns += 1;
                    s.stats.busy_ms += duration.unwrap_or(0);
                    if matches!(outcome, TurnOutcome::Error | TurnOutcome::Refusal) {
                        s.stats.errors += 1;
                    }
                    s.last_turn = Some(LastTurn {
                        outcome: *outcome,
                        ended_at: ts,
                        duration_ms: duration,
                        preview: preview.clone(),
                    });
                }
                Some(id) => {
                    s.subagents.retain(|a| &a.id != id);
                    s.tools.retain(|t| t.agent_id.as_ref() != Some(id));
                }
            }
        }
        Event::ToolStart { id, tool, agent_id, subject } => {
            clear_attention(s, |a| a.kind == AttentionKind::Idle);
            if id.is_none() || !s.tools.iter().any(|t| t.id == *id) {
                s.tools.push(ActiveTool {
                    id: id.clone(),
                    tool: tool.clone(),
                    subject: subject.clone(),
                    agent_id: agent_id.clone(),
                    started_at: ts,
                });
                s.stats.tool_calls += 1;
            }
        }
        Event::ToolEnd { id, tool, agent_id, ok, .. } => {
            let found = match id {
                Some(_) => s.tools.iter().position(|t| t.id == *id),
                None => s.tools.iter().position(|t| &t.tool == tool && t.agent_id == *agent_id),
            };
            if let Some(i) = found {
                s.tools.remove(i);
            }
            if *ok == Some(false) {
                s.stats.tool_errors += 1;
            }
            // The call a question or an approval was about has moved on.
            clear_attention(s, |a| a.tool.as_ref().is_none_or(|t| t == tool));
        }
        Event::Attention { kind, tool, subject, message } => {
            s.attention = Some(Attention {
                kind: *kind,
                since: ts,
                tool: tool.clone(),
                subject: subject.clone(),
                message: message.clone(),
            });
        }
        Event::AttentionClear {} => s.attention = None,
        Event::CompactStart { .. } => s.compacting = true,
        Event::CompactEnd {} => s.compacting = false,
        Event::Usage { context, cost_usd, .. } => {
            if context.is_some() {
                s.context = context.clone();
            }
            if cost_usd.is_some() {
                s.cost_usd = *cost_usd;
            }
            touch = false;
        }
        Event::Notice { .. } | Event::Raw { .. } => touch = false,
    }

    if ts > s.last_seen {
        s.last_seen = ts;
    }
    s.stale = false;
    if touch && ts > s.updated_at {
        s.updated_at = ts;
    }
    let status = derive_status(s);
    if status != s.status {
        s.status = status;
        s.status_since = ts;
    }
    // last_seen alone moving is not worth a broadcast.
    let mut cmp = before;
    cmp.last_seen = s.last_seen;
    cmp != *s
}

fn clear_attention(s: &mut Session, pred: impl Fn(&Attention) -> bool) {
    if s.attention.as_ref().is_some_and(pred) {
        s.attention = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use agentsmon_protocol::TokenUsage;

    fn session() -> Session {
        new_session("claude-code", "s1", 0)
    }

    fn tool_start(id: &str, tool: &str) -> Event {
        Event::ToolStart { id: Some(id.into()), tool: tool.into(), agent_id: None, subject: None }
    }

    fn tool_end(id: &str, tool: &str) -> Event {
        Event::ToolEnd { id: Some(id.into()), tool: tool.into(), agent_id: None, ok: Some(true), duration_ms: None }
    }

    fn turn_start() -> Event {
        Event::TurnStart { turn_id: Some("t".into()), agent_id: None, preview: None }
    }

    fn turn_end(outcome: TurnOutcome) -> Event {
        Event::TurnEnd { turn_id: None, agent_id: None, outcome, duration_ms: None, usage: None, preview: None }
    }

    #[test]
    fn a_turn_walks_through_the_statuses() {
        let mut s = session();
        assert_eq!(s.status, Status::Idle);
        apply(&mut s, 1, &turn_start());
        assert_eq!(s.status, Status::Thinking);
        apply(&mut s, 2, &tool_start("a", "Bash"));
        assert_eq!((s.status, s.status_since), (Status::Tool, 2));
        apply(&mut s, 3, &tool_end("a", "Bash"));
        assert_eq!(s.status, Status::Thinking);
        apply(&mut s, 10, &turn_end(TurnOutcome::Answer));
        assert_eq!(s.status, Status::Idle);
        let last = s.last_turn.as_ref().unwrap();
        assert_eq!((last.outcome, last.duration_ms), (TurnOutcome::Answer, Some(9)));
        assert_eq!((s.stats.turns, s.stats.tool_calls), (1, 1));
    }

    #[test]
    fn permission_waits_until_the_tool_moves_on() {
        let mut s = session();
        apply(&mut s, 1, &turn_start());
        apply(&mut s, 2, &tool_start("a", "Bash"));
        apply(&mut s, 3, &Event::Attention { kind: AttentionKind::Permission, tool: Some("Bash".into()), subject: None, message: None });
        assert_eq!(s.status, Status::WaitingPermission);
        apply(&mut s, 4, &tool_end("a", "Bash"));
        assert_eq!(s.status, Status::Thinking);
    }

    #[test]
    fn a_question_outlives_other_tools_but_not_its_own() {
        let mut s = session();
        apply(&mut s, 1, &turn_start());
        apply(&mut s, 2, &tool_start("q", "AskUserQuestion"));
        apply(&mut s, 2, &Event::Attention { kind: AttentionKind::Input, tool: Some("AskUserQuestion".into()), subject: None, message: None });
        apply(&mut s, 3, &tool_end("x", "Read"));
        assert_eq!(s.status, Status::WaitingInput);
        apply(&mut s, 4, &tool_end("q", "AskUserQuestion"));
        assert_eq!(s.status, Status::Thinking);
    }

    #[test]
    fn subagents_keep_the_session_busy_and_carry_their_tools() {
        let mut s = session();
        apply(&mut s, 1, &turn_start());
        apply(&mut s, 2, &Event::TurnStart { turn_id: None, agent_id: Some("a1".into()), preview: None });
        apply(&mut s, 3, &Event::ToolStart { id: Some("x".into()), tool: "Read".into(), agent_id: Some("a1".into()), subject: None });
        apply(&mut s, 4, &Event::TurnEnd {
            turn_id: None,
            agent_id: Some("a1".into()),
            outcome: TurnOutcome::Answer,
            duration_ms: None,
            usage: Some(TokenUsage { input: 5, output: 7, ..Default::default() }),
            preview: None,
        });
        assert!(s.tools.is_empty() && s.subagents.is_empty());
        assert_eq!(s.status, Status::Thinking);
        assert_eq!((s.stats.subagent_runs, s.stats.output_tokens), (1, 7));
    }

    #[test]
    fn background_subagents_outlive_the_main_turn() {
        let mut s = session();
        apply(&mut s, 1, &turn_start());
        apply(&mut s, 2, &Event::TurnStart { turn_id: None, agent_id: Some("bg".into()), preview: None });
        apply(&mut s, 3, &turn_end(TurnOutcome::Answer));
        assert_eq!(s.status, Status::Thinking);
        apply(&mut s, 4, &Event::Heartbeat { busy: Some(false), meta: SessionMeta::default() });
        assert_eq!(s.status, Status::Idle);
    }

    #[test]
    fn an_interrupt_settles_the_main_loop() {
        let mut s = session();
        apply(&mut s, 1, &turn_start());
        apply(&mut s, 2, &tool_start("a", "Bash"));
        apply(&mut s, 3, &turn_end(TurnOutcome::Aborted));
        assert_eq!(s.status, Status::Idle);
        assert!(s.tools.is_empty());
    }

    #[test]
    fn end_and_resume() {
        let mut s = session();
        apply(&mut s, 1, &Event::SessionEnd { reason: Some("other".into()) });
        assert_eq!(s.status, Status::Ended);
        apply(&mut s, 2, &Event::Prompt { preview: Some("again".into()) });
        assert_eq!(s.status, Status::Idle);
        assert!(s.ended_at.is_none());
    }

    #[test]
    fn heartbeats_change_nothing_visible() {
        let mut s = session();
        assert!(!apply(&mut s, 5, &Event::Heartbeat { busy: None, meta: SessionMeta::default() }));
        assert_eq!(s.last_seen, 5);
        assert_eq!(s.updated_at, 0);
    }

    #[test]
    fn an_idle_heartbeat_drops_lost_work() {
        let mut s = session();
        apply(&mut s, 1, &turn_start());
        apply(&mut s, 2, &tool_start("a", "Bash"));
        assert!(apply(&mut s, 3, &Event::Heartbeat { busy: Some(false), meta: SessionMeta::default() }));
        assert_eq!((s.status, s.status_since), (Status::Idle, 3));
        assert!(!apply(&mut s, 4, &Event::Heartbeat { busy: Some(false), meta: SessionMeta::default() }));
    }

    #[test]
    fn a_parallel_tool_does_not_answer_a_pending_approval() {
        let mut s = session();
        apply(&mut s, 1, &turn_start());
        apply(&mut s, 2, &tool_start("a", "Bash"));
        apply(&mut s, 2, &tool_start("b", "Read"));
        apply(&mut s, 3, &Event::Attention { kind: AttentionKind::Permission, tool: Some("Bash".into()), subject: None, message: None });
        apply(&mut s, 4, &tool_end("b", "Read"));
        assert_eq!(s.status, Status::WaitingPermission);
    }
}
