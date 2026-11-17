/*
 * M7Arc engine C interface.
 *
 * A stable C view of m7arc::ArchiveEngine, implemented inside the m7arc_engine
 * shared library. Only C types cross this boundary; C++ exceptions never do.
 *
 * Conventions
 * -----------
 * Text: every const char* is NUL-terminated UTF-8. Input strings are copied
 *   before a call returns unless stated otherwise. NULL for an optional input
 *   string means "empty / default". Paths that name an archive, destination or
 *   input are required and must not be NULL.
 *
 * Handles: types declared without a body are opaque. A handle returned through
 *   an out parameter is owned by the caller and must be released with its
 *   matching *_free function; *_free accepts NULL. Out parameters are set to
 *   NULL when a call fails before producing a handle.
 *
 * Returned strings and arrays: every const char* or array obtained from an
 *   accessor on a handle (snapshot, result, list, report, manifest) points
 *   into that handle and stays valid until the handle is freed. m7arc_version
 *   and m7arc_last_error return storage owned by the library (see each
 *   function). Nothing returned by this API is released with free().
 *
 * Input arrays: arrays of strings, indices and option structs are borrowed for
 *   the duration of the call only.
 *
 * Errors: functions return an m7arc_status. M7ARC_OK means success. On any
 *   other status m7arc_last_error() returns a message describing the failure
 *   for the calling thread; it stays valid until the next m7arc_* call made on
 *   the same thread. Operations that run to completion (extract, test, create,
 *   remove, rename, replace, set comment, verify) return their per-member
 *   outcome through a result handle: M7ARC_ERR_CANCELLED or
 *   M7ARC_ERR_OPERATION_FAILED are returned together with a valid handle that
 *   lists the errors.
 *
 * Threads: an m7arc_engine belongs to one serial worker; do not use one engine
 *   from two threads at once. Different engines may run on different threads.
 *   Callbacks run on the calling thread or on an internal codec thread and
 *   must not call back into the same engine. m7arc_cancel_token_cancel may be
 *   called from any thread.
 *
 * Indices: entry indices come from m7arc_snapshot_entry and identify members
 *   of that listing. Passing count 0 with a NULL index array selects every
 *   entry where the operation allows it.
 */
#ifndef M7ARC_C_H
#define M7ARC_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define M7ARC_API __attribute__((visibility("default")))
#else
#define M7ARC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t m7arc_status;
enum {
    M7ARC_OK = 0,
    M7ARC_ERR_GENERAL = 1,
    M7ARC_ERR_CANCELLED = 2,
    M7ARC_ERR_PASSWORD_REQUIRED = 3,
    M7ARC_ERR_PASSWORD_OR_CORRUPTION = 4,
    M7ARC_ERR_INVALID_ARGUMENT = 5,
    /* The operation ran but reported member errors; inspect the result handle. */
    M7ARC_ERR_OPERATION_FAILED = 6,
    M7ARC_ERR_INTERNAL = 7
};

/* Progress stages, matching m7arc::Stage. */
enum {
    M7ARC_STAGE_NONE = 0,
    M7ARC_STAGE_SCANNING,
    M7ARC_STAGE_OPENING,
    M7ARC_STAGE_COMPRESSING,
    M7ARC_STAGE_COMMITTING,
    M7ARC_STAGE_TESTING,
    M7ARC_STAGE_EXTRACTING,
    M7ARC_STAGE_RESTORING_MAC_METADATA,
    M7ARC_STAGE_PRESERVING_MAC_METADATA,
    M7ARC_STAGE_UPDATING,
    M7ARC_STAGE_HASHING,
    M7ARC_STAGE_BENCHMARK,
    M7ARC_STAGE_BENCHMARK_COMPRESSING,
    M7ARC_STAGE_BENCHMARK_DECOMPRESSING
};

/* m7arc_extract_options.path_mode and m7arc_create_options.path_mode. */
enum { M7ARC_EXTRACT_PATHS_FULL = 0, M7ARC_EXTRACT_PATHS_FLAT, M7ARC_EXTRACT_PATHS_CURRENT, M7ARC_EXTRACT_PATHS_ABSOLUTE };
enum { M7ARC_ARCHIVE_PATHS_RELATIVE = 0, M7ARC_ARCHIVE_PATHS_FULL, M7ARC_ARCHIVE_PATHS_ABSOLUTE };

