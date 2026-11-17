#include "FileMetadataDialog.h"
#include "OperationController.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cerrno>
#include <cstring>
#include <sys/xattr.h>

namespace m7arc::ui {
void showFileMetadata(QWidget* parent, const QString& path, OperationController* jobs) {
    const auto encoded = QFile::encodeName(path);
    const auto length = listxattr(encoded.constData(), nullptr, 0, XATTR_NOFOLLOW);
    if (length < 0) {
        QMessageBox::warning(parent, QObject::tr("无法读取扩展属性"),
                             QString::fromLocal8Bit(std::strerror(errno)));
        return;
    }
    QByteArray names(length, '\0');
    if (length && listxattr(encoded.constData(), names.data(), names.size(), XATTR_NOFOLLOW) < 0) {
        QMessageBox::warning(parent, QObject::tr("无法读取扩展属性"),
                             QString::fromLocal8Bit(std::strerror(errno)));
        return;
    }
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("扩展属性与资源叉 — %1").arg(QFileInfo(path).fileName()));
    dialog.resize(720, 480);
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(0, 2, &dialog);
    table->setHorizontalHeaderLabels({QObject::tr("属性名称"), QObject::tr("大小")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (const auto& name : names.split('\0')) {
        if (name.isEmpty())
            continue;
        const auto size = getxattr(encoded.constData(), name.constData(), nullptr, 0, 0, XATTR_NOFOLLOW);
        const auto row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(name)));
        table->setItem(
            row, 1,
            new QTableWidgetItem(size >= 0 ? QLocale().formattedDataSize(size) : QObject::tr("不可读取")));
    }
    layout->addWidget(table);
    if (!table->rowCount())
        layout->addWidget(new QLabel(QObject::tr("此文件没有扩展属性或资源叉。"), &dialog));
    auto* preview = new QPlainTextEdit(&dialog);
    preview->setReadOnly(true);
    preview->setMaximumHeight(150);
    layout->addWidget(preview);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QObject::tr("导出属性…"));
    buttons->button(QDialogButtonBox::Save)->setEnabled(false);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(table, &QTableWidget::itemSelectionChanged, &dialog, [=] {
        const auto row = table->currentRow();
        if (row < 0)
            return;
        buttons->button(QDialogButtonBox::Save)->setEnabled(true);
        const auto name = table->item(row, 0)->text().toUtf8();
        QByteArray data;
        if (name == "com.apple.ResourceFork") {
            QFile fork(path + "/..namedfork/rsrc");
            if (fork.open(QIODevice::ReadOnly))
                data = fork.read(4096);
        } else {
            const auto size = getxattr(encoded.constData(), name.constData(), nullptr, 0, 0, XATTR_NOFOLLOW);
            if (size > 1024 * 1024) {
                preview->setPlainText(QObject::tr("二进制属性较大，可导出后查看。"));
                return;
            }
            if (size >= 0) {
                data.resize(size);
                if (getxattr(encoded.constData(), name.constData(), data.data(), data.size(), 0,
                             XATTR_NOFOLLOW) < 0)
                    data.clear();
            }
        }
        bool text = !data.contains('\0') && QString::fromUtf8(data).toUtf8() == data;
        preview->setPlainText(text ? QString::fromUtf8(data.left(4096))
                                   : QString::fromLatin1(data.left(4096).toHex(' ')));
    });
    QObject::connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, &dialog, [=, &dialog] {
        const auto row = table->currentRow();
        if (row < 0)
            return;
        const auto name = table->item(row, 0)->text().toUtf8();
        const auto destination =
            QFileDialog::getSaveFileName(&dialog, QObject::tr("导出扩展属性"), QString::fromUtf8(name));
        if (destination.isEmpty())
            return;
        dialog.accept();
        jobs->run(QObject::tr("导出扩展属性"), [path, encoded, name,
                                                destination](ArchiveEngine&, const Callbacks& callbacks) {
            OperationResult result;
            QSaveFile output(destination);
            if (!output.open(QIODevice::WriteOnly))
                throw EngineError(output.errorString().toStdString());
            if (name == "com.apple.ResourceFork") {
                QFile fork(path + "/..namedfork/rsrc");
                if (!fork.open(QIODevice::ReadOnly))
                    throw EngineError(fork.errorString().toStdString());
                std::uint64_t completed = 0;
                while (!fork.atEnd()) {
                    auto block = fork.read(1024 * 1024);
                    if ((block.isEmpty() && fork.error() != QFile::NoError) ||
                        output.write(block) != block.size())
                        throw EngineError("Unable to read or write resource fork data");
                    completed += block.size();
                    if (callbacks.progress && !callbacks.progress({"导出资源叉", path.toStdString(),
                                                                   completed, quint64(fork.size()), true})) {
                        result.cancelled = true;
                        return result;
                    }
                }
            } else {
                const auto size =
                    getxattr(encoded.constData(), name.constData(), nullptr, 0, 0, XATTR_NOFOLLOW);
                if (size < 0)
                    throw EngineError(std::strerror(errno));
                QByteArray data(size, '\0');
                if (getxattr(encoded.constData(), name.constData(), data.data(), data.size(), 0,
                             XATTR_NOFOLLOW) < 0 ||
                    output.write(data) != data.size())
                    throw EngineError("Unable to read or write the extended attribute");
            }
            if (!output.commit())
                throw EngineError(output.errorString().toStdString());
            result.outputPaths.push_back(destination.toStdString());
            return result;
        });
    });
    dialog.exec();
}
void showArchiveStreams(QWidget* parent, const QString& archive, const QList<ArchiveRow>& streams,
                        OperationController* jobs) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("归档备用数据流"));
    dialog.resize(720, 450);
    const QFileInfo original(archive);
    const auto modified = original.lastModified();
    const auto size = original.size();
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(streams.size(), 3, &dialog);
    table->setHorizontalHeaderLabels({QObject::tr("路径"), QObject::tr("大小"), QObject::tr("方法")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (qsizetype i = 0; i < streams.size(); ++i) {
        const auto& stream = streams[i];
        table->setItem(i, 0, new QTableWidgetItem(stream.path));
        table->setItem(i, 1, new QTableWidgetItem(QLocale().formattedDataSize(stream.size)));
        table->setItem(i, 2, new QTableWidgetItem(stream.method));
    }
    layout->addWidget(table);
    layout->addWidget(new QLabel(streams.isEmpty() ? QObject::tr("所选项目没有备用数据流。")
                                                   : QObject::tr("数据流可提取为独立文件。"),
                                 &dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QObject::tr("提取所选数据流…"));
    buttons->button(QDialogButtonBox::Save)->setEnabled(false);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(table, &QTableWidget::itemSelectionChanged, &dialog, [=] {
        buttons->button(QDialogButtonBox::Save)
            ->setEnabled(!table->selectionModel()->selectedRows().isEmpty());
    });
    QObject::connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, &dialog, [=, &dialog] {
        std::vector<std::uint32_t> indices;
        for (const auto& row : table->selectionModel()->selectedRows())
            indices.push_back(streams[row.row()].index);
        if (indices.empty())
            return;
        const auto destination = QFileDialog::getExistingDirectory(&dialog, QObject::tr("提取数据流到"));
        if (destination.isEmpty())
            return;
        const QFileInfo current(archive);
        if (!current.exists() || current.size() != size || current.lastModified() != modified) {
            QMessageBox::warning(&dialog, QObject::tr("归档已变化"),
                                 QObject::tr("请刷新归档列表并重新选择数据流。"));
            dialog.reject();
            return;
        }
        dialog.accept();
        jobs->run(
            QObject::tr("提取备用数据流"),
            [archive, destination, indices](ArchiveEngine& engine, const Callbacks& callbacks) {
                ExtractOptions options;
                options.pathMode = PathMode::Flat;
                return engine.extract(archive.toStdString(), destination.toStdString(), indices, options,
                                      callbacks);
            },
            {}, true);
    });
    dialog.exec();
}
} // namespace m7arc::ui
