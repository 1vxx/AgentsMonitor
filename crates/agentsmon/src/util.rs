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

/// Days since 1970-01-01 of a proleptic Gregorian date.
fn days_from_civil(y: i64, m: i64, d: i64) -> i64 {
    let y = if m <= 2 { y - 1 } else { y };
    let era = y.div_euclid(400);
    let yoe = y - era * 400;
    let doy = (153 * (m + if m > 2 { -3 } else { 9 }) + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146_097 + doe - 719_468
}

/// Milliseconds since the epoch of an RFC 3339 UTC timestamp
/// (`2026-10-27T07:48:17.923Z`); `None` for any other form.
pub fn parse_utc_ms(s: &str) -> Option<i64> {
    let b = s.as_bytes();
    if b.len() < 20 || b[4] != b'-' || b[7] != b'-' || b[10] != b'T' || b[13] != b':' || b[16] != b':' {
        return None;
    }
    let num = |r: std::ops::Range<usize>| s.get(r)?.parse::<i64>().ok();
    let days = days_from_civil(num(0..4)?, num(5..7)?, num(8..10)?);
    let secs = days * 86_400 + num(11..13)? * 3_600 + num(14..16)? * 60 + num(17..19)?;
    let rest = s.get(19..)?.strip_suffix('Z')?;
    let millis = match rest.strip_prefix('.') {
        None if rest.is_empty() => 0,
        Some(frac) if !frac.is_empty() && frac.bytes().all(|c| c.is_ascii_digit()) => {
            format!("{frac:0<3}")[..3].parse::<i64>().ok()?
        }
        _ => return None,
    };
    Some(secs * 1_000 + millis)
}

/// RFC 3339 UTC form of seconds since the epoch.
pub fn format_utc(secs: i64) -> String {
    let (days, rem) = (secs.div_euclid(86_400), secs.rem_euclid(86_400));
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1_460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + if m <= 2 { 1 } else { 0 };
    format!("{y:04}-{m:02}-{d:02}T{:02}:{:02}:{:02}Z", rem / 3_600, rem % 3_600 / 60, rem % 60)
}

/// RFC 3339 UTC form of milliseconds since the epoch, to the millisecond.
pub fn format_utc_ms(ms: i64) -> String {
    let secs = format_utc(ms.div_euclid(1_000));
    format!("{}.{:03}Z", secs.trim_end_matches('Z'), ms.rem_euclid(1_000))
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
    fn reads_and_writes_utc_timestamps() {
        assert_eq!(parse_utc_ms("2026-10-27T07:48:17.923Z"), Some(1_793_087_297_923));
        assert_eq!(parse_utc_ms("1970-01-01T00:00:00Z"), Some(0));
        assert_eq!(parse_utc_ms("2024-02-29T12:00:00.5Z"), Some(1_709_208_000_500));
        assert_eq!(parse_utc_ms("2026-10-27 07:48:17Z"), None);
        assert_eq!(parse_utc_ms("2026-10-27T07:48:17+08:00"), None);
        assert_eq!(format_utc(1_793_104_669), "2026-10-27T12:37:49Z");
        assert_eq!(format_utc(0), "1970-01-01T00:00:00Z");
        assert_eq!(parse_utc_ms(&format_utc(1_709_208_000)), Some(1_709_208_000_000));
        assert_eq!(format_utc_ms(1_793_087_297_023), "2026-10-27T07:48:17.023Z");
        assert_eq!(parse_utc_ms(&format_utc_ms(1_793_087_297_923)), Some(1_793_087_297_923));
    }

    #[test]
    fn project_is_last_component() {
        assert_eq!(project_name("/Users/x/AgentsMonitor/"), "AgentsMonitor");
        assert_eq!(project_name("/"), "/");
    }
}
