#include "ArchiveEngine.h"
#include "platform/MacMetadata.h"

#include "7zVersion.h"
#include "7zip/Common/FileStreams.h"
#include "7zip/Common/RegisterCodec.h"
#include "7zip/UI/Common/ArchiveExtractCallback.h"
#include "7zip/UI/Common/Bench.h"
#include "7zip/UI/Common/HashCalc.h"
#include "7zip/UI/Common/LoadCodecs.h"
#include "7zip/UI/Common/OpenArchive.h"
#include "7zip/UI/Common/PropIDUtils.h"
#include "7zip/UI/Common/SetProperties.h"
#include "7zip/UI/Common/Update.h"
#include "Common/MyCom.h"
#include "Common/MyInitGuid.h"
#include "Common/StringConvert.h"
#include "Common/UTFConvert.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <system_error>
#include <unistd.h>

extern unsigned g_NumCodecs;
extern const CCodecInfo* g_Codecs[];
extern unsigned g_NumHashers;
extern const CHasherInfo* g_Hashers[];

namespace m7arc {
namespace {
namespace fs = std::filesystem;
using NWindows::NCOM::CPropVariant;

UString wide(const std::string& s) {
    UString out;
    ConvertUTF8ToUnicode(AString(s.c_str()), out);
    return out;
}
std::string utf8(const UString& s) {
    AString out;
    ConvertUnicodeToUTF8(s, out);
    return out.Ptr();
}
std::string utf8(const wchar_t* s) {
    return s ? utf8(UString(s)) : std::string();
}
std::string errorCode(HRESULT hr) {
    std::ostringstream s;
    s << "7-Zip error 0x" << std::hex << static_cast<std::uint32_t>(hr);
    return s.str();
}
void require(HRESULT hr, const std::string& context) {
    if (hr != S_OK)
        throw EngineError(context + ": " + errorCode(hr),
                          hr == E_ABORT ? EngineErrorKind::Cancelled : EngineErrorKind::General);
}
std::string operationError(Int32 code) {
    switch (code) {
    case NArchive::NExtract::NOperationResult::kUnsupportedMethod:
        return "Unsupported compression method";
    case NArchive::NExtract::NOperationResult::kDataError:
        return "Data error or incorrect password";
    case NArchive::NExtract::NOperationResult::kCRCError:
        return "CRC error or incorrect password";
    case NArchive::NExtract::NOperationResult::kUnavailable:
        return "Archive data is unavailable";
    case NArchive::NExtract::NOperationResult::kUnexpectedEnd:
        return "Unexpected end of archive";
    case NArchive::NExtract::NOperationResult::kDataAfterEnd:
        return "Data after end of archive";
    case NArchive::NExtract::NOperationResult::kIsNotArc:
        return "Not an archive";
    case NArchive::NExtract::NOperationResult::kHeadersError:
        return "Archive headers error";
    case NArchive::NExtract::NOperationResult::kWrongPassword:
        return "Incorrect password";
    default:
        return "Archive operation failed (" + std::to_string(code) + ")";
    }
}
struct OperationContext {
    const Callbacks& callbacks;
    std::string password;
    bool passwordDefined;
    bool passwordRequested = false;
    OperationResult result;
    Progress progress;
    std::recursive_mutex mutex;
    std::vector<fs::path> temporaryDirectories;
    std::map<std::string, std::string> sourceMappings;
    ~OperationContext() {
        for (const auto& p : temporaryDirectories) {
            std::error_code ec;
            fs::remove_all(p, ec);
        }
    }
    explicit OperationContext(const Callbacks& cb, std::string pw = {})
        : callbacks(cb), password(std::move(pw)), passwordDefined(!password.empty()) {}
    HRESULT check() {
        std::lock_guard lock(mutex);
        if (result.cancelled || (callbacks.cancelled && callbacks.cancelled())) {
            result.cancelled = true;
            return E_ABORT;
        }
        return S_OK;
    }
    HRESULT report(std::optional<std::uint64_t> completed = {}, std::optional<std::uint64_t> total = {}) {
        std::lock_guard lock(mutex);
        if (check() != S_OK)
            return E_ABORT;
        if (completed)
            progress.completed = *completed;
        if (total) {
            progress.total = *total;
            progress.totalKnown = true;
        }
        if (callbacks.progress && !callbacks.progress(progress)) {
            result.cancelled = true;
            return E_ABORT;
        }
        return S_OK;
    }
    HRESULT phase(const std::string& name, const std::string& path = {}) {
        std::lock_guard lock(mutex);
        progress = {name, path, 0, 0, false};
        return report();
    }
    HRESULT item(const std::string& name) {
        std::lock_guard lock(mutex);
        progress.path = name;
        return report();
    }
    HRESULT getPassword(BSTR* out, bool ask = true) {
        std::lock_guard lock(mutex);
        if (check() != S_OK)
            return E_ABORT;
        if (ask)
            passwordRequested = true;
        if (!passwordDefined && ask) {
            if (!callbacks.password) {
                result.errors.push_back("A password is required");
                return E_ABORT;
            }
            auto value = callbacks.password();
            if (!value) {
                result.cancelled = true;
                return E_ABORT;
            }
            password = *value;
            passwordDefined = true;
        }
        return StringToBstr(wide(password), out);
    }
    HRESULT memberResult(Int32 code, const std::string& name = {}, bool encrypted = false) {
        std::lock_guard lock(mutex);
        if (code == NArchive::NExtract::NOperationResult::kWrongPassword ||
            (encrypted && (code == NArchive::NExtract::NOperationResult::kDataError ||
                           code == NArchive::NExtract::NOperationResult::kCRCError)))
            result.passwordMayBeIncorrect = true;
        if (code != NArchive::NExtract::NOperationResult::kOK)
            result.errors.push_back((name.empty() ? progress.path : name) + ": " + operationError(code));
        else
            ++result.completedItems;
        return check();
    }
    void finish(HRESULT hr) {
        if (hr == E_ABORT && result.errors.empty())
            result.cancelled = true;
        else if (hr != S_OK)
            result.errors.push_back(errorCode(hr));
    }
};
class OpenCallback final : public IOpenCallbackUI {
    OperationContext& ctx;

  public:
    explicit OpenCallback(OperationContext& c) : ctx(c) {}
    HRESULT Open_CheckBreak() override {
        return ctx.check();
    }
    HRESULT Open_SetTotal(const UInt64*, const UInt64* bytes) override {
        return ctx.report({}, bytes ? std::optional<std::uint64_t>(*bytes) : std::nullopt);
    }
    HRESULT Open_SetCompleted(const UInt64*, const UInt64* bytes) override {
        return ctx.report(bytes ? std::optional<std::uint64_t>(*bytes) : std::nullopt);
    }
    HRESULT Open_Finished() override {
        return ctx.check();
    }
    HRESULT Open_CryptoGetTextPassword(BSTR* p) override {
        return ctx.getPassword(p);
    }
};
class ExtractCallback final : public IFolderArchiveExtractCallback,
                              public IFolderArchiveExtractCallback2,
                              public ICryptoGetTextPassword,
                              public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(IFolderArchiveExtractCallback)
    Z7_COM_QI_ENTRY(IFolderArchiveExtractCallback2)
    Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
    OperationContext& ctx;

  public:
    explicit ExtractCallback(OperationContext& c) : ctx(c) {}
    Z7_COM7F_IMF(SetTotal(UInt64 size)) override {
        return ctx.report({}, size);
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64* v)) override {
        return ctx.report(v ? std::optional<std::uint64_t>(*v) : std::nullopt);
    }
    Z7_COM7F_IMF(AskOverwrite(const wchar_t* exist, const FILETIME*, const UInt64* existSize,
                              const wchar_t* incoming, const FILETIME*, const UInt64* incomingSize,
                              Int32* answer))
    override {
        if (ctx.check() != S_OK)
            return E_ABORT;
        if (!ctx.callbacks.overwrite) {
            *answer = NOverwriteAnswer::kNo;
            ctx.result.errors.push_back("Output already exists: " + utf8(exist));
            return S_OK;
        }
        OverwriteRequest r{utf8(exist), utf8(incoming), {}, {}};
        if (existSize)
            r.existingSize = *existSize;
        if (incomingSize)
            r.incomingSize = *incomingSize;
        const auto a = ctx.callbacks.overwrite(r);
        *answer = static_cast<Int32>(a);
        if (a == OverwriteAnswer::Cancel) {
            ctx.result.cancelled = true;
            return E_ABORT;
        }
        return S_OK;
    }
    Z7_COM7F_IMF(PrepareOperation(const wchar_t* name, Int32, Int32, const UInt64*)) override {
        return ctx.item(utf8(name));
    }
    Z7_COM7F_IMF(MessageError(const wchar_t* message)) override {
        ctx.result.errors.push_back(utf8(message));
        return ctx.check();
    }
    Z7_COM7F_IMF(SetOperationResult(Int32 code, Int32 encrypted)) override {
        return ctx.memberResult(code, {}, encrypted != 0);
    }
    Z7_COM7F_IMF(ReportExtractResult(Int32 code, Int32 encrypted, const wchar_t* name)) override {
        return ctx.memberResult(code, utf8(name), encrypted != 0);
    }
    Z7_COM7F_IMF(CryptoGetTextPassword(BSTR* p)) override {
        return ctx.getPassword(p);
    }
};
class UpdateCallback final : public IUpdateCallbackUI2 {
    OperationContext& ctx;

