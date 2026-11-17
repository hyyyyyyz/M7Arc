#pragma once
#include "ArchiveModel.h"
#include "app/CommandRegistry.h"
#include "app/PaneState.h"
#include "app/Settings.h"
#include "app/Workspace.h"
#include "SystemClipboard.h"
#include <QHash>
#include <QMainWindow>
class QAction;
class QMenu;
class QSplitter;
class QToolBar;
namespace m7arc::ui {
class FilePane;
class OperationController;
// Window, menus and toolbar. Commands are registered by identifier and carried out by the
// application layer (app::Workspace); this class asks the questions and refreshes the panes.
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openPath(const QString& path);
    void runService(const QString& action, const QStringList& paths);

  protected:
    void closeEvent(QCloseEvent*) override;

  private:
    QAction* command(const QString& id, const QString& text, const QKeySequence& shortcut = {},
                     bool checkable = false);
    void buildMenus();
    void registerCommands();
    void updateActions();
    void applySettings();
    void openArchive(FilePane* pane, const QString& path, const QString& type = {}, bool nested = false);
    void openMember(FilePane* pane, const ArchiveRow& row, const QString& program = {}, bool internal = true);
    void openCommand(const QString& id);
    void createArchive(const QString& format = {});
    void extract(bool here = false, bool folder = false);
    void testArchive();
    void copyMove(bool move);
    void copyToClipboard();
    void pasteFromClipboard();
    void refreshPanes();
    void removeSelection();
    void renameSelection();
    void createItem(bool directory);
    void createLink();
    void properties();
    void editComment();
    void hashes(const QStringList& algorithms);
    void showStreams();
    void showTemporaryFiles();
    void splitFile();
    void combineFiles();
    void selectMask(bool on, bool byType = false);
    void checkEditedFiles();
    void transferToPane(FilePane* source, FilePane* target, bool move, bool confirm = true);
    void dragArchiveMembers(FilePane* pane, QWidget* source);
    bool verifyArchiveSelection(FilePane* pane);
    FilePane* otherPane() const;
    FilePane* active_;
    FilePane* left_;
    FilePane* right_;
    OperationController* jobs_;
    SystemClipboard clipboard_;
    app::Workspace* workspace_;
    app::CommandRegistry commands_;
    app::Settings settings_;
    QSplitter* splitter_;
    QToolBar* toolbar_;
    QMenu* fileMenu_;
    QMenu* favorites_;
    QHash<QString, QAction*> actions_;
    bool checkingEdits_ = false;
    QStringList serviceInputs_;
};
} // namespace m7arc::ui
