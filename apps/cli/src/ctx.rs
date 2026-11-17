//! Per-invocation state shared by the commands: output mode, password,
//! cancellation and the callbacks handed to the engine.

use crate::args::{flag, usage, valued, Opt, Parsed, UsageError};
use crate::json::Json;
use crate::term;
use m7arc_core::{
    Callbacks, CancelToken, Engine, Error, MemoryAnswer, OperationReport, OverwriteAnswer, OverwriteMode,
};
use std::io::{self, Read, Write};

pub const EXIT_FAILED: i32 = 1;
pub const EXIT_USAGE: i32 = 2;
pub const EXIT_PASSWORD: i32 = 3;
pub const EXIT_CANCELLED: i32 = 130;

const PASSWORD_HINT: &str =
    "the archive is encrypted; give the password with --password-file, --password-env or --ask-password";

pub const PASSWORD_ENV: &str = "M7ARC_PASSWORD";

/// Options every command accepts.
pub const COMMON_OPTS: &[Opt] = &[
    flag("help", Some('h'), "Show this help."),
    flag("json", None, "Print the result as one JSON document on stdout."),
    flag("quiet", Some('q'), "Print nothing on success."),
    flag("no-progress", None, "Never draw the progress line."),
];

/// Options for commands that open archives.
pub const PASSWORD_OPTS: &[Opt] = &[
    valued("password-file", None, "FILE", "Read the password from the first line of FILE ('-' reads stdin)."),
    valued("password-env", None, "VAR", "Read the password from environment variable VAR."),
    valued(
        "password",
        None,
        "TEXT",
        "Use TEXT as the password. Visible in the process list; prefer the other sources.",
    ),
    flag("ask-password", Some('P'), "Ask for the password on the terminal (input is not echoed)."),
    flag("allow-high-memory", None, "Continue when a codec needs more memory than the engine allows."),
];

pub enum CliError {
    Usage(String),
    Failed(String),
    /// Failure whose details were already printed as the command's own JSON result.
    Reported,
    Engine(Error),
}

impl From<UsageError> for CliError {
    fn from(e: UsageError) -> CliError {
        CliError::Usage(e.0)
    }
}

impl From<Error> for CliError {
    fn from(e: Error) -> CliError {
        CliError::Engine(e)
    }
}

impl CliError {
    pub fn failed(message: impl Into<String>) -> CliError {
        CliError::Failed(message.into())
    }

    pub fn exit_code(&self) -> i32 {
        match self {
            CliError::Usage(_) => EXIT_USAGE,
            CliError::Failed(_) | CliError::Reported => EXIT_FAILED,
            CliError::Engine(e) if e.is_cancelled() || term::interrupted() => EXIT_CANCELLED,
            CliError::Engine(e) if e.is_password_error() => EXIT_PASSWORD,
            CliError::Engine(_) => EXIT_FAILED,
        }
    }

    fn kind(&self) -> &'static str {
        match self {
            CliError::Usage(_) => "usage",
            CliError::Failed(_) | CliError::Reported => "failed",
            CliError::Engine(e) if e.is_cancelled() => "cancelled",
            CliError::Engine(Error::PasswordRequired(_)) => "password_required",
            CliError::Engine(Error::OperationFailed(r)) if r.needs_password() => "password_required",
            CliError::Engine(e) if e.is_password_error() => "wrong_password",
            CliError::Engine(Error::InvalidArgument(_)) => "invalid_argument",
            CliError::Engine(Error::OperationFailed(_)) => "operation_failed",
            CliError::Engine(_) => "engine",
        }
    }

    fn message(&self) -> String {
        match self {
            CliError::Usage(m) | CliError::Failed(m) => m.clone(),
            CliError::Reported => String::new(),
            CliError::Engine(Error::OperationFailed(r)) if r.needs_password() => PASSWORD_HINT.to_string(),
            CliError::Engine(Error::PasswordRequired(m)) => format!("{m} (give the password with --password-file, --password-env or --ask-password)"),
            CliError::Engine(e) => e.to_string(),
        }
    }

    fn details(&self) -> &[String] {
        match self {
            CliError::Engine(Error::OperationFailed(report)) if !report.needs_password() => &report.errors,
            _ => &[],
        }
    }

    /// Reports the error: a diagnostic on stderr and, with `--json`, a document on stdout.
    pub fn report(&self, json: bool) {
        if let CliError::Reported = self {
            return;
        }
        let mut err = io::stderr().lock();
        let details = self.details();
        if details.is_empty() {
            let _ = writeln!(err, "m7arc: {}", self.message());
        } else {
            for detail in details.iter().take(20) {
                let _ = writeln!(err, "m7arc: {detail}");
            }
            if details.len() > 20 {
                let _ = writeln!(err, "m7arc: ... and {} more errors", details.len() - 20);
            }
        }
        if let CliError::Usage(_) = self {
            let _ = writeln!(err, "Try 'm7arc --help' for usage.");
        }
        if json {
            let doc = Json::object()
                .with("ok", false)
                .with("exit_code", self.exit_code() as i64)
                .with("error", Json::object().with("kind", self.kind()).with("message", self.message()))
                .with("errors", Json::strings(details));
            println!("{}", doc.render());
        }
    }
}