  public:
    explicit UpdateCallback(OperationContext& c) : ctx(c) {}
    HRESULT WriteSfx(const wchar_t* name, UInt64 size) override {
        ctx.item(utf8(name));
        return ctx.report({}, size);
    }
    HRESULT SetTotal(UInt64 size) override {
        return ctx.report({}, size);
    }
    HRESULT SetCompleted(const UInt64* v) override {
        return ctx.report(v ? std::optional<std::uint64_t>(*v) : std::nullopt);
    }
    HRESULT SetRatioInfo(const UInt64*, const UInt64*) override {
        return ctx.check();
    }
    HRESULT CheckBreak() override {
        return ctx.check();
    }
    HRESULT SetNumItems(const CArcToDoStat&) override {
        return ctx.check();
    }
    HRESULT GetStream(const wchar_t* name, bool, bool, UInt32) override {
        return ctx.item(utf8(name));
    }
    HRESULT OpenFileError(const FString& path, DWORD err) override {
        ctx.result.errors.push_back(std::string(path.Ptr()) + ": " + std::system_category().message(err));
        return E_FAIL;
    }
    HRESULT ReadingFileError(const FString& path, DWORD err) override {
        return OpenFileError(path, err);
    }
    HRESULT SetOperationResult(Int32 code) override {
        return ctx.memberResult(code);
    }
    HRESULT ReportExtractResult(Int32 code, Int32 encrypted, const wchar_t* name) override {
        return ctx.memberResult(code, utf8(name), encrypted != 0);
    }
    HRESULT ReportUpdateOperation(UInt32, const wchar_t* name, bool) override {
        return ctx.item(utf8(name));
    }
    HRESULT CryptoGetTextPassword2(Int32* defined, BSTR* p) override {
        *defined = ctx.passwordDefined ? 1 : 0;
        return ctx.getPassword(p, false);
    }
    HRESULT CryptoGetTextPassword(BSTR* p) override {
        return ctx.getPassword(p);
    }
    HRESULT ShowDeleteFile(const wchar_t* name, bool) override {
        return ctx.item(utf8(name));
    }
    HRESULT OpenResult(const CCodecs*, const CArchiveLink&, const wchar_t*, HRESULT r) override {
        return r;
    }
    HRESULT StartScanning() override {
        return ctx.phase("Scanning");
    }
    HRESULT FinishScanning(const CDirItemsStat&) override {
        return ctx.check();
    }
    HRESULT StartOpenArchive(const wchar_t* name) override {
        return ctx.phase("Opening", utf8(name));
    }
    HRESULT StartArchive(const wchar_t* name, bool) override {
        return ctx.phase("Compressing", utf8(name));
    }
    HRESULT FinishArchive(const CFinishArchiveStat&) override {
        return ctx.check();
    }
    HRESULT DeletingAfterArchiving(const FString& path, bool) override {
        return ctx.item(path.Ptr());
    }
    HRESULT FinishDeletingAfterArchiving() override {
        return ctx.check();
    }
    HRESULT MoveArc_Start(const wchar_t*, const wchar_t* dest, UInt64 size, Int32) override {
        ctx.phase("Committing", utf8(dest));
        return ctx.report({}, size);
    }
    HRESULT MoveArc_Progress(UInt64 total, UInt64 current) override {
        return ctx.report(current, total);
    }
    HRESULT MoveArc_Finish() override {
        return ctx.check();
    }
    HRESULT ScanError(const FString& path, DWORD err) override {
        return OpenFileError(path, err);
    }
    HRESULT ScanProgress(const CDirItemsStat&, const FString& path, bool) override {
        return ctx.item(path.Ptr());
    }
    HRESULT M7Arc_PrepareDirItems(CDirItems& items) override {
        if (ctx.sourceMappings.empty())
            return S_OK;
        for (unsigned i = 0; i < items.Items.Size(); ++i) {
            const auto found = ctx.sourceMappings.find(items.GetPhyPath(i).Ptr());
            if (found == ctx.sourceMappings.end())
                continue;
            NWindows::NFile::NFind::CFileInfo source;
            if (!source.Find(FString(found->second.c_str())))
                return HRESULT_FROM_WIN32(errno);
            auto& item = items.Items[i];
            item.uid = source.uid;
            item.gid = source.gid;
            item.mode = source.mode;
            item.PhyParent = static_cast<int>(
                items.AddPrefix(-1, -1, wide(fs::path(found->second).parent_path().string() + "/")));
            if (item.IsDir()) {
                item.MTime = source.MTime;
                item.ATime = source.ATime;
                item.CTime = source.CTime;
            }
        }
        if (items.StoreOwnerName) {
            items.OwnerNameMap.Numbers.Clear();
            items.OwnerNameMap.Strings.Clear();
            items.OwnerGroupMap.Numbers.Clear();
            items.OwnerGroupMap.Strings.Clear();
            return items.FillDeviceSizes();
        }
        return ctx.check();
    }
};
std::string propertyName(PROPID id) {
    switch (id) {
    case kpidPath:
        return "Path";
    case kpidName:
        return "Name";
    case kpidExtension:
        return "Extension";
    case kpidIsDir:
        return "Directory";
    case kpidSize:
        return "Size";
    case kpidPackSize:
        return "Packed size";
    case kpidAttrib:
        return "Attributes";
    case kpidCTime:
        return "Created";
    case kpidMTime:
        return "Modified";
    case kpidATime:
        return "Accessed";
    case kpidEncrypted:
        return "Encrypted";
    case kpidCRC:
        return "CRC";
    case kpidMethod:
        return "Method";
    case kpidBlock:
        return "Block";
    case kpidPosixAttrib:
        return "POSIX permissions";
    case kpidHostOS:
        return "Host OS";
    case kpidComment:
        return "Comment";
    case kpidPhySize:
        return "Physical size";
    case kpidHeadersSize:
        return "Headers size";
    case kpidSolid:
        return "Solid";
    case kpidNumBlocks:
        return "Blocks";
    case kpidNumVolumes:
        return "Volumes";
    case kpidWarning:
        return "Warning";
    case kpidError:
        return "Error";
    case kpidOffset:
        return "Offset";
    case kpidSymLink:
        return "Symbolic link";
    case kpidHardLink:
        return "Hard link";
    case kpidUser:
        return "User";
    case kpidGroup:
        return "Group";
    case kpidUserId:
        return "User ID";
    case kpidGroupId:
        return "Group ID";
    case kpidIsAnti:
        return "Anti item";
    default:
        return "Property " + std::to_string(id);
    }
}
std::string propertyValue(const PROPVARIANT& value, PROPID id) {
    UString s;
    ConvertPropertyToString2(s, value, id);
    return utf8(s);
}
std::uint64_t integer(const PROPVARIANT& p) {
    switch (p.vt) {
    case VT_UI8:
        return p.uhVal.QuadPart;
    case VT_UI4:
        return p.ulVal;
    case VT_UI2:
        return p.uiVal;
    case VT_UI1:
        return p.bVal;
    default:
        return 0;
    }
}
std::string canonicalFormat(std::string f) {
    std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return std::tolower(c); });
    return f;
}
struct TemporaryFile {
    fs::path path;
    explicit TemporaryFile(const fs::path& directory, const std::string& suffix = {}) {
        fs::create_directories(directory);
        std::string name = (directory / (".m7arc-XXXXXX" + suffix)).string();
        std::vector<char> buf(name.begin(), name.end());
        buf.push_back(0);
        const int fd = mkstemps(buf.data(), static_cast<int>(suffix.size()));
        if (fd < 0)
            throw EngineError("Cannot create temporary output in " + directory.string());
        close(fd);
        path = buf.data();
    }
    ~TemporaryFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};
