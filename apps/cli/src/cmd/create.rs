use super::{expect_positionals, to_absolute};
use crate::args::{flag, usage, valued, Opt, Parsed};
use crate::ctx::{CliError, Ctx};
use crate::help::Spec;
use crate::{term, util};
use m7arc_core::{ArchivePathMode, CancelToken, CreateOptions, UpdateMode};
use std::path::Path;

const CREATE_OPTS: &[Opt] = &[
    valued("format", Some('f'), "FORMAT", "7z, zip, tar, tar.gz, tar.bz2, tar.xz, gzip, bzip2, xz, wim. Default: from the archive name, else 7z."),
    valued("level", Some('l'), "0-9", "Compression level; 0 stores, 9 is smallest. Default 5."),
    valued("method", Some('m'), "NAME", "Compression method (for example lzma2, ppmd, deflate, zstd)."),
    valued("dictionary", None, "SIZE", "Dictionary size such as 64m (suffixes k, m, g)."),
    valued("threads", Some('t'), "N", "Worker threads. Default: automatic."),
    flag("no-solid", None, "Compress every file separately (7z)."),
    valued("volume", Some('v'), "SIZE[,SIZE]", "Split into volumes of SIZE bytes (suffixes k, m, g)."),
    valued("include", Some('i'), "PATTERN", "Only store files matching PATTERN. Repeatable."),
    valued("exclude", Some('x'), "PATTERN", "Skip files matching PATTERN. Repeatable."),
    valued("prefix", None, "DIR", "Store everything below the folder DIR inside the archive."),
    valued("paths", None, "MODE", "How input paths are stored: relative (default), full or absolute."),
    flag("encrypt-names", None, "Also encrypt file names (7z). Needs a password."),
    valued("encryption-method", None, "NAME", "For example aes256 (default for 7z) or zipcrypto."),
    flag("no-mac-metadata", None, "Do not store macOS metadata (resource forks, extended attributes)."),
    valued("set", None, "NAME=VALUE", "Pass a raw format/codec property. Repeatable."),
];

const CREATE_ONLY: &[Opt] = &[flag("force", None, "Replace the archive if it already exists.")];

const ADD_ONLY: &[Opt] = &[
    flag("update", None, "Replace members only when the input is newer; add new ones."),
    flag("freshen", None, "Replace members only when the input is newer; add nothing."),
    flag("sync", None, "Make the archive match the inputs (members missing on disk are removed)."),
];

/// Password sources for choosing a password; M7ARC_PASSWORD is not read implicitly.
const NEW_PASSWORD_OPTS: &[Opt] = &[
    valued("password-file", None, "FILE", "Read the password from the first line of FILE ('-' reads stdin)."),
    valued("password-env", None, "VAR", "Read the password from environment variable VAR."),
    valued("password", None, "TEXT", "Use TEXT as the password. Visible in the process list; prefer the other sources."),
    flag("ask-password", Some('P'), "Ask for the password twice on the terminal (input is not echoed)."),
    flag("allow-high-memory", None, "Continue when a codec needs more memory than the engine allows."),
];

pub const CREATE_SPEC: Spec = Spec {
    name: "create",
    aliases: &["c"],
    usage: "create [options] <archive> <input>...",
    summary: "Create a new archive from files and folders.",
    description: "Fails if <archive> exists (use --force to replace it, or 'add' to extend it). Folders are\nstored with their name, files are stored by name. Patterns for --include/--exclude use\n'*' (any characters including '/') and '?'. A password comes from --password-file,\n--password-env or --ask-password; M7ARC_PASSWORD is not used when creating.",
    options: &[CREATE_ONLY, CREATE_OPTS, NEW_PASSWORD_OPTS],
    examples: &[
        "m7arc create photos.7z ~/Pictures -l 9",
        "m7arc create site.zip public --exclude '*.map' --exclude '.DS_Store'",
        "m7arc create secret.7z notes --password-env ARCHIVE_PW --encrypt-names",
        "m7arc create big.7z data --volume 700m",
    ],
};

