#pragma once
#include "ArchiveModel.h"
#include <QString>
class QWidget;
namespace m7arc::ui {
class OperationController;
void showFileMetadata(QWidget* parent, const QString& path, OperationController* jobs);
void showArchiveStreams(QWidget* parent, const QString& archive, const QList<ArchiveRow>& streams,
                        OperationController* jobs);
} // namespace m7arc::ui
