//! End-to-end tests: run the `m7arc` binary against the real engine.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output, Stdio};
use std::time::Duration;

struct Sandbox {
    root: PathBuf,
}

impl Sandbox {
    fn new(name: &str) -> Sandbox {
        let root = std::env::temp_dir().join(format!("m7arc-cli-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(root.join("input/sub")).unwrap();
        fs::write(root.join("input/hello.txt"), "hello archive\n").unwrap();
        fs::write(root.join("input/sub/notes.md"), "# notes\n").unwrap();
        fs::write(root.join("input/sub/中文.txt"), "unicode path\n").unwrap();
        fs::write(root.join("input/data.bin"), vec![7u8; 50_000]).unwrap();
        Sandbox { root }
    }

    fn path(&self, name: &str) -> PathBuf {
        self.root.join(name)
    }

    fn text(&self, name: &str) -> String {
        fs::read_to_string(self.path(name)).unwrap()
    }

    /// Runs `m7arc` in the sandbox with stdin closed (so it can never prompt).
    fn run(&self, args: &[&str]) -> Output {
        self.command(args).output().unwrap()
    }

    fn command(&self, args: &[&str]) -> Command {
        let mut command = Command::new(env!("CARGO_BIN_EXE_m7arc"));
        command.args(args).current_dir(&self.root).stdin(Stdio::null()).env_remove("M7ARC_PASSWORD");
        command
    }
}

impl Drop for Sandbox {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.root);
    }
}

fn code(output: &Output) -> i32 {
    output.status.code().unwrap_or(-1)
}

fn stdout(output: &Output) -> String {
    String::from_utf8_lossy(&output.stdout).into_owned()
}

fn stderr(output: &Output) -> String {
    String::from_utf8_lossy(&output.stderr).into_owned()
}

fn names(output: &Output) -> Vec<String> {
    stdout(output).lines().map(str::to_string).collect()
}

fn ok(output: Output) -> Output {
    assert_eq!(code(&output), 0, "stdout: {}\nstderr: {}", stdout(&output), stderr(&output));
    output
}

fn create_7z(sandbox: &Sandbox) {
    ok(sandbox.run(&["create", "a.7z", "input", "--no-mac-metadata"]));
}

#[test]
fn create_list_extract_test_roundtrip() {
    let s = Sandbox::new("roundtrip");
    let created = ok(s.run(&["create", "a.7z", "input", "-l", "3", "--no-mac-metadata"]));
    assert!(stdout(&created).starts_with("Created "));
    assert!(stderr(&created).is_empty(), "no diagnostics expected: {}", stderr(&created));

    let listing = ok(s.run(&["list", "--names", "a.7z"]));
    let listed = names(&listing);
    for expected in ["input", "input/hello.txt", "input/sub/notes.md", "input/sub/中文.txt", "input/data.bin"] {
        assert!(listed.iter().any(|n| n == expected), "{expected} missing from {listed:?}");
    }

    let table = stdout(&ok(s.run(&["list", "a.7z"])));
    assert!(table.contains("Format:  7z") && table.contains("input/hello.txt"));

    ok(s.run(&["test", "a.7z"]));
    ok(s.run(&["extract", "a.7z", "-o", "out"]));
    assert_eq!(s.text("out/input/hello.txt"), "hello archive\n");
    assert_eq!(s.text("out/input/sub/中文.txt"), "unicode path\n");
    assert_eq!(fs::read(s.path("out/input/data.bin")).unwrap(), vec![7u8; 50_000]);
}

#[test]
fn list_json_is_a_complete_document() {
    let s = Sandbox::new("listjson");
    create_7z(&s);
    let out = ok(s.run(&["list", "--json", "a.7z"]));
    let json = stdout(&out);
    assert_eq!(json.lines().count(), 1);
    assert!(json.starts_with('{') && json.trim_end().ends_with('}'));
    for needle in [
        r#""ok":true"#,
        r#""command":"list""#,
        r#""format":"7z""#,
        r#""path":"input/hello.txt""#,
        r#""size":14"#,
        r#""is_directory":true"#,
        r#""encrypted":false"#,
    ] {
        assert!(json.contains(needle), "{needle} missing from {json}");
    }
}

#[test]
fn zip_and_tar_gz_formats_from_names() {
    let s = Sandbox::new("formats");
    ok(s.run(&["create", "a.zip", "input", "--no-mac-metadata"]));
    ok(s.run(&["create", "a.tar.gz", "input", "--no-mac-metadata"]));
    assert!(stdout(&ok(s.run(&["list", "a.zip"]))).contains("Format:  zip"));
    assert!(names(&ok(s.run(&["list", "--names", "a.tar.gz"]))).contains(&"input/hello.txt".to_string()));
    ok(s.run(&["extract", "a.zip", "-o", "z"]));
    assert_eq!(s.text("z/input/hello.txt"), "hello archive\n");

    let unknown = s.run(&["create", "a.unknown", "input"]);
    assert_eq!(code(&unknown), 2);
    assert!(!s.path("a.unknown").exists());
}

