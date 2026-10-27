//! Reading on in log files other programs append to.
//!
//! A collector that left off at some offset reads on from there only while
//! the file still starts as it did. One that shrank, or begins with other
//! bytes, was replaced (rewritten, or a new file under the old name), and is
//! read again from the top. A file's [`Head`] is how it is known again, in
//! this process and, kept beside its cursor in the store, after a restart.
//! [`Dirs`] finds the files without listing every folder on every pass.

use std::collections::HashMap;
use std::fs::File;
use std::io::{BufRead, BufReader, Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

/// How much of a file's start identifies it: an agent log's first line names
/// its session and when it began.
const HEAD_BYTES: u64 = 1024;

/// A hash of a file's first bytes, and how many there were.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Head {
    len: u64,
    hash: u64,
}

impl Head {
    /// The head of `file`, `len` bytes long.
    pub fn of(file: &mut File, len: u64) -> std::io::Result<Head> {
        let n = len.min(HEAD_BYTES);
        Ok(Head { len: n, hash: fnv1a(&prefix(file, n)?) })
    }

    /// Whether `file` still starts with the bytes this head was taken from.
    pub fn matches(&self, file: &mut File) -> std::io::Result<bool> {
        let bytes = prefix(file, self.len)?;
        Ok(bytes.len() as u64 == self.len && fnv1a(&bytes) == self.hash)
    }

    /// Whether more of a file `len` bytes long would identify it better: it
    /// was taken while the file was shorter.
    pub fn short_of(&self, len: u64) -> bool {
        self.len < HEAD_BYTES && len > self.len
    }

    pub fn encode(&self) -> String {
        format!("{}:{:016x}", self.len, self.hash)
    }

    pub fn decode(s: &str) -> Option<Head> {
        let (len, hash) = s.split_once(':')?;
        Some(Head { len: len.parse().ok()?, hash: u64::from_str_radix(hash, 16).ok()? })
    }
}

/// Whether the file read up to `read`, `head` taken of it, is another one
/// now: shorter than what was read, or starting with other bytes.
pub fn replaced(file: &mut File, len: u64, read: u64, head: Option<&Head>) -> std::io::Result<bool> {
    if len < read {
        return Ok(true);
    }
    match head {
        Some(h) => Ok(!h.matches(file)?),
        None => Ok(false),
    }
}

/// Where a reader is in one file.
#[derive(Debug, Default)]
pub struct Position {
    /// Past the last whole line handled.
    pub read: u64,
    /// In a line longer than a chunk (an image, a huge tool output), which is
    /// no record: skipped to its end.
    pub in_long_line: bool,
}

/// What is read from a file at a time: memory stays at this and the longest
/// line, however much a pass reads.
const READ_BUF: usize = 64 << 10;

impl Position {
    /// Hands `take` each whole line from `read` on, with the offset where it
    /// ends, reading at most `chunk` bytes of the file `len` bytes long. A
    /// line still being written is left for the next call; one as long as a
    /// chunk is no record and is skipped to its end. Returns whether it moved.
    pub fn read_lines(&mut self, file: &mut File, len: u64, chunk: u64, mut take: impl FnMut(&[u8], u64)) -> std::io::Result<bool> {
        let start = self.read;
        file.seek(SeekFrom::Start(self.read))?;
        let mut reader = BufReader::with_capacity(READ_BUF, Read::take(&mut *file, chunk.min(len.saturating_sub(self.read))));
        let mut line = Vec::new();
        let mut consumed = self.read;
        loop {
            let buf = reader.fill_buf()?;
            if buf.is_empty() {
                break;
            }
            let newline = buf.iter().position(|&b| b == b'\n');
            let n = newline.map_or(buf.len(), |i| i + 1);
            if !self.in_long_line {
                let piece = &buf[..newline.unwrap_or(buf.len())];
                if (line.len() + piece.len()) as u64 >= chunk {
                    self.in_long_line = true;
                    line = Vec::new();
                } else {
                    line.extend_from_slice(piece);
                }
            }
            reader.consume(n);
            consumed += n as u64;
            if newline.is_some() {
                if !self.in_long_line {
                    take(&line, consumed);
                }
                self.in_long_line = false;
                line.clear();
                self.read = consumed;
            }
        }
        // A long line is not read again; a short one still being written is.
        if self.in_long_line {
            self.read = consumed;
        }
        Ok(self.read > start)
    }
}