/* m7arc_extract_options.overwrite. */
enum {
    M7ARC_OVERWRITE_ASK = 0,
    M7ARC_OVERWRITE_REPLACE,
    M7ARC_OVERWRITE_SKIP,
    M7ARC_OVERWRITE_RENAME_INCOMING,
    M7ARC_OVERWRITE_RENAME_EXISTING
};

/* Answers returned by the overwrite callback. */
enum {
    M7ARC_ANSWER_REPLACE = 0,
    M7ARC_ANSWER_REPLACE_ALL,
    M7ARC_ANSWER_SKIP,
    M7ARC_ANSWER_SKIP_ALL,
    M7ARC_ANSWER_RENAME_INCOMING,
    M7ARC_ANSWER_CANCEL
};

/* m7arc_create_options.update_mode. */
enum { M7ARC_UPDATE_ADD = 0, M7ARC_UPDATE_UPDATE, M7ARC_UPDATE_FRESHEN, M7ARC_UPDATE_SYNCHRONIZE };

/* m7arc_manifest_entry.status. */
enum { M7ARC_VERIFY_PASSED = 0, M7ARC_VERIFY_MISSING, M7ARC_VERIFY_MISMATCH, M7ARC_VERIFY_ERROR };

/* Answers returned by the memory-request callback. */
enum { M7ARC_MEMORY_CONTINUE = 0, M7ARC_MEMORY_CANCEL };

typedef struct m7arc_engine m7arc_engine;
typedef struct m7arc_snapshot m7arc_snapshot;
typedef struct m7arc_result m7arc_result;
typedef struct m7arc_format_list m7arc_format_list;
typedef struct m7arc_hasher_list m7arc_hasher_list;
typedef struct m7arc_hash_report m7arc_hash_report;
typedef struct m7arc_manifest m7arc_manifest;
typedef struct m7arc_cancel_token m7arc_cancel_token;

typedef struct m7arc_string_pair {
    const char* first;
    const char* second;
} m7arc_string_pair;

/* ---- Library ---------------------------------------------------------- */

/* Upstream 7-Zip version text. Static storage, valid for the process lifetime. */
M7ARC_API const char* m7arc_version(void);
/* Message for the last failed call on this thread; "" when there was none. */
M7ARC_API const char* m7arc_last_error(void);

/* ---- Cancellation ------------------------------------------------------ */

M7ARC_API m7arc_cancel_token* m7arc_cancel_token_new(void);
M7ARC_API void m7arc_cancel_token_cancel(m7arc_cancel_token* token); /* any thread */
M7ARC_API void m7arc_cancel_token_free(m7arc_cancel_token* token);

/* ---- Callbacks ---------------------------------------------------------- */

typedef struct m7arc_progress_info {
    int32_t stage; /* M7ARC_STAGE_* */
    const char* phase; /* English display text for the stage */
    const char* path;
    uint64_t completed;
    uint64_t total;
    bool total_known;
} m7arc_progress_info;

typedef struct m7arc_overwrite_request {
    const char* existing_path;
    const char* incoming_path;
    bool has_existing_size;
    uint64_t existing_size;
    bool has_incoming_size;
    uint64_t incoming_size;
} m7arc_overwrite_request;

typedef struct m7arc_memory_request {
    const char* path;
    uint64_t required_bytes;
    uint64_t allowed_bytes;
    bool report_only;
} m7arc_memory_request;

/*
 * Every member is optional. user_data is passed back unchanged. Strings in the
 * request structs are valid only during the callback.
 *
 *   progress        return false to cancel.
 *   cancelled       return true to cancel; polled frequently.
 *   password        return the password, or NULL to cancel. The string is
 *                   copied before the engine continues.
 *   overwrite       return an M7ARC_ANSWER_* value.
 *   memory_request  return an M7ARC_MEMORY_* value.
 *   cancel_token    when set and cancelled, the operation stops like a true
 *                   return from `cancelled`. Borrowed; must outlive the call.
 *   password_overrides / password_override_count
 *                   (archive path, password) pairs tried before asking. Used
 *                   when opening the named archive, including nested opens.
 *
 * Callbacks may run on an internal codec thread; the engine serializes them,
 * so two callbacks of one operation never overlap.
 */
