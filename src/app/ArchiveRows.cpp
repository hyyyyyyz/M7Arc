#include "ArchiveRows.h"
#include "FormatCatalog.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

namespace m7arc::app {
namespace {
QList<ArchiveProperty> toProperties(const std::vector<Property>& values) {
    QList<ArchiveProperty> result;
    for (const auto& property : values)
        result.append(
            {property.id, QString::fromStdString(property.name), QString::fromStdString(property.value)});
    return result;
}
QString folderPrefix(const QString& path) {
    return path.endsWith('/') ? path : path + '/';
}
} // namespace

ArchiveListing toListing(const ArchiveSnapshot& snapshot) {
    ArchiveListing listing;
    listing.path = QString::fromStdString(snapshot.path);
    listing.format = QString::fromStdString(snapshot.format);
    listing.canUpdate = snapshot.canUpdate;
    listing.canSetComment = snapshot.canSetComment;
    listing.properties = toProperties(snapshot.properties);
    listing.volumes = toQStringList(snapshot.volumes);
    for (const auto& e : snapshot.entries) {
        ArchiveRow item;
        item.index = e.index;
        item.path = QString::fromStdString(e.path);
        item.size = e.size;
        item.packedSize = e.packedSize;
        item.directory = e.isDirectory;
        item.encrypted = e.encrypted;
        item.alternateStream = e.alternateStream;
        if (e.modifiedUnixSeconds)
            item.modified = QDateTime::fromSecsSinceEpoch(e.modifiedUnixSeconds);
        item.method = QString::fromStdString(e.method);
        item.rawProperties = toProperties(e.properties);
        for (const auto& p : e.properties)
            item.properties[QString::fromStdString(p.name)] = QString::fromStdString(p.value);
        item.properties["Attributes"] = QString::fromStdString(e.attributes);
        listing.rows.append(item);
    }
    return listing;
}
QStringList toQStringList(const std::vector<std::string>& values) {
    QStringList result;
    for (const auto& value : values)
        result.append(QString::fromStdString(value));
    return result;
}
std::vector<std::string> toStdStrings(const QStringList& values) {
    std::vector<std::string> result;
    for (const auto& value : values)
        result.push_back(value.toStdString());
    return result;
}
std::vector<std::uint32_t> toIndexVector(const QList<quint32>& values) {
    return {values.begin(), values.end()};
}
QList<ArchiveRow> outermostRows(const QList<ArchiveRow>& selected) {
    QList<ArchiveRow> result;
    for (const auto& row : selected) {
        const bool covered = std::any_of(selected.begin(), selected.end(), [&](const ArchiveRow& other) {
            return other.directory && other.path != row.path && row.path.startsWith(folderPrefix(other.path));
        });
        if (!covered)
            result.append(row);
    }
    return result;
}
bool isArchiveName(const QString& file) {
    const auto suffix = QFileInfo(file).suffix();
    for (const auto& format : loadFormatCatalog()["formats"].toArray())
        for (const auto& extension : format.toObject()["extensions"].toArray())
            if (extension.toString().compare(suffix, Qt::CaseInsensitive) == 0)
                return true;
    return suffix == "001";
}
} // namespace m7arc::app
