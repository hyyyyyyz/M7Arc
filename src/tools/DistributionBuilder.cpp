#include "DistributionBuilder.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtEndian>

namespace m7arc::tools {
namespace {

QString run(const QString &program, const QStringList &arguments,
            const std::function<bool()> &cancelled = {}) {
    if (cancelled && cancelled()) return QStringLiteral("Self-extracting package creation was cancelled.");
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted()) return process.errorString();
    while (process.state() != QProcess::NotRunning) {
        if (process.waitForFinished(100)) break;
        if (cancelled && cancelled()) {
            process.kill();
            process.waitForFinished();
            return QStringLiteral("Self-extracting package creation was cancelled.");
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString details = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return details.isEmpty() ? QStringLiteral("%1 failed (%2).").arg(program).arg(process.exitCode()) : details;
    }
    return {};
}

QString copy(const QString &source, const QString &destination, const std::function<bool()> &cancelled) {
    return run(QStringLiteral("/usr/bin/ditto"), {source, destination}, cancelled);
}

QString validatePayload(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return file.errorString();
    if (file.read(6) != QByteArray::fromHex("377abcaf271c"))
        return QStringLiteral("Self-extracting packages require a 7z payload.");
    return {};
}

QString bundleValue(const QString &path, const QString &key) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"key" && xml.readElementText() == key) {
            if (xml.readNextStartElement()) return xml.readElementText();
        }
    }
    return {};
}

QString writePlist(const QString &source, const QString &destination, const QString &name) {
    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeDTD(QStringLiteral("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"));
    xml.writeStartElement(QStringLiteral("plist"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeStartElement(QStringLiteral("dict"));
    const auto value = [&](const QString &key, const QString &text) {
        xml.writeTextElement(QStringLiteral("key"), key);
        xml.writeTextElement(QStringLiteral("string"), text);
    };
    value(QStringLiteral("CFBundleExecutable"), QStringLiteral("M7ArcExtractor"));
    value(QStringLiteral("CFBundleIdentifier"), QStringLiteral("org.m7arc.extractor"));
    value(QStringLiteral("CFBundleName"), name);
    value(QStringLiteral("CFBundleDisplayName"), name);
    value(QStringLiteral("CFBundlePackageType"), QStringLiteral("APPL"));
    value(QStringLiteral("CFBundleInfoDictionaryVersion"), QStringLiteral("6.0"));
    const auto icon = bundleValue(source, QStringLiteral("CFBundleIconFile"));
    if (!icon.isEmpty()) value(QStringLiteral("CFBundleIconFile"), icon);
    for (const auto &key : {QStringLiteral("CFBundleVersion"), QStringLiteral("CFBundleShortVersionString"),
                            QStringLiteral("M7ArcVersion"), QStringLiteral("LSMinimumSystemVersion")})
        value(key, bundleValue(source, key));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    return file.error() == QFileDevice::NoError ? QString() : file.errorString();
}

}

QString createGraphicalSfx(const QString &sourceApp, const QString &archive, const QString &outputApp,
                           const SfxBundleOptions &options) {
    if (QFileInfo::exists(outputApp)) return QStringLiteral("The output application already exists.");
    QString error = validatePayload(archive);
    if (!error.isEmpty()) return error;
    const QDir source(sourceApp + QStringLiteral("/Contents"));
    if (!QFileInfo::exists(source.filePath(QStringLiteral("MacOS/M7ArcExtractor"))) ||
        !QFileInfo::exists(source.filePath(QStringLiteral("Frameworks/QtWidgets.framework"))) ||
        !QFileInfo::exists(source.filePath(QStringLiteral("PlugIns/platforms/libqcocoa.dylib"))) ||
        !QFileInfo::exists(source.filePath(QStringLiteral("Resources/Licenses/M7Arc.txt"))))
        return QStringLiteral("Create a graphical self-extracting package from an installed M7Arc release with bundled Qt runtime.");
    QDir output(outputApp + QStringLiteral("/Contents"));
    if (!output.mkpath(QStringLiteral("MacOS")) || !output.mkpath(QStringLiteral("Resources"))) {
        QDir(outputApp).removeRecursively();
        return QStringLiteral("Cannot create the extractor application directory.");
    }
    const auto fail = [&](const QString &message) {
        QDir(outputApp).removeRecursively();
        return message;
    };
    error = copy(source.filePath(QStringLiteral("MacOS/M7ArcExtractor")),
                 output.filePath(QStringLiteral("MacOS/M7ArcExtractor")), options.cancelled);
    if (!error.isEmpty()) return fail(error);
    error = copy(source.filePath(QStringLiteral("Frameworks")), output.filePath(QStringLiteral("Frameworks")), options.cancelled);
    if (!error.isEmpty()) return fail(error);
    const QDir plugins(source.filePath(QStringLiteral("PlugIns")));
    for (const auto &entry : plugins.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files)) {
        if (entry.suffix() == QStringLiteral("appex")) continue;
        error = copy(entry.absoluteFilePath(), output.filePath(QStringLiteral("PlugIns/") + entry.fileName()), options.cancelled);
        if (!error.isEmpty()) return fail(error);
    }
    for (const auto &name : {QStringLiteral("Licenses"), QStringLiteral("qt.conf"), QStringLiteral("M7Arc.icns")}) {
        const auto path = source.filePath(QStringLiteral("Resources/") + name);
        if (QFileInfo::exists(path)) {
            error = copy(path, output.filePath(QStringLiteral("Resources/") + name), options.cancelled);
            if (!error.isEmpty()) return fail(error);
        }
    }
    if (options.embedPayload) {
        error = copy(archive, output.filePath(QStringLiteral("Resources/Payload.7z")), options.cancelled);
        if (!error.isEmpty()) return fail(error);
    }
    const QString name = options.displayName.isEmpty() ? QFileInfo(outputApp).completeBaseName() : options.displayName;
    error = writePlist(source.filePath(QStringLiteral("Info.plist")), output.filePath(QStringLiteral("Info.plist")), name);
    if (!error.isEmpty()) return fail(error);
    error = run(QStringLiteral("/usr/bin/codesign"), {QStringLiteral("--force"), QStringLiteral("--sign"),
                QStringLiteral("-"), QStringLiteral("--timestamp=none"), outputApp}, options.cancelled);
    if (!error.isEmpty()) return fail(error);
    error = run(QStringLiteral("/usr/bin/codesign"), {QStringLiteral("--verify"), QStringLiteral("--deep"),
                QStringLiteral("--strict"), outputApp}, options.cancelled);
    if (!error.isEmpty()) return fail(error);
    return {};
}

