#include "SystemClipboard.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>

namespace m7arc::ui {
void SystemClipboard::setFiles(const QStringList& paths, bool cut) {
    auto* mime = new QMimeData;
    QList<QUrl> urls;
    for (const auto& path : paths)
        urls << QUrl::fromLocalFile(path);
    mime->setUrls(urls);
    if (cut)
        mime->setData("application/x-m7arc-cut", "1");
    QApplication::clipboard()->setMimeData(mime);
}
QStringList SystemClipboard::files() const {
    QStringList files;
    for (const auto& url : QApplication::clipboard()->mimeData()->urls())
        if (url.isLocalFile())
            files << url.toLocalFile();
    return files;
}
bool SystemClipboard::isCut() const {
    return QApplication::clipboard()->mimeData()->hasFormat("application/x-m7arc-cut");
}
void SystemClipboard::clear() {
    QApplication::clipboard()->clear();
}
} // namespace m7arc::ui
