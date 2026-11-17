#include "MainWindow.h"
#include "ArchiveDialogs.h"
#include "BenchmarkDialog.h"
#include "FileMetadataDialog.h"
#include "FilePane.h"
#include "HashDialog.h"
#include "HelpDialog.h"
#include "OperationController.h"
#include "SfxDialog.h"
#include "ToolbarIcons.h"
#include "app/ArchiveRows.h"
#include "app/FileSplit.h"
#include "platform/drag/MacFilePromises.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
namespace m7arc::ui {
namespace {
void launchFile(const QString& program, const QStringList& paths) {
    if (program.isEmpty()) {
        for (const auto& p : paths)
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    } else if (program.endsWith(".app", Qt::CaseInsensitive))
        QProcess::startDetached("/usr/bin/open", QStringList{"-a", program} + paths);
    else
        QProcess::startDetached(program, paths);
}
// Snapshot of what a pane shows and has selected, for the application layer.
app::PaneState stateOf(FilePane* pane) {
    app::PaneState state;
    state.archivePath = pane->archivePath();
    state.archiveDirectory = pane->archiveDirectory();
    state.archiveFormat = pane->archiveFormat().toLower();
    state.localDirectory = pane->localDirectory();
    state.files = pane->selectedFiles();
    state.rows = pane->selectedArchiveRows();
    state.indices = pane->selectedArchiveIndexes();
    return state;
}
} // namespace
void MainWindow::dragArchiveMembers(FilePane* pane, QWidget* source) {
    if (jobs_->busy() || !verifyArchiveSelection(pane))
        return;
    const auto selected = pane->view()->selectionModel()->selectedRows();
    auto* model = pane->archiveModel();
    QHash<QString, QModelIndex> byPath;
    QList<app::ArchiveRow> rows;
    for (const auto& index : selected) {
        rows.append(model->entry(index.row()));
        byPath.insert(rows.last().path, index);
    }
    QList<platform::ArchivePromiseItem> items;
    for (const auto& entry : app::outermostRows(rows)) {
        const auto path = entry.path.endsWith('/') ? entry.path.chopped(1) : entry.path;
        items.append({QFileInfo(path).fileName(), entry.directory, pane->archivePath(), entry.path,
                      model->indexesForRows({byPath.value(entry.path)})});
    }
    if (items.isEmpty())
        return;
    std::unique_ptr<QMimeData> mime(model->mimeData(selected));
    const auto payload = mime->data("application/x-m7arc-members");
    const auto original = QFileInfo(pane->archivePath());
    const auto size = original.size();
    const auto modified = original.lastModified();
    QPointer<MainWindow> window(this);
    const bool started = platform::beginArchivePromiseDrag(
        source, items, payload,
        [window, size, modified](const platform::ArchivePromiseItem& item, const QString& destination,
                                 platform::PromiseCompletion completion) {
            if (!window) {
                completion(QObject::tr("M7Arc 窗口已关闭。"));
                return;
            }
            if (window->jobs_->busy()) {
                completion(QObject::tr("另一个任务正在运行，请在完成后重新拖出文件。"));
                return;
            }
            const QFileInfo current(item.archivePath);
            if (!current.exists() || current.size() != size || current.lastModified() != modified) {
                completion(QObject::tr("归档已发生变化，请刷新列表并重新拖出文件。"));
                return;
            }
            window->jobs_->run(
                QObject::tr("提取到 Finder"),
                [item, destination](ArchiveEngine& engine, const Callbacks& callbacks) {
                    return platform::writeArchivePromise(engine, item, destination, callbacks);
                },
                [completion = std::move(completion)](const OperationResult& result) {
                    QStringList errors;
                    if (result.cancelled)
                        errors << QObject::tr("提取已取消。");
                    for (const auto& error : result.errors)
                        errors << QString::fromStdString(error);
                    completion(errors.join('\n'));
                },
                true);
        });
    if (!started)
        statusBar()->showMessage(tr("无法开始拖出文件，请使用“提取”。"));
}
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), active_(nullptr), left_(new FilePane(this)), right_(new FilePane(this)),
      jobs_(new OperationController(this)), workspace_(new app::Workspace(*jobs_, clipboard_, this)),
      splitter_(new QSplitter(this)) {
    setWindowTitle("M7Arc");
    setMinimumSize(760, 520);
    resize(1120, 720);
    setUnifiedTitleAndToolBarOnMac(true);
    splitter_->addWidget(left_);
    splitter_->addWidget(right_);
    setCentralWidget(splitter_);
    active_ = left_;
    right_->setVisible(settings_.dualPane());
    left_->navigate(settings_.startupPath());
    right_->navigate(settings_.rightPanePath());
    for (auto* pane : {left_, right_}) {
        pane->setLeaveGuard([this] {
            if (workspace_->edits().anyChanged()) {
                checkEditedFiles();
                return false;
            }
            return !jobs_->busy();
        });
        connect(pane, &FilePane::activated, this, [this](FilePane* p) {
            active_ = p;
            updateActions();
        });
        connect(pane, &FilePane::selectionChanged, this, &MainWindow::updateActions);
        connect(pane, &FilePane::locationChanged, this, &MainWindow::updateActions);
        connect(pane, &FilePane::openRequested, this, [this, pane](const QString& path) {
            active_ = pane;
            if (app::isArchiveName(path))
                openArchive(pane, path);
            else
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        connect(pane, &FilePane::archiveMemberRequested, this,
                [this, pane](const ArchiveRow& r) { openMember(pane, r); });
        connect(pane, &FilePane::archiveDragRequested, this,
                [this, pane](QWidget* source) { dragArchiveMembers(pane, source); });
        connect(pane, &FilePane::archiveRefreshRequested, this,
                [this, pane](const QString& p) { openArchive(pane, p); });
        connect(pane, &FilePane::contextMenuRequested, this, [this](const QPoint& p) { fileMenu_->exec(p); });
        connect(pane, &FilePane::filesDropped, this, [this, pane](const QStringList& paths, bool move) {
            if (paths.isEmpty() || jobs_->busy())
                return;
            const auto target = stateOf(pane);
            if (target.inArchive() && !workspace_->writable(target.archivePath))
                return;
            app::PaneState source;
            source.files = paths;
            workspace_->transfer(source, target, move, [pane](const OperationResult&) { pane->refresh(); });
        });
        connect(pane, &FilePane::archiveDropped, this, [this, pane](const QString& archive, bool move) {
            for (auto* source : {left_, right_})
                if (source != pane && source->archivePath() == archive) {
                    transferToPane(source, pane, move, false);
                    break;
                }
        });
    }
    buildMenus();
    registerCommands();
    toolbar_ = addToolBar(tr("操作"));
    toolbar_->setObjectName("MainToolbar");
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(28, 28));
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    const QList<QPair<QString, ToolbarGlyph>> icons = {
        {"add", ToolbarGlyph::Add},   {"extract", ToolbarGlyph::Extract}, {"test", ToolbarGlyph::Test},
        {"copy", ToolbarGlyph::Copy}, {"move", ToolbarGlyph::Move},       {"delete", ToolbarGlyph::Delete},
        {"info", ToolbarGlyph::Info}};
    for (int n = 0; n < icons.size(); ++n) {
        if (n == 3)
            toolbar_->addSeparator();
        auto* a = actions_[icons[n].first];
        a->setIcon(toolbarIcon(icons[n].second));
        toolbar_->addAction(a);
    }
    auto* space = new QWidget(toolbar_);
    space->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(space);
    actions_["formats"]->setIcon(toolbarIcon(ToolbarGlyph::Formats));
    actions_["two-panels"]->setIcon(toolbarIcon(ToolbarGlyph::Split));
    toolbar_->addAction(actions_["formats"]);
    toolbar_->addAction(actions_["two-panels"]);
    actions_["two-panels"]->setChecked(!right_->isHidden());
    statusBar()->showMessage(tr("就绪"));
    auto* progress = new QPushButton(tr("任务进度"), this);
    statusBar()->addPermanentWidget(progress);
    connect(progress, &QPushButton::clicked, jobs_, &OperationController::showProgress);
    connect(jobs_, &OperationController::busyChanged, this, &MainWindow::updateActions);
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            QTimer::singleShot(200, this, &MainWindow::checkEditedFiles);
    });
    connect(jobs_, &OperationController::statusChanged, statusBar(),
            [this](const QString& text) { statusBar()->showMessage(text); });
    applySettings();
    if (settings_.restoreWindow() && settings_.hasWindowGeometry())
        restoreGeometry(settings_.windowGeometry());
    updateActions();
}
QAction* MainWindow::command(const QString& id, const QString& text, const QKeySequence& shortcut,
                             bool checkable) {
    if (actions_.contains(id))
        return actions_[id];
    auto* a = new QAction(text, this);
    a->setMenuRole(QAction::NoRole);
    a->setObjectName(id);
    if (!shortcut.isEmpty())
        a->setShortcut(shortcut);
    a->setCheckable(checkable);
    actions_[id] = a;
    connect(a, &QAction::triggered, this, [this, id] { commands_.dispatch(id); });
    return a;
}

