#pragma once
#include "engine/ArchiveEngine.h"
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <atomic>
#include <deque>
#include <functional>
#include <optional>

namespace m7arc::app {
// Runs engine work one job at a time on a worker thread and reports back on the owning thread.
// Jobs submitted while another one runs wait in a queue, so every request ends in its completion
// callback. The decisions only a person can make (passwords, overwrite and memory prompts,
// whether to retry after a wrong password, how to show errors) go through the protected virtual
// functions, which are always called on the owning thread; a front end overrides them.
class JobRunner : public QObject {
    Q_OBJECT
  public:
    using Work = std::function<OperationResult(ArchiveEngine&, const Callbacks&)>;
    using Done = std::function<void(const OperationResult&)>;
    explicit JobRunner(QObject* parent = nullptr, const EngineOptions& engineOptions = {});
    // True while a job is executing. Queued jobs do not count.
    bool busy() const {
        return busy_;
    }
    int queued() const {
        return int(queue_.size());
    }
    // `done` is called exactly once on the owning thread, after the job finishes, fails or is
    // cancelled. Jobs still queued when the runner is destroyed are discarded.
    void run(const QString& title, Work work, Done done = {}, bool allowPasswordRetry = false);
    void cancel();
    // Pauses and resumes progress reporting of the running job; a paused job waits inside the
    // engine's next progress callback.
    void setPaused(bool paused);
    bool paused() const {
        return paused_;
    }
  signals:
    void busyChanged(bool);
    void statusChanged(const QString&);
    void jobStarted(const QString& title);
    void jobQueued(const QString& title);
    void attemptStarted();
    void progressChanged(const QString& phase, const QString& path, quint64 completed, quint64 total,
                         bool totalKnown);

  protected:
    // An empty result cancels the job.
    virtual std::optional<QString> requestPassword();
    virtual MemoryAnswer requestMemory(const MemoryRequest& request);
    virtual OverwriteAnswer requestOverwrite(const OverwriteRequest& request);
    // After a decryption failure: should the same operation run again with a new password?
    virtual bool confirmPasswordRetry(const QString& archive, const QStringList& errors);
    virtual std::optional<QString> requestRetryPassword(const QString& archive);
    virtual void reportErrors(const QStringList& errors);

  private:
    struct Job {
        QString title;
        Work work;
        Done done;
        bool allowPasswordRetry = false;
    };
    Callbacks callbacks();
    void start(Job job);
    void startAttempt();
    void finish();
    bool retryPassword(const OperationResult& result);
    QFutureWatcher<OperationResult> watcher_;
    std::atomic<bool> cancelled_{false}, paused_{false};
    std::atomic<qint64> lastProgress_{0};
    bool busy_ = false;
    Job current_;
    std::deque<Job> queue_;
    EngineOptions engineOptions_;
    QString openedArchive_;
    std::vector<std::pair<std::string, std::string>> passwordOverrides_;
};
} // namespace m7arc::app
