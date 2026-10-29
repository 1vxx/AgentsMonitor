//! Keeps the daemon's data bounded and in step with the agents' own:
//! forgets ended sessions once they are old enough or once Claude Code
//! deleted their transcript (Codex, its thread), has the store prune and fold
//! the rest, and picks up the titles the agents give their sessions.

use std::collections::HashSet;
use std::path::Path;
use std::time::{Duration, Instant};

use serde::Serialize;

use crate::codex::Threads;
use crate::config::Retention;
use crate::desktop::Desktop;
use crate::hub::Hub;
use crate::store::{self, Cutoffs, Op};
use crate::titles::{self, Titles};
use crate::util::now_ms;

const DAY_MS: i64 = 86_400_000;

#[derive(Debug, Default, PartialEq, Serialize)]
pub struct Pass {
    pub too_old: usize,
    pub transcript_gone: usize,
    /// Ended sessions removed only because every ended one was asked for.
    pub ended: usize,
    /// Archived in Claude Desktop: out of the live view, kept in the history.
    pub archived: usize,
}

/// What a pass may forget.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Scope {
    /// What the retention says: old enough, or deleted in Claude Code.
    Retention,
    /// Every ended session as well (archived ones included).
    AllEnded,
}

/// What a pass did, for the person who asked for it.
#[derive(Debug, Serialize)]
pub struct Report {
    #[serde(flatten)]
    pub pass: Pass,
    pub forgotten: usize,
    pub db_bytes_before: u64,
    pub db_bytes_after: u64,
    pub duration_ms: u64,
}

pub fn cutoffs(now: i64, r: &Retention) -> Cutoffs {
    let days = |d: u32| now - d as i64 * DAY_MS;
    Cutoffs {
        events: days(r.events_days),
        quota_samples: days(r.quota_days),
        token_detail: days(r.token_detail_days),
        token_hourly: days(r.token_hourly_days),
        token_daily: days(r.token_daily_days),
    }
}

/// Which ended sessions to forget, and why.
pub fn sessions_to_forget(
    ended: &[(String, i64, Option<String>)],
    now: i64,
    r: &Retention,
    scope: Scope,
) -> (Vec<String>, Pass) {
    let mut pass = Pass::default();
    let mut keys = Vec::new();
    for (key, ended_at, transcript) in ended {
        let age = now - ended_at;
        if age > r.sessions_days as i64 * DAY_MS {
            pass.too_old += 1;
        } else if r.follow_transcripts && transcript.as_deref().is_some_and(is_gone) {
            pass.transcript_gone += 1;
        } else if scope == Scope::AllEnded {
            pass.ended += 1;
        } else {
            continue;
        }
        keys.push(key.clone());
    }
    (keys, pass)
}

/// Only a definite "not there": a path that cannot be checked is kept.
fn is_gone(path: &str) -> bool {
    matches!(Path::new(path).try_exists(), Ok(false))
}

/// What the quick pass remembers between runs.
pub struct Watch {
    /// Sessions whose transcript was there once.
    seen: HashSet<String>,
    desktop: Desktop,
    /// Codex's own records of its threads.
    codex: Threads,
    /// How far each live session's transcript has been read for its title.
    titles: Titles,
    /// Stored sessions already looked at for a title, found or not.
    titles_tried: HashSet<String>,
}

impl Default for Watch {
    fn default() -> Watch {
        Watch {
            seen: HashSet::new(),
            desktop: Desktop::new(),
            codex: Threads::new(),
            titles: Titles::default(),
            titles_tried: HashSet::new(),
        }
    }
}

#[derive(Debug, Default, PartialEq, Serialize)]
pub struct Quick {
    /// Forgotten: deleted in Claude Code or Codex.
    pub deleted: usize,
    /// Taken out of the live view: archived in Claude Desktop or Codex.
    pub archived: usize,
}

