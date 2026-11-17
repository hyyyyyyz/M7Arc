#include "platform/drag/MacFilePromises.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const QString &message) {
    if (!value) throw std::runtime_error(message.toStdString());
}
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), file.errorString());
    return file.readAll();
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir work;
        require(work.isValid(), work.errorString());
        const QString source = work.filePath(QStringLiteral("source/nested"));
        require(QDir().mkpath(source), QStringLiteral("Cannot create the input folder."));
        const QByteArray payload("Finder deferred extraction\n");
        QFile input(source + QStringLiteral("/中文.txt"));
        require(input.open(QIODevice::WriteOnly), input.errorString());
        require(input.write(payload) == payload.size(), input.errorString());
        input.close();
        m7arc::ArchiveEngine engine;
        const QString archive = work.filePath(QStringLiteral("source.7z"));
        const auto created = engine.create({work.filePath(QStringLiteral("source")).toStdString()}, archive.toStdString());
        require(created.ok(), QStringLiteral("Cannot create the test archive."));
        const auto snapshot = engine.list(archive.toStdString());
        m7arc::platform::ArchivePromiseItem item;
        item.archivePath = archive;
        for (const auto &entry : snapshot.entries) {
            if (QString::fromStdString(entry.path).endsWith(QStringLiteral("/中文.txt"))) {
                item.name = QStringLiteral("中文.txt");
                item.memberPath = QString::fromStdString(entry.path);
                item.indices = {entry.index};
            }
        }
        require(!item.indices.isEmpty(), QStringLiteral("The nested file is absent."));
        const QString destination = work.filePath(QStringLiteral("Finder renamed.txt"));
        const auto copied = m7arc::platform::writeArchivePromise(engine, item, destination, {});
        require(copied.ok(), copied.errors.empty() ? QStringLiteral("Copy failed.") : QString::fromStdString(copied.errors.front()));
        require(read(destination) == payload, QStringLiteral("The promised contents differ."));
        m7arc::platform::ArchivePromiseItem folder;
        folder.archivePath = archive;
        folder.name = QStringLiteral("nested");
        folder.directory = true;
        folder.memberPath = QFileInfo(item.memberPath).path();
        for (const auto &entry : snapshot.entries) {
            const QString path = QString::fromStdString(entry.path);
            if (path == folder.memberPath || path.startsWith(folder.memberPath + '/')) folder.indices.append(entry.index);
        }
        const QString directory = work.filePath(QStringLiteral("Renamed Folder"));
        const auto copiedFolder = m7arc::platform::writeArchivePromise(engine, folder, directory, {});
        require(copiedFolder.ok(), QStringLiteral("Cannot fulfill a directory promise."));
        require(read(directory + QStringLiteral("/中文.txt")) == payload, QStringLiteral("The folder contents differ."));
        m7arc::Callbacks cancelled;
        cancelled.cancelled = [] { return true; };
        const QString cancelledOutput = work.filePath(QStringLiteral("cancelled.txt"));
        try {
            const auto result = m7arc::platform::writeArchivePromise(engine, item, cancelledOutput, cancelled);
            require(result.cancelled, QStringLiteral("Cancellation was ignored."));
        } catch (const m7arc::EngineError &) {
        }
        require(!QFileInfo::exists(cancelledOutput), QStringLiteral("Cancelled output remained."));
        require(QDir(work.path()).entryList({QStringLiteral(".m7arc-drag-*")}, QDir::Dirs | QDir::Hidden).isEmpty(),
                QStringLiteral("A temporary extraction folder remained."));
        std::cout << "File promises: renamed file, directory, cancellation and cleanup passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