void MainWindow::buildMenus() {
    fileMenu_ = menuBar()->addMenu(tr("文件"));
    auto add = [this](QMenu* menu, const QString& id, const QString& text,
                      const QKeySequence& key = QKeySequence{}, bool check = false) {
        auto* a = command(id, text, key, check);
        menu->addAction(a);
        return a;
    };
    auto* seven = fileMenu_->addMenu("7-Zip");
    add(seven, "add", tr("添加"));
    add(seven, "extract", tr("提取"));
    add(seven, "extract-here", tr("提取到当前位置"));
    add(seven, "extract-folder", tr("提取到同名文件夹"));
    add(seven, "test", tr("测试"));
    add(seven, "add-7z", tr("添加为 7z…"));
    add(seven, "add-zip", tr("添加为 ZIP…"));
    auto* openAs = seven->addMenu(tr("按格式打开"));
    for (const auto& type : QStringList{"*", "#", "#:e", "7z", "zip", "cab", "rar"})
        add(openAs, "open-as:" + type, type);
    fileMenu_->addSeparator();
    add(fileMenu_, "open", tr("打开"));
    add(fileMenu_, "open-inside", tr("内部打开"), QKeySequence("Ctrl+PgDown"));
    add(fileMenu_, "open-outside", tr("外部打开"), QKeySequence("Shift+Return"));
    add(fileMenu_, "view-file", tr("查看"), QKeySequence(Qt::Key_F3));
    add(fileMenu_, "edit-file", tr("编辑"), QKeySequence(Qt::Key_F4));
    add(fileMenu_, "edit-save", tr("检查并保存归档内编辑…"));
    fileMenu_->addSeparator();
    add(fileMenu_, "rename", tr("重命名…"), QKeySequence(Qt::Key_F2));
    add(fileMenu_, "copy", tr("复制到…"), QKeySequence(Qt::Key_F5));
    add(fileMenu_, "move", tr("移动到…"), QKeySequence(Qt::Key_F6));
    add(fileMenu_, "delete", tr("删除…"), QKeySequence::Delete);
    fileMenu_->addSeparator();
    add(fileMenu_, "split", tr("分割文件…"));
    add(fileMenu_, "combine", tr("合并文件…"));
    add(fileMenu_, "info", tr("信息"), QKeySequence("Alt+Return"));
    add(fileMenu_, "comment", tr("注释…"));
    auto* hashMenu = fileMenu_->addMenu("CRC / SHA");
    ArchiveEngine engine;
    for (const auto& h : engine.hashers())
        add(hashMenu, "hash:" + QString::fromStdString(h.name), QString::fromStdString(h.name));
    add(hashMenu, "hash:*", tr("全部算法"));
    add(hashMenu, "hash:choose", tr("选择算法…"));
    hashMenu->addSeparator();
    add(hashMenu, "verify-manifest", tr("验证摘要清单…"));
    add(fileMenu_, "diff", tr("比较文件…"));
    fileMenu_->addSeparator();
    add(fileMenu_, "create-folder", tr("新建文件夹…"), QKeySequence(Qt::Key_F7));
    add(fileMenu_, "create-file", tr("新建文件…"));
    add(fileMenu_, "link", tr("创建链接…"));
    add(fileMenu_, "streams", tr("备用数据流…"));
    fileMenu_->addSeparator();
    add(fileMenu_, "exit", tr("退出 M7Arc"), QKeySequence::Quit)->setMenuRole(QAction::QuitRole);
    auto* edit = menuBar()->addMenu(tr("编辑"));
    add(edit, "select-all", tr("全选"), QKeySequence::SelectAll);
    add(edit, "deselect-all", tr("取消全选"), QKeySequence("Ctrl+Shift+A"));
    add(edit, "invert-selection", tr("反向选择"));
    add(edit, "select-mask", tr("按通配符选择…"));
    add(edit, "deselect-mask", tr("按通配符取消选择…"));
    add(edit, "select-type", tr("按类型选择"));
    add(edit, "deselect-type", tr("按类型取消选择"));
    edit->addSeparator();
    add(edit, "clipboard-cut", tr("剪切"), QKeySequence::Cut);
    add(edit, "clipboard-copy", tr("复制"), QKeySequence::Copy);
    add(edit, "clipboard-paste", tr("粘贴"), QKeySequence::Paste);
    auto* view = menuBar()->addMenu(tr("查看"));
    auto* mode = new QActionGroup(this);
    for (const auto& pair : QList<QPair<QString, QString>>{{"large", tr("大图标")},
                                                           {"small", tr("小图标")},
                                                           {"list", tr("列表")},
                                                           {"details", tr("详细信息")}}) {
        auto* a = add(view, "view-" + pair.first, pair.second, {}, true);
        mode->addAction(a);
        if (pair.first == "details")
            a->setChecked(true);
    }
    view->addSeparator();
    auto* sort = view->addMenu(tr("排序"));
    for (const auto& pair : QList<QPair<QString, QString>>{
             {"0", tr("名称")}, {"1", tr("大小")}, {"2", tr("类型")}, {"3", tr("修改时间")}})
        add(sort, "sort-" + pair.first, pair.second);
    add(view, "flat", tr("平面视图"), {}, true);
    add(view, "two-panels", tr("双面板"), QKeySequence(Qt::Key_F9), true);
    auto* tb = view->addMenu(tr("工具栏"));
    add(tb, "toolbar-text", tr("显示文字"), {}, true)->setChecked(true);
    add(tb, "toolbar-large", tr("大图标"), {}, true)->setChecked(true);
    view->addSeparator();
    add(view, "root", tr("根目录"));
    add(view, "up", tr("上一级"), QKeySequence("Alt+Up"));
    add(view, "address", tr("输入路径…"), QKeySequence("Ctrl+L"));
    add(view, "refresh", tr("刷新"), QKeySequence::Refresh);
    add(view, "show-hidden", tr("显示隐藏文件"), QKeySequence("Ctrl+Shift+."), true)
        ->setChecked(settings_.showHidden());
    favorites_ = menuBar()->addMenu(tr("收藏夹"));
    auto* set = favorites_->addMenu(tr("设置书签"));
    set->menuAction()->setMenuRole(QAction::NoRole);
    for (int i = 0; i < 10; ++i)
        add(set, "favorite-set:" + QString::number(i), tr("槽位 %1").arg(i));
    for (int i = 0; i < 10; ++i)
        add(favorites_, "favorite:" + QString::number(i),
            tr("%1 — %2").arg(i).arg(settings_.favorite(i).isEmpty() ? tr("未设置") : settings_.favorite(i)));
    auto* tools = menuBar()->addMenu(tr("工具"));
    add(tools, "formats", tr("格式与方法…"));
    add(tools, "options", tr("设置…"), QKeySequence::Preferences)->setMenuRole(QAction::PreferencesRole);
    add(tools, "benchmark", tr("性能测试…"));
    add(tools, "temp-cleanup", tr("临时文件…"));
    add(tools, "sfx", tr("创建自解压程序…"));
    auto* help = menuBar()->addMenu(tr("帮助"));
    add(help, "help", tr("M7Arc 帮助"), QKeySequence::HelpContents);
    add(help, "about", tr("关于 M7Arc"))->setMenuRole(QAction::AboutRole);
    actions_["comment"]->setToolTip(tr("编辑 ZIP 归档成员的注释"));
}

