//! What Claude Desktop knows about its sessions that Claude Code does not
//! say: which ones the person archived.
//!
//! Claude Desktop keeps one record per session under
//! `~/Library/Application Support/Claude/claude-code-sessions/<account>/<org>/`:
//! `local_<id>.json`, whose `cliSessionId` is the Claude Code session id, and
//! `archived-sessions.idx`, the archived ones' local ids. Neither is a public
//! format: everything here is read-only and best effort, and anything that
//! does not read as expected is ignored.

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::time::SystemTime;

use serde::Deserialize;

const INDEX: &str = "archived-sessions.idx";

#[derive(Deserialize)]
struct Index {
    #[serde(default)]
    archived: Vec<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct Record {
    cli_session_id: Option<String>,
}

pub struct Desktop {
    root: PathBuf,
    /// Per account/org folder: the index's mtime when read, and what it listed.
    indexes: HashMap<PathBuf, (SystemTime, Vec<String>)>,
    /// Local id to Claude Code session id, read once per session.
    cli_ids: HashMap<String, Option<String>>,
}

impl Desktop {
    pub fn new() -> Desktop {
        let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default();
        Desktop::at(home.join("Library/Application Support/Claude/claude-code-sessions"))
    }

    pub fn at(root: PathBuf) -> Desktop {
        Desktop { root, indexes: HashMap::new(), cli_ids: HashMap::new() }
    }

    fn folders(&self) -> Vec<PathBuf> {
        let dirs = |p: &Path| -> Vec<PathBuf> {
            std::fs::read_dir(p)
                .map(|it| it.filter_map(Result::ok).map(|e| e.path()).filter(|p| p.is_dir()).collect())
                .unwrap_or_default()
        };
        dirs(&self.root).iter().flat_map(|account| dirs(account)).collect()
    }

    /// Claude Code session ids of the sessions archived in Claude Desktop.
    /// Re-reads an index only when it changed; empty without Claude Desktop.
    pub fn archived(&mut self) -> HashSet<String> {
        let mut out = HashSet::new();
        for folder in self.folders() {
            let index = folder.join(INDEX);
            let Ok(mtime) = std::fs::metadata(&index).and_then(|m| m.modified()) else { continue };
            if self.indexes.get(&folder).is_none_or(|(seen, _)| *seen != mtime) {
                let listed = std::fs::read(&index)
                    .ok()
                    .and_then(|b| serde_json::from_slice::<Index>(&b).ok())
                    .map(|i| i.archived)
                    .unwrap_or_default();
                self.indexes.insert(folder.clone(), (mtime, listed));
            }
            let Some((_, listed)) = self.indexes.get(&folder) else { continue };
            for local in listed {
                let cli = self.cli_ids.entry(local.clone()).or_insert_with(|| {
                    std::fs::read(folder.join(format!("{local}.json")))
                        .ok()
                        .and_then(|b| serde_json::from_slice::<Record>(&b).ok())
                        .and_then(|r| r.cli_session_id)
                });
                if let Some(id) = cli {
                    out.insert(id.clone());
                }
            }
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_archived_sessions_and_follows_the_index() {
        let root = std::env::temp_dir().join(format!("agentsmon-desktop-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let folder = root.join("account").join("org");
        std::fs::create_dir_all(&folder).unwrap();
        let record = |local: &str, cli: &str| {
            std::fs::write(folder.join(format!("{local}.json")), format!(r#"{{"sessionId":"{local}","cliSessionId":"{cli}","isArchived":true}}"#)).unwrap()
        };
        record("local_a", "cli-a");
        record("local_b", "cli-b");
        std::fs::write(folder.join("local_junk.json"), "not json").unwrap();
        std::fs::write(folder.join(INDEX), r#"{"v":1,"archived":["local_a","local_junk","local_missing"]}"#).unwrap();

        let mut d = Desktop::at(root.clone());
        assert_eq!(d.archived(), HashSet::from(["cli-a".to_string()]));

        // Archiving another one rewrites the index.
        std::thread::sleep(std::time::Duration::from_millis(20));
        std::fs::write(folder.join(INDEX), r#"{"v":1,"archived":["local_a","local_b"]}"#).unwrap();
        let later = SystemTime::now() + std::time::Duration::from_secs(1);
        std::fs::File::options().write(true).open(folder.join(INDEX)).unwrap().set_modified(later).unwrap();
        assert_eq!(d.archived(), HashSet::from(["cli-a".to_string(), "cli-b".to_string()]));

        assert!(Desktop::at(root.join("nowhere")).archived().is_empty());
        std::fs::remove_dir_all(&root).ok();
    }
}
