use m7arc_core::*;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Mutex, MutexGuard};

// Engines of one process share a runtime; run the tests one at a time.
static SERIAL: Mutex<()> = Mutex::new(());

struct Fixture {
    root: PathBuf,
    _guard: MutexGuard<'static, ()>,
}

impl Fixture {
    fn new(name: &str) -> Fixture {
        let guard = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
        let root = std::env::temp_dir().join(format!("m7arc-core-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(root.join("input/sub")).unwrap();
        fs::write(root.join("input/hello.txt"), "hello archive\n").unwrap();
        fs::write(root.join("input/sub/中文.txt"), "unicode path\n").unwrap();
        fs::write(root.join("input/data.bin"), vec![7u8; 100_000]).unwrap();
        Fixture { root, _guard: guard }
    }

    fn input(&self) -> PathBuf {
        self.root.join("input")
    }

    fn path(&self, name: &str) -> PathBuf {
        self.root.join(name)
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        if !std::thread::panicking() {
            let _ = fs::remove_dir_all(&self.root);
        }
    }
}

fn engine() -> Engine {
    Engine::with_options(&EngineOptions { load_user_modules: false, ..EngineOptions::default() }).unwrap()
}

fn none() -> Callbacks<'static> {
    Callbacks::new()
}

fn replace_all() -> ExtractOptions {
    ExtractOptions { overwrite: OverwriteMode::Replace, ..ExtractOptions::default() }
}

fn read(path: impl AsRef<Path>) -> Vec<u8> {
    fs::read(path).unwrap()
}

#[test]
fn lists_registered_formats_and_hashers() {
    let _f = Fixture::new("formats");
    let engine = engine();
    assert!(!Engine::version().is_empty());
    let formats = engine.formats().unwrap();
    let seven = formats.iter().find(|f| f.name == "7z").expect("7z registered");
    assert!(seven.can_write && seven.can_read && seven.extensions.iter().any(|e| e == "7z"));
    assert!(engine.hashers().unwrap().iter().any(|h| h.name == "SHA256" && h.digest_size == 32));
}

#[test]
fn create_list_test_extract_roundtrip_for_each_container() {
    let f = Fixture::new("roundtrip");
    let engine = engine();
    for format in ["7z", "zip", "tar", "tar.gz", "tar.xz"] {
        let archive = f.path(&format!("out.{format}"));
        let mut options = CreateOptions::with_format(format);
        options.level = 1;
        let report = engine.create(&[f.input()], &archive, &options, &mut none()).unwrap_or_else(|e| panic!("{format}: {e}"));
        assert!(report.errors.is_empty(), "{format}");

        let listing = engine.list(&archive, &OpenOptions::default(), &mut none()).unwrap();
        let hello = listing.find("input/hello.txt").unwrap_or_else(|| panic!("{format}: member missing"));
        assert_eq!(hello.size, 14, "{format}");
        assert!(!hello.is_directory && !hello.encrypted);
        assert!(listing.find("input/sub/中文.txt").is_some(), "{format}: unicode name");
        assert!(listing.find("input/sub").is_some_and(|e| e.is_directory), "{format}");

        engine.test(&archive, &[], &OpenOptions::default(), &mut none()).unwrap_or_else(|e| panic!("{format}: {e}"));

        let destination = f.path(&format!("extracted-{format}"));
        let report = engine.extract(&archive, &destination, &[], &replace_all(), &mut none()).unwrap();
        assert!(report.completed_items >= 3, "{format}");
        assert_eq!(read(destination.join("input/hello.txt")), b"hello archive\n", "{format}");
        assert_eq!(read(destination.join("input/sub/中文.txt")), b"unicode path\n", "{format}");
        assert_eq!(read(destination.join("input/data.bin")), vec![7u8; 100_000], "{format}");
    }
}

#[test]
fn selective_extraction_and_flat_paths() {
    let f = Fixture::new("select");
    let engine = engine();
    let archive = f.path("a.zip");
    engine.create(&[f.input()], &archive, &CreateOptions::with_format("zip"), &mut none()).unwrap();
    let index = engine.list(&archive, &OpenOptions::default(), &mut none()).unwrap().find("input/sub/中文.txt").unwrap().index;
    let destination = f.path("flat");
    let options = ExtractOptions { path_mode: ExtractPathMode::Flat, overwrite: OverwriteMode::Replace, ..ExtractOptions::default() };
    engine.extract(&archive, &destination, &[index], &options, &mut none()).unwrap();
    assert_eq!(read(destination.join("中文.txt")), b"unicode path\n");
    assert!(!destination.join("hello.txt").exists());
}

#[test]
fn edits_archive_members() {
    let f = Fixture::new("edit");
    let engine = engine();
    let archive = f.path("edit.7z");
    engine.create(&[f.input()], &archive, &CreateOptions::default(), &mut none()).unwrap();
    let open = OpenOptions::default();
    let find = |path: &str| engine.list(&archive, &open, &mut none()).unwrap().find(path).map(|e| e.index);

    engine.rename(&archive, find("input/hello.txt").unwrap(), "input/renamed.txt", &open, &mut none()).unwrap();
    assert!(find("input/hello.txt").is_none() && find("input/renamed.txt").is_some());

    fs::write(f.path("new.txt"), "replacement\n").unwrap();
    engine.replace(&archive, find("input/renamed.txt").unwrap(), f.path("new.txt"), &open, &mut none()).unwrap();
    engine.remove(&archive, &[find("input/data.bin").unwrap()], &open, &mut none()).unwrap();
    assert!(find("input/data.bin").is_none());

    let destination = f.path("edited");
    engine.extract(&archive, &destination, &[], &replace_all(), &mut none()).unwrap();
    assert_eq!(read(destination.join("input/renamed.txt")), b"replacement\n");
}

#[test]
fn encrypted_archive_reports_password_errors() {
    let f = Fixture::new("password");
    let engine = engine();
    let archive = f.path("secret.7z");
    let options = CreateOptions { password: Some("correct horse".into()), encrypt_headers: true, ..CreateOptions::default() };
    engine.create(&[f.input()], &archive, &options, &mut none()).unwrap();

    let error = engine.list(&archive, &OpenOptions::default(), &mut none()).unwrap_err();
    assert!(matches!(error, Error::PasswordRequired(_)), "{error:?}");
    assert!(error.is_password_error());

    let error = engine.list(&archive, &OpenOptions::with_password("wrong"), &mut none()).unwrap_err();
    assert!(matches!(error, Error::PasswordOrCorruption(_)), "{error:?}");

    let listing = engine.list(&archive, &OpenOptions::with_password("correct horse"), &mut none()).unwrap();
    assert!(listing.find("input/hello.txt").is_some());

    let mut asked = 0;
    let mut callbacks = Callbacks::new().on_password(|| {
        asked += 1;
        Some("correct horse".to_string())
    });
    engine.test(&archive, &[], &OpenOptions::default(), &mut callbacks).unwrap();
    drop(callbacks);
    assert!(asked > 0);

    let mut callbacks = Callbacks::new().password_for(archive.to_str().unwrap(), "correct horse");
    engine.list(&archive, &OpenOptions::default(), &mut callbacks).unwrap();

    let error = engine.extract(&archive, f.path("denied"), &[], &replace_all(), &mut Callbacks::new().on_password(|| None)).unwrap_err();
    assert!(error.is_cancelled(), "{error:?}");
}

#[test]
fn wrong_password_on_data_only_encryption() {
    let f = Fixture::new("datapassword");
    let engine = engine();
    let archive = f.path("secret.zip");
    let options = CreateOptions { password: Some("right".into()), ..CreateOptions::with_format("zip") };
    engine.create(&[f.input()], &archive, &options, &mut none()).unwrap();
    let listing = engine.list(&archive, &OpenOptions::default(), &mut none()).unwrap();
    assert!(listing.find("input/hello.txt").unwrap().encrypted);
    let extract = ExtractOptions { password: Some("wrong".into()), ..replace_all() };
    let error = engine.extract(&archive, f.path("out"), &[], &extract, &mut none()).unwrap_err();
    assert!(error.is_password_error(), "{error:?}");
    match error {
        Error::OperationFailed(report) => assert!(!report.errors.is_empty() && report.password_may_be_incorrect),
        other => assert!(matches!(other, Error::PasswordOrCorruption(_))),
    }
}

#[test]
fn progress_and_cancellation() {
    let f = Fixture::new("cancel");
    let engine = engine();
    let mut stages = Vec::new();
    let mut callbacks = Callbacks::new().on_progress(|p| {
        if stages.last() != Some(&p.stage) {
            stages.push(p.stage);
        }
        true
    });
    engine.create(&[f.input()], f.path("p.7z"), &CreateOptions::default(), &mut callbacks).unwrap();
    drop(callbacks);
    assert!(stages.contains(&Stage::Compressing), "{stages:?}");

    let target = f.path("cancelled.7z");
    let mut callbacks = Callbacks::new().on_progress(|p| p.stage != Stage::Compressing);
    let error = engine.create(&[f.input()], &target, &CreateOptions::default(), &mut callbacks).unwrap_err();
    assert!(error.is_cancelled(), "{error:?}");
    assert!(!target.exists());

    let token = CancelToken::new();
    token.cancel();
    let error = engine.create(&[f.input()], &target, &CreateOptions::default(), &mut Callbacks::new().cancel_token(&token)).unwrap_err();
    assert!(error.is_cancelled());

    let error = engine.create(&[f.input()], &target, &CreateOptions::default(), &mut Callbacks::new().on_cancel_check(|| true)).unwrap_err();
    assert!(error.is_cancelled());
}

#[test]
fn cancelling_inner_extraction_of_compressed_tar_is_reported_as_cancelled() {
    let f = Fixture::new("targz-cancel");
    let engine = engine();
    let archive = f.path("a.tar.gz");
    engine.create(&[f.input()], &archive, &CreateOptions::with_format("tar.gz"), &mut none()).unwrap();
    let mut callbacks = Callbacks::new().on_progress(|p| p.stage != Stage::Extracting);
    let error = engine.list(&archive, &OpenOptions::default(), &mut callbacks).unwrap_err();
    assert!(error.is_cancelled(), "{error:?}");
}

#[test]
fn panic_in_callback_cancels_and_propagates() {
    let f = Fixture::new("panic");
    let engine = engine();
    let target = f.path("panic.7z");
    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let mut callbacks = Callbacks::new().on_progress(|_| panic!("callback failure"));
        let _ = engine.create(&[f.input()], &target, &CreateOptions::default(), &mut callbacks);
    }));
    let payload = outcome.expect_err("the panic is resumed on the calling thread");
    assert_eq!(payload.downcast_ref::<&str>(), Some(&"callback failure"));
    assert!(!target.exists());
    // The engine remains usable.
    engine.create(&[f.input()], &target, &CreateOptions::default(), &mut none()).unwrap();
}