typedef struct m7arc_callbacks {
    void* user_data;
    bool (*progress)(const m7arc_progress_info* info, void* user_data);
    bool (*cancelled)(void* user_data);
    const char* (*password)(void* user_data);
    int32_t (*overwrite)(const m7arc_overwrite_request* request, void* user_data);
    int32_t (*memory_request)(const m7arc_memory_request* request, void* user_data);
    const m7arc_cancel_token* cancel_token;
    const m7arc_string_pair* password_overrides;
    size_t password_override_count;
} m7arc_callbacks;

/* ---- Options ------------------------------------------------------------ */

typedef struct m7arc_engine_options {
    bool load_user_modules;
    const char* const* additional_module_directories;
    size_t additional_module_directory_count;
} m7arc_engine_options;

typedef struct m7arc_open_options {
    const char* password;
    const char* type; /* forced archive type name, NULL to auto-detect */
} m7arc_open_options;

typedef struct m7arc_extract_options {
    const char* password;
    int32_t path_mode; /* M7ARC_EXTRACT_PATHS_* */
    int32_t overwrite; /* M7ARC_OVERWRITE_* */
    bool eliminate_duplicate_root;
    bool create_archive_folder;
    bool restore_mac_metadata;
    const char* current_directory;
} m7arc_extract_options;

typedef struct m7arc_create_options {
    const char* format; /* "7z", "zip", "tar", "wim", "gzip", "bzip2", "xz", "tar.gz", ... */
    uint32_t level;
    const char* method;
    uint64_t dictionary_bytes;
    uint32_t word_size;
    uint32_t threads; /* 0 = automatic */
    bool solid;
    const char* solid_block;
    const char* password;
    bool encrypt_headers;
    const char* encryption_method;
    const uint64_t* volume_sizes;
    size_t volume_size_count;
    int32_t update_mode; /* M7ARC_UPDATE_* */
    bool store_symbolic_links;
    bool store_hard_links;
    bool delete_after_compressing;
    bool preserve_access_time;
    bool preserve_mac_metadata;
    const char* archive_prefix;
    bool open_shared_files;
    bool set_archive_mtime;
    bool store_owner_id;
    bool store_owner_name;
    int32_t path_mode; /* M7ARC_ARCHIVE_PATHS_* */
    const char* const* include_patterns;
    size_t include_pattern_count;
    const char* const* exclude_patterns;
    size_t exclude_pattern_count;
    const m7arc_string_pair* properties; /* free-form (name, value) codec properties */
    size_t property_count;
} m7arc_create_options;

/* Fill the structs with the engine defaults. */
M7ARC_API void m7arc_engine_options_init(m7arc_engine_options* options);
M7ARC_API void m7arc_open_options_init(m7arc_open_options* options);
M7ARC_API void m7arc_extract_options_init(m7arc_extract_options* options);
M7ARC_API void m7arc_create_options_init(m7arc_create_options* options);
M7ARC_API void m7arc_callbacks_init(m7arc_callbacks* callbacks);

/* ---- Engine ------------------------------------------------------------ */

/* options may be NULL for the defaults. The first engine in a process fixes the
 * module directory set; later engines must use the same set. */
M7ARC_API m7arc_status m7arc_engine_new(const m7arc_engine_options* options, m7arc_engine** out);
M7ARC_API void m7arc_engine_free(m7arc_engine* engine);

M7ARC_API m7arc_status m7arc_formats(m7arc_engine* engine, m7arc_format_list** out);
M7ARC_API size_t m7arc_format_list_count(const m7arc_format_list* list);
M7ARC_API const char* m7arc_format_list_name(const m7arc_format_list* list, size_t index);
M7ARC_API size_t m7arc_format_list_extension_count(const m7arc_format_list* list, size_t index);
M7ARC_API const char* m7arc_format_list_extension(const m7arc_format_list* list, size_t index, size_t extension);
M7ARC_API bool m7arc_format_list_can_read(const m7arc_format_list* list, size_t index);
M7ARC_API bool m7arc_format_list_can_write(const m7arc_format_list* list, size_t index);
M7ARC_API bool m7arc_format_list_is_hash(const m7arc_format_list* list, size_t index);
M7ARC_API void m7arc_format_list_free(m7arc_format_list* list);

M7ARC_API m7arc_status m7arc_hashers(m7arc_engine* engine, m7arc_hasher_list** out);
M7ARC_API size_t m7arc_hasher_list_count(const m7arc_hasher_list* list);
M7ARC_API const char* m7arc_hasher_list_name(const m7arc_hasher_list* list, size_t index);
M7ARC_API uint32_t m7arc_hasher_list_digest_size(const m7arc_hasher_list* list, size_t index);
M7ARC_API void m7arc_hasher_list_free(m7arc_hasher_list* list);

