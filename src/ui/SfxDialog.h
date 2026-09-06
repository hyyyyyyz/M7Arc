#pragma once
#include <QStringList>
#include <functional>
class QWidget;
namespace m7arc::ui {
class OperationController;
void showSfxCreation(QWidget* parent, const QStringList& inputs, const QString& directory,
                     OperationController* jobs, std::function<void()> completed = {});
} // namespace m7arc::ui