pub const ADD_SPEC: Spec = Spec {
    name: "add",
    aliases: &["a"],
    usage: "add [options] <archive> <input>...",
    summary: "Add files and folders to an archive, creating it if needed.",
    description: "Existing members with the same path are replaced. An existing encrypted archive uses the\nsame password sources as 'create'.",
    options: &[ADD_ONLY, CREATE_OPTS, NEW_PASSWORD_OPTS],
    examples: &["m7arc add backup.7z notes.txt", "m7arc add backup.zip ~/Documents --update"],
};


pub fn run_create(parsed: Parsed, token: CancelToken) -> Result<(), CliError> {
    run(parsed, token, false)
}

pub fn run_add(parsed: Parsed, token: CancelToken) -> Result<(), CliError> {
    run(parsed, token, true)
}

fn run(parsed: Parsed, token: CancelToken, adding: bool) -> Result<(), CliError> {
    let command = if adding { "add" } else { "create" };
    expect_positionals(&parsed, 2, None, &format!("m7arc {command} <archive> <input>..."))?;
    let archive_arg = &parsed.positionals[0];
    let options = build_options(&parsed, archive_arg, adding)?;
    let ctx = Ctx::new(&parsed, token, true)?;
    let options = CreateOptions { password: ctx.password.clone(), ..options };
    if options.encrypt_headers && options.password.is_none() {
        return Err(CliError::Usage("--encrypt-names needs a password".into()));
    }
    let archive = to_absolute(archive_arg)?;

    let mut inputs = Vec::new();
    for raw in &parsed.positionals[1..] {
        if !Path::new(raw).exists() && std::fs::symlink_metadata(raw).is_err() {
            return Err(CliError::failed(format!("{raw}: no such file or directory")));
        }
        inputs.push(to_absolute(raw)?);
    }

    let first_file = if options.volume_sizes.is_empty() { archive.clone() } else { format!("{archive}.001") };
    if !adding && Path::new(&first_file).exists() {
        if !parsed.flag("force") {
            return Err(CliError::failed(format!("{archive} already exists (use --force to replace it, or 'add' to extend it)")));
        }
        remove_archive_files(&archive, &first_file)?;
    }

    let mut callbacks = ctx.callbacks();
    if adding {
        if let Some(password) = &options.password {
            callbacks = callbacks.password_for(archive.clone(), password.clone());
        }
    }
    let result = ctx.engine.create(&inputs, &archive, &options, &mut callbacks);
    term::finish_progress();
    let report = result?;
    let summary = format!("{} {archive} ({} items)", if adding { "Updated" } else { "Created" }, report.completed_items);
    ctx.finish(command, &archive, &summary, &report);
    Ok(())
}

fn remove_archive_files(archive: &str, first_file: &str) -> Result<(), CliError> {
    let remove = |path: &str| {
        std::fs::remove_file(path).map_err(|e| CliError::failed(format!("cannot remove {path}: {e}")))
    };
    if Path::new(archive).exists() {
        remove(archive)?;
    }
    if first_file != archive {
        for n in 1.. {
            let volume = format!("{archive}.{n:03}");
            if !Path::new(&volume).exists() {
                break;
            }
            remove(&volume)?;
        }
    }
    Ok(())
}

