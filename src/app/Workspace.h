#pragma once
#include "ArchiveRows.h"
#include "ClipboardPort.h"
#include "EditSession.h"
#include "JobRunner.h"
#include "PaneState.h"
#include "Settings.h"
#include "TempStore.h"
#include <QHash>
#include <QObject>
#include <functional>
#include <memory>
#include <optional>

namespace m7arc::app {
// The operations behind the commands of a file manager window: opening and refreshing archives,
// extracting, creating, testing, transferring between panes, editing members in external programs,
// clipboard, hashing, splitting. Each call prepares engine work, hands it to the job runner and
// reports the outcome through a callback. It never shows anything; questions for the user are
// answered by the job runner's front end, and the caller handles what happens on screen.
class Workspace : public QObject {
    Q_OBJECT
  public:
    using Done = JobRunner::Done;
    Workspace(JobRunner& jobs, ClipboardPort& clipboard, QObject* parent = nullptr);
    JobRunner& jobs() {
        return jobs_;
    }
    TempStore& temp() {
        return temp_;
    }
    EditSessionTracker& edits() {
        return edits_;
    }

    // What the engine reported when each archive was last listed.
    bool writable(const QString& archive) const {
        return writable_.value(archive);
    }
    bool commentWritable(const QString& archive) const {
        return commentWritable_.value(archive);
    }

    void openArchive(const QString& path, const QString& type,
                     std::function<void(const ArchiveListing&)> opened);
    void reloadArchives(const QStringList& paths, std::function<void(const QList<ArchiveListing>&)> reloaded);

    struct OpenedMember {
        QString localPath;
        bool archive = false; // opened as a nested archive rather than for an external program
    };
    // Extracts one member and starts watching it for edits. False when no scratch folder exists.
    bool openMember(const QString& archive, const ArchiveRow& row, bool internal,
                    std::function<void(const OpenedMember&)> ready);
    // Writes edits back into the archive (and the archives around it), or restores the member.
    void settleEdit(const std::shared_ptr<EditSession>& session, bool save, Done done);

    void createArchive(const QStringList& inputs, const QString& path, const CreateOptions& options, Done done);
    // Extracts each archive into `target`; a nonempty `indices` selects members of the single archive.
    void extract(const QStringList& archives, const QString& target, const QList<quint32>& indices,
                 const ExtractOptions& options, Done done);
    void test(const QStringList& archives, const QList<quint32>& indices, Done done);
    void transfer(const PaneState& source, const PaneState& target, bool move, Done done);

    // Local items go to the trash; members are removed from their archive.
    void removeSelection(const PaneState& pane, Done done);
    void renameMember(const PaneState& pane, const ArchiveRow& row, const QString& newName, Done done);
    bool renameLocal(const QString& path, const QString& newName);
    void createArchiveItem(const PaneState& pane, const QString& name, bool directory, Done done);
    bool createLocalItem(const QString& directory, const QString& name, bool directoryItem);
    // True while `name` is not used by an entry of the archive folder in `pane`.
    static bool memberNameFree(const QList<ArchiveRow>& entries, const QString& archiveDirectory, const QString& name);
    bool createLink(const QString& target, const QString& linkPath, bool symbolic);
    void setComment(const QString& archive, const ArchiveRow& row, const QString& comment, Done done);

    // Copies the selected members to the clipboard as files; `copied` gets how many.
    bool copyMembersToClipboard(const PaneState& pane, std::function<void(int count)> copied);
    void copyFilesToClipboard(const QStringList& files, bool cut);
    // False when there is nothing to paste or the target cannot take it.
    bool pasteFromClipboard(const PaneState& target, Done done);
    QStringList clipboardFiles() const {
        return clipboard_.files();
    }

    // Hashes local files, or the selected members of an archive after extracting them.
    bool hash(const PaneState& pane, const QStringList& algorithms, std::shared_ptr<HashSummary> summary, Done done);
    void splitFile(const QString& source, const QString& directory, quint64 volumeBytes, Done done);
    void combineFiles(const QStringList& parts, const QString& destination, Done done);

  private:
    void remember(const ArchiveListing& listing);
    JobRunner& jobs_;
    ClipboardPort& clipboard_;
    TempStore temp_;
    EditSessionTracker edits_;
    QHash<QString, bool> writable_;
    QHash<QString, bool> commentWritable_;
};
} // namespace m7arc::app
