#pragma once
#include "engine/ArchiveEngine.h"
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace m7arc::app {
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
// One archive as the engine listed it, ready for display.
struct ArchiveListing {
    QString path;
    QString format;
    bool canUpdate = false;
    bool canSetComment = false;
    QList<ArchiveRow> rows;
    QList<ArchiveProperty> properties;
    QStringList volumes;
};

ArchiveListing toListing(const ArchiveSnapshot& snapshot);
QStringList toQStringList(const std::vector<std::string>& values);
std::vector<std::string> toStdStrings(const QStringList& values);
std::vector<std::uint32_t> toIndexVector(const QList<quint32>& values);
// Selected members that are not already covered by a selected folder; the others travel with it.
QList<ArchiveRow> outermostRows(const QList<ArchiveRow>& selected);
// True for a path whose suffix belongs to a supported archive format, or the first split volume.
bool isArchiveName(const QString& file);
} // namespace m7arc::app
