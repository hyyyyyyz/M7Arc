#include "EditSession.h"
#include <QFileInfo>
#include <algorithm>
namespace m7arc {
bool EditSession::changed() const {
    QFileInfo f(localPath);
    return !ignored && f.exists() && (f.size() != size || f.lastModified() != modified);
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
    ignored = false;
}
OperationResult saveEditedMember(ArchiveEngine& engine, const EditSession& session,
                                 const EditSessions& sessions, const Callbacks& callbacks) {
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
        auto parent = std::find_if(sessions.begin(), sessions.end(), [&current](const auto& s) {
            return s->localPath == current.archivePath && !s->ignored;
        });
        if (parent == sessions.end())
            return result;
        current = **parent;
    }
}
} // namespace m7arc
