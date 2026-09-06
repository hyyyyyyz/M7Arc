#include "Translation.h"

#include <QApplication>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace m7arc {
void loadTranslations(QApplication& application) {
    QString language = QSettings().value("ui/language", "system").toString();
    if (language == "system")
        language = QLocale::system().language() == QLocale::Chinese ? "zh_CN" : "en";
    auto* translator = new QTranslator(&application);
    const auto catalog = language == "zh_CN" ? ":/i18n/qtbase_zh_CN.qm" : ":/i18n/m7arc_en.qm";
    if (translator->load(catalog))
        application.installTranslator(translator);
    else
        delete translator;
}
} // namespace m7arc
