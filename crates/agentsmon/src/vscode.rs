//! Which Codex threads the person opened in VS Code.
//!
//! Codex's VS Code extension writes no read receipts. It keeps its own list of
//! unread threads in the editor's `globalStorage/state.vscdb`, in the JSON
//! under `openai.chatgpt`, as the atom `unread-thread-ids-by-host-v1`: a thread
//! goes on when a turn ends out of view and comes off once it shows. Coming off
//! is the person opening it; not being on says nothing, since a thread that
//! finished in view never was. Not a public format: read-only and best effort,
//! and anything that does not read as expected is ignored.

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use rusqlite::{Connection, OpenFlags, OptionalExtension};
use serde::Deserialize;

const EDITORS: [&str; 5] = ["Code", "Code - Insiders", "Cursor", "Windsurf", "VSCodium"];
const STATE: &str = "globalStorage/state.vscdb";
const EXTENSION: &str = "openai.chatgpt";

#[derive(Deserialize)]
struct Extension {
    #[serde(default, rename = "persisted-atom-state")]
    atoms: Atoms,
}

#[derive(Default, Deserialize)]
struct Atoms {
    /// Per host, the threads unread on it.
    #[serde(default, rename = "unread-thread-ids-by-host-v1")]
    unread: HashMap<String, Vec<String>>,
}

pub struct VsCode {
    /// Where editors keep their settings.
    roots: Vec<PathBuf>,
    /// Each database as last read: when it was written, and its unread threads.
    read: HashMap<PathBuf, (SystemTime, HashSet<String>)>,
    /// Threads unread at the last look.
    marked: HashSet<String>,
}

impl VsCode {
    pub fn new() -> VsCode {
        let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default();
        VsCode::at(vec![home.join("Library/Application Support"), home.join(".config")])
    }

    pub fn at(roots: Vec<PathBuf>) -> VsCode {
        VsCode { roots, read: HashMap::new(), marked: HashSet::new() }
    }

    /// Every editor's database, one per profile.
    fn databases(&self) -> Vec<PathBuf> {
        let mut out = Vec::new();
        for user in self.roots.iter().flat_map(|root| EDITORS.map(|e| root.join(e).join("User"))) {
            out.push(user.join(STATE));
            let Ok(profiles) = std::fs::read_dir(user.join("profiles")) else { continue };
            out.extend(profiles.filter_map(Result::ok).map(|p| p.path().join(STATE)));
        }
        out
    }

    /// Of these threads, those that came off the list since the last look.
    /// Reads a database again only when it was written since.
    pub fn opened(&mut self, ids: &HashSet<String>) -> Vec<String> {
        if ids.is_empty() {
            self.marked.clear();
            return Vec::new();
        }
        for db in self.databases() {
            let Ok(mtime) = std::fs::metadata(&db).and_then(|m| m.modified()) else { continue };
            if self.read.get(&db).is_none_or(|(seen, _)| *seen != mtime) {
                // One that cannot be read says nothing: what it listed stands
                if let Some(unread) = unread(&db) {
                    self.read.insert(db, (mtime, unread));
                }
            }
        }
        let marked: HashSet<String> =
            self.read.values().flat_map(|(_, unread)| unread).filter(|id| ids.contains(*id)).cloned().collect();
        let opened = self.marked.iter().filter(|id| ids.contains(*id) && !marked.contains(*id)).cloned().collect();
        self.marked = marked;
        opened
    }
}

/// The threads a database lists as unread; none before the extension ran.
fn unread(db: &Path) -> Option<HashSet<String>> {
    let c = Connection::open_with_flags(db, OpenFlags::SQLITE_OPEN_READ_ONLY | OpenFlags::SQLITE_OPEN_NO_MUTEX).ok()?;
    c.busy_timeout(Duration::from_millis(200)).ok()?;
    let state: Option<Vec<u8>> = c
        .query_row("SELECT value FROM ItemTable WHERE key = ?1", [EXTENSION], |r| Ok(r.get_ref(0)?.as_bytes()?.to_vec()))
        .optional()
        .ok()?;
    let Some(state) = state else { return Some(HashSet::new()) };
    let state: Extension = serde_json::from_slice(&state).ok()?;
    Some(state.atoms.unread.into_values().flatten().collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Writes an editor's database as VS Code does, stamped as given so that a
    /// coarse clock cannot hide the write.
    fn write(db: &Path, local: &str, at: SystemTime) {
        std::fs::create_dir_all(db.parent().unwrap()).unwrap();
        let c = Connection::open(db).unwrap();
        c.execute_batch("CREATE TABLE IF NOT EXISTS ItemTable (key TEXT UNIQUE ON CONFLICT REPLACE, value BLOB)").unwrap();
        let state = format!(
            r#"{{"persisted-atom-state":{{"unread-thread-ids-by-host-v1":{{"local":[{local}]}},"other":1}},"queued-follow-ups":{{}}}}"#
        );
        c.execute("INSERT INTO ItemTable VALUES (?1, ?2)", (EXTENSION, state.as_bytes())).unwrap();
        drop(c);
        std::fs::File::options().write(true).open(db).unwrap().set_modified(at).unwrap();
    }

    #[test]
    fn coming_off_the_list_is_the_thread_opened() {
        let root = std::env::temp_dir().join(format!("agentsmon-vscode-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let db = root.join("Code/User").join(STATE);
        let ids = HashSet::from(["a".to_string(), "b".to_string(), "c".to_string()]);
        let mut v = VsCode::at(vec![root.clone()]);
        let now = SystemTime::now();

        // Never listed: nothing is known of it, whatever else is
        write(&db, r#""a","b","other""#, now);
        assert!(v.opened(&ids).is_empty());
        assert_eq!(v.marked, HashSet::from(["a".to_string(), "b".to_string()]));

        // Nothing new, nothing read
        assert!(v.opened(&ids).is_empty());

        // "a" was looked at; "c", never listed, stays unknown
        write(&db, r#""b","other""#, now + Duration::from_secs(1));
        assert_eq!(v.opened(&ids), ["a"]);
        assert!(v.opened(&ids).is_empty());

        // A database that does not read keeps what it said
        std::fs::write(&db, "not a database").unwrap();
        std::fs::File::options().write(true).open(&db).unwrap().set_modified(now + Duration::from_secs(2)).unwrap();
        assert!(v.opened(&ids).is_empty());
        assert_eq!(v.marked, HashSet::from(["b".to_string()]));

        // Once a thread is at work again, or seen, its place is let go
        assert!(v.opened(&HashSet::from(["a".to_string()])).is_empty());
        assert!(v.marked.is_empty());
        std::fs::remove_dir_all(&root).ok();
    }

    #[test]
    fn profiles_have_databases_of_their_own() {
        let root = std::env::temp_dir().join(format!("agentsmon-vscode-profiles-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let db = root.join("Cursor/User/profiles/1a2b").join(STATE);
        write(&db, r#""a""#, SystemTime::now());
        let mut v = VsCode::at(vec![root.clone()]);
        v.opened(&HashSet::from(["a".to_string()]));
        assert!(v.marked.contains("a"));

        // An editor where the extension never ran has no row to read
        let empty = root.join("Code/User").join(STATE);
        std::fs::create_dir_all(empty.parent().unwrap()).unwrap();
        Connection::open(&empty).unwrap().execute_batch("CREATE TABLE ItemTable (key TEXT, value BLOB)").unwrap();
        assert_eq!(unread(&empty), Some(HashSet::new()));
        std::fs::remove_dir_all(&root).ok();
    }
}
