#include "app/ArchiveRows.h"
#include "app/FileSplit.h"
#include "app/Transfer.h"
#include "app/Workspace.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace m7arc;

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

namespace {
class AnsweringRunner final : public app::JobRunner {
  public:
    OverwriteAnswer overwriteAnswer = OverwriteAnswer::Replace;
    int overwriteQuestions = 0;

  protected:
    OverwriteAnswer requestOverwrite(const OverwriteRequest&) override {
        ++overwriteQuestions;
        return overwriteAnswer;
    }
};
class MemoryClipboard final : public app::ClipboardPort {
  public:
    QStringList stored;
    bool wasCut = false;
    void setFiles(const QStringList& paths, bool cut) override {
        stored = paths;
        wasCut = cut;
    }
    QStringList files() const override {
        return stored;
    }
    bool isCut() const override {
        return wasCut;
    }
    void clear() override {
        stored.clear();
    }
};

void write(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write test input");
}
QByteArray read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read test output");
    return file.readAll();
}
OperationResult wait(const std::function<void(const app::Workspace::Done&)>& start) {
    QEventLoop loop;
    OperationResult result;
    bool finished = false;
    start([&](const OperationResult& r) {
        result = r;
        finished = true;
        loop.quit();
    });
    QTimer limit;
    limit.setSingleShot(true);
    QObject::connect(&limit, &QTimer::timeout, &loop, [] {
        std::cerr << "Workspace operation did not complete within 30 seconds\n";
        std::_Exit(1);
    });
    limit.start(30000);
    if (!finished)
        loop.exec();
    return result;
}
std::vector<std::string> memberPaths(const QString& archive) {
    ArchiveEngine engine;
    std::vector<std::string> paths;
    for (const auto& entry : engine.list(archive.toStdString()).entries)
        paths.push_back(entry.path);
    std::sort(paths.begin(), paths.end());
    return paths;
}
bool has(const QString& archive, const std::string& path) {
    const auto paths = memberPaths(archive);
    return std::find(paths.begin(), paths.end(), path) != paths.end();
}
app::PaneState archiveState(const QString& archive, const QString& directory = {}) {
    ArchiveEngine engine;
    app::PaneState state;
    state.archivePath = archive;
    state.archiveDirectory = directory;
    state.archiveFormat = "7z";
    const auto listing = app::toListing(engine.list(archive.toStdString()));
    for (const auto& row : listing.rows) {
        state.rows.append(row);
        state.indices.append(row.index);
    }
    return state;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication qtApp(argc, argv);
    QTemporaryDir root;
    require(root.isValid(), "Temporary directory unavailable");
    QCoreApplication::setOrganizationName("M7ArcWorkspaceTest");
    QCoreApplication::setApplicationName("M7ArcWorkspaceTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.filePath("settings"));
    try {
        AnsweringRunner runner;
        MemoryClipboard clipboard;
        app::Workspace workspace(runner, clipboard);
        app::Settings settings;
        const auto dir = [&](const QString& name) { return root.filePath(name); };

        // Every way of adding to an archive shares one definition of the options.
        settings.setPreserveMacMetadata(false);
        app::PaneState target;
        target.archivePath = dir("a.7z");
        target.archiveDirectory = "docs/";
        target.archiveFormat = "ZIP";
        auto insert = app::insertOptions(target);
        require(insert.format == "zip" && insert.archivePrefix == "docs/" && !insert.preserveMacMetadata,
                "Insert options must follow the target and the metadata preference");
        settings.setPreserveMacMetadata(true);
        require(app::makeTransfer({}, target, false, "/tmp").insert.preserveMacMetadata,
                "Metadata preference must reach every transfer");

        // Local copy and move, with an overwrite question.
        write(dir("src/one.txt"), "one");
        write(dir("src/two.txt"), "two");
        write(dir("dst/one.txt"), "old");
        app::PaneState files;
        files.files = {dir("src/one.txt"), dir("src/two.txt")};
        app::PaneState folder;
        folder.localDirectory = dir("dst");
        runner.overwriteAnswer = OverwriteAnswer::Skip;
        auto result = wait([&](auto done) { workspace.transfer(files, folder, false, done); });
        require(result.ok() && read(dir("dst/one.txt")) == "old" && read(dir("dst/two.txt")) == "two" &&
                    QFile::exists(dir("src/one.txt")),
                "Copy must skip the declined overwrite and keep the sources");
        runner.overwriteAnswer = OverwriteAnswer::Replace;
        result = wait([&](auto done) { workspace.transfer(files, folder, true, done); });
        require(result.ok() && read(dir("dst/one.txt")) == "one" && !QFile::exists(dir("src/one.txt")) &&
                    !QFile::exists(dir("src/two.txt")),
                "Move must replace the target and remove the sources");
        require(runner.overwriteQuestions == 3, "Each conflict is put to the user once, whatever the earlier answer");

        // Local to archive, archive to archive and archive to local, all through one path.
        app::PaneState archiveTarget;
        archiveTarget.archivePath = dir("first.7z");
        archiveTarget.archiveFormat = "7z";
        ArchiveEngine engine;
        write(dir("seed/seed.txt"), "seed");
        require(engine.create({dir("seed/seed.txt").toStdString()}, archiveTarget.archivePath.toStdString()).ok(),
                "create archive");
        app::PaneState addition;
        addition.files = {dir("dst/one.txt")};
        archiveTarget.archiveDirectory = "inner/";
        workspace.openArchive(archiveTarget.archivePath, {}, [](const app::ArchiveListing&) {});
        result = wait([&](auto done) { workspace.transfer(addition, archiveTarget, true, done); });
        require(result.ok() && has(archiveTarget.archivePath, "inner/one.txt") && !QFile::exists(dir("dst/one.txt")),
                "Moving a file into an archive folder must store it there and delete the original");

        auto firstState = archiveState(archiveTarget.archivePath);
        firstState.rows.erase(std::remove_if(firstState.rows.begin(), firstState.rows.end(),
                                             [](const auto& row) { return row.path != "inner/one.txt"; }),
                              firstState.rows.end());
        require(firstState.rows.size() == 1, "member selected");
        firstState.indices = {firstState.rows.first().index};
        app::PaneState second;
        second.archivePath = dir("second.7z");
        second.archiveFormat = "7z";
        require(engine.create({dir("seed/seed.txt").toStdString()}, second.archivePath.toStdString()).ok(),
                "create second archive");
        result = wait([&](auto done) { workspace.transfer(firstState, second, false, done); });
        require(result.ok() && has(second.archivePath, "one.txt") && has(archiveTarget.archivePath, "inner/one.txt"),
                "Archive to archive copy keeps the source member");
        app::PaneState outside;
        outside.localDirectory = dir("out");
        result = wait([&](auto done) { workspace.transfer(firstState, outside, true, done); });
        require(result.ok() && read(dir("out/one.txt")) == "one" &&
                    !has(archiveTarget.archivePath, "inner/one.txt"),
                "Archive to local move extracts and removes the member");

        // Writability learned from the engine.
        require(workspace.writable(archiveTarget.archivePath), "Opening an archive records that it can be updated");
        require(!workspace.writable(dir("unknown.7z")), "Unknown archives are not writable");

        // Clipboard: cut, paste into a folder, clipboard emptied.
        write(dir("clip/a.txt"), "a");
        workspace.copyFilesToClipboard({dir("clip/a.txt")}, true);
        app::PaneState pasteTarget;
        pasteTarget.localDirectory = dir("pasted");
        require(workspace.pasteFromClipboard(pasteTarget, [](const OperationResult&) {}), "paste started");
        wait([&](auto done) { runner.run("sync", [](ArchiveEngine&, const Callbacks&) { return OperationResult{}; }, done); });
        require(read(dir("pasted/a.txt")) == "a" && !QFile::exists(dir("clip/a.txt")) && clipboard.stored.isEmpty(),
                "Pasting cut files moves them and clears the clipboard");
        require(!workspace.pasteFromClipboard(pasteTarget, {}), "Nothing to paste");

        // Editing a member in an external program and writing it back.
        const auto editable = dir("edit.7z");
        write(dir("editing/note.txt"), "before");
        CreateOptions plain;
        plain.preserveMacMetadata = false;
        require(engine.create({dir("editing/note.txt").toStdString()}, editable.toStdString(), plain).ok(),
                "create editable");
        auto rows = app::toListing(engine.list(editable.toStdString())).rows;
        require(rows.size() == 1, "one member");
        app::Workspace::OpenedMember opened;
        require(workspace.openMember(editable, rows.first(), false,
                                     [&](const app::Workspace::OpenedMember& m) { opened = m; }),
                "member scheduled");
        wait([&](auto done) { runner.run("sync", [](ArchiveEngine&, const Callbacks&) { return OperationResult{}; }, done); });
        require(!opened.localPath.isEmpty() && !opened.archive && read(opened.localPath) == "before",
                "Member extracted for the external program");
        require(!workspace.edits().anyChanged(), "Untouched member is not an edit");
        require(!workspace.temp().paths().isEmpty(), "Opened members are tracked temporary folders");
        write(opened.localPath, "after the edit");
        auto session = workspace.edits().firstChanged();
        require(session != nullptr, "Edit noticed");
        result = wait([&](auto done) { workspace.settleEdit(session, true, done); });
        require(result.ok() && !workspace.edits().anyChanged(), "Saved edit becomes the new baseline");
        ArchiveEngine verify;
        require(verify.extract(editable.toStdString(), dir("verify").toStdString()).ok() &&
                    read(dir("verify/note.txt")) == "after the edit",
                "Edit written back to the archive");

        // Pure helpers.
        require(app::parseVolumeSize("100m") == 100ull * 1024 * 1024 && app::parseVolumeSize(" 2G ") == 2ull << 30 &&
                    app::parseVolumeSize("512") == 512 && !app::parseVolumeSize("0") && !app::parseVolumeSize("1.5M") &&
                    !app::parseVolumeSize("10X") && !app::parseVolumeSize(""),
                "Volume sizes");
        QList<app::ArchiveRow> rowsInTree(3);
        rowsInTree[0].path = "dir";
        rowsInTree[0].directory = true;
        rowsInTree[1].path = "dir/file.txt";
        rowsInTree[2].path = "dirty.txt";
        const auto outer = app::outermostRows(rowsInTree);
        require(outer.size() == 2 && outer[0].path == "dir" && outer[1].path == "dirty.txt",
                "Members inside a selected folder travel with it; similar names do not");
        QList<app::ArchiveRow> entries(2);
        entries[0].path = "docs/readme";
        entries[1].path = "docs/sub/x";
        require(!app::Workspace::memberNameFree(entries, "docs/", "readme") &&
                    !app::Workspace::memberNameFree(entries, "docs/", "sub") &&
                    app::Workspace::memberNameFree(entries, "docs/", "other") &&
                    app::Workspace::memberNameFree(entries, "", "readme"),
                "Names are free unless they collide with a member or a folder");

        // Split and combine.
        write(dir("big.bin"), QByteArray(2500, 'x'));
        QDir().mkpath(dir("parts"));
        result = wait([&](auto done) { workspace.splitFile(dir("big.bin"), dir("parts"), 1000, done); });
        require(result.ok() && result.outputPaths.size() == 3 && QFileInfo(dir("parts/big.bin.003")).size() == 500,
                "Split into three volumes");
        const auto parts = app::volumeParts(dir("parts/big.bin.001"));
        require(parts.size() == 3 && app::joinedName(dir("parts/big.bin.001")) == QFileInfo(dir("parts/big.bin")).absoluteFilePath(),
                "Volumes found in order");
        result = wait([&](auto done) { workspace.combineFiles(parts, dir("joined.bin"), done); });
        require(result.ok() && read(dir("joined.bin")) == read(dir("big.bin")), "Combine restores the file");
        result = wait([&](auto done) { workspace.splitFile(dir("big.bin"), dir("parts"), 1000, done); });
        require(!result.ok(), "Splitting never replaces existing volumes");

        std::cout << "Workspace transfers, edits, clipboard and splitting work without a window\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
