#pragma once
#include <QByteArray>
#include <QString>

namespace m7arc::app {
// Typed access to the persistent preferences. Every key and its default lives in Settings.cpp so
// that the settings dialog, the panes and the application layer cannot disagree about either.
class Settings {
  public:
    // Folder for scratch files. Falls back to the system temporary directory while no valid
    // override is stored, so callers can always append a subdirectory template to it.
    QString tempDirectory() const;
    // The stored override exactly as entered; empty while unset. For the settings dialog.
    QString tempDirectoryOverride() const;
    void setTempDirectory(const QString& path);

    bool showHidden() const;
    void setShowHidden(bool);
    bool alternatingRows() const;
    void setAlternatingRows(bool);
    bool autoRefresh() const;
    void setAutoRefresh(bool);
    bool preserveMacMetadata() const;
    void setPreserveMacMetadata(bool);
    bool restoreWindow() const;
    void setRestoreWindow(bool);
    bool dualPane() const;
    void setDualPane(bool);

    QString language() const; // "system", "zh_CN" or "en"
    // The language actually in effect: "zh_CN" or "en".
    QString effectiveLanguage() const;
    void setLanguage(const QString& language);

    QString startupPath() const;
    void setStartupPath(const QString& path);
    QString rightPanePath() const;
    void setRightPanePath(const QString& path);

    QString viewer() const;
    void setViewer(const QString& program);
    QString editor() const;
    void setEditor(const QString& program);
    QString diffTool() const;
    void setDiffTool(const QString& program);

    // Commands that can be hidden from the menus and toolbar; visible unless switched off.
    bool commandVisible(const QString& id) const;
    void setCommandVisible(const QString& id, bool visible);

    QString favorite(int slot) const;
    void setFavorite(int slot, const QString& path);

    bool hasWindowGeometry() const;
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);
};
} // namespace m7arc::app
