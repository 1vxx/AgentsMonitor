//! The glanceable form of everything live: what a small display draws.

use agentsmon_protocol::{Mood, Quota, QuotaBrief, Session, Status, StatusCounts, Summary, SummaryItem, TurnOutcome};

use crate::util::project_name;

const MAX_ITEMS: usize = 8;
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

    // Most urgent first; among equals, the one waiting longest.
    live.sort_by(|a, b| {
        (a.stale, std::cmp::Reverse(a.status), a.status_since).cmp(&(b.stale, std::cmp::Reverse(b.status), b.status_since))
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
                status: s.status,
                since: s.status_since,
                tool: s.tools.last().map(|t| t.tool.clone()),
                stale: s.stale,
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
    fn nothing_live_is_offline() {
        let mut gone = new_session("claude-code", "a", 0);
        apply(&mut gone, 1, &Event::SessionEnd { reason: None });
        assert_eq!(build([&gone].into_iter(), &[], 2).mood, Mood::Offline);
    }
}
