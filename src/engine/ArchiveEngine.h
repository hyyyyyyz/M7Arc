#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace m7arc {

struct FormatInfo {
    std::string name;
    std::vector<std::string> extensions;
    bool canRead = true;
    bool canWrite = false;
    bool isHash = false;
    std::string modulePath;
};
struct CodecInfo {
    std::string name;
    std::uint64_t id = 0;
    bool encode = false;
    bool decode = false;
    bool filter = false;
    std::string modulePath;
};
struct HasherInfo {
    std::string name;
    std::uint64_t id = 0;
    std::uint32_t digestSize = 0;
    std::string modulePath;
};
struct EngineOptions {
    bool loadUserModules = true;
    std::vector<std::string> additionalModuleDirectories;
};
struct ModuleInfo {
    std::string path;
    bool loaded = false;
    std::uint32_t version = 0;
    std::vector<std::string> formats;
    std::vector<std::string> codecs;
    std::vector<std::string> hashers;
    std::string error;
};
struct Property {
    std::uint32_t id = 0;
    std::string name;
    std::string value;
};
struct ArchiveEntry {
    std::uint32_t index = 0;
    std::string path;
    std::uint64_t size = 0;
    std::uint64_t packedSize = 0;
    bool isDirectory = false;
    bool encrypted = false;
    bool alternateStream = false;
    std::int64_t modifiedUnixSeconds = 0;
    std::string method;
    std::string attributes;
    std::vector<Property> properties;
};
struct ArchiveSnapshot {
    std::string path;
    std::string format;
    bool canUpdate = false;
    bool canSetComment = false;
    std::vector<ArchiveEntry> entries;
    std::vector<Property> properties;
    std::vector<std::string> volumes;
};
struct Progress {
    std::string phase;
    std::string path;
    std::uint64_t completed = 0;
    std::uint64_t total = 0;
    bool totalKnown = false;
};
enum class Overwrite { Ask, Replace, Skip, RenameIncoming, RenameExisting };
enum class OverwriteAnswer { Replace, ReplaceAll, Skip, SkipAll, RenameIncoming, Cancel };
struct OverwriteRequest {
    std::string existingPath;
    std::string incomingPath;
    std::optional<std::uint64_t> existingSize;
    std::optional<std::uint64_t> incomingSize;
};
struct MemoryRequest {
    std::string path;
    std::uint64_t requiredBytes = 0;
    std::uint64_t allowedBytes = 0;
    bool reportOnly = false;
};
enum class MemoryAnswer { Continue, Cancel };
struct Callbacks {
    // Callbacks run on the engine worker or an internal codec thread. Returning
    // false from progress cancels the operation; it can also wait while paused.
    std::function<bool(const Progress&)> progress;
    std::function<bool()> cancelled;
    std::function<std::optional<std::string>()> password;
    std::function<OverwriteAnswer(const OverwriteRequest&)> overwrite;
    std::function<MemoryAnswer(const MemoryRequest&)> memoryRequest;
    std::vector<std::pair<std::string, std::string>> passwordOverrides;
};
struct OpenOptions {
    std::string password;
    std::string type;
};
enum class PathMode { Full, Flat, Current, Absolute };
struct ExtractOptions {
    std::string password;
    PathMode pathMode = PathMode::Full;
    Overwrite overwrite = Overwrite::Ask;
    bool eliminateDuplicateRoot = false;
    bool createArchiveFolder = false;
    bool restoreMacMetadata = true;
    std::string currentDirectory;
};
enum class UpdateMode { Add, Update, Freshen, Synchronize };
enum class ArchivePathMode { Relative, Full, Absolute };
struct CreateOptions {
    std::string format = "7z";
    unsigned level = 5;
    std::string method;
    std::uint64_t dictionaryBytes = 0;
    unsigned wordSize = 0;
    unsigned threads = 0;
    bool solid = true;
    std::string solidBlock;
    std::string password;
    bool encryptHeaders = false;
    std::string encryptionMethod;
    std::vector<std::uint64_t> volumeSizes;
    UpdateMode updateMode = UpdateMode::Add;
    bool storeSymbolicLinks = true;
    bool storeHardLinks = false;
    bool deleteAfterCompressing = false;
    bool preserveAccessTime = false;
    bool preserveMacMetadata = true;
    std::string archivePrefix;
    bool openSharedFiles = false;
    bool setArchiveMTime = false;
    bool storeOwnerId = false;
    bool storeOwnerName = false;
    ArchivePathMode pathMode = ArchivePathMode::Relative;
    std::vector<std::string> includePatterns;
    std::vector<std::string> excludePatterns;
    std::vector<std::pair<std::string, std::string>> properties;
};
struct OperationResult {
    bool cancelled = false;
    bool passwordMayBeIncorrect = false;
    std::vector<std::string> errors;
    std::vector<std::string> outputPaths;
    std::uint64_t completedItems = 0;
    bool ok() const {
        return !cancelled && errors.empty();
    }
};
struct HashResult {
    std::string path;
    std::string algorithm;
    std::string digest;
    std::uint64_t size = 0;
};
struct HashTotal {
    std::string algorithm;
    std::string dataSum;
    std::string dataAndNamesSum;
};
struct HashSummary {
    std::vector<HashResult> files;
    std::vector<HashTotal> totals;
    std::uint64_t fileCount = 0;
    std::uint64_t directoryCount = 0;
    std::uint64_t totalBytes = 0;
};
enum class VerificationStatus { Passed, Missing, Mismatch, Error };
struct ManifestEntryResult {
    std::string path;
    VerificationStatus status = VerificationStatus::Error;
    std::string message;
};
struct ManifestResult {
    std::vector<ManifestEntryResult> entries;
    bool cancelled = false;
    std::vector<std::string> errors;
    bool ok() const;
};
struct BenchmarkOptions {
    unsigned threads = 2;
    std::uint64_t dictionaryBytes = 1 << 24;
    unsigned seconds = 2;
};
struct BenchmarkResult {
    std::uint64_t encodeBytesPerSecond = 0;
    std::uint64_t decodeBytesPerSecond = 0;
    std::uint64_t encodeRating = 0;
    std::uint64_t decodeRating = 0;
    std::uint64_t encodeCpuPercent = 0;
    std::uint64_t decodeCpuPercent = 0;
};
struct MemoryEstimate {
    std::optional<std::uint64_t> compressBytes;
    std::optional<std::uint64_t> decompressBytes;
    std::string note;
};
enum class EngineErrorKind { General, Cancelled, PasswordRequired, PasswordOrCorruption };
class EngineError : public std::runtime_error {
  public:
    explicit EngineError(const std::string& message, EngineErrorKind kind = EngineErrorKind::General)
        : std::runtime_error(message), kind_(kind) {}
    EngineErrorKind kind() const noexcept {
        return kind_;
    }