QString createSfxDmg(const QString &extractorApp, const QString &externalPayload, const QString &outputDmg,
                    const std::function<bool()> &cancelled) {
    if (QFileInfo::exists(outputDmg)) return QStringLiteral("The output disk image already exists.");
    if (!QFileInfo::exists(extractorApp + QStringLiteral("/Contents/MacOS/M7ArcExtractor")))
        return QStringLiteral("Select a graphical M7Arc self-extracting application.");
    if (!externalPayload.isEmpty()) {
        const auto error = validatePayload(externalPayload);
        if (!error.isEmpty()) return error;
    } else {
        const auto error = validatePayload(extractorApp + QStringLiteral("/Contents/Resources/Payload.7z"));
        if (!error.isEmpty()) return error;
    }
    QTemporaryDir temporary;
    if (!temporary.isValid()) return temporary.errorString();
    QString error = copy(extractorApp, temporary.filePath(QFileInfo(extractorApp).fileName()), cancelled);
    if (!error.isEmpty()) return error;
    if (!externalPayload.isEmpty()) {
        error = copy(externalPayload, temporary.filePath(QStringLiteral("Payload.7z")), cancelled);
        if (!error.isEmpty()) return error;
    }
    QFile instructions(temporary.filePath(QStringLiteral("Read Me.txt")));
    if (!instructions.open(QIODevice::WriteOnly)) return instructions.errorString();
    instructions.write("Open the application, choose a destination folder, then click Extract.\n"
                       "If requested, enter the archive password. You can cancel extraction.\n"
                       "The extractor does not run files from the archive.\n\n"
                       "Signing: ad-hoc. Developer ID: no. Apple notarization: no.\n"
                       "If macOS blocks this application, use System Settings > Privacy & Security > Open Anyway.\n");
    instructions.close();
    error = run(QStringLiteral("/usr/bin/hdiutil"), {QStringLiteral("create"), QStringLiteral("-volname"),
                QFileInfo(extractorApp).completeBaseName(), QStringLiteral("-srcfolder"), temporary.path(),
                QStringLiteral("-format"), QStringLiteral("UDZO"), QStringLiteral("-fs"), QStringLiteral("HFS+"), outputDmg}, cancelled);
    if (!error.isEmpty()) QFile::remove(outputDmg);
    return error;
}

