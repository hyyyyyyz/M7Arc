# m7arc — command-line archiver

`m7arc` creates, lists, extracts, tests and edits archives from the terminal and from shell scripts. It is a thin Rust front end over the same engine as the M7Arc desktop app (upstream 7-Zip behind the C interface in `src/engine/capi`), used through the `m7arc-core` crate. Formats are those of the engine: read and write 7z, zip, tar, gzip, bzip2, xz, wim and the compound `tar.gz`/`tar.bz2`/`tar.xz`; read-only support for many more (`m7arc formats`).

- Results go to **stdout**; progress and diagnostics go to **stderr**.
- `--json` turns the result (and any error) into one JSON line on stdout.
- Exit codes are fixed and documented below.
- It never waits for input when it has no terminal.
- No third-party crates are used.

## Build

Requirements: macOS 13+, Rust 1.80+, CMake 3.24+ and the Xcode command line tools. Qt is not needed.

```sh
cargo build --release        # from the repository root
build/cargo/release/m7arc --version
```

The first build compiles the vendored 7-Zip sources into `libm7arc_engine.dylib` (see `crates/README.md`). The binary finds the library through these rpaths, in order of use:

| rpath | Purpose |
|---|---|
| the engine build directory (`build/engine-rust/lib`, or `M7ARC_ENGINE_LIB_DIR`) | development builds run in place |
| `@executable_path/../lib` | an installed prefix: `bin/m7arc` next to `lib/libm7arc_engine.dylib` |
| `@executable_path/../Frameworks` | an app bundle: `Contents/MacOS/m7arc` next to `Contents/Frameworks/` |

Installing into a prefix:

```sh
PREFIX=/usr/local
install -d "$PREFIX/bin" "$PREFIX/lib"
install build/cargo/release/m7arc "$PREFIX/bin/"
install build/engine-rust/lib/libm7arc_engine.dylib "$PREFIX/lib/"
```

Tests (these run the real engine end to end):

```sh
cargo test --workspace
```

## Commands

```
m7arc <command> [options] [arguments]

  list, l, ls     List the members of an archive.
  extract, x      Extract an archive, or only the members matching patterns.
  create, c       Create a new archive from files and folders.
  add, a          Add files and folders to an archive, creating it if needed.
  test, t         Check the integrity of an archive.
  delete, rm, d   Remove members from an archive.
  rename, mv      Rename a member (or a folder) inside an archive.
  hash, h         Compute checksums, or verify a checksum file.
  formats         List supported formats (or checksum algorithms).
```

`m7arc --help`, `m7arc help <command>` and `m7arc <command> --help` print the complete reference, including every option. Options may appear anywhere after the command; `--` ends option parsing. Relative paths are resolved against the working directory.

### Selecting members

`list`, `extract`, `test` and `delete` take patterns after the archive name. A pattern is either a member path (a folder selects everything below it) or a wildcard: `*` matches any characters **including `/`**, `?` matches one character. Quote wildcards. A pattern that matches nothing is an error (exit 1).

### list

```sh
m7arc list photos.7z                  # table: modified (UTC), flags (D folder, E encrypted), size, packed size, name
m7arc list --long photos.7z           # adds the compression method
m7arc list --names backup.zip '*.jpg' # paths only, one per line
m7arc list --json photos.7z           # {"ok":true,"command":"list","format":"7z","entries":[...],"totals":{...},...}
```

### extract

```sh
m7arc extract photos.7z -o ~/Pictures
m7arc extract backup.zip 'docs/*.pdf' -o out
m7arc extract --archive-folder photos.7z -o ~/Pictures   # into ~/Pictures/photos/
```

| Option | Meaning |
|---|---|
| `-o, --output DIR` | destination (created if needed); default: current directory |
| `--overwrite MODE` | `ask`, `replace`, `skip`, `rename` (new file gets a new name), `rename-existing` |
| `-y, --yes` | same as `--overwrite replace` |
| `--flat` | drop folders, put every file in DIR |
| `--archive-folder` | extract into a folder named after the archive |
| `--dedupe-root` | avoid `DIR/name/name/...` when the archive has a single top folder named like itself |
| `--no-mac-metadata` | do not restore macOS metadata stored in the archive |

**Existing files.** On a terminal `extract` asks (replace / replace all / skip / skip all / rename / quit). With no terminal it **skips** existing files, so a script can never hang or overwrite data by accident; say `--overwrite replace` (or `-y`) when replacing is intended.

### create and add

