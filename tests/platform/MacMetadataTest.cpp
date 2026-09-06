#include "platform/MacMetadata.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/acl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace fs = std::filesystem;
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
std::string attribute(const fs::path &path, const char *name) {
    auto size = getxattr(path.c_str(), name, nullptr, 0, 0, XATTR_NOFOLLOW);
    if (size < 0) throw std::runtime_error(std::string("Missing attribute: ") + name);
    std::string value(size, '\0');
    require(getxattr(path.c_str(), name, value.data(), value.size(), 0, XATTR_NOFOLLOW) == size,
            "Cannot read attribute");
    return value;
}
void setAttribute(const fs::path &path, const char *name, const std::string &value) {
    require(setxattr(path.c_str(), name, value.data(), value.size(), 0, XATTR_NOFOLLOW) == 0,
            "Cannot set test attribute");
}
int main() {
    char name[] = "/tmp/m7arc-metadata-XXXXXX";
    const char *directory = mkdtemp(name);
    if (!directory) return 1;
    fs::path root(directory);
    try {
        const auto source = root / "文件.txt";
        const auto target = root / "restored.txt";
        const auto sidecar = root / "._文件.txt";
        std::ofstream(source) << "source content";
        std::ofstream(target) << "source content";
        setAttribute(source, "org.m7arc.test", "attribute value");
        setAttribute(source, "com.apple.ResourceFork", "resource fork bytes");
        std::string finderInfo(32, '\0'); finderInfo[0] = 'T'; finderInfo[1] = 'E';
        setAttribute(source, "com.apple.FinderInfo", finderInfo);
        setAttribute(source, "com.apple.metadata:_kMDItemUserTags", "test-tag-data");
        m7arc::mac::setCreationTime(source, {1700000000, 123456789});
        m7arc::mac::packMetadata(source, sidecar);
        require(m7arc::mac::isAppleDouble(sidecar), "Not AppleDouble");
        m7arc::mac::unpackMetadata(sidecar, target);
        for (const char *key : {"org.m7arc.test", "com.apple.ResourceFork", "com.apple.FinderInfo", "com.apple.metadata:_kMDItemUserTags"})
            require(attribute(source, key) == attribute(target, key), "Attribute changed after round trip");
        require(m7arc::mac::creationTime(source) == m7arc::mac::creationTime(target), "Creation time changed");

        const auto folder = root / "folder";
        fs::create_directory(folder);
        std::ofstream(folder / "script") << "#!/bin/sh\nexit 0\n";
        chmod((folder / "script").c_str(), 0755);
        fs::create_symlink("script", folder / "link");
        setAttribute(folder / "script", "org.m7arc.test", "copied attribute");
        const auto copied = root / "copied";
        m7arc::mac::copyItem(folder, copied);
        require(fs::is_symlink(copied / "link"), "Copy dereferenced a symbolic link");
        require(attribute(copied / "script", "org.m7arc.test") == "copied attribute", "Copy lost metadata");
        struct stat info {};
        require(stat((copied / "script").c_str(), &info) == 0 && (info.st_mode & 0777) == 0755,
                "Copy lost executable permission");
        fs::remove_all(root);
        std::cout << "AppleDouble attributes, resource fork, FinderInfo, tags, birth time and native copy passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << " (sample: " << root << ")\n";
        return 1;
    }
}
