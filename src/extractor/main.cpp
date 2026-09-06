#include "app/Translation.h"
#include "engine/ArchiveEngine.h"
#include "ui/OperationController.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace {

m7arc::EngineOptions bundledEngineOptions() {
    m7arc::EngineOptions options;
    options.loadUserModules = false;
    return options;
}

QString payloadPath() {
    const QDir executable(QCoreApplication::applicationDirPath());
    const auto embedded = executable.absoluteFilePath(QStringLiteral("../Resources/Payload.7z"));
    if (QFileInfo::exists(embedded)) return QDir::cleanPath(embedded);
    return QDir::cleanPath(executable.absoluteFilePath(QStringLiteral("../../../Payload.7z")));
}

class ExtractorWindow final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ExtractorWindow)
  public:
    explicit ExtractorWindow(const QString &payload) : payload_(payload), jobs_(this, bundledEngineOptions()) {
        setWindowTitle(tr("M7Arc 图形自解压"));
        resize(590, 280);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 22, 24, 22);
        layout->setSpacing(16);
        auto *title = new QLabel(tr("选择解压位置"), this);
        auto font = title->font();
        font.setPointSize(font.pointSize() + 5);
        font.setBold(true);
        title->setFont(font);
        layout->addWidget(title);
        auto *description = new QLabel(tr("归档：%1\n提取完成后，可以在 Finder 中打开目标文件夹。")
                                       .arg(QFileInfo(payload).fileName()), this);
        description->setWordWrap(true);
        layout->addWidget(description);
        auto *form = new QFormLayout;
        auto *pathRow = new QHBoxLayout;
        const QString bundleName = QFileInfo(QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."))).completeBaseName();
        const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        destination_ = new QLineEdit(QDir(downloads).filePath(bundleName), this);
        auto *browse = new QPushButton(tr("选择…"), this);
        pathRow->addWidget(destination_, 1);
        pathRow->addWidget(browse);
        form->addRow(tr("目标文件夹"), pathRow);
        password_ = new QLineEdit(this);
        password_->setEchoMode(QLineEdit::Password);
        password_->setPlaceholderText(tr("无密码时留空"));
        form->addRow(tr("密码"), password_);
        auto *showPassword = new QCheckBox(tr("显示密码"), this);
        form->addRow(QString(), showPassword);
        layout->addLayout(form);
        auto *buttons = new QHBoxLayout;
        status_ = new QLabel(this);
        status_->setWordWrap(true);
        buttons->addWidget(status_, 1);
        reveal_ = new QPushButton(tr("在 Finder 中打开"), this);
        reveal_->setEnabled(false);
        auto *progress = new QPushButton(tr("显示进度"), this);
        progress->setVisible(false);
        extract_ = new QPushButton(tr("解压"), this);
        extract_->setDefault(true);
        buttons->addWidget(reveal_);
        buttons->addWidget(progress);
        buttons->addWidget(extract_);
        layout->addLayout(buttons);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getExistingDirectory(this, tr("选择目标文件夹"), destination_->text());
            if (!path.isEmpty()) destination_->setText(path);
        });
        connect(showPassword, &QCheckBox::toggled, this, [this](bool show) {
            password_->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        });
        connect(reveal_, &QPushButton::clicked, this, [this] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(completedDestination_));
        });
        connect(progress, &QPushButton::clicked, &jobs_, &m7arc::ui::OperationController::showProgress);
        connect(&jobs_, &m7arc::ui::OperationController::busyChanged, this, [this, browse, progress](bool busy) {
            extract_->setEnabled(!busy);
            destination_->setEnabled(!busy);
            password_->setEnabled(!busy);
            browse->setEnabled(!busy);
            progress->setVisible(busy);
        });
        connect(extract_, &QPushButton::clicked, this, [this] { extract(); });
        if (!QFileInfo::exists(payload_)) {
            status_->setText(tr("未找到 Payload.7z，请保持外部载荷与应用位于同一目录。"));
            extract_->setEnabled(false);
        }
    }

  protected:
    void closeEvent(QCloseEvent *event) override {
        if (jobs_.busy()) {
            jobs_.cancel();
            status_->setText(tr("正在取消，请等待解压结束后关闭。"));
            event->ignore();
        } else event->accept();
    }

  private:
    void extract() {
        const QString destination = QDir::cleanPath(destination_->text().trimmed());
        if (!QDir::isAbsolutePath(destination)) {
            QMessageBox::information(this, tr("目标文件夹"), tr("请选择完整的目标文件夹路径。"));
            return;
        }
        m7arc::ExtractOptions options;
        options.password = password_->text().toStdString();
        const auto archive = payload_.toStdString();
        reveal_->setEnabled(false);
        status_->clear();
        jobs_.run(tr("正在解压"), [archive, destination, options](m7arc::ArchiveEngine &engine, const m7arc::Callbacks &callbacks) {
            return engine.extract(archive, destination.toStdString(), {}, options, callbacks);
        }, [this, destination](const m7arc::OperationResult &result) {
            status_->setText(result.cancelled ? tr("已取消") : result.ok() ? tr("解压完成") : tr("解压包含错误"));
            reveal_->setEnabled(result.ok());
            if (result.ok()) completedDestination_ = destination;
        }, true);
    }
    QString payload_, completedDestination_;
    m7arc::ui::OperationController jobs_;
    QLineEdit *destination_, *password_;
    QLabel *status_;
    QPushButton *extract_, *reveal_;
};

int extractFromCommandLine(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    const auto destinationIndex = arguments.indexOf(QStringLiteral("--extract-to"));
    if (destinationIndex + 1 >= arguments.size()) return 2;
    const auto destination = arguments[destinationIndex + 1];
    m7arc::ExtractOptions options;
    options.overwrite = m7arc::Overwrite::Skip;
    const auto passwordIndex = arguments.indexOf(QStringLiteral("--password-file"));
    if (passwordIndex >= 0) {
        if (passwordIndex + 1 >= arguments.size()) return 2;
        QFile password(arguments[passwordIndex + 1]);
        if (!password.open(QIODevice::ReadOnly)) return 2;
        QByteArray bytes = password.readAll();
        if (bytes.endsWith('\n')) bytes.chop(1);
        if (bytes.endsWith('\r')) bytes.chop(1);
        options.password = bytes.toStdString();
    }
    try {
        m7arc::ArchiveEngine engine(bundledEngineOptions());
        const auto result = engine.extract(payloadPath().toStdString(), destination.toStdString(), {}, options);
        for (const auto &error : result.errors) QTextStream(stderr) << QString::fromStdString(error) << '\n';
        return result.ok() ? 0 : 1;
    } catch (const std::exception &error) {
        QTextStream(stderr) << error.what() << '\n';
        return 1;
    }
}

}

int main(int argc, char **argv) {
    for (int index = 1; index < argc; ++index)
        if (QByteArray(argv[index]) == "--extract-to") return extractFromCommandLine(argc, argv);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("M7Arc Extractor"));
    app.setOrganizationName(QStringLiteral("M7Arc"));
    app.setApplicationVersion(QStringLiteral(M7ARC_VERSION));
    m7arc::loadTranslations(app);
    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        m7arc::ArchiveEngine engine(bundledEngineOptions());
        return engine.formats().empty() ? 1 : 0;
    }
    ExtractorWindow window(payloadPath());
    window.show();
    return app.exec();
}
