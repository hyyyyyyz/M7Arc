#pragma once
#include <QList>
#include <QStringList>
#include <QTemporaryDir>
#include <memory>

namespace m7arc::app {
// Scratch folders that must outlive a single job: members opened in external programs and files
// handed to the clipboard. They live under the configured temporary directory and are removed
// when the store is destroyed, unless kept for recovery.
class TempStore {
  public:
    // A new folder "<root>/<name>-XXXXXX", or null when it cannot be created. Not tracked until
    // `adopt` is called, so a short lived folder disappears with its last user.
    std::shared_ptr<QTemporaryDir> create(const QString& name) const;
    void adopt(std::shared_ptr<QTemporaryDir> folder);
    // Stops removing the adopted folder that holds `path`, so work in it can be recovered.
    void retainFolderOf(const QString& path);
    // Adopts the folder and leaves it on disk after the store is gone (clipboard files must
    // outlive the application).
    void keep(const std::shared_ptr<QTemporaryDir>& folder);
    QStringList paths() const;

  private:
    QList<std::shared_ptr<QTemporaryDir>> folders_;
};
} // namespace m7arc::app
