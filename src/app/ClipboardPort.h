#pragma once
#include <QStringList>

namespace m7arc::app {
// The file clipboard as the application layer sees it: a list of local files that were copied
// or cut. A front end maps it onto the system clipboard.
class ClipboardPort {
  public:
    virtual ~ClipboardPort() = default;
    virtual void setFiles(const QStringList& paths, bool cut) = 0;
    virtual QStringList files() const = 0;
    virtual bool isCut() const = 0;
    virtual void clear() = 0;
};
} // namespace m7arc::app
