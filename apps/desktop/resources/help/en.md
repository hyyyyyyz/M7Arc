# Getting started

M7Arc is a macOS archive and file manager powered by 7-Zip. Its seven toolbar buttons are Add, Extract, Test, Copy, Move, Delete and Information.

**Create an archive:** select local files or folders, choose Add, select an output path and format, adjust compression options, then confirm.

**Open an archive:** double-click it or enter its path in the address bar. For a file without a familiar extension, use File → 7-Zip → Open as format.

**Extract:** choose Extract and select a destination and overwrite policy. Inside an archive, selected members are extracted; with no selection, every member is extracted.

**Check integrity:** choose Test. The engine reads and decodes the selected data and reports any errors.

Search this offline manual by topic, format or shortcut. Command+F focuses search. The language selector changes the manual; application language is configured under Tools → Settings → Language.

# Navigation, panels and views

The address bar accepts local and archive paths. `~` opens your home folder. Backspace or the up arrow goes to the parent. Leaving a nested archive with unsaved changes first asks you to handle those changes.

F9 toggles one or two panels. Each panel has its own location, selection and status. Click a panel to make it active. In dual-panel mode, F5 and F6 use the other panel as the default destination.

View offers Large icons, Small icons, List and Details. The “≡” button selects visible columns; click column headings to sort. Archive columns include original and packed sizes, type, time, method, encryption, CRC, attributes and additional properties supplied by the format.

Flat view lists archive members recursively below the current directory. Show hidden files controls hidden local items. Refresh rereads the current folder or archive.

The panel status shows selected and total item counts, selected size, focused item size and modification time.

Favorites provides ten slots. Set a slot to the current location, then use it to return. Home folders, volumes under `/Volumes`, and mounted network shares are accessible through the address bar and bookmarks.

# Selection, shortcuts and file operations

Command-click adds items to a selection; Shift-click selects a range. Edit provides Select all, Deselect all, Invert selection, selection by file type and selection or deselection by wildcard. Examples include `*.txt` and `photo-??.jpg`.

| Shortcut | Action |
| --- | --- |
| Enter | Open focused item |
| Shift+Enter | Open with an external application |
| Backspace | Parent directory |
| F2 | Rename |
| F3 / F4 | View / Edit |
| F5 / F6 | Copy to / Move to |
| F7 | New folder |
| F9 | Toggle two panels |
| Command+A | Select all |
| Command+C / V | Copy / Paste |
| Command+X | Cut local items |
| Command+L | Enter a path |
| Command+, | Settings |
| Command+Q | Quit |

Some Mac keyboards require Fn with F2–F9 to send the function key.

Deleting local files moves them to Trash. Deleting archive members rewrites the archive; the confirmation identifies the selected scope. Rename, creation and deletion depend on the current archive's update capability.

Local items can have symbolic or hard links. Hard links apply to files on the same filesystem.

# Formats and methods

Tools → Formats and methods lists the engine's actual archive formats, extensions, codecs and hashers, with search. The bundled 7-Zip 26.03 baseline registers 61 handlers, 25 codecs and 10 hashers.

| Creation format | Purpose |
| --- | --- |
| 7z | Multiple files, solid compression, AES and encrypted filenames |
| ZIP | Multiple files and common archive interchange |
| TAR | Packaging files and POSIX attributes |
| GZip / BZip2 / XZ | A single compressed stream |
| WIM | File image container; creation uses Store |
| TAR+GZip / TAR+BZip2 / TAR+XZ | Package multiple files or directories, then compress the stream |

RAR/RAR5, Zstandard, DMG, ISO, APFS, HFS, NTFS, virtual disks and executable containers are readable through their respective handlers. The bundled engine does not create RAR or Zstandard archives.

Reading, creation and updating are separate capabilities. Solid resources, encryption, volumes, format versions and container structure can affect whether an existing archive is writable. Read-only archives can still be browsed, tested and extracted.

# Creating and updating archives

Add exposes format, level, method, dictionary, word size, solid block, threads, memory estimates, volumes and encryption options. Available choices change with the format.

Higher levels and larger dictionaries generally require more time and memory. Solid compression shares compression context across files; extracting individual members or updating may need to read more data. Memory values are estimates based on selected parameters, not a fixed allocation guarantee.

GZip, BZip2 and XZ directly accept one file. To compress several files or a directory, select a TAR combination.

Update modes:

- **Add / replace:** add new files and replace members with matching paths.
- **Update:** add new files and update members when the local source is newer.
- **Freshen:** update existing members only.
- **Synchronize:** synchronize the selected input scope, including removal of members absent from that scope.

When browsing a writable archive, Add selects local inputs and writes them into the current archive directory. F7 creates a directory; File → New file creates an empty file.

Advanced options cover stored path mode, include/exclude rules, timestamps, ownership, symbolic and hard links, shared files, access-time preservation and source deletion after compression.