/* ---- Listing ----------------------------------------------------------- */

typedef struct m7arc_entry_info {
    uint32_t index;
    const char* path;
    uint64_t size;
    uint64_t packed_size;
    bool is_directory;
    bool encrypted;
    bool alternate_stream;
    int64_t modified_unix_seconds;
    const char* method;
    const char* attributes;
} m7arc_entry_info;

typedef struct m7arc_property_info {
    uint32_t id;
    const char* name;
    const char* value;
} m7arc_property_info;

/* opts and callbacks may be NULL. */
M7ARC_API m7arc_status m7arc_list(m7arc_engine* engine, const char* archive, const m7arc_open_options* opts,
                                  const m7arc_callbacks* callbacks, m7arc_snapshot** out);
M7ARC_API const char* m7arc_snapshot_path(const m7arc_snapshot* snapshot);
M7ARC_API const char* m7arc_snapshot_format(const m7arc_snapshot* snapshot);
M7ARC_API bool m7arc_snapshot_can_update(const m7arc_snapshot* snapshot);
M7ARC_API bool m7arc_snapshot_can_set_comment(const m7arc_snapshot* snapshot);
M7ARC_API size_t m7arc_snapshot_entry_count(const m7arc_snapshot* snapshot);
/* Returns false when index is out of range. */
M7ARC_API bool m7arc_snapshot_entry(const m7arc_snapshot* snapshot, size_t index, m7arc_entry_info* out);
M7ARC_API size_t m7arc_snapshot_entry_property_count(const m7arc_snapshot* snapshot, size_t entry);
M7ARC_API bool m7arc_snapshot_entry_property(const m7arc_snapshot* snapshot, size_t entry, size_t property,
                                             m7arc_property_info* out);
M7ARC_API size_t m7arc_snapshot_property_count(const m7arc_snapshot* snapshot);
M7ARC_API bool m7arc_snapshot_property(const m7arc_snapshot* snapshot, size_t property, m7arc_property_info* out);
M7ARC_API size_t m7arc_snapshot_volume_count(const m7arc_snapshot* snapshot);
M7ARC_API const char* m7arc_snapshot_volume(const m7arc_snapshot* snapshot, size_t index);
M7ARC_API void m7arc_snapshot_free(m7arc_snapshot* snapshot);

/* ---- Operations -------------------------------------------------------- */

/* out may be NULL when the caller only needs the status. For operations that
 * return a result handle it is set on M7ARC_OK, M7ARC_ERR_CANCELLED and
 * M7ARC_ERR_OPERATION_FAILED. */
M7ARC_API m7arc_status m7arc_extract(m7arc_engine* engine, const char* archive, const char* destination,
                                     const uint32_t* indices, size_t index_count,
                                     const m7arc_extract_options* opts, const m7arc_callbacks* callbacks,
                                     m7arc_result** out);
M7ARC_API m7arc_status m7arc_test(m7arc_engine* engine, const char* archive, const uint32_t* indices,
                                  size_t index_count, const m7arc_open_options* opts,
                                  const m7arc_callbacks* callbacks, m7arc_result** out);
/* inputs are absolute paths. */
M7ARC_API m7arc_status m7arc_create(m7arc_engine* engine, const char* const* inputs, size_t input_count,
                                    const char* destination, const m7arc_create_options* opts,
                                    const m7arc_callbacks* callbacks, m7arc_result** out);
/* indices must be non-empty. */
M7ARC_API m7arc_status m7arc_remove(m7arc_engine* engine, const char* archive, const uint32_t* indices,
                                    size_t index_count, const m7arc_open_options* opts,
                                    const m7arc_callbacks* callbacks, m7arc_result** out);
M7ARC_API m7arc_status m7arc_rename(m7arc_engine* engine, const char* archive, uint32_t index,
                                    const char* new_path, const m7arc_open_options* opts,
                                    const m7arc_callbacks* callbacks, m7arc_result** out);
M7ARC_API m7arc_status m7arc_rename_directory(m7arc_engine* engine, const char* archive, const char* old_prefix,
                                              const char* new_prefix, const m7arc_open_options* opts,
                                              const m7arc_callbacks* callbacks, m7arc_result** out);
