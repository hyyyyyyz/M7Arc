use crate::callbacks::Callbacks;
use crate::error::{Error, Result};
use crate::options::*;
use crate::types::*;
use m7arc_sys as sys;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::path::Path;
use std::ptr::{self, NonNull};

unsafe fn text(value: *const c_char) -> String {
    if value.is_null() {
        String::new()
    } else {
        CStr::from_ptr(value).to_string_lossy().into_owned()
    }
}

fn last_error() -> String {
    unsafe { text(sys::m7arc_last_error()) }
}

fn cstring(value: &str) -> Result<CString> {
    CString::new(value).map_err(|_| Error::InvalidArgument(format!("text contains a NUL byte: {value:?}")))
}

fn path_cstring(path: &Path) -> Result<CString> {
    let text = path.to_str().ok_or_else(|| Error::InvalidArgument(format!("path is not valid UTF-8: {}", path.display())))?;
    cstring(text)
}

fn optional(value: &Option<String>) -> Result<Option<CString>> {
    value.as_deref().map(cstring).transpose()
}

fn ptr_of(value: &Option<CString>) -> *const c_char {
    value.as_ref().map_or(ptr::null(), |v| v.as_ptr())
}

/// Owned C strings plus the pointer array that refers to them.
struct CStrings {
    _owned: Vec<CString>,
    pointers: Vec<*const c_char>,
}

impl CStrings {
    fn new<S: AsRef<str>>(values: &[S]) -> Result<CStrings> {
        let owned = values.iter().map(|v| cstring(v.as_ref())).collect::<Result<Vec<_>>>()?;
        let pointers = owned.iter().map(|v| v.as_ptr()).collect();
        Ok(CStrings { _owned: owned, pointers })
    }

    fn from_paths<P: AsRef<Path>>(values: &[P]) -> Result<CStrings> {
        let owned = values.iter().map(|v| path_cstring(v.as_ref())).collect::<Result<Vec<_>>>()?;
        let pointers = owned.iter().map(|v| v.as_ptr()).collect();
        Ok(CStrings { _owned: owned, pointers })
    }

    fn ptr(&self) -> *const *const c_char {
        if self.pointers.is_empty() {
            ptr::null()
        } else {
            self.pointers.as_ptr()
        }
    }

    fn len(&self) -> usize {
        self.pointers.len()
    }
}

fn slice_ptr<T>(values: &[T]) -> *const T {
    if values.is_empty() {
        ptr::null()
    } else {
        values.as_ptr()
    }
}

/// Raw open options kept alive for the duration of a call.
struct RawOpen {
    _password: Option<CString>,
    _kind: Option<CString>,
    raw: sys::m7arc_open_options,
}

fn raw_open(options: &OpenOptions) -> Result<RawOpen> {
    let password = optional(&options.password)?;
    let kind = optional(&options.archive_type)?;
    let raw = sys::m7arc_open_options { password: ptr_of(&password), type_: ptr_of(&kind) };
    Ok(RawOpen { _password: password, _kind: kind, raw })
}

macro_rules! owned_handle {
    ($name:ident, $raw:ty, $free:path) => {
        struct $name(*mut $raw);
        impl Drop for $name {
            fn drop(&mut self) {
                if !self.0.is_null() {
                    unsafe { $free(self.0) }
                }
            }
        }
    };
}
owned_handle!(ResultHandle, sys::m7arc_result, sys::m7arc_result_free);
owned_handle!(SnapshotHandle, sys::m7arc_snapshot, sys::m7arc_snapshot_free);
owned_handle!(FormatsHandle, sys::m7arc_format_list, sys::m7arc_format_list_free);
owned_handle!(HashersHandle, sys::m7arc_hasher_list, sys::m7arc_hasher_list_free);
owned_handle!(HashHandle, sys::m7arc_hash_report, sys::m7arc_hash_report_free);
owned_handle!(ManifestHandle, sys::m7arc_manifest, sys::m7arc_manifest_free);

