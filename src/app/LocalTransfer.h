#pragma once
#include "engine/ArchiveEngine.h"
#include <QString>
#include <QStringList>

namespace m7arc::app {
// Copies or moves files and folders between local directories, keeping macOS metadata. Existing
// targets are resolved through callbacks.overwrite. Each item is staged next to its destination,
// so a failed or cancelled copy never leaves a half written target.
OperationResult transferLocal(const QStringList& sources, const QString& directory, bool move,
                              const Callbacks& callbacks);
bool removePath(const QString& path);
// "name.ext" -> "name_1.ext", the first free sibling name.
QString renamedPath(const QString& path);
} // namespace m7arc::app
