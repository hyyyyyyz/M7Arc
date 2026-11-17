use crate::args::{flag, Opt, Parsed};
use crate::ctx::{CliError, Ctx};
use crate::help::Spec;
use crate::json::Json;
use m7arc_core::CancelToken;

const OPTS: &[Opt] = &[
    flag("writable", None, "Only formats archives can be created in."),
    flag("hashers", None, "List checksum algorithms instead of archive formats."),
];

pub const SPEC: Spec = Spec {
    name: "formats",
    aliases: &[],
    usage: "formats [options]",
    summary: "List the supported archive formats (or checksum algorithms).",
    description: "",
    options: &[OPTS],
    examples: &["m7arc formats", "m7arc formats --writable --json", "m7arc formats --hashers"],
};

pub fn run(parsed: Parsed, token: CancelToken) -> Result<(), CliError> {
    if !parsed.positionals.is_empty() {
        return Err(CliError::Usage("formats takes no arguments".into()));
    }
    let ctx = Ctx::new(&parsed, token, false)?;
    if parsed.flag("hashers") {
        let hashers = ctx.engine.hashers()?;
        if ctx.json {
            let items = hashers.iter().map(|h| Json::object().with("name", h.name.as_str()).with("digest_bytes", h.digest_size)).collect::<Vec<_>>();
            ctx.emit_json(Json::object().with("ok", true).with("command", "formats").with("hashers", items));
        } else if !ctx.quiet {
            for h in &hashers {
                println!("{:<12} {} bytes", h.name, h.digest_size);
            }
        }
        return Ok(());
    }
    let formats: Vec<_> = ctx.engine.formats()?.into_iter().filter(|f| !f.is_hash && (!parsed.flag("writable") || f.can_write)).collect();
    if ctx.json {
        let items = formats
            .iter()
            .map(|f| {
                Json::object()
                    .with("name", f.name.as_str())
                    .with("extensions", Json::strings(&f.extensions))
                    .with("can_read", f.can_read)
                    .with("can_write", f.can_write)
            })
            .collect::<Vec<_>>();
        ctx.emit_json(Json::object().with("ok", true).with("command", "formats").with("formats", items));
    } else if !ctx.quiet {
        println!("{:<14} {:<5} Extensions", "Format", "Mode");
        for f in &formats {
            let mode = format!("{}{}", if f.can_read { "r" } else { "-" }, if f.can_write { "w" } else { "-" });
            println!("{:<14} {:<5} {}", f.name, mode, f.extensions.join(" "));
        }
    }
    Ok(())
}