void MainWindow::applySettings() {
    left_->applySettings();
    right_->applySettings();
    for (const auto& id : QStringList{"add", "extract", "extract-here", "extract-folder", "test", "copy",
                                      "move", "delete", "info"})
        actions_[id]->setVisible(settings_.commandVisible(id));
    actions_["show-hidden"]->setChecked(settings_.showHidden());
}
void MainWindow::updateActions() {
    if (actions_.isEmpty())
        return;
    const bool archive = active_->inArchive();
    const bool chosen =
        archive ? !active_->selectedArchiveRows().isEmpty() : !active_->selectedFiles().isEmpty();
    const bool local = !archive;
    const bool busy = jobs_->busy();
    const bool write = local || workspace_->writable(active_->archivePath());
    for (const auto& id : QStringList{"add", "extract", "extract-here", "extract-folder", "test", "rename",
                                      "copy", "move", "delete", "split", "combine", "create-folder",
                                      "create-file", "link", "clipboard-paste", "add-7z", "add-zip"})
        actions_[id]->setEnabled(!busy);
    for (const auto& id : QStringList{"rename", "move", "delete"})
        actions_[id]->setEnabled(!busy && chosen && write);
    actions_["copy"]->setEnabled(!busy && chosen);
    for (const auto& id : QStringList{"create-folder", "create-file"})
        actions_[id]->setEnabled(!busy && write);
    actions_["link"]->setEnabled(!busy && local);
    actions_["clipboard-paste"]->setEnabled(!busy && write);
    actions_["clipboard-copy"]->setEnabled(!busy && chosen);
    actions_["clipboard-cut"]->setEnabled(!busy && local && chosen);
    for (const auto& id : QStringList{"split", "combine"})
        actions_[id]->setEnabled(!busy && local && active_->selectedFiles().size() == 1);
    for (auto it = actions_.begin(); it != actions_.end(); ++it)
        if (it.key().startsWith("hash:"))
            it.value()->setEnabled(!busy && chosen);
    actions_["verify-manifest"]->setEnabled(!busy);
    actions_["flat"]->setEnabled(archive);
    actions_["streams"]->setText(archive ? tr("备用数据流…") : tr("扩展属性与资源叉…"));
    const auto& entries = active_->archiveModel()->allEntries();
    const auto selectedRows = active_->selectedArchiveRows();
    const bool hasStreams = std::any_of(entries.begin(), entries.end(),
                                        [](const ArchiveRow& row) { return row.alternateStream; });
    const bool realMember =
        selectedRows.size() == 1 && std::any_of(entries.begin(), entries.end(), [&](const ArchiveRow& row) {
            return row.index == selectedRows.first().index && row.path == selectedRows.first().path;
        });
    actions_["streams"]->setEnabled(!busy && (archive ? hasStreams : active_->selectedFiles().size() == 1));
    actions_["comment"]->setEnabled(!busy && archive && realMember &&
                                    workspace_->commentWritable(active_->archivePath()));
    actions_["info"]->setEnabled(chosen || archive);
    actions_["add"]->setEnabled(!busy && write);
    actions_["add-7z"]->setEnabled(!busy && local && chosen);
    actions_["add-zip"]->setEnabled(!busy && local && chosen);
    actions_["sfx"]->setEnabled(!busy && local && chosen);
    actions_["diff"]->setEnabled(local && active_->selectedFiles().size() == 2 &&
                                 !settings_.diffTool().isEmpty());
    setWindowTitle("M7Arc — " + active_->location());
}
FilePane* MainWindow::otherPane() const {
    return active_ == left_ ? right_ : left_;
}
bool MainWindow::verifyArchiveSelection(FilePane* pane) {
    if (pane->archiveUnchanged())
        return true;
    QMessageBox::information(this, tr("归档已经更新"),
                             tr("归档在外部发生了修改。正在刷新列表，请重新选择要操作的成员。"));
    openArchive(pane, pane->archivePath());
    return false;
}
void MainWindow::openPath(const QString& path) {
    QFileInfo f(path);
    if (f.isDir())
        active_->navigate(f.absoluteFilePath());
    else
        openArchive(active_, f.absoluteFilePath());
}
void MainWindow::runService(const QString& action, const QStringList& paths) {
    if (paths.isEmpty())
        return;
    if (jobs_->busy()) {
        QMessageBox::information(this, tr("任务仍在运行"), tr("请等待当前任务完成后再次执行服务。"));
        return;
    }
    active_->navigate(QFileInfo(paths.first()).absolutePath());
    serviceInputs_ = paths;
    if (action == "add")
        createArchive();
    else if (action == "extract")
        extract();
    else if (action == "test")
        testArchive();
    serviceInputs_.clear();
}
void MainWindow::openArchive(FilePane* pane, const QString& path, const QString& type, bool nested) {
    workspace_->openArchive(path, type, [this, pane, nested](const app::ArchiveListing& listing) {
        pane->showArchive(listing.path, listing.rows, listing.format, nested, listing.properties,
                          listing.volumes);
        updateActions();
    });
}
void MainWindow::openMember(FilePane* pane, const ArchiveRow& row, const QString& program, bool internal) {
    if (!verifyArchiveSelection(pane))
        return;
    const bool started = workspace_->openMember(
        pane->archivePath(), row, internal, [this, pane, program](const app::Workspace::OpenedMember& opened) {
            if (opened.archive)
                openArchive(pane, opened.localPath, {}, true);
            else
                launchFile(program, {opened.localPath});
        });
    if (!started)
        QMessageBox::warning(this, tr("临时目录不可用"), tr("无法创建临时文件夹。"));
}
void MainWindow::createArchive(const QString& format) {
    auto inputs =
        serviceInputs_.isEmpty() ? active_->selectedFiles() : std::exchange(serviceInputs_, QStringList{});
    QString targetDir = active_->localDirectory();
    const bool updating = active_->inArchive();
    QString target = active_->archivePath();
    if (updating) {
        inputs = QFileDialog::getOpenFileNames(this, tr("选择添加到归档的文件"), targetDir);
        if (inputs.isEmpty())
            return;
    }
    if (inputs.isEmpty()) {
        QMessageBox::information(this, tr("添加到压缩包"), tr("请在文件列表中选择文件或文件夹。"));
        return;
    }
    CreateDialog dialog(inputs, targetDir, this);
    if (!format.isEmpty())
        dialog.setFormat(format);
    else if (updating)
        dialog.setFormat(active_->archiveFormat().toLower());
    if (updating)
        dialog.setDestination(target);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto options = dialog.options();
    if (updating)
        options.archivePrefix = active_->archiveDirectory().toStdString();
    auto path = dialog.destination();
    if (QFileInfo::exists(path) && !updating &&
        QMessageBox::question(this, tr("更新现有归档"),
                              tr("归档已存在，将按所选更新模式修改：\n%1").arg(path)) != QMessageBox::Yes)
        return;
    workspace_->createArchive(inputs, path, options, [this, updating](const OperationResult& r) {
        if (!r.ok())
            return;
        if (updating)
            active_->refresh();
        else {
            left_->refresh();
            right_->refresh();
        }
    });
}
void MainWindow::extract(bool here, bool folder) {
    QStringList paths;
    if (!serviceInputs_.isEmpty())
        paths = std::exchange(serviceInputs_, QStringList{});
    else if (active_->inArchive())
        paths << active_->archivePath();
    else
        paths = active_->selectedFiles();
    if (paths.isEmpty())
        return;
    auto path = paths.first();
    auto target = !right_->isHidden() && !otherPane()->inArchive() ? otherPane()->localDirectory()
                                                                   : QFileInfo(path).absolutePath();
    ExtractOptions options;
    if (!here && !folder) {
        ExtractDialog dialog(path, target, this);
        dialog.setCurrentDirectory(active_->archiveDirectory());
        if (dialog.exec() != QDialog::Accepted)
            return;
        target = dialog.destination();
        options = dialog.options();
    } else {
        target = QFileInfo(path).absolutePath();
        options.createArchiveFolder = folder;
    }
    workspace_->extract(paths, target, active_->selectedArchiveIndexes(), options, [this](const OperationResult&) {
        if (!left_->inArchive())
            left_->refresh();
        if (!right_->inArchive())
            right_->refresh();
    });
}
void MainWindow::testArchive() {
    QStringList paths = !serviceInputs_.isEmpty() ? std::exchange(serviceInputs_, QStringList{})
                        : active_->inArchive()    ? QStringList{active_->archivePath()}
                                                  : active_->selectedFiles();
    if (paths.isEmpty())
        return;
    workspace_->test(paths, active_->selectedArchiveIndexes(), [this](const OperationResult& r) {
        if (r.ok())
            QMessageBox::information(this, tr("测试完成"), tr("所选归档的数据完整性测试通过。"));
    });
}
void MainWindow::copyMove(bool move) {
    if (!right_->isHidden() && (active_->inArchive() || otherPane()->inArchive())) {
        transferToPane(active_, otherPane(), move);
        return;
    }
    if (active_->inArchive()) {
        if (!move) {
            extract();
            return;
        }
        auto* pane = active_;
        if (!workspace_->writable(pane->archivePath()) || !verifyArchiveSelection(pane))
            return;
        const auto target = QFileDialog::getExistingDirectory(this, tr("移动归档成员到"), pane->localDirectory());
        if (target.isEmpty())
            return;
        const auto source = stateOf(pane);
        if (source.indices.isEmpty())
            return;
        app::PaneState destination;
        destination.localDirectory = target;
        workspace_->transfer(source, destination, true, [this](const OperationResult&) { refreshPanes(); });
        return;
    }
    app::PaneState source = stateOf(active_);
    if (source.files.isEmpty())
        return;
    QString target = !right_->isHidden() && !otherPane()->inArchive() ? otherPane()->localDirectory()
                                                                      : active_->localDirectory();
    bool ok;
    target = QInputDialog::getText(this, move ? tr("移动到") : tr("复制到"), tr("目标文件夹"),
                                   QLineEdit::Normal, target, &ok);
    if (!ok || target.isEmpty())
        return;
    app::PaneState destination;
    destination.localDirectory = target;
    workspace_->transfer(source, destination, move, [this](const OperationResult&) {
        left_->refresh();
        right_->refresh();
    });
}
void MainWindow::copyToClipboard() {
    auto* pane = active_;
    const auto state = stateOf(pane);
    if (state.indices.isEmpty() || !verifyArchiveSelection(pane))
        return;
    const bool started = workspace_->copyMembersToClipboard(state, [this](int count) {
        statusBar()->showMessage(tr("已复制 %1 个项目，可在 Finder 或另一归档中粘贴。").arg(count));
    });
    if (!started)
        QMessageBox::warning(this, tr("临时目录不可用"), tr("无法创建临时文件夹。"));
}
void MainWindow::pasteFromClipboard() {
    if (workspace_->clipboardFiles().isEmpty() || !verifyArchiveSelection(active_))
        return;
    workspace_->pasteFromClipboard(stateOf(active_), [this](const OperationResult&) { refreshPanes(); });
}
void MainWindow::refreshPanes() {
    QList<FilePane*> shown;
    QStringList archives;
    for (auto* pane : {left_, right_}) {
        if (pane->inArchive()) {
            shown.append(pane);
            archives.append(pane->archivePath());
        } else
            pane->refresh();
    }
    if (archives.isEmpty())
        return;
    workspace_->reloadArchives(archives, [this, shown](const QList<app::ArchiveListing>& listings) {
        for (qsizetype i = 0; i < listings.size(); ++i)
            if (shown[i]->archivePath() == listings[i].path)
                shown[i]->showArchive(listings[i].path, listings[i].rows, listings[i].format, false,
                                      listings[i].properties, listings[i].volumes);
        updateActions();
    });
}
void MainWindow::removeSelection() {
    const bool archive = active_->inArchive();
    const int count = archive ? active_->selectedArchiveRows().size() : active_->selectedFiles().size();
    if (!count)
        return;
    if (QMessageBox::question(this, tr("删除所选项目"),
                              archive ? tr("从归档中删除 %1 个所选项目？").arg(count)
                                      : tr("将 %1 个所选项目移到废纸篓？").arg(count)) != QMessageBox::Yes)
        return;
    auto* pane = active_;
    workspace_->removeSelection(stateOf(pane), [pane](const OperationResult&) { pane->refresh(); });
}
void MainWindow::renameSelection() {
    auto files = active_->selectedFiles();
    auto archiveRows = active_->selectedArchiveRows();
    if ((active_->inArchive() ? archiveRows.size() : files.size()) != 1)
        return;
    auto oldName = active_->inArchive() ? archiveRows.first().path : files.first();
    bool ok;
    auto name = QInputDialog::getText(this, tr("重命名"), tr("新名称"), QLineEdit::Normal,
                                      QFileInfo(oldName).fileName(), &ok);
    if (!ok || name.isEmpty() || name.contains('/'))
        return;
    auto* pane = active_;
    if (pane->inArchive()) {
        workspace_->renameMember(stateOf(pane), archiveRows.first(), name,
                                 [pane](const OperationResult&) { pane->refresh(); });
        return;
    }
    if (!workspace_->renameLocal(oldName, name))
        QMessageBox::warning(this, tr("重命名失败"), tr("目标可能已经存在，或文件不可写。"));
    pane->refresh();
}
void MainWindow::createItem(bool directory) {
    bool ok;
    auto name = QInputDialog::getText(this, directory ? tr("新建文件夹") : tr("新建文件"), tr("名称"),
                                      QLineEdit::Normal, {}, &ok);
    if (!ok || name.isEmpty() || name == "." || name == ".." || name.contains('/'))
        return;
    auto* pane = active_;
    if (pane->inArchive()) {
        if (!workspace_->writable(pane->archivePath()) || !verifyArchiveSelection(pane))
            return;
        if (!app::Workspace::memberNameFree(pane->archiveModel()->allEntries(), pane->archiveDirectory(), name)) {
            QMessageBox::warning(this, tr("创建失败"), tr("归档中已存在同名项目。"));
            return;
        }
        workspace_->createArchiveItem(stateOf(pane), name, directory, [pane](const OperationResult& result) {
            if (result.ok())
                pane->refresh();
        });
        return;
    }
    if (!workspace_->createLocalItem(pane->localDirectory(), name, directory))
        QMessageBox::warning(this, tr("创建失败"), tr("目标已存在或目录不可写。"));
    pane->refresh();
}
void MainWindow::createLink() {
    auto files = active_->selectedFiles();
    if (files.size() != 1)
        return;
    bool ok;
    auto type = QInputDialog::getItem(this, tr("创建链接"), tr("类型"), {tr("符号链接"), tr("硬链接（文件）")}, 0,
                                      false, &ok);
    if (!ok)
        return;
    auto dest = QFileDialog::getSaveFileName(this, tr("链接位置"), files.first() + " link");
    if (dest.isEmpty())
        return;
    if (!workspace_->createLink(files.first(), dest, type.startsWith(tr("符号"))))
        QMessageBox::warning(this, tr("创建链接失败"), tr("目标已存在或文件系统不支持此链接。"));
    active_->refresh();
}
void MainWindow::editComment() {
    auto* pane = active_;
    const auto selected = pane->selectedArchiveRows();
    if (selected.size() != 1 || !workspace_->commentWritable(pane->archivePath()) || !verifyArchiveSelection(pane))
        return;
    const auto entry = selected.first();
    bool accepted = false;
    const auto comment = QInputDialog::getMultiLineText(
        this, tr("编辑归档成员注释"), entry.path, entry.properties.value("Comment").toString(), &accepted);
    if (!accepted || !verifyArchiveSelection(pane))
        return;
    workspace_->setComment(pane->archivePath(), entry, comment, [pane](const OperationResult& result) {
        if (result.ok())
            pane->refresh();
    });
}
void MainWindow::properties() {
    QString text;
    if (active_->inArchive()) {
        const auto containers = active_->archiveContainers();
        for (qsizetype level = 0; level < containers.size(); ++level) {
            const auto& container = containers[level];
            text += tr("归档层 %1：%2\n格式：%3\n可更新：%4\n")
                        .arg(level + 1)
                        .arg(container.path, container.format,
                             workspace_->writable(container.path) ? tr("是") : tr("否"));
            for (const auto& property : container.properties)
                text += QString("%1 [%2]: %3\n").arg(property.name).arg(property.id).arg(property.value);
            if (!container.volumes.isEmpty())
                text += tr("分卷：\n%1\n").arg(container.volumes.join('\n'));
            text += '\n';
        }
        for (const auto& r : active_->selectedArchiveRows()) {
            text += r.path + "\n" + tr("大小：%1\n压缩后：%2\n").arg(r.size).arg(r.packedSize);
            for (const auto& property : r.rawProperties)
                text += QString("%1 [%2]: %3\n").arg(property.name).arg(property.id).arg(property.value);
            text += '\n';
        }
    } else
        for (const auto& p : active_->selectedFiles()) {
            QFileInfo f(p);
            text += tr("名称：%1\n路径：%2\n大小：%3 字节\n修改：%4\n权限：%5\n\n")
                        .arg(f.fileName(), p)
                        .arg(f.size())
                        .arg(f.lastModified().toString(Qt::ISODate))
                        .arg(uint(f.permissions()), 0, 16);
        }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("信息"));
    dialog.resize(670, 480);
    auto* l = new QVBoxLayout(&dialog);
    auto* t = new QPlainTextEdit(text, &dialog);
    t->setReadOnly(true);
    l->addWidget(t);
    auto* b = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    l->addWidget(b);
    connect(b, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}