/// A folder's entry.
#[derive(Clone, Debug, PartialEq)]
pub struct Entry {
    pub path: PathBuf,
    pub is_dir: bool,
}

/// Folder listings, read again only when a folder changed: adding, removing
/// or renaming an entry moves its modification time. Listing every folder of
/// an agent's logs on every pass was most of what a collector cost.
#[derive(Default)]
pub struct Dirs {
    listed: HashMap<PathBuf, Listing>,
}

struct Listing {
    modified: SystemTime,
    /// When it was read: a change within the same tick of a coarse clock
    /// (one second on HFS+) leaves the modification time where it was.
    at: SystemTime,
    entries: Vec<Entry>,
    touched: bool,
}

/// How long after a folder's last change its listing is still read again.
const SETTLE: Duration = Duration::from_secs(2);

impl Dirs {
    /// The entries of `dir`, sorted by name; none when it cannot be read.
    pub fn list(&mut self, dir: &Path) -> &[Entry] {
        let Ok(modified) = std::fs::metadata(dir).and_then(|m| m.modified()) else {
            self.listed.remove(dir);
            return &[];
        };
        let settled = |l: &Listing| l.modified == modified && l.at.duration_since(modified).is_ok_and(|d| d > SETTLE);
        if !self.listed.get(dir).is_some_and(settled) {
            let at = SystemTime::now();
            let mut entries: Vec<Entry> = std::fs::read_dir(dir)
                .map(|it| {
                    it.filter_map(Result::ok)
                        .map(|e| {
                            let path = e.path();
                            // A link is followed, as the agent follows it.
                            let is_dir = e.file_type().is_ok_and(|t| t.is_dir() || (t.is_symlink() && path.is_dir()));
                            Entry { path, is_dir }
                        })
                        .collect()
                })
                .unwrap_or_default();
            entries.sort_by(|a, b| a.path.cmp(&b.path));
            self.listed.insert(dir.to_path_buf(), Listing { modified, at, entries, touched: true });
        }
        let listing = self.listed.get_mut(dir).expect("just listed");
        listing.touched = true;
        &listing.entries
    }

    /// Forgets the folders not listed since the last call: gone, or no
    /// longer looked at.
    pub fn forget_untouched(&mut self) {
        self.listed.retain(|_, l| std::mem::take(&mut l.touched));
    }
}

fn prefix(file: &mut File, n: u64) -> std::io::Result<Vec<u8>> {
    file.seek(SeekFrom::Start(0))?;
    let mut buf = Vec::with_capacity(n as usize);
    Read::take(&mut *file, n).read_to_end(&mut buf)?;
    Ok(buf)
}