#[test]
fn extract_selection_and_overwrite_policy() {
    let s = Sandbox::new("select");
    create_7z(&s);

    ok(s.run(&["extract", "a.7z", "*.md", "input/hello.txt", "-o", "out"]));
    assert!(s.path("out/input/sub/notes.md").exists());
    assert!(s.path("out/input/hello.txt").exists());
    assert!(!s.path("out/input/data.bin").exists());

    ok(s.run(&["extract", "a.7z", "input/sub", "-o", "folder"]));
    assert!(s.path("folder/input/sub/中文.txt").exists());
    assert!(!s.path("folder/input/hello.txt").exists());

    ok(s.run(&["extract", "a.7z", "*.txt", "--flat", "-o", "flat"]));
    assert!(s.path("flat/hello.txt").exists());

    let missing = s.run(&["extract", "a.7z", "nothing-here", "-o", "out2"]);
    assert_eq!(code(&missing), 1);
    assert!(stderr(&missing).contains("nothing-here"));

    // Without a terminal an existing file is kept; --overwrite replace changes it.
    fs::write(s.path("out/input/hello.txt"), "local edit\n").unwrap();
    ok(s.run(&["extract", "a.7z", "input/hello.txt", "-o", "out"]));
    assert_eq!(s.text("out/input/hello.txt"), "local edit\n");
    ok(s.run(&["extract", "a.7z", "input/hello.txt", "-o", "out", "--overwrite", "replace"]));
    assert_eq!(s.text("out/input/hello.txt"), "hello archive\n");
}