fn build_options(parsed: &Parsed, archive: &str, adding: bool) -> Result<CreateOptions, CliError> {
    let format = match parsed.value("format") {
        Some(f) => f.to_ascii_lowercase(),
        None => infer_format(archive)
            .ok_or_else(|| CliError::Usage(format!("cannot tell the format from the name '{archive}'; use --format (see 'm7arc formats --writable')")))?
            .to_string(),
    };
    let mut options = CreateOptions::with_format(format);
    if let Some(level) = parsed.number::<u32>("level")? {
        if level > 9 {
            return usage("--level must be between 0 and 9").map_err(Into::into);
        }
        options.level = level;
    }
    options.method = parsed.value("method").map(str::to_string);
    if let Some(text) = parsed.value("dictionary") {
        options.dictionary_bytes = util::parse_size(text).ok_or_else(|| CliError::Usage(format!("invalid size for --dictionary: '{text}'")))?;
    }
    if let Some(threads) = parsed.number::<u32>("threads")? {
        options.threads = threads;
    }
    options.solid = !parsed.flag("no-solid");
    if let Some(text) = parsed.value("volume") {
        for part in text.split(',') {
            match util::parse_size(part) {
                Some(size) if size > 0 => options.volume_sizes.push(size),
                _ => return usage(format!("invalid size for --volume: '{part}'")).map_err(Into::into),
            }
        }
    }
    options.include_patterns = parsed.values("include").into_iter().map(str::to_string).collect();
    options.exclude_patterns = parsed.values("exclude").into_iter().map(str::to_string).collect();
    options.archive_prefix = parsed.value("prefix").map(str::to_string);
    options.path_mode = match parsed.value("paths") {
        None | Some("relative") => ArchivePathMode::Relative,
        Some("full") => ArchivePathMode::Full,
        Some("absolute") => ArchivePathMode::Absolute,
        Some(other) => return usage(format!("invalid --paths value '{other}' (relative, full, absolute)")).map_err(Into::into),
    };
    options.encrypt_headers = parsed.flag("encrypt-names");
    options.encryption_method = parsed.value("encryption-method").map(str::to_string);
    options.preserve_mac_metadata = !parsed.flag("no-mac-metadata");
    for item in parsed.values("set") {
        match item.split_once('=') {
            Some((name, value)) if !name.is_empty() => options.properties.push((name.to_string(), value.to_string())),
            _ => return usage(format!("--set expects NAME=VALUE, got '{item}'")).map_err(Into::into),
        }
    }
    if adding {
        let modes = [("update", UpdateMode::Update), ("freshen", UpdateMode::Freshen), ("sync", UpdateMode::Synchronize)];
        let chosen: Vec<_> = modes.iter().filter(|(name, _)| parsed.flag(name)).collect();
        if chosen.len() > 1 {
            return usage("use only one of --update, --freshen, --sync").map_err(Into::into);
        }
        if let Some((_, mode)) = chosen.first() {
            options.update_mode = *mode;
        }
    }
    Ok(options)
}

/// The format implied by an archive file name.
pub fn infer_format(name: &str) -> Option<&'static str> {
    let lower = name.to_ascii_lowercase();
    let lower = lower.strip_suffix(".001").unwrap_or(&lower);
    const TABLE: [(&str, &str); 14] = [
        (".tar.gz", "tar.gz"),
        (".tgz", "tar.gz"),
        (".tar.bz2", "tar.bz2"),
        (".tbz2", "tar.bz2"),
        (".tar.xz", "tar.xz"),
        (".txz", "tar.xz"),
        (".7z", "7z"),
        (".zip", "zip"),
        (".tar", "tar"),
        (".gz", "gzip"),
        (".bz2", "bzip2"),
        (".xz", "xz"),
        (".wim", "wim"),
        (".swm", "wim"),
    ];
    TABLE.iter().find(|(suffix, _)| lower.ends_with(suffix)).map(|(_, format)| *format)
}

#[cfg(test)]
mod tests {
    use super::infer_format;

    #[test]
    fn formats_from_names() {
        assert_eq!(infer_format("a/b.tar.gz"), Some("tar.gz"));
        assert_eq!(infer_format("B.TGZ"), Some("tar.gz"));
        assert_eq!(infer_format("x.7z.001"), Some("7z"));
        assert_eq!(infer_format("x.zip"), Some("zip"));
        assert_eq!(infer_format("x.gz"), Some("gzip"));
        assert_eq!(infer_format("noext"), None);
    }
}
