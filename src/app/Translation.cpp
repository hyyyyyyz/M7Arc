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
    if (language == "en") {
        auto* translator = new QTranslator(&application);
        if (translator->load(":/i18n/m7arc_en.qm"))
            application.installTranslator(translator);
        else
            delete translator;
    }
}
} // namespace m7arc
