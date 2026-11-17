#include "Transfer.h"
#include "LocalTransfer.h"
#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>

namespace m7arc::app {
namespace {
QString trimmedMemberPath(QString path) {
    while (path.endsWith('/'))
        path.chop(1);
    return path;
}
} // namespace

CreateOptions insertOptions(const PaneState& target) {
    CreateOptions options;
    options.format = target.archiveFormat.toLower().toStdString();
    options.archivePrefix = target.archiveDirectory.toStdString();
    options.preserveMacMetadata = Settings().preserveMacMetadata();
    return options;
}

TransferRequest makeTransfer(const PaneState& source, const PaneState& target, bool move,
                             const QString& scratchRoot) {
    return {source, target, move, insertOptions(target), scratchRoot};
}

OperationResult runTransfer(ArchiveEngine& engine, const TransferRequest& request, const Callbacks& callbacks) {
    const auto& source = request.source;
    const auto& target = request.target;
    if (!source.inArchive() && !target.inArchive())
        return transferLocal(source.files, target.localDirectory, request.move, callbacks);

    QTemporaryDir stage(request.scratchRoot + "/M7Arc-Transfer-XXXXXX");
    QStringList inputs = source.files;
    OperationResult result;
    if (source.inArchive()) {
        if (!stage.isValid())
            throw EngineError("Unable to create temporary directory");
        result = engine.extract(source.archivePath.toStdString(), stage.path().toStdString(),
                                toIndexVector(source.indices), {}, callbacks);
        if (!result.ok())
            return result;
        inputs.clear();
        for (const auto& row : source.rows)
            inputs << QDir(stage.path()).filePath(trimmedMemberPath(row.path));
    }
    if (target.inArchive())
        result = engine.create(toStdStrings(inputs), target.archivePath.toStdString(), request.insert, callbacks);
    else
        result = transferLocal(inputs, target.localDirectory, false, callbacks);
    if (!result.ok() || !request.move)
        return result;
    if (source.inArchive()) {
        if (!target.inArchive() && result.completedItems != quint64(inputs.size())) {
            result.errors.push_back(QObject::tr("部分项目已跳过；源归档成员全部保留。").toStdString());
            return result;
        }
        return engine.remove(source.archivePath.toStdString(), toIndexVector(source.indices), {}, callbacks);
    }
    for (const auto& path : source.files)
        if (!removePath(path))
            result.errors.push_back(QObject::tr("已写入目标，但无法删除源：%1").arg(path).toStdString());
    return result;
}

OperationResult createArchiveItem(ArchiveEngine& engine, const PaneState& target, const CreateOptions& insert,
                                  const QString& name, bool directory, const QString& scratchRoot,
                                  const Callbacks& callbacks) {
    QTemporaryDir stage(scratchRoot + "/M7Arc-New-XXXXXX");
    if (!stage.isValid())
        throw EngineError("Unable to create temporary directory");
    const auto path = QDir(stage.path()).filePath(name);
    const bool created = directory ? QDir().mkdir(path) : QFile(path).open(QIODevice::WriteOnly | QIODevice::NewOnly);
    if (!created)
        throw EngineError("Unable to create new archive member");
    return engine.create({path.toStdString()}, target.archivePath.toStdString(), insert, callbacks);
}
} // namespace m7arc::app
