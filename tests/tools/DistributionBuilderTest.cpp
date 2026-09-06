#include "tools/SfxBuilder.h"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include "Common/TextConfig.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    QString error;
    if (args.size() == 5 && args[1] == QStringLiteral("windows"))
        error = m7arc::tools::createWindowsSfx(args[2], args[3], args[4]);
    else if (args.size() == 5 && args[1].startsWith(QStringLiteral("installer-"))) {
        m7arc::tools::WindowsInstallerOptions options;
        options.title = QStringLiteral("中文 \"Setup\"");
        options.beginPrompt = QStringLiteral("Install M7Arc?\nSecond line");
        options.directoryPrefix.clear();
        options.runProgram = QStringLiteral("\"%%T\\setup.exe\" /quiet");
        if (args[1] == QStringLiteral("installer-open")) {
            options.launch = m7arc::tools::WindowsInstallerLaunch::ExecuteFile;
            options.executeFile = QStringLiteral("readme.txt");
            options.executeParameters = QStringLiteral("/example \"quoted value\"");
            options.showProgress = false;
        }
        error = m7arc::tools::createWindowsInstallerSfx(args[2], args[3], args[4], options);
        if (error.isEmpty()) {
            QFile file(args[4]);
            if (!file.open(QIODevice::ReadOnly)) return 1;
            const auto bytes = file.readAll();
            const QByteArray start(";!@Install@!UTF-8!\n"), end(";!@InstallEnd@!");
            const auto first = bytes.indexOf(start);
            const auto last = bytes.indexOf(end, first + start.size());
            if (first < 0 || last < 0) return 1;
            const auto config = bytes.mid(first + start.size(), last - first - start.size());
            CObjectVector<CTextConfigPair> pairs;
            if (!GetTextConfig(AString(config.constData()), pairs)) return 1;
            const auto value = [&pairs](const char *key) {
                const auto text = GetTextConfigValue(pairs, key);
                return QString::fromWCharArray(text.Ptr(), text.Len());
            };
            if (value("Title") != options.title || value("BeginPrompt") != options.beginPrompt) return 1;
            if (options.launch == m7arc::tools::WindowsInstallerLaunch::RunProgram) {
                if (value("RunProgram") != options.runProgram || value("Directory") != options.directoryPrefix) return 1;
            } else if (value("ExecuteFile") != options.executeFile ||
                       value("ExecuteParameters") != options.executeParameters || value("Progress") != "no") return 1;
        }
    }
    else if (args.size() == 6 && args[1] == QStringLiteral("app")) {
        m7arc::tools::SfxBundleOptions options;
        options.embedPayload = args[5] == QStringLiteral("embedded");
        if (args[5] == QStringLiteral("cancel")) options.cancelled = [] { return true; };
        error = m7arc::tools::createGraphicalSfx(args[2], args[3], args[4], options);
    } else if (args.size() == 5 && args[1] == QStringLiteral("dmg"))
        error = m7arc::tools::createSfxDmg(args[2], args[3], args[4]);
    else return 2;
    if (!error.isEmpty()) QTextStream(stderr) << error << '\n';
    return error.isEmpty() ? 0 : 1;
}
