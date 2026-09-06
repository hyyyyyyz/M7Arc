#pragma once

#include "ArchiveEngine.h"
#include "7zip/UI/Common/LoadCodecs.h"
#include "7zip/Common/CreateCoder.h"

namespace m7arc {
struct EngineRuntime {
    CCodecs* codecs = new CCodecs;
    CMyComPtr<ICompressCodecsInfo> reference = codecs;
    CExternalCodecs external;
    CCodecs::CReleaser releaser;
    std::vector<std::string> directories;
    std::vector<ModuleInfo> modules;

    explicit EngineRuntime(std::vector<std::string> roots);
};
std::shared_ptr<EngineRuntime> engineRuntime(const EngineOptions& options);
} // namespace m7arc
