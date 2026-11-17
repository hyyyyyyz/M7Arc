#pragma once
#include "engine/ArchiveEngine.h"
#include <QString>
#include <QStringList>
#include <optional>

namespace m7arc::app {
// "100M", "2g", "512": a positive byte count with an optional K/M/G/T binary suffix.
std::optional<quint64> parseVolumeSize(const QString& text);
// Writes name.001, name.002, ... into `directory`, never replacing an existing volume.
OperationResult splitFile(const QString& source, const QString& directory, quint64 volumeBytes,
                          const Callbacks& callbacks);
// The consecutive volumes starting at a "*.001" file, in order.
QStringList volumeParts(const QString& firstVolume);
// "/a/b.7z.001" -> "/a/b.7z", the natural name of the joined file.
QString joinedName(const QString& firstVolume);
// Concatenates `parts` into `destination` atomically.
OperationResult combineVolumes(const QStringList& parts, const QString& destination, const Callbacks& callbacks);
} // namespace m7arc::app
