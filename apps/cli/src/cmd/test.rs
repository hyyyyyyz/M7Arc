use super::{expect_positionals, open, existing_archive, open_options};
use crate::ctx::{CliError, Ctx, PASSWORD_OPTS};
use crate::help::Spec;
use crate::{select, term};
use m7arc_core::CancelToken;

pub const SPEC: Spec = Spec {
    name: "test",
    aliases: &["t"],
    usage: "test [options] <archive> [pattern...]",
    summary: "Check the integrity of an archive, or of the members matching the patterns.",
    description: "Reads and decompresses the data and compares checksums without writing any files.\nExit status 0 means everything is intact; 1 means damaged members; 3 a missing or wrong\npassword.",
    options: &[PASSWORD_OPTS],
    examples: &["m7arc test backup.7z", "m7arc test --json backup.zip 'docs/*'"],
};

pub fn run(parsed: crate::args::Parsed, token: CancelToken) -> Result<(), CliError> {
    expect_positionals(&parsed, 1, None, "m7arc test <archive> [pattern...]")?;
    let ctx = Ctx::new(&parsed, token, false)?;
    let archive_path = existing_archive(&parsed, 0)?;
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
    let mut callbacks = ctx.callbacks();
    let result = ctx.engine.test(&archive_path, &indices, &open_options(&ctx), &mut callbacks);
    term::finish_progress();
    let report = result?;
    let summary = format!("{archive_path}: OK ({} items tested)", report.completed_items);
    ctx.finish("test", &archive_path, &summary, &report);
    Ok(())
}

