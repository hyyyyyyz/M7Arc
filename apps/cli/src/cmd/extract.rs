use super::{expect_positionals, open, existing_archive, to_absolute};
use crate::args::{flag, usage, valued, Opt, Parsed};
use crate::ctx::{overwrite_mode, CliError, Ctx, OVERWRITE_OPT, PASSWORD_OPTS};
use crate::help::Spec;
use crate::{select, term};
use m7arc_core::{CancelToken, ExtractOptions, ExtractPathMode, OverwriteMode};

const OPTS: &[Opt] = &[
    valued("output", Some('o'), "DIR", "Extract into DIR (created if needed). Default: the current directory."),
    OVERWRITE_OPT,
    flag("yes", Some('y'), "Same as --overwrite replace."),
    flag("flat", None, "Put every file directly into DIR, without folders."),
    flag("archive-folder", None, "Extract into a folder named after the archive inside DIR."),
    flag("dedupe-root", None, "Avoid a doubled top-level folder (DIR/name/name/...)."),
    flag("no-mac-metadata", None, "Do not restore macOS metadata stored in the archive."),
];

pub const SPEC: Spec = Spec {
    name: "extract",
    aliases: &["x"],
    usage: "extract [options] <archive> [pattern...]",
    summary: "Extract an archive, or only the members matching the patterns.",
    description: "A pattern is a member path (a folder selects everything below it) or a wildcard:\n'*' matches any characters including '/', '?' one character. Quote wildcards so the\nshell does not expand them.\n\nExisting files: on a terminal you are asked; otherwise they are skipped (safe, never\nwaits for input). Choose explicitly with --overwrite or -y.",
    options: &[OPTS, PASSWORD_OPTS],
    examples: &[
        "m7arc extract photos.7z -o ~/Pictures",
        "m7arc extract backup.zip 'docs/*.pdf' -o out --overwrite replace",
        "m7arc extract --password-file pw.txt secret.7z -o secret",
    ],
};

pub fn run(parsed: Parsed, token: CancelToken) -> Result<(), CliError> {
    expect_positionals(&parsed, 1, None, "m7arc extract <archive> [pattern...]")?;
    if parsed.flag("yes") && parsed.value("overwrite").is_some() {
        return usage("-y cannot be combined with --overwrite").map_err(Into::into);
    }
    let overwrite = if parsed.flag("yes") { OverwriteMode::Replace } else { overwrite_mode(&parsed, term::interactive())? };
    let ctx = Ctx::new(&parsed, token, false)?;
    let archive_path = existing_archive(&parsed, 0)?;
    let destination = to_absolute(parsed.value("output").unwrap_or("."))?;

    let patterns = &parsed.positionals[1..];
    let indices = if patterns.is_empty() {
        Vec::new()
    } else {
        let listing = open(&ctx, &archive_path)?;
        let selection = select::select(&listing.entries, patterns);
        if let Some(first) = selection.unmatched.first() {
            return Err(CliError::failed(format!("no member matches '{first}'")));
        }
        selection.indices
    };

    let options = ExtractOptions {
        password: ctx.password.clone(),
        path_mode: if parsed.flag("flat") { ExtractPathMode::Flat } else { ExtractPathMode::Full },
        overwrite,
        eliminate_duplicate_root: parsed.flag("dedupe-root"),
        create_archive_folder: parsed.flag("archive-folder"),
        restore_mac_metadata: !parsed.flag("no-mac-metadata"),
        current_directory: None,
    };
    let mut callbacks = if overwrite == OverwriteMode::Ask { ctx.callbacks_asking_overwrite() } else { ctx.callbacks() };
    let result = ctx.engine.extract(&archive_path, &destination, &indices, &options, &mut callbacks);
    term::finish_progress();
    let report = result?;
    let summary = format!("Extracted {} items to {destination}", report.completed_items);
    ctx.finish("extract", &archive_path, &summary, &report);
    Ok(())
}
