#pragma once
#include "engine/ArchiveEngine.h"
#include <QDialog>
#include <QJsonObject>
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QLabel;
namespace m7arc::ui {
class CreateDialog final : public QDialog {
    Q_OBJECT
  public:
    CreateDialog(const QStringList& inputs, const QString& directory, QWidget* parent = nullptr);
    QString destination() const;
    CreateOptions options() const;
    void setFormat(const QString& format);
    void setDestination(const QString& path);

  private:
    void updateFormat();
    void updateMethod();
    void updateMemory();
    void validate();
    QLineEdit *destination_, *password_, *confirmation_, *volumes_, *raw_;
    QComboBox *format_, *level_, *method_, *dictionary_, *word_, *solid_, *update_, *encryption_;
    QSpinBox* threads_;
    QCheckBox *headers_, *deleteSources_, *links_, *hardLinks_, *preserveAtime_;
    QCheckBox *metadata_, *shared_, *latestTime_, *ownerId_, *ownerName_, *advancedMethods_;
    QComboBox *storedPaths_, *filter_, *mtime_, *ctime_, *atime_, *precision_;
    QLineEdit *include_, *exclude_;
    QLabel *capability_, *compressionMemory_, *decompressionMemory_;
    QStringList inputs_;
};
class ExtractDialog final : public QDialog {
    Q_OBJECT
  public:
    ExtractDialog(const QString& archive, const QString& destination, QWidget* parent = nullptr);
    QString destination() const;
    ExtractOptions options() const;
    void setCurrentDirectory(const QString& directory);

  private:
    QLineEdit *destination_, *password_;
    QComboBox *pathMode_, *overwrite_;
    QCheckBox *subfolder_, *duplicateRoot_, *metadata_;
    QString currentDirectory_;
};
QJsonObject formatCatalog();
void showFormatCatalog(QWidget* parent);
void showSettings(QWidget* parent);
} // namespace m7arc::ui
