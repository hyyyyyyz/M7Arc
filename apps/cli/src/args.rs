//! Minimal command-line parser: long options (`--name`, `--name=value`,
//! `--name value`), short options (`-n`, `-nvalue`, combined booleans such as
//! `-qy`), and `--` to end option parsing.

use std::fmt;

/// One option accepted by a command.
pub struct Opt {
    pub long: &'static str,
    pub short: Option<char>,
    /// Placeholder name when the option takes a value.
    pub value: Option<&'static str>,
    pub help: &'static str,
}

pub const fn flag(long: &'static str, short: Option<char>, help: &'static str) -> Opt {
    Opt { long, short, value: None, help }
}

pub const fn valued(long: &'static str, short: Option<char>, value: &'static str, help: &'static str) -> Opt {
    Opt { long, short, value: Some(value), help }
}

#[derive(Debug)]
pub struct UsageError(pub String);

impl fmt::Display for UsageError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

pub fn usage<T>(message: impl Into<String>) -> Result<T, UsageError> {
    Err(UsageError(message.into()))
}

#[derive(Debug, Default)]
pub struct Parsed {
    seen: Vec<(&'static str, Option<String>)>,
    pub positionals: Vec<String>,
}

impl Parsed {
    pub fn flag(&self, long: &str) -> bool {
        self.seen.iter().any(|(name, _)| *name == long)
    }

    /// The last value given for `long`.
    pub fn value(&self, long: &str) -> Option<&str> {
        self.seen.iter().rev().find(|(name, _)| *name == long).and_then(|(_, v)| v.as_deref())
    }

    /// Every value given for `long`, in order.
    pub fn values(&self, long: &str) -> Vec<&str> {
        self.seen.iter().filter(|(name, _)| *name == long).filter_map(|(_, v)| v.as_deref()).collect()
    }

    /// Parses the numeric value of `long`, if present.
    pub fn number<T: std::str::FromStr>(&self, long: &str) -> Result<Option<T>, UsageError> {
        match self.value(long) {
            None => Ok(None),
            Some(text) => text
                .parse()
                .map(Some)
                .or_else(|_| usage(format!("invalid value for --{long}: '{text}'"))),
        }
    }
}

pub fn parse(args: &[String], tables: &[&[Opt]]) -> Result<Parsed, UsageError> {
    let find_long = |name: &str| tables.iter().flat_map(|t| t.iter()).find(|o| o.long == name);
    let find_short = |c: char| tables.iter().flat_map(|t| t.iter()).find(|o| o.short == Some(c));
    let mut parsed = Parsed::default();
    let mut iter = args.iter();
    let mut options_ended = false;
    while let Some(arg) = iter.next() {
        if options_ended || arg == "-" || !arg.starts_with('-') {
            parsed.positionals.push(arg.clone());
        } else if arg == "--" {
            options_ended = true;
        } else if let Some(body) = arg.strip_prefix("--") {
            let (name, inline) = match body.split_once('=') {
                Some((name, value)) => (name, Some(value.to_string())),
                None => (body, None),
            };
            let Some(opt) = find_long(name) else {
                return usage(format!("unknown option: --{name}"));
            };
            match (opt.value, inline) {
                (None, None) => parsed.seen.push((opt.long, None)),
                (None, Some(_)) => return usage(format!("option --{name} does not take a value")),
                (Some(_), Some(value)) => parsed.seen.push((opt.long, Some(value))),
                (Some(meta), None) => match iter.next() {
                    Some(value) => parsed.seen.push((opt.long, Some(value.clone()))),
                    None => return usage(format!("option --{name} needs a value ({meta})")),
                },
            }
        } else {
            let chars: Vec<char> = arg[1..].chars().collect();
            let mut i = 0;
            while i < chars.len() {
                let c = chars[i];
                let Some(opt) = find_short(c) else {
                    return usage(format!("unknown option: -{c}"));
                };
                if let Some(meta) = opt.value {
                    let rest: String = chars[i + 1..].iter().collect();
                    let value = if !rest.is_empty() {
                        rest
                    } else {
                        match iter.next() {
                            Some(value) => value.clone(),
                            None => return usage(format!("option -{c} needs a value ({meta})")),
                        }
                    };
                    parsed.seen.push((opt.long, Some(value)));
                    break;
                }
                parsed.seen.push((opt.long, None));
                i += 1;
            }
        }
    }
    Ok(parsed)
}

#[cfg(test)]
mod tests {
    use super::*;

    const OPTS: &[Opt] = &[
        flag("quiet", Some('q'), ""),
        flag("yes", Some('y'), ""),
        valued("output", Some('o'), "DIR", ""),
        valued("include", Some('i'), "PAT", ""),
    ];

    fn strings(items: &[&str]) -> Vec<String> {
        items.iter().map(|s| s.to_string()).collect()
    }

    #[test]
    fn long_short_and_positionals() {
        let p = parse(&strings(&["a.7z", "-qy", "-o", "out", "--include=*.txt", "--include", "x", "--", "-weird"]), &[OPTS]).unwrap();
        assert_eq!(p.positionals, ["a.7z", "-weird"]);
        assert!(p.flag("quiet") && p.flag("yes"));
        assert_eq!(p.value("output"), Some("out"));
        assert_eq!(p.values("include"), ["*.txt", "x"]);
    }

    #[test]
    fn attached_short_value_and_errors() {
        let p = parse(&strings(&["-oout"]), &[OPTS]).unwrap();
        assert_eq!(p.value("output"), Some("out"));
        assert!(parse(&strings(&["--nope"]), &[OPTS]).is_err());
        assert!(parse(&strings(&["-o"]), &[OPTS]).is_err());
        assert!(parse(&strings(&["--quiet=1"]), &[OPTS]).is_err());
    }

    #[test]
    fn numbers() {
        let opts: &[Opt] = &[valued("level", Some('l'), "N", "")];
        let p = parse(&strings(&["-l", "9"]), &[opts]).unwrap();
        assert_eq!(p.number::<u32>("level").unwrap(), Some(9));
        let p = parse(&strings(&["-l", "x"]), &[opts]).unwrap();
        assert!(p.number::<u32>("level").is_err());
    }
}