struct TemporaryDirectory {
    fs::path path;
    explicit TemporaryDirectory(const fs::path& parent) {
        fs::create_directories(parent);
        std::string pattern = (parent / ".m7arc-XXXXXX").string();
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back(0);
        const char* created = mkdtemp(name.data());
        if (!created)
            throw EngineError("Cannot create temporary directory in " + parent.string());
        path = created;
    }
    ~TemporaryDirectory() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
std::string metadataPath(const std::string& member) {
    fs::path p = fs::path(member).relative_path();
    return (fs::path("__MACOSX") / p.parent_path() / ("._" + p.filename().string())).generic_string();
}
std::optional<std::string> metadataTarget(const std::string& member) {
    const auto relative = fs::path(member).relative_path().generic_string();
    fs::path p(relative);
    const auto name = p.filename().string();
    if (!relative.starts_with("__MACOSX/") || !name.starts_with("._"))
        return {};
    const fs::path withoutPrefix = fs::path(relative.substr(9));
    return (withoutPrefix.parent_path() / name.substr(2)).generic_string();
}
void addPatterns(NWildcard::CCensorNode& node, const std::vector<std::string>& patterns) {
    for (const auto& pattern : patterns) {
        NWildcard::CItem item;
        item.Recursive = true;
        item.ForFile = true;
        item.ForDir = true;
        item.WildcardMatching = true;
        SplitPathToParts(wide(pattern), item.PathParts);
        node.AddItem(true, item);
    }
}
bool matchesPattern(const NWildcard::CCensorNode& node, const fs::path& relative, bool file) {
    UStringVector parts;
    SplitPathToParts(wide(relative.generic_string()), parts);
    bool include = false;
    return node.CheckPathVect(parts, file, include) && include;
}
struct SourceState {
    fs::path path;
    struct stat info;
};
std::vector<SourceState> sourceStates(const std::vector<std::string>& inputs, const CreateOptions& options) {
    std::vector<SourceState> states;
    NWildcard::CCensorNode include, exclude;
    addPatterns(include, options.includePatterns);
    addPatterns(exclude, options.excludePatterns);
    for (const auto& input : inputs) {
        const auto source = fs::absolute(input).lexically_normal();
        std::vector<fs::path> members{source};
        if (fs::is_directory(fs::symlink_status(source)))
            for (const auto& e : fs::recursive_directory_iterator(source))
                members.push_back(e.path());
        for (const auto& member : members) {
            const auto relative =
                fs::path(options.archivePrefix) / (options.pathMode == ArchivePathMode::Relative
                                                       ? member.lexically_relative(source.parent_path())
                                                       : member.relative_path());
            const bool file = !fs::is_directory(fs::symlink_status(member));
            if (matchesPattern(exclude, relative, file))
                continue;
            if (file && !options.includePatterns.empty() && !matchesPattern(include, relative, true))
                continue;
            SourceState state{member, {}};
            if (lstat(member.c_str(), &state.info) != 0)
                throw EngineError("Cannot read input attributes: " + member.string());
            states.push_back(state);
        }
    }
    std::sort(states.begin(), states.end(), [](const auto& a, const auto& b) {
        return std::distance(a.path.begin(), a.path.end()) > std::distance(b.path.begin(), b.path.end());
    });
    return states;
}
void deleteSources(const std::vector<SourceState>& inputs, OperationResult& result) {
    for (const auto& source : inputs) {
        struct stat now{};
        if (lstat(source.path.c_str(), &now) != 0) {
            if (errno != ENOENT)
                result.errors.push_back(source.path.string() + ": cannot inspect source before deletion");
            continue;
        }
        const auto& before = source.info;
        const bool directory = S_ISDIR(before.st_mode);
        if (now.st_dev != before.st_dev || now.st_ino != before.st_ino || now.st_mode != before.st_mode ||
            (!directory &&
             (now.st_size != before.st_size || now.st_mtimespec.tv_sec != before.st_mtimespec.tv_sec ||
              now.st_mtimespec.tv_nsec != before.st_mtimespec.tv_nsec ||
              now.st_ctimespec.tv_sec != before.st_ctimespec.tv_sec ||
              now.st_ctimespec.tv_nsec != before.st_ctimespec.tv_nsec))) {
            result.errors.push_back(source.path.string() +
                                    ": source changed while archiving and was retained");
            continue;
        }
        std::error_code ec;
        fs::remove(source.path, ec);
        if (ec && !(directory && ec == std::errc::directory_not_empty))
            result.errors.push_back(source.path.string() + ": " + ec.message());
    }
}
void addProperty(CObjectVector<CProperty>& properties, const std::string& name, const std::string& value) {
    auto& p = properties.AddNew();
    p.Name = wide(name);
    p.Value = wide(value);
}
} // namespace

