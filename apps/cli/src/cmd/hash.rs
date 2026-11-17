use super::{expect_positionals, to_absolute};
use crate::args::{usage, valued, Opt, Parsed};
use crate::ctx::{CliError, Ctx};
use crate::help::Spec;
use crate::json::Json;
use crate::term;
use m7arc_core::{CancelToken, VerifyStatus};

const OPTS: &[Opt] = &[
    valued("algorithm", Some('a'), "NAME[,NAME]", "Checksum algorithms (see 'm7arc formats --hashers'). Default SHA256."),
    valued("check", Some('c'), "MANIFEST", "Verify the files listed in a checksum file (.sha256, .md5, ...) instead of hashing."),
];

pub const SPEC: Spec = Spec {
    name: "hash",
    aliases: &["h"],
    usage: "hash [options] <path>...\n       m7arc hash --check <manifest>",
    summary: "Compute checksums of files and folders, or verify a checksum file.",
    description: "Folders are hashed recursively. With one algorithm each line is 'DIGEST  PATH'; with\nseveral, 'ALGORITHM  DIGEST  PATH'. With --check the exit status is 0 only when every\nlisted file matches.",
    options: &[OPTS],
    examples: &[
        "m7arc hash big.iso",
        "m7arc hash -a sha256,crc32 --json docs",
        "m7arc hash -c release.sha256",
    ],
};

pub fn run(parsed: Parsed, token: CancelToken) -> Result<(), CliError> {
    let ctx = Ctx::new(&parsed, token, false)?;
    if let Some(manifest) = parsed.value("check") {
        if !parsed.positionals.is_empty() || parsed.value("algorithm").is_some() {
            return usage("--check takes no paths and no --algorithm").map_err(Into::into);
        }
        return check(&ctx, manifest);
    }
    expect_positionals(&parsed, 1, None, "m7arc hash <path>...")?;

    let available = ctx.engine.hashers()?;
    let requested = parsed.value("algorithm").unwrap_or("SHA256");
    let mut algorithms = Vec::new();
    for name in requested.split(',').filter(|n| !n.is_empty()) {
        let Some(found) = available.iter().find(|h| h.name.eq_ignore_ascii_case(name) || h.name.eq_ignore_ascii_case(&name.replace('-', ""))) else {
            return usage(format!("unknown algorithm '{name}' (see 'm7arc formats --hashers')")).map_err(Into::into);
        };
        if !algorithms.contains(&found.name.as_str()) {
            algorithms.push(found.name.as_str());
        }
    }
    if algorithms.is_empty() {
        return usage("--algorithm needs at least one name").map_err(Into::into);
    }

    let mut inputs = Vec::new();
    let mut roots = Vec::new();
    for raw in &parsed.positionals {
        if std::fs::symlink_metadata(raw).is_err() {
            return Err(CliError::failed(format!("{raw}: no such file or directory")));
        }
        let absolute = to_absolute(raw)?;
        roots.push((absolute.clone(), raw.trim_end_matches('/').to_string()));
        inputs.push(absolute);
    }
    // Report paths the way the caller wrote them.
    let shown = |path: &str| {
        for (absolute, raw) in &roots {
            if path == absolute {
                return raw.clone();
            }
            if let Some(rest) = path.strip_prefix(absolute.as_str()).filter(|r| r.starts_with('/')) {
                return format!("{raw}{rest}");
            }
        }
        path.to_string()
    };
    let mut callbacks = ctx.callbacks();
    let result = ctx.engine.hash_summary(&inputs, &algorithms, &mut callbacks);
    term::finish_progress();
    let summary = result?;
    if ctx.json {
        let files = summary
            .files
            .iter()
            .map(|f| Json::object().with("path", shown(&f.path)).with("algorithm", f.algorithm.as_str()).with("digest", f.digest.as_str()).with("size", f.size))
            .collect::<Vec<_>>();
        let totals = summary
            .totals
            .iter()
            .map(|t| {
                Json::object()
                    .with("algorithm", t.algorithm.as_str())
                    .with("data_sum", t.data_sum.as_str())
                    .with("data_and_names_sum", t.data_and_names_sum.as_str())
            })
            .collect::<Vec<_>>();
        ctx.emit_json(
            Json::object()
                .with("ok", true)
                .with("command", "hash")
                .with("algorithms", Json::strings(&algorithms))
                .with("files", files)
                .with("totals", totals)
                .with("file_count", summary.file_count)
                .with("directory_count", summary.directory_count)
                .with("total_bytes", summary.total_bytes),
        );
    } else if !ctx.quiet {
        for f in &summary.files {
            if algorithms.len() == 1 {
                println!("{}  {}", f.digest.to_lowercase(), shown(&f.path));
            } else {
                println!("{}  {}  {}", f.algorithm, f.digest.to_lowercase(), shown(&f.path));
            }
        }
    }
    Ok(())
}

fn check(ctx: &Ctx, manifest: &str) -> Result<(), CliError> {
    let manifest = to_absolute(manifest)?;
    let mut callbacks = ctx.callbacks();
    let result = ctx.engine.verify_manifest(&manifest, &mut callbacks);
    term::finish_progress();
    let report = result?;
    let word = |s: VerifyStatus| match s {
        VerifyStatus::Passed => "ok",
        VerifyStatus::Missing => "missing",
        VerifyStatus::Mismatch => "mismatch",
        VerifyStatus::Error => "error",
    };
    if ctx.json {
        let entries = report
            .entries
            .iter()
            .map(|e| Json::object().with("path", e.path.as_str()).with("status", word(e.status)).with("message", e.message.as_str()))
            .collect::<Vec<_>>();
        ctx.emit_json(
            Json::object()
                .with("ok", report.ok())
                .with("command", "hash")
                .with("manifest", manifest.as_str())
                .with("entries", entries)
                .with("errors", Json::strings(&report.errors)),
        );
    } else if !ctx.quiet || !report.ok() {
        for e in &report.entries {
            if e.status != VerifyStatus::Passed || !ctx.quiet {
                println!("{}: {}", e.path, word(e.status));
            }
        }
        if report.entries.is_empty() {
            for error in &report.errors {
                eprintln!("m7arc: {error}");
            }
        }
    }
    if report.ok() {
        Ok(())
    } else {
        let bad = report.entries.iter().filter(|e| e.status != VerifyStatus::Passed).count();
        if ctx.json {
            return Err(CliError::Reported);
        }
        Err(CliError::Failed(format!("{bad} of {} files failed the check", report.entries.len())))
    }
}
