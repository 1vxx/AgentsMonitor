//! Keeps the daemon's data bounded and in step with the agents' own:
//! forgets ended sessions once they are old enough or once Claude Code
//! deleted their transcript (Codex, its thread), has the store prune and fold
//! the rest, and picks up the titles the agents give their sessions.

use std::collections::HashSet;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant, UNIX_EPOCH};

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
    /// Stored sessions already looked at for a title, found or not, while
    /// the store lists them as untitled.
    titles_tried: HashSet<String>,
    /// Where terminals are found.
    dev: PathBuf,
}

impl Default for Watch {
    fn default() -> Watch {
        Watch {
            seen: HashSet::new(),
            desktop: Desktop::new(),
            codex: Threads::new(),
            titles: Titles::default(),
            titles_tried: HashSet::new(),
            dev: PathBuf::from("/dev"),
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

/// How long after a turn ends the displays show it as unseen, so how long the
/// seen pass looks out for it being seen.
const UNSEEN_MS: i64 = 60 * 60_000;

/// How long a terminal's stamp may lag what it read: input from before a turn
/// ended must not pass for input after it.
const TTY_LAG_MS: i64 = 2_000;

/// When a terminal last took input: keys, the mouse, and focus coming or going
/// where the terminal reports it. The kernel stamps its device for each read,
/// and what a session prints moves nothing.
fn touched(dev: &Path, tty: &str) -> Option<i64> {
    if !(tty.starts_with("tty") || tty.starts_with("pts/")) || tty.contains("..") {
        return None;
    }
    let at = std::fs::metadata(dev.join(tty)).ok()?.accessed().ok()?;
    Some(at.duration_since(UNIX_EPOCH).ok()?.as_millis() as i64)
}

/// The seen pass, every few seconds: for the idle sessions whose last turn
/// ended in the last hour and that the person has not seen since, whether
/// what each runs in says they have: a Codex client acknowledged the turn in
/// Codex's receipts, or the terminal took input after the turn ended. Sessions
/// report the rest themselves, with `attend`. Nothing is read while there are
/// none, which is most of the time.
pub fn seen_pass(hub: &Hub) -> usize {
    use agentsmon_protocol::Session;
    use agentsmon_protocol::agents::CODEX;
    let now = now_ms();
    let unseen: Vec<_> = hub
        .presence()
        .into_iter()
        .filter(|p| p.idle && !p.ended && p.unseen_since.is_some_and(|end| now - end < UNSEEN_MS))
        .collect();
    if unseen.is_empty() {
        return 0;
    }
    let watch = hub.watch.lock();
    let turns: Vec<(String, i64)> =
        unseen.iter().filter(|p| p.agent == CODEX).filter_map(|p| Some((p.session_id.clone(), p.turn_began?))).collect();
    let mut seen: Vec<(String, i64)> = if turns.is_empty() {
        Vec::new()
    } else {
        watch.codex.read(&turns).iter().map(|id| (Session::key_for(CODEX, id), now)).collect()
    };
    seen.extend(unseen.iter().filter_map(|p| {
        let at = touched(&watch.dev, p.tty.as_deref()?)?;
        (at >= p.unseen_since? + TTY_LAG_MS).then(|| (p.key.clone(), at))
    }));
    drop(watch);
    hub.set_seen(&seen)
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
    let listed: HashSet<&String> = untitled.iter().map(|(key, _)| key).collect();
    watch.titles_tried.retain(|key| listed.contains(key));
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
    fn remembers_the_titles_tried_only_while_the_store_lists_them() {
        let dir = std::env::temp_dir().join(format!("agentsmon-backfill-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let hub = Hub::new(crate::store::Store::open(&dir.join("t.db")).unwrap(), crate::config::LiveView::default(), Retention::default());
        let file = |sid: &str| dir.join(format!("{sid}.jsonl")).to_string_lossy().to_string();
        let env = |json: String| serde_json::from_str::<agentsmon_protocol::Envelope>(&json).unwrap();
        // Stored and out of memory, with no title in their transcripts
        for sid in ["kept", "forgotten"] {
            std::fs::write(file(sid), "{}").unwrap();
            hub.ingest(vec![
                env(format!(r#"{{"agent":"claude-code","sid":"{sid}","type":"session.start","transcript_path":"{}"}}"#, file(sid))),
                env(format!(r#"{{"agent":"claude-code","sid":"{sid}","type":"session.end"}}"#)),
            ]);
        }
        hub.hide_many(&["claude-code:kept".into(), "claude-code:forgotten".into()]);
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        backfill_titles(&hub);
        assert_eq!(hub.watch.lock().titles_tried.len(), 2);

        hub.forget("claude-code:forgotten");
        std::fs::write(file("kept"), "{\"type\":\"ai-title\",\"aiTitle\":\"Late\"}\n").unwrap();
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        backfill_titles(&hub);
        assert!(hub.store().flush(std::time::Duration::from_secs(5)));
        // Gone from the store, it is let go; still listed, it is not read twice
        assert_eq!(hub.watch.lock().titles_tried, HashSet::from(["claude-code:kept".to_string()]));
        let stored = hub.store().read(|c| store::session(c, "claude-code:kept")).unwrap().unwrap();
        assert_eq!(stored.meta.title, None);
        std::fs::remove_dir_all(&dir).ok();
    }

    /// A terminal device stamped with when it last took input.
    fn terminal(dev: &Path, tty: &str, at: i64) {
        let path = dev.join(tty);
        std::fs::write(&path, "").unwrap();
        let at = UNIX_EPOCH + Duration::from_millis(at as u64);
        let times = std::fs::FileTimes::new().set_accessed(at);
        std::fs::File::options().write(true).open(&path).unwrap().set_times(times).unwrap();
    }

    #[test]
    fn the_seen_pass_looks_only_at_idle_sessions_finished_in_the_last_hour() {
        let dir = std::env::temp_dir().join(format!("agentsmon-seen-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let hub = Hub::new(crate::store::Store::open(&dir.join("t.db")).unwrap(), crate::config::LiveView::default(), Retention::default());
        let now = now_ms();
        // Each terminal took input just now
        for tty in ["ttys001", "ttys002", "ttys003"] {
            terminal(&dir, tty, now);
        }
        *hub.watch.lock() = Watch { dev: dir.clone(), ..Watch::default() };

        let event = |sid: &str, ts: i64, rest: &str| {
            serde_json::from_str::<agentsmon_protocol::Envelope>(&format!(r#"{{"agent":"claude-code","sid":"{sid}","ts":{ts},{rest}}}"#)).unwrap()
        };
        let start = r#""type":"turn.start""#;
        let end = r#""type":"turn.end","outcome":"answer""#;
        let hour = 3_600_000;
        hub.ingest(vec![
            event("recent", now - 300_000, r#""type":"session.start","tty":"ttys001""#),
            event("old", now - 3 * hour, r#""type":"session.start","tty":"ttys002""#),
            event("busy", now - 300_000, r#""type":"session.start","tty":"ttys003""#),
            // Finished a minute ago: looked for
            event("recent", now - 120_000, start),
            event("recent", now - 60_000, end),
            // Finished two hours ago: past what the displays show
            event("old", now - 2 * hour - 60_000, start),
            event("old", now - 2 * hour, end),
            // Finished, then at work again
            event("busy", now - 120_000, start),
            event("busy", now - 90_000, end),
            event("busy", now - 30_000, start),
        ]);
        assert_eq!(seen_pass(&hub), 1);
        let seen = |sid: &str| hub.session(&format!("claude-code:{sid}")).and_then(|s| s.seen_at);
        assert_eq!((seen("recent"), seen("old"), seen("busy")), (Some(now), None, None));
        // Seen now, it is not looked for again
        assert_eq!(seen_pass(&hub), 0);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn a_terminal_counts_once_it_took_input_after_the_turn_ended() {
        let dir = std::env::temp_dir().join(format!("agentsmon-touched-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let hub = Hub::new(crate::store::Store::open(&dir.join("t.db")).unwrap(), crate::config::LiveView::default(), Retention::default());
        let now = now_ms();
        let ended = now - 60_000;
        *hub.watch.lock() = Watch { dev: dir.clone(), ..Watch::default() };
        let event = |ts: i64, rest: &str| {
            serde_json::from_str::<agentsmon_protocol::Envelope>(&format!(r#"{{"agent":"claude-code","sid":"s","ts":{ts},{rest}}}"#)).unwrap()
        };
        hub.ingest(vec![
            event(ended - 30_000, r#""type":"session.start","tty":"ttys001""#),
            event(ended - 20_000, r#""type":"turn.start""#),
            event(ended, r#""type":"turn.end","outcome":"answer""#),
        ]);
        let seen = || hub.session("claude-code:s").and_then(|s| s.seen_at);
        // Typed before it ended, or as it ended
        terminal(&dir, "ttys001", ended - 1_000);
        seen_pass(&hub);
        terminal(&dir, "ttys001", ended + TTY_LAG_MS - 1);
        seen_pass(&hub);
        assert_eq!(seen(), None);
        // Touched since: left for another tab, say, or clicked in
        terminal(&dir, "ttys001", ended + 30_000);
        seen_pass(&hub);
        assert_eq!(seen(), Some(ended + 30_000));

        assert_eq!(touched(&dir, "../t.db"), None);
        assert_eq!(touched(&dir, "tty/../../t.db"), None);
        assert_eq!(touched(&dir, "??"), None);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn a_turn_a_codex_client_acknowledged_is_seen() {
        let dir = std::env::temp_dir().join(format!("agentsmon-receipts-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let hub = Hub::new(crate::store::Store::open(&dir.join("t.db")).unwrap(), crate::config::LiveView::default(), Retention::default());
        let now = now_ms();
        let (began, ended) = (now - 90_000, now - 60_000);
        // A Codex client acknowledged "read" through its last turn, "unread" not
        let codex = dir.join("codex");
        std::fs::create_dir_all(&codex).unwrap();
        let hex = format!("{began:012x}");
        let turn = format!("{}-{}-7000-8000-000000000000", &hex[..8], &hex[8..]);
        rusqlite::Connection::open(codex.join("state_5.sqlite"))
            .unwrap()
            .execute_batch(&format!(
                "CREATE TABLE thread_read_receipts (thread_id TEXT PRIMARY KEY NOT NULL, first_unread_turn TEXT, revision TEXT NOT NULL, last_published_turn TEXT);
                 INSERT INTO thread_read_receipts VALUES ('read', NULL, 'r1', '{turn}'), ('unread', '{turn}', 'r2', '{turn}');"
            ))
            .unwrap();
        *hub.watch.lock() = Watch { codex: Threads::at(codex), dev: dir.clone(), ..Watch::default() };

        let event = |sid: &str, ts: i64, rest: &str| {
            serde_json::from_str::<agentsmon_protocol::Envelope>(&format!(r#"{{"agent":"codex","sid":"{sid}","ts":{ts},{rest}}}"#)).unwrap()
        };
        let mut batch = Vec::new();
        for sid in ["read", "unread"] {
            batch.push(event(sid, began, r#""type":"turn.start""#));
            batch.push(event(sid, ended, r#""type":"turn.end","outcome":"answer""#));
        }
        hub.ingest(batch);
        assert_eq!(seen_pass(&hub), 1);
        assert!(hub.session("codex:read").and_then(|s| s.seen_at).is_some());
        assert_eq!(hub.session("codex:unread").and_then(|s| s.seen_at), None);
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