fn read_report(handle: &ResultHandle) -> OperationReport {
    let r = handle.0;
    unsafe {
        OperationReport {
            completed_items: sys::m7arc_result_completed_items(r),
            output_paths: (0..sys::m7arc_result_output_path_count(r)).map(|i| text(sys::m7arc_result_output_path(r, i))).collect(),
            errors: (0..sys::m7arc_result_error_count(r)).map(|i| text(sys::m7arc_result_error(r, i))).collect(),
            password_may_be_incorrect: sys::m7arc_result_password_may_be_incorrect(r),
        }
    }
}

/// Converts the status and result handle of an operation call.
fn operation_outcome(status: i32, handle: ResultHandle) -> Result<OperationReport> {
    match status {
        sys::M7ARC_OK => Ok(read_report(&handle)),
        sys::M7ARC_ERR_OPERATION_FAILED if !handle.0.is_null() => Err(Error::OperationFailed(read_report(&handle))),
        other => Err(Error::from_status(other, last_error())),
    }
}

fn status_only(status: i32) -> Result<()> {
    if status == sys::M7ARC_OK {
        Ok(())
    } else {
        Err(Error::from_status(status, last_error()))
    }
}

/// Handle to the archive engine. See the crate documentation for the threading rules.
pub struct Engine {
    raw: NonNull<sys::m7arc_engine>,
}

// The engine may move between threads; it is not safe to use from two at once.
unsafe impl Send for Engine {}

impl Drop for Engine {
    fn drop(&mut self) {
        unsafe { sys::m7arc_engine_free(self.raw.as_ptr()) }
    }
}

impl Engine {
    /// An engine with the default options, including user plug-in modules.
    pub fn new() -> Result<Engine> {
        Engine::with_options(&EngineOptions::default())
    }

    /// The first engine of a process fixes the module directory set; later ones must match it.
    pub fn with_options(options: &EngineOptions) -> Result<Engine> {
        let directories = CStrings::from_paths(&options.additional_module_directories)?;
        let raw_options = sys::m7arc_engine_options {
            load_user_modules: options.load_user_modules,
            additional_module_directories: directories.ptr(),
            additional_module_directory_count: directories.len(),
        };
        let mut raw = ptr::null_mut();
        status_only(unsafe { sys::m7arc_engine_new(&raw_options, &mut raw) })?;
        NonNull::new(raw).map(|raw| Engine { raw }).ok_or_else(|| Error::Internal("engine creation returned null".into()))
    }

    /// Upstream 7-Zip version the engine is built from.
    pub fn version() -> String {
        unsafe { text(sys::m7arc_version()) }
    }

    fn handle(&self) -> *mut sys::m7arc_engine {
        self.raw.as_ptr()
    }

    pub fn formats(&self) -> Result<Vec<FormatInfo>> {
        let mut out = ptr::null_mut();
        status_only(unsafe { sys::m7arc_formats(self.handle(), &mut out) })?;
        let list = FormatsHandle(out);
        let formats = unsafe {
            (0..sys::m7arc_format_list_count(list.0))
                .map(|i| FormatInfo {
                    name: text(sys::m7arc_format_list_name(list.0, i)),
                    extensions: (0..sys::m7arc_format_list_extension_count(list.0, i))
                        .map(|j| text(sys::m7arc_format_list_extension(list.0, i, j)))
                        .collect(),
                    can_read: sys::m7arc_format_list_can_read(list.0, i),
                    can_write: sys::m7arc_format_list_can_write(list.0, i),
                    is_hash: sys::m7arc_format_list_is_hash(list.0, i),
                })
                .collect()
        };
        Ok(formats)
    }

    pub fn hashers(&self) -> Result<Vec<HasherInfo>> {
        let mut out = ptr::null_mut();
        status_only(unsafe { sys::m7arc_hashers(self.handle(), &mut out) })?;
        let list = HashersHandle(out);
        let hashers = unsafe {
            (0..sys::m7arc_hasher_list_count(list.0))
                .map(|i| HasherInfo {
                    name: text(sys::m7arc_hasher_list_name(list.0, i)),
                    digest_size: sys::m7arc_hasher_list_digest_size(list.0, i),
                })
                .collect()
        };
        Ok(hashers)
    }

