#include "tools/SfxBuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const QString &message) {
    if (!value) throw std::runtime_error(message.toStdString());
}

void run(const QString &program, const QStringList &arguments, const QString &directory) {
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start(program, arguments);
    require(process.waitForStarted(), process.errorString());
    require(process.waitForFinished(30000), QStringLiteral("Tool timed out."));
    require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            QString::fromUtf8(process.readAllStandardError()) + QString::fromUtf8(process.readAllStandardOutput()));
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    try {
        require(argc == 3, QStringLiteral("Expected CLI and SFX stub paths."));
        QTemporaryDir work;
        require(work.isValid(), work.errorString());
        const QString input = work.filePath(QStringLiteral("文档.txt"));
        const QByteArray contents("Console SFX AES round trip\n");
        QFile file(input);
        require(file.open(QIODevice::WriteOnly), file.errorString());
        require(file.write(contents) == contents.size(), file.errorString());
        file.close();
        const QString archive = work.filePath(QStringLiteral("payload.7z"));
        run(QString::fromLocal8Bit(argv[1]), {QStringLiteral("a"), QStringLiteral("-t7z"),
            QStringLiteral("-m0=LZMA2"), QStringLiteral("-pM7ArcExampleTest"), QStringLiteral("-mhe=on"),
            archive, QStringLiteral("文档.txt")}, work.path());
        const QString output = work.filePath(QStringLiteral("Test Archive.command"));
        QString error = m7arc::tools::createConsoleSfx(QString::fromLocal8Bit(argv[2]), archive, output);
        require(error.isEmpty(), error);
        run(QStringLiteral("/usr/bin/codesign"), {QStringLiteral("--verify"), QStringLiteral("--strict"), output}, work.path());
        const QString destination = work.filePath(QStringLiteral("extracted"));
        run(output, {QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-pM7ArcExampleTest"),
            QStringLiteral("-o") + destination}, work.path());
        QFile extracted(destination + QStringLiteral("/文档.txt"));
        require(extracted.open(QIODevice::ReadOnly), extracted.errorString());
        require(extracted.readAll() == contents, QStringLiteral("Extracted payload differs."));
        error = m7arc::tools::createConsoleSfx(QString::fromLocal8Bit(argv[2]), archive, output);
        require(!error.isEmpty() && QFile::exists(output), QStringLiteral("An existing output must be preserved."));
        const auto cancelledOutput = work.filePath(QStringLiteral("Cancelled.command"));
        error = m7arc::tools::createConsoleSfx(QString::fromLocal8Bit(argv[2]), archive, cancelledOutput, [] { return true; });
        require(!error.isEmpty() && !QFile::exists(cancelledOutput), QStringLiteral("Cancelled output must be removed."));
        std::cout << "Qt console SFX: encrypted payload extraction passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
