#pragma once
#include "engine/ArchiveEngine.h"
#include <QStringList>
class QWidget;
namespace m7arc::ui {
class OperationController;
QStringList chooseHashAlgorithms(QWidget* parent, const QStringList& algorithms);
void showHashResults(QWidget* parent, const HashSummary& result);
void verifyHashManifest(QWidget* parent, OperationController* operations, const QString& path);
} // namespace m7arc::ui