/// FNV-1a: the same in every build, which the standard library's hasher is
/// not promised to be.
fn fnv1a(bytes: &[u8]) -> u64 {
    bytes.iter().fold(0xcbf2_9ce4_8422_2325, |h, &b| (h ^ b as u64).wrapping_mul(0x0100_0000_01b3))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    #[test]
    fn knows_a_file_again_and_tells_a_replaced_one() {
        let dir = std::env::temp_dir().join(format!("agentsmon-tail-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("log.jsonl");
        std::fs::write(&path, "{\"session\":\"a\"}\n").unwrap();
        let mut f = File::open(&path).unwrap();
        let head = Head::of(&mut f, 16).unwrap();
        assert_eq!(Head::decode(&head.encode()), Some(head));
        assert!(head.short_of(2_000) && !head.short_of(16));

        // Grown: the same file.
        std::fs::OpenOptions::new().append(true).open(&path).unwrap().write_all(b"{\"more\":1}\n").unwrap();
        let mut f = File::open(&path).unwrap();
        assert!(!replaced(&mut f, 27, 16, Some(&head)).unwrap());
        // Rewritten longer, with another start: a new file.
        std::fs::write(&path, "{\"session\":\"b\"}\n{\"more\":2}\n{\"more\":3}\n").unwrap();
        let mut f = File::open(&path).unwrap();
        assert!(replaced(&mut f, 38, 16, Some(&head)).unwrap());
        // Shorter than what was read: a new file, head or not.
        assert!(replaced(&mut f, 10, 16, None).unwrap());
        assert!(!replaced(&mut f, 38, 16, None).unwrap());
        assert_eq!(Head::decode("nonsense"), None);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn lists_a_folder_again_only_once_it_changed() {
        let dir = std::env::temp_dir().join(format!("agentsmon-dirs-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(dir.join("sub")).unwrap();
        std::fs::write(dir.join("b.jsonl"), "").unwrap();
        let mut dirs = Dirs::default();
        let names = |entries: &[Entry]| entries.iter().map(|e| (e.path.file_name().unwrap().to_string_lossy().into_owned(), e.is_dir)).collect::<Vec<_>>();
        assert_eq!(names(dirs.list(&dir)), vec![("b.jsonl".into(), false), ("sub".into(), true)]);

        // Changed a moment ago: listed again, so nothing added in the same
        // tick of a coarse clock is missed.
        std::fs::write(dir.join("a.jsonl"), "").unwrap();
        assert_eq!(dirs.list(&dir).len(), 3);
        // Settled: the listing held is used, until the folder changes.
        let l = dirs.listed.get_mut(&dir).unwrap();
        l.at = l.modified + Duration::from_secs(5);
        l.entries.pop();
        assert_eq!(dirs.list(&dir).len(), 2);
        std::fs::remove_file(dir.join("b.jsonl")).unwrap();
        assert_eq!(names(dirs.list(&dir)), vec![("a.jsonl".into(), false), ("sub".into(), true)]);

        // A folder gone lists nothing, and one not listed since is forgotten.
        assert!(dirs.list(&dir.join("nowhere")).is_empty());
        dirs.list(&dir.join("sub"));
        dirs.forget_untouched();
        dirs.list(&dir);
        dirs.forget_untouched();
        assert_eq!(dirs.listed.keys().collect::<Vec<_>>(), vec![&dir]);
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn reads_whole_lines_a_chunk_at_a_time_and_skips_long_ones() {
        let dir = std::env::temp_dir().join(format!("agentsmon-lines-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("log.jsonl");
        // A line exactly a chunk long, then one past it: neither is read.
        let text = format!("a\nbb\n{}\nccc\n{}\nee\ndd", "x".repeat(16), "y".repeat(40));
        std::fs::write(&path, &text).unwrap();
        let mut pos = Position::default();
        let mut lines: Vec<(String, u64)> = Vec::new();
        let pass = |pos: &mut Position, lines: &mut Vec<(String, u64)>| {
            let mut f = File::open(&path).unwrap();
            let len = f.metadata().unwrap().len();
            pos.read_lines(&mut f, len, 16, |l, end| lines.push((String::from_utf8_lossy(l).into_owned(), end))).unwrap()
        };
        while pass(&mut pos, &mut lines) {}
        let got: Vec<(&str, u64)> = lines.iter().map(|(l, e)| (l.as_str(), *e)).collect();
        assert_eq!(got, vec![("a", 2), ("bb", 5), ("ccc", 26), ("ee", 70)]);
        // The last line waits until it is complete.
        assert_eq!(pos.read, 70);
        std::fs::OpenOptions::new().append(true).open(&path).unwrap().write_all(b"\n").unwrap();
        assert!(pass(&mut pos, &mut lines));
        assert_eq!(lines.last().map(|(l, e)| (l.as_str(), *e)), Some(("dd", 73)));
        assert!(!pass(&mut pos, &mut lines));
        std::fs::remove_dir_all(&dir).ok();
    }
}
