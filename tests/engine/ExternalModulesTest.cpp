#include "engine/ArchiveEngine.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
void good(const m7arc::OperationResult& result) {
    std::string errors;
    for (const auto& error : result.errors)
        errors += error + "\n";
    require(result.ok(), errors.empty() ? "Operation cancelled" : errors);
}
std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    const auto root = fs::temp_directory_path() / ("m7arc-modules-" + std::to_string(getpid()));
    try {
        fs::create_directories(root / "plugins/Formats");
        fs::create_directories(root / "plugins/Codecs");
        fs::copy_file(argv[1], root / "plugins/Formats/Sample.dylib");
        fs::create_symlink("../Formats/Sample.dylib", root / "plugins/Codecs/Sample.so");
        std::ofstream(root / "plugins/Broken.dylib") << "not a Mach-O library";
        std::ofstream(root / "plugins/README.txt") << "This is not a module.";
        m7arc::EngineOptions options;
        options.loadUserModules = false;
        options.additionalModuleDirectories.push_back((root / "plugins").string());
        m7arc::ArchiveEngine engine(options);
        require(engine.formats().size() == 62,
                "Native module adds a format without losing built-in handlers");
        require(engine.codecs().size() == 26, "Native module adds a codec without losing built-in methods");
        require(engine.hashers().size() == 10, "Internal hashers remain available");
        bool loaded = false, rejected = false;
        for (const auto& module : engine.modules()) {
            if (fs::path(module.path).filename() == "Sample.dylib")
                loaded = module.loaded && module.formats == std::vector<std::string>{"M7Module"} &&
                         module.codecs == std::vector<std::string>{"M7Xor"};
            if (fs::path(module.path).filename() == "Broken.dylib")
                rejected = !module.loaded && !module.error.empty();
        }
        require(loaded && rejected, "Catalog reports loaded capabilities and actionable loading failures");
        const std::string payload = "External module: 中文 payload\n";
        std::ofstream(root / "source.txt") << payload;
        m7arc::CreateOptions create;
        create.method = "M7Xor";
        create.preserveMacMetadata = false;
        good(engine.create({(root / "source.txt").string()}, (root / "external.7z").string(), create));
        auto archive = engine.list((root / "external.7z").string());
        require(archive.entries.size() == 1 && archive.entries[0].method.find("M7Xor") != std::string::npos,
                "Internal 7z handler encodes data using the external method");
        good(engine.test((root / "external.7z").string()));
        good(engine.extract((root / "external.7z").string(), (root / "codec-out").string()));
        require(read(root / "codec-out/source.txt") == payload, "External codec restores the original bytes");
        std::ofstream(root / "external.m7module") << "M7MOD001" << payload;
        archive = engine.list((root / "external.m7module").string());
        require(archive.format == "M7Module" && archive.canUpdate,
                "External format participates in signature detection");
        {
            m7arc::ArchiveEngine second(options);
            good(second.test((root / "external.m7module").string()));
        }
        unsigned memoryRequests = 0;
        m7arc::Callbacks approveMemory;
        approveMemory.memoryRequest = [&](const m7arc::MemoryRequest& request) {
            require(request.requiredBytes == 65536 && request.allowedBytes == 32768 &&
                        request.path == "payload.txt",
                    "Memory request preserves official callback fields");
            ++memoryRequests;
            return m7arc::MemoryAnswer::Continue;
        };
        good(engine.extract((root / "external.m7module").string(), (root / "format-out").string(), {}, {},
                            approveMemory));
        require(memoryRequests == 1, "Official memory request reaches the application callback");
        m7arc::Callbacks denyMemory;
        denyMemory.memoryRequest = [](const m7arc::MemoryRequest&) { return m7arc::MemoryAnswer::Cancel; };
        require(engine.test((root / "external.m7module").string(), {}, {}, denyMemory).cancelled,
                "Rejecting an official memory request cancels the operation");
        require(read(root / "format-out/payload.txt") == payload,
                "External format decodes using the host's internal Copy codec after another engine closes");
        m7arc::CreateOptions externalCreate;
        externalCreate.format = "M7Module";
        good(engine.create({(root / "source.txt").string()}, (root / "created.m7module").string(),
                           externalCreate));
        good(engine.test((root / "created.m7module").string()));
        good(engine.extract((root / "created.m7module").string(), (root / "created-out").string()));
        require(read(root / "created-out/payload.txt") == payload,
                "External writer without ISetProperties creates an archive using default options");
        require(engine.hash({(root / "source.txt").string()}, {"SHA256"}).size() == 1,
                "Hashing retains the internal method catalog");
        std::cout << "External codec, external format, host codec access and module diagnostics passed\n";
        fs::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\nFixtures: " << root << "\n";
        return 1;
    }
}
