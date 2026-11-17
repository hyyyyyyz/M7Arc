#include "engine/capi/m7arc_c.h"
#include "engine/ArchiveEngine.h"
#include <atomic>
#include <exception>
#include <new>
#include <string>
#include <vector>

using m7arc::ArchiveEngine;

struct m7arc_engine {
    explicit m7arc_engine(const m7arc::EngineOptions& options) : engine(options) {}
    ArchiveEngine engine;
};
struct m7arc_cancel_token {
    std::atomic<bool> flag{false};
};
struct m7arc_snapshot {
    m7arc::ArchiveSnapshot value;
};
struct m7arc_result {
    m7arc::OperationResult value;
};
struct m7arc_format_list {
    std::vector<m7arc::FormatInfo> formats;
    std::vector<std::vector<const char*>> extensions;
};
struct m7arc_hasher_list {
    std::vector<m7arc::HasherInfo> hashers;
};
struct m7arc_hash_report {
    m7arc::HashSummary value;
};
struct m7arc_manifest {
    m7arc::ManifestResult value;
};

namespace {
thread_local std::string lastError;

const std::string& version() {
    static const std::string value = ArchiveEngine::version();
    return value;
}
std::string text(const char* value) {
    return value ? std::string(value) : std::string();
}
std::vector<std::string> strings(const char* const* values, size_t count) {
    std::vector<std::string> out;
    if (values)
        for (size_t i = 0; i < count; ++i)
            out.push_back(text(values[i]));
    return out;
}
std::vector<std::uint32_t> indexList(const std::uint32_t* indices, size_t count) {
    return indices ? std::vector<std::uint32_t>(indices, indices + count) : std::vector<std::uint32_t>();
}
struct InvalidArgument {
    std::string message;
};
[[noreturn]] void invalid(const char* message) {
    throw InvalidArgument{message};
}
const char* required(const char* value, const char* name) {
    if (!value)
        invalid(name);
    return value;
}
template <class T>
T enumValue(std::int32_t value, std::int32_t last, const char* name) {
    if (value < 0 || value > last)
        invalid(name);
    return static_cast<T>(value);
}

template <class F>
m7arc_status guarded(F&& body) noexcept {
    lastError.clear();
    try {
        return body();
    } catch (const InvalidArgument& e) {
        lastError = "Invalid argument: " + e.message;
        return M7ARC_ERR_INVALID_ARGUMENT;
    } catch (const m7arc::EngineError& e) {
        lastError = e.what();
        switch (e.kind()) {
        case m7arc::EngineErrorKind::Cancelled:
            return M7ARC_ERR_CANCELLED;
        case m7arc::EngineErrorKind::PasswordRequired:
            return M7ARC_ERR_PASSWORD_REQUIRED;
        case m7arc::EngineErrorKind::PasswordOrCorruption:
            return M7ARC_ERR_PASSWORD_OR_CORRUPTION;
        case m7arc::EngineErrorKind::General:
            break;
        }
        return M7ARC_ERR_GENERAL;
    } catch (const std::bad_alloc&) {
        lastError = "Out of memory";
        return M7ARC_ERR_INTERNAL;
    } catch (const std::exception& e) {
        lastError = e.what();
        return M7ARC_ERR_GENERAL;
    } catch (...) {
        lastError = "Unknown internal error";
        return M7ARC_ERR_INTERNAL;
    }
}

m7arc::Callbacks adapt(const m7arc_callbacks* source) {
    m7arc::Callbacks out;
    if (!source)
        return out;
    const m7arc_callbacks c = *source;
    if (c.progress)
        out.progress = [c](const m7arc::Progress& p) {
            const m7arc_progress_info info{static_cast<std::int32_t>(p.stage), p.phase.c_str(),
                                           p.path.c_str(),                     p.completed,
                                           p.total,                            p.totalKnown};
            return c.progress(&info, c.user_data);
        };
    if (c.cancelled || c.cancel_token)
        out.cancelled = [c] {
            return (c.cancel_token && c.cancel_token->flag.load()) ||
                   (c.cancelled && c.cancelled(c.user_data));
        };
    if (c.password)
        out.password = [c]() -> std::optional<std::string> {
            const char* value = c.password(c.user_data);
            if (!value)
                return std::nullopt;
            return std::string(value);
        };
    if (c.overwrite)
        out.overwrite = [c](const m7arc::OverwriteRequest& r) {
            const m7arc_overwrite_request request{r.existingPath.c_str(),
                                                  r.incomingPath.c_str(),
                                                  r.existingSize.has_value(),
                                                  r.existingSize.value_or(0),
                                                  r.incomingSize.has_value(),
                                                  r.incomingSize.value_or(0)};
            const auto answer = c.overwrite(&request, c.user_data);
            if (answer < 0 || answer > M7ARC_ANSWER_CANCEL)
                return m7arc::OverwriteAnswer::Cancel;
            return static_cast<m7arc::OverwriteAnswer>(answer);
        };
    if (c.memory_request)
        out.memoryRequest = [c](const m7arc::MemoryRequest& r) {
            const m7arc_memory_request request{r.path.c_str(), r.requiredBytes, r.allowedBytes, r.reportOnly};
            return c.memory_request(&request, c.user_data) == M7ARC_MEMORY_CONTINUE ? m7arc::MemoryAnswer::Continue
                                                                                    : m7arc::MemoryAnswer::Cancel;
        };
    if (c.password_overrides)
        for (size_t i = 0; i < c.password_override_count; ++i)
            out.passwordOverrides.emplace_back(text(c.password_overrides[i].first),
                                               text(c.password_overrides[i].second));
    return out;
}
m7arc::OpenOptions adapt(const m7arc_open_options* o) {
    m7arc::OpenOptions out;
    if (o) {
        out.password = text(o->password);
        out.type = text(o->type);
    }
    return out;
}
m7arc::ExtractOptions adapt(const m7arc_extract_options* o) {
    m7arc::ExtractOptions out;
    if (o) {
        out.password = text(o->password);
        out.pathMode = enumValue<m7arc::PathMode>(o->path_mode, M7ARC_EXTRACT_PATHS_ABSOLUTE, "path_mode");
        out.overwrite =
            enumValue<m7arc::Overwrite>(o->overwrite, M7ARC_OVERWRITE_RENAME_EXISTING, "overwrite");
        out.eliminateDuplicateRoot = o->eliminate_duplicate_root;
        out.createArchiveFolder = o->create_archive_folder;
        out.restoreMacMetadata = o->restore_mac_metadata;
        out.currentDirectory = text(o->current_directory);
    }
    return out;
}
m7arc::CreateOptions adapt(const m7arc_create_options* o) {
    m7arc::CreateOptions out;
    if (!o)
        return out;
    out.format = text(o->format);
    out.level = o->level;
    out.method = text(o->method);
    out.dictionaryBytes = o->dictionary_bytes;
    out.wordSize = o->word_size;
    out.threads = o->threads;
    out.solid = o->solid;
    out.solidBlock = text(o->solid_block);
    out.password = text(o->password);
    out.encryptHeaders = o->encrypt_headers;
    out.encryptionMethod = text(o->encryption_method);
    if (o->volume_sizes)
        out.volumeSizes.assign(o->volume_sizes, o->volume_sizes + o->volume_size_count);
    out.updateMode = enumValue<m7arc::UpdateMode>(o->update_mode, M7ARC_UPDATE_SYNCHRONIZE, "update_mode");
    out.storeSymbolicLinks = o->store_symbolic_links;
    out.storeHardLinks = o->store_hard_links;
    out.deleteAfterCompressing = o->delete_after_compressing;
    out.preserveAccessTime = o->preserve_access_time;
    out.preserveMacMetadata = o->preserve_mac_metadata;
    out.archivePrefix = text(o->archive_prefix);
    out.openSharedFiles = o->open_shared_files;
    out.setArchiveMTime = o->set_archive_mtime;
    out.storeOwnerId = o->store_owner_id;
    out.storeOwnerName = o->store_owner_name;
    out.pathMode = enumValue<m7arc::ArchivePathMode>(o->path_mode, M7ARC_ARCHIVE_PATHS_ABSOLUTE, "path_mode");
    out.includePatterns = strings(o->include_patterns, o->include_pattern_count);
    out.excludePatterns = strings(o->exclude_patterns, o->exclude_pattern_count);
    if (o->properties)
        for (size_t i = 0; i < o->property_count; ++i)
            out.properties.emplace_back(text(o->properties[i].first), text(o->properties[i].second));
    return out;
}

m7arc_status finish(m7arc::OperationResult&& value, m7arc_result** out) {
    const auto status = value.cancelled  ? M7ARC_ERR_CANCELLED
                        : value.ok()     ? M7ARC_OK
                                         : M7ARC_ERR_OPERATION_FAILED;
    if (status == M7ARC_ERR_CANCELLED)
        lastError = "Operation cancelled";
    else if (status == M7ARC_ERR_OPERATION_FAILED)
        lastError = value.errors.empty() ? "Operation failed" : value.errors.front();
    if (out)
        *out = new m7arc_result{std::move(value)};
    return status;
}
void clear(void* out) {
    if (out)
        *static_cast<void**>(out) = nullptr;
}
template <class T>
const T* at(const std::vector<T>& values, size_t index) {
    return index < values.size() ? &values[index] : nullptr;
}
void fill(const m7arc::Property& p, m7arc_property_info* out) {
    *out = {p.id, p.name.c_str(), p.value.c_str()};
}
} // namespace

