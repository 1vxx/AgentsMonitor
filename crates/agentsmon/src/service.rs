//! Running the daemon at login: a launchd agent on macOS.

use std::path::PathBuf;
use std::process::Command;
use std::time::{Duration, Instant};

pub const LABEL: &str = "dev.agentsmon.daemon";

fn plist_path() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default();
    home.join("Library/LaunchAgents").join(format!("{LABEL}.plist"))
}

fn domain() -> String {
    format!("gui/{}", unsafe { libc::getuid() })
}

fn xml_escape(s: &str) -> String {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;")
}

fn plist(exe: &str, log: &str, home: &str) -> String {
    let (exe, log, home) = (xml_escape(exe), xml_escape(log), xml_escape(home));
    format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>{LABEL}</string>
  <key>ProgramArguments</key>
  <array><string>{exe}</string><string>serve</string></array>
  <key>EnvironmentVariables</key>
  <dict><key>AGENTSMON_HOME</key><string>{home}</string></dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>{log}</string>
  <key>StandardErrorPath</key><string>{log}</string>
</dict>
</plist>
"#
    )
}

fn launchctl(args: &[&str]) -> Result<String, String> {
    let out = Command::new("/bin/launchctl").args(args).output().map_err(|e| format!("launchctl: {e}"))?;
    let text = String::from_utf8_lossy(&out.stdout).to_string() + &String::from_utf8_lossy(&out.stderr);
    if out.status.success() { Ok(text) } else { Err(text.trim().to_string()) }
}

/// Waits for a service just booted out to leave launchd. `bootout` returns
/// while the daemon still shuts down (it flushes the store first), and a
/// `bootstrap` before it has left fails with "5: Input/output error".
fn gone(target: &str) -> bool {
    let until = Instant::now() + Duration::from_secs(15);
    while launchctl(&["print", target]).is_ok() {
        if Instant::now() > until {
            return false;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    true
}

pub fn install() -> Result<(), String> {
    if !cfg!(target_os = "macos") {
        return Err("only macOS (launchd) is supported for now; run `agentsmon serve` under your service manager".into());
    }
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    let home = crate::config::home();
    std::fs::create_dir_all(&home).map_err(|e| e.to_string())?;
    let log = home.join("agentsmon.log");
    let path = plist_path();
    std::fs::create_dir_all(path.parent().unwrap()).map_err(|e| e.to_string())?;
    // Replace a loaded copy so a new binary path takes effect.
    let target = format!("{}/{LABEL}", domain());
    let _ = launchctl(&["bootout", &target]);
    if !gone(&target) {
        return Err("the running daemon is still stopping; run this again in a moment".into());
    }
    std::fs::write(&path, plist(&exe.to_string_lossy(), &log.to_string_lossy(), &home.to_string_lossy()))
        .map_err(|e| e.to_string())?;
    launchctl(&["bootstrap", &domain(), &path.to_string_lossy()])?;
    println!("installed {} → {}", path.display(), exe.display());
    println!("logs: {}", log.display());
    Ok(())
}

pub fn uninstall() -> Result<(), String> {
    let _ = launchctl(&["bootout", &format!("{}/{LABEL}", domain())]);
    let path = plist_path();
    match std::fs::remove_file(&path) {
        Ok(()) => println!("removed {}", path.display()),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => println!("not installed"),
        Err(e) => return Err(e.to_string()),
    }
    Ok(())
}

pub fn status() -> Result<(), String> {
    match launchctl(&["print", &format!("{}/{LABEL}", domain())]) {
        Ok(text) => {
            for line in text.lines().filter(|l| {
                let l = l.trim_start();
                l.starts_with("state =") || l.starts_with("pid =") || l.starts_with("last exit code")
            }) {
                println!("{}", line.trim());
            }
            Ok(())
        }
        Err(_) => {
            println!("not loaded ({})", plist_path().display());
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn plist_escapes_paths() {
        let p = super::plist("/a&b/agentsmon", "/l", "/h");
        assert!(p.contains("/a&amp;b/agentsmon"));
        assert!(p.contains("<string>serve</string>"));
    }
}
