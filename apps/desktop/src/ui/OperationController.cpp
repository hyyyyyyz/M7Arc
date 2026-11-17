#include "OperationController.h"
#include <QDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
namespace m7arc::ui {
OperationController::OperationController(QWidget* window, const EngineOptions& engineOptions)
    : JobRunner(window, engineOptions), window_(window), dialog_(new QDialog(window)),
      detail_(new QLabel(dialog_)), bar_(new QProgressBar(dialog_)), pause_(new QPushButton(tr("暂停"), dialog_)) {
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
        setPaused(!paused());
        pause_->setText(paused() ? tr("继续") : tr("暂停"));
    });
    connect(background, &QPushButton::clicked, dialog_, &QDialog::hide);
    connect(cancelButton, &QPushButton::clicked, this, &OperationController::cancel);
    connect(this, &JobRunner::jobQueued, this, &OperationController::showProgress);
    connect(this, &JobRunner::jobStarted, dialog_, &QWidget::setWindowTitle);
    connect(this, &JobRunner::attemptStarted, this, [this] {
        detail_->setText(tr("准备中…"));
        bar_->setRange(0, 0);
        pause_->setText(tr("暂停"));
        dialog_->show();
    });
    connect(this, &JobRunner::busyChanged, this, [this](bool busy) {
        if (!busy)
            dialog_->hide();
    });
    connect(this, &JobRunner::progressChanged, this,
            [this](const QString& phase, const QString& path, quint64 completed, quint64 total, bool known) {
                detail_->setText(phase + "\n" + path);
                if (known && total) {
                    bar_->setRange(0, 1000);
                    bar_->setValue(int((static_cast<long double>(completed) / total) * 1000));
                } else
                    bar_->setRange(0, 0);
            });
}
void OperationController::cancel() {
    JobRunner::cancel();
    detail_->setText(tr("正在取消，等待引擎完成收尾…"));
}
void OperationController::showProgress() {
    if (busy()) {
        dialog_->show();
        dialog_->raise();
        dialog_->activateWindow();
    }
}
std::optional<QString> OperationController::requestPassword() {
    bool ok = false;
    auto text = QInputDialog::getText(window_, tr("归档密码"), tr("输入打开此归档所需的密码"),
                                      QLineEdit::Password, {}, &ok);
    if (!ok)
        return std::nullopt;
    return text;
}
MemoryAnswer OperationController::requestMemory(const MemoryRequest& request) {
    const auto message = tr("归档：%1\n需要内存：%2\n当前允许：%3")
                             .arg(QString::fromStdString(request.path),
                                  QLocale().formattedDataSize(request.requiredBytes),
                                  QLocale().formattedDataSize(request.allowedBytes));
    if (request.reportOnly) {
        QMessageBox::information(window_, tr("解压内存需求"), message);
        return MemoryAnswer::Continue;
    }
    QMessageBox box(QMessageBox::Question, tr("增加解压内存"), message, QMessageBox::Cancel, window_);
    auto* proceed = box.addButton(tr("允许并继续"), QMessageBox::AcceptRole);
    box.exec();
    return box.clickedButton() == proceed ? MemoryAnswer::Continue : MemoryAnswer::Cancel;
}
OverwriteAnswer OperationController::requestOverwrite(const OverwriteRequest& r) {
    QMessageBox box(QMessageBox::Question, tr("文件已经存在"),
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
        return OverwriteAnswer::Replace;
    if (box.clickedButton() == all)
        return OverwriteAnswer::ReplaceAll;
    if (box.clickedButton() == rename)
        return OverwriteAnswer::RenameIncoming;
    if (box.clickedButton() == skip)
        return OverwriteAnswer::Skip;
    if (box.clickedButton() == skipAll)
        return OverwriteAnswer::SkipAll;
    return OverwriteAnswer::Cancel;
}
bool OperationController::confirmPasswordRetry(const QString& archive, const QStringList& errors) {
    QMessageBox box(QMessageBox::Warning, tr("无法解密归档"),
                    tr("密码可能不正确，或归档数据已损坏。重新输入密码后将继续相同的操作。\n%1").arg(archive),
                    QMessageBox::Retry | QMessageBox::Cancel, window_);
    box.setDetailedText(errors.join('\n'));
    return box.exec() == QMessageBox::Retry;
}
std::optional<QString> OperationController::requestRetryPassword(const QString& archive) {
    bool accepted = false;
    const auto password =
        QInputDialog::getText(window_, tr("重新输入密码"), archive, QLineEdit::Password, {}, &accepted);
    if (!accepted)
        return std::nullopt;
    return password;
}
void OperationController::reportErrors(const QStringList& errors) {
    QMessageBox box(QMessageBox::Warning, tr("操作结果"), tr("%1 个错误").arg(errors.size()), QMessageBox::Ok,
                    window_);
    box.setDetailedText(errors.join('\n'));
    box.exec();
}
} // namespace m7arc::ui