extern "C" {

const char* m7arc_version(void) {
    try {
        return version().c_str();
    } catch (...) {
        return "";
    }
}
const char* m7arc_last_error(void) {
    return lastError.c_str();
}

m7arc_cancel_token* m7arc_cancel_token_new(void) {
    return new (std::nothrow) m7arc_cancel_token;
}
void m7arc_cancel_token_cancel(m7arc_cancel_token* token) {
    if (token)
        token->flag.store(true);
}
void m7arc_cancel_token_free(m7arc_cancel_token* token) {
    delete token;
}

void m7arc_engine_options_init(m7arc_engine_options* o) {
    if (o)
        *o = {true, nullptr, 0};
}
void m7arc_open_options_init(m7arc_open_options* o) {
    if (o)
        *o = {nullptr, nullptr};
}
void m7arc_extract_options_init(m7arc_extract_options* o) {
    if (o)
        *o = {nullptr, M7ARC_EXTRACT_PATHS_FULL, M7ARC_OVERWRITE_ASK, false, false, true, nullptr};
}
void m7arc_create_options_init(m7arc_create_options* o) {
    if (!o)
        return;
    const m7arc::CreateOptions d;
    *o = {};
    o->format = "7z";
    o->level = d.level;
    o->solid = d.solid;
    o->update_mode = M7ARC_UPDATE_ADD;
    o->store_symbolic_links = d.storeSymbolicLinks;
    o->preserve_mac_metadata = d.preserveMacMetadata;
    o->path_mode = M7ARC_ARCHIVE_PATHS_RELATIVE;
}
void m7arc_callbacks_init(m7arc_callbacks* c) {
    if (c)
        *c = {};
}

m7arc_status m7arc_engine_new(const m7arc_engine_options* options, m7arc_engine** out) {
    return guarded([&] {
        if (!out)
            invalid("out");
        *out = nullptr;
        m7arc::EngineOptions o;
        if (options) {
            o.loadUserModules = options->load_user_modules;
            o.additionalModuleDirectories =
                strings(options->additional_module_directories, options->additional_module_directory_count);
        }
        *out = new m7arc_engine(o);
        return M7ARC_OK;
    });
}
void m7arc_engine_free(m7arc_engine* engine) {
    delete engine;
}

m7arc_status m7arc_formats(m7arc_engine* engine, m7arc_format_list** out) {
    return guarded([&] {
        if (!engine || !out)
            invalid("engine/out");
        *out = nullptr;
        auto list = std::make_unique<m7arc_format_list>();
        list->formats = engine->engine.formats();
        for (const auto& f : list->formats) {
            auto& pointers = list->extensions.emplace_back();
            for (const auto& e : f.extensions)
                pointers.push_back(e.c_str());
        }
        *out = list.release();
        return M7ARC_OK;
    });
}
size_t m7arc_format_list_count(const m7arc_format_list* l) {
    return l ? l->formats.size() : 0;
}
const char* m7arc_format_list_name(const m7arc_format_list* l, size_t i) {
    const auto* f = l ? at(l->formats, i) : nullptr;
    return f ? f->name.c_str() : nullptr;
}
size_t m7arc_format_list_extension_count(const m7arc_format_list* l, size_t i) {
    const auto* e = l ? at(l->extensions, i) : nullptr;
    return e ? e->size() : 0;
}
const char* m7arc_format_list_extension(const m7arc_format_list* l, size_t i, size_t j) {
    const auto* e = l ? at(l->extensions, i) : nullptr;
    return e && j < e->size() ? (*e)[j] : nullptr;
}
bool m7arc_format_list_can_read(const m7arc_format_list* l, size_t i) {
    const auto* f = l ? at(l->formats, i) : nullptr;
    return f && f->canRead;
}
bool m7arc_format_list_can_write(const m7arc_format_list* l, size_t i) {
    const auto* f = l ? at(l->formats, i) : nullptr;
    return f && f->canWrite;
}
bool m7arc_format_list_is_hash(const m7arc_format_list* l, size_t i) {
    const auto* f = l ? at(l->formats, i) : nullptr;
    return f && f->isHash;
}
void m7arc_format_list_free(m7arc_format_list* list) {
    delete list;
}

m7arc_status m7arc_hashers(m7arc_engine* engine, m7arc_hasher_list** out) {
    return guarded([&] {
        if (!engine || !out)
            invalid("engine/out");
        *out = nullptr;
        auto list = std::make_unique<m7arc_hasher_list>();
        list->hashers = engine->engine.hashers();
        *out = list.release();
        return M7ARC_OK;
    });
}
size_t m7arc_hasher_list_count(const m7arc_hasher_list* l) {
    return l ? l->hashers.size() : 0;
}
const char* m7arc_hasher_list_name(const m7arc_hasher_list* l, size_t i) {
    const auto* h = l ? at(l->hashers, i) : nullptr;
    return h ? h->name.c_str() : nullptr;
}
uint32_t m7arc_hasher_list_digest_size(const m7arc_hasher_list* l, size_t i) {
    const auto* h = l ? at(l->hashers, i) : nullptr;
    return h ? h->digestSize : 0;
}
void m7arc_hasher_list_free(m7arc_hasher_list* list) {
    delete list;
}

m7arc_status m7arc_list(m7arc_engine* engine, const char* archive, const m7arc_open_options* opts,
                        const m7arc_callbacks* callbacks, m7arc_snapshot** out) {
    return guarded([&] {
        if (!engine || !out)
            invalid("engine/out");
        *out = nullptr;
        auto value = engine->engine.list(required(archive, "archive"), adapt(opts), adapt(callbacks));
        *out = new m7arc_snapshot{std::move(value)};
        return M7ARC_OK;
    });
}
const char* m7arc_snapshot_path(const m7arc_snapshot* s) {
    return s ? s->value.path.c_str() : nullptr;
}
const char* m7arc_snapshot_format(const m7arc_snapshot* s) {
    return s ? s->value.format.c_str() : nullptr;
}
bool m7arc_snapshot_can_update(const m7arc_snapshot* s) {
    return s && s->value.canUpdate;
}
bool m7arc_snapshot_can_set_comment(const m7arc_snapshot* s) {
    return s && s->value.canSetComment;
}
size_t m7arc_snapshot_entry_count(const m7arc_snapshot* s) {
    return s ? s->value.entries.size() : 0;
}
bool m7arc_snapshot_entry(const m7arc_snapshot* s, size_t i, m7arc_entry_info* out) {
    const auto* e = s && out ? at(s->value.entries, i) : nullptr;
    if (!e)
        return false;
    *out = {e->index,
            e->path.c_str(),
            e->size,
            e->packedSize,
            e->isDirectory,
            e->encrypted,
            e->alternateStream,
            e->modifiedUnixSeconds,
            e->method.c_str(),
            e->attributes.c_str()};
    return true;
}
size_t m7arc_snapshot_entry_property_count(const m7arc_snapshot* s, size_t i) {
    const auto* e = s ? at(s->value.entries, i) : nullptr;
    return e ? e->properties.size() : 0;
}
bool m7arc_snapshot_entry_property(const m7arc_snapshot* s, size_t i, size_t j, m7arc_property_info* out) {
    const auto* e = s && out ? at(s->value.entries, i) : nullptr;
    const auto* p = e ? at(e->properties, j) : nullptr;
    if (!p)
        return false;
    fill(*p, out);
    return true;
}
size_t m7arc_snapshot_property_count(const m7arc_snapshot* s) {
    return s ? s->value.properties.size() : 0;
}
bool m7arc_snapshot_property(const m7arc_snapshot* s, size_t j, m7arc_property_info* out) {
    const auto* p = s && out ? at(s->value.properties, j) : nullptr;
    if (!p)
        return false;
    fill(*p, out);
    return true;
}
size_t m7arc_snapshot_volume_count(const m7arc_snapshot* s) {
    return s ? s->value.volumes.size() : 0;
}
const char* m7arc_snapshot_volume(const m7arc_snapshot* s, size_t i) {
    const auto* v = s ? at(s->value.volumes, i) : nullptr;
    return v ? v->c_str() : nullptr;
}
void m7arc_snapshot_free(m7arc_snapshot* snapshot) {
    delete snapshot;
}

m7arc_status m7arc_extract(m7arc_engine* engine, const char* archive, const char* destination,
                           const uint32_t* indices, size_t index_count, const m7arc_extract_options* opts,
                           const m7arc_callbacks* callbacks, m7arc_result** out) {
    clear(out);
    return guarded([&] {
        if (!engine)
            invalid("engine");
        return finish(engine->engine.extract(required(archive, "archive"), required(destination, "destination"),
                                             indexList(indices, index_count), adapt(opts), adapt(callbacks)),
                      out);
    });
}
m7arc_status m7arc_test(m7arc_engine* engine, const char* archive, const uint32_t* indices, size_t index_count,
                        const m7arc_open_options* opts, const m7arc_callbacks* callbacks, m7arc_result** out) {
    clear(out);
    return guarded([&] {
        if (!engine)
            invalid("engine");
        return finish(engine->engine.test(required(archive, "archive"), indexList(indices, index_count),
                                          adapt(opts), adapt(callbacks)),
                      out);
    });
}
m7arc_status m7arc_create(m7arc_engine* engine, const char* const* inputs, size_t input_count,
                          const char* destination, const m7arc_create_options* opts,
                          const m7arc_callbacks* callbacks, m7arc_result** out) {
    clear(out);
    return guarded([&] {
        if (!engine || (!inputs && input_count))
            invalid("engine/inputs");
        return finish(engine->engine.create(strings(inputs, input_count), required(destination, "destination"),
                                            adapt(opts), adapt(callbacks)),
                      out);
    });
}
m7arc_status m7arc_remove(m7arc_engine* engine, const char* archive, const uint32_t* indices, size_t index_count,
                          const m7arc_open_options* opts, const m7arc_callbacks* callbacks, m7arc_result** out) {
    clear(out);
    return guarded([&] {
        if (!engine || !indices || !index_count)
            invalid("engine/indices");
        return finish(engine->engine.remove(required(archive, "archive"), indexList(indices, index_count),
                                            adapt(opts), adapt(callbacks)),
                      out);
    });
}
m7arc_status m7arc_rename(m7arc_engine* engine, const char* archive, uint32_t index, const char* new_path,
                          const m7arc_open_options* opts, const m7arc_callbacks* callbacks, m7arc_result** out) {
    clear(out);
    return guarded([&] {
        if (!engine)
            invalid("engine");
        return finish(engine->engine.rename(required(archive, "archive"), index, required(new_path, "new_path"),
                                            adapt(opts), adapt(callbacks)),
                      out);
    });
}
m7arc_status m7arc_rename_directory(m7arc_engine* engine, const char* archive, const char* old_prefix,
                                    const char* new_prefix, const m7arc_open_options* opts,
                                    const m7arc_callbacks* callbacks, m7arc_result** out) {
    clear(out);
    return guarded([&] {
        if (!engine)
            invalid("engine");
        return finish(engine->engine.renameDirectory(required(archive, "archive"),
                                                     required(old_prefix, "old_prefix"),
                                                     required(new_prefix, "new_prefix"), adapt(opts),
                                                     adapt(callbacks)),
                      out);
    });
}
m7arc_status m7arc_replace(m7arc_engine* engine, const char* archive, uint32_t index,
                           const char* edited_local_file, const m7arc_open_options* opts,
                           const m7arc_callbacks* callbacks, m7arc_result** out) {
    clear(out);
    return guarded([&] {
        if (!engine)
            invalid("engine");
        return finish(engine->engine.replace(required(archive, "archive"), index,
                                             required(edited_local_file, "edited_local_file"), adapt(opts),
                                             adapt(callbacks)),
                      out);
    });
}
m7arc_status m7arc_set_comment(m7arc_engine* engine, const char* archive, uint32_t index, const char* comment,
                               const m7arc_open_options* opts, const m7arc_callbacks* callbacks,
                               m7arc_result** out) {
    clear(out);
    return guarded([&] {
        if (!engine)
            invalid("engine");
        return finish(engine->engine.setComment(required(archive, "archive"), index, text(comment), adapt(opts),
                                                adapt(callbacks)),
                      out);
    });
}

bool m7arc_result_cancelled(const m7arc_result* r) {
    return r && r->value.cancelled;
}
bool m7arc_result_password_may_be_incorrect(const m7arc_result* r) {
    return r && r->value.passwordMayBeIncorrect;
}
uint64_t m7arc_result_completed_items(const m7arc_result* r) {
    return r ? r->value.completedItems : 0;
}
size_t m7arc_result_error_count(const m7arc_result* r) {
    return r ? r->value.errors.size() : 0;
}
const char* m7arc_result_error(const m7arc_result* r, size_t i) {
    const auto* e = r ? at(r->value.errors, i) : nullptr;
    return e ? e->c_str() : nullptr;
}
size_t m7arc_result_output_path_count(const m7arc_result* r) {
    return r ? r->value.outputPaths.size() : 0;
}
const char* m7arc_result_output_path(const m7arc_result* r, size_t i) {
    const auto* e = r ? at(r->value.outputPaths, i) : nullptr;
    return e ? e->c_str() : nullptr;
}
void m7arc_result_free(m7arc_result* result) {
    delete result;
}

m7arc_status m7arc_hash(m7arc_engine* engine, const char* const* inputs, size_t input_count,
                        const char* const* algorithms, size_t algorithm_count, const m7arc_callbacks* callbacks,
                        m7arc_hash_report** out) {
    return guarded([&] {
        if (!engine || !out)
            invalid("engine/out");
        *out = nullptr;
        m7arc::HashSummary summary;
        summary.files = engine->engine.hash(strings(inputs, input_count), strings(algorithms, algorithm_count),
                                            adapt(callbacks));
        *out = new m7arc_hash_report{std::move(summary)};
        return M7ARC_OK;
    });
}
m7arc_status m7arc_hash_summary(m7arc_engine* engine, const char* const* inputs, size_t input_count,
                                const char* const* algorithms, size_t algorithm_count,
                                const m7arc_callbacks* callbacks, m7arc_hash_report** out) {
    return guarded([&] {
        if (!engine || !out)
            invalid("engine/out");
        *out = nullptr;
        auto summary = engine->engine.hashSummary(strings(inputs, input_count),
                                                  strings(algorithms, algorithm_count), adapt(callbacks));
        *out = new m7arc_hash_report{std::move(summary)};
        return M7ARC_OK;
    });
}
size_t m7arc_hash_report_file_count(const m7arc_hash_report* r) {
    return r ? r->value.files.size() : 0;
}
bool m7arc_hash_report_file(const m7arc_hash_report* r, size_t i, m7arc_hash_info* out) {
    const auto* f = r && out ? at(r->value.files, i) : nullptr;
    if (!f)
        return false;
    *out = {f->path.c_str(), f->algorithm.c_str(), f->digest.c_str(), f->size};
    return true;
}
size_t m7arc_hash_report_total_count(const m7arc_hash_report* r) {
    return r ? r->value.totals.size() : 0;
}
bool m7arc_hash_report_total(const m7arc_hash_report* r, size_t i, m7arc_hash_total_info* out) {
    const auto* t = r && out ? at(r->value.totals, i) : nullptr;
    if (!t)
        return false;
    *out = {t->algorithm.c_str(), t->dataSum.c_str(), t->dataAndNamesSum.c_str()};
    return true;
}
uint64_t m7arc_hash_report_files_hashed(const m7arc_hash_report* r) {
    return r ? r->value.fileCount : 0;
}
uint64_t m7arc_hash_report_directories(const m7arc_hash_report* r) {
    return r ? r->value.directoryCount : 0;
}
uint64_t m7arc_hash_report_total_bytes(const m7arc_hash_report* r) {
    return r ? r->value.totalBytes : 0;
}
void m7arc_hash_report_free(m7arc_hash_report* report) {
    delete report;
}

m7arc_status m7arc_verify_manifest(m7arc_engine* engine, const char* manifest_path,
                                   const m7arc_callbacks* callbacks, m7arc_manifest** out) {
    clear(out);
    return guarded([&] {
        if (!engine)
            invalid("engine");
        auto value = engine->engine.verifyManifest(required(manifest_path, "manifest_path"), adapt(callbacks));
        const auto status = value.cancelled ? M7ARC_ERR_CANCELLED : value.ok() ? M7ARC_OK : M7ARC_ERR_OPERATION_FAILED;
        if (status == M7ARC_ERR_CANCELLED)
            lastError = "Operation cancelled";
        else if (status == M7ARC_ERR_OPERATION_FAILED)
            lastError = value.errors.empty() ? "Manifest verification found problems" : value.errors.front();
        if (out)
            *out = new m7arc_manifest{std::move(value)};
        return status;
    });
}
size_t m7arc_manifest_entry_count(const m7arc_manifest* m) {
    return m ? m->value.entries.size() : 0;
}
bool m7arc_manifest_entry_at(const m7arc_manifest* m, size_t i, m7arc_manifest_entry* out) {
    const auto* e = m && out ? at(m->value.entries, i) : nullptr;
    if (!e)
        return false;
    *out = {e->path.c_str(), static_cast<std::int32_t>(e->status), e->message.c_str()};
    return true;
}
bool m7arc_manifest_cancelled(const m7arc_manifest* m) {
    return m && m->value.cancelled;
}
size_t m7arc_manifest_error_count(const m7arc_manifest* m) {
    return m ? m->value.errors.size() : 0;
}
const char* m7arc_manifest_error(const m7arc_manifest* m, size_t i) {
    const auto* e = m ? at(m->value.errors, i) : nullptr;
    return e ? e->c_str() : nullptr;
}
void m7arc_manifest_free(m7arc_manifest* manifest) {
    delete manifest;
}

} // extern "C"
