//! Raw bindings to `src/engine/capi/m7arc_c.h`. See that header for the
//! ownership, threading and error rules; this crate adds nothing on top of it.
#![allow(non_camel_case_types)]

use std::os::raw::c_char;

pub type m7arc_status = i32;
pub const M7ARC_OK: i32 = 0;
pub const M7ARC_ERR_GENERAL: i32 = 1;
pub const M7ARC_ERR_CANCELLED: i32 = 2;
pub const M7ARC_ERR_PASSWORD_REQUIRED: i32 = 3;
pub const M7ARC_ERR_PASSWORD_OR_CORRUPTION: i32 = 4;
pub const M7ARC_ERR_INVALID_ARGUMENT: i32 = 5;
pub const M7ARC_ERR_OPERATION_FAILED: i32 = 6;
pub const M7ARC_ERR_INTERNAL: i32 = 7;

pub const M7ARC_STAGE_NONE: i32 = 0;
pub const M7ARC_STAGE_SCANNING: i32 = 1;
pub const M7ARC_STAGE_OPENING: i32 = 2;
pub const M7ARC_STAGE_COMPRESSING: i32 = 3;
pub const M7ARC_STAGE_COMMITTING: i32 = 4;
pub const M7ARC_STAGE_TESTING: i32 = 5;
pub const M7ARC_STAGE_EXTRACTING: i32 = 6;
pub const M7ARC_STAGE_RESTORING_MAC_METADATA: i32 = 7;
pub const M7ARC_STAGE_PRESERVING_MAC_METADATA: i32 = 8;
pub const M7ARC_STAGE_UPDATING: i32 = 9;
pub const M7ARC_STAGE_HASHING: i32 = 10;
pub const M7ARC_STAGE_BENCHMARK: i32 = 11;
pub const M7ARC_STAGE_BENCHMARK_COMPRESSING: i32 = 12;
pub const M7ARC_STAGE_BENCHMARK_DECOMPRESSING: i32 = 13;

pub const M7ARC_EXTRACT_PATHS_FULL: i32 = 0;
pub const M7ARC_EXTRACT_PATHS_FLAT: i32 = 1;
pub const M7ARC_EXTRACT_PATHS_CURRENT: i32 = 2;
pub const M7ARC_EXTRACT_PATHS_ABSOLUTE: i32 = 3;
pub const M7ARC_ARCHIVE_PATHS_RELATIVE: i32 = 0;
pub const M7ARC_ARCHIVE_PATHS_FULL: i32 = 1;
pub const M7ARC_ARCHIVE_PATHS_ABSOLUTE: i32 = 2;

pub const M7ARC_OVERWRITE_ASK: i32 = 0;
pub const M7ARC_OVERWRITE_REPLACE: i32 = 1;
pub const M7ARC_OVERWRITE_SKIP: i32 = 2;
pub const M7ARC_OVERWRITE_RENAME_INCOMING: i32 = 3;
pub const M7ARC_OVERWRITE_RENAME_EXISTING: i32 = 4;

pub const M7ARC_ANSWER_REPLACE: i32 = 0;
pub const M7ARC_ANSWER_REPLACE_ALL: i32 = 1;
pub const M7ARC_ANSWER_SKIP: i32 = 2;
pub const M7ARC_ANSWER_SKIP_ALL: i32 = 3;
pub const M7ARC_ANSWER_RENAME_INCOMING: i32 = 4;
pub const M7ARC_ANSWER_CANCEL: i32 = 5;

pub const M7ARC_UPDATE_ADD: i32 = 0;
pub const M7ARC_UPDATE_UPDATE: i32 = 1;
pub const M7ARC_UPDATE_FRESHEN: i32 = 2;
pub const M7ARC_UPDATE_SYNCHRONIZE: i32 = 3;

pub const M7ARC_VERIFY_PASSED: i32 = 0;
pub const M7ARC_VERIFY_MISSING: i32 = 1;
pub const M7ARC_VERIFY_MISMATCH: i32 = 2;
pub const M7ARC_VERIFY_ERROR: i32 = 3;

pub const M7ARC_MEMORY_CONTINUE: i32 = 0;
pub const M7ARC_MEMORY_CANCEL: i32 = 1;

macro_rules! opaque {
    ($($name:ident),*) => {$(
        #[repr(C)]
        pub struct $name {
            _private: [u8; 0],
            _pin: std::marker::PhantomData<(*mut u8, std::marker::PhantomPinned)>,
        }
    )*};
}
opaque!(
    m7arc_engine,
    m7arc_snapshot,
    m7arc_result,
    m7arc_format_list,
    m7arc_hasher_list,
    m7arc_hash_report,
    m7arc_manifest,
    m7arc_cancel_token
);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct m7arc_string_pair {
    pub first: *const c_char,
    pub second: *const c_char,
}

