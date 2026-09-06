// Memory model adapted from 7-Zip 26.03 CompressDialog.cpp, Igor Pavlov.
// Distributed under the upstream GNU LGPL terms in third_party/7zip/DOC/License.txt.
#include "ArchiveEngine.h"
#include "LzmaEnc.h"
#include <algorithm>
#include <cctype>
#include <thread>

namespace m7arc {
namespace {
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}
constexpr std::uint64_t MiB = 1 << 20;
} // namespace
MemoryEstimate ArchiveEngine::estimateMemory(const CreateOptions& options) {
    MemoryEstimate result;
    result.note = "7-Zip working-memory estimate; file lists and Mac metadata staging are additional.";
    auto format = lower(options.format);
    if (format == "tar.gz")
        format = "gzip";
    if (format == "tar.bz2")
        format = "bzip2";
    if (format == "tar.xz")
        format = "xz";
    auto method = lower(options.method);
    if (method.empty()) {
        if (format == "7z" || format == "xz")
            method = "lzma2";
        else if (format == "zip" || format == "gzip")
            method = "deflate";
        else if (format == "bzip2")
            method = "bzip2";
    }
    if (format == "tar" || format == "wim" || options.level == 0 || method == "copy" || method == "store") {
        result.compressBytes = result.decompressBytes = MiB;
        return result;
    }
    if (format == "xz" && !options.solidBlock.empty()) {
        result.note = "A custom XZ block size requires the engine to determine memory usage.";
        return result;
    }
    if (!options.properties.empty()) {
        result.note = "Advanced codec properties require the engine to determine memory usage.";
        return result;
    }
    unsigned threads = options.threads ? options.threads : std::max(1u, std::thread::hardware_concurrency());
    if (method == "lzma" && format != "zip")
        threads = std::min(threads, 2u);
    if ((method == "ppmd" && format != "zip") || format == "gzip")
        threads = 1;
    const unsigned level = options.level;
    std::uint64_t size = format == "7z" && level >= 9 ? 29 * MiB : 0;
    unsigned zipThreads = 1;
    if (format == "zip") {
        const unsigned subThreads = method == "lzma" && threads > 1 && level >= 5 ? 2 : 1;
        zipThreads = std::max(1u, threads / subThreads);
        if (zipThreads > 1)
            size += std::uint64_t(zipThreads) * (std::uint64_t(sizeof(std::size_t)) << 23);
    }
    if (method == "lzma" || method == "lzma2") {
        CLzmaEncProps props;
        LzmaEncProps_Init(&props);
        props.level = static_cast<int>(level);
        if (options.dictionaryBytes)
            props.dictSize = static_cast<UInt32>(
                std::min<std::uint64_t>(options.dictionaryBytes, std::uint64_t(15) << 28));
        LzmaEncProps_Normalize(&props);
        const std::uint32_t dict = props.dictSize;
        std::uint32_t hash = dict - 1;
        hash |= hash >> 1;
        hash |= hash >> 2;
        hash |= hash >> 4;
        hash |= hash >> 8;
        hash >>= 1;
        if (hash >= (1 << 24))
            hash >>= 1;
        hash |= (1 << 16) - 1;
        if (level < 5)
            hash |= (256 << 10) - 1;
        ++hash;
        std::uint64_t perThread = std::uint64_t(hash) * 4 + std::uint64_t(dict) * 4 + 2 * MiB;
        if (level >= 5)
            perThread += std::uint64_t(dict) * 4;
        unsigned innerThreads = 1;
        if (threads > 1 && level >= 5) {
            perThread += 6 * MiB;
            innerThreads = 2;
        }
        const unsigned blocks = std::max(1u, threads / innerThreads);
        std::uint64_t chunk = 0;
        if (method == "lzma2" && blocks != 1) {
            chunk = std::max(std::uint64_t(dict),
                             std::clamp(std::uint64_t(dict) * 4, MiB, std::uint64_t(1) << 28));
            chunk = (chunk + MiB - 1) & ~(MiB - 1);
        }
        if (chunk == 0) {
            std::uint64_t block = std::uint64_t(dict) + (1 << 16) + (innerThreads > 1 ? MiB : 0);
            block += block >> (block < (std::uint64_t(1) << 30) ? 1 : 2);
            block = std::min(block, (std::uint64_t(1) << 32) - (1 << 16));
            size += blocks * (perThread + block);
        } else {
            size += blocks * (perThread + chunk);
            size += (blocks + blocks / 8 + 1) * chunk;
        }
        result.compressBytes = size;
        result.decompressBytes = std::uint64_t(dict) + 2 * MiB;
        return result;
    }
    if (method == "ppmd") {
        const auto dict = options.dictionaryBytes ? options.dictionaryBytes
                                                  : (std::uint64_t(1) << (std::min(level, 9u) + 19));
        result.decompressBytes = dict + 2 * MiB;
        result.compressBytes = size + *result.decompressBytes * (format == "zip" ? threads : 1);
    } else if (method == "deflate" || method == "deflate64") {
        result.compressBytes = size + 4 * MiB * zipThreads;
        result.decompressBytes = 2 * MiB;
    } else if (method == "bzip2") {
        result.compressBytes = size + 10 * MiB * threads;
        result.decompressBytes = 7 * MiB;
    } else {
        result.note = "The upstream dialog has no memory estimate for this method.";
    }
    return result;
}
} // namespace m7arc
