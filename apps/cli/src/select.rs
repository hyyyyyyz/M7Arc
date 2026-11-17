//! Choosing archive members by path or wildcard.

use m7arc_core::Entry;

/// True if `pattern` has wildcard characters.
fn has_wildcards(pattern: &str) -> bool {
    pattern.contains(['*', '?'])
}

/// `*` matches any run of characters (including `/`), `?` exactly one.
pub fn glob(pattern: &[char], text: &[char]) -> bool {
    let (mut p, mut t) = (0, 0);
    let mut star: Option<(usize, usize)> = None;
    while t < text.len() {
        if p < pattern.len() && (pattern[p] == '?' || pattern[p] == text[t]) && pattern[p] != '*' {
            p += 1;
            t += 1;
        } else if p < pattern.len() && pattern[p] == '*' {
            star = Some((p, t));
            p += 1;
        } else if let Some((sp, st)) = star {
            p = sp + 1;
            t = st + 1;
            star = Some((sp, st + 1));
        } else {
            return false;
        }
    }
    pattern[p..].iter().all(|&c| c == '*')
}

fn matches_path(pattern: &str, path: &str) -> bool {
    let pattern = pattern.trim_start_matches("./").trim_end_matches('/');
    if has_wildcards(pattern) {
        let p: Vec<char> = pattern.chars().collect();
        glob(&p, &path.chars().collect::<Vec<_>>())
    } else {
        path == pattern
    }
}

/// True when `pattern` selects the entry itself or one of its parent folders.
fn selects(pattern: &str, path: &str) -> bool {
    if matches_path(pattern, path) {
        return true;
    }
    let mut end = path.len();
    while let Some(slash) = path[..end].rfind('/') {
        if matches_path(pattern, &path[..slash]) {
            return true;
        }
        end = slash;
    }
    false
}

pub struct Selection {
    pub indices: Vec<u32>,
    /// Patterns that matched nothing.
    pub unmatched: Vec<String>,
}

/// Entries selected by any pattern. A pattern naming a folder (or matching
/// one) selects everything below it. No patterns selects nothing.
pub fn select(entries: &[Entry], patterns: &[String]) -> Selection {
    let mut indices = Vec::new();
    let mut hit = vec![false; patterns.len()];
    for entry in entries {
        let mut selected = false;
        for (i, pattern) in patterns.iter().enumerate() {
            if selects(pattern, &entry.path) {
                hit[i] = true;
                selected = true;
            }
        }
        if selected {
            indices.push(entry.index);
        }
    }
    let unmatched = patterns.iter().zip(&hit).filter(|(_, h)| !**h).map(|(p, _)| p.clone()).collect();
    Selection { indices, unmatched }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(index: u32, path: &str) -> Entry {
        Entry {
            index,
            path: path.into(),
            size: 0,
            packed_size: 0,
            is_directory: false,
            encrypted: false,
            alternate_stream: false,
            modified_unix_seconds: 0,
            method: String::new(),
            attributes: String::new(),
            properties: Vec::new(),
        }
    }

    fn g(p: &str, t: &str) -> bool {
        glob(&p.chars().collect::<Vec<_>>(), &t.chars().collect::<Vec<_>>())
    }

    #[test]
    fn globbing() {
        assert!(g("*.txt", "a/b/c.txt"));
        assert!(g("a?c", "abc"));
        assert!(!g("a?c", "ac"));
        assert!(g("*a*b*", "xxaxxbxx"));
        assert!(!g("*.txt", "c.txt.bak"));
        assert!(g("*", ""));
    }

    #[test]
    fn selection_by_name_folder_and_wildcard() {
        let entries = vec![entry(0, "d"), entry(1, "d/a.txt"), entry(2, "d/sub/b.bin"), entry(3, "top.txt")];
        let pick = |pats: &[&str]| select(&entries, &pats.iter().map(|s| s.to_string()).collect::<Vec<_>>());
        assert_eq!(pick(&["d"]).indices, [0, 1, 2]);
        assert_eq!(pick(&["d/"]).indices, [0, 1, 2]);
        assert_eq!(pick(&["*.txt"]).indices, [1, 3]);
        assert_eq!(pick(&["d/sub"]).indices, [2]);
        assert_eq!(pick(&["top.txt", "nope"]).unmatched, ["nope"]);
        assert!(pick(&[]).indices.is_empty());
    }
}
