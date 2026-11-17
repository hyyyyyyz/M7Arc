#include "SfxBuilder.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QtEndian>

namespace m7arc::tools {
namespace {

QString sign(const QStringList &arguments, const std::function<bool()> &cancelled) {
    if (cancelled && cancelled()) return QStringLiteral("Self-extracting package creation was cancelled.");
    QProcess process;
    process.start(QStringLiteral("/usr/bin/codesign"), arguments);
    if (!process.waitForStarted()) return process.errorString();
    while (process.state() != QProcess::NotRunning) {
        if (process.waitForFinished(100)) break;
        if (cancelled && cancelled()) {
            process.kill();
            process.waitForFinished();
            return QStringLiteral("Self-extracting package creation was cancelled.");
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return QString::fromUtf8(process.readAllStandardError()).trimmed();
    return {};
}

QString extendLinkedit(QFile &file) {
    if (!file.seek(0)) return file.errorString();
    const QByteArray header = file.read(32);
    if (header.size() != 32 || qFromLittleEndian<quint32>(header.constData()) != 0xfeedfacf)
        return QStringLiteral("Console SFX requires a single-architecture 64-bit Mach-O stub.");
    const auto commands = qFromLittleEndian<quint32>(header.constData() + 16);
    qint64 offset = 32;
    for (quint32 index = 0; index < commands; ++index) {
        if (!file.seek(offset)) return file.errorString();
        const QByteArray command = file.read(72);
        if (command.size() != 72) return QStringLiteral("The extractor has an incomplete Mach-O load command.");
        const auto kind = qFromLittleEndian<quint32>(command.constData());
        const auto length = qFromLittleEndian<quint32>(command.constData() + 4);
        if (kind == 0x19 && command.mid(8, 16).split('\0').first() == "__LINKEDIT") {
            const auto fileOffset = qFromLittleEndian<quint64>(command.constData() + 40);
            const auto size = quint64(file.size()) - fileOffset;
            char bytes[8];
            qToLittleEndian<quint64>((size + 0x3fff) & ~quint64(0x3fff), bytes);
            if (!file.seek(offset + 32) || file.write(bytes, 8) != 8) return file.errorString();
            qToLittleEndian<quint64>(size, bytes);
            if (!file.seek(offset + 48) || file.write(bytes, 8) != 8) return file.errorString();
            return {};
        }
        if (length < 8) return QStringLiteral("The extractor has an invalid Mach-O load command.");
        offset += length;
    }
    return QStringLiteral("The extractor does not contain a __LINKEDIT segment.");
}

}

QString createConsoleSfx(const QString &stub, const QString &archive, const QString &output,
                         const std::function<bool()> &cancelled) {
    if (QFileInfo(output).exists()) return QStringLiteral("The output file already exists.");
    QFile payload(archive);
    if (!payload.open(QIODevice::ReadOnly)) return payload.errorString();
    if (payload.read(6) != QByteArray::fromHex("377abcaf271c"))
        return QStringLiteral("Console SFX requires a 7z archive.");
    payload.seek(0);
    if (!QFile::copy(stub, output)) return QStringLiteral("Cannot copy the console extractor to the output path.");
    const auto fail = [&output](const QString &error) {
        QFile::remove(output);
        return error;
    };
    QString error = sign({QStringLiteral("--remove-signature"), output}, cancelled);
    if (!error.isEmpty()) return fail(error);
    QFile result(output);
    if (!result.open(QIODevice::ReadWrite)) return fail(result.errorString());
    if (!result.seek(result.size())) return fail(result.errorString());
    QByteArray block(1024 * 1024, Qt::Uninitialized);
    while (!payload.atEnd()) {
        if (cancelled && cancelled()) return fail(QStringLiteral("Self-extracting package creation was cancelled."));
        const qint64 count = payload.read(block.data(), block.size());
        if (count < 0) return fail(payload.errorString());
        if (result.write(block.constData(), count) != count) return fail(result.errorString());
    }
    error = extendLinkedit(result);
    result.close();
    if (!error.isEmpty()) return fail(error);
    if (!QFile::setPermissions(output, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                     QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther))
        return fail(QStringLiteral("Cannot set executable permissions on the output."));
    error = sign({QStringLiteral("--force"), QStringLiteral("--sign"), QStringLiteral("-"),
                  QStringLiteral("--timestamp=none"), output}, cancelled);
    if (!error.isEmpty()) return fail(error);
    return {};
}

}
