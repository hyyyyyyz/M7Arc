//! Terminal interaction: tty detection, progress line, prompts, signals.

use m7arc_core::{CancelToken, Progress};
use std::io::{self, BufRead, IsTerminal, Write};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

mod ffi {
    use std::os::raw::c_int;
    extern "C" {
        pub fn signal(signum: c_int, handler: usize) -> usize;
        pub fn _exit(status: c_int) -> !;
    }
}

const SIGINT: i32 = 2;
const SIGPIPE: i32 = 13;
const SIGTERM: i32 = 15;
const SIG_DFL: usize = 0;

static INTERRUPTED: AtomicBool = AtomicBool::new(false);
/// Set while the terminal has echo disabled for a password prompt.
static ECHO_OFF: AtomicBool = AtomicBool::new(false);
/// Set while a prompt waits for the user; an interrupt then ends the process.
static PROMPTING: AtomicBool = AtomicBool::new(false);

extern "C" fn on_signal(_: i32) {
    // The first signal asks the watcher thread to cancel cleanly; a second one
    // while that is still going on ends the process at once.
    if INTERRUPTED.swap(true, Ordering::SeqCst) {
        unsafe { ffi::_exit(130) }
    }
}

pub fn stdin_is_tty() -> bool {
    io::stdin().is_terminal()
}

pub fn stdout_is_tty() -> bool {
    io::stdout().is_terminal()
}

pub fn stderr_is_tty() -> bool {
    io::stderr().is_terminal()
}

/// Prompts are possible when both stdin and stderr are a terminal.
pub fn interactive() -> bool {
    stdin_is_tty() && stderr_is_tty()
}

/// Makes SIGINT and SIGTERM cancel `token`, and lets SIGPIPE end the process
/// quietly like other command-line tools.
pub fn install_signal_handling(token: CancelToken) {
    unsafe {
        ffi::signal(SIGINT, on_signal as *const () as usize);
        ffi::signal(SIGTERM, on_signal as *const () as usize);
        ffi::signal(SIGPIPE, SIG_DFL);
    }
    std::thread::spawn(move || loop {
        std::thread::sleep(Duration::from_millis(30));
        if !INTERRUPTED.load(Ordering::SeqCst) {
            continue;
        }
        if PROMPTING.load(Ordering::SeqCst) {
            restore_echo();
            let _ = writeln!(io::stderr());
            std::process::exit(130);
        }
        clear_line();
        let _ = writeln!(io::stderr(), "interrupted, cleaning up...");
        token.cancel();
        return;
    });
}

pub fn interrupted() -> bool {
    INTERRUPTED.load(Ordering::SeqCst)
}

fn stty(arg: &str) {
    let _ = Command::new("stty").arg(arg).stdin(Stdio::inherit()).stdout(Stdio::null()).stderr(Stdio::null()).status();
}

fn restore_echo() {
    if ECHO_OFF.swap(false, Ordering::SeqCst) {
        stty("echo");
    }
}

/// Reads one line from stdin after showing `prompt` on stderr. `None` at end of input.
pub fn prompt_line(prompt: &str, hidden: bool) -> Option<String> {
    clear_line();
    let mut err = io::stderr();
    let _ = write!(err, "{prompt}");
    let _ = err.flush();
    PROMPTING.store(true, Ordering::SeqCst);
    if hidden {
        ECHO_OFF.store(true, Ordering::SeqCst);
        stty("-echo");
    }
    let mut line = String::new();
    let read = io::stdin().lock().read_line(&mut line);
    if hidden {
        restore_echo();
        let _ = writeln!(err);
    }
    PROMPTING.store(false, Ordering::SeqCst);
    match read {
        Ok(0) | Err(_) => None,
        Ok(_) => Some(line.trim_end_matches(['\n', '\r']).to_string()),
    }
}

fn clear_line() {
    if stderr_is_tty() {
        let _ = write!(io::stderr(), "\r\x1b[2K");
    }
}

fn terminal_width() -> usize {
    std::env::var("COLUMNS").ok().and_then(|c| c.parse().ok()).filter(|w| *w >= 20).unwrap_or(80)
}

/// Keeps the tail of `text` within `width` characters.
fn fit(text: &str, width: usize) -> String {
    let count = text.chars().count();
    if count <= width {
        return text.to_string();
    }
    let tail: String = text.chars().skip(count - (width - 1)).collect();
    format!("…{tail}")
}

/// Progress callback that redraws one status line on stderr, at most ten times a second.
pub fn progress_renderer() -> impl FnMut(&Progress) -> bool + Send {
    let mut last = Instant::now() - Duration::from_secs(1);
    move |p: &Progress| {
        if last.elapsed() < Duration::from_millis(100) {
            return true;
        }
        last = Instant::now();
        let percent = if p.total_known && p.total > 0 {
            format!("{:>3}%", (p.completed.min(p.total) as f64 / p.total as f64 * 100.0) as u64)
        } else {
            "    ".to_string()
        };
        let head = format!("{:<14}{percent} ", p.phase);
        let width = terminal_width().saturating_sub(head.chars().count() + 1);
        let _ = write!(io::stderr(), "\r\x1b[2K{head}{}", fit(&p.path, width));
        let _ = io::stderr().flush();
        true
    }
}

/// Erases the progress line.
pub fn finish_progress() {
    clear_line();
    let _ = io::stderr().flush();
}

#[cfg(test)]
mod tests {
    use super::fit;

    #[test]
    fn keeps_the_tail() {
        assert_eq!(fit("abc", 5), "abc");
        assert_eq!(fit("abcdefgh", 5), "…efgh");
        assert_eq!(fit("中文中文中文", 4), "…文中文");
    }
}
