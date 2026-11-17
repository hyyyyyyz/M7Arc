//! Formatting and path helpers.

use std::path::{Component, Path, PathBuf};

/// `path` made absolute against the working directory, with `.` and `..` folded.
pub fn absolute(path: &Path) -> std::io::Result<PathBuf> {
    let joined = if path.is_absolute() { path.to_path_buf() } else { std::env::current_dir()?.join(path) };
    let mut out = PathBuf::new();
    for component in joined.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            other => out.push(other.as_os_str()),
        }
    }
    Ok(out)
}

/// Parses `100`, `64k`, `10m`, `2g` (binary multiples; case-insensitive).
pub fn parse_size(text: &str) -> Option<u64> {
    let text = text.trim();
    let split = text.find(|c: char| !c.is_ascii_digit()).unwrap_or(text.len());
    let (digits, suffix) = text.split_at(split);
    let value: u64 = digits.parse().ok()?;
    let multiplier: u64 = match suffix.to_ascii_lowercase().as_str() {
        "" | "b" => 1,
        "k" | "kb" => 1 << 10,
        "m" | "mb" => 1 << 20,
        "g" | "gb" => 1 << 30,
        "t" | "tb" => 1 << 40,
        _ => return None,
    };
    value.checked_mul(multiplier)
}

/// `1.5 MiB`-style size for humans.
pub fn human_size(bytes: u64) -> String {
    const UNITS: [&str; 5] = ["B", "KiB", "MiB", "GiB", "TiB"];
    let mut value = bytes as f64;
    let mut unit = 0;
    while value >= 1024.0 && unit < UNITS.len() - 1 {
        value /= 1024.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{bytes} B")
    } else {
        format!("{value:.1} {}", UNITS[unit])
    }
}

/// `YYYY-MM-DD HH:MM:SS` in UTC. Zero means "no timestamp".
pub fn format_time(unix_seconds: i64) -> String {
    if unix_seconds == 0 {
        return "-".repeat(19);
    }
    let (y, m, d, hh, mm, ss) = civil(unix_seconds);
    format!("{y:04}-{m:02}-{d:02} {hh:02}:{mm:02}:{ss:02}")
}

/// ISO 8601 form of [`format_time`], or `None` for no timestamp.
pub fn iso_time(unix_seconds: i64) -> Option<String> {
    (unix_seconds != 0).then(|| {
        let (y, m, d, hh, mm, ss) = civil(unix_seconds);
        format!("{y:04}-{m:02}-{d:02}T{hh:02}:{mm:02}:{ss:02}Z")
    })
}

fn civil(unix_seconds: i64) -> (i64, u32, u32, u32, u32, u32) {
    let days = unix_seconds.div_euclid(86400);
    let rem = unix_seconds.rem_euclid(86400);
    // Days since 1970-01-01 to a civil date (Howard Hinnant's algorithm).
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    let y = yoe + era * 400 + if m <= 2 { 1 } else { 0 };
    (y, m, d, (rem / 3600) as u32, (rem % 3600 / 60) as u32, (rem % 60) as u32)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sizes() {
        assert_eq!(parse_size("100"), Some(100));
        assert_eq!(parse_size("64K"), Some(65536));
        assert_eq!(parse_size("2g"), Some(2 << 30));
        assert_eq!(parse_size("x"), None);
        assert_eq!(parse_size("5q"), None);
        assert_eq!(human_size(1536), "1.5 KiB");
        assert_eq!(human_size(12), "12 B");
    }

    #[test]
    fn times() {
        assert_eq!(format_time(0), "-------------------");
        assert_eq!(format_time(1_700_000_000), "2023-11-14 22:13:20");
        assert_eq!(iso_time(951_782_400).as_deref(), Some("2000-02-29T00:00:00Z"));
    }

    #[test]
    fn paths() {
        let p = absolute(Path::new("/a/b/../c/./d/")).unwrap();
        assert_eq!(p, Path::new("/a/c/d"));
    }
}
