//! The glanceable form of everything live: what a small display draws.

use agentsmon_protocol::{
    AttentionBrief, Mood, Quota, QuotaBrief, Session, Status, StatusCounts, Summary, SummaryItem, TurnBrief, TurnOutcome,
};

use crate::util::{preview, project_name};

const MAX_ITEMS: usize = 8;
/// Longest attention text a summary carries; a display shows two short lines.
const MAX_TEXT: usize = 120;
/// How long an errored last turn colours the mood while the session idles.
const ERROR_MOOD_MS: i64 = 5 * 60_000;

pub fn build<'a>(sessions: impl Iterator<Item = &'a Session>, quotas: &[Quota], now: i64) -> Summary {
    let mut counts = StatusCounts::default();
    let mut live: Vec<&Session> = Vec::new();
    let mut errored = false;

    for s in sessions.filter(|s| !s.is_ended()) {
        counts.live += 1;
        if s.stale {
            counts.stale += 1;
        }
        match s.status {
            Status::Idle => counts.idle += 1,
            Status::Thinking => counts.thinking += 1,
            Status::Tool => counts.tool += 1,
            Status::Compacting => counts.compacting += 1,
            Status::WaitingInput | Status::WaitingPermission => counts.waiting += 1,
            Status::Ended => {}
        }
        if s.status == Status::Idle
            && s.last_turn.as_ref().is_some_and(|t| {
                matches!(t.outcome, TurnOutcome::Error | TurnOutcome::Refusal) && now - t.ended_at < ERROR_MOOD_MS
            })
        {
            errored = true;
        }
        live.push(s);
    }

    live.sort_by(|a, b| {
        board_key(a).cmp(&board_key(b)).then(b.started_at.cmp(&a.started_at)).then_with(|| a.key.cmp(&b.key))
    });

    let fresh = |pred: fn(Status) -> bool| live.iter().any(|s| !s.stale && pred(s.status));
    let mood = if fresh(Status::is_waiting) {
        Mood::Attention
    } else if fresh(Status::is_working) {
        Mood::Working
    } else if errored {
        Mood::Error
    } else if counts.live > 0 {
        Mood::Idle
    } else {
        Mood::Offline
    };

    Summary {
        ts: now,
        mood,
        counts,
        sessions: live
            .into_iter()
            .take(MAX_ITEMS)
            .map(|s| SummaryItem {
                key: s.key.clone(),
                agent: s.agent.clone(),
                project: s
                    .meta
                    .project
                    .clone()
                    .or_else(|| s.meta.cwd.as_deref().map(project_name))
                    .unwrap_or_else(|| s.session_id.chars().take(8).collect()),
                title: s.meta.title.clone(),
                status: s.status,
                since: s.status_since,
                tool: s.tools.last().map(|t| t.tool.clone()),
                stale: s.stale,
                attention: s.attention.as_ref().map(|a| AttentionBrief {
                    kind: a.kind,
                    since: a.since,
                    tool: a.tool.clone(),
                    subject: a.subject.as_deref().and_then(|t| preview(t, MAX_TEXT)),
                    message: a.message.as_deref().and_then(|t| preview(t, MAX_TEXT)),
                }),
                last_turn: s.last_turn.as_ref().map(|t| TurnBrief { outcome: t.outcome, ended_at: t.ended_at }),
            })
            .collect(),
        quotas: quotas
            .iter()
            .map(|q| QuotaBrief {
                agent: q.agent.clone(),
                windows: q.windows.iter().map(|w| (w.kind.clone(), w.percent_used)).collect(),
            })
            .collect(),
    }
}

/// Where a session stands, as the Live view orders its cards. Bands first:
/// waiting on the person, working, idle, and last anything without a signal,
/// whatever its status says. Within a band, a moment that moves only on a
/// transition, never per tool call: the longest wait first, then the newest
/// turn, the latest to settle, the last heard from.
fn board_key(s: &Session) -> (u8, i64) {
    if s.stale {
        (3, -s.last_seen)
    } else if s.status.is_waiting() {
        (0, s.attention.as_ref().map_or(s.status_since, |a| a.since))
    } else if s.status.is_working() {
        let start = s.turn.as_ref().map(|t| t.started_at).or_else(|| s.subagents.first().map(|a| a.started_at));
        (1, -start.unwrap_or(s.status_since))
    } else {
        (2, -s.status_since)
    }
}