    /// Opens `archive` and returns its members and properties.
    pub fn list(&self, archive: impl AsRef<Path>, options: &OpenOptions, callbacks: &mut Callbacks<'_>) -> Result<Archive> {
        let archive = path_cstring(archive.as_ref())?;
        let open = raw_open(options)?;
        let mut out = ptr::null_mut();
        let status = callbacks.with_raw(|cb| unsafe { sys::m7arc_list(self.handle(), archive.as_ptr(), &open.raw, cb, &mut out) });
        let snapshot = SnapshotHandle(out);
        status_only(status)?;
        Ok(read_snapshot(&snapshot))
    }

    /// Extracts the entries with the given indices, or every entry when `indices` is empty.
    pub fn extract(
        &self,
        archive: impl AsRef<Path>,
        destination: impl AsRef<Path>,
        indices: &[u32],
        options: &ExtractOptions,
        callbacks: &mut Callbacks<'_>,
    ) -> Result<OperationReport> {
        let archive = path_cstring(archive.as_ref())?;
        let destination = path_cstring(destination.as_ref())?;
        let password = optional(&options.password)?;
        let current = options.current_directory.as_deref().map(path_cstring).transpose()?;
        let raw = sys::m7arc_extract_options {
            password: ptr_of(&password),
            path_mode: match options.path_mode {
                ExtractPathMode::Full => sys::M7ARC_EXTRACT_PATHS_FULL,
                ExtractPathMode::Flat => sys::M7ARC_EXTRACT_PATHS_FLAT,
                ExtractPathMode::Current => sys::M7ARC_EXTRACT_PATHS_CURRENT,
                ExtractPathMode::Absolute => sys::M7ARC_EXTRACT_PATHS_ABSOLUTE,
            },
            overwrite: match options.overwrite {
                OverwriteMode::Ask => sys::M7ARC_OVERWRITE_ASK,
                OverwriteMode::Replace => sys::M7ARC_OVERWRITE_REPLACE,
                OverwriteMode::Skip => sys::M7ARC_OVERWRITE_SKIP,
                OverwriteMode::RenameIncoming => sys::M7ARC_OVERWRITE_RENAME_INCOMING,
                OverwriteMode::RenameExisting => sys::M7ARC_OVERWRITE_RENAME_EXISTING,
            },
            eliminate_duplicate_root: options.eliminate_duplicate_root,
            create_archive_folder: options.create_archive_folder,
            restore_mac_metadata: options.restore_mac_metadata,
            current_directory: ptr_of(&current),
        };
        let mut out = ptr::null_mut();
        let status = callbacks.with_raw(|cb| unsafe {
            sys::m7arc_extract(self.handle(), archive.as_ptr(), destination.as_ptr(), slice_ptr(indices), indices.len(), &raw, cb, &mut out)
        });
        operation_outcome(status, ResultHandle(out))
    }

    /// Checks the integrity of the selected entries (all when `indices` is empty).
    pub fn test(&self, archive: impl AsRef<Path>, indices: &[u32], options: &OpenOptions, callbacks: &mut Callbacks<'_>) -> Result<OperationReport> {
        let archive = path_cstring(archive.as_ref())?;
        let open = raw_open(options)?;
        let mut out = ptr::null_mut();
        let status = callbacks.with_raw(|cb| unsafe {
            sys::m7arc_test(self.handle(), archive.as_ptr(), slice_ptr(indices), indices.len(), &open.raw, cb, &mut out)
        });
        operation_outcome(status, ResultHandle(out))
    }

