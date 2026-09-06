#pragma once

#include "engine/ArchiveEngine.h"
#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>

class QWidget;

namespace m7arc::platform {

struct ArchivePromiseItem {
    QString name;
    bool directory = false;
    QString archivePath;
    QString memberPath;
    QList<quint32> indices;
};

using PromiseCompletion = std::function<void(const QString &error)>;
using PromiseFulfill = std::function<void(const ArchivePromiseItem &item, const QString &destination,
                                          PromiseCompletion completion)>;

// Call during a mouse drag event. File data is requested only after an accepted drop.
// Fulfill is called serially on the Qt main thread; invoke completion after the worker finishes.
bool beginArchivePromiseDrag(QWidget *source, const QList<ArchivePromiseItem> &items,
                             const QByteArray &internalPayload, PromiseFulfill fulfill);

// Run on the application's engine worker. The destination is the exact URL chosen by Finder.
OperationResult writeArchivePromise(ArchiveEngine &engine, const ArchivePromiseItem &item,
                                   const QString &destination, const Callbacks &callbacks);

}