/// Two summaries say the same thing (the timestamp aside).
pub fn same(a: &Summary, b: &Summary) -> bool {
    a.mood == b.mood && a.counts == b.counts && a.sessions == b.sessions && a.quotas == b.quotas
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::reducer::{apply, new_session};
    use agentsmon_protocol::{AttentionKind, Event};

    #[test]
    fn waiting_wins_and_sorts_first() {
        let mut busy = new_session("claude-code", "a", 0);
        apply(&mut busy, 1, &Event::TurnStart { turn_id: None, agent_id: None, preview: None });
        let mut asking = new_session("claude-code", "b", 0);
        apply(&mut asking, 2, &Event::Attention { kind: AttentionKind::Permission, tool: None, subject: None, message: None });
        let summary = build([&busy, &asking].into_iter(), &[], 10);
        assert_eq!(summary.mood, Mood::Attention);
        assert_eq!(summary.sessions[0].key, "claude-code:b");
        assert_eq!((summary.counts.live, summary.counts.waiting, summary.counts.thinking), (2, 1, 1));
    }

    #[test]
    fn orders_by_band_then_moment() {
        let turn = Event::TurnStart { turn_id: None, agent_id: None, preview: None };
        let ask = |kind| Event::Attention { kind, tool: None, subject: None, message: None };
        let answered = Event::TurnEnd { turn_id: None, agent_id: None, outcome: TurnOutcome::Answer, duration_ms: None, usage: None, preview: None };
        let tool = Event::ToolStart { id: Some("t".into()), tool: "Bash".into(), agent_id: None, subject: None };

        let mut early_turn = new_session("claude-code", "early-turn", 0);
        apply(&mut early_turn, 10, &turn);
        apply(&mut early_turn, 90, &tool); // a tool call does not move it
        let mut late_turn = new_session("claude-code", "late-turn", 0);
        apply(&mut late_turn, 50, &turn);
        let mut asked_first = new_session("claude-code", "asked-first", 0);
        apply(&mut asked_first, 20, &ask(AttentionKind::Input));
        let mut asked_next = new_session("claude-code", "asked-next", 0);
        apply(&mut asked_next, 30, &ask(AttentionKind::Permission));
        let mut idle_long = new_session("claude-code", "idle-long", 0);
        apply(&mut idle_long, 5, &turn);
        apply(&mut idle_long, 6, &answered);
        let mut idle_new = new_session("claude-code", "idle-new", 0);
        apply(&mut idle_new, 5, &turn);
        apply(&mut idle_new, 60, &answered);
        let mut quiet = new_session("claude-code", "quiet", 0);
        apply(&mut quiet, 1, &ask(AttentionKind::Permission));
        quiet.stale = true;

        let all = [&quiet, &idle_long, &early_turn, &asked_next, &idle_new, &late_turn, &asked_first];
        let keys: Vec<_> = build(all.into_iter(), &[], 100).sessions.into_iter().map(|s| s.key).collect();
        let want = ["asked-first", "asked-next", "late-turn", "early-turn", "idle-new", "idle-long", "quiet"];
        assert_eq!(keys, want.map(|k| format!("claude-code:{k}")));
    }

    #[test]
    fn carries_what_a_session_waits_for_and_how_it_last_ended() {
        let mut asking = new_session("claude-code", "a", 0);
        let command = format!("npm run build {}", "x".repeat(200));
        apply(&mut asking, 2, &Event::Attention { kind: AttentionKind::Permission, tool: Some("Bash".into()), subject: Some(command), message: None });
        let mut done = new_session("claude-code", "b", 0);
        apply(&mut done, 1, &Event::TurnStart { turn_id: None, agent_id: None, preview: None });
        apply(&mut done, 5, &Event::TurnEnd { turn_id: None, agent_id: None, outcome: TurnOutcome::Error, duration_ms: None, usage: None, preview: None });

        let summary = build([&asking, &done].into_iter(), &[], 10);
        let wait = summary.sessions[0].attention.as_ref().unwrap();
        assert_eq!((wait.kind, wait.since, wait.tool.as_deref()), (AttentionKind::Permission, 2, Some("Bash")));
        let subject = wait.subject.as_deref().unwrap();
        assert!(subject.starts_with("npm run build") && subject.ends_with('…') && subject.chars().count() == MAX_TEXT);
        assert_eq!(summary.sessions[1].last_turn, Some(TurnBrief { outcome: TurnOutcome::Error, ended_at: 5 }));
    }

    #[test]
    fn nothing_live_is_offline() {
        let mut gone = new_session("claude-code", "a", 0);
        apply(&mut gone, 1, &Event::SessionEnd { reason: None });
        assert_eq!(build([&gone].into_iter(), &[], 2).mood, Mood::Offline);
    }
}