/// The quick pass, every few seconds, over the sessions in memory (what the
/// dashboard shows). Deleted in Claude Code: an ended session whose
/// transcript is gone (or never was), a running one whose transcript was
/// there and is gone; those are forgotten. Archived in Claude Desktop: taken
/// out of the live view, kept in the history. The rest get the title their
/// transcript gives them. Codex sessions follow their thread records the
/// same way: gone from them is deleted, and the name is the title.
pub fn quick_pass(hub: &Hub) -> Quick {
    let mut watch = hub.watch.lock();
    let Watch { seen, desktop, codex, titles, .. } = &mut *watch;
    let sessions = hub.presence();
    let mut gone = Vec::new();
    if hub.retention.follow_transcripts {
        for s in &sessions {
            let Some(path) = &s.transcript else { continue };
            match Path::new(path).try_exists() {
                Ok(true) => {
                    seen.insert(s.key.clone());
                }
                Ok(false) if s.ended || seen.contains(&s.key) => gone.push(s.key.clone()),
                _ => {}
            }
        }
    }
    let present: HashSet<&String> = sessions.iter().map(|s| &s.key).collect();
    seen.retain(|k| present.contains(k));

    let archived_ids = desktop.archived();
    let mut archived: Vec<String> = sessions
        .iter()
        .filter(|s| s.agent == agentsmon_protocol::agents::CLAUDE_CODE && archived_ids.contains(&s.session_id))
        .filter(|s| !gone.contains(&s.key))
        .map(|s| s.key.clone())
        .collect();
    let readable: Vec<(String, String)> = sessions
        .iter()
        .filter(|s| s.agent == agentsmon_protocol::agents::CLAUDE_CODE && !gone.contains(&s.key))
        .filter_map(|s| Some((s.key.clone(), s.transcript.clone()?)))
        .collect();
    let mut named = titles.scan(&readable);

    let codex_ids: Vec<String> =
        sessions.iter().filter(|s| s.agent == agentsmon_protocol::agents::CODEX).map(|s| s.session_id.clone()).collect();
    let followed = codex.follow(&codex_ids);
    let key = |id: &String| agentsmon_protocol::Session::key_for(agentsmon_protocol::agents::CODEX, id);
    gone.extend(followed.deleted.iter().map(key));
    archived.extend(followed.archived.iter().map(key));
    named.extend(followed.titles.iter().map(|(id, title)| (key(id), title.clone())));
    drop(watch);

    hub.set_titles(&named);
    let quick = Quick { deleted: hub.forget_many(&gone), archived: hub.hide_many(&archived) };
    if quick != Quick::default() {
        tracing::info!("{} deleted sessions forgotten, {} archived ones out of the live view", quick.deleted, quick.archived);
    }
    quick
}

/// Stored sessions from before titles were read (or that ended while the
/// daemon was away) get theirs from the transcript, each looked at once.
fn backfill_titles(hub: &Hub) {
    let in_memory: HashSet<String> = hub.presence().into_iter().map(|p| p.key).collect();
    let untitled = match hub.store().read(store::untitled_sessions) {
        Ok(rows) => rows,
        Err(e) => {
            tracing::warn!("janitor could not read untitled sessions: {e}");
            return;
        }
    };
    let mut watch = hub.watch.lock();
    let mut titled = 0;
    for (key, path) in untitled {
        if in_memory.contains(&key) || !watch.titles_tried.insert(key.clone()) {
            continue;
        }
        if let Some(title) = titles::read(&path) {
            hub.store().send(Op::Title { key, title });
            titled += 1;
        }
    }
    if titled > 0 {
        tracing::info!("titled {titled} stored sessions from their transcripts");
    }
}

