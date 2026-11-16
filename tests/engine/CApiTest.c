#include "engine/capi/m7arc_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            fprintf(stderr, "FAIL %s:%d %s (%s)\n", __FILE__, __LINE__, msg, m7arc_last_error());           \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (0)

static void write_file(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}
static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    char* buffer = calloc(1, 4096);
    size_t n = fread(buffer, 1, 4095, f);
    buffer[n] = 0;
    fclose(f);
    return buffer;
}

static int progress_calls = 0;
static bool count_progress(const m7arc_progress_info* info, void* user) {
    (void)user;
    CHECK(info->phase != NULL && info->path != NULL, "progress strings are set");
    ++progress_calls;
    return true;
}
static bool cancel_on_compress(const m7arc_progress_info* info, void* user) {
    (void)user;
    return info->stage != M7ARC_STAGE_COMPRESSING;
}
static int password_calls = 0;
static const char* give_password(void* user) {
    ++password_calls;
    return (const char*)user;
}

static int find_entry(const m7arc_snapshot* snapshot, const char* path, m7arc_entry_info* out) {
    for (size_t i = 0; i < m7arc_snapshot_entry_count(snapshot); ++i) {
        m7arc_entry_info info;
        if (m7arc_snapshot_entry(snapshot, i, &info) && strcmp(info.path, path) == 0) {
            *out = info;
            return 1;
        }
    }
    return 0;
}

