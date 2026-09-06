#include "ui/ArchiveDialogs.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QDirIterator>
#include <QFormLayout>
#include <QLabel>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

using namespace m7arc;

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

static QComboBox* field(QDialog& dialog, const QString& label) {
    for (auto* form : dialog.findChildren<QFormLayout*>())
        for (int row = 0; row < form->rowCount(); ++row) {
            auto* item = form->itemAt(row, QFormLayout::LabelRole);
            auto* text = item ? qobject_cast<QLabel*>(item->widget()) : nullptr;
            if (text && text->text() == label)
                return qobject_cast<QComboBox*>(form->itemAt(row, QFormLayout::FieldRole)->widget());
        }
    throw std::runtime_error("Compression field unavailable");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("M7ArcTests");
    QCoreApplication::setApplicationName("CreateDialog");
    try {
        QTemporaryDir temp;
        require(temp.isValid(), "Temporary directory unavailable");
        const auto source = temp.filePath("样本.txt");
        QFile input(source);
        require(input.open(QIODevice::WriteOnly), "Cannot write input");
        const QByteArray payload("Qt options reach the archive engine\n");
        input.write(payload);
        input.close();
        ArchiveEngine engine;
        ui::CreateDialog dialog({source}, temp.path());
        for (const QString format :
             {"7z", "zip", "gzip", "bzip2", "xz", "tar", "wim", "tar.gz", "tar.bz2", "tar.xz"}) {
            dialog.setFormat(format);
            const auto archive = temp.filePath("roundtrip." + format);
            dialog.setDestination(archive);
            const auto options = dialog.options();
            auto created = engine.create({source.toStdString()}, archive.toStdString(), options);
            if (!created.ok())
                throw std::runtime_error(format.toStdString() + ": " + created.errors.front());
            const auto destination = temp.filePath("out-" + format);
            auto extracted = engine.extract(archive.toStdString(), destination.toStdString());
            require(extracted.ok(), "Cannot extract UI-created archive");
            bool found = false;
            QDirIterator files(destination, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                QFile file(files.next());
                found |= file.open(QIODevice::ReadOnly) && file.readAll() == payload;
            }
            if (!found)
                throw std::runtime_error(format.toStdString() +
                                         " UI defaults did not preserve file contents");
        }
        dialog.setFormat("7z");
        field(dialog, "字典 / 内存")->setEditText("3.75 GiB");
        require(dialog.options().dictionaryBytes == 4026531840ULL, "Fractional dictionary size was lost");
        field(dialog, "固实块")->setEditText("32M");
        require(dialog.options().solidBlock == "32M", "Custom solid block size was lost");
        field(dialog, "固实块")->setCurrentText("全部固实");
        require(dialog.options().solidBlock == "18446744073709551615b", "Full solid mode was lost");
        dialog.setFormat("xz");
        field(dialog, "固实块")->setCurrentText("单块");
        require(dialog.options().solidBlock == "on", "XZ single-block mode was lost");
        std::cout << "Ten Qt format presets produced extractable archives; custom sizes preserved\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