Additional method properties use `name=value`, separated by semicolons, for example `fb=64;mc=16`. The chosen handler and method must support each property. Unsupported settings produce an engine error.

# Passwords, encryption and volumes

Enter matching passwords in the password and confirmation fields. 7z supports AES and encrypted filenames; ZIP uses the selected encryption method. Encrypted filenames require a password even to read the directory. Encrypting content alone may leave directory information visible.

Opening or extracting encrypted data asks for a password as needed. A read-only operation can retry an incorrect password while retaining the original selection, destination and extraction settings. Some password errors can also indicate damaged data; use Test and the error details to investigate.

Volume sizes accept positive integers and `K`, `M`, `G` or `T`, such as `100M`. If several sizes are specified, they apply in order and the last size is repeated.

Keep all volumes together and open the first one, such as `.7z.001`. If a volume is missing, inspect the error, restore the missing part and reopen. The Split file tool creates byte segments `.001`, `.002`, etc.; these are distinct from a format's own multivolume structure.

# Extraction, paths and overwriting

Extract offers destination, archive-named folder, duplicate-root handling, metadata and password settings.

| Path mode | Result |
| --- | --- |
| Full paths | Keep relative directory structure stored in the archive |
| No paths | Flatten files into the destination |
| Relative to current directory | Remove the current archive-directory prefix and keep paths below it |
| Absolute paths | Use stored absolute-path semantics for an archive whose paths you intend to restore |

Overwrite choices are Ask, Replace, Skip, Rename incoming and Rename existing. Interactive conflict prompts can apply replacement or skipping to all remaining conflicts.

Extract here uses the archive's local directory. Extract to archive-named folder adds a subfolder. Multiple archives selected in the local panel can be extracted or tested in one operation.

Flattening may bring files from different archive directories to the same name. Choose an overwrite or automatic rename policy suitable for those conflicts.

# Copying, moving and Finder drag and drop

Dual-panel operations support local-to-local, local-to-archive, archive-to-local and archive-to-archive transfers. Select items and use F5/F6, or drag into the other panel.

Moving archive members writes the destination before deleting the source. Failure or cancellation during the destination stage keeps source members. If an archive-to-local move skips any item, all source members are retained and the result explains the incomplete move. A failure to delete the source leaves the completed destination copy intact.

Dragging archive members into Finder starts extraction after Finder accepts the drop. Password, progress and cancellation remain in M7Arc. Dragged folders include their contents.

Command+C inside an archive extracts selected items and places file URLs on the system clipboard. Paste them in Finder or another archive. Clipboard files remain in a temporary directory so pasting still works after the window closes.

Files copied in Finder or a local panel can be pasted with Command+V into the current directory of a writable archive. Pasting cut local files into an archive removes the local sources only after the archive write succeeds.

# External editing and nested archives

F3 uses the viewer and F4 the editor. Configure executables or `.app` bundles under Settings → View / Edit / Diff. An empty setting uses the system default application.

An archive member is extracted into a separate temporary directory before an external application opens it. Save your changes and return to M7Arc to choose Save to archive, Discard changes or Cancel. File → Check and save archive edits also checks manually.

- **Save to archive:** replace the original exact member and save nested archives from the inside out.
- **Discard changes:** restore the temporary file from the original archive.
- **Cancel:** keep the current edit and temporary files available.

For example, editing a text file in `inner.zip` inside `outer.7z` updates the ZIP first and the outer 7z next.

If another application changes the original archive during editing, M7Arc asks you to reopen it rather than writing to an uncertain member identity. Failed saves retain edited temporary files. Tools → Temporary files shows their directories so you can save a separate copy.

Each parent archive must support updating. Resolve pending edits before quitting.

# macOS metadata, alternate streams and comments

Preserve / restore macOS metadata in creation and extraction controls extended attributes, resource forks, Finder information and ACL data. AppleDouble sidecars store Mac data where needed. Local copy operations also preserve metadata.

Select one local item and choose File → Extended attributes and resource fork to inspect names, sizes and up to 4 KiB of text or hexadecimal preview. Export saves a selected attribute separately.

Archives containing Windows alternate streams expose them through File → Alternate streams. Select and extract streams as independent files. Windows alternate streams and macOS resource forks are different data structures.

Select a ZIP member and choose File → Comment to view or edit its comment. This action is enabled by the format's actual capability.

Information shows archive and member sizes, timestamps, methods and raw properties. Restoring permissions and ownership also depends on the output format and destination filesystem.

# Hashes, splitting, combining and benchmarking

Select local files and use File → CRC / SHA to choose one algorithm or all algorithms. Results show actual file digests and can be saved as a manifest. The bundled hashers are CRC32, CRC64, MD5, SHA1, SHA256, SHA384, SHA512, SHA3-256, XXH64 and BLAKE2sp; consult Formats and methods for the current set.

