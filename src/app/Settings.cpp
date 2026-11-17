#include "Settings.h"
#include <QDir>
#include <QLocale>
#include <QSettings>

namespace m7arc::app {
namespace {
constexpr auto kTempFolder = "folders/temp";
constexpr auto kShowHidden = "view/hidden";
constexpr auto kAlternating = "view/alternating";
constexpr auto kAutoRefresh = "view/autoRefresh";
constexpr auto kMacMetadata = "archive/macMetadata";
constexpr auto kRestoreWindow = "window/restore";
constexpr auto kWindowGeometry = "window/geometry";
constexpr auto kDualPane = "view/dual";
constexpr auto kLanguage = "ui/language";
constexpr auto kStartupPath = "startup/path";
constexpr auto kRightPane = "pane/right";
constexpr auto kViewer = "tools/viewer";
constexpr auto kEditor = "tools/editor";
constexpr auto kDiff = "tools/diff";

bool flag(const char* key, bool fallback) {
    return QSettings().value(key, fallback).toBool();
}
QString commandKey(const QString& id) {
    return "menus/" + id;
}
QString favoriteKey(int slot) {
    return "favorites/" + QString::number(slot);
}
} // namespace

QString Settings::tempDirectoryOverride() const {
    return QSettings().value(kTempFolder).toString().trimmed();
}
QString Settings::tempDirectory() const {
    const auto path = tempDirectoryOverride();
    return path.isEmpty() ? QDir::tempPath() : path;
}
void Settings::setTempDirectory(const QString& path) {
    QSettings settings;
    const auto trimmed = path.trimmed();
    if (trimmed.isEmpty())
        settings.remove(kTempFolder);
    else
        settings.setValue(kTempFolder, trimmed);
}

bool Settings::showHidden() const {
    return flag(kShowHidden, false);
}
void Settings::setShowHidden(bool on) {
    QSettings().setValue(kShowHidden, on);
}
bool Settings::alternatingRows() const {
    return flag(kAlternating, true);
}
void Settings::setAlternatingRows(bool on) {
    QSettings().setValue(kAlternating, on);
}
bool Settings::autoRefresh() const {
    return flag(kAutoRefresh, true);
}
void Settings::setAutoRefresh(bool on) {
    QSettings().setValue(kAutoRefresh, on);
}
bool Settings::preserveMacMetadata() const {
    return flag(kMacMetadata, true);
}
void Settings::setPreserveMacMetadata(bool on) {
    QSettings().setValue(kMacMetadata, on);
}
bool Settings::restoreWindow() const {
    return flag(kRestoreWindow, true);
}
void Settings::setRestoreWindow(bool on) {
    QSettings().setValue(kRestoreWindow, on);
}
bool Settings::dualPane() const {
    return flag(kDualPane, false);
}
void Settings::setDualPane(bool on) {
    QSettings().setValue(kDualPane, on);
}

QString Settings::language() const {
    return QSettings().value(kLanguage, "system").toString();
}
QString Settings::effectiveLanguage() const {
    const auto stored = language();
    if (stored != "system")
        return stored;
    return QLocale::system().language() == QLocale::Chinese ? "zh_CN" : "en";
}
void Settings::setLanguage(const QString& language) {
    QSettings().setValue(kLanguage, language);
}

QString Settings::startupPath() const {
    return QSettings().value(kStartupPath, QDir::homePath()).toString();
}
void Settings::setStartupPath(const QString& path) {
    QSettings().setValue(kStartupPath, path);
}
QString Settings::rightPanePath() const {
    return QSettings().value(kRightPane, QDir::homePath()).toString();
}
void Settings::setRightPanePath(const QString& path) {
    QSettings().setValue(kRightPane, path);
}

QString Settings::viewer() const {
    return QSettings().value(kViewer).toString();
}
void Settings::setViewer(const QString& program) {
    QSettings().setValue(kViewer, program);
}
QString Settings::editor() const {
    return QSettings().value(kEditor).toString();
}
void Settings::setEditor(const QString& program) {
    QSettings().setValue(kEditor, program);
}
QString Settings::diffTool() const {
    return QSettings().value(kDiff).toString();
}
void Settings::setDiffTool(const QString& program) {
    QSettings().setValue(kDiff, program);
}

bool Settings::commandVisible(const QString& id) const {
    return QSettings().value(commandKey(id), true).toBool();
}
void Settings::setCommandVisible(const QString& id, bool visible) {
    QSettings().setValue(commandKey(id), visible);
}

QString Settings::favorite(int slot) const {
    return QSettings().value(favoriteKey(slot)).toString();
}
void Settings::setFavorite(int slot, const QString& path) {
    QSettings().setValue(favoriteKey(slot), path);
}

bool Settings::hasWindowGeometry() const {
    return QSettings().contains(kWindowGeometry);
}
QByteArray Settings::windowGeometry() const {
    return QSettings().value(kWindowGeometry).toByteArray();
}
void Settings::setWindowGeometry(const QByteArray& geometry) {
    QSettings().setValue(kWindowGeometry, geometry);
}
} // namespace m7arc::app