    /// Creates (or updates) `destination` from files and folders given as absolute paths.
    pub fn create<P: AsRef<Path>>(
        &self,
        inputs: &[P],
        destination: impl AsRef<Path>,
        options: &CreateOptions,
        callbacks: &mut Callbacks<'_>,
    ) -> Result<OperationReport> {
        let inputs = CStrings::from_paths(inputs)?;
        let destination = path_cstring(destination.as_ref())?;
        let format = cstring(&options.format)?;
        let method = optional(&options.method)?;
        let solid_block = optional(&options.solid_block)?;
        let password = optional(&options.password)?;
        let encryption = optional(&options.encryption_method)?;
        let prefix = optional(&options.archive_prefix)?;
        let include = CStrings::new(&options.include_patterns)?;
        let exclude = CStrings::new(&options.exclude_patterns)?;
        let names = options.properties.iter().map(|(n, _)| cstring(n)).collect::<Result<Vec<_>>>()?;
        let values = options.properties.iter().map(|(_, v)| cstring(v)).collect::<Result<Vec<_>>>()?;
        let pairs: Vec<sys::m7arc_string_pair> =
            names.iter().zip(&values).map(|(n, v)| sys::m7arc_string_pair { first: n.as_ptr(), second: v.as_ptr() }).collect();
        let raw = sys::m7arc_create_options {
            format: format.as_ptr(),
            level: options.level,
            method: ptr_of(&method),
            dictionary_bytes: options.dictionary_bytes,
            word_size: options.word_size,
            threads: options.threads,
            solid: options.solid,
            solid_block: ptr_of(&solid_block),
            password: ptr_of(&password),
            encrypt_headers: options.encrypt_headers,
            encryption_method: ptr_of(&encryption),
            volume_sizes: slice_ptr(&options.volume_sizes),
            volume_size_count: options.volume_sizes.len(),
            update_mode: match options.update_mode {
                UpdateMode::Add => sys::M7ARC_UPDATE_ADD,
                UpdateMode::Update => sys::M7ARC_UPDATE_UPDATE,
                UpdateMode::Freshen => sys::M7ARC_UPDATE_FRESHEN,
                UpdateMode::Synchronize => sys::M7ARC_UPDATE_SYNCHRONIZE,
            },
            store_symbolic_links: options.store_symbolic_links,
            store_hard_links: options.store_hard_links,
            delete_after_compressing: options.delete_after_compressing,
            preserve_access_time: options.preserve_access_time,
            preserve_mac_metadata: options.preserve_mac_metadata,
            archive_prefix: ptr_of(&prefix),
            open_shared_files: options.open_shared_files,
            set_archive_mtime: options.set_archive_mtime,
            store_owner_id: options.store_owner_id,
            store_owner_name: options.store_owner_name,
            path_mode: match options.path_mode {
                ArchivePathMode::Relative => sys::M7ARC_ARCHIVE_PATHS_RELATIVE,
                ArchivePathMode::Full => sys::M7ARC_ARCHIVE_PATHS_FULL,
                ArchivePathMode::Absolute => sys::M7ARC_ARCHIVE_PATHS_ABSOLUTE,
            },
            include_patterns: include.ptr(),
            include_pattern_count: include.len(),
            exclude_patterns: exclude.ptr(),
            exclude_pattern_count: exclude.len(),
            properties: slice_ptr(&pairs),
            property_count: pairs.len(),
        };
        let mut out = ptr::null_mut();
        let status = callbacks.with_raw(|cb| unsafe {
            sys::m7arc_create(self.handle(), inputs.ptr(), inputs.len(), destination.as_ptr(), &raw, cb, &mut out)
        });
        operation_outcome(status, ResultHandle(out))
    }

    /// Deletes the given entries (and the contents of deleted folders) from the archive.
    pub fn remove(&self, archive: impl AsRef<Path>, indices: &[u32], options: &OpenOptions, callbacks: &mut Callbacks<'_>) -> Result<OperationReport> {
        let archive = path_cstring(archive.as_ref())?;
        let open = raw_open(options)?;
        let mut out = ptr::null_mut();
        let status = callbacks.with_raw(|cb| unsafe {
            sys::m7arc_remove(self.handle(), archive.as_ptr(), slice_ptr(indices), indices.len(), &open.raw, cb, &mut out)
        });
        operation_outcome(status, ResultHandle(out))
    }

    pub fn rename(&self, archive: impl AsRef<Path>, index: u32, new_path: &str, options: &OpenOptions, callbacks: &mut Callbacks<'_>) -> Result<OperationReport> {
        let archive = path_cstring(archive.as_ref())?;
        let new_path = cstring(new_path)?;
        let open = raw_open(options)?;
        let mut out = ptr::null_mut();
        let status = callbacks
            .with_raw(|cb| unsafe { sys::m7arc_rename(self.handle(), archive.as_ptr(), index, new_path.as_ptr(), &open.raw, cb, &mut out) });
        operation_outcome(status, ResultHandle(out))
    }

