#include "OperationController.h"
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>
namespace m7arc::ui {
OperationController::OperationController(QWidget* window, const EngineOptions& engineOptions)
    : QObject(window), window_(window), dialog_(new QDialog(window)), detail_(new QLabel(dialog_)),
      bar_(new QProgressBar(dialog_)), pause_(new QPushButton(tr("暂停"), dialog_)) {
    engineOptions_ = engineOptions;
    dialog_->setWindowModality(Qt::NonModal);
    dialog_->setMinimumWidth(510);
    auto* layout = new QVBoxLayout(dialog_);
    layout->setSpacing(15);
    detail_->setWordWrap(true);
    layout->addWidget(detail_);
    layout->addWidget(bar_);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(pause_);
    buttons->addStretch();
    auto* background = new QPushButton(tr("后台"), dialog_);
    auto* cancelButton = new QPushButton(tr("取消"), dialog_);
    buttons->addWidget(background);
    buttons->addWidget(cancelButton);
    layout->addLayout(buttons);
    connect(pause_, &QPushButton::clicked, this, [this] {
        paused_ = !paused_.load();
        pause_->setText(paused_ ? tr("继续") : tr("暂停"));
    });
    connect(background, &QPushButton::clicked, dialog_, &QDialog::hide);
    connect(cancelButton, &QPushButton::clicked, this, &OperationController::cancel);
    connect(&watcher_, &QFutureWatcher<OperationResult>::finished, this, [this] {
        auto result = watcher_.result();
        if (allowPasswordRetry_ && result.passwordMayBeIncorrect && !result.cancelled &&
            !openedArchive_.isEmpty()) {
            if (retryPassword(result))
                return;
            result.cancelled = true;
            result.errors.clear();
        }
        busy_ = false;
        work_ = {};
        passwordOverrides_.clear();
        dialog_->hide();
        emit busyChanged(false);
        emit statusChanged(result.cancelled ? tr("操作已取消")
                           : result.ok()    ? tr("操作完成")
                                            : tr("操作包含错误"));
        if (!result.errors.empty()) {
            QStringList errors;
            for (const auto& s : result.errors)
                errors << QString::fromStdString(s);
            QMessageBox box(QMessageBox::Warning, tr("操作结果"), tr("%1 个错误").arg(errors.size()),
                            QMessageBox::Ok, window_);
            box.setDetailedText(errors.join('\n'));
            box.exec();
        }
        auto finished = std::move(done_);
        done_ = {};
        if (finished)
            finished(result);
    });
}
void OperationController::run(const QString& title, Work work,
                              std::function<void(const OperationResult&)> done, bool allowPasswordRetry) {
    if (busy_) {
        showProgress();
        return;
    }
    busy_ = true;
    allowPasswordRetry_ = allowPasswordRetry;
    work_ = std::move(work);
    openedArchive_.clear();
    passwordOverrides_.clear();
    done_ = std::move(done);
    dialog_->setWindowTitle(title);
    emit busyChanged(true);
    startAttempt();
}
void OperationController::startAttempt() {
    cancelled_ = false;
    paused_ = false;
    lastProgress_ = 0;
    detail_->setText(tr("准备中…"));
    bar_->setRange(0, 0);
    pause_->setText(tr("暂停"));
    dialog_->show();
    auto cb = callbacks();
    watcher_.setFuture(QtConcurrent::run([work = work_, cb, options = engineOptions_] {
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
bool OperationController::retryPassword(const OperationResult& result) {
    QStringList errors;
    for (const auto& error : result.errors)
        errors << QString::fromStdString(error);
    QMessageBox box(
        QMessageBox::Warning, tr("无法解密归档"),
        tr("密码可能不正确，或归档数据已损坏。重新输入密码后将继续相同的操作。\n%1").arg(openedArchive_),
        QMessageBox::Retry | QMessageBox::Cancel, window_);
    box.setDetailedText(errors.join('\n'));
    if (box.exec() != QMessageBox::Retry)
        return false;
    bool accepted = false;
    const auto password = QInputDialog::getText(window_, tr("重新输入密码"), openedArchive_,
                                                QLineEdit::Password, {}, &accepted);
    if (!accepted)
        return false;
    const auto path = openedArchive_.toStdString();
    auto entry = std::find_if(passwordOverrides_.begin(), passwordOverrides_.end(),
                              [&path](const auto& value) { return value.first == path; });
    if (entry == passwordOverrides_.end())
        passwordOverrides_.emplace_back(path, password.toStdString());
    else
        entry->second = password.toStdString();
    startAttempt();
    return true;
}
void OperationController::cancel() {
    cancelled_ = true;
    paused_ = false;
    detail_->setText(tr("正在取消，等待引擎完成收尾…"));
}
void OperationController::showProgress() {
    if (busy_) {
        dialog_->show();
        dialog_->raise();
        dialog_->activateWindow();
    }
}
Callbacks OperationController::callbacks() {
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
                if (p.phase == "Opening")
                    openedArchive_ = QString::fromStdString(p.path);
                detail_->setText(QString::fromStdString(p.phase) + "\n" + QString::fromStdString(p.path));
                if (p.totalKnown && p.total) {
                    bar_->setRange(0, 1000);
                    bar_->setValue(int((static_cast<long double>(p.completed) / p.total) * 1000));
                } else
                    bar_->setRange(0, 0);
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
                bool ok = false;
                auto text = QInputDialog::getText(window_, tr("归档密码"), tr("输入打开此归档所需的密码"),
                                                  QLineEdit::Password, {}, &ok);
                if (ok)
                    result = text.toStdString();
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
                const auto message = tr("归档：%1\n需要内存：%2\n当前允许：%3")
                                         .arg(QString::fromStdString(request.path),
                                              QLocale().formattedDataSize(request.requiredBytes),
                                              QLocale().formattedDataSize(request.allowedBytes));
                if (request.reportOnly) {
                    QMessageBox::information(window_, tr("解压内存需求"), message);
                    answer = MemoryAnswer::Continue;
                    return;
                }
                QMessageBox box(QMessageBox::Question, tr("增加解压内存"), message, QMessageBox::Cancel,
                                window_);
                auto* proceed = box.addButton(tr("允许并继续"), QMessageBox::AcceptRole);
                box.exec();
                if (box.clickedButton() == proceed)
                    answer = MemoryAnswer::Continue;
                else
                    cancelled_ = true;
            },
            Qt::BlockingQueuedConnection);
        return answer;
    };
    cb.overwrite = [this](const OverwriteRequest& r) {
        OverwriteAnswer answer = OverwriteAnswer::Cancel;
        QMetaObject::invokeMethod(
            this,
            [this, &r, &answer] {
                QMessageBox box(
                    QMessageBox::Question, tr("文件已经存在"),
                    tr("目标：%1\n传入：%2")
                        .arg(QString::fromStdString(r.existingPath), QString::fromStdString(r.incomingPath)),
                    QMessageBox::NoButton, window_);
                auto* replace = box.addButton(tr("替换"), QMessageBox::AcceptRole);
                auto* all = box.addButton(tr("全部替换"), QMessageBox::AcceptRole);
                auto* rename = box.addButton(tr("自动重命名"), QMessageBox::ActionRole);
                auto* skip = box.addButton(tr("跳过"), QMessageBox::DestructiveRole);
                auto* skipAll = box.addButton(tr("全部跳过"), QMessageBox::DestructiveRole);
                box.addButton(QMessageBox::Cancel);
                box.exec();
                if (box.clickedButton() == replace)
                    answer = OverwriteAnswer::Replace;
                else if (box.clickedButton() == all)
                    answer = OverwriteAnswer::ReplaceAll;
                else if (box.clickedButton() == rename)
                    answer = OverwriteAnswer::RenameIncoming;
                else if (box.clickedButton() == skip)
                    answer = OverwriteAnswer::Skip;
                else if (box.clickedButton() == skipAll)
                    answer = OverwriteAnswer::SkipAll;
                else
                    cancelled_ = true;
            },
            Qt::BlockingQueuedConnection);
        return answer;
    };
    return cb;
}
} // namespace m7arc::ui
