#include "ArchiveModel.h"
#include <QApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSet>
#include <QStyle>
#include <algorithm>

namespace m7arc::ui {
ArchiveModel::ArchiveModel(QObject* p)
    : QAbstractTableModel(p), folderIcon_(QApplication::style()->standardIcon(QStyle::SP_DirIcon)),
      fileIcon_(QApplication::style()->standardIcon(QStyle::SP_FileIcon)) {}
int ArchiveModel::rowCount(const QModelIndex& p) const {
    return p.isValid() ? 0 : visible_.size();
}
int ArchiveModel::columnCount(const QModelIndex& p) const {
    return p.isValid() ? 0 : 9 + extraProperties_.size();
}
QVariant ArchiveModel::headerData(int n, Qt::Orientation o, int r) const {
    static const QStringList titles = {tr("名称"), tr("大小"), tr("压缩后"), tr("类型"), tr("修改时间"),
                                       tr("方法"), tr("加密"), tr("CRC"),    tr("属性")};
    if (o != Qt::Horizontal || r != Qt::DisplayRole || n < 0)
        return {};
    if (n < titles.size())
        return titles[n];
    return n - titles.size() < extraProperties_.size() ? extraProperties_[n - titles.size()] : QVariant{};
}
QVariant ArchiveModel::data(const QModelIndex& i, int r) const {
    if (!i.isValid() || i.row() >= visible_.size())
        return {};
    const auto& e = visible_[i.row()];
    if (r == Qt::DecorationRole && i.column() == 0)
        return e.directory ? folderIcon_ : fileIcon_;
    if (r == Qt::TextAlignmentRole && (i.column() == 1 || i.column() == 2))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (r == Qt::ToolTipRole)
        return e.path;
    if (r != Qt::DisplayRole)
        return {};
    switch (i.column()) {
    case 0:
        return flat_ ? e.path : e.path.mid(directory_.size());
    case 1:
        return e.directory ? QVariant{} : QVariant(QLocale().formattedDataSize(e.size));
    case 2:
        return e.directory ? QVariant{} : QVariant(QLocale().formattedDataSize(e.packedSize));
    case 3:
        return e.directory ? tr("文件夹") : QFileInfo(e.path).suffix();
    case 4:
        return e.modified.isValid() ? e.modified.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : QString{};
    case 5:
        return e.method;
    case 6:
        return e.encrypted ? tr("是") : tr("否");
    case 7:
        return e.properties.value("CRC");
    case 8:
        return e.properties.value("Attributes");
    }
    if (i.column() >= 9 && i.column() - 9 < extraProperties_.size())
        return e.properties.value(extraProperties_[i.column() - 9]);
    return {};
}
Qt::ItemFlags ArchiveModel::flags(const QModelIndex& i) const {
    return QAbstractTableModel::flags(i) | (i.isValid() ? Qt::ItemIsDragEnabled : Qt::NoItemFlags);
}
QStringList ArchiveModel::mimeTypes() const {
    return {"application/x-m7arc-members"};
}
QMimeData* ArchiveModel::mimeData(const QModelIndexList& indexes) const {
    auto* mime = new QMimeData;
    QJsonObject object;
    object["archive"] = archivePath_;
    QJsonArray ids;
    for (auto id : indexesForRows(indexes))
        ids.append(int(id));
    object["indices"] = ids;
    mime->setData("application/x-m7arc-members", QJsonDocument(object).toJson(QJsonDocument::Compact));
    return mime;
}
void ArchiveModel::setEntries(QList<ArchiveRow> e, const QString& directory) {
    entries_ = std::move(e);
    directory_ = directory;
    if (!directory_.isEmpty() && !directory_.endsWith('/'))
        directory_ += '/';
    rebuild(true);
}
void ArchiveModel::setDirectory(const QString& p) {
    const auto normalized = p.isEmpty() || p.endsWith('/') ? p : p + '/';
    if (directory_ == normalized)
        return;
    directory_ = normalized;
    rebuild();
}
void ArchiveModel::setFlat(bool f) {
    if (flat_ == f)
        return;
    flat_ = f;
    rebuild();
}
void ArchiveModel::sort(int c, Qt::SortOrder o) {
    if (sortColumn_ == c && order_ == o)
        return;
    sortColumn_ = c;
    order_ = o;
    rebuild();
}
void ArchiveModel::rebuild(bool updateProperties) {
    beginResetModel();
    if (updateProperties) {
        extraProperties_.clear();
        QSet<QString> represented = {"Path",     "Name",   "Size",      "Packed size", "Directory",
                                     "Modified", "Method", "Encrypted", "CRC",         "Attributes"};
        for (const auto& entry : entries_)
            for (auto it = entry.properties.begin(); it != entry.properties.end(); ++it)
                if (!represented.contains(it.key())) {
                    extraProperties_ << it.key();
                    represented.insert(it.key());
                }
    }
    visible_.clear();
    visible_.reserve(entries_.size());
    QSet<QString> dirs;
    for (const auto& e : entries_) {
        if (!e.path.startsWith(directory_) || e.path == directory_)
            continue;
        const QString rest = e.path.mid(directory_.size());
        const auto slash = rest.indexOf('/');
        if (!flat_ && slash >= 0 && slash < rest.size() - 1) {
            const auto p = directory_ + rest.left(slash);
            if (!dirs.contains(p)) {
                ArchiveRow d;
                d.path = p;
                d.directory = true;
                visible_.append(d);
                dirs.insert(p);
            }
        } else {
            if (e.directory) {
                const auto p = e.path.endsWith('/') ? e.path.chopped(1) : e.path;
                if (dirs.contains(p))
                    continue;
                dirs.insert(p);
            }
            visible_.append(e);
        }
    }
    std::stable_sort(visible_.begin(), visible_.end(), [this](const ArchiveRow& a, const ArchiveRow& b) {
        if (a.directory != b.directory)
            return a.directory;
        int compare = 0;
        if (sortColumn_ == 1)
            compare = (a.size > b.size) - (a.size < b.size);
        else if (sortColumn_ == 2)
            compare = (a.packedSize > b.packedSize) - (a.packedSize < b.packedSize);
        else if (sortColumn_ == 4)
            compare = (a.modified > b.modified) - (a.modified < b.modified);
        else if (sortColumn_ == 3)
            compare = QString::localeAwareCompare(QFileInfo(a.path).suffix(), QFileInfo(b.path).suffix());
        else
            compare = QString::localeAwareCompare(a.path, b.path);
        return order_ == Qt::AscendingOrder ? compare < 0 : compare > 0;
    });
    endResetModel();
}
QList<quint32> ArchiveModel::indexesForRows(const QModelIndexList& rows) const {
    QSet<quint32> values;
    for (const auto& i : rows) {
        const auto& picked = visible_.at(i.row());
        if (!picked.directory) {
            values.insert(picked.index);
            continue;
        }
        for (const auto& entry : entries_) {
            const auto p = picked.path.endsWith('/') ? picked.path : picked.path + '/';
            if (entry.path == picked.path || (picked.directory && entry.path.startsWith(p)))
                values.insert(entry.index);
        }
    }
    QList<quint32> result(values.begin(), values.end());
    std::sort(result.begin(), result.end());
    return result;
}
} // namespace m7arc::ui