#[repr(C)]
pub struct m7arc_progress_info {
    pub stage: i32,
    pub phase: *const c_char,
    pub path: *const c_char,
    pub completed: u64,
    pub total: u64,
    pub total_known: bool,
}

#[repr(C)]
pub struct m7arc_overwrite_request {
    pub existing_path: *const c_char,
    pub incoming_path: *const c_char,
    pub has_existing_size: bool,
    pub existing_size: u64,
    pub has_incoming_size: bool,
    pub incoming_size: u64,
}

#[repr(C)]
pub struct m7arc_memory_request {
    pub path: *const c_char,
    pub required_bytes: u64,
    pub allowed_bytes: u64,
    pub report_only: bool,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct m7arc_callbacks {
    pub user_data: *mut std::ffi::c_void,
    pub progress: Option<unsafe extern "C" fn(*const m7arc_progress_info, *mut std::ffi::c_void) -> bool>,
    pub cancelled: Option<unsafe extern "C" fn(*mut std::ffi::c_void) -> bool>,
    pub password: Option<unsafe extern "C" fn(*mut std::ffi::c_void) -> *const c_char>,
    pub overwrite:
        Option<unsafe extern "C" fn(*const m7arc_overwrite_request, *mut std::ffi::c_void) -> i32>,
    pub memory_request:
        Option<unsafe extern "C" fn(*const m7arc_memory_request, *mut std::ffi::c_void) -> i32>,
    pub cancel_token: *const m7arc_cancel_token,
    pub password_overrides: *const m7arc_string_pair,
    pub password_override_count: usize,
}

#[repr(C)]
pub struct m7arc_engine_options {
    pub load_user_modules: bool,
    pub additional_module_directories: *const *const c_char,
    pub additional_module_directory_count: usize,
}

#[repr(C)]
pub struct m7arc_open_options {
    pub password: *const c_char,
    pub type_: *const c_char,
}

#[repr(C)]
pub struct m7arc_extract_options {
    pub password: *const c_char,
    pub path_mode: i32,
    pub overwrite: i32,
    pub eliminate_duplicate_root: bool,
    pub create_archive_folder: bool,
    pub restore_mac_metadata: bool,
    pub current_directory: *const c_char,
}

#[repr(C)]
pub struct m7arc_create_options {
    pub format: *const c_char,
    pub level: u32,
    pub method: *const c_char,
    pub dictionary_bytes: u64,
    pub word_size: u32,
    pub threads: u32,
    pub solid: bool,
    pub solid_block: *const c_char,
    pub password: *const c_char,
    pub encrypt_headers: bool,
    pub encryption_method: *const c_char,
    pub volume_sizes: *const u64,
    pub volume_size_count: usize,
    pub update_mode: i32,
    pub store_symbolic_links: bool,
    pub store_hard_links: bool,
    pub delete_after_compressing: bool,
    pub preserve_access_time: bool,
    pub preserve_mac_metadata: bool,
    pub archive_prefix: *const c_char,
    pub open_shared_files: bool,
    pub set_archive_mtime: bool,
    pub store_owner_id: bool,
    pub store_owner_name: bool,
    pub path_mode: i32,
    pub include_patterns: *const *const c_char,
    pub include_pattern_count: usize,
    pub exclude_patterns: *const *const c_char,
    pub exclude_pattern_count: usize,
    pub properties: *const m7arc_string_pair,
    pub property_count: usize,
}

#[repr(C)]
pub struct m7arc_entry_info {
    pub index: u32,
    pub path: *const c_char,
    pub size: u64,
    pub packed_size: u64,
    pub is_directory: bool,
    pub encrypted: bool,
    pub alternate_stream: bool,
    pub modified_unix_seconds: i64,
    pub method: *const c_char,
    pub attributes: *const c_char,
}

#[repr(C)]
pub struct m7arc_property_info {
    pub id: u32,
    pub name: *const c_char,
    pub value: *const c_char,
}

#[repr(C)]
pub struct m7arc_hash_info {
    pub path: *const c_char,
    pub algorithm: *const c_char,
    pub digest: *const c_char,
    pub size: u64,
}

#[repr(C)]
pub struct m7arc_hash_total_info {
    pub algorithm: *const c_char,
    pub data_sum: *const c_char,
    pub data_and_names_sum: *const c_char,
}

#[repr(C)]
pub struct m7arc_manifest_entry {
    pub path: *const c_char,
    pub status: i32,
    pub message: *const c_char,
}

type Str = *const c_char;
type StrList = *const *const c_char;

extern "C" {
    pub fn m7arc_version() -> Str;
    pub fn m7arc_last_error() -> Str;

    pub fn m7arc_cancel_token_new() -> *mut m7arc_cancel_token;
    pub fn m7arc_cancel_token_cancel(token: *mut m7arc_cancel_token);
    pub fn m7arc_cancel_token_free(token: *mut m7arc_cancel_token);

    pub fn m7arc_engine_options_init(options: *mut m7arc_engine_options);
    pub fn m7arc_open_options_init(options: *mut m7arc_open_options);
    pub fn m7arc_extract_options_init(options: *mut m7arc_extract_options);
    pub fn m7arc_create_options_init(options: *mut m7arc_create_options);
    pub fn m7arc_callbacks_init(callbacks: *mut m7arc_callbacks);

    pub fn m7arc_engine_new(options: *const m7arc_engine_options, out: *mut *mut m7arc_engine) -> m7arc_status;
    pub fn m7arc_engine_free(engine: *mut m7arc_engine);

    pub fn m7arc_formats(engine: *mut m7arc_engine, out: *mut *mut m7arc_format_list) -> m7arc_status;
    pub fn m7arc_format_list_count(list: *const m7arc_format_list) -> usize;
    pub fn m7arc_format_list_name(list: *const m7arc_format_list, index: usize) -> Str;
    pub fn m7arc_format_list_extension_count(list: *const m7arc_format_list, index: usize) -> usize;
    pub fn m7arc_format_list_extension(list: *const m7arc_format_list, index: usize, extension: usize) -> Str;
    pub fn m7arc_format_list_can_read(list: *const m7arc_format_list, index: usize) -> bool;
    pub fn m7arc_format_list_can_write(list: *const m7arc_format_list, index: usize) -> bool;
    pub fn m7arc_format_list_is_hash(list: *const m7arc_format_list, index: usize) -> bool;
    pub fn m7arc_format_list_free(list: *mut m7arc_format_list);

    pub fn m7arc_hashers(engine: *mut m7arc_engine, out: *mut *mut m7arc_hasher_list) -> m7arc_status;
    pub fn m7arc_hasher_list_count(list: *const m7arc_hasher_list) -> usize;
    pub fn m7arc_hasher_list_name(list: *const m7arc_hasher_list, index: usize) -> Str;
    pub fn m7arc_hasher_list_digest_size(list: *const m7arc_hasher_list, index: usize) -> u32;
    pub fn m7arc_hasher_list_free(list: *mut m7arc_hasher_list);

    pub fn m7arc_list(
        engine: *mut m7arc_engine,
        archive: Str,
        opts: *const m7arc_open_options,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_snapshot,
    ) -> m7arc_status;
    pub fn m7arc_snapshot_path(snapshot: *const m7arc_snapshot) -> Str;
    pub fn m7arc_snapshot_format(snapshot: *const m7arc_snapshot) -> Str;
    pub fn m7arc_snapshot_can_update(snapshot: *const m7arc_snapshot) -> bool;
    pub fn m7arc_snapshot_can_set_comment(snapshot: *const m7arc_snapshot) -> bool;
    pub fn m7arc_snapshot_entry_count(snapshot: *const m7arc_snapshot) -> usize;
    pub fn m7arc_snapshot_entry(snapshot: *const m7arc_snapshot, index: usize, out: *mut m7arc_entry_info) -> bool;
    pub fn m7arc_snapshot_entry_property_count(snapshot: *const m7arc_snapshot, entry: usize) -> usize;
    pub fn m7arc_snapshot_entry_property(
        snapshot: *const m7arc_snapshot,
        entry: usize,
        property: usize,
        out: *mut m7arc_property_info,
    ) -> bool;
    pub fn m7arc_snapshot_property_count(snapshot: *const m7arc_snapshot) -> usize;
    pub fn m7arc_snapshot_property(snapshot: *const m7arc_snapshot, property: usize, out: *mut m7arc_property_info)
        -> bool;
    pub fn m7arc_snapshot_volume_count(snapshot: *const m7arc_snapshot) -> usize;
    pub fn m7arc_snapshot_volume(snapshot: *const m7arc_snapshot, index: usize) -> Str;
    pub fn m7arc_snapshot_free(snapshot: *mut m7arc_snapshot);

    pub fn m7arc_extract(
        engine: *mut m7arc_engine,
        archive: Str,
        destination: Str,
        indices: *const u32,
        index_count: usize,
        opts: *const m7arc_extract_options,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_result,
    ) -> m7arc_status;
    pub fn m7arc_test(
        engine: *mut m7arc_engine,
        archive: Str,
        indices: *const u32,
        index_count: usize,
        opts: *const m7arc_open_options,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_result,
    ) -> m7arc_status;
    pub fn m7arc_create(
        engine: *mut m7arc_engine,
        inputs: StrList,
        input_count: usize,
        destination: Str,
        opts: *const m7arc_create_options,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_result,
    ) -> m7arc_status;
    pub fn m7arc_remove(
        engine: *mut m7arc_engine,
        archive: Str,
        indices: *const u32,
        index_count: usize,
        opts: *const m7arc_open_options,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_result,
    ) -> m7arc_status;
    pub fn m7arc_rename(
        engine: *mut m7arc_engine,
        archive: Str,
        index: u32,
        new_path: Str,
        opts: *const m7arc_open_options,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_result,
    ) -> m7arc_status;
    pub fn m7arc_rename_directory(
        engine: *mut m7arc_engine,
        archive: Str,
        old_prefix: Str,
        new_prefix: Str,
        opts: *const m7arc_open_options,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_result,
    ) -> m7arc_status;
    pub fn m7arc_replace(
        engine: *mut m7arc_engine,
        archive: Str,
        index: u32,
        edited_local_file: Str,
        opts: *const m7arc_open_options,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_result,
    ) -> m7arc_status;
    pub fn m7arc_set_comment(
        engine: *mut m7arc_engine,
        archive: Str,
        index: u32,
        comment: Str,
        opts: *const m7arc_open_options,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_result,
    ) -> m7arc_status;

    pub fn m7arc_result_cancelled(result: *const m7arc_result) -> bool;
    pub fn m7arc_result_password_may_be_incorrect(result: *const m7arc_result) -> bool;
    pub fn m7arc_result_completed_items(result: *const m7arc_result) -> u64;
    pub fn m7arc_result_error_count(result: *const m7arc_result) -> usize;
    pub fn m7arc_result_error(result: *const m7arc_result, index: usize) -> Str;
    pub fn m7arc_result_output_path_count(result: *const m7arc_result) -> usize;
    pub fn m7arc_result_output_path(result: *const m7arc_result, index: usize) -> Str;
    pub fn m7arc_result_free(result: *mut m7arc_result);

    pub fn m7arc_hash(
        engine: *mut m7arc_engine,
        inputs: StrList,
        input_count: usize,
        algorithms: StrList,
        algorithm_count: usize,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_hash_report,
    ) -> m7arc_status;
    pub fn m7arc_hash_summary(
        engine: *mut m7arc_engine,
        inputs: StrList,
        input_count: usize,
        algorithms: StrList,
        algorithm_count: usize,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_hash_report,
    ) -> m7arc_status;
    pub fn m7arc_hash_report_file_count(report: *const m7arc_hash_report) -> usize;
    pub fn m7arc_hash_report_file(report: *const m7arc_hash_report, index: usize, out: *mut m7arc_hash_info) -> bool;
    pub fn m7arc_hash_report_total_count(report: *const m7arc_hash_report) -> usize;
    pub fn m7arc_hash_report_total(
        report: *const m7arc_hash_report,
        index: usize,
        out: *mut m7arc_hash_total_info,
    ) -> bool;
    pub fn m7arc_hash_report_files_hashed(report: *const m7arc_hash_report) -> u64;
    pub fn m7arc_hash_report_directories(report: *const m7arc_hash_report) -> u64;
    pub fn m7arc_hash_report_total_bytes(report: *const m7arc_hash_report) -> u64;
    pub fn m7arc_hash_report_free(report: *mut m7arc_hash_report);

    pub fn m7arc_verify_manifest(
        engine: *mut m7arc_engine,
        manifest_path: Str,
        callbacks: *const m7arc_callbacks,
        out: *mut *mut m7arc_manifest,
    ) -> m7arc_status;
    pub fn m7arc_manifest_entry_count(manifest: *const m7arc_manifest) -> usize;
    pub fn m7arc_manifest_entry_at(manifest: *const m7arc_manifest, index: usize, out: *mut m7arc_manifest_entry)
        -> bool;
    pub fn m7arc_manifest_cancelled(manifest: *const m7arc_manifest) -> bool;
    pub fn m7arc_manifest_error_count(manifest: *const m7arc_manifest) -> usize;
    pub fn m7arc_manifest_error(manifest: *const m7arc_manifest, index: usize) -> Str;
    pub fn m7arc_manifest_free(manifest: *mut m7arc_manifest);
}