Split file writes consecutive `.001`, `.002` byte segments at the chosen size. Combine file starts at a selected `.001`, reads consecutive segments and writes a new output. Source segments remain available.

Tools → Benchmark runs the official compression benchmark. Set dictionary, threads, passes and seconds per pass. Results include compression and decompression speed, rating, CPU use and averages. Stop, restart or save the log as needed.

The task window provides Pause, Resume, Background and Cancel. Background hides the window while work continues; Task progress in the main window shows it again. Cancellation waits for necessary engine cleanup. Failure details identify individual errors.

# Finder Services, Quick Actions and preview

M7Arc provides Compress, Extract and Test Finder Services for selected files. Settings → System → Install Finder Quick Actions installs the corresponding Automator workflows.

Workflows live in `~/Library/Services`. Find them in Finder's Quick Actions or Services menus. If absent, check macOS extension and Services settings.

Settings → System lists format associations and can make M7Arc the default for selected types. Finder → Get Info → Open with provides another way to choose the default application.

The bundled Quick Look extension displays archive members, sizes and methods. Open the archive in M7Arc when a password, missing volume or other interaction is required. Extension loading depends on installation and macOS extension settings.

# Self-extracting archives and the command line

Select local files and choose Tools → Create self-extracting archive. Choose a target, method, level and password. The default is LZMA2 with a 7z payload, which is integrity-tested before packaging.

| Output | Use |
| --- | --- |
| macOS graphical `.app` | Embedded archive; double-click for destination and password prompts |
| macOS graphical DMG, embedded | A standalone graphical extractor inside the DMG |
| macOS graphical DMG, external | Keep the extractor beside `Payload.7z` |
| macOS `.command` | Official console SFX for the current Mac architecture, run in Terminal |
| Windows GUI / Console `.exe` | Official x86 module for Windows systems supporting 32-bit applications |

The Mac graphical extractor includes progress and macOS metadata restoration. Mac outputs use ad-hoc signing. Windows outputs contain no installation or post-extraction execution commands.

The application includes the full official command line:

```sh
"/Applications/M7Arc.app/Contents/MacOS/7zz" i
"/Applications/M7Arc.app/Contents/MacOS/7zz" a archive.7z Documents
"/Applications/M7Arc.app/Contents/MacOS/7zz" x archive.7z -oextracted
"/Applications/M7Arc.app/Contents/MacOS/7zz" h -scrcSHA256 archive.7z
"/Applications/M7Arc.app/Contents/MacOS/7zz" b
```

`i` lists capabilities, `a` creates or updates, `x` extracts, `t` tests, `h` calculates digests and `b` benchmarks. Quote paths containing spaces.

Console SFX requires the corresponding Mac architecture. It supports testing, extraction, destination and password arguments. It does not automatically run files from the archive. Windows SFX, Mac console SFX and graphical extractors are separate distribution types.

# Settings, installation and removal

Settings has six pages: System, Menus, Folders, View / Edit / Diff, General and Language. Menus shows or hides common commands. Folders selects startup and temporary directories. General controls hidden files, alternating rows, automatic refresh, metadata and window geometry.

Application language can follow the system, use Simplified Chinese or use English. Changes take effect after restarting. The help window's language selector only changes this manual.

Download the Apple Silicon or Intel DMG from the project's GitHub Release, open it and drag M7Arc into Applications. Quit the old version before replacing it. Releases use ad-hoc signing and do not have Developer ID signing or Apple notarization. Follow macOS's displayed instructions when opening the downloaded application.

To uninstall, move M7Arc.app to Trash. If Quick Actions were installed, also remove `M7Arc Compress.workflow`, `M7Arc Extract.workflow` and `M7Arc Test.workflow` from `~/Library/Services`. Restore other default applications through Finder's Get Info when desired.

Versions use SemVer: patch versions contain fixes, minor versions add features and major versions indicate significant agreed changes. `alpha`, `beta` and `rc` identify prereleases.

# Troubleshooting and limitations

**Why is an action disabled?** Selection, task state or archive capability does not meet its requirements. RAR cannot update, comments require supported ZIP members and Copy requires a selection.

**Why can I read a format but not create it?** Reading and writing are separate capabilities. Check their labels under Formats and methods.

**Why does the password still fail?** Ensure all volumes are present and run Test. Error details can identify damaged data, read failures or unsupported methods.

**Why did a nested save fail?** Check that every parent is writable and that the outer archive was not modified elsewhere. Keep the edited temporary file, reopen the archive and write it separately.

**Out of disk space or memory?** Use output and temporary directories with free space, or reduce dictionary and thread settings. Archive updates and transfers can require temporary storage.

**Finder extension missing?** Check application installation, launch it, and review macOS extension and Services settings. The main application can handle archives independently.

Known limitations: releases are not Apple-notarized; bundled RAR support is read-only; TAR combinations rebuild their outer stream; console SFX targets one Mac architecture; Windows attributes do not directly map to macOS filesystem properties.
