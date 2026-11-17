//! The subcommands.

pub mod create;
pub mod edit;
pub mod extract;
pub mod formats;
pub mod hash;
pub mod list;
pub mod test;

use crate::args::{usage, Parsed};
use crate::ctx::{CliError, Ctx};
use crate::help::Spec;
use crate::util;
use m7arc_core::{Archive, OpenOptions};
use m7arc_core::CancelToken;

pub type RunFn = fn(Parsed, CancelToken) -> Result<(), CliError>;

/// Every command with its entry point, in the order shown by `--help`.
pub fn all() -> Vec<(&'static Spec, RunFn)> {
    vec![
        (&list::SPEC, list::run as RunFn),
        (&extract::SPEC, extract::run),
        (&create::CREATE_SPEC, create::run_create),
        (&create::ADD_SPEC, create::run_add),
        (&test::SPEC, test::run),
        (&edit::DELETE_SPEC, edit::run_delete),
        (&edit::RENAME_SPEC, edit::run_rename),
        (&hash::SPEC, hash::run),
        (&formats::SPEC, formats::run),
    ]
}

/// The positional argument at `index` as an absolute archive path.
pub fn path_arg(parsed: &Parsed, index: usize, what: &str) -> Result<String, CliError> {
    let Some(raw) = parsed.positionals.get(index) else {
        return usage(format!("missing {what}")).map_err(Into::into);
    };
    to_absolute(raw)
}

/// Like [`path_arg`], for an archive that must already exist.
pub fn existing_archive(parsed: &Parsed, index: usize) -> Result<String, CliError> {
    let path = path_arg(parsed, index, "archive")?;
    if !std::path::Path::new(&path).is_file() {
        return Err(CliError::failed(format!("{}: no such file", parsed.positionals[index])));
    }
    Ok(path)
}

pub fn to_absolute(raw: &str) -> Result<String, CliError> {
    let path = util::absolute(std::path::Path::new(raw)).map_err(|e| CliError::failed(format!("{raw}: {e}")))?;
    Ok(path.to_string_lossy().into_owned())
}

pub fn open_options(ctx: &Ctx) -> OpenOptions {
    OpenOptions { password: ctx.password.clone(), archive_type: None }
}

/// Lists `archive`, asking for a password when needed.
pub fn open(ctx: &Ctx, archive: &str) -> Result<Archive, CliError> {
    let mut callbacks = ctx.callbacks();
    let result = ctx.engine.list(archive, &open_options(ctx), &mut callbacks);
    crate::term::finish_progress();
    Ok(result?)
}

/// Fails with a usage error when the command received an unexpected number of positionals.
pub fn expect_positionals(parsed: &Parsed, min: usize, max: Option<usize>, synopsis: &str) -> Result<(), CliError> {
    let n = parsed.positionals.len();
    if n < min || max.is_some_and(|m| n > m) {
        return Err(CliError::Usage(format!("expected: {synopsis}")));
    }
    Ok(())
}