static QString joinWindowsSfx(const QString &stub, const QString &archive, const QString &outputExe,
                              const QByteArray &configuration, const std::function<bool()> &cancelled) {
    if (QFileInfo::exists(outputExe)) return QStringLiteral("The output executable already exists.");
    QString error = validatePayload(archive);
    if (!error.isEmpty()) return error;
    QFile module(stub);
    if (!module.open(QIODevice::ReadOnly)) return module.errorString();
    const QByteArray header = module.read(64);
    if (header.size() != 64 || !header.startsWith("MZ"))
        return QStringLiteral("Windows SFX requires a Windows PE executable stub.");
    const quint32 pe = qFromLittleEndian<quint32>(header.constData() + 60);
    if (!module.seek(pe) || module.read(4) != QByteArray("PE\0\0", 4))
        return QStringLiteral("The selected stub has no Windows PE header.");
    QFile output(outputExe);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return output.errorString();
    const auto fail = [&](const QString &message) {
        output.close();
        QFile::remove(outputExe);
        return message;
    };
    for (const auto &path : {stub, archive}) {
        if (path == archive && !configuration.isEmpty())
            if (output.write(configuration) != configuration.size()) return fail(output.errorString());
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) return fail(input.errorString());
        QByteArray block(1024 * 1024, Qt::Uninitialized);
        while (!input.atEnd()) {
            if (cancelled && cancelled()) return fail(QStringLiteral("Self-extracting package creation was cancelled."));
            const qint64 count = input.read(block.data(), block.size());
            if (count < 0) return fail(input.errorString());
            if (output.write(block.constData(), count) != count) return fail(output.errorString());
        }
    }
    if (!output.flush()) return fail(output.errorString());
    return {};
}

QString createWindowsSfx(const QString &stub, const QString &archive, const QString &outputExe,
                        const std::function<bool()> &cancelled) {
    return joinWindowsSfx(stub, archive, outputExe, {}, cancelled);
}

QString createWindowsInstallerSfx(const QString &stub, const QString &archive, const QString &outputExe,
                                 const WindowsInstallerOptions &options, const std::function<bool()> &cancelled) {
    if (options.launch == WindowsInstallerLaunch::RunProgram && options.runProgram.trimmed().isEmpty())
        return QStringLiteral("Enter the Windows program command line.");
    if (options.launch == WindowsInstallerLaunch::ExecuteFile && options.executeFile.trimmed().isEmpty())
        return QStringLiteral("Enter the Windows file or program to open.");
    QList<QPair<QString, QString>> values = {
        {QStringLiteral("Title"), options.title}, {QStringLiteral("BeginPrompt"), options.beginPrompt},
        {QStringLiteral("Progress"), options.showProgress ? QStringLiteral("yes") : QStringLiteral("no")}};
    if (options.launch == WindowsInstallerLaunch::RunProgram) {
        values.append({QStringLiteral("RunProgram"), options.runProgram});
        values.append({QStringLiteral("Directory"), options.directoryPrefix});
    } else {
        values.append({QStringLiteral("ExecuteFile"), options.executeFile});
        values.append({QStringLiteral("ExecuteParameters"), options.executeParameters});
    }
    QByteArray configuration(";!@Install@!UTF-8!\n");
    for (const auto &entry : values) {
        QString value = entry.second;
        if (value.contains(QChar::Null) || value.contains(QStringLiteral(";!@InstallEnd@!")))
            return QStringLiteral("Installer fields cannot contain NUL or the configuration end marker.");
        value.replace('\\', QStringLiteral("\\\\"));
        value.replace('"', QStringLiteral("\\\""));
        value.replace('\n', QStringLiteral("\\n"));
        value.replace('\t', QStringLiteral("\\t"));
        configuration += entry.first.toUtf8() + "=\"" + value.toUtf8() + "\"\n";
    }
    configuration += ";!@InstallEnd@!\n";
    return joinWindowsSfx(stub, archive, outputExe, configuration, cancelled);
}

}
