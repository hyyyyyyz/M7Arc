#include "app/EditSession.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void write(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "create test input");
    require(file.write(bytes) == bytes.size(), "write test input");
}
QByteArray read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read test output");
    return file.readAll();
}
m7arc::ArchiveEntry member(m7arc::ArchiveEngine& engine, const QString& archive, const std::string& path) {
    auto snapshot = engine.list(archive.toStdString());
    auto i = std::find_if(snapshot.entries.begin(), snapshot.entries.end(),
                          [&](const auto& entry) { return entry.path == path; });
    require(i != snapshot.entries.end(), "archive member exists");
    return *i;
}
std::shared_ptr<m7arc::EditSession> session(const QString& archive, const m7arc::ArchiveEntry& entry,
                                            const QString& local) {
    auto result = std::make_shared<m7arc::EditSession>();
    result->archivePath = archive;
    result->memberPath = QString::fromStdString(entry.path);
    result->index = entry.index;
    result->localPath = local;
    result->remember();
    result->rememberArchive();
    return result;
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(), "test directory");
        QDir root(temporary.path());
        const auto input = root.filePath("input"), inner = root.filePath("inner.zip"),
                   outer = root.filePath("outer.7z");
        write(QDir(input).filePath("docs/text.txt"), "before\n");
        write(QDir(input).filePath("elsewhere/text.txt"), "other directory\n");
        m7arc::ArchiveEngine engine;
        m7arc::CreateOptions zip;
        zip.format = "zip";
        require(engine
                    .create({QDir(input).filePath("docs").toStdString(),
                             QDir(input).filePath("elsewhere").toStdString()},
                            inner.toStdString(), zip)
                    .ok(),
                "create inner zip");
        require(engine.create({inner.toStdString()}, outer.toStdString()).ok(), "create outer 7z");
        const auto outerMember = member(engine, outer, "inner.zip");
        const auto work = root.filePath("work");
        require(engine.extract(outer.toStdString(), work.toStdString(), {outerMember.index}).ok(),
                "open nested archive");
        const auto nestedPath = QDir(work).filePath("inner.zip");
        auto nested = session(outer, outerMember, nestedPath);
        nested->nestedArchive = true;
        const auto editedMember = member(engine, nestedPath, "docs/text.txt");
        const auto editsDirectory = root.filePath("edits");
        require(
            engine.extract(nestedPath.toStdString(), editsDirectory.toStdString(), {editedMember.index}).ok(),
            "open editable member");
        const auto editedPath = QDir(editsDirectory).filePath("docs/text.txt");
        auto edited = session(nestedPath, editedMember, editedPath);
        m7arc::EditSessions sessions{nested, edited};
        require(!edited->changed(), "unchanged edit does not request rewrite");
        write(editedPath, "after — nested archive edit\n");
        require(edited->changed(), "editing requests rewrite");
        require(m7arc::saveEditedMember(engine, *edited, sessions, {}).ok(), "save nested archive chain");
        auto verify = root.filePath("verify");
        require(engine.extract(outer.toStdString(), verify.toStdString()).ok(),
                "extract updated outer archive");
        auto contents = root.filePath("contents");
        require(engine.extract(QDir(verify).filePath("inner.zip").toStdString(), contents.toStdString()).ok(),
                "extract updated inner archive");
        require(read(QDir(contents).filePath("docs/text.txt")) == read(editedPath),
                "edited bytes propagated to outer archive");
        require(read(QDir(contents).filePath("elsewhere/text.txt")) == "other directory\n",
                "same basename in another directory preserved");
        edited->remember();
        edited->rememberArchive();
        nested->remember();
        nested->rememberArchive();
        write(editedPath, "unsaved work remains available\n");
        auto other = member(engine, nestedPath, "elsewhere/text.txt");
        require(engine.rename(nestedPath.toStdString(), other.index, "elsewhere/renamed.txt").ok(),
                "modify archive independently");
        bool rejected = false;
        try {
            m7arc::saveEditedMember(engine, *edited, sessions, {});
        } catch (const m7arc::EngineError&) {
            rejected = true;
        }
        require(rejected, "stale archive edit must be rejected");
        require(read(editedPath) == "unsaved work remains available\n", "failed rewrite retains edited file");
        std::cout << "Edit session integration passed: nested propagation, unrelated member preservation, "
                     "stale revision rejection, retained edits.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
