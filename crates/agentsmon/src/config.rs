//! `~/.agentsmon/config.toml`, every field optional. `AGENTSMON_HOME` moves
//! the whole folder.

use std::net::SocketAddr;
use std::path::{Path, PathBuf};
use std::time::Duration;

use serde::Deserialize;

pub const DEFAULT_HTTP: &str = "127.0.0.1:7788";

#[derive(Debug, Clone)]
pub struct Config {
    pub home: PathBuf,
    /// Where integrations on this machine send events; owner-only.
    pub socket: PathBuf,
    /// The dashboard's and the display device's door. `None` turns it off.
    pub http: Option<SocketAddr>,
    /// Bearer token the TCP listener asks for; required off loopback.
    pub token: Option<String>,
    /// Browser origins allowed besides loopback ones.
    pub allowed_origins: Vec<String>,
    pub db: PathBuf,
    pub retention: Retention,
    /// How long an ended session stays in memory (and in the live view).
    pub keep_ended_hours: u32,
    /// How Claude Code's usage limits are fetched again; `None` never.
    pub claude_refresh: Option<crate::claude::Refresh>,
}

/// How long each kind of data lives, so the database stays bounded.
#[derive(Debug, Clone, Copy, PartialEq, serde::Serialize)]
pub struct Retention {
    /// Ended sessions, with their events, are forgotten this many days after
    /// they ended.
    pub sessions_days: u32,
    /// An ended session whose transcript Claude Code deleted (its cleanup,
    /// or a deletion in the app) is forgotten too.
    pub follow_transcripts: bool,
    pub events_days: u32,
    pub quota_days: u32,
    /// Token counts: per request, then per hour, then per day; each step
    /// keeps the totals exact, and the last one ends the history.
    pub token_detail_days: u32,
    pub token_hourly_days: u32,
    pub token_daily_days: u32,
}

impl Default for Retention {
    fn default() -> Retention {
        Retention {
            sessions_days: 30,
            follow_transcripts: true,
            events_days: 30,
            quota_days: 30,
            token_detail_days: 90,
            token_hourly_days: 400,
            token_daily_days: 1825,
        }
    }
}

#[derive(Debug, Default, Deserialize)]
#[serde(deny_unknown_fields)]
struct File {
    socket: Option<String>,
    #[serde(default)]
    http: HttpFile,
    #[serde(default)]
    store: StoreFile,
    #[serde(default)]
    sessions: SessionsFile,
    #[serde(default)]
    retention: RetentionFile,
    #[serde(default)]
    claude: ClaudeFile,
}

#[derive(Debug, Default, Deserialize)]
#[serde(deny_unknown_fields)]
struct HttpFile {
    /// `"127.0.0.1:7788"`, or `""` / `"off"` to turn TCP off.
    listen: Option<String>,
    token: Option<String>,
    #[serde(default)]
    allowed_origins: Vec<String>,
}

#[derive(Debug, Default, Deserialize)]
#[serde(deny_unknown_fields)]
struct StoreFile {
    path: Option<String>,
}

#[derive(Debug, Default, Deserialize)]
#[serde(deny_unknown_fields)]
struct RetentionFile {
    sessions_days: Option<u32>,
    follow_transcripts: Option<bool>,
    events_days: Option<u32>,
    quota_days: Option<u32>,
    token_detail_days: Option<u32>,
    token_hourly_days: Option<u32>,
    token_daily_days: Option<u32>,
}

impl RetentionFile {
    fn resolve(self) -> Retention {
        let d = Retention::default();
        let days = |v: Option<u32>, default: u32| v.unwrap_or(default).max(1);
        let detail = days(self.token_detail_days, d.token_detail_days);
        // Each token tier outlives the one before it, or totals would drop.
        let hourly = days(self.token_hourly_days, d.token_hourly_days).max(detail);
        Retention {
            sessions_days: days(self.sessions_days, d.sessions_days),
            follow_transcripts: self.follow_transcripts.unwrap_or(d.follow_transcripts),
            events_days: days(self.events_days, d.events_days),
            quota_days: days(self.quota_days, d.quota_days),
            token_detail_days: detail,
            token_hourly_days: hourly,
            token_daily_days: days(self.token_daily_days, d.token_daily_days).max(hourly),
        }
    }
}

#[derive(Debug, Default, Deserialize)]
#[serde(deny_unknown_fields)]
struct SessionsFile {
    keep_ended_hours: Option<u32>,
}

#[derive(Debug, Default, Deserialize)]
#[serde(deny_unknown_fields)]
struct ClaudeFile {
    /// Minutes between `claude -p /usage` runs at most; 0 turns them off.
    usage_refresh_minutes: Option<u32>,
    binary: Option<String>,
}

impl ClaudeFile {
    fn resolve(self, home: &Path) -> Option<crate::claude::Refresh> {
        match self.usage_refresh_minutes.unwrap_or(10) {
            0 => None,
            minutes => Some(crate::claude::Refresh {
                every: Duration::from_secs(minutes as u64 * 60),
                binary: self.binary.filter(|b| !b.trim().is_empty()).map(|b| expand(&b, home)),
            }),
        }
    }
}

pub fn home() -> PathBuf {
    if let Some(dir) = std::env::var_os("AGENTSMON_HOME") {
        return PathBuf::from(dir);
    }
    user_home().join(".agentsmon")
}

fn user_home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("."))
}

fn expand(path: &str, home: &Path) -> PathBuf {
    match path.strip_prefix("~/") {
        Some(rest) => user_home().join(rest),
        None if Path::new(path).is_relative() => home.join(path),
        None => PathBuf::from(path),
    }
}