#[test]
fn overwrite_callback_decides_conflicts() {
    let f = Fixture::new("overwrite");
    let engine = engine();
    let archive = f.path("o.zip");
    engine.create(&[f.input()], &archive, &CreateOptions::with_format("zip"), &mut none()).unwrap();
    let destination = f.path("dest");
    engine.extract(&archive, &destination, &[], &replace_all(), &mut none()).unwrap();
    fs::write(destination.join("input/hello.txt"), "local edit").unwrap();

    let asked = AtomicBool::new(false);
    let mut callbacks = Callbacks::new().on_overwrite(|request| {
        if request.existing_path.ends_with("hello.txt") {
            asked.store(true, Ordering::SeqCst);
        }
        OverwriteAnswer::Skip
    });
    engine.extract(&archive, &destination, &[], &ExtractOptions::default(), &mut callbacks).unwrap();
    drop(callbacks);
    assert!(asked.load(Ordering::SeqCst));
    assert_eq!(read(destination.join("input/hello.txt")), b"local edit");
}

#[test]
fn filters_and_comments() {
    let f = Fixture::new("filter");
    let engine = engine();
    let archive = f.path("filtered.zip");
    let options = CreateOptions { include_patterns: vec!["*.txt".into()], exclude_patterns: vec!["*中文*".into()], ..CreateOptions::with_format("zip") };
    engine.create(&[f.input()], &archive, &options, &mut none()).unwrap();
    let listing = engine.list(&archive, &OpenOptions::default(), &mut none()).unwrap();
    assert!(listing.find("input/hello.txt").is_some());
    assert!(listing.find("input/data.bin").is_none() && listing.find("input/sub/中文.txt").is_none());
    assert!(listing.can_set_comment);
    engine.set_comment(&archive, 0, "note", &OpenOptions::default(), &mut none()).unwrap();
}

