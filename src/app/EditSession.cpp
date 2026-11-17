#include "EditSession.h"
#include <QFileInfo>
#include <algorithm>

namespace m7arc::app {
bool EditSession::changed() const {
    QFileInfo f(localPath);
    return f.exists() && (f.size() != size || f.lastModified() != modified);
}
void EditSession::rememberArchive() {
    QFileInfo f(archivePath);
    archiveModified = f.lastModified();
    archiveSize = f.size();
}
void EditSession::remember() {
    QFileInfo f(localPath);
    modified = f.lastModified();
    size = f.size();
}

std::shared_ptr<EditSession> EditSessionTracker::track(const QString& archivePath, const QString& memberPath,
                                                       const QString& localPath, quint32 index) {
    auto session = std::make_shared<EditSession>();
    session->archivePath = archivePath;
    session->memberPath = memberPath;
    session->localPath = localPath;
    session->index = index;
    session->rememberArchive();
    session->remember();
    sessions_.append(session);
    return session;
}
void EditSessionTracker::prune() {
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [](const auto& s) { return !QFileInfo::exists(s->localPath); }),
                    sessions_.end());
}
std::shared_ptr<EditSession> EditSessionTracker::firstChanged() {
    prune();
    auto found = std::find_if(sessions_.begin(), sessions_.end(), [](const auto& s) { return s->changed(); });
    return found == sessions_.end() ? nullptr : *found;
}
bool EditSessionTracker::anyChanged() {
    return firstChanged() != nullptr;
}
QStringList EditSessionTracker::chain(const EditSession& session) const {
    QStringList files{session.localPath};
    QString parentPath = session.archivePath;
    for (;;) {
        auto parent = std::find_if(sessions_.begin(), sessions_.end(),
                                   [&](const auto& s) { return s->localPath == parentPath; });
        if (parent == sessions_.end() || files.contains((*parent)->localPath))
            break;
        files << (*parent)->localPath;
        parentPath = (*parent)->archivePath;
    }
    return files;
}
void EditSessionTracker::markSaved(const std::shared_ptr<EditSession>& session, const QStringList& chain) {
    for (const auto& s : sessions_)
        if (s == session || chain.contains(s->localPath)) {
            s->remember();
            s->rememberArchive();
        }
}
void EditSessionTracker::markRestored(const std::shared_ptr<EditSession>& session) {
    session->remember();
    session->rememberArchive();
}

OperationResult saveEditedMember(ArchiveEngine& engine, const EditSession& session,
                                 const EditSessionList& sessions, const Callbacks& callbacks) {
    auto current = session;
    OperationResult result;
    for (;;) {
        QFileInfo archive(current.archivePath);
        if (archive.size() != current.archiveSize || archive.lastModified() != current.archiveModified)
            throw EngineError("The archive changed while its member was being edited. Reopen the archive; "
                              "the edited file remains in its temporary directory");
        auto snapshot = engine.list(current.archivePath.toStdString(), {}, callbacks);
        if (!snapshot.canUpdate)
            throw EngineError("The parent archive is read-only");
        auto found =
            std::find_if(snapshot.entries.begin(), snapshot.entries.end(), [&current](const auto& e) {
                return e.index == current.index && QString::fromStdString(e.path) == current.memberPath;
            });
        if (found == snapshot.entries.end())
            throw EngineError(
                "The archive member no longer exists; the edited file remains in the temporary directory");
        result = engine.replace(current.archivePath.toStdString(), found->index,
                                current.localPath.toStdString(), {}, callbacks);
        if (!result.ok())
            return result;
        auto parent = std::find_if(sessions.begin(), sessions.end(),
                                   [&current](const auto& s) { return s->localPath == current.archivePath; });
        if (parent == sessions.end())
            return result;
        current = **parent;
    }
}

OperationResult restoreEditedMember(ArchiveEngine& engine, const EditSession& session, const Callbacks& callbacks) {
    QFileInfo archive(session.archivePath);
    if (archive.size() != session.archiveSize || archive.lastModified() != session.archiveModified)
        throw EngineError("The archive changed; reopen it before restoring the edited member");
    auto snapshot = engine.list(session.archivePath.toStdString(), {}, callbacks);
    auto entry = std::find_if(snapshot.entries.begin(), snapshot.entries.end(), [&](const auto& v) {
        return v.index == session.index && QString::fromStdString(v.path) == session.memberPath;
    });
    if (entry == snapshot.entries.end())
        throw EngineError("The original archive member no longer exists");
    QString root = session.localPath;
    root.chop(session.memberPath.size());
    ExtractOptions options;
    options.overwrite = Overwrite::Replace;
    return engine.extract(session.archivePath.toStdString(), root.toStdString(), {entry->index}, options,
                          callbacks);
}
} // namespace m7arc::app
