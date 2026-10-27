//! The short title an agent gave each session, for the dashboard to show
//! instead of the folder name.
//!
//! Claude Code writes titles into the transcript as lines of their own:
//! `custom-title` (a `/rename`, or the title Claude Desktop gives), `ai-title`
//! (the one it generates) and `agent-name`. The latest of each kind counts,
//! and the kinds rank in that order. Like the Desktop records, this is not a
//! public format: lines that do not read as expected are skipped.
//!
//! Transcripts grow to megabytes, so each is read incrementally: only what
//! was appended since the last look, and only lines that name a title kind
//! are parsed.

use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::{BufRead, BufReader, Seek, SeekFrom};

use serde::Deserialize;

/// Longer titles are cut here (in characters).
const MAX_CHARS: usize = 120;
const KINDS: [&[u8]; 3] = [br#""type":"custom-title""#, br#""type":"ai-title""#, br#""type":"agent-name""#];

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct Line {
    #[serde(rename = "type")]
    kind: String,
    custom_title: Option<String>,
    ai_title: Option<String>,
    agent_name: Option<String>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Found {
    custom: Option<String>,
    ai: Option<String>,
    agent: Option<String>,
}

impl Found {
    pub fn best(&self) -> Option<&str> {
        self.custom.as_deref().or(self.ai.as_deref()).or(self.agent.as_deref())
    }

    fn take(&mut self, line: &[u8]) {
        if !KINDS.iter().any(|k| line.windows(k.len()).any(|w| w == *k)) {
            return;
        }
        let Ok(l) = serde_json::from_slice::<Line>(line) else { return };
        let clean = |v: Option<String>| v.map(|s| tidy(&s)).filter(|s| !s.is_empty());
        match l.kind.as_str() {
            "custom-title" => self.custom = clean(l.custom_title).or(self.custom.take()),
            "ai-title" => self.ai = clean(l.ai_title).or(self.ai.take()),
            "agent-name" => self.agent = clean(l.agent_name).or(self.agent.take()),
            _ => {}
        }
    }
}

/// One line, trimmed, at most `MAX_CHARS`.
fn tidy(s: &str) -> String {
    let one = s.split_whitespace().collect::<Vec<_>>().join(" ");
    match one.char_indices().nth(MAX_CHARS) {
        Some((i, _)) => format!("{}…", &one[..i]),
        None => one,
    }
}

struct Tail {
    path: String,
    /// Bytes read so far, up to the end of the last whole line.
    offset: u64,
    found: Found,
}

/// Reads the titles from the end of `path` onwards; `offset` moves past every
/// whole line read. A file that shrank (rewritten) is read again from the top.
fn read_from(path: &str, offset: &mut u64, found: &mut Found) -> std::io::Result<()> {
    let mut file = File::open(path)?;
    let len = file.metadata()?.len();
    if len < *offset {
        *offset = 0;
        *found = Found::default();
    }
    if len == *offset {
        return Ok(());
    }
    file.seek(SeekFrom::Start(*offset))?;
    let mut reader = BufReader::with_capacity(64 * 1024, file);
    let mut line = Vec::new();
    loop {
        line.clear();
        let n = reader.read_until(b'\n', &mut line)?;
        // A last line still being written waits for the next look.
        if n == 0 || line.last() != Some(&b'\n') {
            return Ok(());
        }
        *offset += n as u64;
        found.take(&line);
    }
}

/// The title in a transcript, read whole: for a session seen once.
pub fn read(path: &str) -> Option<String> {
    let (mut offset, mut found) = (0, Found::default());
    read_from(path, &mut offset, &mut found).ok()?;
    found.best().map(str::to_string)
}

/// What has been read of each session's transcript.
#[derive(Default)]
pub struct Titles {
    tails: HashMap<String, Tail>,
}

impl Titles {
    /// Reads what each `(key, transcript)` gained since the last call and
    /// returns every session's current title; forgets sessions not passed.
    pub fn scan(&mut self, sessions: &[(String, String)]) -> Vec<(String, String)> {
        let keep: HashSet<&String> = sessions.iter().map(|(k, _)| k).collect();
        self.tails.retain(|k, _| keep.contains(k));
        let mut titles = Vec::new();
        for (key, path) in sessions {
            let tail = self.tails.entry(key.clone()).or_insert_with(|| Tail { path: path.clone(), offset: 0, found: Found::default() });
            if tail.path != *path {
                *tail = Tail { path: path.clone(), offset: 0, found: Found::default() };
            }
            // A transcript not there yet (or unreadable) is tried again next time.
            let _ = read_from(&tail.path, &mut tail.offset, &mut tail.found);
            if let Some(t) = tail.found.best() {
                titles.push((key.clone(), t.to_string()));
            }
        }
        titles
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    #[test]
    fn ranks_the_kinds_follows_appends_and_skips_noise() {
        let dir = std::env::temp_dir().join(format!("agentsmon-titles-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("t.jsonl");
        let p = path.to_str().unwrap().to_string();
        let append = |s: &str| std::fs::OpenOptions::new().create(true).append(true).open(&path).unwrap().write_all(s.as_bytes()).unwrap();

        // Text that only mentions a kind, and a broken line, are not titles.
        append(r#"{"type":"user","message":{"content":"what is \"type\":\"ai-title\"?"}}"#);
        append("\n{\"type\":\"ai-title\",\"aiTitle\":\n");
        append(r#"{"type":"agent-name","agentName":"Helper","sessionId":"s"}"#);
        append("\n");
        let mut titles = Titles::default();
        let sessions = vec![("k".to_string(), p.clone())];
        assert_eq!(titles.scan(&sessions), vec![("k".to_string(), "Helper".to_string())]);

        // The generated title outranks the agent name; a half-written line waits.
        append("{\"type\":\"ai-title\",\"aiTitle\":\"  Fix   the\\nlogin flow \",\"sessionId\":\"s\"}\n");
        append(r#"{"type":"custom-title","customTitle":"Renamed""#);
        assert_eq!(titles.scan(&sessions)[0].1, "Fix the login flow");

        // A rename outranks both, once its line is complete.
        append(",\"sessionId\":\"s\"}\n");
        assert_eq!(titles.scan(&sessions)[0].1, "Renamed");
        assert_eq!(read(&p).as_deref(), Some("Renamed"));

        // A rewritten (shorter) file is read again from the top.
        std::fs::write(&path, "{\"type\":\"ai-title\",\"aiTitle\":\"Fresh\"}\n").unwrap();
        assert_eq!(titles.scan(&sessions)[0].1, "Fresh");

        // A missing transcript gives nothing, and sessions not passed are forgotten.
        assert!(titles.scan(&[("m".to_string(), dir.join("none").to_string_lossy().into())]).is_empty());
        assert!(!titles.tails.contains_key("k"));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn cuts_long_titles_on_a_character() {
        let long = "标题".repeat(100);
        let t = tidy(&long);
        assert_eq!(t.chars().count(), MAX_CHARS + 1);
        assert!(t.ends_with('…'));
    }
}
