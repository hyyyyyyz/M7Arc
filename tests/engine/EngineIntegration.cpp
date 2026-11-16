#include "engine/ArchiveEngine.h"
#include "platform/MacMetadata.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace fs = std::filesystem;
void check(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void good(const m7arc::OperationResult& r, const std::string& label) {
    if (!r.ok()) {
        std::string errors;
        for (const auto& e : r.errors)
            errors += "\n" + e;
        throw std::runtime_error(label + errors);
    }
}
int main() {
    fs::path root = fs::temp_directory_path() / ("m7arc-engine-test-" + std::to_string(getpid()));
    try {
        fs::create_directories(root / "input/empty");
        std::ofstream(root / "input/中文.txt") << "hello archive\n";
        std::ofstream(root / "input/script.sh") << "#!/bin/sh\necho hi\n";
        fs::permissions(root / "input/script.sh", fs::perms::owner_all);
        m7arc::EngineOptions engineOptions;
        engineOptions.loadUserModules = false;
        m7arc::ArchiveEngine engine(engineOptions);
        std::cerr << "Registered " << engine.formats().size() << " formats, " << engine.codecs().size()
                  << " codecs, " << engine.hashers().size() << " hashers\n";
        for (const auto& f : engine.formats())
            std::cerr << f.name << " ";
        std::cerr << "\n";
        check(engine.formats().size() == 61, "All 61 registered formats must link");
        check(engine.codecs().size() == 25, "All 25 codecs must link");
        check(engine.hashers().size() == 10, "All 10 hashers must link");
        for (const std::string format :
             {"7z", "zip", "tar", "wim", "gzip", "bzip2", "xz", "tar.gz", "tar.bz2", "tar.xz"}) {
            m7arc::CreateOptions create;
            create.format = format;
            create.threads = 2;
            const bool stream = format == "gzip" || format == "bzip2" || format == "xz";
            auto path = root / ("test." + format);
            good(engine.create({(root / (stream ? "input/中文.txt" : "input")).string()}, path.string(),
                               create),
                 "Create " + format);
            auto list = engine.list(path.string());
            check(!list.entries.empty(), "List " + format);
            if (!stream) {
                bool emptyDirectory = false;
                for (const auto& e : list.entries)
                    if (e.path == "input/empty" && e.isDirectory)
                        emptyDirectory = true;
                check(emptyDirectory, "Empty directory type from " + format);
            }
            good(engine.test(path.string()), "Test " + format);
            m7arc::ExtractOptions extract;
            extract.overwrite = m7arc::Overwrite::Replace;
            auto dest = root / ("out-" + format);
            good(engine.extract(path.string(), dest.string(), {}, extract), "Extract " + format);
            bool content = false;
            for (const auto& e : fs::recursive_directory_iterator(dest))
                if (e.is_regular_file() && read(e.path()) == "hello archive\n")
                    content = true;
            check(content, "Byte roundtrip " + format);
        }
        m7arc::ExtractOptions stripRoot;
        stripRoot.eliminateDuplicateRoot = true;
        stripRoot.overwrite = m7arc::Overwrite::Replace;
        good(engine.extract((root / "test.7z").string(), (root / "stripped").string(), {}, stripRoot),
             "Eliminate common root with Mac sidecars");
        check(read(root / "stripped/中文.txt") == "hello archive\n" && !fs::exists(root / "stripped/input"),
              "Root elimination keeps file hierarchy below common folder");
        auto hashes = engine.hash({(root / "input/中文.txt").string()}, {"SHA256", "CRC32"});
        check(hashes.size() == 2, "Two checksum results");
        m7arc::CreateOptions encrypted;
        encrypted.password = "test password";
        encrypted.encryptHeaders = true;
        auto enc = root / "secret.7z";
        good(engine.create({(root / "input").string()}, enc.string(), encrypted), "Encrypted create");
        m7arc::OpenOptions password;
        password.password = encrypted.password;
        good(engine.test(enc.string(), {}, password), "Encrypted test");
        m7arc::Callbacks ask;
        ask.password = []() { return std::optional<std::string>("test password"); };
        check(!engine.list(enc.string(), {}, ask).entries.empty(), "Password callback");
        bool passwordError = false;
        try {
            engine.list(enc.string(), {"wrong password", {}});
        } catch (const m7arc::EngineError& e) {
            passwordError = e.kind() == m7arc::EngineErrorKind::PasswordOrCorruption;
        }
        check(passwordError, "Encrypted headers expose password retry classification");
        bool passwordRequired = false;
        try {
            engine.list(enc.string());
        } catch (const m7arc::EngineError& e) {
            passwordRequired = e.kind() == m7arc::EngineErrorKind::PasswordRequired;
        }
        check(passwordRequired, "Missing password exposes password-required classification");
        m7arc::Callbacks cancelPassword;
        cancelPassword.password = []() { return std::optional<std::string>(); };
        bool passwordCancelled = false;
        try {
            engine.list(enc.string(), {}, cancelPassword);
        } catch (const m7arc::EngineError& e) {
            passwordCancelled = e.kind() == m7arc::EngineErrorKind::Cancelled;
        }
        check(passwordCancelled, "Cancelled password prompt remains cancellation");
        m7arc::Callbacks retriedPassword;
        retriedPassword.passwordOverrides.push_back({enc.string(), "test password"});
        m7arc::ExtractOptions oldPassword;
        oldPassword.password = "wrong password";
        oldPassword.overwrite = m7arc::Overwrite::Replace;
        good(engine.extract(enc.string(), (root / "password-retry").string(), {}, oldPassword,
                            retriedPassword),
             "Password override retries the original extract options");
        check(read(root / "password-retry/input/中文.txt") == "hello archive\n",
              "Retry preserves output directory and member paths");
        auto comments = engine.list((root / "test.zip").string());
        check(comments.canSetComment, "ZIP supports entry comments");
        for (const auto& e : comments.entries)
            if (e.path == "input/script.sh") {
                good(engine.setComment((root / "test.zip").string(), e.index, "中文注释 / notes"),
                     "Set ZIP comment");
                break;
            }
        comments = engine.list((root / "test.zip").string());
        bool commentFound = false;
        for (const auto& e : comments.entries)
            if (e.path == "input/script.sh")
                for (const auto& p : e.properties)
                    if (p.name == "Comment" && p.value == "中文注释 / notes")
                        commentFound = true;
        check(commentFound, "Unicode comment survives member update");
        good(engine.test((root / "test.zip").string()), "Comment update preserves archive data");
        auto list = engine.list((root / "test.7z").string());
        std::uint32_t member = 0;
        for (const auto& e : list.entries)
            if (e.path.ends_with("中文.txt"))
                member = e.index;
        m7arc::ExtractOptions extract;
        extract.overwrite = m7arc::Overwrite::Replace;
        auto selected = root / "selected";
        good(engine.extract((root / "test.7z").string(), selected.string(), {member}, extract),
             "Selected extract");
        int selectedFiles = 0;
        for (const auto& e : fs::recursive_directory_iterator(selected))
            if (e.is_regular_file())
                ++selectedFiles;
        check(selectedFiles == 1, "Selection extracts exactly one member");
        bool renameCollision = false;
        try {
            engine.rename((root / "test.7z").string(), member, "input/script.sh");
        } catch (const m7arc::EngineError&) {
            renameCollision = true;
        }
        check(renameCollision, "Renaming onto an existing archive member is rejected");
        check(setxattr((root / "test.7z").c_str(), "com.m7arc.archive", "retained", 8, 0, 0) == 0,
              "Set archive metadata");
        good(engine.rename((root / "test.7z").string(), member, "input/renamed.txt"), "Rename");
        char archiveAttr[16]{};
        check(getxattr((root / "test.7z").c_str(), "com.m7arc.archive", archiveAttr, sizeof(archiveAttr), 0,
                       0) == 8 &&
                  std::string(archiveAttr, 8) == "retained",
              "Archive updates preserve its extended attributes");
        list = engine.list((root / "test.7z").string());
        bool renamed = false;
        for (const auto& e : list.entries)
            if (e.path == "input/renamed.txt") {
                renamed = true;
                member = e.index;
            }
        check(renamed, "Rename visible");
        good(engine.remove((root / "test.7z").string(), {member}), "Delete");
        list = engine.list((root / "test.7z").string());
        for (const auto& e : list.entries)
            check(e.path != "input/renamed.txt", "Deleted member absent");
        good(engine.test((root / "test.7z").string()), "Archive valid after updates");
        m7arc::CreateOptions split;
        split.volumeSizes = {128};
        auto splitPath = root / "split.7z";
        auto splitResult = engine.create({(root / "input").string()}, splitPath.string(), split);
        good(splitResult, "Split create");
        check(splitResult.outputPaths.size() > 1, "Multiple split volumes");
        good(engine.test(splitPath.string() + ".001"), "Split test");
        std::ofstream(root / "abc.txt") << "abc";
        auto knownHash = engine.hash({(root / "abc.txt").string()}, {"SHA256"});
        check(knownHash[0].digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256 known vector");
        std::ofstream(root / "manifest.sha256") << knownHash[0].digest << "  abc.txt\n";
        good(engine.test((root / "manifest.sha256").string()), "Verify checksum manifest");
        std::ofstream(root / "abc.txt") << "changed";
        check(!engine.test((root / "manifest.sha256").string()).ok(),
              "Checksum verification detects mismatch");
        m7arc::CreateOptions advanced;
        advanced.method = "LZMA2";
        advanced.dictionaryBytes = 1 << 20;
        advanced.wordSize = 64;
        advanced.solidBlock = "16m";
        advanced.threads = 2;
        good(engine.create({(root / "input").string()}, (root / "advanced.7z").string(), advanced),
             "Advanced 7z properties");
        advanced.format = "zip";
        advanced.method = "Deflate";
        advanced.dictionaryBytes = 0;
        advanced.wordSize = 32;
        advanced.password = "zip password";
        advanced.encryptionMethod = "AES256";
        good(engine.create({(root / "input").string()}, (root / "aes.zip").string(), advanced),
             "ZIP AES create");
        good(engine.test((root / "aes.zip").string(), {}, {advanced.password, {}}), "ZIP AES test");
        auto existing = root / "preserve.7z";
        good(engine.create({(root / "input").string()}, existing.string()),
             "Create original for cancellation");
        auto originalBytes = read(existing);
        m7arc::Callbacks cancel;
        cancel.progress = [](const m7arc::Progress& p) { return p.phase != "Compressing"; };
        auto cancelled = engine.create({(root / "abc.txt").string()}, existing.string(), {}, cancel);
        check(cancelled.cancelled, "Cancel update callback");
        check(read(existing) == originalBytes, "Cancelled update leaves original bytes unchanged");
        auto conflict = root / "conflict";
        good(engine.extract((root / "test.zip").string(), conflict.string(), {}, extract),
             "Prepare overwrite destination");
        std::ofstream(conflict / "input/中文.txt") << "keep original";
        m7arc::Callbacks skip;
        int asked = 0;
        skip.overwrite = [&](const m7arc::OverwriteRequest&) {
            ++asked;
            return m7arc::OverwriteAnswer::Skip;
        };
        m7arc::ExtractOptions askExtract;
        good(engine.extract((root / "test.zip").string(), conflict.string(), {}, askExtract, skip),
             "Skip overwrite");
        check(asked > 0 && read(conflict / "input/中文.txt") == "keep original",
              "Overwrite callback preserves skipped file");
        const auto nativeFile = root / "input/中文.txt";
        const std::string attribute = "finder metadata";
        check(setxattr(nativeFile.c_str(), "com.m7arc.test", attribute.data(), attribute.size(), 0, 0) == 0,
              "Set source xattr");
        const m7arc::mac::FileTime birth{1700000000, 123456789};
        m7arc::mac::setCreationTime(nativeFile, birth);
        for (const std::string f : {"7z", "zip", "tar", "wim", "tar.gz"}) {
            m7arc::CreateOptions settings;
            settings.format = f;
            auto a = root / ("metadata." + f);
            good(engine.create({(root / "input").string()}, a.string(), settings), "Metadata archive " + f);
            const auto d = root / ("metadata-out-" + f);
            good(engine.extract(a.string(), d.string(), {}, extract), "Metadata extract " + f);
            char value[128]{};
            const auto target = d / "input/中文.txt";
            const auto length = getxattr(target.c_str(), "com.m7arc.test", value, sizeof(value), 0, 0);
            check(length == static_cast<ssize_t>(attribute.size()) && std::string(value, length) == attribute,
                  "Native xattr roundtrip " + f);
            check(m7arc::mac::creationTime(target) == birth, "Birth time roundtrip " + f);
        }
        auto metadataArchive = root / "metadata.7z";
        auto metadataList = engine.list(metadataArchive.string());
        std::uint32_t metadataMember = 0;
        for (const auto& e : metadataList.entries)
            if (e.path == "input/中文.txt")
                metadataMember = e.index;
        const auto skipped = root / "metadata-skip/input/中文.txt";
        fs::create_directories(skipped.parent_path());
        std::ofstream(skipped) << "existing";
        check(setxattr(skipped.c_str(), "com.m7arc.test", "untouched", 9, 0, 0) == 0,
              "Set existing metadata");
        m7arc::ExtractOptions skipExisting;
        skipExisting.overwrite = m7arc::Overwrite::Skip;
        good(engine.extract(metadataArchive.string(), (root / "metadata-skip").string(), {metadataMember},
                            skipExisting),
             "Metadata skip");
        char oldAttr[128]{};
        check(getxattr(skipped.c_str(), "com.m7arc.test", oldAttr, sizeof(oldAttr), 0, 0) == 9 &&
                  std::string(oldAttr, 9) == "untouched",
              "Skipping file preserves original metadata");
        m7arc::ExtractOptions renameIncoming;
        renameIncoming.overwrite = m7arc::Overwrite::RenameIncoming;
        good(engine.extract(metadataArchive.string(), (root / "metadata-skip").string(), {metadataMember},
                            renameIncoming),
             "Metadata auto rename");
        bool renamedMetadata = false;
        for (const auto& e : fs::directory_iterator(skipped.parent_path()))
            if (e.path() != skipped) {
                char v[128]{};
                const auto n = getxattr(e.path().c_str(), "com.m7arc.test", v, sizeof(v), 0, 0);
                if (n == static_cast<ssize_t>(attribute.size()) && std::string(v, n) == attribute)
                    renamedMetadata = true;
            }
        check(renamedMetadata, "Auto-renamed file receives matching metadata");
        good(engine.rename(metadataArchive.string(), metadataMember, "input/native-renamed.txt"),
             "Rename metadata member");
        good(engine.extract(metadataArchive.string(), (root / "metadata-renamed").string(), {}, extract),
             "Extract renamed metadata");
        char renamedAttr[128]{};
        check(getxattr((root / "metadata-renamed/input/native-renamed.txt").c_str(), "com.m7arc.test",
                       renamedAttr, sizeof(renamedAttr), 0, 0) == static_cast<ssize_t>(attribute.size()),
              "Rename carries sidecar");
        m7arc::CreateOptions prefixOptions;
        prefixOptions.archivePrefix = "nested/folder";
        auto prefixArchive = root / "prefix.7z";
        good(engine.create({nativeFile.string()}, prefixArchive.string(), prefixOptions),
             "Create in archive directory");
        auto prefixList = engine.list(prefixArchive.string());
        std::uint32_t replaceIndex = 0;
        bool prefixFound = false;
        for (const auto& e : prefixList.entries)
            if (e.path == "nested/folder/中文.txt") {
                prefixFound = true;
                replaceIndex = e.index;
            }
        check(prefixFound, "Archive prefix preserves hierarchy");
        auto edited = root / "edited.txt";
        std::ofstream(edited) << "edited content";
        check(setxattr(edited.c_str(), "com.m7arc.test", "edited metadata", 15, 0, 0) == 0,
              "Set edited metadata");
        good(engine.replace(prefixArchive.string(), replaceIndex, edited.string()), "Replace exact member");
        good(engine.extract(prefixArchive.string(), (root / "replace-out").string(), {}, extract),
             "Extract replaced member");
        check(read(root / "replace-out/nested/folder/中文.txt") == "edited content",
              "Replacement keeps original member path");
        char editedAttr[128]{};
        check(getxattr((root / "replace-out/nested/folder/中文.txt").c_str(), "com.m7arc.test", editedAttr,
                       sizeof(editedAttr), 0, 0) == 15 &&
                  std::string(editedAttr, 15) == "edited metadata",
              "Replacement updates member metadata");
        auto beforeReplaceCancel = read(prefixArchive);
        m7arc::Callbacks cancelReplace;
        cancelReplace.progress = [](const m7arc::Progress& p) { return p.phase != "Updating"; };
        check(engine.replace(prefixArchive.string(), replaceIndex, edited.string(), {}, cancelReplace)
                  .cancelled,
              "Cancel member replacement");
        check(read(prefixArchive) == beforeReplaceCancel, "Cancelled replacement preserves original bytes");
        auto wrongZip = engine.test((root / "aes.zip").string(), {}, {"wrong password", {}});
        check(!wrongZip.ok() && wrongZip.passwordMayBeIncorrect,
              "Encrypted data failures expose password retry classification");
        good(engine.renameDirectory(prefixArchive.string(), "nested", "relocated"),
             "Rename implicit archive directory");
        auto relocated = engine.list(prefixArchive.string());
        bool relocatedMember = false;
        for (const auto& entry : relocated.entries) {
            check(!entry.path.starts_with("nested/"), "Implicit directory rename moves all descendants");
            if (entry.path == "relocated/folder/中文.txt")
                relocatedMember = true;
        }
        check(relocatedMember, "Implicit directory has its new prefix");
        good(engine.extract(prefixArchive.string(), (root / "relocated-out").string(), {}, extract),
             "Extract renamed implicit directory");
        char relocatedAttr[128]{};
        check(getxattr((root / "relocated-out/relocated/folder/中文.txt").c_str(), "com.m7arc.test",
                       relocatedAttr, sizeof(relocatedAttr), 0, 0) == 15,
              "Implicit directory rename preserves member metadata");
        auto encryptedList = engine.list((root / "aes.zip").string());
        std::uint32_t encryptedIndex = 0;
        for (const auto& e : encryptedList.entries)
            if (e.path == "input/中文.txt")
                encryptedIndex = e.index;
        m7arc::Callbacks zipPassword;
        zipPassword.password = []() { return std::optional<std::string>("zip password"); };
        good(engine.replace((root / "aes.zip").string(), encryptedIndex, edited.string(), {}, zipPassword),
             "Replace encrypted ZIP member");
        good(engine.test((root / "aes.zip").string(), {}, {"zip password", {}}),
             "Encrypted ZIP remains valid");
        encryptedList = engine.list((root / "aes.zip").string());
        for (const auto& e : encryptedList.entries)
            if (e.path == "input/中文.txt")
                check(e.encrypted && e.method.find("AES") != std::string::npos,
                      "Replacement retains AES encryption");
        fs::create_directories(root / "filters/cache");
        std::ofstream(root / "filters/keep.cpp") << "keep";
        std::ofstream(root / "filters/drop.txt") << "drop";
        std::ofstream(root / "filters/cache/also.cpp") << "excluded directory";
        m7arc::CreateOptions filters;
        filters.includePatterns = {"*.cpp"};
        filters.excludePatterns = {"cache"};
        auto filtered = root / "filtered.7z";
        good(engine.create({(root / "filters").string()}, filtered.string(), filters),
             "Include/exclude archive");
        auto filteredList = engine.list(filtered.string());
        bool keepFound = false;
        for (const auto& e : filteredList.entries) {
            check(e.path.find("drop.txt") == std::string::npos &&
                      e.path.find("also.cpp") == std::string::npos,
                  "Excluded data and sidecar absent");
            if (e.path == "filters/keep.cpp")
                keepFound = true;
        }
        check(keepFound, "Include filter retains match");
        for (const auto mode : {m7arc::ArchivePathMode::Full, m7arc::ArchivePathMode::Absolute}) {
            m7arc::CreateOptions paths;
            paths.pathMode = mode;
            auto a = root / (mode == m7arc::ArchivePathMode::Full ? "full.7z" : "absolute.7z");
            good(engine.create({nativeFile.string()}, a.string(), paths), "Path-mode creation");
            auto p = engine.list(a.string());
            auto expected = mode == m7arc::ArchivePathMode::Full ? nativeFile.relative_path().generic_string()
                                                                 : nativeFile.generic_string();
            bool found = false;
            for (const auto& e : p.entries)
                if (e.path == expected)
                    found = true;
            check(found, "Requested logical archive path mode");
        }
        m7arc::ExtractOptions current;
        current.pathMode = m7arc::PathMode::Current;
        current.currentDirectory = "relocated/folder";
        current.overwrite = m7arc::Overwrite::Replace;
        good(engine.extract(prefixArchive.string(), (root / "current-path").string(), {}, current),
             "Extract current folder");
        check(read(root / "current-path/中文.txt") == "edited content", "Current folder path removed");
        m7arc::CreateOptions memory;
        memory.format = "zip";
        memory.method = "Deflate";
        memory.threads = 1;
        auto estimate = engine.estimateMemory(memory);
        check(estimate.compressBytes == 4u * 1024 * 1024 && estimate.decompressBytes == 2u * 1024 * 1024,
              "Official Deflate memory model");
        for (const std::string f : {"7z", "zip"})
            for (const std::string method : {"LZMA", "PPMd", "BZip2", "Deflate", "Deflate64"}) {
                m7arc::CreateOptions methodOptions;
                methodOptions.format = f;
                methodOptions.method = method;
                methodOptions.threads = 1;
                methodOptions.dictionaryBytes = method == "BZip2"       ? 900000
                                                : method == "Deflate"   ? 32768
                                                : method == "Deflate64" ? 65536
                                                                        : 1 << 20;
                methodOptions.wordSize = method == "PPMd" ? 6 : method == "BZip2" ? 0 : 32;
                auto a = root / ("method-" + f + "-" + method + "." + f);
                good(engine.create({nativeFile.string()}, a.string(), methodOptions),
                     "Create " + f + " " + method);
                good(engine.test(a.string()), "Test " + f + " " + method);
            }
        fs::create_directories(root / "delete-filter");
        std::ofstream(root / "delete-filter/yes.cpp") << "included";
        std::ofstream(root / "delete-filter/no.txt") << "excluded";
        m7arc::CreateOptions deleteFiltered;
        deleteFiltered.includePatterns = {"*.cpp"};
        deleteFiltered.deleteAfterCompressing = true;
        good(engine.create({(root / "delete-filter").string()}, (root / "delete-filter.7z").string(),
                           deleteFiltered),
             "Delete archived filtered sources");
        check(!fs::exists(root / "delete-filter/yes.cpp") && fs::exists(root / "delete-filter/no.txt"),
              "Only archived source files deleted");
        fs::create_directories(root / "delete-dirs/unmatched-empty");
        fs::create_directories(root / "delete-dirs/kept");
        std::ofstream(root / "delete-dirs/kept/yes.cpp") << "included";
        std::ofstream(root / "delete-dirs/no.txt") << "excluded";
        good(engine.create({(root / "delete-dirs").string()}, (root / "delete-dirs.7z").string(),
                           deleteFiltered),
             "Delete archived sources with an unmatched empty directory");
        check(fs::exists(root / "delete-dirs/unmatched-empty") && !fs::exists(root / "delete-dirs/kept/yes.cpp") &&
                  fs::exists(root / "delete-dirs/no.txt"),
              "Empty directories excluded by include patterns are neither archived nor deleted");
        {
            bool listedEmpty = false;
            for (const auto& e : engine.list((root / "delete-dirs.7z").string()).entries)
                listedEmpty = listedEmpty || e.path.find("unmatched-empty") != std::string::npos;
            check(!listedEmpty, "Unmatched empty directory is absent from the filtered archive");
        }
        m7arc::CreateOptions tarGz;
        tarGz.format = "tar.gz";
        good(engine.create({(root / "input").string()}, (root / "cancel.tar.gz").string(), tarGz),
             "Create compressed TAR for cancellation");
        {
            m7arc::Callbacks cancelInner;
            cancelInner.progress = [](const m7arc::Progress& p) { return p.phase != "Extracting"; };
            bool cancelledKind = false;
            try {
                engine.list((root / "cancel.tar.gz").string(), {}, cancelInner);
            } catch (const m7arc::EngineError& e) {
                cancelledKind = e.kind() == m7arc::EngineErrorKind::Cancelled;
            }
            check(cancelledKind, "Cancelling the inner TAR extraction reports the Cancelled error kind");
        }
        auto changing = root / "changing.txt";
        std::ofstream(changing) << "before";
        m7arc::CreateOptions deleteChanging;
        deleteChanging.deleteAfterCompressing = true;
        bool changed = false;
        m7arc::Callbacks changeInput;
        changeInput.progress = [&](const m7arc::Progress& p) {
            if (p.phase == "Compressing" && !changed) {
                changed = true;
                std::ofstream(changing) << "newer data";
            }
            return true;
        };
        auto changedResult = engine.create({changing.string()}, (root / "changed-source.7z").string(),
                                           deleteChanging, changeInput);
        check(changed && !changedResult.ok() && read(changing) == "newer data",
              "Newly changed source retained after snapshot compression");
        m7arc::CreateOptions owner;
        owner.format = "tar";
        owner.storeOwnerId = true;
        owner.storeOwnerName = true;
        auto ownerArchive = root / "owner.tar";
        good(engine.create({"/usr/bin/true"}, ownerArchive.string(), owner),
             "Store source ownership through staging");
        auto owners = engine.list(ownerArchive.string());
        bool rootOwner = false;
        for (const auto& e : owners.entries)
            if (e.path == "true")
                for (const auto& p : e.properties) {
                    if (p.name == "User ID" && p.value == "0")
                        rootOwner = true;
                }
        check(rootOwner, "Root-owned input retains original uid in archive");
        fs::create_directories(root / "hash-tree/empty");
        std::ofstream(root / "hash-tree/a.txt") << "abc";
        std::ofstream(root / "hash-tree/b.txt") << "abc";
        const auto summary = engine.hashSummary({(root / "hash-tree").string()}, {"SHA256", "CRC32"});
        check(summary.fileCount == 2 && summary.directoryCount == 2 && summary.totalBytes == 6 &&
                  summary.files.size() == 4,
              "Hash summary counts files and directories in one traversal");
        fs::rename(root / "hash-tree/b.txt", root / "hash-tree/renamed.txt");
        const auto renamedSummary = engine.hashSummary({(root / "hash-tree").string()}, {"SHA256", "CRC32"});
        check(summary.totals[0].dataSum == renamedSummary.totals[0].dataSum &&
                  summary.totals[0].dataAndNamesSum != renamedSummary.totals[0].dataAndNamesSum,
              "Data checksum totals are stable while name-inclusive totals reflect a rename");
        std::ofstream(root / "verify-pass.txt") << "abc";
        std::ofstream(root / "verify-mismatch.txt") << "different bytes";
        std::ofstream(root / "verify.sha256") << knownHash[0].digest << "  verify-pass.txt\n"
                                              << knownHash[0].digest << "  verify-mismatch.txt\n"
                                              << knownHash[0].digest << "  verify-missing.txt\n";
        const auto verification = engine.verifyManifest((root / "verify.sha256").string());
        check(!verification.ok() && verification.entries.size() == 3,
              "Manifest verification reports every attempted member");
        for (const auto& item : verification.entries) {
            if (item.path == "verify-pass.txt")
                check(item.status == m7arc::VerificationStatus::Passed, "Manifest matching member passes");
            if (item.path == "verify-mismatch.txt")
                check(item.status == m7arc::VerificationStatus::Mismatch,
                      "Manifest changed member reports mismatch");
            if (item.path == "verify-missing.txt")
                check(item.status == m7arc::VerificationStatus::Missing,
                      "Manifest absent member reports missing");
        }
        m7arc::BenchmarkOptions bench;
        bench.threads = 1;
        bench.dictionaryBytes = 1 << 18;
        bench.seconds = 1;
        auto rates = engine.benchmark(bench);
        check(rates.encodeBytesPerSecond > 0 && rates.decodeBytesPerSecond > 0,
              "Official benchmark reports actual throughput");
        check(rates.encodeCpuPercent > 0 && rates.decodeCpuPercent > 0,
              "Official benchmark reports CPU usage");
        std::cout << "All formats, archive operations, cancellation, overwrite, checksum verification and "
                     "benchmark integration passed\n";
        fs::remove_all(root);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\nFixtures: " << root << '\n';
        return 1;
    }
}