int main(void) {
    char root[] = "/tmp/m7arc-capi-XXXXXX";
    if (!mkdtemp(root))
        return 2;
    char path[1024], archive[1024], out_dir[1024];
    snprintf(path, sizeof path, "%s/input", root);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/input/hello.txt", root);
    write_file(path, "hello from C\n");
    snprintf(path, sizeof path, "%s/input/second.txt", root);
    write_file(path, "second file\n");
    char input[1024];
    snprintf(input, sizeof input, "%s/input", root);

    m7arc_engine_options engine_options;
    m7arc_engine_options_init(&engine_options);
    engine_options.load_user_modules = false;
    m7arc_engine* engine = NULL;
    CHECK(m7arc_engine_new(&engine_options, &engine) == M7ARC_OK && engine, "engine created");
    CHECK(strlen(m7arc_version()) > 0, "version text");

    m7arc_format_list* formats = NULL;
    CHECK(m7arc_formats(engine, &formats) == M7ARC_OK, "formats");
    int found7z = 0;
    for (size_t i = 0; i < m7arc_format_list_count(formats); ++i)
        if (strcmp(m7arc_format_list_name(formats, i), "7z") == 0 && m7arc_format_list_can_write(formats, i) &&
            m7arc_format_list_extension_count(formats, i) > 0)
            found7z = 1;
    CHECK(found7z, "7z is writable and has extensions");
    m7arc_format_list_free(formats);
    m7arc_hasher_list* hashers = NULL;
    CHECK(m7arc_hashers(engine, &hashers) == M7ARC_OK && m7arc_hasher_list_count(hashers) > 0, "hashers");
    m7arc_hasher_list_free(hashers);

    /* plain create / list / test / extract */
    snprintf(archive, sizeof archive, "%s/plain.zip", root);
    m7arc_create_options create;
    m7arc_create_options_init(&create);
    create.format = "zip";
    m7arc_callbacks callbacks;
    m7arc_callbacks_init(&callbacks);
    callbacks.progress = count_progress;
    const char* inputs[] = {input};
    m7arc_result* result = NULL;
    CHECK(m7arc_create(engine, inputs, 1, archive, &create, &callbacks, &result) == M7ARC_OK && result,
          "create zip");
    CHECK(progress_calls > 0, "progress callback ran");
    CHECK(m7arc_result_error_count(result) == 0, "create has no errors");
    m7arc_result_free(result);

    m7arc_snapshot* snapshot = NULL;
    CHECK(m7arc_list(engine, archive, NULL, NULL, &snapshot) == M7ARC_OK, "list");
    m7arc_entry_info entry;
    CHECK(find_entry(snapshot, "input/hello.txt", &entry) && entry.size == 13 && !entry.is_directory,
          "listed member has size");
    CHECK(strcmp(m7arc_snapshot_format(snapshot), "zip") == 0, "snapshot format");
    CHECK(m7arc_snapshot_can_update(snapshot), "zip can update");
    uint32_t hello_index = entry.index;
    m7arc_snapshot_free(snapshot);

    CHECK(m7arc_test(engine, archive, NULL, 0, NULL, NULL, &result) == M7ARC_OK, "test archive");
    m7arc_result_free(result);

    snprintf(out_dir, sizeof out_dir, "%s/out", root);
    m7arc_extract_options extract;
    m7arc_extract_options_init(&extract);
    extract.overwrite = M7ARC_OVERWRITE_REPLACE;
    CHECK(m7arc_extract(engine, archive, out_dir, &hello_index, 1, &extract, NULL, &result) == M7ARC_OK,
          "extract one member");
    m7arc_result_free(result);
    snprintf(path, sizeof path, "%s/out/input/hello.txt", root);
    char* text = read_file(path);
    CHECK(text && strcmp(text, "hello from C\n") == 0, "extracted content");
    free(text);
    snprintf(path, sizeof path, "%s/out/input/second.txt", root);
    CHECK(access(path, F_OK) != 0, "unselected member not extracted");

    /* edit members */
    CHECK(m7arc_rename(engine, archive, hello_index, "input/renamed.txt", NULL, NULL, &result) == M7ARC_OK,
          "rename member");
    m7arc_result_free(result);
    CHECK(m7arc_list(engine, archive, NULL, NULL, &snapshot) == M7ARC_OK, "list after rename");
    CHECK(find_entry(snapshot, "input/renamed.txt", &entry), "renamed member present");
    uint32_t removed = entry.index;
    m7arc_snapshot_free(snapshot);
    CHECK(m7arc_remove(engine, archive, &removed, 1, NULL, NULL, &result) == M7ARC_OK, "remove member");
    m7arc_result_free(result);
    CHECK(m7arc_list(engine, archive, NULL, NULL, &snapshot) == M7ARC_OK, "list after remove");
    CHECK(!find_entry(snapshot, "input/renamed.txt", &entry), "removed member gone");
    m7arc_snapshot_free(snapshot);

    /* encryption: password callback, wrong password, missing password */
    snprintf(archive, sizeof archive, "%s/secret.7z", root);
    create.format = "7z";
    create.password = "correct horse";
    create.encrypt_headers = true;
    CHECK(m7arc_create(engine, inputs, 1, archive, &create, NULL, &result) == M7ARC_OK, "create encrypted");
    m7arc_result_free(result);
    snapshot = (m7arc_snapshot*)1;
    CHECK(m7arc_list(engine, archive, NULL, NULL, &snapshot) == M7ARC_ERR_PASSWORD_REQUIRED && !snapshot,
          "password required status");
    CHECK(strlen(m7arc_last_error()) > 0, "error message set");
    m7arc_callbacks wrong;
    m7arc_callbacks_init(&wrong);
    wrong.password = give_password;
    wrong.user_data = "wrong";
    CHECK(m7arc_list(engine, archive, NULL, &wrong, &snapshot) == M7ARC_ERR_PASSWORD_OR_CORRUPTION,
          "wrong password status");
    m7arc_callbacks right = wrong;
    right.user_data = "correct horse";
    password_calls = 0;
    CHECK(m7arc_list(engine, archive, NULL, &right, &snapshot) == M7ARC_OK && password_calls > 0,
          "password callback opens archive");
    m7arc_snapshot_free(snapshot);
    m7arc_string_pair override = {archive, "correct horse"};
    m7arc_callbacks overrides;
    m7arc_callbacks_init(&overrides);
    overrides.password_overrides = &override;
    overrides.password_override_count = 1;
    CHECK(m7arc_list(engine, archive, NULL, &overrides, &snapshot) == M7ARC_OK, "password override");
    m7arc_snapshot_free(snapshot);

    /* cancellation: callback and token */
    snprintf(archive, sizeof archive, "%s/cancelled.7z", root);
    m7arc_callbacks cancelling;
    m7arc_callbacks_init(&cancelling);
    cancelling.progress = cancel_on_compress;
    create.password = NULL;
    create.encrypt_headers = false;
    CHECK(m7arc_create(engine, inputs, 1, archive, &create, &cancelling, &result) == M7ARC_ERR_CANCELLED &&
              result && m7arc_result_cancelled(result),
          "progress callback cancels create");
    m7arc_result_free(result);
    CHECK(access(archive, F_OK) != 0, "cancelled create leaves no archive");
    m7arc_cancel_token* token = m7arc_cancel_token_new();
    m7arc_callbacks tokened;
    m7arc_callbacks_init(&tokened);
    tokened.cancel_token = token;
    m7arc_cancel_token_cancel(token);
    CHECK(m7arc_create(engine, inputs, 1, archive, &create, &tokened, &result) == M7ARC_ERR_CANCELLED,
          "cancel token cancels create");
    m7arc_result_free(result);
    m7arc_cancel_token_free(token);

    /* argument and file errors become statuses */
    CHECK(m7arc_list(engine, NULL, NULL, NULL, &snapshot) == M7ARC_ERR_INVALID_ARGUMENT, "NULL archive rejected");
    snprintf(path, sizeof path, "%s/missing.7z", root);
    CHECK(m7arc_list(engine, path, NULL, NULL, &snapshot) == M7ARC_ERR_GENERAL && snapshot == NULL,
          "missing archive reports an error");
    m7arc_extract_options bad = extract;
    bad.overwrite = 99;
    CHECK(m7arc_extract(engine, archive, out_dir, NULL, 0, &bad, NULL, &result) == M7ARC_ERR_INVALID_ARGUMENT,
          "bad enum rejected");

    /* hashing */
    snprintf(path, sizeof path, "%s/input/hello.txt", root);
    const char* hash_inputs[] = {path};
    const char* algorithms[] = {"SHA256"};
    m7arc_hash_report* report = NULL;
    CHECK(m7arc_hash_summary(engine, hash_inputs, 1, algorithms, 1, NULL, &report) == M7ARC_OK, "hash summary");
    m7arc_hash_info hash;
    CHECK(m7arc_hash_report_file(report, 0, &hash) && strlen(hash.digest) == 64 && hash.size == 13,
          "sha256 digest");
    CHECK(m7arc_hash_report_files_hashed(report) == 1, "hashed file count");
    m7arc_hash_report_free(report);

    m7arc_engine_free(engine);
    char command[2048];
    snprintf(command, sizeof command, "rm -rf '%s'", root);
    if (failures == 0 && system(command) != 0)
        return 2;
    if (failures) {
        fprintf(stderr, "%d failure(s); fixtures in %s\n", failures, root);
        return 1;
    }
    puts("C API integration passed");
    return 0;
}