/// One pass, the scheduled one or one a person asked for. Blocking (file
/// system and database), so run it off the async runtime; passes never
/// overlap.
pub fn run(hub: &Hub, scope: Scope) -> Report {
    let _one_at_a_time = hub.janitor.lock();
    let started = Instant::now();
    let store = hub.store();
    let wait = Duration::from_secs(10);
    let quick = quick_pass(hub);
    // What was just ingested is in the database before it is judged.
    store.flush(wait);
    let db_bytes_before = store.disk_bytes();
    let now = now_ms();
    let retention = hub.retention;
    let (keys, mut pass) = match store.read(store::ended_sessions) {
        Ok(ended) => sessions_to_forget(&ended, now, &retention, scope),
        Err(e) => {
            tracing::warn!("janitor could not read sessions: {e}");
            (Vec::new(), Pass::default())
        }
    };
    if !keys.is_empty() {
        hub.forget_many(&keys);
        tracing::info!(
            "forgot {} ended sessions ({} past retention, {} deleted in Claude Code, {} on request)",
            keys.len(),
            pass.too_old,
            pass.transcript_gone,
            pass.ended
        );
    }
    backfill_titles(hub);
    store.send(Op::Maintain(cutoffs(now, &retention)));
    store.flush(wait);
    pass.transcript_gone += quick.deleted;
    pass.archived = quick.archived;
    Report {
        pass,
        forgotten: keys.len() + quick.deleted,
        db_bytes_before,
        db_bytes_after: store.disk_bytes(),
        duration_ms: started.elapsed().as_millis() as u64,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn forgets_old_sessions_and_ones_whose_transcript_is_gone() {
        let dir = std::env::temp_dir().join(format!("agentsmon-janitor-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let kept = dir.join("kept.jsonl");
        std::fs::write(&kept, "{}").unwrap();
        let gone = dir.join("gone.jsonl").to_string_lossy().to_string();
        let kept = kept.to_string_lossy().to_string();

        let now = 100 * DAY_MS;
        let r = Retention::default();
        let hour = 3_600_000;
        let ended = vec![
            ("old".to_string(), now - 31 * DAY_MS, Some(kept.clone())),
            ("deleted".to_string(), now - hour, Some(gone.clone())),
            ("just-deleted".to_string(), now - 60_000, Some(gone.clone())),
            ("alive-transcript".to_string(), now - hour, Some(kept)),
            ("no-transcript-known".to_string(), now - hour, None),
        ];
        let (keys, pass) = sessions_to_forget(&ended, now, &r, Scope::Retention);
        // A deletion counts at once, however recent the end.
        assert_eq!(keys, vec!["old".to_string(), "deleted".to_string(), "just-deleted".to_string()]);
        assert_eq!(pass, Pass { too_old: 1, transcript_gone: 2, ended: 0, archived: 0 });

        let off = Retention { follow_transcripts: false, ..r };
        assert_eq!(sessions_to_forget(&ended, now, &off, Scope::Retention).0, vec!["old".to_string()]);

        // Asked for every ended one: the rest go too, counted apart.
        let (all, pass) = sessions_to_forget(&ended, now, &r, Scope::AllEnded);
        assert_eq!(all.len(), 5);
        assert_eq!(pass, Pass { too_old: 1, transcript_gone: 2, ended: 2, archived: 0 });
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn cutoffs_follow_the_retention() {
        let r = Retention::default();
        let c = cutoffs(1000 * DAY_MS, &r);
        assert_eq!(c.events, (1000 - 3) * DAY_MS);
        assert_eq!(c.token_detail, (1000 - 90) * DAY_MS);
        assert_eq!(c.token_daily, (1000 - 1825) * DAY_MS);
    }

    #[test]
    fn a_requested_pass_reports_and_leaves_live_sessions() {
        let dir = std::env::temp_dir().join(format!("agentsmon-janitor-run-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let hub = Hub::new(crate::store::Store::open(&dir.join("t.db")).unwrap(), crate::config::LiveView::default(), Retention::default());
        let env = |json: &str| serde_json::from_str::<agentsmon_protocol::Envelope>(json).unwrap();
        hub.ingest(vec![
            env(r#"{"agent":"claude-code","sid":"live","type":"prompt"}"#),
            env(r#"{"agent":"claude-code","sid":"done","type":"session.end"}"#),
        ]);
        let routine = run(&hub, Scope::Retention);
        assert_eq!(routine.forgotten, 0, "a session that just ended is within retention");
        let all = run(&hub, Scope::AllEnded);
        assert_eq!((all.forgotten, all.pass.ended), (1, 1));
        let left: Vec<String> = hub.sessions().into_iter().map(|s| s.key).collect();
        assert_eq!(left, vec!["claude-code:live".to_string()]);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn the_quick_pass_follows_deletions_and_archiving() {
        let dir = std::env::temp_dir().join(format!("agentsmon-quick-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let hub = Hub::new(crate::store::Store::open(&dir.join("t.db")).unwrap(), crate::config::LiveView::default(), Retention::default());
        // A Claude Desktop that archived the session "archived".
        let folder = dir.join("desktop").join("acct").join("org");
        std::fs::create_dir_all(&folder).unwrap();
        std::fs::write(folder.join("local_x.json"), r#"{"cliSessionId":"archived","isArchived":true}"#).unwrap();
        std::fs::write(folder.join("archived-sessions.idx"), r#"{"v":1,"archived":["local_x"]}"#).unwrap();
        *hub.watch.lock() = Watch { desktop: Desktop::at(dir.join("desktop")), ..Watch::default() };

        let file = |name: &str| dir.join(name).to_string_lossy().to_string();
        for name in ["ended.jsonl", "running.jsonl"] {
            std::fs::write(file(name), "{}").unwrap();
        }
        std::fs::write(file("kept.jsonl"), "{\"type\":\"ai-title\",\"aiTitle\":\"Kept work\"}\n").unwrap();
        std::fs::write(file("archived.jsonl"), "{\"type\":\"custom-title\",\"customTitle\":\"Old work\"}\n").unwrap();
        let start = |sid: &str, path: &str| format!(r#"{{"agent":"claude-code","sid":"{sid}","type":"session.start","transcript_path":"{path}"}}"#);
        let end = |sid: &str| format!(r#"{{"agent":"claude-code","sid":"{sid}","type":"session.end"}}"#);
        let env = |json: &str| serde_json::from_str::<agentsmon_protocol::Envelope>(json).unwrap();
        hub.ingest(vec![
            env(&start("ended", &file("ended.jsonl"))),
            env(&end("ended")),
            env(&start("running", &file("running.jsonl"))),
            env(&start("fresh", &file("not-written-yet.jsonl"))),
            env(&start("kept", &file("kept.jsonl"))),
            env(&end("kept")),
            env(&start("archived", &file("archived.jsonl"))),
            env(&end("archived")),
        ]);
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        assert_eq!(quick_pass(&hub), Quick { deleted: 0, archived: 1 });
        // A session in memory takes its title from the transcript.
        assert_eq!(hub.session("claude-code:kept").and_then(|s| s.meta.title).as_deref(), Some("Kept work"));

        // Deleted in Claude Code: one after it ended, one while it ran.
        std::fs::remove_file(file("ended.jsonl")).unwrap();
        std::fs::remove_file(file("running.jsonl")).unwrap();
        assert_eq!(quick_pass(&hub), Quick { deleted: 2, archived: 0 });
        let mut left: Vec<String> = hub.sessions().into_iter().map(|s| s.key).collect();
        left.sort();
        // A running session without a transcript yet is not taken for deleted.
        assert_eq!(left, vec!["claude-code:fresh".to_string(), "claude-code:kept".to_string()]);
        // The archived one left the live view only: the history keeps it.
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        assert!(hub.store().read(|c| store::session(c, "claude-code:archived")).unwrap().is_some());
        // ...and a stored one out of memory gets its title in the store.
        backfill_titles(&hub);
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        let stored = hub.store().read(|c| store::session(c, "claude-code:archived")).unwrap().unwrap();
        assert_eq!(stored.meta.title.as_deref(), Some("Old work"));
        std::fs::remove_dir_all(&dir).ok();
    }
}
