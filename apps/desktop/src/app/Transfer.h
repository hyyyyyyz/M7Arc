#pragma once
#include "PaneState.h"
#include "engine/ArchiveEngine.h"

namespace m7arc::app {
// Options for putting files into an existing archive shown in `target`: its own format, the folder
// inside it and the user's macOS metadata preference. Every path that adds to an archive (drops,
// paste, transfer between panes, new items) uses this one definition.
CreateOptions insertOptions(const PaneState& target);

struct TransferRequest {
    PaneState source;       // files or archive members to move or copy
    PaneState target;       // local folder or archive receiving them
    bool move = false;
    CreateOptions insert;   // how members are written when the target is an archive
    QString scratchRoot;    // where members leaving an archive are staged
};
TransferRequest makeTransfer(const PaneState& source, const PaneState& target, bool move,
                             const QString& scratchRoot);
// Local to local, local to archive, archive to local or archive to archive. With `move` the
// sources are removed once the data has been written; a partly skipped transfer keeps them.
OperationResult runTransfer(ArchiveEngine& engine, const TransferRequest& request, const Callbacks& callbacks);

// Adds an empty file or folder named `name` to the archive in `target`, written with `insert`.
OperationResult createArchiveItem(ArchiveEngine& engine, const PaneState& target, const CreateOptions& insert,
                                  const QString& name, bool directory, const QString& scratchRoot,
                                  const Callbacks& callbacks);
} // namespace m7arc::app
