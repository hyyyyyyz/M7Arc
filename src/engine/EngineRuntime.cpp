#include "EngineRuntime.h"
#include "7zip/UI/Common/HashCalc.h"
#include "Common/UTFConvert.h"

#include <algorithm>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

// Explicit contexts are passed to operations; upstream fallback callers use an empty catalog.
CExternalCodecs g_ExternalCodecs;
const CExternalCodecs* g_ExternalCodecs_Ptr = nullptr;

namespace m7arc {
namespace {
namespace fs = std::filesystem;
const char imageAnchor = 0;

std::string utf8(const UString& value) {
    AString text;
    ConvertUnicodeToUTF8(value, text);
    return text.Ptr();
}

std::vector<std::string> directoriesFor(const EngineOptions& options) {
    std::vector<std::string> roots;
    Dl_info image{};
    if (dladdr(&imageAnchor, &image) && image.dli_fname) {
        for (auto parent = fs::absolute(image.dli_fname).parent_path(); !parent.empty();
             parent = parent.parent_path()) {
            if (parent.extension() == ".app") {
                roots.push_back((parent / "Contents/PlugIns/7zip").string());
                break;
            }
            if (parent == parent.root_path())
                break;
        }
    }
    if (options.loadUserModules)
        if (const auto* home = std::getenv("HOME"))
            roots.push_back((fs::path(home) / "Library/Application Support/M7Arc/7zip").string());
    roots.insert(roots.end(), options.additionalModuleDirectories.begin(),
                 options.additionalModuleDirectories.end());
    for (auto& root : roots)
        root = fs::absolute(root).lexically_normal().string();
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    return roots;
}

std::string status(HRESULT result) {
    std::ostringstream text;
    text << "7-Zip module error 0x" << std::hex << static_cast<std::uint32_t>(result);
    return text.str();
}
} // namespace

EngineRuntime::EngineRuntime(std::vector<std::string> roots) : directories(std::move(roots)) {
    releaser.Set(codecs);
    std::set<std::string> paths;
    for (const auto& root : directories) {
        for (const auto& directory :
             {fs::path(root), fs::path(root) / "Codecs", fs::path(root) / "Formats"}) {
            std::error_code error;
            if (!fs::exists(directory, error))
                continue;
            fs::directory_iterator iterator(directory, error);
            if (error) {
                ModuleInfo info;
                info.path = directory.string();
                info.error = error.message();
                modules.push_back(std::move(info));
                continue;
            }
            for (const auto& entry : iterator) {
                const auto extension = entry.path().extension();
                if ((extension == ".dylib" || extension == ".so") && entry.is_regular_file())
                    paths.insert(fs::canonical(entry.path()).string());
            }
        }
    }
    FStringVector files;
    for (const auto& path : paths)
        files.Add(FString(path.c_str()));
    const HRESULT result = codecs->Load(&files);
    for (const auto& path : paths) {
        ModuleInfo info;
        info.path = path;
        for (unsigned i = 0; i < codecs->Errors.Size(); ++i) {
            const auto& error = codecs->Errors[i];
            if (path == error.Path.Ptr()) {
                if (!info.error.empty())
                    info.error += "\n";
                info.error += error.Message.IsEmpty() ? status(error.ErrorCode) : error.Message.Ptr();
            }
        }
        for (unsigned i = 0; i < codecs->Libs.Size(); ++i) {
            const auto& library = codecs->Libs[i];
            if (path != library.Path.Ptr())
                continue;
            info.loaded = true;
            info.version = library.Version;
            for (unsigned j = 0; j < codecs->Formats.Size(); ++j)
                if (codecs->Formats[j].LibIndex == static_cast<int>(i))
                    info.formats.push_back(utf8(codecs->Formats[j].Name));
            UInt32 count = 0;
            reference->GetNumMethods(&count);
            for (UInt32 j = 0; j < count; ++j)
                if (codecs->GetCodec_LibIndex(j) == static_cast<int>(i))
                    info.codecs.emplace_back(codecs->GetCodec_Name(j).Ptr());
            count = static_cast<IHashers*>(codecs)->GetNumHashers();
            for (UInt32 j = 0; j < count; ++j)
                if (codecs->GetHasherLibIndex(j) == static_cast<int>(i))
                    info.hashers.emplace_back(codecs->GetHasherName(j).Ptr());
        }
        if (!info.loaded && info.error.empty())
            info.error = "No 7-Zip formats, codecs or hashers exported";
        modules.push_back(std::move(info));
    }
    if (result != S_OK) {
        codecs->CloseLibs();
        const FStringVector none;
        const auto fallback = codecs->Load(&none);
        if (fallback != S_OK)
            throw EngineError(status(fallback));
        for (auto& module : modules) {
            module.loaded = false;
            if (module.error.empty())
                module.error = "External module initialization failed: " + status(result);
        }
    }
    Codecs_AddHashArcHandler(codecs);
    external.GetCodecs = codecs;
    external.GetHashers = codecs;
    const auto loaded = external.Load();
    if (loaded != S_OK)
        throw EngineError(status(loaded));
    g_ExternalCodecs_Ptr = &external;
}

std::shared_ptr<EngineRuntime> engineRuntime(const EngineOptions& options) {
    static std::mutex mutex;
    static std::shared_ptr<EngineRuntime> runtime;
    std::lock_guard lock(mutex);
    const auto directories = directoriesFor(options);
    if (!runtime) {
        runtime = std::make_shared<EngineRuntime>(directories);
        // Release SetCodecs references before the loaded modules' global destructors.
        std::atexit([] { runtime.reset(); });
    } else if (runtime->directories != directories)
        throw EngineError("Module directories are fixed at startup. Restart M7Arc to change them.");
    return runtime;
}
} // namespace m7arc