    pub fn rename_directory(
        &self,
        archive: impl AsRef<Path>,
        old_prefix: &str,
        new_prefix: &str,
        options: &OpenOptions,
        callbacks: &mut Callbacks<'_>,
    ) -> Result<OperationReport> {
        let archive = path_cstring(archive.as_ref())?;
        let old_prefix = cstring(old_prefix)?;
        let new_prefix = cstring(new_prefix)?;
        let open = raw_open(options)?;
        let mut out = ptr::null_mut();
        let status = callbacks.with_raw(|cb| unsafe {
            sys::m7arc_rename_directory(self.handle(), archive.as_ptr(), old_prefix.as_ptr(), new_prefix.as_ptr(), &open.raw, cb, &mut out)
        });
        operation_outcome(status, ResultHandle(out))
    }

    /// Replaces the content of an entry with a local file.
    pub fn replace(
        &self,
        archive: impl AsRef<Path>,
        index: u32,
        local_file: impl AsRef<Path>,
        options: &OpenOptions,
        callbacks: &mut Callbacks<'_>,
    ) -> Result<OperationReport> {
        let archive = path_cstring(archive.as_ref())?;
        let local = path_cstring(local_file.as_ref())?;
        let open = raw_open(options)?;
        let mut out = ptr::null_mut();
        let status = callbacks
            .with_raw(|cb| unsafe { sys::m7arc_replace(self.handle(), archive.as_ptr(), index, local.as_ptr(), &open.raw, cb, &mut out) });
        operation_outcome(status, ResultHandle(out))
    }

    pub fn set_comment(&self, archive: impl AsRef<Path>, index: u32, comment: &str, options: &OpenOptions, callbacks: &mut Callbacks<'_>) -> Result<OperationReport> {
        let archive = path_cstring(archive.as_ref())?;
        let comment = cstring(comment)?;
        let open = raw_open(options)?;
        let mut out = ptr::null_mut();
        let status = callbacks
            .with_raw(|cb| unsafe { sys::m7arc_set_comment(self.handle(), archive.as_ptr(), index, comment.as_ptr(), &open.raw, cb, &mut out) });
        operation_outcome(status, ResultHandle(out))
    }

    /// Per-file digests for files and folders with the named algorithms (for example `"SHA256"`).
    pub fn hash<P: AsRef<Path>>(&self, inputs: &[P], algorithms: &[&str], callbacks: &mut Callbacks<'_>) -> Result<Vec<FileHash>> {
        self.hash_report(inputs, algorithms, callbacks, false).map(|summary| summary.files)
    }

    /// Like [`hash`](Self::hash), plus data/name totals and file counts.
    pub fn hash_summary<P: AsRef<Path>>(&self, inputs: &[P], algorithms: &[&str], callbacks: &mut Callbacks<'_>) -> Result<HashSummary> {
        self.hash_report(inputs, algorithms, callbacks, true)
    }

    fn hash_report<P: AsRef<Path>>(&self, inputs: &[P], algorithms: &[&str], callbacks: &mut Callbacks<'_>, summary: bool) -> Result<HashSummary> {
        let inputs = CStrings::from_paths(inputs)?;
        let algorithms = CStrings::new(algorithms)?;
        let mut out = ptr::null_mut();
        let status = callbacks.with_raw(|cb| unsafe {
            let call = if summary { sys::m7arc_hash_summary } else { sys::m7arc_hash };
            call(self.handle(), inputs.ptr(), inputs.len(), algorithms.ptr(), algorithms.len(), cb, &mut out)
        });
        let report = HashHandle(out);
        status_only(status)?;
        let r = report.0;
        unsafe {
            let mut result = HashSummary {
                file_count: sys::m7arc_hash_report_files_hashed(r),
                directory_count: sys::m7arc_hash_report_directories(r),
                total_bytes: sys::m7arc_hash_report_total_bytes(r),
                ..HashSummary::default()
            };
            for i in 0..sys::m7arc_hash_report_file_count(r) {
                let mut info = std::mem::zeroed::<sys::m7arc_hash_info>();
                if sys::m7arc_hash_report_file(r, i, &mut info) {
                    result.files.push(FileHash { path: text(info.path), algorithm: text(info.algorithm), digest: text(info.digest), size: info.size });
                }
            }
            for i in 0..sys::m7arc_hash_report_total_count(r) {
                let mut info = std::mem::zeroed::<sys::m7arc_hash_total_info>();
                if sys::m7arc_hash_report_total(r, i, &mut info) {
                    result.totals.push(HashTotal {
                        algorithm: text(info.algorithm),
                        data_sum: text(info.data_sum),
                        data_and_names_sum: text(info.data_and_names_sum),
                    });
                }
            }
            Ok(result)
        }
    }

