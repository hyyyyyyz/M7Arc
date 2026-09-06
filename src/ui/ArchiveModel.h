#pragma once
#include <QAbstractTableModel>
#include <QDateTime>
#include <QIcon>
#include <QVariantMap>

namespace m7arc::ui {
struct ArchiveProperty {
    quint32 id = 0;
    QString name;
    QString value;
};
struct ArchiveContainer {
    QString path;
    QString format;
    QList<ArchiveProperty> properties;
    QStringList volumes;
};
struct ArchiveRow {
    quint32 index = 0;
    QString path;
    quint64 size = 0, packedSize = 0;
    bool directory = false, encrypted = false, alternateStream = false;
    QDateTime modified;
    QString method;
    QVariantMap properties;
    QList<ArchiveProperty> rawProperties;
};
class ArchiveModel final : public QAbstractTableModel {
    Q_OBJECT
  public:
    explicit ArchiveModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    int columnCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex&, int role) const override;
    QVariant headerData(int, Qt::Orientation, int role) const override;
    void sort(int column, Qt::SortOrder order) override;
    Qt::ItemFlags flags(const QModelIndex&) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList&) const override;
    void setArchivePath(const QString& path) {
        archivePath_ = path;
    }
    void setEntries(QList<ArchiveRow> entries, const QString& directory = {});
    void setDirectory(const QString& path);
    void setFlat(bool flat);
    QString directory() const {
        return directory_;
    }
    const ArchiveRow& entry(int row) const {
        return visible_.at(row);
    }
    QList<quint32> indexesForRows(const QModelIndexList& rows) const;
    const QList<ArchiveRow>& allEntries() const {
        return entries_;
    }

  private:
    void rebuild(bool updateProperties = false);
    QList<ArchiveRow> entries_, visible_;
    QStringList extraProperties_;
    QString directory_, archivePath_;
    bool flat_ = false;
    int sortColumn_ = 0;
    Qt::SortOrder order_ = Qt::AscendingOrder;
    QIcon folderIcon_, fileIcon_;
};
} // namespace m7arc::ui
