#pragma once
#include "app/EditSession.h"
#include "engine/ArchiveEngine.h"
#include <QHash>
#include <QMainWindow>
#include <memory>
class QAction;
class QMenu;
class QSplitter;
class QToolBar;
class QTemporaryDir;
namespace m7arc::ui {
class FilePane;
class OperationController;
struct ArchiveRow;
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
    void dispatch(const QString& id);
    void updateActions();
    void applySettings();
    void openArchive(FilePane* pane, const QString& path, const QString& type = {}, bool nested = false);
    void openMember(FilePane* pane, const ArchiveRow& row, const QString& program = {}, bool internal = true);
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
    void properties();
    void editComment();
    void hashes(const QStringList& algorithms);
    void splitFile();
    void combineFiles();
    void selectMask(bool on, bool byType = false);
    void showHelp();
    void checkEditedFiles();
    void transferToPane(FilePane* source, FilePane* target, bool move, bool confirm = true);
    void createSfx();
    void dragArchiveMembers(FilePane* pane, QWidget* source);
    bool isArchiveName(const QString& file) const;
    bool verifyArchiveSelection(FilePane* pane);
    FilePane* otherPane() const;
    FilePane* active_;
    FilePane* left_;
    FilePane* right_;
    OperationController* jobs_;
    QSplitter* splitter_;
    QToolBar* toolbar_;
    QMenu* fileMenu_;
    QMenu* favorites_;
    QHash<QString, QAction*> actions_;
    QHash<QString, bool> writable_;
    QHash<QString, bool> commentWritable_;
    QList<std::shared_ptr<QTemporaryDir>> temporary_;
    EditSessions edits_;
    bool checkingEdits_ = false;
    QStringList serviceInputs_;
};
} // namespace m7arc::ui
