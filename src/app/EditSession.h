#pragma once
#include "engine/ArchiveEngine.h"
#include <QDateTime>
#include <QList>
#include <QString>
#include <memory>
namespace m7arc {
struct EditSession {
    QString archivePath;
    QString memberPath;
    QString localPath;
    quint32 index = 0;
    QDateTime archiveModified;
    qint64 archiveSize = 0;
    QDateTime modified;
    qint64 size = 0;
    bool nestedArchive = false;
    bool ignored = false;
    bool changed() const;
    void remember();
    void rememberArchive();
};
using EditSessions = QList<std::shared_ptr<EditSession>>;
// Validates the original member index against a fresh snapshot before each rewrite and updates
// outer archives in order when the edited file belongs to a nested archive.
OperationResult saveEditedMember(ArchiveEngine& engine, const EditSession& session,
                                 const EditSessions& sessions, const Callbacks& callbacks);
} // namespace m7arc