```sh
m7arc create photos.7z ~/Pictures                      # format from the name; default level 5
m7arc create site.zip public --exclude '*.map' --exclude '.DS_Store'
m7arc create logs.tar.xz /var/log/app -l 9
m7arc create big.7z data --volume 700m                 # big.7z.001, big.7z.002, ...
m7arc create secret.7z docs --password-env ARCHIVE_PW --encrypt-names
m7arc add backup.7z notes.txt                          # add or replace members
m7arc add backup.7z ~/Documents --update               # only newer files
```

`create` refuses to overwrite an existing archive (exit 1) unless `--force` is given; `add` extends an existing archive or creates it. The format comes from `--format` or the archive name (`.7z`, `.zip`, `.tar`, `.tar.gz`/`.tgz`, `.tar.bz2`/`.tbz2`, `.tar.xz`/`.txz`, `.gz`, `.bz2`, `.xz`, `.wim`); a name without a known extension needs `--format`.

| Option | Meaning |
|---|---|
| `-f, --format F` | `7z`, `zip`, `tar`, `tar.gz`, `tar.bz2`, `tar.xz`, `gzip`, `bzip2`, `xz`, `wim` |
| `-l, --level 0-9` | 0 stores, 9 is smallest (default 5) |
| `-m, --method NAME`, `--dictionary SIZE`, `-t, --threads N`, `--no-solid` | codec tuning |
| `-v, --volume SIZE[,SIZE]` | split into volumes (suffixes `k`, `m`, `g`) |
| `-i, --include PAT`, `-x, --exclude PAT` | repeatable file filters |
| `--prefix DIR`, `--paths relative\|full\|absolute` | how paths are stored |
| `--encrypt-names` | encrypt file names too (7z; needs a password) |
| `--encryption-method NAME` | for example `aes256` or `zipcrypto` |
| `--no-mac-metadata` | do not store macOS metadata (resource forks, extended attributes) |
| `--set NAME=VALUE` | raw format/codec property, repeatable |
| `--update`, `--freshen`, `--sync` (`add` only) | replace by date / only existing / mirror the inputs |

By default macOS metadata (resource forks, extended attributes) is stored as `__MACOSX/` members and restored on extraction, like the desktop app. Use `--no-mac-metadata` for archives meant for other systems.

### test

```sh
m7arc test backup.7z                  # exit 0 only if every member is intact
m7arc test backup.zip 'docs/*' --json
```

### delete and rename

```sh
m7arc delete backup.7z old-notes.txt '*.tmp'
m7arc rename backup.7z notes.txt docs/notes.txt
m7arc rename backup.7z docs documents      # renames the folder and everything below it
```

The archive is rewritten; formats the engine cannot update are refused (exit 1).

### hash

```sh
m7arc hash big.iso                          # <sha256>  big.iso
m7arc hash -a sha256,crc32 docs             # ALGORITHM  DIGEST  PATH, folders recursively
m7arc hash docs > docs.sha256               # same format as sha256sum
m7arc hash --check docs.sha256              # verify; paths are relative to the checksum file
```

`m7arc formats --hashers` lists the algorithms. Paths are printed the way you typed them.

### formats

```sh
m7arc formats                  # name, r/w mode, extensions
m7arc formats --writable --json
m7arc formats --hashers
```

## Scripting

### Passwords

| Source | Notes |
|---|---|
| `--password-file FILE` | first line of FILE; `-` reads stdin |
| `--password-env VAR` | environment variable VAR |
| `M7ARC_PASSWORD` | used implicitly by commands that **open** archives; never used by `create`/`add` |
| `-P, --ask-password` | terminal prompt without echo (twice when creating) |
| `--password TEXT` | works, but TEXT is visible to every user in the process list; prefer the sources above |

If an archive needs a password and none was supplied, a terminal session is prompted; without a terminal the command exits with status 3 and says how to supply one. At most one explicit source may be given.

### Output

- `--json` prints exactly one line of JSON on stdout. Success documents carry `"ok":true`; failures print `"ok":false`, `"exit_code"`, `"error":{"kind","message"}` and `"errors":[...]` and also write the message to stderr. Error kinds: `usage`, `failed`, `cancelled`, `password_required`, `wrong_password`, `operation_failed`, `invalid_argument`, `engine`.
- `-q, --quiet` suppresses the success message (and, for `list`, the table).
- The progress line is drawn on stderr only when both stdout and stderr are terminals; `--no-progress` turns it off.
- Times are UTC. `list --json` gives `modified` (Unix seconds) and `modified_iso`.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | the operation failed: unreadable files, damaged archive, member errors, no member matches a pattern, checksum mismatch, existing archive without `--force` |
| 2 | usage error: unknown command or option, missing argument, invalid value |
| 3 | the archive needs a password, or the password is wrong |
| 130 | cancelled by Ctrl-C or SIGTERM |

