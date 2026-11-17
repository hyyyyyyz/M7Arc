#pragma once
#include "app/JobRunner.h"
class QDialog;
class QLabel;
class QProgressBar;
class QPushButton;
class QWidget;
namespace m7arc::ui {
// Widget front end for the job runner: a progress window with pause, background and cancel, and
// the dialogs for passwords, overwrite conflicts, memory requests and error summaries.
class OperationController final : public app::JobRunner {
    Q_OBJECT
  public:
    explicit OperationController(QWidget* window, const EngineOptions& engineOptions = {});
    void cancel();
    void showProgress();

  protected:
    std::optional<QString> requestPassword() override;
    MemoryAnswer requestMemory(const MemoryRequest& request) override;
    OverwriteAnswer requestOverwrite(const OverwriteRequest& request) override;
    bool confirmPasswordRetry(const QString& archive, const QStringList& errors) override;
    std::optional<QString> requestRetryPassword(const QString& archive) override;
    void reportErrors(const QStringList& errors) override;

  private:
    QWidget* window_;
    QDialog* dialog_;
    QLabel* detail_;
    QProgressBar* bar_;
    QPushButton* pause_;
};
} // namespace m7arc::ui
