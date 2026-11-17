#include "Translation.h"
#include "Settings.h"
#include <QCoreApplication>
#include <QTranslator>

namespace m7arc {
void loadTranslations(QCoreApplication& application) {
    const auto language = app::Settings().effectiveLanguage();
    auto* translator = new QTranslator(&application);
    const auto catalog = language == "zh_CN" ? ":/i18n/qtbase_zh_CN.qm" : ":/i18n/m7arc_en.qm";
    if (translator->load(catalog))
        application.installTranslator(translator);
    else
        delete translator;
}
} // namespace m7arc
