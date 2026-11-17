#include "HashDialog.h"
#include "OperationController.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <memory>

namespace m7arc::ui {
namespace {
QString string(const std::string& value) {
    return QString::fromStdString(value);
}
QTableWidget* table(QWidget* parent, const QStringList& columns) {
    auto* widget = new QTableWidget(0, columns.size(), parent);
    widget->setHorizontalHeaderLabels(columns);
    widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    widget->setSelectionBehavior(QAbstractItemView::SelectRows);
    widget->setAlternatingRowColors(true);
    widget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    widget->horizontalHeader()->setStretchLastSection(true);
    return widget;
}
void append(QTableWidget* table, const QStringList& values) {
    const auto row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < values.size(); ++column)
        table->setItem(row, column, new QTableWidgetItem(values[column]));
}
bool saveText(QWidget* parent, const QString& path, const QByteArray& contents) {
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.commit())
        return true;
    QMessageBox::warning(parent, QObject::tr("保存失败"), file.errorString());
    return false;
}
QString statusText(VerificationStatus status) {
    switch (status) {
    case VerificationStatus::Passed:
        return QObject::tr("通过");
    case VerificationStatus::Missing:
        return QObject::tr("缺失");
    case VerificationStatus::Mismatch:
        return QObject::tr("不匹配");
    case VerificationStatus::Error:
        return QObject::tr("错误");
    }
    return {};
}
} // namespace
QStringList chooseHashAlgorithms(QWidget* parent, const QStringList& algorithms) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("选择摘要算法"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    for (const auto& algorithm : algorithms) {
        auto* item = new QListWidgetItem(algorithm, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(algorithm == "SHA256" ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QStringList selected;
    if (dialog.exec() == QDialog::Accepted)
        for (int index = 0; index < list->count(); ++index)
            if (list->item(index)->checkState() == Qt::Checked)
                selected << list->item(index)->text();
    return selected;
}
void showHashResults(QWidget* parent, const HashSummary& result) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("CRC / SHA 结果"));
    dialog.resize(970, 620);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QObject::tr("%1 个文件 · %2 个文件夹 · %3 字节")
                                     .arg(result.fileCount)
                                     .arg(result.directoryCount)
                                     .arg(result.totalBytes),
                                 &dialog));
    auto* tabs = new QTabWidget(&dialog);
    auto* files =
        table(tabs, {QObject::tr("文件"), QObject::tr("算法"), QObject::tr("摘要"), QObject::tr("字节")});
    QString report;
    for (const auto& value : result.files) {
        append(files, {string(value.path), string(value.algorithm), string(value.digest),
                       QString::number(value.size)});
        report += string(value.algorithm) + "\t" + string(value.digest) + "\t" + string(value.path) + "\n";
    }
    tabs->addTab(files, QObject::tr("逐文件结果"));
    auto* totals =
        table(tabs, {QObject::tr("算法"), QObject::tr("数据总和"), QObject::tr("数据与文件名总和")});
    for (const auto& value : result.totals) {
        append(totals, {string(value.algorithm), string(value.dataSum), string(value.dataAndNamesSum)});
        report += string(value.algorithm) + "\tData=" + string(value.dataSum) +
                  "\tData+Names=" + string(value.dataAndNamesSum) + "\n";
    }
    tabs->addTab(totals, QObject::tr("总和"));
    layout->addWidget(tabs);
    auto* note = new QLabel(
        QObject::tr("总和采用 7-Zip 的摘要累加规则；数据与文件名总和也包含相对路径及目录。"), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* saveReport = buttons->addButton(QObject::tr("保存结果…"), QDialogButtonBox::ActionRole);
    auto* saveManifest = buttons->addButton(QObject::tr("保存 SHA 清单…"), QDialogButtonBox::ActionRole);
    QStringList available;
    for (const auto& item : result.files)
        if ((item.algorithm == "SHA256" || item.algorithm == "SHA1") &&
            !available.contains(string(item.algorithm)))
            available << string(item.algorithm);
    saveManifest->setEnabled(!available.isEmpty());
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(saveReport, &QPushButton::clicked, &dialog, [&] {
        const auto path =
            QFileDialog::getSaveFileName(&dialog, QObject::tr("保存摘要结果"), "M7Arc-hashes.txt");
        if (!path.isEmpty())
            saveText(&dialog, path, report.toUtf8());
    });
    QObject::connect(saveManifest, &QPushButton::clicked, &dialog, [&] {
        bool accepted = false;
        const auto algorithm = QInputDialog::getItem(&dialog, QObject::tr("保存 SHA 清单"),
                                                     QObject::tr("算法"), available, 0, false, &accepted);
        if (!accepted)
            return;
        const auto path = QFileDialog::getSaveFileName(&dialog, QObject::tr("保存 SHA 清单"),
                                                       "checksums." + algorithm.toLower());
        if (path.isEmpty())
            return;
        QString contents;
        for (const auto& value : result.files) {
            if (string(value.algorithm) != algorithm)
                continue;
            auto name = string(value.path);
            if (QFileInfo(name).isAbsolute())
                name = QDir(QFileInfo(path).absolutePath()).relativeFilePath(name);
            contents += string(value.digest) + "  " + name + "\n";
        }
        saveText(&dialog, path, contents.toUtf8());
    });
    dialog.exec();
}
void verifyHashManifest(QWidget* parent, OperationController* operations, const QString& path) {
    auto report = std::make_shared<ManifestResult>();
    operations->run(
        QObject::tr("验证摘要清单"),
        [path, report](ArchiveEngine& engine, const Callbacks& callbacks) {
            *report = engine.verifyManifest(path.toStdString(), callbacks);
            OperationResult result;
            result.cancelled = report->cancelled;
            return result;
        },
        [parent, path, report](const OperationResult& result) {
            if (result.cancelled || !result.ok())
                return;
            QDialog dialog(parent);
            dialog.setWindowTitle(QObject::tr("摘要清单验证"));
            dialog.resize(860, 540);
            auto* layout = new QVBoxLayout(&dialog);
            auto* heading = new QLabel(
                path + "\n" +
                    (report->ok() ? QObject::tr("全部通过") : QObject::tr("包含缺失、不匹配或错误项")),
                &dialog);
            heading->setWordWrap(true);
            layout->addWidget(heading);
            auto* rows = table(&dialog, {QObject::tr("文件"), QObject::tr("状态"), QObject::tr("详情")});
            QString text = path + "\n";
            for (const auto& entry : report->entries) {
                append(rows, {string(entry.path), statusText(entry.status), string(entry.message)});
                text += statusText(entry.status) + "\t" + string(entry.path) + "\t" + string(entry.message) +
                        "\n";
            }
            for (const auto& error : report->errors) {
                append(rows, {{}, QObject::tr("错误"), string(error)});
                text += string(error) + "\n";
            }
            layout->addWidget(rows);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Save, &dialog);
            layout->addWidget(buttons);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            QObject::connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, &dialog, [&] {
                const auto output = QFileDialog::getSaveFileName(&dialog, QObject::tr("保存验证结果"),
                                                                 "M7Arc-verification.txt");
                if (!output.isEmpty())
                    saveText(&dialog, output, text.toUtf8());
            });
            dialog.exec();
        },
        true);
}
} // namespace m7arc::ui
