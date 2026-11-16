#pragma once
#include "ArchiveRows.h"
#include <QString>
#include <QStringList>

namespace m7arc::app {
// What the application layer needs to know about a file pane, captured when a command starts.
// Front ends fill it from their widgets; operations never reach back into the widgets.
struct PaneState {
    QString archivePath;      // the archive being browsed; empty while showing a local folder
    QString archiveDirectory; // the folder inside the archive, with trailing '/', or empty for the root
    QString archiveFormat;    // lower case
    QString localDirectory;   // the folder shown, or the folder holding the archive
    QStringList files;        // selected local items
    QList<ArchiveRow> rows;   // selected archive members
    QList<quint32> indices;   // engine indices that cover `rows`, folders expanded to their contents
    bool inArchive() const {
        return !archivePath.isEmpty();
    }
};
} // namespace m7arc::app
