use super::{expect_positionals, open, open_options, existing_archive};
use crate::ctx::{CliError, Ctx, PASSWORD_OPTS};
use crate::help::Spec;
use crate::{select, term};
use m7arc_core::CancelToken;

pub const DELETE_SPEC: Spec = Spec {
    name: "delete",
    aliases: &["rm", "d"],
    usage: "delete [options] <archive> <pattern>...",
    summary: "Remove members from an archive.",
    description: "Patterns are member paths (a folder removes everything below it) or wildcards ('*' any\ncharacters including '/', '?' one character). The archive is rewritten; formats that\ncannot be modified (for example rar) are refused.",
    options: &[PASSWORD_OPTS],
    examples: &["m7arc delete backup.7z old-notes.txt", "m7arc delete backup.zip '*.tmp' cache"],
};

pub const RENAME_SPEC: Spec = Spec {
    name: "rename",
    aliases: &["mv"],
    usage: "rename [options] <archive> <old-path> <new-path>",
    summary: "Rename a member (or a folder and everything below it) inside an archive.",
    description: "Paths are exact member paths as shown by 'm7arc list --names'.",
    options: &[PASSWORD_OPTS],
    examples: &["m7arc rename backup.7z notes.txt docs/notes.txt"],
};


pub fn run_delete(parsed: crate::args::Parsed, token: CancelToken) -> Result<(), CliError> {
    expect_positionals(&parsed, 2, None, "m7arc delete <archive> <pattern>...")?;
    let ctx = Ctx::new(&parsed, token, false)?;
    let archive_path = existing_archive(&parsed, 0)?;
    let listing = open(&ctx, &archive_path)?;
    if !listing.can_update {
        return Err(CliError::failed(format!("{} archives cannot be modified", listing.format)));
    }
    let selection = select::select(&listing.entries, &parsed.positionals[1..]);
    if let Some(first) = selection.unmatched.first() {
        return Err(CliError::failed(format!("no member matches '{first}'")));
    }
    let mut callbacks = ctx.callbacks();
    let result = ctx.engine.remove(&archive_path, &selection.indices, &open_options(&ctx), &mut callbacks);
    term::finish_progress();
    let report = result?;
    let summary = format!("Removed {} members from {archive_path}", selection.indices.len());
    ctx.finish("delete", &archive_path, &summary, &report);
    Ok(())
}

pub fn run_rename(parsed: crate::args::Parsed, token: CancelToken) -> Result<(), CliError> {
    expect_positionals(&parsed, 3, Some(3), "m7arc rename <archive> <old-path> <new-path>")?;
    let ctx = Ctx::new(&parsed, token, false)?;
    let archive_path = existing_archive(&parsed, 0)?;
    let old = parsed.positionals[1].trim_start_matches("./").trim_end_matches('/');
    let new = parsed.positionals[2].trim_start_matches("./").trim_end_matches('/');
    let listing = open(&ctx, &archive_path)?;
    if !listing.can_update {
        return Err(CliError::failed(format!("{} archives cannot be modified", listing.format)));
    }
    let mut callbacks = ctx.callbacks();
    let options = open_options(&ctx);
    let folder_prefix = format!("{old}/");
    let result = match listing.find(old) {
        Some(entry) if !entry.is_directory => ctx.engine.rename(&archive_path, entry.index, new, &options, &mut callbacks),
        Some(_) => ctx.engine.rename_directory(&archive_path, old, new, &options, &mut callbacks),
        None if listing.entries.iter().any(|e| e.path.starts_with(&folder_prefix)) => {
            ctx.engine.rename_directory(&archive_path, old, new, &options, &mut callbacks)
        }
        None => return Err(CliError::failed(format!("no member named '{old}'"))),
    };
    term::finish_progress();
    let report = result?;
    let summary = format!("Renamed {old} to {new} in {archive_path}");
    ctx.finish("rename", &archive_path, &summary, &report);
    Ok(())
}
