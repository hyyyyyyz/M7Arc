#include "TempStore.h"
#include "Settings.h"

namespace m7arc::app {
std::shared_ptr<QTemporaryDir> TempStore::create(const QString& name) const {
    auto folder = std::make_shared<QTemporaryDir>(Settings().tempDirectory() + "/" + name + "-XXXXXX");
    return folder->isValid() ? folder : nullptr;
}
void TempStore::adopt(std::shared_ptr<QTemporaryDir> folder) {
    folders_.append(std::move(folder));
}
void TempStore::retainFolderOf(const QString& path) {
    for (const auto& folder : folders_)
        if (path.startsWith(folder->path() + "/"))
            folder->setAutoRemove(false);
}
void TempStore::keep(const std::shared_ptr<QTemporaryDir>& folder) {
    folder->setAutoRemove(false);
    if (!folders_.contains(folder))
        folders_.append(folder);
}
QStringList TempStore::paths() const {
    QStringList result;
    for (const auto& folder : folders_)
        result << folder->path();
    return result;
}
} // namespace m7arc::app