/// The socket integrations write to, as the hook forwarder finds it without
/// reading the config: `AGENTSMON_SOCKET`, else the default.
pub fn default_socket() -> PathBuf {
    std::env::var_os("AGENTSMON_SOCKET").map(PathBuf::from).unwrap_or_else(|| home().join("agentsmon.sock"))
}

impl Config {
    pub fn load(explicit: Option<&Path>) -> Result<Config, String> {
        let home = home();
        let path = explicit.map(Path::to_path_buf).unwrap_or_else(|| home.join("config.toml"));
        let file: File = match std::fs::read_to_string(&path) {
            Ok(text) => toml::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))?,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound && explicit.is_none() => File::default(),
            Err(e) => return Err(format!("{}: {e}", path.display())),
        };

        let http: Option<SocketAddr> = match file.http.listen.as_deref().map(str::trim) {
            Some("") | Some("off") => None,
            Some(addr) => Some(addr.parse().map_err(|e| format!("http.listen {addr:?}: {e}"))?),
            None => Some(DEFAULT_HTTP.parse().expect("default address")),
        };
        let token = file.http.token.filter(|t| !t.trim().is_empty());
        if let Some(addr) = http
            && !addr.ip().is_loopback()
            && token.is_none()
        {
            return Err(format!("http.listen {addr} is reachable from the network: set http.token too"));
        }

        Ok(Config {
            socket: file
                .socket
                .map(|s| expand(&s, &home))
                .unwrap_or_else(default_socket),
            http,
            token,
            allowed_origins: file.http.allowed_origins,
            db: file.store.path.map(|p| expand(&p, &home)).unwrap_or_else(|| home.join("agentsmon.db")),
            retention: file.retention.resolve(),
            keep_ended_hours: file.sessions.keep_ended_hours.unwrap_or(24),
            claude_refresh: file.claude.resolve(&home),
            home,
        })
    }
}

pub const EXAMPLE: &str = r#"# ~/.agentsmon/config.toml — every field is optional.

# Unix socket for integrations and the CLI (owner-only).
# socket = "~/.agentsmon/agentsmon.sock"

[http]
# Listener for the dashboard, display devices and OpenTelemetry. "off"
# disables it.
# listen = "127.0.0.1:7788"
# Required when listen is not a loopback address (for example "0.0.0.0:7788"
# for a display on the LAN). Clients send `Authorization: Bearer <token>` or
# `?token=`.
# token = ""
# Browser origins accepted in addition to localhost, 127.0.0.1 and [::1].
# allowed_origins = []

[store]
# path = "~/.agentsmon/agentsmon.db"

[retention]
# Ended sessions and their events are forgotten this many days after they end.
# sessions_days = 30
# Forget a session, running or ended, once Claude Code deletes its transcript
# (deleting it in the app, or the cleanupPeriodDays sweep). Sessions archived
# in Claude Desktop always leave the live view and stay in the history.
# follow_transcripts = true
# events_days = 30
# quota_days = 30
# Token counts are kept per request, then per hour, then per day. Totals stay
# exact at each step; a time window is exact to the request, the hour or the
# day, depending on how far back it reaches. The daily tier ends the history.
# token_detail_days = 90
# token_hourly_days = 400
# token_daily_days = 1825

[sessions]
# How long an ended session stays in memory and in the live view.
# keep_ended_hours = 24

[claude]
# Claude Code keeps the account's usage limits as it last fetched them, which
# it does for /usage. While Claude Code is at work and no reading is newer
# than this many minutes (the plugin reports one after every turn), the daemon
# runs `claude -p /usage`, which asks no model. 0 never runs it.
# usage_refresh_minutes = 10
# The claude to run; by default the one on PATH or where its installers put it.
# binary = "~/.local/bin/claude"
"#;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn example_parses() {
        let file: File = toml::from_str(EXAMPLE).unwrap();
        assert!(file.http.listen.is_none());
    }

    #[test]
    fn claude_usage_refresh_defaults_to_ten_minutes_and_turns_off() {
        let home = Path::new("/h");
        let on = ClaudeFile::default().resolve(home).unwrap();
        assert_eq!((on.every, on.binary), (Duration::from_secs(600), None));
        assert!(ClaudeFile { usage_refresh_minutes: Some(0), binary: None }.resolve(home).is_none());
        let custom = ClaudeFile { usage_refresh_minutes: Some(3), binary: Some("/opt/claude".into()) }.resolve(home).unwrap();
        assert_eq!((custom.every, custom.binary), (Duration::from_secs(180), Some(PathBuf::from("/opt/claude"))));
    }

    #[test]
    fn token_tiers_never_end_before_the_one_before_them() {
        let r = RetentionFile { token_detail_days: Some(500), token_hourly_days: Some(10), ..Default::default() }.resolve();
        assert_eq!((r.token_detail_days, r.token_hourly_days), (500, 500));
        assert!(r.token_daily_days >= 500);
        assert_eq!(RetentionFile::default().resolve(), Retention::default());
    }

    #[test]
    fn network_listener_needs_a_token() {
        let dir = std::env::temp_dir().join(format!("agentsmon-cfg-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("c.toml");
        std::fs::write(&path, "[http]\nlisten = \"0.0.0.0:7788\"\n").unwrap();
        assert!(Config::load(Some(&path)).unwrap_err().contains("token"));
        std::fs::write(&path, "[http]\nlisten = \"0.0.0.0:7788\"\ntoken = \"t\"\n").unwrap();
        assert!(Config::load(Some(&path)).is_ok());
        std::fs::remove_dir_all(&dir).ok();
    }
}