#[test]
fn encrypted_archive_password_handling() {
    let s = Sandbox::new("password");
    fs::write(s.path("pw.txt"), "s3cret\nsecond line ignored\n").unwrap();
    ok(s.run(&["create", "e.7z", "input", "--password-file", "pw.txt", "--encrypt-names", "--no-mac-metadata"]));

    // No password, no terminal: exit 3 and a hint, never a hang.
    let none = s.run(&["list", "e.7z"]);
    assert_eq!(code(&none), 3);
    assert!(stderr(&none).contains("--password-file"));

    let wrong = s.run(&["test", "e.7z", "--password", "nope"]);
    assert_eq!(code(&wrong), 3);
    let wrong_json = s.run(&["test", "e.7z", "--password", "nope", "--json"]);
    assert_eq!(code(&wrong_json), 3);
    assert!(stdout(&wrong_json).contains(r#""ok":false"#) && stdout(&wrong_json).contains(r#""exit_code":3"#));

    ok(s.run(&["test", "e.7z", "--password-file", "pw.txt"]));
    let by_env = s.command(&["list", "--names", "e.7z"]).env("M7ARC_PASSWORD", "s3cret").output().unwrap();
    assert!(names(&ok(by_env)).contains(&"input/hello.txt".to_string()));
    let named_env = s.command(&["test", "e.7z", "--password-env", "MY_PW"]).env("MY_PW", "s3cret").output().unwrap();
    ok(named_env);
    let unset_env = s.run(&["test", "e.7z", "--password-env", "M7ARC_NOT_SET"]);
    assert_eq!(code(&unset_env), 1);

    // Content-only encryption: names list without a password, extraction needs it.
    ok(s.run(&["create", "c.7z", "input", "--password-file", "pw.txt", "--no-mac-metadata"]));
    ok(s.run(&["list", "c.7z"]));
    assert_eq!(code(&s.run(&["extract", "c.7z", "-o", "o1"])), 3);
    assert_eq!(code(&s.run(&["extract", "c.7z", "-o", "o2", "--password", "bad"])), 3);
    ok(s.run(&["extract", "c.7z", "-o", "o3", "--password-file", "pw.txt"]));
    assert_eq!(s.text("o3/input/hello.txt"), "hello archive\n");

    let two_sources = s.run(&["test", "e.7z", "--password", "a", "--password-file", "pw.txt"]);
    assert_eq!(code(&two_sources), 2);
    let no_password = s.run(&["create", "n.7z", "input", "--encrypt-names"]);
    assert_eq!(code(&no_password), 2);
    assert!(!s.path("n.7z").exists());
}

#[test]
fn usage_errors_exit_2() {
    let s = Sandbox::new("usage");
    for args in [
        &["frobnicate"][..],
        &[],
        &["list"],
        &["list", "--bogus", "a.7z"],
        &["extract", "a.7z", "-o"],
        &["create", "a.7z"],
        &["create", "a.7z", "input", "--level", "11"],
        &["create", "a.7z", "input", "--volume", "huge"],
        &["rename", "a.7z", "only-two"],
        &["hash"],
        &["hash", "-a", "nonsense", "input/hello.txt"],
        &["extract", "a.7z", "--overwrite", "maybe"],
        &["formats", "extra"],
    ] {
        let out = s.run(args);
        assert_eq!(code(&out), 2, "{args:?}: {}", stderr(&out));
        assert!(stdout(&out).is_empty(), "{args:?} wrote to stdout");
    }
    let json = s.run(&["list", "--json", "--bogus", "a.7z"]);
    assert_eq!(code(&json), 2);
    assert!(stdout(&json).contains(r#""kind":"usage""#));
}

#[test]
fn operation_failures_exit_1() {
    let s = Sandbox::new("failures");
    assert_eq!(code(&s.run(&["list", "missing.7z"])), 1);
    assert_eq!(code(&s.run(&["create", "x.7z", "no-such-input"])), 1);
    assert!(!s.path("x.7z").exists());
    fs::write(s.path("junk.7z"), b"this is not an archive at all").unwrap();
    assert_eq!(code(&s.run(&["test", "junk.7z"])), 1);

    create_7z(&s);
    // Damage the archive body: the test must fail.
    let mut bytes = fs::read(s.path("a.7z")).unwrap();
    let middle = bytes.len() / 2;
    for b in &mut bytes[middle..middle + 8] {
        *b ^= 0xff;
    }
    fs::write(s.path("damaged.7z"), bytes).unwrap();
    assert_eq!(code(&s.run(&["test", "damaged.7z"])), 1);

    let exists = s.run(&["create", "a.7z", "input"]);
    assert_eq!(code(&exists), 1);
    assert!(stderr(&exists).contains("--force"));
}

#[test]
fn add_force_and_volumes() {
    let s = Sandbox::new("add");
    create_7z(&s);
    fs::write(s.path("extra.txt"), "extra\n").unwrap();
    ok(s.run(&["add", "a.7z", "extra.txt", "--no-mac-metadata"]));
    let listed = names(&ok(s.run(&["list", "--names", "a.7z"])));
    assert!(listed.contains(&"extra.txt".to_string()) && listed.contains(&"input/hello.txt".to_string()));

    ok(s.run(&["create", "a.7z", "extra.txt", "--force", "--no-mac-metadata"]));
    assert_eq!(names(&ok(s.run(&["list", "--names", "a.7z"]))), ["extra.txt"]);

    ok(s.run(&["create", "vol.7z", "input", "--volume", "10k", "-l", "0", "--no-mac-metadata"]));
    assert!(s.path("vol.7z.001").exists() && s.path("vol.7z.002").exists());
    assert!(!s.path("vol.7z").exists());
    ok(s.run(&["test", "vol.7z.001"]));
}

#[test]
fn include_and_exclude_patterns() {
    let s = Sandbox::new("patterns");
    ok(s.run(&["create", "a.zip", "input", "--exclude", "*.bin", "--exclude", "*.md", "--no-mac-metadata"]));
    let listed = names(&ok(s.run(&["list", "--names", "a.zip"])));
    assert!(listed.contains(&"input/hello.txt".to_string()));
    assert!(!listed.iter().any(|n| n.ends_with(".bin") || n.ends_with(".md")), "{listed:?}");
}

#[test]
fn delete_and_rename_members() {
    let s = Sandbox::new("edit");
    create_7z(&s);

    ok(s.run(&["rename", "a.7z", "input/hello.txt", "input/greeting.txt"]));
    ok(s.run(&["rename", "a.7z", "input/sub", "input/docs"]));
    let after_rename = names(&ok(s.run(&["list", "--names", "a.7z"])));
    assert!(after_rename.contains(&"input/greeting.txt".to_string()));
    assert!(after_rename.contains(&"input/docs/notes.md".to_string()));
    assert!(!after_rename.contains(&"input/hello.txt".to_string()));

    ok(s.run(&["delete", "a.7z", "input/docs", "*.bin"]));
    assert_eq!(names(&ok(s.run(&["list", "--names", "a.7z"]))), ["input", "input/greeting.txt"]);
    ok(s.run(&["test", "a.7z"]));

    assert_eq!(code(&s.run(&["delete", "a.7z", "absent"])), 1);
    assert_eq!(code(&s.run(&["rename", "a.7z", "absent", "x"])), 1);
    ok(s.run(&["extract", "a.7z", "-o", "out"]));
    assert_eq!(s.text("out/input/greeting.txt"), "hello archive\n");
}

#[test]
fn hash_and_manifest_check() {
    let s = Sandbox::new("hash");
    let out = ok(s.run(&["hash", "input/hello.txt"]));
    let line = stdout(&out);
    let (digest, path) = line.trim_end().split_once("  ").expect("DIGEST  PATH");
    assert_eq!(digest.len(), 64);
    assert_eq!(path, "input/hello.txt");

    let multi = stdout(&ok(s.run(&["hash", "-a", "crc32,md5", "input/hello.txt"])));
    assert!(multi.lines().any(|l| l.starts_with("CRC32  ")) && multi.lines().any(|l| l.starts_with("MD5  ")));

    let json = stdout(&ok(s.run(&["hash", "--json", "input"])));
    assert!(json.contains(r#""algorithms":["SHA256"]"#) && json.contains(r#""path":"input/sub/notes.md""#));

    fs::write(s.path("sums.sha256"), stdout(&ok(s.run(&["hash", "input/hello.txt", "input/sub"])))).unwrap();
    ok(s.run(&["hash", "--check", "sums.sha256"]));
    fs::write(s.path("input/hello.txt"), "tampered\n").unwrap();
    let bad = s.run(&["hash", "--check", "sums.sha256"]);
    assert_eq!(code(&bad), 1);
    assert!(stdout(&bad).contains("input/hello.txt: mismatch"));
    let bad_json = s.run(&["hash", "-c", "sums.sha256", "--json"]);
    assert_eq!(code(&bad_json), 1);
    assert_eq!(stdout(&bad_json).lines().count(), 1);
    assert!(stdout(&bad_json).contains(r#""ok":false"#));
}

#[test]
fn formats_help_and_version() {
    let s = Sandbox::new("meta");
    let formats = stdout(&ok(s.run(&["formats"])));
    assert!(formats.contains("7z") && formats.contains("zip"));
    let writable = stdout(&ok(s.run(&["formats", "--writable", "--json"])));
    assert!(writable.contains(r#""name":"7z""#) && writable.contains(r#""can_write":true"#) && !writable.contains(r#""can_write":false"#));
    assert!(stdout(&ok(s.run(&["formats", "--hashers"]))).contains("SHA256"));

    assert!(stdout(&ok(s.run(&["--version"]))).starts_with("m7arc "));
    let overview = stdout(&ok(s.run(&["--help"])));
    assert!(overview.contains("Exit codes:") && overview.contains("M7ARC_PASSWORD"));
    for command in ["list", "extract", "create", "add", "test", "delete", "rename", "hash", "formats"] {
        let help = stdout(&ok(s.run(&[command, "--help"])));
        assert!(help.starts_with(&format!("Usage: m7arc {command}")), "{command}: {help}");
        assert_eq!(stdout(&ok(s.run(&["help", command]))), help);
    }
}

#[test]
fn quiet_and_json_results() {
    let s = Sandbox::new("results");
    assert!(stdout(&ok(s.run(&["create", "a.7z", "input", "-q", "--no-mac-metadata"]))).is_empty());
    let json = stdout(&ok(s.run(&["test", "a.7z", "--json"])));
    assert!(json.contains(r#""ok":true"#) && json.contains(r#""command":"test""#) && json.contains(r#""errors":[]"#));
    let extracted = stdout(&ok(s.run(&["extract", "a.7z", "-o", "out", "--json"])));
    assert!(extracted.contains(r#""command":"extract""#) && extracted.contains("/out"));
}

fn write_noise(path: &Path, bytes: usize) {
    let mut state = 0x9e37_79b9_7f4a_7c15u64;
    let mut data = Vec::with_capacity(bytes);
    while data.len() < bytes {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        data.extend_from_slice(&state.to_le_bytes());
    }
    fs::write(path, data).unwrap();
}

#[test]
fn interrupt_cancels_and_leaves_no_archive() {
    let s = Sandbox::new("cancel");
    write_noise(&s.path("noise.bin"), 120_000_000);
    let child = s
        .command(&["create", "big.7z", "noise.bin", "-l", "9", "--no-mac-metadata"])
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    std::thread::sleep(Duration::from_millis(1200));
    let status = Command::new("kill").args(["-INT", &child.id().to_string()]).status().unwrap();
    assert!(status.success());
    let output = child.wait_with_output().unwrap();
    assert_eq!(code(&output), 130, "stderr: {}", stderr(&output));
    assert!(!s.path("big.7z").exists(), "a cancelled create must not leave an archive");
    assert!(stderr(&output).contains("cancel"));
}