struct ArchiveEngine::Impl {
    CCodecs* codecs = new CCodecs;
    CMyComPtr<IUnknown> reference = codecs;
    Impl() {
        require(codecs->Load(), "Load 7-Zip formats");
        Codecs_AddHashArcHandler(codecs);
    }
    void open(CArchiveLink& link, const std::string& path, const OpenOptions& options,
              OperationContext& ctx) {
        const auto absolutePath = fs::absolute(path).lexically_normal();
        for (const auto& [target, password] : ctx.callbacks.passwordOverrides)
            if (fs::absolute(target).lexically_normal() == absolutePath) {
                ctx.password = password;
                ctx.passwordDefined = true;
                break;
            }
        require(ctx.phase("Opening", path), "Open cancelled");
        COpenOptions op;
        op.codecs = codecs;
        op.filePath = wide(path);
        op.props = nullptr;
        CObjectVector<COpenType> types;
        CIntVector excluded;
        op.types = &types;
        op.excludedFormats = &excluded;
        if (!options.type.empty()) {
            if (!ParseOpenTypes(*codecs, wide(options.type), types))
                throw EngineError("Unknown archive type: " + options.type);
            op.types = &types;
        }
        OpenCallback callback(ctx);
        const HRESULT hr = link.Open3(op, &callback);
        if (hr != S_OK) {
            std::string detail = utf8(link.NonOpen_ErrorInfo.ErrorMessage);
            if (!ctx.result.errors.empty())
                detail = ctx.result.errors.front();
            if (ctx.result.cancelled)
                throw EngineError("Operation cancelled", EngineErrorKind::Cancelled);
            const auto kind = ctx.passwordRequested
                                  ? (ctx.passwordDefined ? EngineErrorKind::PasswordOrCorruption
                                                         : EngineErrorKind::PasswordRequired)
                                  : EngineErrorKind::General;
            if (kind == EngineErrorKind::PasswordOrCorruption && detail.empty())
                detail = "Incorrect password or damaged encrypted headers";
            throw EngineError("Cannot open " + path + ": " + (detail.empty() ? errorCode(hr) : detail), kind);
        }
        const auto extension = canonicalFormat(fs::path(path).filename().string());
        const bool tarStream = extension.ends_with(".tar.gz") || extension.ends_with(".tgz") ||
                               extension.ends_with(".tar.bz2") || extension.ends_with(".tbz2") ||
                               extension.ends_with(".tbz") || extension.ends_with(".tar.xz") ||
                               extension.ends_with(".txz");
        const auto currentFormat =
            canonicalFormat(utf8(codecs->GetFormatNamePtr(link.GetArc()->FormatIndex)));
        if (options.type.empty() && tarStream &&
            (currentFormat == "gzip" || currentFormat == "bzip2" || currentFormat == "xz")) {
            std::string pattern = (fs::temp_directory_path() / "m7arc-open-XXXXXX").string();
            std::vector<char> buffer(pattern.begin(), pattern.end());
            buffer.push_back(0);
            const char* created = mkdtemp(buffer.data());
            if (!created)
                throw EngineError("Cannot create archive browsing directory");
            fs::path directory(created);
            ctx.temporaryDirectories.push_back(directory);
            link.Release();
            ExtractOptions extraction;
            extraction.password = ctx.password;
            extraction.overwrite = Overwrite::Replace;
            const auto result = extract(path, directory.string(), {}, extraction,
                                        {ctx.password, currentFormat}, false, ctx.callbacks);
            if (!result.ok())
                throw EngineError(result.cancelled ? "Operation cancelled"
                                                   : (result.errors.empty() ? "Cannot open compressed TAR"
                                                                            : result.errors.front()));
            fs::path payload;
            for (const auto& entry : fs::directory_iterator(directory))
                if (entry.is_regular_file()) {
                    payload = entry.path();
                    break;
                }
            if (payload.empty())
                throw EngineError("Compressed TAR has no payload");
            open(link, payload.string(), {ctx.password, "tar"}, ctx);
            link.Arcs.Back().IsReadOnly = true;
        }
    }
    OperationResult extract(const std::string& path, const std::string& destination,
                            const std::vector<std::uint32_t>& indices, const ExtractOptions& options,
                            const OpenOptions& openOptions, bool testing, const Callbacks& callbacks) {
        OperationContext ctx(callbacks, options.password);
        CArchiveLink link;
        open(link, path, openOptions, ctx);
        const CArc& arc = *link.GetArc();
        UInt32 count = 0;
        require(arc.Archive->GetNumberOfItems(&count), "Read item count");
        const bool hashArchive = arc.FormatIndex >= 0 && codecs->Formats[arc.FormatIndex].Flags_HashHandler();
        if (hashArchive && !testing)
            throw EngineError(
                "A checksum manifest contains names and digests. Use Test to verify its files.");
        CRecordVector<UInt32> selected, metadataIndices;
        std::set<UInt32> ordered(indices.begin(), indices.end());
        if (indices.empty())
            for (UInt32 i = 0; i < count; ++i)
                ordered.insert(i);
        for (auto index : ordered)
            if (index >= count)
                throw EngineError("Archive selection is no longer valid");
        std::vector<std::string> memberPaths(count);
        std::set<std::string> selectedNames;
        for (UInt32 i = 0; i < count; ++i) {
            UString name;
            require(arc.GetItem_Path(i, name), "Read member path");
            memberPaths[i] = utf8(name);
            if (ordered.contains(i))
                selectedNames.insert(fs::path(memberPaths[i]).relative_path().generic_string());
        }
        if (options.restoreMacMetadata && !testing) {
            for (UInt32 i = 0; i < count; ++i) {
                const auto target = metadataTarget(memberPaths[i]);
                if (target) {
                    if (selectedNames.contains(*target))
                        metadataIndices.Add(i);
                    ordered.erase(i);
                } else if (memberPaths[i] == "__MACOSX" || memberPaths[i].starts_with("__MACOSX/")) {
                    CPropVariant dir;
                    arc.Archive->GetProperty(i, kpidIsDir, &dir);
                    if (dir.vt == VT_BOOL && dir.boolVal != VARIANT_FALSE)
                        ordered.erase(i);
                }
            }
        }
        for (auto index : ordered)
            selected.Add(index);
        fs::path output = destination.empty() ? fs::path(".") : fs::path(destination);
        if (options.createArchiveFolder)
            output /= fs::path(path).stem();
        if (!testing)
            fs::create_directories(output);
        UStringVector removeParts;
        if (options.pathMode == PathMode::Current)
            SplitPathToParts(wide(options.currentDirectory), removeParts);
        if (options.eliminateDuplicateRoot && options.pathMode == PathMode::Full && !ordered.empty()) {
            std::optional<std::string> root;
            bool common = true;
            for (auto i : ordered) {
                const auto name = fs::path(memberPaths[i]).relative_path().generic_string();
                const auto slash = name.find('/');
                if (slash == std::string::npos) {
                    CPropVariant dir;
                    arc.Archive->GetProperty(i, kpidIsDir, &dir);
                    if (dir.vt != VT_BOOL || dir.boolVal == VARIANT_FALSE) {
                        common = false;
                        break;
                    }
                }
                const auto prefix = name.substr(0, slash);
                if (!root)
                    root = prefix;
                else if (*root != prefix) {
                    common = false;
                    break;
                }
            }
            if (common && root && !root->empty())
                removeParts.Add(wide(*root));
        }
        auto* callback = new ExtractCallback(ctx);
        CMyComPtr<IFolderArchiveExtractCallback> callbackRef = callback;
        auto* extract = new CArchiveExtractCallback;
        CMyComPtr<IArchiveExtractCallback> extractRef = extract;
        const auto pathMode = options.pathMode == PathMode::Flat       ? NExtract::NPathMode::kNoPaths
                              : options.pathMode == PathMode::Current  ? NExtract::NPathMode::kCurPaths
                              : options.pathMode == PathMode::Absolute ? NExtract::NPathMode::kAbsPaths
                                                                       : NExtract::NPathMode::kFullPaths;
        extract->InitForMulti(false, pathMode,
                              static_cast<NExtract::NOverwriteMode::EEnum>(options.overwrite),
                              NExtract::NZoneIdMode::kNone, false);
        extract->InitBeforeNewArchive();
        CExtractNtOptions nt;
        if (hashArchive)
            extract->DirPathPrefix_for_HashFiles =
                FString((fs::absolute(path).parent_path().string() + "/").c_str());
        std::string directory = output.string();
        if (directory.back() != '/')
            directory += '/';
        extract->Init(nt, nullptr, &arc, callback, false, testing, FString(directory.c_str()), removeParts,
                      false, arc.GetEstmatedPhySize());
        CArchiveExtractCallback_Closer closer(extract);
        if (!testing)
            require(extract->PrepareHardLinks(&selected), "Prepare links");
        ctx.phase(testing ? "Testing" : "Extracting");
        HRESULT hr = arc.Archive->Extract(selected.ConstData(), selected.Size(), testing ? 1 : 0, extract);
        const HRESULT closeResult = closer.Close();
        if (hr == S_OK)
            hr = closeResult;
        ctx.finish(hr);
        if (!testing && !ctx.result.cancelled) {
            std::map<std::string, std::vector<fs::path>> written;
            for (unsigned i = 0; i < extract->M7Arc_WrittenIndices.Size(); ++i)
                written
                    [fs::path(memberPaths[extract->M7Arc_WrittenIndices[i]]).relative_path().generic_string()]
                        .emplace_back(extract->M7Arc_WrittenPaths[i].Ptr());
            if (options.restoreMacMetadata && !metadataIndices.IsEmpty()) {
                TemporaryDirectory metadataDirectory(fs::temp_directory_path());
                auto* metadataExtract = new CArchiveExtractCallback;
                CMyComPtr<IArchiveExtractCallback> metadataRef = metadataExtract;
                metadataExtract->InitForMulti(false, NExtract::NPathMode::kFullPaths,
                                              NExtract::NOverwriteMode::kOverwrite,
                                              NExtract::NZoneIdMode::kNone, false);
                metadataExtract->InitBeforeNewArchive();
                UStringVector noRemoval;
                metadataExtract->Init(nt, nullptr, &arc, callback, false, false,
                                      FString((metadataDirectory.path.string() + "/").c_str()), noRemoval,
                                      false, arc.GetEstmatedPhySize());
                CArchiveExtractCallback_Closer metadataCloser(metadataExtract);
                ctx.phase("Restoring Mac metadata");
                HRESULT metadataResult = arc.Archive->Extract(metadataIndices.ConstData(),
                                                              metadataIndices.Size(), 0, metadataExtract);
                const HRESULT metadataClose = metadataCloser.Close();
                if (metadataResult == S_OK)
                    metadataResult = metadataClose;
                ctx.finish(metadataResult);
                for (unsigned i = 0; i < metadataExtract->M7Arc_WrittenIndices.Size(); ++i) {
                    const auto sidecar = fs::path(metadataExtract->M7Arc_WrittenPaths[i].Ptr());
                    const auto member = metadataTarget(memberPaths[metadataExtract->M7Arc_WrittenIndices[i]]);
                    if (!member || !mac::isAppleDouble(sidecar))
                        continue;
                    const auto found = written.find(*member);
                    if (found == written.end())
                        continue;
                    for (const auto& target : found->second)
                        try {
                            mac::unpackMetadata(sidecar, target);
                        } catch (const std::exception& e) {
                            ctx.result.errors.push_back(target.string() + ": " + e.what());
                        }
                }
            }
            const ssize_t quarantineSize =
                getxattr(path.c_str(), "com.apple.quarantine", nullptr, 0, 0, XATTR_NOFOLLOW);
            if (quarantineSize > 0) {
                std::vector<char> quarantine(static_cast<std::size_t>(quarantineSize));
                if (getxattr(path.c_str(), "com.apple.quarantine", quarantine.data(), quarantine.size(), 0,
                             XATTR_NOFOLLOW) == quarantineSize)
                    for (const auto& [name, paths] : written)
                        for (const auto& target : paths)
                            if (setxattr(target.c_str(), "com.apple.quarantine", quarantine.data(),
                                         quarantine.size(), 0, XATTR_NOFOLLOW) != 0)
                                ctx.result.errors.push_back(target.string() +
                                                            ": cannot preserve quarantine attribute");
            }
            ctx.result.outputPaths.push_back(output.string());
        }
        return ctx.result;
    }
};
ArchiveEngine::ArchiveEngine() : impl_(std::make_unique<Impl>()) {}
ArchiveEngine::~ArchiveEngine() = default;
std::string ArchiveEngine::version() {
    return MY_VERSION;
}
std::vector<FormatInfo> ArchiveEngine::formats() const {
    std::vector<FormatInfo> result;
    for (unsigned i = 0; i < impl_->codecs->Formats.Size(); ++i) {
        const auto& a = impl_->codecs->Formats[i];
        FormatInfo f;
        f.name = utf8(a.Name);
        f.canWrite = a.UpdateEnabled;
        f.isHash = a.Flags_HashHandler();
        for (unsigned j = 0; j < a.Exts.Size(); ++j)
            f.extensions.push_back(utf8(a.Exts[j].Ext));
        result.push_back(std::move(f));
    }
    return result;
}
std::vector<CodecInfo> ArchiveEngine::codecs() const {
    std::vector<CodecInfo> result;
    for (unsigned i = 0; i < g_NumCodecs; ++i) {
        const auto& c = *g_Codecs[i];
        result.push_back({c.Name, c.Id, c.CreateEncoder != nullptr, c.CreateDecoder != nullptr, c.IsFilter});
    }
    return result;
}
std::vector<HasherInfo> ArchiveEngine::hashers() const {
    std::vector<HasherInfo> result;
    for (unsigned i = 0; i < g_NumHashers; ++i) {
        const auto& h = *g_Hashers[i];
        result.push_back({h.Name, h.Id, h.DigestSize});
    }
    return result;
}
ArchiveSnapshot ArchiveEngine::list(const std::string& path, const OpenOptions& options,
                                    const Callbacks& callbacks) {
    OperationContext ctx(callbacks, options.password);
    CArchiveLink link;
    impl_->open(link, path, options, ctx);
    const CArc& arc = *link.GetArc();
    ArchiveSnapshot snapshot;
    snapshot.path = path;
    snapshot.format = utf8(impl_->codecs->GetFormatNamePtr(arc.FormatIndex));
    snapshot.canUpdate = !arc.IsReadOnly && link.Arcs.Size() == 1 && arc.FormatIndex >= 0 &&
                         impl_->codecs->Formats[arc.FormatIndex].UpdateEnabled;
    snapshot.canSetComment = snapshot.canUpdate && impl_->codecs->Formats[arc.FormatIndex].Is_Zip();
    for (unsigned i = 0; i < link.VolumePaths.Size(); ++i)
        snapshot.volumes.push_back(utf8(link.VolumePaths[i]));
    UInt32 count = 0;
    require(arc.Archive->GetNumberOfItems(&count), "Read item count");
    UInt32 propCount = 0;
    require(arc.Archive->GetNumberOfProperties(&propCount), "Read property count");
    std::vector<std::pair<PROPID, std::string>> fields;
    for (UInt32 p = 0; p < propCount; ++p) {
        BSTR name = nullptr;
        PROPID id = 0;
        VARTYPE type = 0;
        require(arc.Archive->GetPropertyInfo(p, &name, &id, &type), "Read property schema");
        fields.emplace_back(id, name ? utf8(name) : propertyName(id));
        SysFreeString(name);
    }
    for (UInt32 i = 0; i < count; ++i) {
        require(ctx.check(), "List cancelled");
        ArchiveEntry e;
        e.index = i;
        UString name;
        require(arc.GetItem_Path(i, name), "Read path");
        e.path = utf8(name);
        for (const auto& [id, label] : fields) {
            CPropVariant v;
            require(arc.Archive->GetProperty(i, id, &v), "Read member property");
            if (v.vt == VT_EMPTY)
                continue;
            const auto value = propertyValue(v, id);
            e.properties.push_back({id, label, value});
            switch (id) {
            case kpidSize:
                e.size = integer(v);
                break;
            case kpidPackSize:
                e.packedSize = integer(v);
                break;
            case kpidIsDir:
                e.isDirectory = v.vt == VT_BOOL && v.boolVal != VARIANT_FALSE;
                break;
            case kpidEncrypted:
                e.encrypted = v.vt == VT_BOOL && v.boolVal != VARIANT_FALSE;
                break;
            case kpidIsAltStream:
                e.alternateStream = v.vt == VT_BOOL && v.boolVal != VARIANT_FALSE;
                break;
            case kpidMethod:
                e.method = value;
                break;
            case kpidAttrib:
            case kpidPosixAttrib:
                e.attributes = value;
                break;
            case kpidMTime:
                if (v.vt == VT_FILETIME)
                    e.modifiedUnixSeconds =
                        static_cast<std::int64_t>(
                            (static_cast<std::uint64_t>(v.filetime.dwHighDateTime) << 32) |
                            v.filetime.dwLowDateTime) /
                            10000000LL -
                        11644473600LL;
                break;
            default:
                break;
            }
        }
        CPropVariant directory;
        require(arc.Archive->GetProperty(i, kpidIsDir, &directory), "Read member type");
        e.isDirectory = directory.vt == VT_BOOL && directory.boolVal != VARIANT_FALSE;
        CPropVariant alternate;
        if (arc.Archive->GetProperty(i, kpidIsAltStream, &alternate) == S_OK)
            e.alternateStream = alternate.vt == VT_BOOL && alternate.boolVal != VARIANT_FALSE;
        snapshot.entries.push_back(std::move(e));
    }
    require(arc.Archive->GetNumberOfArchiveProperties(&propCount), "Read archive properties");
    for (UInt32 p = 0; p < propCount; ++p) {
        BSTR name = nullptr;
        PROPID id = 0;
        VARTYPE type = 0;
        require(arc.Archive->GetArchivePropertyInfo(p, &name, &id, &type), "Read archive schema");
        CPropVariant v;
        require(arc.Archive->GetArchiveProperty(id, &v), "Read archive property");
        if (v.vt != VT_EMPTY)
            snapshot.properties.push_back({id, name ? utf8(name) : propertyName(id), propertyValue(v, id)});
        SysFreeString(name);
    }
    return snapshot;
}
OperationResult ArchiveEngine::extract(const std::string& path, const std::string& destination,
                                       const std::vector<std::uint32_t>& indices,
                                       const ExtractOptions& options, const Callbacks& callbacks) {
    return impl_->extract(path, destination, indices, options, {options.password, {}}, false, callbacks);
}
OperationResult ArchiveEngine::test(const std::string& path, const std::vector<std::uint32_t>& indices,
                                    const OpenOptions& options, const Callbacks& callbacks) {
    ExtractOptions extractOptions;
    extractOptions.password = options.password;
    return impl_->extract(path, "", indices, extractOptions, options, true, callbacks);
}

