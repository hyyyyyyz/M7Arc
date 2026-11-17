#pragma once

#include <QString>
#include <QStringList>
#include <functional>

namespace m7arc::platform {

using ArchiveServiceHandler = std::function<void(const QString &action, const QStringList &paths)>;

// Install after the application window can accept add, extract and test requests.
// The callback runs on the Qt application thread and receives every selected path.
void installArchiveServices(ArchiveServiceHandler handler);

// Returns the current default application's bundle identifier, or an empty string.
QString defaultAppForExtension(const QString &extension);
// These settings actions return an empty string on success or per-item errors.
QString setDefaultForExtensions(const QStringList &extensions);
QString installQuickActions();

}
