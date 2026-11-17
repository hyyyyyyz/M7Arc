#include "JobRunner.h"
#include <QDateTime>
#include <QLocale>
#include <QThread>
#include <QtConcurrentRun>
#include <algorithm>

namespace m7arc::app {
namespace {
// The engine reports the Opening stage while it opens an archive; the path reported then is the archive a
// password failure has to be retried against.
bool isOpeningPhase(const Progress& progress) {
    return progress.stage == Stage::Opening;
}
QStringList toList(const std::vector<std::string>& values) {
    QStringList list;
    for (const auto& value : values)
        list << QString::fromStdString(value);
    return list;
}
} // namespace

JobRunner::JobRunner(QObject* parent, const EngineOptions& engineOptions)
    : QObject(parent), engineOptions_(engineOptions) {
    connect(&watcher_, &QFutureWatcher<OperationResult>::finished, this, &JobRunner::finish);
}

void JobRunner::run(const QString& title, Work work, Done done, bool allowPasswordRetry) {
    Job job{title, std::move(work), std::move(done), allowPasswordRetry};
    if (busy_ || !queue_.empty()) {
        const auto title = job.title;
        queue_.push_back(std::move(job));
        emit jobQueued(title);
        return;
    }
    start(std::move(job));
}

void JobRunner::start(Job job) {
    busy_ = true;
    current_ = std::move(job);
    openedArchive_.clear();
    passwordOverrides_.clear();
    emit jobStarted(current_.title);
    emit busyChanged(true);
    startAttempt();
}

void JobRunner::startAttempt() {
    cancelled_ = false;
    paused_ = false;
    lastProgress_ = 0;
    emit attemptStarted();
    auto cb = callbacks();
    watcher_.setFuture(QtConcurrent::run([work = current_.work, cb, options = engineOptions_] {
        try {
            ArchiveEngine engine(options);
            return work(engine, cb);
        } catch (const EngineError& e) {
            OperationResult result;
            result.cancelled = e.kind() == EngineErrorKind::Cancelled || (cb.cancelled && cb.cancelled());
            result.passwordMayBeIncorrect = e.kind() == EngineErrorKind::PasswordRequired ||
                                            e.kind() == EngineErrorKind::PasswordOrCorruption;
            if (!result.cancelled)
                result.errors.push_back(e.what());
            return result;
        } catch (const std::exception& e) {
            OperationResult result;
            result.cancelled = cb.cancelled && cb.cancelled();
            if (!result.cancelled)
                result.errors.push_back(e.what());
            return result;
        }
    }));
}

void JobRunner::finish() {
    auto result = watcher_.result();
    if (current_.allowPasswordRetry && result.passwordMayBeIncorrect && !result.cancelled &&
        !openedArchive_.isEmpty()) {
        if (retryPassword(result))
            return;
        result.cancelled = true;
        result.errors.clear();
    }
    auto done = std::move(current_.done);
    current_ = {};
    passwordOverrides_.clear();
    busy_ = false;
    emit busyChanged(false);
    emit statusChanged(result.cancelled ? tr("操作已取消")
                       : result.ok()    ? tr("操作完成")
                                        : tr("操作包含错误"));
    if (!result.errors.empty())
        reportErrors(toList(result.errors));
    if (done)
        done(result);
    if (!busy_ && !queue_.empty()) {
        auto next = std::move(queue_.front());
        queue_.pop_front();
        start(std::move(next));
    }
}

bool JobRunner::retryPassword(const OperationResult& result) {
    if (!confirmPasswordRetry(openedArchive_, toList(result.errors)))
        return false;
    const auto password = requestRetryPassword(openedArchive_);
    if (!password)
        return false;
    const auto path = openedArchive_.toStdString();
    auto entry = std::find_if(passwordOverrides_.begin(), passwordOverrides_.end(),
                              [&path](const auto& value) { return value.first == path; });
    if (entry == passwordOverrides_.end())
        passwordOverrides_.emplace_back(path, password->toStdString());
    else
        entry->second = password->toStdString();
    startAttempt();
    return true;
}

void JobRunner::cancel() {
    cancelled_ = true;
    paused_ = false;
}

void JobRunner::setPaused(bool paused) {
    paused_ = paused;
}

Callbacks JobRunner::callbacks() {
    Callbacks cb;
    cb.passwordOverrides = passwordOverrides_;
    cb.cancelled = [this] { return cancelled_.load(); };
    cb.progress = [this](const Progress& p) {
        while (paused_ && !cancelled_)
            QThread::msleep(30);
        if (cancelled_)
            return false;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastProgress_.load() < 80 && p.completed != p.total)
            return true;
        lastProgress_ = now;
        QMetaObject::invokeMethod(
            this,
            [this, p] {
                if (isOpeningPhase(p))
                    openedArchive_ = QString::fromStdString(p.path);
                emit progressChanged(QString::fromStdString(p.phase), QString::fromStdString(p.path),
                                     p.completed, p.total, p.totalKnown);
                emit statusChanged(QString::fromStdString(p.phase) + " " +
                                   QLocale().formattedDataSize(p.completed));
            },
            Qt::QueuedConnection);
        return !cancelled_;
    };
    cb.password = [this]() -> std::optional<std::string> {
        std::optional<std::string> result;
        QMetaObject::invokeMethod(
            this,
            [this, &result] {
                if (const auto password = requestPassword())
                    result = password->toStdString();
                else
                    cancelled_ = true;
            },
            Qt::BlockingQueuedConnection);
        return result;
    };
    cb.memoryRequest = [this](const MemoryRequest& request) {
        MemoryAnswer answer = MemoryAnswer::Cancel;
        QMetaObject::invokeMethod(
            this,
            [this, &request, &answer] {
                if (cancelled_)
                    return;
                answer = requestMemory(request);
                if (answer == MemoryAnswer::Cancel)
                    cancelled_ = true;
            },
            Qt::BlockingQueuedConnection);
        return answer;
    };
    cb.overwrite = [this](const OverwriteRequest& request) {
        OverwriteAnswer answer = OverwriteAnswer::Cancel;
        QMetaObject::invokeMethod(
            this,
            [this, &request, &answer] {
                answer = requestOverwrite(request);
                if (answer == OverwriteAnswer::Cancel)
                    cancelled_ = true;
            },
            Qt::BlockingQueuedConnection);
        return answer;
    };
    return cb;
}

std::optional<QString> JobRunner::requestPassword() {
    return std::nullopt;
}
MemoryAnswer JobRunner::requestMemory(const MemoryRequest&) {
    return MemoryAnswer::Cancel;
}
OverwriteAnswer JobRunner::requestOverwrite(const OverwriteRequest&) {
    return OverwriteAnswer::Cancel;
}
bool JobRunner::confirmPasswordRetry(const QString&, const QStringList&) {
    return false;
}
std::optional<QString> JobRunner::requestRetryPassword(const QString&) {
    return std::nullopt;
}
void JobRunner::reportErrors(const QStringList&) {}
} // namespace m7arc::app
