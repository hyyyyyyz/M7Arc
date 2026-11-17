#include "SfxDialog.h"
#include "OperationController.h"
#include "app/Settings.h"
#include "tools/SfxBuilder.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <memory>

namespace m7arc::ui {
void showSfxCreation(QWidget* parent, const QStringList& inputs, const QString& directory,
                     OperationController* jobs, std::function<void()> completed) {
    if (inputs.isEmpty() || jobs->busy())
        return;
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("创建自解压程序"));
    dialog.resize(670, 490);
    auto* layout = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget(&dialog);
    auto* generalPage = new QWidget(tabs);
    auto* form = new QFormLayout(generalPage);
    tabs->addTab(generalPage, QObject::tr("归档与输出"));
    auto* target = new QComboBox(&dialog);
    target->addItem(QObject::tr("macOS 图形提取器 (.app，内嵌归档)"), "app");
    target->addItem(QObject::tr("macOS 图形提取器 DMG（内嵌归档）"), "dmg-embedded");
    target->addItem(QObject::tr("macOS 图形提取器 DMG（独立 Payload.7z）"), "dmg-external");
    target->addItem(QObject::tr("macOS 终端自解压程序 (.command)"), "console");
    target->addItem(QObject::tr("Windows 图形自解压程序 (.exe, x86)"), "windows-gui");
    target->addItem(QObject::tr("Windows 控制台自解压程序 (.exe, x86)"), "windows-console");
    target->addItem(QObject::tr("Windows 安装器 (.exe, x86)"), "windows-installer");
    form->addRow(QObject::tr("目标类型"), target);
    auto* path = new QLineEdit(QDir(directory).filePath("Archive.app"), &dialog);
    auto* pathRow = new QWidget(&dialog);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    auto* browse = new QPushButton(QObject::tr("选择…"), pathRow);
    pathLayout->addWidget(path, 1);
    pathLayout->addWidget(browse);
    form->addRow(QObject::tr("输出位置"), pathRow);
    auto* name = new QLineEdit("Archive", &dialog);
    form->addRow(QObject::tr("提取器名称"), name);
    auto* method = new QComboBox(&dialog);
    method->addItems({"LZMA2", "LZMA", "PPMd", "Copy"});
    form->addRow(QObject::tr("7z 方法"), method);
    auto* level = new QComboBox(&dialog);
    for (const auto value : {0, 1, 3, 5, 7, 9})
        level->addItem(QString::number(value), value);
    level->setCurrentIndex(3);
    form->addRow(QObject::tr("压缩级别"), level);
    auto* password = new QLineEdit(&dialog);
    auto* confirmation = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    confirmation->setEchoMode(QLineEdit::Password);
    form->addRow(QObject::tr("密码（可选）"), password);
    form->addRow(QObject::tr("确认密码"), confirmation);
    auto* showPassword = new QCheckBox(QObject::tr("显示密码"), &dialog);
    auto* encryptNames = new QCheckBox(QObject::tr("加密文件名"), &dialog);
    auto* metadata = new QCheckBox(QObject::tr("保留 macOS 元数据"), &dialog);
    metadata->setChecked(app::Settings().preserveMacMetadata());
    form->addRow(showPassword);
    form->addRow(encryptNames);
    form->addRow(metadata);
    auto* installerPage = new QWidget(tabs);
    auto* installerForm = new QFormLayout(installerPage);
    auto* installerTitle = new QLineEdit("Setup", installerPage);
    auto* installerPrompt = new QLineEdit(installerPage);
    auto* installerProgress = new QCheckBox(QObject::tr("显示提取进度"), installerPage);
    installerProgress->setChecked(true);
    auto* launchMode = new QComboBox(installerPage);
    launchMode->addItem(QObject::tr("RunProgram：运行归档中的程序"));
    launchMode->addItem(QObject::tr("ExecuteFile：打开文件或 Windows 命令"));
    auto* runProgram = new QLineEdit("setup.exe", installerPage);
    auto* directoryPrefix = new QLineEdit(".\\", installerPage);
    auto* executeFile = new QLineEdit(installerPage);
    auto* executeParameters = new QLineEdit(installerPage);
    installerForm->addRow(QObject::tr("安装器标题"), installerTitle);
    installerForm->addRow(QObject::tr("开始前询问"), installerPrompt);
    installerForm->addRow(installerProgress);
    installerForm->addRow(QObject::tr("启动方式"), launchMode);
    installerForm->addRow(QObject::tr("程序命令行"), runProgram);
    installerForm->addRow(QObject::tr("程序路径前缀"), directoryPrefix);
    installerForm->addRow(QObject::tr("打开文件或程序"), executeFile);
    installerForm->addRow(QObject::tr("打开参数"), executeParameters);
    auto* installerHelp = new QLabel(
        QObject::tr(
            "在 Windows 上运行时，先提取到临时目录，再执行配置的程序并等待退出，随后清理临时文件。"
            "路径前缀不是解压目的地；RunProgram 中 %%T 代表临时目录。M7Arc 只生成安装器，不执行这些命令。"),
        installerPage);
    installerHelp->setWordWrap(true);
    installerForm->addRow(installerHelp);
    tabs->addTab(installerPage, QObject::tr("Windows 安装器配置"));
    layout->addWidget(tabs);
    const auto updateLaunch = [=] {
        const bool run = launchMode->currentIndex() == 0;
        runProgram->setEnabled(run);
        directoryPrefix->setEnabled(run);
        executeFile->setEnabled(!run);
        executeParameters->setEnabled(!run);
    };
    QObject::connect(launchMode, &QComboBox::currentIndexChanged, &dialog, updateLaunch);
    updateLaunch();
    auto* note = new QLabel(&dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("创建"));
    layout->addWidget(buttons);
    auto suffix = [target] {
        const auto kind = target->currentData().toString();
        return kind == "app"            ? ".app"
               : kind.startsWith("dmg") ? ".dmg"
               : kind == "console"      ? ".command"
                                        : ".exe";
    };
    auto updateTarget = [=] {
        const auto kind = target->currentData().toString();
        const bool graphical = kind == "app" || kind.startsWith("dmg");
        const bool installer = kind == "windows-installer";
        const QFileInfo previous(path->text());
        path->setText(previous.dir().filePath(previous.completeBaseName() + suffix()));
        name->setEnabled(graphical);
        metadata->setEnabled(graphical);
        tabs->setTabEnabled(1, installer);
        for (auto* field : {password, confirmation}) {
            field->setEnabled(!installer);
            if (installer)
                field->clear();
        }
        showPassword->setEnabled(!installer);
        encryptNames->setEnabled(!installer);
        if (installer)
            encryptNames->setChecked(false);
        if (installer && method->findText("PPMd") >= 0)
            method->removeItem(method->findText("PPMd"));
        if (!installer && method->findText("PPMd") < 0)
            method->insertItem(2, "PPMd");
        note->setText(
            graphical   ? QObject::tr("图形提取器包含密码、目标目录与进度窗口，并可恢复 macOS 元数据。"
                                      "Mac 产物对应当前应用架构，采用 ad-hoc 签名。")
            : installer ? QObject::tr("官方 7zSD 安装器支持 LZMA、LZMA2 和 Copy，不支持加密。"
                                      "请在 Windows 安装器配置页设置提取后执行的程序。")
            : kind == "console" ? QObject::tr("在同架构 Mac 的 Terminal 中运行，使用官方 console 提取器。"
                                              "不会自动运行归档成员。")
                                : QObject::tr("输出使用官方 Windows x86 模块，需在 Windows 上运行。"
                                              "不包含安装或解压后执行命令。"));
    };
    QObject::connect(target, &QComboBox::currentIndexChanged, &dialog, updateTarget);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [=, &dialog] {
        const auto output = QFileDialog::getSaveFileName(&dialog, QObject::tr("自解压输出"), path->text(),
                                                         QString("*%1").arg(suffix()));
        if (!output.isEmpty())
            path->setText(output);
    });
    QObject::connect(showPassword, &QCheckBox::toggled, &dialog, [=](bool show) {
        password->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        confirmation->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [=, &dialog] {
        if (!path->text().isEmpty() && !path->text().endsWith(suffix(), Qt::CaseInsensitive))
            path->setText(path->text() + suffix());
        if (path->text().isEmpty() || QFileInfo::exists(path->text())) {
            QMessageBox::warning(&dialog, QObject::tr("输出位置无效"),
                                 QObject::tr("请选择一个新的输出文件名。"));
            return;
        }
        if (password->text() != confirmation->text()) {
            QMessageBox::warning(&dialog, QObject::tr("密码不一致"), QObject::tr("两次输入的密码必须一致。"));
            return;
        }
        if (encryptNames->isChecked() && password->text().isEmpty()) {
            QMessageBox::warning(&dialog, QObject::tr("需要密码"), QObject::tr("加密文件名需要设置密码。"));
            return;
        }
        if (target->currentData().toString() == "windows-installer" &&
            (launchMode->currentIndex() == 0 ? runProgram->text().trimmed().isEmpty()
                                             : executeFile->text().trimmed().isEmpty())) {
            tabs->setCurrentIndex(1);
            QMessageBox::warning(&dialog, QObject::tr("需要启动配置"),
                                 QObject::tr("请输入 Windows 上要运行的程序或打开的文件。"));
            return;
        }
        dialog.accept();
    });
    updateTarget();
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto kind = target->currentData().toString();
    const auto output = QFileInfo(path->text()).absoluteFilePath();
    const auto displayName = name->text().trimmed().isEmpty() ? "Archive" : name->text().trimmed();
    const auto applicationDirectory = QCoreApplication::applicationDirPath();
    const auto sourceApp = QDir(applicationDirectory).absoluteFilePath("../..");
    CreateOptions options;
    options.format = "7z";
    options.method = method->currentText().toStdString();
    options.level = level->currentData().toUInt();
    options.threads = qMax(1, QThread::idealThreadCount());
    options.password = password->text().toStdString();
    options.encryptHeaders = encryptNames->isChecked();
    options.preserveMacMetadata = metadata->isEnabled() && metadata->isChecked();
    tools::WindowsInstallerOptions installer;
    installer.title = installerTitle->text();
    installer.beginPrompt = installerPrompt->text();
    installer.showProgress = installerProgress->isChecked();
    installer.launch = launchMode->currentIndex() == 0 ? tools::WindowsInstallerLaunch::RunProgram
                                                       : tools::WindowsInstallerLaunch::ExecuteFile;
    installer.runProgram = runProgram->text();
    installer.directoryPrefix = directoryPrefix->text();
    installer.executeFile = executeFile->text();
    installer.executeParameters = executeParameters->text();
    auto temporary = std::make_shared<QTemporaryDir>(
        app::Settings().tempDirectory() + "/M7Arc-SFX-XXXXXX");
    jobs->run(
        QObject::tr("创建自解压程序"),
        [inputs, kind, output, displayName, applicationDirectory, sourceApp, options, installer,
         temporary](ArchiveEngine& engine, const Callbacks& callbacks) {
            if (!temporary->isValid())
                throw EngineError("Unable to create SFX working directory");
            std::vector<std::string> paths;
            for (const auto& input : inputs)
                paths.push_back(input.toStdString());
            const auto payload = QDir(temporary->path()).filePath("Payload.7z");
            auto result = engine.create(paths, payload.toStdString(), options, callbacks);
            if (!result.ok())
                return result;
            OpenOptions open;
            open.password = options.password;
            result = engine.test(payload.toStdString(), {}, open, callbacks);
            if (!result.ok())
                return result;
            QString error;
            if (kind == "console") {
                error = tools::createConsoleSfx(QDir(applicationDirectory).filePath("7zCon.sfx"), payload,
                                                output, callbacks.cancelled);
            } else if (kind == "windows-installer") {
                const auto stub = QDir(applicationDirectory).filePath("../Resources/Windows SFX/7zSD.sfx");
                error =
                    tools::createWindowsInstallerSfx(stub, payload, output, installer, callbacks.cancelled);
            } else if (kind.startsWith("windows")) {
                const auto module = kind == "windows-gui" ? "7z.sfx" : "7zCon.sfx";
                const auto stub =
                    QDir(applicationDirectory).filePath("../Resources/Windows SFX/" + QString(module));
                error = tools::createWindowsSfx(stub, payload, output, callbacks.cancelled);
            } else {
                const auto app = kind == "app" ? output : QDir(temporary->path()).filePath("Extractor.app");
                tools::SfxBundleOptions bundle;
                bundle.displayName = displayName;
                bundle.embedPayload = kind != "dmg-external";
                bundle.cancelled = callbacks.cancelled;
                error = tools::createGraphicalSfx(sourceApp, payload, app, bundle);
                if (error.isEmpty() && kind.startsWith("dmg"))
                    error = tools::createSfxDmg(app, bundle.embedPayload ? QString{} : payload, output,
                                                callbacks.cancelled);
            }
            result.outputPaths.clear();
            if (callbacks.cancelled && callbacks.cancelled())
                result.cancelled = true;
            else if (!error.isEmpty())
                result.errors.push_back(error.toStdString());
            else
                result.outputPaths.push_back(output.toStdString());
            return result;
        },
        [parent, output, completed = std::move(completed)](const OperationResult& result) {
            if (completed)
                completed();
            if (result.ok())
                QMessageBox::information(parent, QObject::tr("自解压程序已创建"), output);
        });
}
} // namespace m7arc::ui