M7ARC_API m7arc_status m7arc_replace(m7arc_engine* engine, const char* archive, uint32_t index,
                                     const char* edited_local_file, const m7arc_open_options* opts,
                                     const m7arc_callbacks* callbacks, m7arc_result** out);
M7ARC_API m7arc_status m7arc_set_comment(m7arc_engine* engine, const char* archive, uint32_t index,
                                         const char* comment, const m7arc_open_options* opts,
                                         const m7arc_callbacks* callbacks, m7arc_result** out);

M7ARC_API bool m7arc_result_cancelled(const m7arc_result* result);
M7ARC_API bool m7arc_result_password_may_be_incorrect(const m7arc_result* result);
M7ARC_API uint64_t m7arc_result_completed_items(const m7arc_result* result);
M7ARC_API size_t m7arc_result_error_count(const m7arc_result* result);
M7ARC_API const char* m7arc_result_error(const m7arc_result* result, size_t index);
M7ARC_API size_t m7arc_result_output_path_count(const m7arc_result* result);
M7ARC_API const char* m7arc_result_output_path(const m7arc_result* result, size_t index);
M7ARC_API void m7arc_result_free(m7arc_result* result);

/* ---- Checksums --------------------------------------------------------- */

typedef struct m7arc_hash_info {
    const char* path;
    const char* algorithm;
    const char* digest; /* lowercase or uppercase hex as produced by the engine */
    uint64_t size;
} m7arc_hash_info;

typedef struct m7arc_hash_total_info {
    const char* algorithm;
    const char* data_sum;
    const char* data_and_names_sum;
} m7arc_hash_total_info;

/* Hash files and directory trees with the named algorithms (e.g. "SHA256").
 * m7arc_hash fills only the per-file list; m7arc_hash_summary also fills totals
 * and counts. */
M7ARC_API m7arc_status m7arc_hash(m7arc_engine* engine, const char* const* inputs, size_t input_count,
                                  const char* const* algorithms, size_t algorithm_count,
                                  const m7arc_callbacks* callbacks, m7arc_hash_report** out);
M7ARC_API m7arc_status m7arc_hash_summary(m7arc_engine* engine, const char* const* inputs, size_t input_count,
                                          const char* const* algorithms, size_t algorithm_count,
                                          const m7arc_callbacks* callbacks, m7arc_hash_report** out);
M7ARC_API size_t m7arc_hash_report_file_count(const m7arc_hash_report* report);
M7ARC_API bool m7arc_hash_report_file(const m7arc_hash_report* report, size_t index, m7arc_hash_info* out);
M7ARC_API size_t m7arc_hash_report_total_count(const m7arc_hash_report* report);
M7ARC_API bool m7arc_hash_report_total(const m7arc_hash_report* report, size_t index, m7arc_hash_total_info* out);
M7ARC_API uint64_t m7arc_hash_report_files_hashed(const m7arc_hash_report* report);
M7ARC_API uint64_t m7arc_hash_report_directories(const m7arc_hash_report* report);
M7ARC_API uint64_t m7arc_hash_report_total_bytes(const m7arc_hash_report* report);
M7ARC_API void m7arc_hash_report_free(m7arc_hash_report* report);

typedef struct m7arc_manifest_entry {
    const char* path;
    int32_t status; /* M7ARC_VERIFY_* */
    const char* message;
} m7arc_manifest_entry;

/* Verify the files listed in a checksum manifest. The status is M7ARC_OK only
 * when every entry passed; otherwise M7ARC_ERR_CANCELLED or
 * M7ARC_ERR_OPERATION_FAILED with a valid manifest handle. */
M7ARC_API m7arc_status m7arc_verify_manifest(m7arc_engine* engine, const char* manifest_path,
                                             const m7arc_callbacks* callbacks, m7arc_manifest** out);
M7ARC_API size_t m7arc_manifest_entry_count(const m7arc_manifest* manifest);
M7ARC_API bool m7arc_manifest_entry_at(const m7arc_manifest* manifest, size_t index, m7arc_manifest_entry* out);
M7ARC_API bool m7arc_manifest_cancelled(const m7arc_manifest* manifest);
M7ARC_API size_t m7arc_manifest_error_count(const m7arc_manifest* manifest);
M7ARC_API const char* m7arc_manifest_error(const m7arc_manifest* manifest, size_t index);
M7ARC_API void m7arc_manifest_free(m7arc_manifest* manifest);

#ifdef __cplusplus
}
#endif

#endif