#[test]
fn hashing_and_manifest_verification() {
    let f = Fixture::new("hash");
    let engine = engine();
    fs::write(f.path("abc.txt"), "abc").unwrap();
    let files = engine.hash(&[f.path("abc.txt")], &["SHA256"], &mut none()).unwrap();
    assert_eq!(files.len(), 1);
    assert_eq!(files[0].digest.to_lowercase(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    let summary = engine.hash_summary(&[f.input()], &["SHA256", "CRC32"], &mut none()).unwrap();
    assert_eq!(summary.file_count, 3);
    assert_eq!(summary.totals.len(), 2);

    fs::write(f.path("other.txt"), "different").unwrap();
    fs::write(
        f.path("sums.sha256"),
        format!("{}  abc.txt\n{}  other.txt\n{}  gone.txt\n", files[0].digest, files[0].digest, files[0].digest),
    )
    .unwrap();
    let report = engine.verify_manifest(f.path("sums.sha256"), &mut none()).unwrap();
    assert!(!report.ok());
    let status = |name: &str| report.entries.iter().find(|e| e.path == name).map(|e| e.status);
    assert_eq!(status("abc.txt"), Some(VerifyStatus::Passed));
    assert_eq!(status("other.txt"), Some(VerifyStatus::Mismatch));
    assert_eq!(status("gone.txt"), Some(VerifyStatus::Missing));
}

#[test]
fn errors_are_typed() {
    let f = Fixture::new("errors");
    let engine = engine();
    let error = engine.list(f.path("missing.7z"), &OpenOptions::default(), &mut none()).unwrap_err();
    assert!(matches!(error, Error::Engine(_)), "{error:?}");
    assert!(!error.to_string().is_empty());
    let error = engine.create(&[f.input()], f.path("x.7z"), &CreateOptions::with_format("7z\0"), &mut none()).unwrap_err();
    assert!(matches!(error, Error::InvalidArgument(_)));
    let error = engine.remove(f.path("x.7z"), &[], &OpenOptions::default(), &mut none()).unwrap_err();
    assert!(matches!(error, Error::InvalidArgument(_)), "{error:?}");
}

#[test]
fn engine_moves_between_threads() {
    let f = Fixture::new("threads");
    let engine = engine();
    let archive = f.path("t.7z");
    let input = f.input();
    std::thread::spawn(move || {
        engine.create(&[input], &archive, &CreateOptions::default(), &mut Callbacks::new()).unwrap();
    })
    .join()
    .unwrap();
}