pub struct Ctx {
    pub engine: Engine,
    pub json: bool,
    pub quiet: bool,
    pub progress: bool,
    pub interactive: bool,
    pub token: CancelToken,
    /// Password from `--password*`, `M7ARC_PASSWORD` or `--ask-password`.
    pub password: Option<String>,
    allow_high_memory: bool,
}

impl Ctx {
    /// `for_create` is true for commands that choose a new password (`create`, `add`): they
    /// confirm an interactive entry and never pick the password up from `M7ARC_PASSWORD`.
    pub fn new(parsed: &Parsed, token: CancelToken, for_create: bool) -> Result<Ctx, CliError> {
        let json = parsed.flag("json");
        let quiet = parsed.flag("quiet");
        let interactive = term::interactive();
        let progress = !quiet && !parsed.flag("no-progress") && term::stdout_is_tty() && term::stderr_is_tty();
        let password = resolve_password(parsed, interactive, for_create)?;
        let engine = Engine::new()?;
        Ok(Ctx {
            engine,
            json,
            quiet,
            progress,
            interactive,
            token,
            password,
            allow_high_memory: parsed.flag("allow-high-memory"),
        })
    }

    /// Callbacks with cancellation, progress, password and memory handling.
    pub fn callbacks(&self) -> Callbacks<'_> {
        let mut callbacks = Callbacks::new().cancel_token(&self.token);
        if self.progress {
            callbacks = callbacks.on_progress(term::progress_renderer());
        }
        if self.interactive {
            callbacks = callbacks.on_password(|| term::prompt_line("Password: ", true).filter(|p| !p.is_empty()));
        }
        let (interactive, allow) = (self.interactive, self.allow_high_memory);
        callbacks.on_memory_request(move |request| {
            if request.report_only || allow {
                return MemoryAnswer::Continue;
            }
            let gib = |b: u64| b as f64 / (1u64 << 30) as f64;
            let detail = format!(
                "{} needs {:.1} GiB of memory; the limit is {:.1} GiB",
                request.path,
                gib(request.required_bytes),
                gib(request.allowed_bytes)
            );
            if interactive {
                let answer = term::prompt_line(&format!("{detail}. Continue? [y/N] "), false).unwrap_or_default();
                if answer.trim().eq_ignore_ascii_case("y") {
                    return MemoryAnswer::Continue;
                }
            } else {
                eprintln!("m7arc: {detail}; pass --allow-high-memory to continue");
            }
            MemoryAnswer::Cancel
        })
    }

    /// Callbacks that also answer overwrite questions interactively.
    pub fn callbacks_asking_overwrite(&self) -> Callbacks<'_> {
        self.callbacks().on_overwrite(|request| {
            let size = |s: Option<u64>| s.map_or(String::new(), |s| format!(" ({s} bytes)"));
            let prompt = format!(
                "{} already exists{}; replace with the archived copy{}?\n  (y)es, (a)ll, (n)o, (s)kip all, (r)ename, (q)uit: ",
                request.existing_path,
                size(request.existing_size),
                size(request.incoming_size)
            );
            match term::prompt_line(&prompt, false).as_deref().map(str::trim) {
                Some("y") => OverwriteAnswer::Replace,
                Some("a") => OverwriteAnswer::ReplaceAll,
                Some("n") => OverwriteAnswer::Skip,
                Some("s") => OverwriteAnswer::SkipAll,
                Some("r") => OverwriteAnswer::RenameIncoming,
                _ => OverwriteAnswer::Cancel,
            }
        })
    }

    /// A line of normal output; hidden by `--quiet` and `--json`.
    pub fn say(&self, line: impl AsRef<str>) {
        if !self.quiet && !self.json {
            println!("{}", line.as_ref());
        }
    }

    pub fn emit_json(&self, doc: Json) {
        println!("{}", doc.render());
    }

    /// Prints the outcome of a finished operation.
    pub fn finish(&self, command: &str, archive: &str, summary: &str, report: &OperationReport) {
        term::finish_progress();
        if self.json {
            self.emit_json(
                Json::object()
                    .with("ok", true)
                    .with("command", command)
                    .with("archive", archive)
                    .with("items", report.completed_items)
                    .with("outputs", Json::strings(&report.output_paths))
                    .with("errors", Json::strings(&report.errors)),
            );
        } else {
            self.say(summary);
        }
    }
}