  private:
    EngineErrorKind kind_;
};

// One engine instance belongs to one serial worker. Public values own their data.
// Paths and text are UTF-8. Entry indices identify members of the listed archive.
class ArchiveEngine {
  public:
    explicit ArchiveEngine(const EngineOptions& options = {});
    ~ArchiveEngine();
    ArchiveEngine(const ArchiveEngine&) = delete;
    ArchiveEngine& operator=(const ArchiveEngine&) = delete;
    std::vector<FormatInfo> formats() const;
    std::vector<CodecInfo> codecs() const;
    std::vector<HasherInfo> hashers() const;
    static std::string version();
    std::vector<ModuleInfo> modules() const;
    std::vector<std::string> moduleDirectories() const;
    ArchiveSnapshot list(const std::string& path, const OpenOptions& options = {},
                         const Callbacks& callbacks = {});
    // An empty index list selects every entry. Nonempty selections are exact indices.
    OperationResult extract(const std::string& path, const std::string& destination,
                            const std::vector<std::uint32_t>& indices = {},
                            const ExtractOptions& options = {}, const Callbacks& callbacks = {});
    OperationResult test(const std::string& path, const std::vector<std::uint32_t>& indices = {},
                         const OpenOptions& options = {}, const Callbacks& callbacks = {});
    // Inputs are absolute paths; names inside the archive are relative to their
    // common parent. format accepts the seven writable types and tar.gz/bz2/xz.
    OperationResult create(const std::vector<std::string>& inputs, const std::string& destination,
                           const CreateOptions& options = {}, const Callbacks& callbacks = {});
    OperationResult remove(const std::string& path, const std::vector<std::uint32_t>& indices,
                           const OpenOptions& options = {}, const Callbacks& callbacks = {});
    OperationResult rename(const std::string& path, std::uint32_t index, const std::string& newPath,
                           const OpenOptions& options = {}, const Callbacks& callbacks = {});
    OperationResult renameDirectory(const std::string& path, const std::string& oldPrefix,
                                    const std::string& newPrefix, const OpenOptions& options = {},
                                    const Callbacks& callbacks = {});
    OperationResult replace(const std::string& path, std::uint32_t index, const std::string& editedLocalFile,
                            const OpenOptions& options = {}, const Callbacks& callbacks = {});
    OperationResult setComment(const std::string& path, std::uint32_t index, const std::string& comment,
                               const OpenOptions& options = {}, const Callbacks& callbacks = {});
    std::vector<HashResult> hash(const std::vector<std::string>& inputs,
                                 const std::vector<std::string>& algorithms, const Callbacks& callbacks = {});
    HashSummary hashSummary(const std::vector<std::string>& inputs,
                            const std::vector<std::string>& algorithms, const Callbacks& callbacks = {});
    ManifestResult verifyManifest(const std::string& path, const Callbacks& callbacks = {});
    BenchmarkResult benchmark(const BenchmarkOptions& options = {}, const Callbacks& callbacks = {});
    static MemoryEstimate estimateMemory(const CreateOptions& options);

  private:
    OperationResult createRaw(const std::vector<std::string>& inputs, const std::string& destination,
                              const CreateOptions& options, const Callbacks& callbacks,
                              const std::string& baseDirectory = {},
                              const std::vector<std::pair<std::string, std::string>>& sourceMappings = {});
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace m7arc
