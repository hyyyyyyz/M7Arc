#include "app/Settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

using namespace m7arc;

static void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    QCoreApplication qtApp(argc, argv);
    QTemporaryDir home;
    require(home.isValid(), "Temporary directory unavailable");
    QCoreApplication::setOrganizationName("M7ArcSettingsTest");
    QCoreApplication::setApplicationName("M7ArcSettingsTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
    try {
        app::Settings settings;
        require(!settings.showHidden(), "Hidden files must default to hidden");
        require(settings.alternatingRows() && settings.autoRefresh() && settings.preserveMacMetadata() &&
                    settings.restoreWindow() && !settings.dualPane(),
                "Unexpected defaults");
        require(settings.commandVisible("extract"), "Commands must default to visible");
        require(settings.tempDirectory() == QDir::tempPath(), "Temporary directory must default to the system one");

        // An empty override must behave as unset: the scratch folder may never collapse to "/".
        settings.setTempDirectory("");
        require(settings.tempDirectory() == QDir::tempPath(), "Empty override replaced the system temporary directory");
        require(!QSettings().contains("folders/temp"), "Empty override left a stored key behind");
        settings.setTempDirectory("  " + home.path() + "  ");
        require(settings.tempDirectory() == home.path(), "Temporary directory override ignored");
        settings.setTempDirectory("   ");
        require(settings.tempDirectory() == QDir::tempPath() && settings.tempDirectoryOverride().isEmpty(),
                "Clearing the override did not restore the default");
        // A key written as an empty string by an older release is also treated as unset.
        QSettings().setValue("folders/temp", "");
        require(settings.tempDirectory() == QDir::tempPath(), "Stored empty string shadowed the default");

        settings.setShowHidden(true);
        settings.setCommandVisible("copy", false);
        settings.setFavorite(3, "/Users/example");
        settings.setLanguage("en");
        settings.setWindowGeometry("geometry");
        require(app::Settings().showHidden() && !app::Settings().commandVisible("copy"),
                "Values are not shared between instances");
        require(app::Settings().favorite(3) == "/Users/example" && app::Settings().favorite(4).isEmpty(),
                "Favorite slots mixed up");
        require(app::Settings().effectiveLanguage() == "en", "Explicit language ignored");
        require(app::Settings().hasWindowGeometry() && app::Settings().windowGeometry() == "geometry",
                "Window geometry lost");
        settings.setLanguage("system");
        const auto automatic = settings.effectiveLanguage();
        require(automatic == "zh_CN" || automatic == "en", "System language did not resolve");
        std::cout << "Settings defaults and overrides behave consistently\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
