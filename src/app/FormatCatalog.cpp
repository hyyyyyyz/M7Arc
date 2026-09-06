#include "FormatCatalog.h"
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QSettings>

namespace m7arc {
QJsonObject loadFormatCatalog() {
    QString language = QSettings().value("ui/language", "system").toString();
    if (language == "system")
        language = QLocale::system().language() == QLocale::Chinese ? "zh_CN" : "en";
    QFile source(language == "en" ? ":/m7arc/formats.en.json" : ":/m7arc/formats.json");
    if (!source.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(source.readAll()).object();
}
} // namespace m7arc
