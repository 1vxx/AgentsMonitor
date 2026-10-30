//! What the person has in front of them: which app, if they are at the
//! machine at all. Selected in an app is not seen when the app sits behind a
//! game. macOS only; elsewhere nobody is ever in front.

/// Claude Desktop and the Codex app, as macOS knows them.
pub const CLAUDE_APP: &str = "com.anthropic.claudefordesktop";
pub const CODEX_APP: &str = "com.openai.codex";

/// Untouched keyboard, mouse and trackpad this long count as away.
const AWAY_SECS: f64 = 60.0;

/// The bundle id of the app in front, while the person is at the machine.
pub fn app_in_use() -> Option<String> {
    #[cfg(target_os = "macos")]
    {
        #[link(name = "CoreGraphics", kind = "framework")]
        unsafe extern "C" {
            fn CGEventSourceSecondsSinceLastEventType(state: i32, event_type: u32) -> f64;
        }
        // Any input at all, in the login session (locked or asleep, nothing
        // comes, and the person is soon away)
        let idle = unsafe { CGEventSourceSecondsSinceLastEventType(0, u32::MAX) };
        if idle > AWAY_SECS {
            return None;
        }
        // LaunchServices' own answer, with no permission to ask for
        let run = |args: &[&str]| {
            let out = std::process::Command::new("/usr/bin/lsappinfo").args(args).output().ok()?;
            out.status.success().then(|| String::from_utf8_lossy(&out.stdout).into_owned())
        };
        let asn = run(&["front"])?;
        let info = run(&["info", "-only", "bundleID", asn.trim()])?;
        let rest = &info[info.find("bundleID=\"")? + 10..];
        Some(rest[..rest.find('"')?].to_string())
    }
    #[cfg(not(target_os = "macos"))]
    {
        None
    }
}
