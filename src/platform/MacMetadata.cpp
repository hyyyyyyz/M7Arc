#include "MacMetadata.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <copyfile.h>
#include <fstream>
#include <stdexcept>
#include <sys/attr.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace m7arc::mac {
namespace {
constexpr std::uint32_t AppleDoubleMagic = 0x00051607;
constexpr std::array<unsigned char, 8> CreationTimeMarker{'M', '7', 'A', 'r', 'c', 'C', 'T', '1'};

[[noreturn]] void fail(const std::filesystem::path& path) {
    throw std::system_error(errno, std::generic_category(), path.string());
}
std::uint32_t get32(const unsigned char* bytes) {
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
           (std::uint32_t(bytes[2]) << 8) | bytes[3];
}
void put32(unsigned char* bytes, std::uint32_t value) {
    for (int i = 3; i >= 0; --i) {
        bytes[i] = value & 255;
        value >>= 8;
    }
}
void readExact(std::istream& stream, unsigned char* data, std::size_t size) {
    if (!stream.read(reinterpret_cast<char*>(data), size))
        throw std::runtime_error("Incomplete AppleDouble metadata");
}
struct Header {
    std::array<unsigned char, 26> bytes;
    std::vector<std::array<unsigned char, 12>> entries;
};
Header readHeader(std::istream& stream) {
    Header h;
    readExact(stream, h.bytes.data(), h.bytes.size());
    if (get32(h.bytes.data()) != AppleDoubleMagic || get32(h.bytes.data() + 4) != 0x00020000)
        throw std::runtime_error("Unsupported AppleDouble metadata");
    h.entries.resize((unsigned(h.bytes[24]) << 8) | h.bytes[25]);
    for (auto& entry : h.entries)
        readExact(stream, entry.data(), entry.size());
    return h;
}

int copyCallback(int what, int stage, copyfile_state_t state, const char*, const char*, void* context) {
    auto& progress = *static_cast<const CopyProgress*>(context);
    if (!progress)
        return COPYFILE_CONTINUE;
    if (stage == COPYFILE_START || (what == COPYFILE_COPY_DATA && stage == COPYFILE_PROGRESS)) {
        off_t bytes = 0;
        copyfile_state_get(state, COPYFILE_STATE_COPIED, &bytes);
        try {
            return progress(bytes) ? COPYFILE_CONTINUE : COPYFILE_QUIT;
        } catch (...) {
            return COPYFILE_QUIT;
        }
    }
    return COPYFILE_CONTINUE;
}
} // namespace

FileTime creationTime(const std::filesystem::path& path) {
    struct stat info{};
    if (lstat(path.c_str(), &info) != 0)
        fail(path);
    return {info.st_birthtimespec.tv_sec, static_cast<std::int32_t>(info.st_birthtimespec.tv_nsec)};
}

void setCreationTime(const std::filesystem::path& path, FileTime time) {
    struct attrlist attributes{};
    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.commonattr = ATTR_CMN_CRTIME;
    struct timespec value{static_cast<time_t>(time.seconds), time.nanoseconds};
    if (setattrlist(path.c_str(), &attributes, &value, sizeof(value), FSOPT_NOFOLLOW) != 0)
        fail(path);
}

void copyItem(const std::filesystem::path& source, const std::filesystem::path& destination,
              const CopyProgress& progress) {
    copyfile_state_t state = copyfile_state_alloc();
    if (!state)
        fail(source);
    copyfile_state_set(state, COPYFILE_STATE_STATUS_CB, reinterpret_cast<const void*>(copyCallback));
    copyfile_state_set(state, COPYFILE_STATE_STATUS_CTX, &progress);
    const int result = copyfile(source.c_str(), destination.c_str(), state,
                                COPYFILE_ALL | COPYFILE_RECURSIVE | COPYFILE_NOFOLLOW | COPYFILE_CLONE);
    const int error = errno;
    copyfile_state_free(state);
    if (result != 0) {
        errno = error;
        fail(source);
    }
}

bool isAppleDouble(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::array<unsigned char, 8> prefix{};
    return bool(stream.read(reinterpret_cast<char*>(prefix.data()), prefix.size())) &&
           get32(prefix.data()) == AppleDoubleMagic && get32(prefix.data() + 4) == 0x00020000;
}

void packMetadata(const std::filesystem::path& source, const std::filesystem::path& appleDouble) {
    const auto birth = creationTime(source);
    if (copyfile(source.c_str(), appleDouble.c_str(), nullptr,
                 COPYFILE_PACK | COPYFILE_METADATA | COPYFILE_NOFOLLOW) != 0)
        fail(source);
    std::ofstream output(appleDouble, std::ios::binary | std::ios::app);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    std::array<unsigned char, 20> record{};
    std::copy(CreationTimeMarker.begin(), CreationTimeMarker.end(), record.begin());
    const auto seconds = std::bit_cast<std::uint64_t>(birth.seconds);
    put32(record.data() + 8, seconds >> 32);
    put32(record.data() + 12, seconds & 0xFFFFFFFF);
    put32(record.data() + 16, birth.nanoseconds);
    output.write(reinterpret_cast<const char*>(record.data()), record.size());
}

void unpackMetadata(const std::filesystem::path& appleDouble, const std::filesystem::path& destination) {
    std::ifstream input(appleDouble, std::ios::binary);
    readHeader(input);
    std::optional<FileTime> birth;
    input.seekg(-20, std::ios::end);
    std::array<unsigned char, 20> record{};
    readExact(input, record.data(), record.size());
    if (std::equal(CreationTimeMarker.begin(), CreationTimeMarker.end(), record.begin())) {
        const auto seconds = (std::uint64_t(get32(record.data() + 8)) << 32) | get32(record.data() + 12);
        const auto nanoseconds = get32(record.data() + 16);
        if (nanoseconds >= 1000000000)
            throw std::runtime_error("Invalid AppleDouble creation time");
        birth = FileTime{std::bit_cast<std::int64_t>(seconds), static_cast<std::int32_t>(nanoseconds)};
    }
    if (copyfile(appleDouble.c_str(), destination.c_str(), nullptr,
                 COPYFILE_UNPACK | COPYFILE_XATTR | COPYFILE_ACL | COPYFILE_NOFOLLOW) != 0)
        fail(destination);
    if (birth)
        setCreationTime(destination, *birth);
}

} // namespace m7arc::mac
