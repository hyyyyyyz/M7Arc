#pragma once
#include "engine/ArchiveEngine.h"
#include <QDateTime>
#include <QList>
#include <QString>
#include <memory>

namespace m7arc::app {
// An archive member extracted to a temporary file so that an external program can edit it. The
// session remembers the file and archive as they were, to notice edits and to refuse a write-back
// into an archive that changed meanwhile.
struct EditSession {
    QString archivePath;
    QString memberPath;
    QString localPath;
    quint32 index = 0;
    QDateTime archiveModified;
    qint64 archiveSize = 0;
    QDateTime modified;
    qint64 size = 0;
    bool changed() const;
    void remember();
    void rememberArchive();
};
using EditSessionList = QList<std::shared_ptr<EditSession>>;

// The sessions of one running application. Sessions whose temporary file has disappeared are
// dropped whenever the list is consulted.
class EditSessionTracker {
  public:
    std::shared_ptr<EditSession> track(const QString& archivePath, const QString& memberPath,
                                       const QString& localPath, quint32 index);
    std::shared_ptr<EditSession> firstChanged();
    bool anyChanged();
    // The session's own file plus those of the enclosing sessions, innermost first: the files
    // that a write-back of this session also rewrites.
    QStringList chain(const EditSession& session) const;
    // Records the files and archives of the written-back chain as the new baseline.
    void markSaved(const std::shared_ptr<EditSession>& session, const QStringList& chain);
    // The edited file was replaced by the archive's copy.
    void markRestored(const std::shared_ptr<EditSession>& session);
    const EditSessionList& sessions() const {
        return sessions_;
    }

  private:
    void prune();
    EditSessionList sessions_;
};

// Validates the original member index against a fresh snapshot before each rewrite and updates
// outer archives in order when the edited file belongs to a nested archive.
OperationResult saveEditedMember(ArchiveEngine& engine, const EditSession& session,
                                 const EditSessionList& sessions, const Callbacks& callbacks);
// Replaces the edited file with the member as it is in the archive, discarding the edits.
OperationResult restoreEditedMember(ArchiveEngine& engine, const EditSession& session, const Callbacks& callbacks);
} // namespace m7arc::app
