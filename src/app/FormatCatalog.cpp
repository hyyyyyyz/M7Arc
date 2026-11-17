#include "FormatCatalog.h"
#include "Settings.h"
#include <QFile>
#include <QJsonDocument>

namespace m7arc {
QJsonObject loadFormatCatalog() {
    const auto language = app::Settings().effectiveLanguage();
    QFile source(language == "en" ? ":/m7arc/formats.en.json" : ":/m7arc/formats.json");
    if (!source.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(source.readAll()).object();
}
} // namespace m7arc
