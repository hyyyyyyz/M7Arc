#include "app/JobRunner.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace m7arc;

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

namespace {
// Answers every question without a window, recording what was asked.
class ScriptedRunner final : public app::JobRunner {
  public:
    bool retryPasswordWanted = true;
    QString nextPassword = "correct password";
    int retryPrompts = 0, passwordPrompts = 0;
    QStringList reported;

  protected:
    bool confirmPasswordRetry(const QString&, const QStringList&) override {
        ++retryPrompts;
        return retryPasswordWanted;
    }
    std::optional<QString> requestRetryPassword(const QString&) override {
        ++passwordPrompts;
        return nextPassword;
    }
    void reportErrors(const QStringList& errors) override {
        reported << errors;
    }
};

void waitUntil(const std::function<bool()>& condition, const char* message) {
    QEventLoop loop;
    QTimer poll, limit;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (condition())
            loop.quit();
    });
    limit.setSingleShot(true);
    QObject::connect(&limit, &QTimer::timeout, &loop, [&] {
        std::cerr << message << '\n';
        std::_Exit(1);
    });
    poll.start(5);
    limit.start(15000);
    loop.exec();
}

void testQueueKeepsEveryRequest() {
    ScriptedRunner runner;
    QStringList order;
    int busyChanges = 0;
    QObject::connect(&runner, &app::JobRunner::busyChanged, [&](bool) { ++busyChanges; });
    for (const auto* name : {"first", "second", "third"})
        runner.run(
            name,
            [name](ArchiveEngine&, const Callbacks&) {
                QThread::msleep(20);
                OperationResult result;
                result.completedItems = 1;
                return result;
            },
            [&order, name, &runner](const OperationResult& result) {
                require(result.ok() && result.completedItems == 1, "Queued job lost its result");
                require(!runner.busy(), "Completion callback ran while still busy");
                order << name;
            });
    require(runner.busy() && runner.queued() == 2, "Second and third request were not queued");
    waitUntil([&] { return order.size() == 3; }, "Queued jobs did not all complete");
    require(order == QStringList({"first", "second", "third"}), "Queue ran out of order");
    require(!runner.busy() && runner.queued() == 0, "Runner stayed busy after the queue drained");
    require(busyChanges >= 2 && busyChanges % 2 == 0, "busyChanged calls are not paired");
}

void testRequestFromCompletionRunsAfterQueue() {
    ScriptedRunner runner;
    QStringList order;
    auto quick = [](ArchiveEngine&, const Callbacks&) { return OperationResult{}; };
    runner.run("a", quick, [&](const OperationResult&) {
        order << "a";
        runner.run("c", quick, [&](const OperationResult&) { order << "c"; });
    });
    runner.run("b", quick, [&](const OperationResult&) { order << "b"; });
    waitUntil([&] { return order.size() == 3; }, "Jobs did not complete");
    require(order == QStringList({"a", "b", "c"}), "A request made from a completion jumped the queue");
}

void testCancelAndErrors() {
    ScriptedRunner runner;
    bool done = false;
    OperationResult cancelled;
    runner.run(
        "spin",
        [](ArchiveEngine&, const Callbacks& callbacks) {
            while (!callbacks.cancelled())
                QThread::msleep(2);
            throw EngineError("stopped", EngineErrorKind::Cancelled);
            return OperationResult{};
        },
        [&](const OperationResult& result) {
            cancelled = result;
            done = true;
        });
    QTimer::singleShot(30, &runner, [&] { runner.cancel(); });
    waitUntil([&] { return done; }, "Cancelled job did not complete");
    require(cancelled.cancelled && cancelled.errors.empty() && runner.reported.isEmpty(),
            "Cancellation was reported as an error");

    done = false;
    OperationResult failed;
    runner.run(
        "fail", [](ArchiveEngine&, const Callbacks&) -> OperationResult { throw std::runtime_error("boom"); },
        [&](const OperationResult& result) {
            failed = result;
            done = true;
        });
    waitUntil([&] { return done; }, "Failed job did not complete");
    require(!failed.ok() && failed.errors.size() == 1 && failed.errors[0] == "boom", "Exception lost");
    require(runner.reported == QStringList{"boom"}, "Errors were not handed to the front end");
}

void testPasswordRetry(const QString& archive, const QString& destination, bool accept) {
    ScriptedRunner runner;
    runner.retryPasswordWanted = accept;
    int attempts = 0, completed = 0;
    OperationResult final;
    ExtractOptions options;
    options.password = "incorrect password";
    options.overwrite = Overwrite::Replace;
    runner.run(
        "extract",
        [&, options](ArchiveEngine& engine, const Callbacks& callbacks) {
            ++attempts;
            return engine.extract(archive.toStdString(), destination.toStdString(), {0}, options, callbacks);
        },
        [&](const OperationResult& result) {
            ++completed;
            final = result;
        },
        true);
    waitUntil([&] { return completed > 0; }, "Password recovery did not complete");
    require(completed == 1 && !runner.busy(), "Completion callback ran more than once");
    require(runner.retryPrompts == 1, "Wrong password did not ask once about retrying");
    require(attempts == (accept ? 2 : 1), "Retry did not rerun the operation");
    require(runner.passwordPrompts == (accept ? 1 : 0), "Unexpected password prompt");
    if (accept)
        require(final.ok(), "Corrected password did not recover the extraction");
    else
        require(final.cancelled && final.errors.empty() && runner.reported.isEmpty(),
                "Declining the retry did not cancel cleanly");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication qtApp(argc, argv);
    try {
        testQueueKeepsEveryRequest();
        testRequestFromCompletionRunsAfterQueue();
        testCancelAndErrors();
        QTemporaryDir temp;
        require(temp.isValid(), "Temporary directory unavailable");
        for (const auto* name : {"selected.txt", "unselected.txt"}) {
            QFile file(temp.filePath(name));
            require(file.open(QIODevice::WriteOnly), "Cannot write input");
            file.write("payload\n");
        }
        ArchiveEngine engine;
        CreateOptions create;
        create.password = "correct password";
        create.preserveMacMetadata = false;
        create.encryptHeaders = true;
        const auto archive = temp.filePath("encrypted.7z");
        require(engine
                    .create({temp.filePath("selected.txt").toStdString(),
                             temp.filePath("unselected.txt").toStdString()},
                            archive.toStdString(), create)
                    .ok(),
                "Cannot create encrypted fixture");
        testPasswordRetry(archive, temp.filePath("accepted"), true);
        testPasswordRetry(archive, temp.filePath("declined"), false);
        std::cout << "Job runner queued, cancelled and recovered passwords without a window\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