OperationResult ArchiveEngine::create(const std::vector<std::string>& inputs, const std::string& destination,
                                      const CreateOptions& options, const Callbacks& callbacks) {
    if (inputs.empty())
        throw EngineError("Select at least one file or folder");
    const auto format = canonicalFormat(options.format);
    const auto absoluteDestination = fs::absolute(destination).lexically_normal();
    for (const auto& input : inputs) {
        const auto source = fs::absolute(input).lexically_normal();
        if (fs::is_directory(fs::symlink_status(source))) {
            const auto relative = absoluteDestination.lexically_relative(source);
            if (!relative.empty() && *relative.begin() != "..")
                throw EngineError("The output archive must be outside the selected input folder");
        }
    }
    const auto originals =
        options.deleteAfterCompressing ? sourceStates(inputs, options) : std::vector<SourceState>();
    if (format == "tar.gz" || format == "tar.bz2" || format == "tar.xz") {
        if (fs::exists(destination))
            throw EngineError("TAR combination updates require creating a new destination archive");
        TemporaryDirectory staging(fs::absolute(destination).parent_path());
        const auto tar = staging.path / fs::path(destination).stem();
        CreateOptions inner;
        inner.format = "tar";
        inner.storeSymbolicLinks = options.storeSymbolicLinks;
        inner.storeHardLinks = options.storeHardLinks;
        inner.preserveMacMetadata = options.preserveMacMetadata;
        inner.archivePrefix = options.archivePrefix;
        inner.pathMode = options.pathMode;
        inner.includePatterns = options.includePatterns;
        inner.excludePatterns = options.excludePatterns;
        inner.openSharedFiles = options.openSharedFiles;
        inner.setArchiveMTime = options.setArchiveMTime;
        inner.storeOwnerId = options.storeOwnerId;
        inner.storeOwnerName = options.storeOwnerName;
        inner.preserveAccessTime = options.preserveAccessTime;
        auto result = create(inputs, tar.string(), inner, callbacks);
        if (!result.ok())
            return result;
        CreateOptions outer = options;
        outer.format = format == "tar.gz" ? "gzip" : format == "tar.bz2" ? "bzip2" : "xz";
        outer.deleteAfterCompressing = false;
        outer.preserveMacMetadata = false;
        result = createRaw({tar.string()}, destination, outer, callbacks);
        if (result.ok() && options.deleteAfterCompressing)
            deleteSources(originals, result);
        return result;
    }
    const bool container = format == "7z" || format == "zip" || format == "tar" || format == "wim";
    const fs::path prefix = fs::path(options.archivePrefix).lexically_normal();
    if (prefix.is_absolute())
        throw EngineError("Archive destination must be relative");
    for (const auto& part : prefix)
        if (part == "..")
            throw EngineError("Archive destination cannot contain '..'");
    const bool filtering = !options.includePatterns.empty() || !options.excludePatterns.empty();
    if ((options.preserveMacMetadata || !options.archivePrefix.empty() || filtering ||
         options.pathMode != ArchivePathMode::Relative) &&
        container) {
        TemporaryDirectory staging(fs::absolute(destination).parent_path());
        const fs::path data = staging.path / "data";
        fs::create_directories(data);
        OperationContext ctx(callbacks);
        require(ctx.phase("Preserving Mac metadata"), "Compression cancelled");
        std::vector<std::string> stagedInputs;
        std::vector<std::pair<std::string, std::string>> sourceMappings;
        std::map<std::pair<dev_t, ino_t>, fs::path> hardLinks;
        NWildcard::CCensorNode includeFilter, excludeFilter;
        addPatterns(includeFilter, options.includePatterns);
        addPatterns(excludeFilter, options.excludePatterns);
        for (const auto& input : inputs) {
            fs::path source = fs::absolute(input).lexically_normal();
            const auto name =
                options.pathMode == ArchivePathMode::Relative ? source.filename() : source.relative_path();
            const auto copied = data / prefix / name;
            fs::create_directories(copied.parent_path());
            if (fs::exists(copied) || fs::is_symlink(copied))
                throw EngineError("Multiple selected inputs map to the same archive path: " + name.string());
            mac::copyItem(source, copied, [&](std::uint64_t bytes) { return ctx.report(bytes) == S_OK; });
            std::vector<fs::path> members{source};
            if (fs::is_directory(fs::symlink_status(source)))
                for (const auto& e : fs::recursive_directory_iterator(source))
                    members.push_back(e.path());
            std::set<fs::path> kept;
            for (const auto& member : members) {
                const auto relative = prefix / (options.pathMode == ArchivePathMode::Relative
                                                    ? member.lexically_relative(source.parent_path())
                                                    : member.relative_path());
                const bool file = !fs::is_directory(fs::symlink_status(member));
                if (matchesPattern(excludeFilter, relative, file))
                    continue;
                if (options.includePatterns.empty() || matchesPattern(includeFilter, relative, file)) {
                    fs::path p = member;
                    while (p != source.parent_path() && !p.empty()) {
                        kept.insert(p);
                        p = p.parent_path();
                    }
                }
            }
            for (auto it = members.rbegin(); it != members.rend(); ++it)
                if (!kept.contains(*it)) {
                    const auto relative = prefix / (options.pathMode == ArchivePathMode::Relative
                                                        ? it->lexically_relative(source.parent_path())
                                                        : it->relative_path());
                    fs::remove_all(data / relative);
                }
            if (!kept.contains(source))
                continue;
            stagedInputs.push_back(copied.string());
            for (const auto& member : members) {
                if (!kept.contains(member))
                    continue;
                require(ctx.item(member.string()), "Compression cancelled");
                const auto relative = prefix / (options.pathMode == ArchivePathMode::Relative
                                                    ? member.lexically_relative(source.parent_path())
                                                    : member.relative_path());
                sourceMappings.emplace_back((data / relative).string(), member.string());
                if (options.storeHardLinks) {
                    struct stat st{};
                    if (lstat(member.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink > 1) {
                        const auto key = std::make_pair(st.st_dev, st.st_ino);
                        const auto existing = hardLinks.find(key);
                        const auto clone = data / relative;
                        if (existing == hardLinks.end())
                            hardLinks.emplace(key, clone);
                        else {
                            fs::remove(clone);
                            fs::create_hard_link(existing->second, clone);
                        }
                    }
                }
                if (options.preserveMacMetadata) {
                    const auto sidecar = data / metadataPath(relative.generic_string());
                    fs::create_directories(sidecar.parent_path());
                    mac::packMetadata(member, sidecar);
                    stagedInputs.push_back(sidecar.string());
                }
            }
        }
        if (stagedInputs.empty())
            throw EngineError("No input files match the include/exclude patterns");
        CreateOptions staged = options;
        staged.preserveMacMetadata = false;
        staged.deleteAfterCompressing = false;
        staged.archivePrefix.clear();
        auto result = createRaw(stagedInputs, destination, staged, callbacks, data.string(), sourceMappings);
        if (result.ok() && options.deleteAfterCompressing)
            deleteSources(originals, result);
        return result;
    }
    CreateOptions raw = options;
    raw.deleteAfterCompressing = false;
    auto result = createRaw(inputs, destination, raw, callbacks);
    if (result.ok() && options.deleteAfterCompressing)
        deleteSources(originals, result);
    return result;
}
OperationResult
ArchiveEngine::createRaw(const std::vector<std::string>& inputs, const std::string& destination,
                         const CreateOptions& options, const Callbacks& callbacks,
                         const std::string& baseDirectory,
                         const std::vector<std::pair<std::string, std::string>>& sourceMappings) {
    if (inputs.empty())
        throw EngineError("Select at least one file or folder");
    const auto format = canonicalFormat(options.format);
    if ((format == "gzip" || format == "bzip2" || format == "xz") &&
        (inputs.size() != 1 || !fs::is_regular_file(inputs[0])))
        throw EngineError(
            "This stream format accepts one file. Select a TAR combination for folders or multiple files.");
    std::unique_ptr<TemporaryFile> archiveMetadata;
    std::optional<fs::perms> archivePermissions;
    if (fs::exists(destination)) {
        archiveMetadata = std::make_unique<TemporaryFile>(fs::absolute(destination).parent_path());
        mac::packMetadata(destination, archiveMetadata->path);
        archivePermissions = fs::status(destination).permissions();
    }
    OperationContext ctx(callbacks, options.password);
    ctx.sourceMappings.insert(sourceMappings.begin(), sourceMappings.end());
    OpenCallback openCallback(ctx);
    UpdateCallback callback(ctx);
    CUpdateOptions update;
    update.ArcNameMode = k_ArcNameMode_Exact;
    update.SetActionCommand_Add();
    switch (options.updateMode) {
    case UpdateMode::Add:
        break;
    case UpdateMode::Update:
        update.Commands[0].ActionSet = NUpdateArchive::k_ActionSet_Update;
        break;
    case UpdateMode::Freshen:
        update.Commands[0].ActionSet = NUpdateArchive::k_ActionSet_Fresh;
        break;
    case UpdateMode::Synchronize:
        update.Commands[0].ActionSet = NUpdateArchive::k_ActionSet_Sync;
        break;
    }
    const int index = impl_->codecs->FindOutFormatFromName(wide(format));
    if (index < 0)
        throw EngineError("Format does not support creation: " + options.format);
    CObjectVector<COpenType> types;
    COpenType type;
    type.FormatIndex = index;
    types.Add(type);
    update.MethodMode.Type = type;
    update.MethodMode.Type_Defined = true;
    update.SymLinks.SetVal_as_Defined(options.storeSymbolicLinks);
    update.HardLinks.SetVal_as_Defined(options.storeHardLinks);
    update.DeleteAfterCompressing = options.deleteAfterCompressing;
    update.PreserveATime = options.preserveAccessTime;
    update.StopAfterOpenError = true;
    update.OpenShareForWrite = options.openSharedFiles;
    update.SetArcMTime = options.setArchiveMTime;
    update.StoreOwnerId.SetVal_as_Defined(options.storeOwnerId);
    update.StoreOwnerName.SetVal_as_Defined(options.storeOwnerName);
    if (options.pathMode == ArchivePathMode::Absolute)
        update.AddPathPrefix = L"/";
    for (auto size : options.volumeSizes) {
        if (!size)
            throw EngineError("Volume size must be greater than zero");
        update.VolumesSizes.Add(size);
    }
    auto& props = update.MethodMode.Properties;
    if (format != "tar" && format != "wim")
        addProperty(props, "x", std::to_string(options.level));
    if (!options.method.empty()) {
        if (format == "tar")
            addProperty(props, "m", options.method);
        else if (format == "7z")
            addProperty(props, "0", options.method);
        else if (format != "wim")
            addProperty(props, "m", options.method);
    }
    const bool orderMode = canonicalFormat(options.method) == "ppmd";
    const auto namedMethod = canonicalFormat(options.method);
    const bool fixedDictionary = namedMethod == "deflate" || namedMethod == "deflate64";
    if (options.dictionaryBytes && !fixedDictionary)
        addProperty(props, (format == "7z" ? std::string("0") : std::string()) + (orderMode ? "mem" : "d"),
                    std::to_string(options.dictionaryBytes) + "b");
    if (options.wordSize)
        addProperty(props, (format == "7z" ? std::string("0") : std::string()) + (orderMode ? "o" : "fb"),
                    std::to_string(options.wordSize));
    if (options.threads && format != "tar" && format != "wim")
        addProperty(props, "mt", std::to_string(options.threads));
    if (format == "7z") {
        addProperty(props, "s",
                    options.solid ? (options.solidBlock.empty() ? "on" : options.solidBlock) : "off");
        addProperty(props, "he", options.encryptHeaders ? "on" : "off");
    }
    if (format == "xz" && !options.solidBlock.empty())
        addProperty(props, "s", options.solidBlock);
    if (format == "zip" && !options.encryptionMethod.empty())
        addProperty(props, "em", options.encryptionMethod);
    for (const auto& [name, value] : options.properties)
        addProperty(props, name, value);
    NWildcard::CCensor censor;
    for (const auto& input : inputs) {
        if (!fs::exists(input) && !fs::is_symlink(input))
            throw EngineError("Input does not exist: " + input);
        if (baseDirectory.empty())
            censor.AddPreItem_NoWildcard(wide(fs::absolute(input).lexically_normal().string()));
        else {
            if (censor.Pairs.IsEmpty())
                censor.Pairs.AddNew().Prefix = wide(baseDirectory + "/");
            NWildcard::CItem item;
            item.Recursive = false;
            item.ForFile = true;
            item.ForDir = true;
            item.WildcardMatching = false;
            SplitPathToParts(wide(fs::path(input).lexically_relative(baseDirectory).generic_string()),
                             item.PathParts);
            censor.Pairs[0].Head.AddItem(true, item);
        }
    }
    fs::create_directories(fs::absolute(destination).parent_path());
    CUpdateErrorInfo error;
    HRESULT hr;
    try {
        hr = UpdateArchive(impl_->codecs, types, wide(fs::absolute(destination).string()), censor, update,
                           error, &openCallback, &callback, true);
    } catch (const UString& s) {
        throw EngineError(utf8(s));
    } catch (const char* s) {
        throw EngineError(s);
    } catch (const wchar_t* s) {
        throw EngineError(utf8(s));
    }
    if (error.ThereIsError()) {
        std::string message = error.Message.Ptr();
        if (error.SystemError)
            message += " " + std::system_category().message(error.SystemError);
        for (unsigned i = 0; i < error.FileNames.Size(); ++i)
            message += " " + std::string(error.FileNames[i].Ptr());
        ctx.result.errors.push_back(message);
    }
    ctx.finish(hr);
    if (ctx.result.ok()) {
        if (archiveMetadata) {
            mac::unpackMetadata(archiveMetadata->path, destination);
            fs::permissions(destination, *archivePermissions);
        }
        if (options.volumeSizes.empty())
            ctx.result.outputPaths.push_back(destination);
        else
            for (unsigned i = 1;; ++i) {
                std::ostringstream number;
                number << '.' << std::setw(3) << std::setfill('0') << i;
                const auto p = destination + number.str();
                if (!fs::exists(p))
                    break;
                ctx.result.outputPaths.push_back(p);
            }
    }
    return ctx.result;
}

namespace {
class RewriteCallback final : public IArchiveUpdateCallback,
                              public ICryptoGetTextPassword,
                              public ICryptoGetTextPassword2,
                              public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(IArchiveUpdateCallback)
    Z7_COM_QI_ENTRY(ICryptoGetTextPassword)
    Z7_COM_QI_ENTRY(ICryptoGetTextPassword2)
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
  public:
    OperationContext& ctx;
    CMyComPtr<IInArchive> archive;
    std::vector<UInt32> items;
    std::vector<std::optional<std::string>> names;
    std::vector<std::optional<fs::path>> sources;
    std::optional<std::pair<UInt32, std::string>> comment;
    RewriteCallback(OperationContext& c, IInArchive* a) : ctx(c), archive(a) {}
    Z7_COM7F_IMF(SetTotal(UInt64 size)) override {
        return ctx.report({}, size);
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64* v)) override {
        return ctx.report(v ? std::optional<std::uint64_t>(*v) : std::nullopt);
    }
    Z7_COM7F_IMF(GetUpdateItemInfo(UInt32 i, Int32* newData, Int32* newProps, UInt32* index)) override {
        *newData = sources.at(i).has_value() ? 1 : 0;
        *newProps = (names.at(i).has_value() || sources.at(i).has_value() ||
                     (comment && items.at(i) == comment->first))
                        ? 1
                        : 0;
        *index = items.at(i);
        return ctx.check();
    }
    Z7_COM7F_IMF(GetProperty(UInt32 i, PROPID id, PROPVARIANT* v)) override {
        if (id == kpidComment && comment && items.at(i) == comment->first) {
            CPropVariant p(wide(comment->second));
            p.Detach(v);
            return S_OK;
        }
        if (id == kpidPath && names.at(i)) {
            CPropVariant p(wide(*names[i]));
            p.Detach(v);
            return S_OK;
        }
        if (sources.at(i)) {
            struct stat st{};
            if (stat(sources[i]->c_str(), &st) != 0)
                return HRESULT_FROM_WIN32(errno);
            CPropVariant p;
            switch (id) {
            case kpidSize:
                p = static_cast<UInt64>(st.st_size);
                break;
            case kpidIsDir:
                p = false;
                break;
            case kpidPosixAttrib:
                p = static_cast<UInt32>(st.st_mode);
                break;
            case kpidAttrib:
                p = static_cast<UInt32>((st.st_mode << 16) | 0x8000);
                break;
            case kpidMTime:
            case kpidATime:
            case kpidCTime: {
                const auto& t = id == kpidMTime   ? st.st_mtimespec
                                : id == kpidATime ? st.st_atimespec
                                                  : st.st_birthtimespec;
                const UInt64 ticks =
                    (static_cast<UInt64>(t.tv_sec) + 11644473600ULL) * 10000000ULL + t.tv_nsec / 100;
                FILETIME ft{static_cast<UInt32>(ticks), static_cast<UInt32>(ticks >> 32)};
                p = ft;
                break;
            }
            default:
                if (items[i] != (UInt32)-1)
                    return archive->GetProperty(items.at(i), id, v);
                break;
            }
            p.Detach(v);
            return S_OK;
        }
        return archive->GetProperty(items.at(i), id, v);
    }
    Z7_COM7F_IMF(GetStream(UInt32 i, ISequentialInStream** stream)) override {
        *stream = nullptr;
        if (!sources.at(i))
            return E_NOTIMPL;
        RINOK(ctx.item(sources[i]->string()))
        auto* input = new CInFileStream;
        CMyComPtr<ISequentialInStream> ref = input;
        if (!input->Open(sources[i]->c_str()))
            return HRESULT_FROM_WIN32(errno);
        *stream = ref.Detach();
        return S_OK;
    }
    Z7_COM7F_IMF(SetOperationResult(Int32 code)) override {
        return ctx.memberResult(code);
    }
    Z7_COM7F_IMF(CryptoGetTextPassword(BSTR* p)) override {
        return ctx.getPassword(p);
    }
    Z7_COM7F_IMF(CryptoGetTextPassword2(Int32* defined, BSTR* p)) override {
        *defined = ctx.passwordDefined ? 1 : 0;
        return ctx.getPassword(p, false);
    }
};
OperationResult rewriteArchive(CCodecs* codecs, CArchiveLink& link, const std::string& path,
                               const std::set<std::uint32_t>& remove,
                               std::optional<std::pair<std::uint32_t, std::string>> rename,
                               OperationContext& ctx,
                               std::optional<std::pair<std::uint32_t, std::string>> replacement = {},
                               std::optional<std::pair<std::uint32_t, std::string>> comment = {}) {
    const CArc& arc = *link.GetArc();
    if (link.Arcs.Size() != 1 || arc.IsReadOnly || arc.FormatIndex < 0 ||
        !codecs->Formats[arc.FormatIndex].UpdateEnabled)
        throw EngineError("This archive instance cannot be updated");
    if (!link.VolumePaths.IsEmpty())
        throw EngineError("Updating a split archive requires a new destination archive");
    CMyComPtr<IOutArchive> out;
    require(arc.Archive.QueryInterface(IID_IOutArchive, &out), "Archive does not support update");
    auto* callback = new RewriteCallback(ctx, arc.Archive);
    CMyComPtr<IArchiveUpdateCallback> ref = callback;
    callback->comment = comment;
    UInt32 count = 0;
    require(arc.Archive->GetNumberOfItems(&count), "Read item count");
    if ((!remove.empty() && *remove.rbegin() >= count) || (rename && rename->first >= count) ||
        (replacement && replacement->first >= count) || (comment && comment->first >= count))
        throw EngineError("Archive selection is no longer valid");
    std::vector<std::string> archivePaths(count);
    for (UInt32 i = 0; i < count; ++i) {
        UString p;
        require(arc.GetItem_Path(i, p), "Read member path");
        archivePaths[i] = utf8(p);
    }
    std::set<UInt32> allRemoved = remove;
    std::set<std::string> removedNames;
    for (auto index : remove) {
        removedNames.insert(fs::path(archivePaths[index]).relative_path().generic_string());
        CPropVariant dir;
        arc.Archive->GetProperty(index, kpidIsDir, &dir);
        if (dir.vt == VT_BOOL && dir.boolVal != VARIANT_FALSE)
            for (UInt32 i = 0; i < count; ++i)
                if (archivePaths[i].starts_with(archivePaths[index] + "/")) {
                    allRemoved.insert(i);
                    removedNames.insert(fs::path(archivePaths[i]).relative_path().generic_string());
                }
    }
    for (UInt32 i = 0; i < count; ++i) {
        const auto target = metadataTarget(archivePaths[i]);
        if (target && removedNames.contains(*target))
            allRemoved.insert(i);
    }
    std::string oldPrefix;
    if (rename) {
        UString p;
        require(arc.GetItem_Path(rename->first, p), "Read member path");
        oldPrefix = utf8(p);
        fs::path replacement(rename->second);
        if (replacement.empty() || replacement.is_absolute())
            throw EngineError("Enter a relative archive member path");
        for (const auto& part : replacement)
            if (part == "..")
                throw EngineError("An archive member path cannot contain '..'");
    }
    for (UInt32 i = 0; i < count; ++i) {
        if (allRemoved.contains(i))
            continue;
        callback->items.push_back(i);
        std::optional<std::string> newName;
        if (rename) {
            UString p;
            require(arc.GetItem_Path(i, p), "Read member path");
            auto original = utf8(p);
            if (i == rename->first)
                newName = rename->second;
            else if (original.starts_with(oldPrefix + "/"))
                newName = rename->second + original.substr(oldPrefix.size());
            else if (const auto target = metadataTarget(original); target) {
                const auto relativeOld = fs::path(oldPrefix).relative_path().generic_string();
                if (*target == relativeOld)
                    newName = metadataPath(rename->second);
                else if (target->starts_with(relativeOld + "/"))
                    newName = metadataPath(rename->second + target->substr(relativeOld.size()));
            }
        }
        callback->names.push_back(std::move(newName));
        callback->sources.emplace_back();
    }
    if (rename) {
        std::map<std::string, std::size_t> finalNames;
        for (std::size_t i = 0; i < callback->items.size(); ++i) {
            const auto name = callback->names[i].value_or(archivePaths[callback->items[i]]);
            const auto [existing, inserted] = finalNames.emplace(name, i);
            if (!inserted && (callback->names[i] || callback->names[existing->second]))
                throw EngineError("An archive member already exists at " + name);
        }
    }
    std::unique_ptr<TemporaryDirectory> metadataStaging;
    if (replacement) {
        if (!fs::is_regular_file(replacement->second))
            throw EngineError("The edited member must be a regular file");
        CPropVariant dir;
        arc.Archive->GetProperty(replacement->first, kpidIsDir, &dir);
        if (dir.vt == VT_BOOL && dir.boolVal != VARIANT_FALSE)
            throw EngineError("A folder cannot be replaced by a file");
        const auto memberPath = archivePaths[replacement->first];
        const auto sidecarName = metadataPath(memberPath);
        metadataStaging = std::make_unique<TemporaryDirectory>(fs::temp_directory_path());
        const auto sidecar = metadataStaging->path / "metadata";
        mac::packMetadata(replacement->second, sidecar);
        bool hasSidecar = false;
        for (std::size_t i = 0; i < callback->items.size(); ++i) {
            if (callback->items[i] == replacement->first)
                callback->sources[i] = replacement->second;
            if (fs::path(archivePaths[callback->items[i]]).relative_path().generic_string() == sidecarName) {
                callback->sources[i] = sidecar;
                hasSidecar = true;
            }
        }
        if (!hasSidecar) {
            callback->items.push_back((UInt32)-1);
            callback->names.push_back(sidecarName);
            callback->sources.push_back(sidecar);
        }
    }
    CObjectVector<CProperty> rewriteProperties;
    if (replacement) {
        CPropVariant encrypted;
        arc.Archive->GetProperty(replacement->first, kpidEncrypted, &encrypted);
        if (encrypted.vt == VT_BOOL && encrypted.boolVal != VARIANT_FALSE) {
            BSTR secret = nullptr;
            require(ctx.getPassword(&secret), "Password is required to replace an encrypted member");
            SysFreeString(secret);
            if (codecs->Formats[arc.FormatIndex].Is_Zip()) {
                CPropVariant method;
                arc.Archive->GetProperty(replacement->first, kpidMethod, &method);
                const auto name = propertyValue(method, kpidMethod);
                if (name.find("AES") != std::string::npos)
                    addProperty(rewriteProperties, "em",
                                name.find("128") != std::string::npos   ? "AES128"
                                : name.find("192") != std::string::npos ? "AES192"
                                                                        : "AES256");
            }
        }
    }
    if (codecs->Formats[arc.FormatIndex].Is_7z())
        addProperty(rewriteProperties, "he", link.PasswordWasAsked ? "on" : "off");
    if (!rewriteProperties.IsEmpty())
        require(SetProperties(out, rewriteProperties), "Preserve archive encryption");
    TemporaryFile temporary(fs::absolute(path).parent_path());
    auto* stream = new COutFileStream;
    CMyComPtr<IOutStream> streamRef = stream;
    if (!stream->Create_ALWAYS(temporary.path.c_str()))
        throw EngineError("Cannot create updated archive");
    ctx.phase("Updating", path);
    HRESULT hr = out->UpdateItems(stream, static_cast<UInt32>(callback->items.size()), callback);
    const HRESULT closeResult = stream->Close();
    if (hr == S_OK)
        hr = closeResult;
    streamRef.Release();
    ctx.finish(hr);
    if (ctx.result.ok()) {
        require(ctx.phase("Committing", path), "Update cancelled");
        TemporaryFile metadata(fs::absolute(path).parent_path());
        mac::packMetadata(path, metadata.path);
        mac::unpackMetadata(metadata.path, temporary.path);
        fs::permissions(temporary.path, fs::status(path).permissions());
        callback->archive.Release();
        out.Release();
        link.Release();
        fs::rename(temporary.path, path);
        ctx.result.outputPaths.push_back(path);
    }
    return ctx.result;
}
} // namespace
OperationResult ArchiveEngine::remove(const std::string& path, const std::vector<std::uint32_t>& indices,
                                      const OpenOptions& options, const Callbacks& callbacks) {
    if (indices.empty())
        throw EngineError("Select at least one archive member");
    OperationContext ctx(callbacks, options.password);
    CArchiveLink link;
    impl_->open(link, path, options, ctx);
    return rewriteArchive(impl_->codecs, link, path, {indices.begin(), indices.end()}, {}, ctx);
}
OperationResult ArchiveEngine::rename(const std::string& path, std::uint32_t index,
                                      const std::string& newPath, const OpenOptions& options,
                                      const Callbacks& callbacks) {
    OperationContext ctx(callbacks, options.password);
    CArchiveLink link;
    impl_->open(link, path, options, ctx);
    return rewriteArchive(impl_->codecs, link, path, {}, std::make_pair(index, newPath), ctx);
}
OperationResult ArchiveEngine::replace(const std::string& path, std::uint32_t index,
                                       const std::string& editedLocalFile, const OpenOptions& options,
                                       const Callbacks& callbacks) {
    OperationContext ctx(callbacks, options.password);
    CArchiveLink link;
    impl_->open(link, path, options, ctx);
    return rewriteArchive(impl_->codecs, link, path, {}, {}, ctx, std::make_pair(index, editedLocalFile));
}
OperationResult ArchiveEngine::setComment(const std::string& path, std::uint32_t index,
                                          const std::string& comment, const OpenOptions& options,
                                          const Callbacks& callbacks) {
    OperationContext ctx(callbacks, options.password);
    CArchiveLink link;
    impl_->open(link, path, options, ctx);
    if (!impl_->codecs->Formats[link.GetArc()->FormatIndex].Is_Zip())
        throw EngineError("This archive format does not support member comments");
    return rewriteArchive(impl_->codecs, link, path, {}, {}, ctx, {}, std::make_pair(index, comment));
}
std::vector<HashResult> ArchiveEngine::hash(const std::vector<std::string>& inputs,
                                            const std::vector<std::string>& algorithms,
                                            const Callbacks& callbacks) {
    if (algorithms.empty())
        throw EngineError("Select a checksum algorithm");
    OperationContext ctx(callbacks);
    std::vector<HashResult> result;
    CHashBundle bundle;
    UStringVector methods;
    for (const auto& a : algorithms)
        methods.Add(wide(a));
    require(bundle.SetMethods(methods), "Unknown checksum algorithm");
    std::vector<fs::path> files;
    for (const auto& input : inputs) {
        if (fs::is_directory(input)) {
            for (const auto& e : fs::recursive_directory_iterator(input))
                if (e.is_regular_file())
                    files.push_back(e.path());
        } else
            files.emplace_back(input);
    }
    std::sort(files.begin(), files.end());
    std::uint64_t total = 0;
    for (const auto& file : files)
        total += fs::file_size(file);
    ctx.phase("Hashing");
    ctx.report(0, total);
    std::array<char, 1 << 18> buffer{};
    std::uint64_t completed = 0;
    for (const auto& file : files) {
        require(ctx.item(file.string()), "Hash cancelled");
        std::ifstream in(file, std::ios::binary);
        if (!in)
            throw EngineError("Cannot read " + file.string());
        bundle.InitForNewFile();
        std::uint64_t size = 0;
        while (in) {
            in.read(buffer.data(), buffer.size());
            const auto n = in.gcount();
            if (n > 0) {
                bundle.Update(buffer.data(), static_cast<UInt32>(n));
                size += n;
                completed += n;
                require(ctx.report(completed, total), "Hash cancelled");
            }
        }
        if (!in.eof())
            throw EngineError("Read error: " + file.string());
        bundle.SetSize(size);
        bundle.Final(false, false, wide(file.filename().string()));
        for (unsigned i = 0; i < bundle.Hashers.Size(); ++i) {
            char digest[2 * k_HashCalc_DigestSize_Max + 32]{};
            bundle.Hashers[i].WriteToString(k_HashCalc_Index_Current, digest);
            result.push_back({file.string(), bundle.Hashers[i].Name.Ptr(), digest, size});
        }
    }
    return result;
}
namespace {
class BenchmarkCallback final : public IBenchCallback, public IBenchPrintCallback {
  public:
    OperationContext& ctx;
    std::uint64_t dictionary;
    BenchmarkResult result;
    BenchmarkCallback(OperationContext& c, std::uint64_t d) : ctx(c), dictionary(d) {}
    HRESULT SetEncodeResult(const CBenchInfo& info, bool final) override {
        if (final) {
            result.encodeBytesPerSecond = info.GetUnpackSizeSpeed();
            result.encodeRating = info.GetRating_LzmaEnc(dictionary);
            result.encodeCpuPercent = Benchmark_GetUsage_Percents(info.GetUsage());
        }
        return ctx.phase("Benchmark: compressing");
    }
    HRESULT SetDecodeResult(const CBenchInfo& info, bool final) override {
        if (final) {
            result.decodeBytesPerSecond = info.GetUnpackSizeSpeed();
            result.decodeRating = info.GetRating_LzmaDec();
            result.decodeCpuPercent = Benchmark_GetUsage_Percents(info.GetUsage());
        }
        return ctx.phase("Benchmark: decompressing");
    }
    void Print(const char*) override {}
    void NewLine() override {}
    HRESULT CheckBreak() override {
        return ctx.check();
    }
};
} // namespace
BenchmarkResult ArchiveEngine::benchmark(const BenchmarkOptions& options, const Callbacks& callbacks) {
    if (options.threads == 0 || options.dictionaryBytes < (1 << 18) || options.seconds == 0)
        throw EngineError("Benchmark requires threads, at least 256 KiB dictionary, and a positive duration");
    OperationContext ctx(callbacks);
    BenchmarkCallback callback(ctx, options.dictionaryBytes);
    CObjectVector<CProperty> props;
    addProperty(props, "mt", std::to_string(options.threads));
    addProperty(props, "d", std::to_string(options.dictionaryBytes) + "b");
    addProperty(props, "time", std::to_string(options.seconds));
    require(ctx.phase("Benchmark"), "Benchmark cancelled");
    require(Bench(&callback, &callback, props, 1, false), "Benchmark");
    return callback.result;
}
} // namespace m7arc