/// How `--overwrite` maps to an engine mode; `Ask` is only available on a terminal.
pub fn overwrite_mode(parsed: &Parsed, interactive: bool) -> Result<OverwriteMode, UsageError> {
    Ok(match parsed.value("overwrite") {
        Some("ask") if interactive => OverwriteMode::Ask,
        Some("ask") => return usage("--overwrite ask needs a terminal on stdin and stderr"),
        Some("replace") => OverwriteMode::Replace,
        Some("skip") => OverwriteMode::Skip,
        Some("rename") => OverwriteMode::RenameIncoming,
        Some("rename-existing") => OverwriteMode::RenameExisting,
        Some(other) => {
            return usage(format!("invalid --overwrite value '{other}' (ask, replace, skip, rename, rename-existing)"))
        }
        None if interactive => OverwriteMode::Ask,
        None => OverwriteMode::Skip,
    })
}

fn resolve_password(parsed: &Parsed, interactive: bool, for_create: bool) -> Result<Option<String>, CliError> {
    let given = ["password", "password-file", "password-env", "ask-password"]
        .iter()
        .filter(|name| parsed.flag(name))
        .count();
    if given > 1 {
        return Err(CliError::Usage("use only one of --password, --password-file, --password-env, --ask-password".into()));
    }
    if let Some(text) = parsed.value("password") {
        return Ok(Some(text.to_string()));
    }
    if let Some(path) = parsed.value("password-file") {
        let text = if path == "-" {
            let mut text = String::new();
            io::stdin().read_to_string(&mut text).map(|_| text)
        } else {
            std::fs::read_to_string(path)
        }
        .map_err(|e| CliError::failed(format!("cannot read password file {path}: {e}")))?;
        let line = text.lines().next().unwrap_or("");
        return Ok(Some(line.to_string()));
    }
    if let Some(var) = parsed.value("password-env") {
        return match std::env::var(var) {
            Ok(value) => Ok(Some(value)),
            Err(_) => Err(CliError::failed(format!("environment variable {var} is not set"))),
        };
    }
    if parsed.flag("ask-password") {
        if !interactive {
            return Err(CliError::Usage("--ask-password needs a terminal on stdin and stderr".into()));
        }
        let first = term::prompt_line("Password: ", true).unwrap_or_default();
        if for_create {
            let again = term::prompt_line("Repeat password: ", true).unwrap_or_default();
            if first != again {
                return Err(CliError::failed("the passwords do not match"));
            }
        }
        return Ok(Some(first));
    }
    Ok(if for_create { None } else { std::env::var(PASSWORD_ENV).ok() })
}

pub const OVERWRITE_OPT: Opt = valued(
    "overwrite",
    None,
    "MODE",
    "When a file exists: ask, replace, skip, rename or rename-existing. Default: ask on a terminal, skip otherwise.",
);
