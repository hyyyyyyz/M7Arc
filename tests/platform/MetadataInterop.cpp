#include "engine/ArchiveEngine.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <sys/acl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}
void write(const fs::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary);
    output.write(value.data(), value.size());
    require(bool(output), "Cannot write " + path.string());
}
void run(const std::vector<std::string>& arguments) {
    auto pid = fork();
    require(pid >= 0, "Cannot start system tool");
    if (pid == 0) {
        std::vector<char*> argv;
        for (const auto& arg : arguments)
            argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    require(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "System tool failed: " + arguments[0]);
}
void setAttribute(const fs::path& path, const char* name, const std::string& bytes) {
    require(setxattr(path.c_str(), name, bytes.data(), bytes.size(), 0, XATTR_NOFOLLOW) == 0,
            std::string("Cannot set ") + name);
}
std::optional<std::string> attribute(const fs::path& path, const char* name) {
    const auto length = getxattr(path.c_str(), name, nullptr, 0, 0, XATTR_NOFOLLOW);
    if (length < 0)
        return {};
    std::string bytes(length, '\0');
    require(getxattr(path.c_str(), name, bytes.data(), bytes.size(), 0, XATTR_NOFOLLOW) == length,
            std::string("Cannot read ") + name);
    return bytes;
}
std::string aclText(const fs::path& path) {
    acl_t acl = acl_get_file(path.c_str(), ACL_TYPE_EXTENDED);
    require(acl != nullptr, "Cannot read ACL: " + path.string());
    ssize_t length = 0;
    char* text = acl_to_text(acl, &length);
    require(text != nullptr, "Cannot represent ACL");
    std::string value(text, length);
    acl_free(text);
    acl_free(acl);
    return value;
}
mode_t mode(const fs::path& path) {
    struct stat info{};
    require(stat(path.c_str(), &info) == 0, "Cannot read mode: " + path.string());
    return info.st_mode & 0777;
}
void good(const m7arc::OperationResult& result) {
    std::string errors;
    for (const auto& error : result.errors)
        errors += error + '\n';
    require(result.ok(), "Archive operation failed: " + errors);
}
bool compare(const fs::path& source, const fs::path& target, const std::string& direction) {
    bool same = true;
    auto report = [&](const char* property, bool matches) {
        std::cout << direction << " | " << property << " | " << (matches ? "PASS" : "FAIL") << '\n';
        same &= matches;
    };
    require(fs::exists(target), "Expected output missing: " + target.string());
    report("file bytes", read(source) == read(target));
    report("POSIX executable mode", mode(source) == mode(target));
    for (const auto* name : {"org.m7arc.interop", "com.apple.ResourceFork", "com.apple.FinderInfo",
                             "com.apple.metadata:_kMDItemUserTags"})
        report(name, attribute(source, name) == attribute(target, name));
    report("ACL", aclText(source) == aclText(target));
    return same;
}
} // namespace

int main() {
    char temporary[] = "/tmp/m7arc-interop-XXXXXX";
    const auto created = mkdtemp(temporary);
    if (!created)
        return 1;
    const fs::path root(created);
    try {
        const auto input = root / "sample";
        fs::create_directory(input);
        const auto source = input / "run-tool.sh";
        write(source, "#!/bin/sh\nprintf 'M7Arc metadata interoperability\\n'\n");
        require(chmod(source.c_str(), 0751) == 0, "Cannot set executable mode");
        setAttribute(source, "org.m7arc.interop", std::string("custom\0attribute\1bytes", 22));
        setAttribute(source, "com.apple.ResourceFork", "M7Arc resource fork content\n");
        std::string finderInfo(32, '\0');
        finderInfo.replace(0, 8, "TEXTttxt");
        finderInfo[9] = 12;
        setAttribute(source, "com.apple.FinderInfo", finderInfo);
        const auto plist = root / "tags.plist";
        write(plist, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                     "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                     "<plist version=\"1.0\"><array><string>M7ArcInterop\n6</string></array></plist>\n");
        run({"/usr/bin/plutil", "-convert", "binary1", plist.string()});
        setAttribute(source, "com.apple.metadata:_kMDItemUserTags", read(plist));
        const auto* user = getpwuid(getuid());
        require(user != nullptr, "Cannot identify test file owner");
        run({"/bin/chmod", "+a",
             std::string(user->pw_name) +
                 " allow read,write,append,readattr,writeattr,readextattr,writeextattr",
             source.string()});
        require(aclText(source).find("allow") != std::string::npos, "Test ACL must contain a real entry");

        const auto systemZip = root / "ditto.zip";
        run({"/usr/bin/ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", "--rsrc", "--extattr", "--acl",
             input.string(), systemZip.string()});
        m7arc::ArchiveEngine engine;
        m7arc::ExtractOptions extract;
        extract.restoreMacMetadata = true;
        good(engine.extract(systemZip.string(), (root / "m7arc-out").string(), {}, extract));
        const bool imported = compare(source, root / "m7arc-out/sample/run-tool.sh", "ditto -> M7Arc");

        const auto m7arcZip = root / "m7arc.zip";
        m7arc::CreateOptions create;
        create.format = "zip";
        create.method = "Deflate";
        create.preserveMacMetadata = true;
        good(engine.create({input.string()}, m7arcZip.string(), create));
        run({"/usr/bin/ditto", "-x", "-k", "--rsrc", "--extattr", "--acl", m7arcZip.string(),
             (root / "ditto-out").string()});
        const bool exported = compare(source, root / "ditto-out/sample/run-tool.sh", "M7Arc -> ditto");

        if (!imported || !exported) {
            run({"/usr/bin/ditto", "-x", "-k", "--rsrc", "--extattr", "--acl", systemZip.string(),
                 (root / "ditto-control").string()});
            compare(source, root / "ditto-control/sample/run-tool.sh", "ditto -> ditto control");
            throw std::runtime_error("ZIP metadata interoperability differs from the source");
        }
        fs::remove_all(root);
        std::cout << "Bidirectional ditto ZIP metadata interoperability passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\nFixture: " << root << '\n';
        return 1;
    }
}
