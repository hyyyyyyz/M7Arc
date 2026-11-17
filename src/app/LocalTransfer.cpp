#include "LocalTransfer.h"
#include "platform/MacMetadata.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>

namespace m7arc::app {
namespace {
bool copyPath(const QString& src, const QString& dst, const Callbacks& cb, OperationResult& result) {
    if (cb.cancelled && cb.cancelled()) {
        result.cancelled = true;
        return false;
    }
    QTemporaryDir stage(QFileInfo(dst).absolutePath() + "/.m7arc-copy-XXXXXX");
    if (!stage.isValid()) {
        result.errors.push_back(QObject::tr("无法创建复制工作目录：%1").arg(dst).toStdString());
        return false;
    }
    const auto pending = QDir(stage.path()).filePath("item");
    try {
        mac::copyItem(src.toStdString(), pending.toStdString(), [&](std::uint64_t completed) {
            if (cb.progress)
                return cb.progress({"复制", src.toStdString(), completed, quint64(QFileInfo(src).size()),
                                    !QFileInfo(src).isDir()});
            return !cb.cancelled || !cb.cancelled();
        });
    } catch (const std::exception& e) {
        result.cancelled = cb.cancelled && cb.cancelled();
        if (!result.cancelled)
            result.errors.push_back(e.what());
        return false;
    }
    const auto backup = QDir(stage.path()).filePath("previous");
    const bool exists = QFileInfo::exists(dst) || QFileInfo(dst).isSymLink();
    if (exists && !QDir().rename(dst, backup)) {
        result.errors.push_back(QObject::tr("无法替换目标：%1").arg(dst).toStdString());
        return false;
    }
    if (!QDir().rename(pending, dst)) {
        if (exists && !QDir().rename(backup, dst)) {
            stage.setAutoRemove(false);
            result.errors.push_back(QObject::tr("原文件保留在：%1").arg(backup).toStdString());
        }
        result.errors.push_back(QObject::tr("无法完成复制：%1").arg(dst).toStdString());
        return false;
    }
    ++result.completedItems;
    return true;
}
} // namespace

bool removePath(const QString& p) {
    QFileInfo f(p);
    return f.isDir() && !f.isSymLink() ? QDir(p).removeRecursively() : QFile::remove(p);
}

QString renamedPath(const QString& path) {
    QFileInfo f(path);
    for (int n = 1;; ++n) {
        const auto candidate = f.dir().filePath(f.completeBaseName() + "_" + QString::number(n) +
                                                (f.suffix().isEmpty() ? QString{} : "." + f.suffix()));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
}

OperationResult transferLocal(const QStringList& sources, const QString& directory, bool move,
                              const Callbacks& cb) {
    OperationResult result;
    if (!QDir().mkpath(directory)) {
        result.errors.push_back(QObject::tr("无法创建目标目录：%1").arg(directory).toStdString());
        return result;
    }
    bool all = false, skipAll = false;
    for (const auto& src : sources) {
        QFileInfo info(src);
        QString dest = QDir(directory).filePath(info.fileName());
        if (QDir::cleanPath(dest) == QDir::cleanPath(src)) {
            result.errors.push_back(QObject::tr("源和目标相同：%1").arg(src).toStdString());
            continue;
        }
        if (info.isDir() && QDir::cleanPath(dest).startsWith(QDir::cleanPath(src) + "/")) {
            result.errors.push_back(QObject::tr("目标不能位于源文件夹内：%1").arg(dest).toStdString());
            continue;
        }
        if (QFileInfo::exists(dest)) {
            if (skipAll)
                continue;
            auto answer = all ? OverwriteAnswer::Replace
                              : cb.overwrite({dest.toStdString(), src.toStdString(),
                                              quint64(QFileInfo(dest).size()), quint64(info.size())});
            if (answer == OverwriteAnswer::Cancel) {
                result.cancelled = true;
                break;
            }
            if (answer == OverwriteAnswer::Skip)
                continue;
            if (answer == OverwriteAnswer::SkipAll) {
                skipAll = true;
                continue;
            }
            if (answer == OverwriteAnswer::ReplaceAll)
                all = true;
            if (answer == OverwriteAnswer::RenameIncoming)
                dest = renamedPath(dest);
        }
        if (move && !QFileInfo::exists(dest) && QDir().rename(src, dest)) {
            ++result.completedItems;
            continue;
        }
        if (copyPath(src, dest, cb, result) && move && !removePath(src))
            result.errors.push_back(QObject::tr("已复制，但无法移除源：%1").arg(src).toStdString());
        if (result.cancelled)
            break;
    }
    return result;
}
} // namespace m7arc::app
