#include "FileSplit.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace m7arc::app {
namespace {
QString volumeName(const QString& prefix, int number) {
    return prefix + QString::number(number).rightJustified(3, '0');
}
} // namespace

std::optional<quint64> parseVolumeSize(const QString& text) {
    static const QRegularExpression pattern("^([1-9][0-9]*)([KMGT]?)$", QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(text.trimmed());
    if (!match.hasMatch())
        return std::nullopt;
    quint64 size = match.captured(1).toULongLong();
    const auto suffix = match.captured(2).toUpper();
    int power = suffix.isEmpty() ? 0 : QString("KMGT").indexOf(suffix) + 1;
    while (power-- > 0)
        size *= 1024;
    return size;
}

OperationResult splitFile(const QString& source, const QString& directory, quint64 volume, const Callbacks& cb) {
    OperationResult r;
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        r.errors.push_back(input.errorString().toStdString());
        return r;
    }
    const auto prefix = QDir(directory).filePath(QFileInfo(source).fileName() + '.');
    quint64 total = input.size(), done = 0;
    int part = 1;
    while (!input.atEnd()) {
        const auto dest = volumeName(prefix, part++);
        if (QFileInfo::exists(dest)) {
            r.errors.push_back(QObject::tr("分卷已存在：%1").arg(dest).toStdString());
            return r;
        }
        QSaveFile output(dest);
        if (!output.open(QIODevice::WriteOnly)) {
            r.errors.push_back(output.errorString().toStdString());
            return r;
        }
        quint64 written = 0;
        while (written < volume && !input.atEnd()) {
            auto block = input.read(qMin<quint64>(1024 * 1024, volume - written));
            if (block.isEmpty() && input.error() != QFile::NoError) {
                r.errors.push_back(input.errorString().toStdString());
                return r;
            }
            if (output.write(block) != block.size()) {
                r.errors.push_back(output.errorString().toStdString());
                return r;
            }
            written += block.size();
            done += block.size();
            if (cb.progress && !cb.progress({"分割", source.toStdString(), done, total, true})) {
                r.cancelled = true;
                return r;
            }
        }
        if (!output.commit()) {
            r.errors.push_back(output.errorString().toStdString());
            return r;
        }
        r.outputPaths.push_back(dest.toStdString());
        ++r.completedItems;
    }
    return r;
}

QStringList volumeParts(const QString& firstVolume) {
    const auto prefix = QFileInfo(firstVolume).absoluteFilePath().chopped(3);
    QStringList parts;
    for (int n = 1;; ++n) {
        const auto path = volumeName(prefix, n);
        if (!QFileInfo::exists(path))
            break;
        parts << path;
    }
    return parts;
}

QString joinedName(const QString& firstVolume) {
    return QFileInfo(firstVolume).absoluteFilePath().chopped(4);
}

OperationResult combineVolumes(const QStringList& parts, const QString& dest, const Callbacks& cb) {
    OperationResult r;
    QSaveFile output(dest);
    if (!output.open(QIODevice::WriteOnly)) {
        r.errors.push_back(output.errorString().toStdString());
        return r;
    }
    quint64 total = 0, done = 0;
    for (const auto& p : parts)
        total += QFileInfo(p).size();
    for (const auto& p : parts) {
        QFile input(p);
        if (!input.open(QIODevice::ReadOnly)) {
            r.errors.push_back(input.errorString().toStdString());
            return r;
        }
        while (!input.atEnd()) {
            auto block = input.read(1024 * 1024);
            if (block.isEmpty() && input.error() != QFile::NoError) {
                r.errors.push_back(input.errorString().toStdString());
                return r;
            }
            if (output.write(block) != block.size()) {
                r.errors.push_back(output.errorString().toStdString());
                return r;
            }
            done += block.size();
            if (cb.progress && !cb.progress({"合并", p.toStdString(), done, total, true})) {
                r.cancelled = true;
                return r;
            }
        }
        ++r.completedItems;
    }
    if (!output.commit())
        r.errors.push_back(output.errorString().toStdString());
    return r;
}
} // namespace m7arc::app
