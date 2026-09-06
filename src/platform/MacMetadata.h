#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

namespace m7arc::mac {

struct FileTime {
    std::int64_t seconds;
    std::int32_t nanoseconds;
    bool operator==(const FileTime &) const = default;
};

using CopyProgress = std::function<bool(std::uint64_t)>;

FileTime creationTime(const std::filesystem::path &path);
void setCreationTime(const std::filesystem::path &path, FileTime time);

// Copies file data, POSIX attributes, ACLs, resource forks, and extended attributes.
// The destination must not exist. Directories and symbolic links are supported.
void copyItem(const std::filesystem::path &source,
              const std::filesystem::path &destination,
              const CopyProgress &progress = {});

// AppleDouble files carry native metadata; a private trailing record preserves birth time
// at nanosecond precision. AppleDouble files produced by other tools are accepted.
void packMetadata(const std::filesystem::path &source,
                  const std::filesystem::path &appleDouble);
void unpackMetadata(const std::filesystem::path &appleDouble,
                    const std::filesystem::path &destination);
bool isAppleDouble(const std::filesystem::path &path);

}