### Cancelling

The first Ctrl-C (or `kill -TERM`) cancels the running operation through the engine's cancel token; an archive being written is not left behind, then the command exits with 130. A second Ctrl-C ends the process immediately. During a password or overwrite prompt Ctrl-C exits at once.

### Examples

Each of these was run against a real build.

**Extract every archive in a directory into its own folder**

```sh
#!/bin/sh
# usage: extract-all.sh DIR   (extracts DIR/*.{7z,zip,tar.gz} into DIR/extracted/<name>/)
dir=${1:?usage: extract-all.sh DIR}
failed=0
for archive in "$dir"/*.7z "$dir"/*.zip "$dir"/*.tar.gz; do
  [ -e "$archive" ] || continue
  m7arc extract -q --archive-folder "$archive" -o "$dir/extracted"
  case $? in
    0) echo "ok       $archive" ;;
    3) echo "password $archive"; failed=1 ;;
    *) echo "failed   $archive"; failed=1 ;;
  esac
done
exit $failed
```

**Back up, verify, and record a checksum**

```sh
#!/bin/sh
# usage: backup.sh DEST_DIR PATH...   (creates DEST_DIR/backup-<time>.7z, tests it, appends to SHA256SUMS)
set -eu
dest=${1:?usage: backup.sh DEST_DIR PATH...}; shift
name=backup-$(date +%Y%m%d-%H%M%S).7z
mkdir -p "$dest"
m7arc create -q -l 7 "$dest/$name" "$@"
m7arc test -q "$dest/$name"
(cd "$dest" && m7arc hash "$name" >> SHA256SUMS)
echo "wrote $dest/$name"
# later: (cd DEST_DIR && m7arc hash --check SHA256SUMS)
```

**Read JSON from a script**

```sh
# Total uncompressed size and the three largest members of an archive.
m7arc list --json photos.7z | python3 -c '
import json, sys
doc = json.load(sys.stdin)
print("total bytes:", doc["totals"]["size"])
files = [e for e in doc["entries"] if not e["is_directory"]]
for e in sorted(files, key=lambda e: e["size"], reverse=True)[:3]:
    print(e["size"], e["path"])
'
```

**Encrypted archives in an unattended job**

```sh
export ARCHIVE_PW='correct horse battery staple'      # or --password-file /path/to/pw
m7arc create -q secret.7z docs --password-env ARCHIVE_PW --encrypt-names
M7ARC_PASSWORD=$ARCHIVE_PW m7arc test secret.7z
m7arc list secret.7z </dev/null; echo "exit $?"        # 3: password required, no prompt
```

## Relation to the bundled `7zz`

The app bundle also ships upstream's `7zz` (7-Zip command line). Both run the same codecs and format handlers, so for compress/extract/test/list/hash the results are interchangeable.

| | `7zz` | `m7arc` |
|---|---|---|
| Command style | 7-Zip's `a`, `x`, `l`, `t`, `d`, `rn`, `h` with switches such as `-mx9`, `-p`, `-o` | verbs with long, consistently named options |
| Machine-readable output | text, with a `-slt` technical listing | `--json` for list, hash, formats and every operation result and error |
| Exit codes | 0 / 1 (warning) / 2 (fatal) / 7 (command line) / 8 (memory) / 255 (stopped) | 0 / 1 / 2 / 3 (password) / 130 (cancelled) |
| Password | `-p` on the command line (visible in `ps`), or prompt | file, environment variable, prompt, or (discouraged) `--password` |
| Overwrite without a terminal | `-aos`/`-aoa` flags | skips by default, `--overwrite` to choose |
| Member selection | 7-Zip wildcards | member paths and `*`/`?` patterns; folders select their contents |

`7zz` offers many more switches (benchmark, SFX creation, fine-grained `-m` keys beyond `--set`) and remains the choice for scripts already written for it; `m7arc` is for new scripts that want predictable options, JSON and exit codes.

## Limitations

- macOS only, like the rest of the project.
- Paths must be valid UTF-8.
- `rename` and `delete` rewrite the whole archive, as the engine does.
- Files skipped because they already exist are not reported separately in the result.
