#include "Workspace.h"
#include "FileSplit.h"
#include "LocalTransfer.h"
#include "Transfer.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <unistd.h>

namespace m7arc::app {
namespace {
// Runs `step` for each archive in turn and merges the outcomes; stops at a cancellation or when a
// password may be wrong, so the runner can ask for it and repeat the whole operation.
OperationResult forEachArchive(const QStringList& archives,
                               const std::function<OperationResult(const std::string&)>& step) {
    OperationResult total;
    for (const auto& archive : archives) {
        auto r = step(archive.toStdString());
        total.errors.insert(total.errors.end(), r.errors.begin(), r.errors.end());
        total.cancelled |= r.cancelled;
        total.passwordMayBeIncorrect |= r.passwordMayBeIncorrect;
        total.completedItems += r.completedItems;
        if (r.cancelled || r.passwordMayBeIncorrect)
            break;
    }
    return total;
}
} // namespace

Workspace::Workspace(JobRunner& jobs, ClipboardPort& clipboard, QObject* parent)
    : QObject(parent), jobs_(jobs), clipboard_(clipboard) {}

void Workspace::remember(const ArchiveListing& listing) {
    writable_[listing.path] = listing.canUpdate;
    commentWritable_[listing.path] = listing.canSetComment;
}

void Workspace::openArchive(const QString& path, const QString& type,
                            std::function<void(const ArchiveListing&)> opened) {
    auto listing = std::make_shared<ArchiveListing>();
    OpenOptions options;
    options.type = type.toStdString();
    jobs_.run(
        tr("打开归档"),
        [path, options, listing](ArchiveEngine& e, const Callbacks& cb) {
            *listing = toListing(e.list(path.toStdString(), options, cb));
            return OperationResult{};
        },
        [this, path, listing, opened = std::move(opened)](const OperationResult& r) {
            if (!r.ok())
                return;
            listing->path = path;
            remember(*listing);
            if (opened)
                opened(*listing);
        },
        true);
}

void Workspace::reloadArchives(const QStringList& paths,
                               std::function<void(const QList<ArchiveListing>&)> reloaded) {
    auto listings = std::make_shared<QList<ArchiveListing>>();
    jobs_.run(
        tr("刷新归档"),
        [paths, listings](ArchiveEngine& e, const Callbacks& cb) {
            for (const auto& path : paths) {
                auto listing = toListing(e.list(path.toStdString(), {}, cb));
                listing.path = path;
                listings->append(listing);
            }
            return OperationResult{};
        },
        [this, listings, reloaded = std::move(reloaded)](const OperationResult& r) {
            if (!r.ok())
                return;
            for (const auto& listing : *listings)
                remember(listing);
            if (reloaded)
                reloaded(*listings);
        },
        true);
}

bool Workspace::openMember(const QString& archive, const ArchiveRow& row, bool internal,
                           std::function<void(const OpenedMember&)> ready) {
    auto folder = temp_.create("M7Arc");
    if (!folder)
        return false;
    temp_.adopt(folder);
    const auto path = QDir(folder->path()).filePath(row.path);
    jobs_.run(
        tr("打开归档成员"),
        [archive, folder, row](ArchiveEngine& e, const Callbacks& cb) {
            return e.extract(archive.toStdString(), folder->path().toStdString(), {row.index}, {}, cb);
        },
        [this, archive, path, row, internal, ready = std::move(ready)](const OperationResult& r) {
            if (!r.ok())
                return;
            edits_.track(archive, row.path, path, row.index);
            ready({path, internal && isArchiveName(path)});
        },
        true);
    return true;
}

void Workspace::settleEdit(const std::shared_ptr<EditSession>& session, bool save, Done done) {
    const auto chain = edits_.chain(*session);
    const auto sessions = edits_.sessions();
    jobs_.run(
        save ? tr("保存归档内编辑") : tr("恢复归档内文件"),
        [session, sessions, save](ArchiveEngine& e, const Callbacks& cb) {
            return save ? saveEditedMember(e, *session, sessions, cb) : restoreEditedMember(e, *session, cb);
        },
        [this, session, chain, save, done = std::move(done)](const OperationResult& r) {
            if (r.ok()) {
                if (save)
                    edits_.markSaved(session, chain);
                else
                    edits_.markRestored(session);
            } else
                temp_.retainFolderOf(session->localPath);
            if (done)
                done(r);
        });
}

void Workspace::createArchive(const QStringList& inputs, const QString& path, const CreateOptions& options,
                              Done done) {
    jobs_.run(
        tr("创建 / 更新归档"),
        [inputs, path, options](ArchiveEngine& e, const Callbacks& cb) {
            return e.create(toStdStrings(inputs), path.toStdString(), options, cb);
        },
        std::move(done));
}

void Workspace::extract(const QStringList& archives, const QString& target, const QList<quint32>& indices,
                        const ExtractOptions& options, Done done) {
    const auto ids = toIndexVector(indices);
    jobs_.run(
        tr("提取文件"),
        [archives, target, ids, options](ArchiveEngine& e, const Callbacks& cb) {
            return forEachArchive(archives, [&](const std::string& archive) {
                return e.extract(archive, target.toStdString(), ids, options, cb);
            });
        },
        std::move(done), true);
}

void Workspace::test(const QStringList& archives, const QList<quint32>& indices, Done done) {
    const auto ids = toIndexVector(indices);
    jobs_.run(
        tr("测试归档"),
        [archives, ids](ArchiveEngine& e, const Callbacks& cb) {
            return forEachArchive(archives,
                                  [&](const std::string& archive) { return e.test(archive, ids, {}, cb); });
        },
        std::move(done), true);
}

void Workspace::transfer(const PaneState& source, const PaneState& target, bool move, Done done) {
    const auto request = makeTransfer(source, target, move, Settings().tempDirectory());
    jobs_.run(
        move ? tr("移动项目") : tr("复制项目"),
        [request](ArchiveEngine& e, const Callbacks& cb) { return runTransfer(e, request, cb); }, std::move(done));
}

void Workspace::removeSelection(const PaneState& pane, Done done) {
    jobs_.run(
        tr("删除"),
        [pane](ArchiveEngine& e, const Callbacks& cb) {
            if (pane.inArchive())
                return e.remove(pane.archivePath.toStdString(), toIndexVector(pane.indices), {}, cb);
            OperationResult r;
            for (const auto& f : pane.files) {
                if (cb.cancelled && cb.cancelled()) {
                    r.cancelled = true;
                    break;
                }
                if (!QFile::moveToTrash(f))
                    r.errors.push_back(QObject::tr("无法移到废纸篓：%1").arg(f).toStdString());
                else
                    ++r.completedItems;
            }
            return r;
        },
        std::move(done));
}

void Workspace::renameMember(const PaneState& pane, const ArchiveRow& row, const QString& newName, Done done) {
    const QString oldPath = row.path.endsWith('/') ? row.path.chopped(1) : row.path;
    QString newPath = QFileInfo(oldPath).path();
    if (newPath == ".")
        newPath.clear();
    else
        newPath += '/';
    newPath += newName;
    const auto archive = pane.archivePath;
    jobs_.run(
        tr("重命名归档成员"),
        [archive, row, newPath](ArchiveEngine& e, const Callbacks& cb) {
            if (row.directory)
                return e.renameDirectory(archive.toStdString(), row.path.toStdString(), newPath.toStdString(), {},
                                         cb);
            return e.rename(archive.toStdString(), row.index, newPath.toStdString(), {}, cb);
        },
        std::move(done));
}

bool Workspace::renameLocal(const QString& path, const QString& newName) {
    return QDir().rename(path, QFileInfo(path).dir().filePath(newName));
}

bool Workspace::memberNameFree(const QList<ArchiveRow>& entries, const QString& archiveDirectory,
                               const QString& name) {
    const auto fullName = archiveDirectory + name;
    for (const auto& entry : entries)
        if (entry.path == fullName || entry.path.startsWith(fullName + '/'))
            return false;
    return true;
}

void Workspace::createArchiveItem(const PaneState& pane, const QString& name, bool directory, Done done) {
    const auto insert = insertOptions(pane);
    const auto scratch = Settings().tempDirectory();
    jobs_.run(
        directory ? tr("新建归档文件夹") : tr("新建归档文件"),
        [pane, insert, name, directory, scratch](ArchiveEngine& e, const Callbacks& cb) {
            return m7arc::app::createArchiveItem(e, pane, insert, name, directory, scratch, cb);
        },
        std::move(done));
}

bool Workspace::createLocalItem(const QString& directory, const QString& name, bool directoryItem) {
    const auto path = QDir(directory).filePath(name);
    return directoryItem ? QDir().mkdir(path) : QFile(path).open(QIODevice::WriteOnly | QIODevice::NewOnly);
}

bool Workspace::createLink(const QString& target, const QString& linkPath, bool symbolic) {
    const auto from = QFile::encodeName(target);
    const auto to = QFile::encodeName(linkPath);
    return (symbolic ? ::symlink(from.constData(), to.constData()) : ::link(from.constData(), to.constData())) == 0;
}

void Workspace::setComment(const QString& archive, const ArchiveRow& row, const QString& comment, Done done) {
    jobs_.run(
        tr("保存注释"),
        [archive, row, comment](ArchiveEngine& e, const Callbacks& cb) {
            return e.setComment(archive.toStdString(), row.index, comment.toStdString(), {}, cb);
        },
        std::move(done));
}

bool Workspace::copyMembersToClipboard(const PaneState& pane, std::function<void(int)> copied) {
    auto folder = temp_.create("M7Arc-Clipboard");
    if (!folder)
        return false;
    jobs_.run(
        tr("复制归档成员到剪贴板"),
        [archive = pane.archivePath, ids = toIndexVector(pane.indices), folder](ArchiveEngine& e,
                                                                              const Callbacks& cb) {
            return e.extract(archive.toStdString(), folder->path().toStdString(), ids, {}, cb);
        },
        [this, rows = pane.rows, folder, copied = std::move(copied)](const OperationResult& r) {
            if (!r.ok())
                return;
            QStringList files;
            for (const auto& row : outermostRows(rows))
                files << QDir(folder->path()).filePath(row.path);
            clipboard_.setFiles(files, false);
            // Finder may paste after the application closes, so clipboard files outlive this window.
            temp_.keep(folder);
            if (copied)
                copied(int(files.size()));
        },
        true);
    return true;
}

void Workspace::copyFilesToClipboard(const QStringList& files, bool cut) {
    clipboard_.setFiles(files, cut);
}

bool Workspace::pasteFromClipboard(const PaneState& target, Done done) {
    const auto files = clipboard_.files();
    if (files.isEmpty() || (target.inArchive() && !writable(target.archivePath)))
        return false;
    const bool move = clipboard_.isCut();
    PaneState source;
    source.files = files;
    const auto request = makeTransfer(source, target, move, Settings().tempDirectory());
    jobs_.run(
        move ? tr("移动文件") : tr("粘贴文件"),
        [request](ArchiveEngine& e, const Callbacks& cb) { return runTransfer(e, request, cb); },
        [this, move, done = std::move(done)](const OperationResult& r) {
            if (move && r.ok())
                clipboard_.clear();
            if (done)
                done(r);
        });
    return true;
}

bool Workspace::hash(const PaneState& pane, const QStringList& algorithms, std::shared_ptr<HashSummary> summary,
                     Done done) {
    std::shared_ptr<QTemporaryDir> folder;
    if (pane.inArchive()) {
        folder = temp_.create("M7Arc-hash");
        if (!folder)
            return false;
    }
    ExtractOptions extraction;
    extraction.pathMode = PathMode::Current;
    extraction.currentDirectory = pane.archiveDirectory.toStdString();
    extraction.overwrite = Overwrite::Replace;
    jobs_.run(
        tr("计算摘要"),
        [inputs = pane.files, algorithms, folder, archive = pane.archivePath, ids = toIndexVector(pane.indices),
         extraction, summary](ArchiveEngine& engine, const Callbacks& callbacks) {
            auto paths = inputs;
            if (folder) {
                auto extracted =
                    engine.extract(archive.toStdString(), folder->path().toStdString(), ids, extraction, callbacks);
                if (!extracted.ok())
                    return extracted;
                const QDir directory(folder->path());
                for (const auto& item : directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                                QDir::Hidden | QDir::System))
                    paths << item.absoluteFilePath();
            }
            *summary = engine.hashSummary(toStdStrings(paths), toStdStrings(algorithms), callbacks);
            if (folder)
                for (auto& file : summary->files)
                    file.path = QDir(folder->path()).relativeFilePath(QString::fromStdString(file.path)).toStdString();
            OperationResult result;
            result.cancelled = callbacks.cancelled && callbacks.cancelled();
            return result;
        },
        std::move(done), true);
    return true;
}

void Workspace::splitFile(const QString& source, const QString& directory, quint64 volumeBytes, Done done) {
    jobs_.run(
        tr("分割文件"),
        [source, directory, volumeBytes](ArchiveEngine&, const Callbacks& cb) {
            return m7arc::app::splitFile(source, directory, volumeBytes, cb);
        },
        std::move(done));
}

void Workspace::combineFiles(const QStringList& parts, const QString& destination, Done done) {
    jobs_.run(
        tr("合并文件"),
        [parts, destination](ArchiveEngine&, const Callbacks& cb) { return combineVolumes(parts, destination, cb); },
        std::move(done));
}
} // namespace m7arc::app