void MainWindow::hashes(const QStringList& algorithms) {
    if (algorithms.isEmpty())
        return;
    const bool archived = active_->inArchive();
    const auto state = stateOf(active_);
    if ((!archived && state.files.isEmpty()) || (archived && !verifyArchiveSelection(active_)))
        return;
    if (archived && state.indices.isEmpty())
        return;
    auto summary = std::make_shared<HashSummary>();
    const bool started = workspace_->hash(state, algorithms, summary, [this, summary](const OperationResult& result) {
        if (result.ok())
            showHashResults(this, *summary);
    });
    if (!started)
        QMessageBox::warning(this, tr("临时目录不可用"), tr("无法创建临时文件夹。"));
}
void MainWindow::selectMask(bool on, bool byType) {
    QString mask = "*";
    if (byType) {
        auto i = active_->view()->currentIndex();
        mask = "*." + QFileInfo(i.siblingAtColumn(0).data().toString()).suffix();
    } else {
        bool ok;
        mask = QInputDialog::getText(this, on ? tr("选择") : tr("取消选择"), tr("通配符，例如 *.zip"),
                                     QLineEdit::Normal, mask, &ok);
        if (!ok)
            return;
    }
    active_->selectMask(mask, on);
}
void MainWindow::splitFile() {
    auto inputs = active_->selectedFiles();
    if (inputs.size() != 1 || !QFileInfo(inputs.first()).isFile())
        return;
    bool ok;
    auto sizeText = QInputDialog::getText(this, tr("分割文件"), tr("分卷大小（例如 100M）"),
                                          QLineEdit::Normal, "100M", &ok);
    if (!ok)
        return;
    const auto volume = app::parseVolumeSize(sizeText);
    if (!volume) {
        QMessageBox::warning(this, tr("大小无效"), tr("请输入正整数，支持 K/M/G/T 后缀。"));
        return;
    }
    if (*volume >= quint64(QFileInfo(inputs.first()).size())) {
        QMessageBox::information(this, tr("无需分割"), tr("分卷大小不小于文件大小。"));
        return;
    }
    auto directory = QFileDialog::getExistingDirectory(this, tr("分卷输出目录"), active_->localDirectory());
    if (directory.isEmpty())
        return;
    workspace_->splitFile(inputs.first(), directory, *volume, [this](const OperationResult&) {
        left_->refresh();
        right_->refresh();
    });
}
void MainWindow::combineFiles() {
    auto files = active_->selectedFiles();
    if (files.size() != 1)
        return;
    QFileInfo first(files.first());
    if (first.suffix() != "001") {
        QMessageBox::information(this, tr("合并文件"), tr("请选择 .001 首分卷。"));
        return;
    }
    const auto parts = app::volumeParts(first.absoluteFilePath());
    if (parts.size() < 2) {
        QMessageBox::information(this, tr("合并文件"), tr("至少需要两个连续分卷。"));
        return;
    }
    auto dest = QFileDialog::getSaveFileName(this, tr("合并 %1 个分卷").arg(parts.size()),
                                             app::joinedName(first.absoluteFilePath()));
    if (dest.isEmpty())
        return;
    if (parts.contains(dest)) {
        QMessageBox::warning(this, tr("输出位置无效"), tr("合并输出不能覆盖源分卷。"));
        return;
    }
    workspace_->combineFiles(parts, dest, [this](const OperationResult&) {
        left_->refresh();
        right_->refresh();
    });
}
void MainWindow::openCommand(const QString& id) {
    if (active_->inArchive()) {
        auto selected = active_->selectedArchiveRows();
        if (!selected.isEmpty()) {
            auto row = selected.first();
            if (row.directory)
                active_->navigate(active_->archivePath() + "/" + row.path);
            else
                openMember(active_, row,
                           id == "view-file"   ? settings_.viewer()
                           : id == "edit-file" ? settings_.editor()
                                               : QString{},
                           id == "open" || id == "open-inside" || id.startsWith("open-as:"));
        }
        return;
    }
    auto files = active_->selectedFiles();
    if (files.isEmpty())
        return;
    auto path = files.first();
    if (QFileInfo(path).isDir()) {
        active_->navigate(path);
        return;
    }
    if (id == "open-inside" || id.startsWith("open-as:") || (id == "open" && app::isArchiveName(path)))
        openArchive(active_, path, id.startsWith("open-as:") ? id.mid(8) : QString{});
    else
        launchFile(id == "edit-file"   ? settings_.editor()
                   : id == "view-file" ? settings_.viewer()
                                       : QString{},
                   {path});
}
void MainWindow::showStreams() {
    if (active_->inArchive()) {
        if (!verifyArchiveSelection(active_))
            return;
        const auto selected = active_->selectedArchiveRows();
        QList<ArchiveRow> streams;
        for (const auto& entry : active_->archiveModel()->allEntries()) {
            if (!entry.alternateStream)
                continue;
            bool include = selected.isEmpty();
            for (const auto& owner : selected)
                include |= entry.index == owner.index || entry.path.startsWith(owner.path + ':') ||
                           (owner.directory && entry.path.startsWith(owner.path + '/'));
            if (include)
                streams.append(entry);
        }
        showArchiveStreams(this, active_->archivePath(), streams, jobs_);
    } else {
        const auto files = active_->selectedFiles();
        if (files.size() == 1)
            showFileMetadata(this, files.first(), jobs_);
    }
}
void MainWindow::showTemporaryFiles() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("临时文件"));
    auto* l = new QVBoxLayout(&dialog);
    const auto dirs = workspace_->temp().paths();
    auto* text = new QPlainTextEdit(dirs.isEmpty() ? tr("本次会话没有临时文件。") : dirs.join('\n'), &dialog);
    text->setReadOnly(true);
    l->addWidget(text);
    auto* b = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    l->addWidget(b);
    connect(b, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(650, 300);
    dialog.exec();
}
void MainWindow::registerCommands() {
    commands_.setGuard([this](const QString& id) {
        static const QSet<QString> needsFreshArchive{"delete", "rename", "extract", "extract-here",
                                                     "extract-folder", "test", "copy", "move"};
        return !(active_->inArchive() && needsFreshArchive.contains(id) && !verifyArchiveSelection(active_));
    });
    auto& c = commands_;
    c.add("add", [this] { createArchive(); });
    c.add("add-7z", [this] { createArchive("7z"); });
    c.add("add-zip", [this] { createArchive("zip"); });
    c.add("extract", [this] { extract(); });
    c.add("extract-here", [this] { extract(true, false); });
    c.add("extract-folder", [this] { extract(false, true); });
    c.add("test", [this] { testArchive(); });
    c.add("copy", [this] { copyMove(false); });
    c.add("move", [this] { copyMove(true); });
    c.add("delete", [this] { removeSelection(); });
    c.add("rename", [this] { renameSelection(); });
    c.add("create-file", [this] { createItem(false); });
    c.add("create-folder", [this] { createItem(true); });
    c.add("info", [this] { properties(); });
    c.add("formats", [this] { showFormatCatalog(this); });
    c.add("sfx", [this] {
        showSfxCreation(this, active_->selectedFiles(), active_->localDirectory(), jobs_, [this] { refreshPanes(); });
    });
    c.add("edit-save", [this] { checkEditedFiles(); });
    c.add("options", [this] {
        showSettings(this);
        applySettings();
        updateActions();
    });
    c.add("two-panels", [this] {
        right_->setVisible(actions_["two-panels"]->isChecked());
        if (right_->isHidden())
            active_ = left_;
        settings_.setDualPane(!right_->isHidden());
        updateActions();
    });
    c.add("root", [this] { active_->navigate(QDir::rootPath()); });
    c.add("up", [this] {
        checkEditedFiles();
        if (!jobs_->busy())
            active_->up();
    });
    c.add("refresh", [this] { active_->refresh(); });
    c.add("address", [this] {
        bool ok;
        auto p = QInputDialog::getText(this, tr("转到目录"), tr("本地路径"), QLineEdit::Normal,
                                       active_->localDirectory(), &ok);
        if (ok)
            active_->navigate(p);
    });
    c.add("flat", [this] { active_->setFlat(actions_["flat"]->isChecked()); });
    c.add("show-hidden", [this] {
        settings_.setShowHidden(actions_["show-hidden"]->isChecked());
        left_->applySettings();
        right_->applySettings();
    });
    c.add("select-all", [this] { active_->selectAll(); });
    c.add("deselect-all", [this] { active_->view()->clearSelection(); });
    c.add("invert-selection", [this] { active_->invertSelection(); });
    for (const auto& id : QStringList{"select-mask", "deselect-mask", "select-type", "deselect-type"})
        c.add(id, [this, id] { selectMask(id.startsWith("select"), id.endsWith("type")); });
    c.add("toolbar-text", [this] {
        toolbar_->setToolButtonStyle(actions_["toolbar-text"]->isChecked() ? Qt::ToolButtonTextUnderIcon
                                                                           : Qt::ToolButtonIconOnly);
    });
    c.add("toolbar-large", [this] {
        toolbar_->setIconSize(actions_["toolbar-large"]->isChecked() ? QSize(28, 28) : QSize(18, 18));
    });
    c.addPrefix("view-", [this](const QString& mode) { active_->setViewMode(mode); });
    c.addPrefix("sort-", [this](const QString& choice) {
        int column = choice.toInt();
        if (active_->inArchive()) {
            if (column == 2)
                column = 3;
            else if (column == 3)
                column = 4;
        }
        active_->view()->sortByColumn(column, Qt::AscendingOrder);
    });
    c.addPrefix("favorite-set:", [this](const QString& slot) {
        settings_.setFavorite(slot.toInt(), active_->location());
        actions_["favorite:" + slot]->setText(slot + " — " + active_->location());
    });
    c.addPrefix("favorite:", [this](const QString& slot) {
        const auto path = settings_.favorite(slot.toInt());
        if (!path.isEmpty())
            openPath(path);
    });
    c.add("verify-manifest", [this] {
        const auto selected = active_->selectedFiles();
        auto path = selected.size() == 1 ? selected.first() : QString{};
        if (path.isEmpty())
            path = QFileDialog::getOpenFileName(this, tr("选择摘要清单"), active_->location());
        if (!path.isEmpty())
            verifyHashManifest(this, jobs_, path);
    });
    c.addPrefix("hash:", [this](const QString& name) {
        QStringList algorithms;
        if (name == "*" || name == "choose") {
            ArchiveEngine e;
            for (const auto& h : e.hashers())
                algorithms << QString::fromStdString(h.name);
            if (name == "choose")
                algorithms = chooseHashAlgorithms(this, algorithms);
        } else
            algorithms << name;
        hashes(algorithms);
    });
    c.add("split", [this] { splitFile(); });
    c.add("combine", [this] { combineFiles(); });
    c.add("clipboard-copy", [this] {
        if (active_->inArchive())
            copyToClipboard();
        else
            workspace_->copyFilesToClipboard(active_->selectedFiles(), false);
    });
    c.add("clipboard-cut", [this] {
        if (!active_->inArchive())
            workspace_->copyFilesToClipboard(active_->selectedFiles(), true);
    });
    c.add("clipboard-paste", [this] { pasteFromClipboard(); });
    c.add("link", [this] { createLink(); });
    c.add("diff", [this] {
        auto files = active_->selectedFiles();
        if (files.size() == 2)
            launchFile(settings_.diffTool(), files);
    });
    for (const auto& id : QStringList{"open", "open-inside", "open-outside", "view-file", "edit-file"})
        c.add(id, [this, id] { openCommand(id); });
    c.addPrefix("open-as:", [this](const QString& type) { openCommand("open-as:" + type); });
    c.add("benchmark", [this] { showBenchmark(this, jobs_); });
    c.add("comment", [this] { editComment(); });
    c.add("streams", [this] { showStreams(); });
    c.add("temp-cleanup", [this] { showTemporaryFiles(); });
    c.add("help", [this] { showHelp(this); });
    c.add("about", [this] {
        QMessageBox::about(
            this, tr("关于 M7Arc"),
            tr("M7Arc %1\n\n面向 macOS 的开源归档与文件管理器\n7-Zip 引擎 %2\nQt %3\n\n7-Zip © Igor "
               "Pavlov\n第三方许可随应用提供。\n当前发行包未经 Developer ID 签名和公证。")
                .arg(QStringLiteral(M7ARC_VERSION), QString::fromStdString(ArchiveEngine::version()),
                     QString::fromLatin1(qVersion())));
    });
    c.add("exit", [this] { close(); });
}
void MainWindow::checkEditedFiles() {
    if (checkingEdits_ || jobs_->busy() || QApplication::activeModalWidget())
        return;
    auto session = workspace_->edits().firstChanged();
    if (!session)
        return;
    checkingEdits_ = true;
    QMessageBox prompt(QMessageBox::Question, tr("归档内文件已经修改"),
                       tr("%1\n\n保存到 %2？").arg(session->memberPath, session->archivePath),
                       QMessageBox::NoButton, this);
    auto* save = prompt.addButton(tr("保存回归档"), QMessageBox::AcceptRole);
    auto* discard = prompt.addButton(tr("放弃修改"), QMessageBox::DestructiveRole);
    prompt.addButton(QMessageBox::Cancel);
    prompt.exec();
    if (prompt.clickedButton() != save && prompt.clickedButton() != discard) {
        checkingEdits_ = false;
        return;
    }
    workspace_->settleEdit(session, prompt.clickedButton() == save, [this](const OperationResult& r) {
        checkingEdits_ = false;
        if (r.ok() && active_->inArchive())
            active_->refresh();
    });
}
void MainWindow::transferToPane(FilePane* source, FilePane* target, bool move, bool confirm) {
    if (!verifyArchiveSelection(source) || !verifyArchiveSelection(target))
        return;
    const auto from = stateOf(source), to = stateOf(target);
    if (from.inArchive() ? from.indices.isEmpty() : from.files.isEmpty())
        return;
    if (to.inArchive() && !workspace_->writable(to.archivePath)) {
        QMessageBox::information(this, tr("目标归档只读"), tr("此归档可以浏览和提取，无法写入。"));
        return;
    }
    if (move && from.inArchive() && !workspace_->writable(from.archivePath))
        return;
    if (from.inArchive() && to.inArchive() && from.archivePath == to.archivePath) {
        QMessageBox::information(this, tr("同一归档"), tr("请使用重命名调整成员路径，或将文件复制到另一归档。"));
        return;
    }
    if (confirm && QMessageBox::question(this, move ? tr("移动所选项目") : tr("复制所选项目"),
                                         tr("目标：%1").arg(target->location())) != QMessageBox::Yes)
        return;
    workspace_->transfer(from, to, move, [this](const OperationResult& r) {
        if (r.ok()) {
            refreshPanes();
            QTimer::singleShot(300, this, &MainWindow::checkEditedFiles);
        }
    });
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (workspace_->edits().anyChanged()) {
        checkEditedFiles();
        event->ignore();
        return;
    }
    if (jobs_->busy()) {
        jobs_->showProgress();
        QMessageBox::information(this, tr("任务仍在运行"), tr("请等待任务完成，或取消任务后退出。"));
        event->ignore();
        return;
    }
    settings_.setWindowGeometry(saveGeometry());
    settings_.setRightPanePath(right_->localDirectory());
    settings_.setDualPane(!right_->isHidden());
    event->accept();
}
} // namespace m7arc::ui
