#include "ui/OperationController.h"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace m7arc;

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

static void exerciseRetry(const QString& archive, const QString& destination, bool acceptRetry) {
    QWidget window;
    ui::OperationController operations(&window);
    QEventLoop loop;
    int attempts = 0, completed = 0, retryPrompts = 0, passwordPrompts = 0;
    OperationResult finalResult;
    QTimer answer;
    QObject::connect(&answer, &QTimer::timeout, &window, [&] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible())
                continue;
            if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                if (box->standardButtons().testFlag(QMessageBox::Retry)) {
                    ++retryPrompts;
                    box->button(acceptRetry ? QMessageBox::Retry : QMessageBox::Cancel)->click();
                } else if (auto* button = box->button(QMessageBox::Ok))
                    button->click();
            } else if (auto* input = qobject_cast<QInputDialog*>(widget)) {
                ++passwordPrompts;
                input->setTextValue("correct password");
                input->accept();
            }
        }
    });
    answer.start(15);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        operations.cancel();
        std::cerr << "Password recovery did not complete within 15 seconds\n";
        std::_Exit(1);
    });
    timeout.start(15000);
    ExtractOptions options;
    options.password = "incorrect password";
    options.overwrite = Overwrite::Replace;
    operations.run(
        "Selected extraction",
        [&, options](ArchiveEngine& engine, const Callbacks& callbacks) {
            ++attempts;
            return engine.extract(archive.toStdString(), destination.toStdString(), {0}, options, callbacks);
        },
        [&](const OperationResult& result) {
            ++completed;
            finalResult = result;
            loop.quit();
        },
        true);
    loop.exec();
    require(!operations.busy(), "Controller remained busy after completion");
    require(completed == 1, "Completion callback ran more than once");
    require(retryPrompts == 1, "Password error did not produce one retry prompt");
    require(attempts == (acceptRetry ? 2 : 1), "Retry did not preserve the requested operation");
    require(passwordPrompts == (acceptRetry ? 1 : 0), "Unexpected password input prompt");
    if (acceptRetry) {
        require(finalResult.ok(), "Corrected password did not recover extraction");
        QFile file(destination + "/selected.txt");
        require(file.open(QIODevice::ReadOnly) && file.readAll() == "selected payload\n",
                "Recovered extraction produced incorrect content");
        require(!QFile::exists(destination + "/unselected.txt"), "Retry lost the selected-member filter");
    } else
        require(finalResult.cancelled && finalResult.errors.empty(),
                "Cancelling retry did not cancel cleanly");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir temp;
        require(temp.isValid(), "Temporary directory unavailable");
        for (const auto& name : {"selected.txt", "unselected.txt"}) {
            QFile file(temp.filePath(name));
            require(file.open(QIODevice::WriteOnly), "Cannot write input");
            file.write(name == QString("selected.txt") ? "selected payload\n" : "unselected payload\n");
        }
        ArchiveEngine engine;
        CreateOptions create;
        create.password = "correct password";
        create.preserveMacMetadata = false;
        for (const bool encryptHeaders : {true, false}) {
            create.encryptHeaders = encryptHeaders;
            const auto suffix = encryptHeaders ? "headers" : "data";
            const auto archive = temp.filePath(QString("encrypted-%1.7z").arg(suffix));
            require(engine
                        .create({temp.filePath("selected.txt").toStdString(),
                                 temp.filePath("unselected.txt").toStdString()},
                                archive.toStdString(), create)
                        .ok(),
                    "Cannot create encrypted fixture");
            exerciseRetry(archive, temp.filePath(QString("recovered-%1").arg(suffix)), true);
            exerciseRetry(archive, temp.filePath(QString("cancelled-%1").arg(suffix)), false);
        }
        std::cout << "Encrypted selected extraction recovered and cancelled through Qt dialogs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
