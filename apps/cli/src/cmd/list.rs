use super::{expect_positionals, open, existing_archive};
use crate::args::{flag, Opt};
use crate::ctx::{Ctx, CliError, PASSWORD_OPTS};
use crate::help::Spec;
use crate::json::Json;
use crate::{select, util};
use m7arc_core::{Archive, CancelToken, Entry};

const OPTS: &[Opt] = &[
    flag("long", Some('l'), "Also show method, attributes and encryption."),
    flag("names", None, "Print only the member paths, one per line."),
    flag("properties", None, "Also print the archive's own properties."),
];

pub const SPEC: Spec = Spec {
    name: "list",
    aliases: &["l", "ls"],
    usage: "list [options] <archive> [pattern...]",
    summary: "List the members of an archive.",
    description: "Without patterns every member is listed. A pattern is a member path (a folder\nselects everything below it) or a wildcard: '*' matches any characters including '/',\n'?' one character. Times are UTC. --json prints every member with all fields.",
    options: &[OPTS, PASSWORD_OPTS],
    examples: &["m7arc list photos.7z", "m7arc list --names backup.zip '*.jpg'", "m7arc list --json data.tar.gz"],
};

pub fn run(parsed: crate::args::Parsed, token: CancelToken) -> Result<(), CliError> {
    expect_positionals(&parsed, 1, None, "m7arc list <archive> [pattern...]")?;
    let ctx = Ctx::new(&parsed, token, false)?;
    let path = existing_archive(&parsed, 0)?;
    let archive = open(&ctx, &path)?;
    let patterns = &parsed.positionals[1..];
    let shown: Vec<&Entry> = if patterns.is_empty() {
        archive.entries.iter().collect()
    } else {
        let selection = select::select(&archive.entries, patterns);
        if let Some(first) = selection.unmatched.first() {
            return Err(CliError::failed(format!("no member matches '{first}'")));
        }
        archive.entries.iter().filter(|e| selection.indices.contains(&e.index)).collect()
    };
    if ctx.json {
        ctx.emit_json(document(&archive, &shown));
    } else if parsed.flag("names") {
        for entry in &shown {
            println!("{}", entry.path);
        }
    } else if !ctx.quiet {
        print_table(&archive, &shown, parsed.flag("long"), parsed.flag("properties"));
    }
    Ok(())
}

fn totals(entries: &[&Entry]) -> (u64, u64, u64, u64) {
    let mut t = (0, 0, 0, 0);
    for e in entries {
        if e.is_directory {
            t.1 += 1;
        } else {
            t.0 += 1;
            t.2 += e.size;
            t.3 += e.packed_size;
        }
    }
    t
}

fn document(archive: &Archive, shown: &[&Entry]) -> Json {
    let (files, dirs, size, packed) = totals(shown);
    let entries = shown
        .iter()
        .map(|e| {
            Json::object()
                .with("index", e.index)
                .with("path", e.path.as_str())
                .with("is_directory", e.is_directory)
                .with("size", e.size)
                .with("packed_size", e.packed_size)
                .with("encrypted", e.encrypted)
                .with("modified", e.modified_unix_seconds)
                .with("modified_iso", util::iso_time(e.modified_unix_seconds).map_or(Json::Null, Json::Str))
                .with("method", e.method.as_str())
                .with("attributes", e.attributes.as_str())
        })
        .collect::<Vec<_>>();
    let properties = archive
        .properties
        .iter()
        .map(|p| Json::object().with("name", p.name.as_str()).with("value", p.value.as_str()))
        .collect::<Vec<_>>();
    Json::object()
        .with("ok", true)
        .with("command", "list")
        .with("archive", archive.path.as_str())
        .with("format", archive.format.as_str())
        .with("can_update", archive.can_update)
        .with("volumes", Json::strings(&archive.volumes))
        .with("properties", properties)
        .with("totals", Json::object().with("files", files).with("directories", dirs).with("size", size).with("packed_size", packed))
        .with("entries", entries)
}

fn print_table(archive: &Archive, shown: &[&Entry], long: bool, properties: bool) {
    println!("Archive: {}", archive.path);
    println!("Format:  {}", archive.format);
    if properties {
        for p in &archive.properties {
            println!("{}: {}", p.name, p.value);
        }
    }
    println!();
    if long {
        println!("{:<19}  {:<5}  {:>12}  {:>12}  {:<10}  Name", "Modified (UTC)", "Flags", "Size", "Packed", "Method");
    } else {
        println!("{:<19}  {:<5}  {:>12}  {:>12}  Name", "Modified (UTC)", "Flags", "Size", "Packed");
    }
    for e in shown {
        let flags = format!("{}{}", if e.is_directory { 'D' } else { '.' }, if e.encrypted { 'E' } else { '.' });
        let name = if e.is_directory && !e.path.ends_with('/') { format!("{}/", e.path) } else { e.path.clone() };
        if long {
            println!(
                "{:<19}  {:<5}  {:>12}  {:>12}  {:<10}  {}",
                util::format_time(e.modified_unix_seconds),
                flags,
                e.size,
                e.packed_size,
                e.method,
                name
            );
        } else {
            println!("{:<19}  {:<5}  {:>12}  {:>12}  {}", util::format_time(e.modified_unix_seconds), flags, e.size, e.packed_size, name);
        }
    }
    let (files, dirs, size, packed) = totals(shown);
    println!();
    println!("{files} files, {dirs} folders, {} bytes ({}), {packed} bytes packed", size, util::human_size(size));
}

