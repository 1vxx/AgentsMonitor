use std::time::{SystemTime, UNIX_EPOCH};

pub fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

/// Cuts `s` to at most `max` chars on one line, marking the cut with `…`.
pub fn preview(s: &str, max: usize) -> Option<String> {
    let line = s.split_whitespace().collect::<Vec<_>>().join(" ");
    if line.is_empty() {
        return None;
    }
    if line.chars().count() <= max {
        return Some(line);
    }
    let mut out: String = line.chars().take(max.saturating_sub(1)).collect();
    out.push('…');
    Some(out)
}

/// The last path component of `cwd`, what a session is called on screen.
pub fn project_name(cwd: &str) -> String {
    cwd.trim_end_matches('/')
        .rsplit('/')
        .next()
        .filter(|s| !s.is_empty())
        .unwrap_or(cwd)
        .to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn preview_collapses_and_cuts() {
        assert_eq!(preview("  a \n b  ", 10).as_deref(), Some("a b"));
        assert_eq!(preview("abcdef", 4).as_deref(), Some("abc…"));
        assert_eq!(preview("   ", 4), None);
    }

    #[test]
    fn project_is_last_component() {
        assert_eq!(project_name("/Users/x/AgentsMonitor/"), "AgentsMonitor");
        assert_eq!(project_name("/"), "/");
    }
}
