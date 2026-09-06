#pragma once
#include "engine/ArchiveEngine.h"
#include <QFutureWatcher>
#include <QObject>
#include <atomic>
#include <functional>
class QDialog;
class QLabel;
class QProgressBar;
class QPushButton;
namespace m7arc::ui {
class OperationController final : public QObject {
    Q_OBJECT
  public:
    using Work = std::function<OperationResult(ArchiveEngine&, const Callbacks&)>;
    explicit OperationController(QWidget* window, const EngineOptions& engineOptions = {});
    bool busy() const {
        return busy_;
    }
    void run(const QString& title, Work work, std::function<void(const OperationResult&)> done = {},
             bool allowPasswordRetry = false);
    void cancel();
    void showProgress();
  signals:
    void busyChanged(bool);
    void statusChanged(const QString&);

  private:
    Callbacks callbacks();
    void startAttempt();
    bool retryPassword(const OperationResult& result);
    QWidget* window_;
    QDialog* dialog_;
    QLabel* detail_;
    QProgressBar* bar_;
    QPushButton* pause_;
    QFutureWatcher<OperationResult> watcher_;
    std::atomic<bool> cancelled_{false}, paused_{false};
    std::atomic<qint64> lastProgress_{0};
    bool busy_ = false;
    bool allowPasswordRetry_ = false;
    Work work_;
    EngineOptions engineOptions_;
    QString openedArchive_;
    std::vector<std::pair<std::string, std::string>> passwordOverrides_;
    std::function<void(const OperationResult&)> done_;
};
} // namespace m7arc::ui