    /// Checks the files listed in a checksum manifest (`.sha256`, `.md5`, ...). Mismatches
    /// and missing files are reported in the returned [`ManifestReport`], not as an error.
    pub fn verify_manifest(&self, manifest: impl AsRef<Path>, callbacks: &mut Callbacks<'_>) -> Result<ManifestReport> {
        let manifest = path_cstring(manifest.as_ref())?;
        let mut out = ptr::null_mut();
        let status = callbacks.with_raw(|cb| unsafe { sys::m7arc_verify_manifest(self.handle(), manifest.as_ptr(), cb, &mut out) });
        let handle = ManifestHandle(out);
        if status != sys::M7ARC_OK && status != sys::M7ARC_ERR_OPERATION_FAILED {
            return Err(Error::from_status(status, last_error()));
        }
        let m = handle.0;
        let mut report = ManifestReport::default();
        unsafe {
            for i in 0..sys::m7arc_manifest_entry_count(m) {
                let mut entry = std::mem::zeroed::<sys::m7arc_manifest_entry>();
                if sys::m7arc_manifest_entry_at(m, i, &mut entry) {
                    report.entries.push(ManifestEntry {
                        path: text(entry.path),
                        status: match entry.status {
                            sys::M7ARC_VERIFY_PASSED => VerifyStatus::Passed,
                            sys::M7ARC_VERIFY_MISSING => VerifyStatus::Missing,
                            sys::M7ARC_VERIFY_MISMATCH => VerifyStatus::Mismatch,
                            _ => VerifyStatus::Error,
                        },
                        message: text(entry.message),
                    });
                }
            }
            report.errors = (0..sys::m7arc_manifest_error_count(m)).map(|i| text(sys::m7arc_manifest_error(m, i))).collect();
        }
        Ok(report)
    }
}

fn read_snapshot(handle: &SnapshotHandle) -> Archive {
    let s = handle.0;
    unsafe {
        let property = |read: &dyn Fn(&mut sys::m7arc_property_info) -> bool| {
            let mut info = std::mem::zeroed::<sys::m7arc_property_info>();
            read(&mut info).then(|| Property { id: info.id, name: text(info.name), value: text(info.value) })
        };
        let entries = (0..sys::m7arc_snapshot_entry_count(s))
            .filter_map(|i| {
                let mut info = std::mem::zeroed::<sys::m7arc_entry_info>();
                if !sys::m7arc_snapshot_entry(s, i, &mut info) {
                    return None;
                }
                Some(Entry {
                    index: info.index,
                    path: text(info.path),
                    size: info.size,
                    packed_size: info.packed_size,
                    is_directory: info.is_directory,
                    encrypted: info.encrypted,
                    alternate_stream: info.alternate_stream,
                    modified_unix_seconds: info.modified_unix_seconds,
                    method: text(info.method),
                    attributes: text(info.attributes),
                    properties: (0..sys::m7arc_snapshot_entry_property_count(s, i))
                        .filter_map(|j| property(&|out| sys::m7arc_snapshot_entry_property(s, i, j, out)))
                        .collect(),
                })
            })
            .collect();
        Archive {
            path: text(sys::m7arc_snapshot_path(s)),
            format: text(sys::m7arc_snapshot_format(s)),
            can_update: sys::m7arc_snapshot_can_update(s),
            can_set_comment: sys::m7arc_snapshot_can_set_comment(s),
            entries,
            properties: (0..sys::m7arc_snapshot_property_count(s))
                .filter_map(|j| property(&|out| sys::m7arc_snapshot_property(s, j, out)))
                .collect(),
            volumes: (0..sys::m7arc_snapshot_volume_count(s)).map(|i| text(sys::m7arc_snapshot_volume(s, i))).collect(),
        }
    }
}
