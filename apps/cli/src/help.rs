//! Help text rendering.

use crate::args::Opt;
use crate::ctx::COMMON_OPTS;

pub struct Spec {
    pub name: &'static str,
    pub aliases: &'static [&'static str],
    pub usage: &'static str,
    pub summary: &'static str,
    pub description: &'static str,
    /// Option tables shown under "Options"; the common table is appended.
    pub options: &'static [&'static [Opt]],
    pub examples: &'static [&'static str],
}

pub const EXIT_CODES: &str = "\
Exit codes:
    0   success
    1   the operation failed (unreadable files, damaged archive, member errors, mismatching checksums)
    2   usage error (unknown option, missing argument, invalid value)
    3   the archive needs a password, or the password is wrong
  130   cancelled (Ctrl-C or SIGTERM); an archive being written is not left behind";

pub const ENVIRONMENT: &str = "\
Environment:
  M7ARC_PASSWORD   Password used to open archives when no password option is given.
  COLUMNS          Width used for the progress line (default 80).";

fn option_line(opt: &Opt) -> (String, &'static str) {
    let mut left = String::from("  ");
    match opt.short {
        Some(c) => left.push_str(&format!("-{c}, ")),
        None => left.push_str("    "),
    }
    left.push_str(&format!("--{}", opt.long));
    if let Some(meta) = opt.value {
        left.push_str(&format!(" {meta}"));
    }
    (left, opt.help)
}

pub fn render_options(tables: &[&[Opt]]) -> String {
    let mut out = String::new();
    for table in tables {
        for opt in *table {
            let (left, help) = option_line(opt);
            if left.chars().count() >= 28 {
                out.push_str(&format!("{left}\n{:30}{help}\n", ""));
            } else {
                out.push_str(&format!("{left:<30}{help}\n"));
            }
        }
    }
    out
}

pub fn command_help(spec: &Spec) -> String {
    let mut out = format!("Usage: m7arc {}\n\n{}\n", spec.usage, spec.summary);
    if !spec.description.is_empty() {
        out.push('\n');
        out.push_str(spec.description);
        out.push('\n');
    }
    out.push_str("\nOptions:\n");
    let mut tables: Vec<&[Opt]> = spec.options.to_vec();
    tables.push(COMMON_OPTS);
    out.push_str(&render_options(&tables));
    if !spec.examples.is_empty() {
        out.push_str("\nExamples:\n");
        for example in spec.examples {
            out.push_str(&format!("  {example}\n"));
        }
    }
    out.push_str("\nSee 'm7arc --help' for exit codes.\n");
    out
}

pub fn overview(specs: &[&Spec]) -> String {
    let mut out = String::from(
        "m7arc - create, inspect and extract archives (7z, zip, tar, gzip, xz, bzip2, wim, ... )\n\n\
Usage: m7arc <command> [options] [arguments]\n\n\
Commands:\n",
    );
    for spec in specs {
        let names = std::iter::once(spec.name).chain(spec.aliases.iter().copied()).collect::<Vec<_>>().join(", ");
        out.push_str(&format!("  {names:<18}{}\n", spec.summary));
    }
    out.push_str(
        "  help <command>    Show help for a command.\n\n\
Global options:\n  -h, --help        Show help (also: m7arc <command> --help).\n      --version     Print the version.\n\n",
    );
    out.push_str("Output:\n  Results go to stdout; progress and diagnostics go to stderr. The progress line is\n  drawn only when stdout and stderr are terminals. Every command accepts --json for one\n  machine-readable JSON document on stdout (errors included).\n\n");
    out.push_str("Passwords:\n  Use --password-file FILE, --password-env VAR, M7ARC_PASSWORD, or --ask-password.\n  --password TEXT is accepted but the text is visible to other users in the process list.\n  When an archive needs a password and none was given, a terminal session is prompted\n  without echo; otherwise the command exits with status 3.\n\n");
    out.push_str("Overwriting:\n  extract asks before replacing a file when run on a terminal. Without a terminal it\n  skips existing files unless --overwrite says otherwise, so it never waits for input.\n\n");
    out.push_str(EXIT_CODES);
    out.push_str("\n\n");
    out.push_str(ENVIRONMENT);
    out.push('\n');
    out
}
