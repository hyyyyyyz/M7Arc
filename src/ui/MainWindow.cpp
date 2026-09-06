#include "MainWindow.h"
#include "ArchiveDialogs.h"
#include "BenchmarkDialog.h"
#include "FileMetadataDialog.h"
#include "FilePane.h"
#include "HelpDialog.h"
#include "HashDialog.h"
#include "OperationController.h"
#include "SfxDialog.h"
#include "platform/MacMetadata.h"
#include "platform/drag/MacFilePromises.h"
#include "tools/SfxBuilder.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <unistd.h>
#include <utility>
namespace m7arc::ui {
namespace {
std::vector<std::string> strings(const QStringList& s) {
    std::vector<std::string> r;
    for (const auto& x : s)
        r.push_back(x.toStdString());
    return r;
}
std::vector<std::uint32_t> indices(const QList<quint32>& s) {
    return {s.begin(), s.end()};
}
QList<ArchiveProperty> properties(const std::vector<Property>& values) {
    QList<ArchiveProperty> result;
    for (const auto& property : values)
        result.append(
            {property.id, QString::fromStdString(property.name), QString::fromStdString(property.value)});
    return result;
}
QStringList strings(const std::vector<std::string>& values) {
    QStringList result;
    for (const auto& value : values)
        result.append(QString::fromStdString(value));
    return result;
}
QList<ArchiveRow> rows(const ArchiveSnapshot& snapshot) {
    QList<ArchiveRow> r;
    for (const auto& e : snapshot.entries) {
        ArchiveRow item;
        item.index = e.index;
        item.path = QString::fromStdString(e.path);
        item.size = e.size;
        item.packedSize = e.packedSize;
        item.directory = e.isDirectory;
        item.encrypted = e.encrypted;
        item.alternateStream = e.alternateStream;
        if (e.modifiedUnixSeconds)
            item.modified = QDateTime::fromSecsSinceEpoch(e.modifiedUnixSeconds);
        item.method = QString::fromStdString(e.method);
        item.rawProperties = properties(e.properties);
        for (const auto& p : e.properties)
            item.properties[QString::fromStdString(p.name)] = QString::fromStdString(p.value);
        item.properties["Attributes"] = QString::fromStdString(e.attributes);
        r.append(item);
    }
    return r;
}
void launchFile(const QString& program, const QStringList& paths) {
    if (program.isEmpty()) {
        for (const auto& p : paths)
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    } else if (program.endsWith(".app", Qt::CaseInsensitive))
        QProcess::startDetached("/usr/bin/open", QStringList{"-a", program} + paths);
    else
        QProcess::startDetached(program, paths);
}
bool copyPath(const QString& src, const QString& dst, const Callbacks& cb, OperationResult& result) {
    if (cb.cancelled && cb.cancelled()) {
        result.cancelled = true;
        return false;
    }
    QTemporaryDir stage(QFileInfo(dst).absolutePath() + "/.m7arc-copy-XXXXXX");
    if (!stage.isValid()) {
        result.errors.push_back(QObject::tr("无法创建复制工作目录：%1").arg(dst).toStdString());
        return false;
    }
    const auto pending = QDir(stage.path()).filePath("item");
    try {
        mac::copyItem(src.toStdString(), pending.toStdString(), [&](std::uint64_t completed) {
            if (cb.progress)
                return cb.progress({"复制", src.toStdString(), completed, quint64(QFileInfo(src).size()),
                                    !QFileInfo(src).isDir()});
            return !cb.cancelled || !cb.cancelled();
        });
    } catch (const std::exception& e) {
        result.cancelled = cb.cancelled && cb.cancelled();
        if (!result.cancelled)
            result.errors.push_back(e.what());
        return false;
    }
    const auto backup = QDir(stage.path()).filePath("previous");
    const bool exists = QFileInfo::exists(dst) || QFileInfo(dst).isSymLink();
    if (exists && !QDir().rename(dst, backup)) {
        result.errors.push_back(QObject::tr("无法替换目标：%1").arg(dst).toStdString());
        return false;
    }
    if (!QDir().rename(pending, dst)) {
        if (exists && !QDir().rename(backup, dst)) {
            stage.setAutoRemove(false);
            result.errors.push_back(QObject::tr("原文件保留在：%1").arg(backup).toStdString());
        }
        result.errors.push_back(QObject::tr("无法完成复制：%1").arg(dst).toStdString());
        return false;
    }
    ++result.completedItems;
    return true;
}
bool removePath(const QString& p) {
    QFileInfo f(p);
    return f.isDir() && !f.isSymLink() ? QDir(p).removeRecursively() : QFile::remove(p);
}
QString renamedPath(const QString& path) {
    QFileInfo f(path);
    for (int n = 1;; ++n) {
        const auto candidate = f.dir().filePath(f.completeBaseName() + "_" + QString::number(n) +
                                                (f.suffix().isEmpty() ? QString{} : "." + f.suffix()));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
}
OperationResult transfer(const QStringList& sources, const QString& directory, bool move,
                         const Callbacks& cb) {
    OperationResult result;
    if (!QDir().mkpath(directory)) {
        result.errors.push_back(QObject::tr("无法创建目标目录：%1").arg(directory).toStdString());
        return result;
    }
    bool all = false, skipAll = false;
    for (const auto& src : sources) {
        QFileInfo info(src);
        QString dest = QDir(directory).filePath(info.fileName());
        if (QDir::cleanPath(dest) == QDir::cleanPath(src)) {
            result.errors.push_back(QObject::tr("源和目标相同：%1").arg(src).toStdString());
            continue;
        }
        if (info.isDir() && QDir::cleanPath(dest).startsWith(QDir::cleanPath(src) + "/")) {
            result.errors.push_back(QObject::tr("目标不能位于源文件夹内：%1").arg(dest).toStdString());
            continue;
        }
        if (QFileInfo::exists(dest)) {
            if (skipAll)
                continue;
            auto answer = all ? OverwriteAnswer::Replace
                              : cb.overwrite({dest.toStdString(), src.toStdString(),
                                              quint64(QFileInfo(dest).size()), quint64(info.size())});
            if (answer == OverwriteAnswer::Cancel) {
                result.cancelled = true;
                break;
            }
            if (answer == OverwriteAnswer::Skip)
                continue;
            if (answer == OverwriteAnswer::SkipAll) {
                skipAll = true;
                continue;
            }
            if (answer == OverwriteAnswer::ReplaceAll)
                all = true;
            if (answer == OverwriteAnswer::RenameIncoming)
                dest = renamedPath(dest);
        }
        if (move && !QFileInfo::exists(dest) && QDir().rename(src, dest)) {
            ++result.completedItems;
            continue;
        }
        if (copyPath(src, dest, cb, result) && move && !removePath(src))
            result.errors.push_back(QObject::tr("已复制，但无法移除源：%1").arg(src).toStdString());
        if (result.cancelled)
            break;
    }
    return result;
}
} // namespace
void MainWindow::dragArchiveMembers(FilePane* pane, QWidget* source) {
    if (jobs_->busy() || !verifyArchiveSelection(pane))
        return;
    const auto selected = pane->view()->selectionModel()->selectedRows();
    auto* model = pane->archiveModel();
    QList<platform::ArchivePromiseItem> items;
    for (const auto& index : selected) {
        const auto& entry = model->entry(index.row());
        bool contained = false;
        for (const auto& other : selected) {
            const auto& parent = model->entry(other.row());
            const auto prefix = parent.path.endsWith('/') ? parent.path : parent.path + '/';
            if (other != index && parent.directory && entry.path.startsWith(prefix)) {
                contained = true;
                break;
            }
        }
        if (!contained) {
            const auto path = entry.path.endsWith('/') ? entry.path.chopped(1) : entry.path;
            items.append({QFileInfo(path).fileName(), entry.directory, pane->archivePath(), entry.path,
                          model->indexesForRows({index})});
        }
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
      jobs_(new OperationController(this)), splitter_(new QSplitter(this)) {
    setWindowTitle("M7Arc");
    setMinimumSize(760, 520);
    resize(1120, 720);
    setUnifiedTitleAndToolBarOnMac(true);
    splitter_->addWidget(left_);
    splitter_->addWidget(right_);
    setCentralWidget(splitter_);
    active_ = left_;
    QSettings settings;
    right_->setVisible(settings.value("view/dual", false).toBool());
    left_->navigate(settings.value("startup/path", QDir::homePath()).toString());
    right_->navigate(settings.value("pane/right", QDir::homePath()).toString());
    for (auto* pane : {left_, right_}) {
        pane->setLeaveGuard([this] {
            if (std::any_of(edits_.begin(), edits_.end(),
                            [](const auto& session) { return session->changed(); })) {
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
            if (isArchiveName(path))
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
            const auto archive = pane->archivePath(), prefix = pane->archiveDirectory(),
                       format = pane->archiveFormat().toLower(), directory = pane->localDirectory();
            if (pane->inArchive() && !writable_.value(archive))
                return;
            jobs_->run(
                move ? tr("移动项目") : tr("复制项目"),
                [paths, move, archive, prefix, format, directory](ArchiveEngine& e, const Callbacks& cb) {
                    if (archive.isEmpty())
                        return transfer(paths, directory, move, cb);
                    CreateOptions options;
                    options.format = format.toStdString();
                    options.archivePrefix = prefix.toStdString();
                    auto result = e.create(strings(paths), archive.toStdString(), options, cb);
                    if (result.ok() && move)
                        for (const auto& p : paths)
                            if (!removePath(p))
                                result.errors.push_back(QObject::tr("无法删除源：%1").arg(p).toStdString());
                    return result;
                },
                [this, pane](const OperationResult&) { pane->refresh(); });
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
    toolbar_ = addToolBar(tr("操作"));
    toolbar_->setObjectName("MainToolbar");
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(28, 28));
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    const QList<QPair<QString, QStyle::StandardPixmap>> icons = {
        {"add", QStyle::SP_FileDialogNewFolder}, {"extract", QStyle::SP_ArrowDown},
        {"test", QStyle::SP_DialogApplyButton},  {"copy", QStyle::SP_FileDialogContentsView},
        {"move", QStyle::SP_ArrowForward},       {"delete", QStyle::SP_TrashIcon},
        {"info", QStyle::SP_FileDialogInfoView}};
    for (int n = 0; n < icons.size(); ++n) {
        if (n == 3)
            toolbar_->addSeparator();
        auto* a = actions_[icons[n].first];
        a->setIcon(style()->standardIcon(icons[n].second));
        toolbar_->addAction(a);
    }
    auto* space = new QWidget(toolbar_);
    space->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(space);
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
    if (settings.value("window/restore", true).toBool() && settings.contains("window/geometry"))
        restoreGeometry(settings.value("window/geometry").toByteArray());
    updateActions();
}
QAction* MainWindow::command(const QString& id, const QString& text, const QKeySequence& shortcut,
                             bool checkable) {
    if (actions_.contains(id))
        return actions_[id];
    auto* a = new QAction(text, this);
    a->setObjectName(id);
    if (!shortcut.isEmpty())
        a->setShortcut(shortcut);
    a->setCheckable(checkable);
    actions_[id] = a;
    connect(a, &QAction::triggered, this, [this, id] { dispatch(id); });
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
        ->setChecked(QSettings().value("view/hidden", false).toBool());
    favorites_ = menuBar()->addMenu(tr("收藏夹"));
    auto* set = favorites_->addMenu(tr("设置书签"));
    for (int i = 0; i < 10; ++i)
        add(set, "favorite-set:" + QString::number(i), tr("槽位 %1").arg(i));
    for (int i = 0; i < 10; ++i)
        add(favorites_, "favorite:" + QString::number(i),
            tr("%1 — %2").arg(i).arg(
                QSettings().value("favorites/" + QString::number(i), tr("未设置")).toString()));
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
    QSettings settings;
    left_->applySettings();
    right_->applySettings();
    for (const auto& id : QStringList{"add", "extract", "extract-here", "extract-folder", "test", "copy",
                                      "move", "delete", "info"})
        actions_[id]->setVisible(settings.value("menus/" + id, true).toBool());
}
void MainWindow::updateActions() {
    if (actions_.isEmpty())
        return;
    const bool archive = active_->inArchive();
    const bool chosen =
        archive ? !active_->selectedArchiveRows().isEmpty() : !active_->selectedFiles().isEmpty();
    const bool local = !archive;
    const bool busy = jobs_->busy();
    const bool write = local || writable_.value(active_->archivePath());
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
                                    commentWritable_.value(active_->archivePath()));
    actions_["info"]->setEnabled(chosen || archive);
    actions_["add"]->setEnabled(!busy && write);
    actions_["add-7z"]->setEnabled(!busy && local && chosen);
    actions_["add-zip"]->setEnabled(!busy && local && chosen);
    actions_["sfx"]->setEnabled(!busy && local && chosen);
    actions_["diff"]->setEnabled(local && active_->selectedFiles().size() == 2 &&
                                 !QSettings().value("tools/diff").toString().isEmpty());
    setWindowTitle("M7Arc — " + active_->location());
}
FilePane* MainWindow::otherPane() const {
    return active_ == left_ ? right_ : left_;
}
bool MainWindow::isArchiveName(const QString& file) const {
    const auto suffix = QFileInfo(file).suffix();
    for (const auto& f : formatCatalog()["formats"].toArray())
        for (const auto& ext : f.toObject()["extensions"].toArray())
            if (ext.toString().compare(suffix, Qt::CaseInsensitive) == 0)
                return true;
    return suffix == "001";
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
    auto snapshot = std::make_shared<ArchiveSnapshot>();
    OpenOptions options;
    options.type = type.toStdString();
    jobs_->run(
        tr("打开归档"),
        [path, options, snapshot](ArchiveEngine& e, const Callbacks& cb) {
            *snapshot = e.list(path.toStdString(), options, cb);
            return OperationResult{};
        },
        [this, pane, path, snapshot, nested](const OperationResult& r) {
            if (r.ok()) {
                writable_[path] = snapshot->canUpdate;
                commentWritable_[path] = snapshot->canSetComment;
                pane->showArchive(path, rows(*snapshot), QString::fromStdString(snapshot->format), nested,
                                  m7arc::ui::properties(snapshot->properties), strings(snapshot->volumes));
                updateActions();
            }
        },
        true);
}
void MainWindow::openMember(FilePane* pane, const ArchiveRow& row, const QString& program, bool internal) {
    if (!verifyArchiveSelection(pane))
        return;
    auto temp = std::make_shared<QTemporaryDir>(
        QSettings().value("folders/temp", QDir::tempPath()).toString() + "/M7Arc-XXXXXX");
    if (!temp->isValid()) {
        QMessageBox::warning(this, tr("临时目录不可用"), tr("无法创建临时文件夹。"));
        return;
    }
    temporary_.append(temp);
    const auto archive = pane->archivePath();
    const auto path = QDir(temp->path()).filePath(row.path);
    jobs_->run(
        tr("打开归档成员"),
        [archive, temp, row](ArchiveEngine& e, const Callbacks& cb) {
            return e.extract(archive.toStdString(), temp->path().toStdString(), {row.index}, {}, cb);
        },
        [this, pane, path, archive, row, program, internal](const OperationResult& r) {
            if (!r.ok())
                return;
            auto session = std::make_shared<EditSession>();
            session->archivePath = archive;
            session->memberPath = row.path;
            session->localPath = path;
            session->index = row.index;
            session->rememberArchive();
            session->nestedArchive = internal && isArchiveName(path);
            session->remember();
            edits_.append(session);
            if (session->nestedArchive)
                openArchive(pane, path, {}, true);
            else
                launchFile(program, {path});
        },
        true);
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
    jobs_->run(
        tr("创建 / 更新归档"),
        [inputs, path, options](ArchiveEngine& e, const Callbacks& cb) {
            return e.create(strings(inputs), path.toStdString(), options, cb);
        },
        [this, updating](const OperationResult& r) {
            if (r.ok()) {
                if (updating)
                    active_->refresh();
                else {
                    left_->refresh();
                    right_->refresh();
                }
            }
        });
}
void MainWindow::extract(bool here, bool folder) {
    QStringList paths;
    auto selected = active_->selectedArchiveIndexes();
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
    auto ids = indices(selected);
    jobs_->run(
        tr("提取文件"),
        [paths, target, ids, options](ArchiveEngine& e, const Callbacks& cb) {
            OperationResult result;
            for (const auto& p : paths) {
                auto r = e.extract(p.toStdString(), target.toStdString(), ids, options, cb);
                result.errors.insert(result.errors.end(), r.errors.begin(), r.errors.end());
                result.cancelled |= r.cancelled;
                result.passwordMayBeIncorrect |= r.passwordMayBeIncorrect;
                result.completedItems += r.completedItems;
                if (r.cancelled || r.passwordMayBeIncorrect)
                    break;
            }
            return result;
        },
        [this](const OperationResult&) {
            if (!left_->inArchive())
                left_->refresh();
            if (!right_->inArchive())
                right_->refresh();
        },
        true);
}
void MainWindow::testArchive() {
    QStringList paths = !serviceInputs_.isEmpty() ? std::exchange(serviceInputs_, QStringList{})
                        : active_->inArchive()    ? QStringList{active_->archivePath()}
                                                  : active_->selectedFiles();
    auto selected = indices(active_->selectedArchiveIndexes());
    if (paths.isEmpty())
        return;
    jobs_->run(
        tr("测试归档"),
        [paths, selected](ArchiveEngine& e, const Callbacks& cb) {
            OperationResult result;
            for (const auto& p : paths) {
                auto r = e.test(p.toStdString(), selected, {}, cb);
                result.errors.insert(result.errors.end(), r.errors.begin(), r.errors.end());
                result.cancelled |= r.cancelled;
                result.passwordMayBeIncorrect |= r.passwordMayBeIncorrect;
                result.completedItems += r.completedItems;
                if (r.cancelled || r.passwordMayBeIncorrect)
                    break;
            }
            return result;
        },
        [this](const OperationResult& r) {
            if (r.ok())
                QMessageBox::information(this, tr("测试完成"), tr("所选归档的数据完整性测试通过。"));
        },
        true);
}
void MainWindow::copyMove(bool move) {
    if (!right_->isHidden() && (active_->inArchive() || otherPane()->inArchive())) {
        transferToPane(active_, otherPane(), move);
        return;
    }
    if (active_->inArchive()) {
        if (move) {
            auto* pane = active_;
            if (!writable_.value(pane->archivePath()) || !verifyArchiveSelection(pane))
                return;
            const auto target =
                QFileDialog::getExistingDirectory(this, tr("移动归档成员到"), pane->localDirectory());
            if (target.isEmpty())
                return;
            const auto ids = indices(pane->selectedArchiveIndexes());
            const auto selected = pane->selectedArchiveRows();
            if (ids.empty())
                return;
            const auto archive = pane->archivePath();
            auto temp = std::make_shared<QTemporaryDir>(
                QSettings().value("folders/temp", QDir::tempPath()).toString() + "/M7Arc-Move-XXXXXX");
            jobs_->run(
                tr("移动归档成员"),
                [archive, target, ids, selected, temp](ArchiveEngine& engine, const Callbacks& callbacks) {
                    if (!temp->isValid())
                        throw EngineError("Unable to create temporary directory");
                    auto result =
                        engine.extract(archive.toStdString(), temp->path().toStdString(), ids, {}, callbacks);
                    if (!result.ok())
                        return result;
                    QStringList inputs;
                    for (const auto& entry : selected)
                        inputs << QDir(temp->path()).filePath(entry.path);
                    result = transfer(inputs, target, false, callbacks);
                    if (!result.ok())
                        return result;
                    if (result.completedItems != quint64(inputs.size())) {
                        result.errors.push_back(
                            QObject::tr("部分项目已跳过；源归档成员全部保留。").toStdString());
                        return result;
                    }
                    return engine.remove(archive.toStdString(), ids, {}, callbacks);
                },
                [this](const OperationResult&) { refreshPanes(); });
            return;
        }
        extract();
        return;
    }
    auto sources = active_->selectedFiles();
    if (sources.isEmpty())
        return;
    QString target = !right_->isHidden() && !otherPane()->inArchive() ? otherPane()->localDirectory()
                                                                      : active_->localDirectory();
    bool ok;
    target = QInputDialog::getText(this, move ? tr("移动到") : tr("复制到"), tr("目标文件夹"),
                                   QLineEdit::Normal, target, &ok);
    if (!ok || target.isEmpty())
        return;
    jobs_->run(
        move ? tr("移动文件") : tr("复制文件"),
        [sources, target, move](ArchiveEngine&, const Callbacks& cb) {
            return transfer(sources, target, move, cb);
        },
        [this](const OperationResult&) {
            left_->refresh();
            right_->refresh();
        });
}
void MainWindow::copyToClipboard() {
    auto* pane = active_;
    const auto selected = pane->selectedArchiveRows();
    const auto ids = indices(pane->selectedArchiveIndexes());
    if (ids.empty() || !verifyArchiveSelection(pane))
        return;
    auto temp = std::make_shared<QTemporaryDir>(
        QSettings().value("folders/temp", QDir::tempPath()).toString() + "/M7Arc-Clipboard-XXXXXX");
    if (!temp->isValid()) {
        QMessageBox::warning(this, tr("临时目录不可用"), tr("无法创建临时文件夹。"));
        return;
    }
    const auto archive = pane->archivePath();
    jobs_->run(
        tr("复制归档成员到剪贴板"),
        [archive, ids, temp](ArchiveEngine& engine, const Callbacks& callbacks) {
            return engine.extract(archive.toStdString(), temp->path().toStdString(), ids, {}, callbacks);
        },
        [this, selected, temp](const OperationResult& result) {
            if (!result.ok())
                return;
            QList<QUrl> urls;
            for (const auto& entry : selected) {
                const bool contained = std::any_of(selected.begin(), selected.end(), [&](const auto& p) {
                    const auto prefix = p.path.endsWith('/') ? p.path : p.path + '/';
                    return p.directory && p.path != entry.path && entry.path.startsWith(prefix);
                });
                if (!contained)
                    urls << QUrl::fromLocalFile(QDir(temp->path()).filePath(entry.path));
            }
            auto* mime = new QMimeData;
            mime->setUrls(urls);
            QApplication::clipboard()->setMimeData(mime);
            // Finder may paste after the application closes, so clipboard files outlive this window.
            temp->setAutoRemove(false);
            temporary_.append(temp);
            statusBar()->showMessage(tr("已复制 %1 个项目，可在 Finder 或另一归档中粘贴。").arg(urls.size()));
        },
        true);
}
void MainWindow::pasteFromClipboard() {
    const auto* mime = QApplication::clipboard()->mimeData();
    QStringList files;
    for (const auto& url : mime->urls())
        if (url.isLocalFile())
            files << url.toLocalFile();
    if (files.isEmpty() || !verifyArchiveSelection(active_))
        return;
    const bool move = mime->hasFormat("application/x-m7arc-cut");
    const auto archive = active_->archivePath(), directory = active_->localDirectory(),
               prefix = active_->archiveDirectory(), format = active_->archiveFormat().toLower();
    if (!archive.isEmpty() && !writable_.value(archive))
        return;
    jobs_->run(
        move ? tr("移动文件") : tr("粘贴文件"),
        [files, move, archive, directory, prefix, format](ArchiveEngine& engine, const Callbacks& callbacks) {
            if (archive.isEmpty())
                return transfer(files, directory, move, callbacks);
            CreateOptions options;
            options.format = format.toStdString();
            options.archivePrefix = prefix.toStdString();
            options.preserveMacMetadata = QSettings().value("archive/macMetadata", true).toBool();
            auto result = engine.create(strings(files), archive.toStdString(), options, callbacks);
            if (result.ok() && move)
                for (const auto& file : files)
                    if (!removePath(file))
                        result.errors.push_back(
                            QObject::tr("已写入归档，但无法删除源：%1").arg(file).toStdString());
            return result;
        },
        [this, move](const OperationResult& result) {
            if (move && result.ok())
                QApplication::clipboard()->clear();
            refreshPanes();
        });
}
void MainWindow::refreshPanes() {
    struct Refresh {
        FilePane* pane;
        QString path;
        ArchiveSnapshot snapshot;
    };
    auto refresh = std::make_shared<QList<Refresh>>();
    for (auto* pane : {left_, right_}) {
        if (pane->inArchive())
            refresh->append({pane, pane->archivePath(), {}});
        else
            pane->refresh();
    }
    if (refresh->isEmpty())
        return;
    jobs_->run(
        tr("刷新归档"),
        [refresh](ArchiveEngine& engine, const Callbacks& callbacks) {
            for (auto& item : *refresh)
                item.snapshot = engine.list(item.path.toStdString(), {}, callbacks);
            return OperationResult{};
        },
        [this, refresh](const OperationResult& result) {
            if (!result.ok())
                return;
            for (const auto& item : *refresh) {
                if (item.pane->archivePath() != item.path)
                    continue;
                writable_[item.path] = item.snapshot.canUpdate;
                commentWritable_[item.path] = item.snapshot.canSetComment;
                item.pane->showArchive(
                    item.path, rows(item.snapshot), QString::fromStdString(item.snapshot.format), false,
                    m7arc::ui::properties(item.snapshot.properties), strings(item.snapshot.volumes));
            }
            updateActions();
        },
        true);
}
void MainWindow::removeSelection() {
    const bool archive = active_->inArchive();
    auto files = active_->selectedFiles();
    auto picked = indices(active_->selectedArchiveIndexes());
    int count = archive ? active_->selectedArchiveRows().size() : files.size();
    if (!count)
        return;
    if (QMessageBox::question(this, tr("删除所选项目"),
                              archive ? tr("从归档中删除 %1 个所选项目？").arg(count)
                                      : tr("将 %1 个所选项目移到废纸篓？").arg(count)) != QMessageBox::Yes)
        return;
    const auto path = active_->archivePath();
    auto* pane = active_;
    jobs_->run(
        tr("删除"),
        [files, picked, path, archive](ArchiveEngine& e, const Callbacks& cb) {
            if (archive)
                return e.remove(path.toStdString(), picked, {}, cb);
            OperationResult r;
            for (const auto& f : files) {
                if (cb.cancelled && cb.cancelled()) {
                    r.cancelled = true;
                    break;
                }
                if (!QFile::moveToTrash(f))
                    r.errors.push_back(QObject::tr("无法移到废纸篓：%1").arg(f).toStdString());
                else
                    ++r.completedItems;
            }
            return r;
        },
        [pane](const OperationResult&) { pane->refresh(); });
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
        auto row = archiveRows.first();
        auto path = pane->archivePath();
        QString oldPath = row.path.endsWith('/') ? row.path.chopped(1) : row.path;
        QString newPath = QFileInfo(oldPath).path();
        if (newPath == ".")
            newPath.clear();
        else
            newPath += '/';
        newPath += name;
        jobs_->run(
            tr("重命名归档成员"),
            [path, row, newPath](ArchiveEngine& e, const Callbacks& cb) {
                if (row.directory)
                    return e.renameDirectory(path.toStdString(), row.path.toStdString(),
                                             newPath.toStdString(), {}, cb);
                return e.rename(path.toStdString(), row.index, newPath.toStdString(), {}, cb);
            },
            [pane](const OperationResult&) { pane->refresh(); });
    } else {
        auto dest = QFileInfo(oldName).dir().filePath(name);
        if (!QDir().rename(oldName, dest))
            QMessageBox::warning(this, tr("重命名失败"), tr("目标可能已经存在，或文件不可写。"));
        pane->refresh();
    }
}
void MainWindow::createItem(bool directory) {
    bool ok;
    auto name = QInputDialog::getText(this, directory ? tr("新建文件夹") : tr("新建文件"), tr("名称"),
                                      QLineEdit::Normal, {}, &ok);
    if (!ok || name.isEmpty() || name == "." || name == ".." || name.contains('/'))
        return;
    if (active_->inArchive()) {
        auto* pane = active_;
        if (!writable_.value(pane->archivePath()) || !verifyArchiveSelection(pane))
            return;
        const auto archive = pane->archivePath(), prefix = pane->archiveDirectory(),
                   format = pane->archiveFormat().toLower();
        const auto fullName = prefix + name;
        const auto& entries = pane->archiveModel()->allEntries();
        if (std::any_of(entries.begin(), entries.end(), [&](const ArchiveRow& entry) {
                return entry.path == fullName || entry.path.startsWith(fullName + '/');
            })) {
            QMessageBox::warning(this, tr("创建失败"), tr("归档中已存在同名项目。"));
            return;
        }
        auto temp = std::make_shared<QTemporaryDir>(
            QSettings().value("folders/temp", QDir::tempPath()).toString() + "/M7Arc-New-XXXXXX");
        jobs_->run(
            directory ? tr("新建归档文件夹") : tr("新建归档文件"),
            [archive, prefix, format, name, directory, temp](ArchiveEngine& engine,
                                                             const Callbacks& callbacks) {
                if (!temp->isValid())
                    throw EngineError("Unable to create temporary directory");
                const auto path = QDir(temp->path()).filePath(name);
                const bool created = directory ? QDir().mkdir(path)
                                               : QFile(path).open(QIODevice::WriteOnly | QIODevice::NewOnly);
                if (!created)
                    throw EngineError("Unable to create new archive member");
                CreateOptions options;
                options.format = format.toStdString();
                options.archivePrefix = prefix.toStdString();
                return engine.create({path.toStdString()}, archive.toStdString(), options, callbacks);
            },
            [pane](const OperationResult& result) {
                if (result.ok())
                    pane->refresh();
            });
        return;
    }
    auto path = QDir(active_->localDirectory()).filePath(name);
    bool result =
        directory ? QDir().mkdir(path) : QFile(path).open(QIODevice::WriteOnly | QIODevice::NewOnly);
    if (!result)
        QMessageBox::warning(this, tr("创建失败"), tr("目标已存在或目录不可写。"));
    active_->refresh();
}
void MainWindow::editComment() {
    auto* pane = active_;
    const auto selected = pane->selectedArchiveRows();
    if (selected.size() != 1 || !commentWritable_.value(pane->archivePath()) || !verifyArchiveSelection(pane))
        return;
    const auto entry = selected.first();
    bool accepted = false;
    const auto comment = QInputDialog::getMultiLineText(
        this, tr("编辑归档成员注释"), entry.path, entry.properties.value("Comment").toString(), &accepted);
    if (!accepted || !verifyArchiveSelection(pane))
        return;
    const auto archive = pane->archivePath();
    jobs_->run(
        tr("保存注释"),
        [archive, entry, comment](ArchiveEngine& engine, const Callbacks& callbacks) {
            return engine.setComment(archive.toStdString(), entry.index, comment.toStdString(), {},
                                     callbacks);
        },
        [pane](const OperationResult& result) {
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
                             writable_.value(container.path) ? tr("是") : tr("否"));
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
    const auto inputs = active_->selectedFiles();
    if ((!archived && inputs.isEmpty()) || (archived && !verifyArchiveSelection(active_)))
        return;
    const auto selected = indices(active_->selectedArchiveIndexes());
    if (archived && selected.empty())
        return;
    std::shared_ptr<QTemporaryDir> temporary;
    if (archived) {
        temporary = std::make_shared<QTemporaryDir>(
            QSettings().value("folders/temp", QDir::tempPath()).toString() + "/M7Arc-hash-XXXXXX");
        if (!temporary->isValid()) {
            QMessageBox::warning(this, tr("临时目录不可用"), tr("无法创建临时文件夹。"));
            return;
        }
    }
    const auto archive = active_->archivePath();
    ExtractOptions extract;
    extract.pathMode = PathMode::Current;
    extract.currentDirectory = active_->archiveDirectory().toStdString();
    extract.overwrite = Overwrite::Replace;
    auto summary = std::make_shared<HashSummary>();
    jobs_->run(
        tr("计算摘要"),
        [inputs, algorithms, temporary, archive, selected, extract, summary](ArchiveEngine& engine,
                                                                             const Callbacks& callbacks) {
            auto paths = inputs;
            if (temporary) {
                auto extracted = engine.extract(archive.toStdString(), temporary->path().toStdString(),
                                                selected, extract, callbacks);
                if (!extracted.ok())
                    return extracted;
                const QDir directory(temporary->path());
                for (const auto& item : directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                                QDir::Hidden | QDir::System))
                    paths << item.absoluteFilePath();
            }
            *summary = engine.hashSummary(strings(paths), strings(algorithms), callbacks);
            if (temporary)
                for (auto& file : summary->files)
                    file.path = QDir(temporary->path())
                                    .relativeFilePath(QString::fromStdString(file.path))
                                    .toStdString();
            OperationResult result;
            result.cancelled = callbacks.cancelled && callbacks.cancelled();
            return result;
        },
        [this, summary](const OperationResult& result) {
            if (result.ok())
                showHashResults(this, *summary);
        },
        true);
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
    QRegularExpression re("^([1-9][0-9]*)([KMGT]?)$", QRegularExpression::CaseInsensitiveOption);
    auto match = re.match(sizeText.trimmed());
    if (!match.hasMatch()) {
        QMessageBox::warning(this, tr("大小无效"), tr("请输入正整数，支持 K/M/G/T 后缀。"));
        return;
    }
    quint64 volume = match.captured(1).toULongLong();
    auto suffix = match.captured(2).toUpper();
    int power = suffix.isEmpty() ? 0 : QString("KMGT").indexOf(suffix) + 1;
    while (power-- > 0)
        volume *= 1024;
    if (volume >= quint64(QFileInfo(inputs.first()).size())) {
        QMessageBox::information(this, tr("无需分割"), tr("分卷大小不小于文件大小。"));
        return;
    }
    auto directory = QFileDialog::getExistingDirectory(this, tr("分卷输出目录"), active_->localDirectory());
    if (directory.isEmpty())
        return;
    auto source = inputs.first();
    jobs_->run(
        tr("分割文件"),
        [source, directory, volume](ArchiveEngine&, const Callbacks& cb) {
            OperationResult r;
            QFile input(source);
            if (!input.open(QIODevice::ReadOnly)) {
                r.errors.push_back(input.errorString().toStdString());
                return r;
            }
            quint64 total = input.size(), done = 0;
            int part = 1;
            while (!input.atEnd()) {
                auto dest = QDir(directory).filePath(QFileInfo(source).fileName() + '.' +
                                                     QString::number(part++).rightJustified(3, '0'));
                if (QFileInfo::exists(dest)) {
                    r.errors.push_back(QObject::tr("分卷已存在：%1").arg(dest).toStdString());
                    return r;
                }
                QSaveFile output(dest);
                if (!output.open(QIODevice::WriteOnly)) {
                    r.errors.push_back(output.errorString().toStdString());
                    return r;
                }
                quint64 written = 0;
                while (written < volume && !input.atEnd()) {
                    auto block = input.read(qMin<quint64>(1024 * 1024, volume - written));
                    if (block.isEmpty() && input.error() != QFile::NoError) {
                        r.errors.push_back(input.errorString().toStdString());
                        return r;
                    }
                    if (output.write(block) != block.size()) {
                        r.errors.push_back(output.errorString().toStdString());
                        return r;
                    }
                    written += block.size();
                    done += block.size();
                    if (cb.progress && !cb.progress({"分割", source.toStdString(), done, total, true})) {
                        r.cancelled = true;
                        return r;
                    }
                }
                if (!output.commit()) {
                    r.errors.push_back(output.errorString().toStdString());
                    return r;
                }
                r.outputPaths.push_back(dest.toStdString());
                ++r.completedItems;
            }
            return r;
        },
        [this](const OperationResult&) {
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
    QString prefix = first.absoluteFilePath().chopped(3);
    QStringList parts;
    for (int n = 1;; ++n) {
        auto path = prefix + QString::number(n).rightJustified(3, '0');
        if (!QFileInfo::exists(path))
            break;
        parts << path;
    }
    if (parts.size() < 2) {
        QMessageBox::information(this, tr("合并文件"), tr("至少需要两个连续分卷。"));
        return;
    }
    auto dest = QFileDialog::getSaveFileName(this, tr("合并 %1 个分卷").arg(parts.size()), prefix.chopped(1));
    if (dest.isEmpty())
        return;
    if (parts.contains(dest)) {
        QMessageBox::warning(this, tr("输出位置无效"), tr("合并输出不能覆盖源分卷。"));
        return;
    }
    jobs_->run(
        tr("合并文件"),
        [parts, dest](ArchiveEngine&, const Callbacks& cb) {
            OperationResult r;
            QSaveFile output(dest);
            if (!output.open(QIODevice::WriteOnly)) {
                r.errors.push_back(output.errorString().toStdString());
                return r;
            }
            quint64 total = 0, done = 0;
            for (const auto& p : parts)
                total += QFileInfo(p).size();
            for (const auto& p : parts) {
                QFile input(p);
                if (!input.open(QIODevice::ReadOnly)) {
                    r.errors.push_back(input.errorString().toStdString());
                    return r;
                }
                while (!input.atEnd()) {
                    auto block = input.read(1024 * 1024);
                    if (block.isEmpty() && input.error() != QFile::NoError) {
                        r.errors.push_back(input.errorString().toStdString());
                        return r;
                    }
                    if (output.write(block) != block.size()) {
                        r.errors.push_back(output.errorString().toStdString());
                        return r;
                    }
                    done += block.size();
                    if (cb.progress && !cb.progress({"合并", p.toStdString(), done, total, true})) {
                        r.cancelled = true;
                        return r;
                    }
                }
                ++r.completedItems;
            }
            if (!output.commit())
                r.errors.push_back(output.errorString().toStdString());
            return r;
        },
        [this](const OperationResult&) {
            left_->refresh();
            right_->refresh();
        });
}
void MainWindow::dispatch(const QString& id) {
    if (active_->inArchive() &&
        (id == "delete" || id == "rename" || id == "extract" || id == "extract-here" ||
         id == "extract-folder" || id == "test" || id == "copy" || id == "move") &&
        !verifyArchiveSelection(active_))
        return;
    if (id == "add" || id == "add-7z" || id == "add-zip") {
        createArchive(id == "add" ? QString{} : id.mid(4));
        return;
    }
    if (id == "extract" || id == "extract-here" || id == "extract-folder") {
        extract(id == "extract-here", id == "extract-folder");
        return;
    }
    if (id == "test") {
        testArchive();
        return;
    }
    if (id == "copy" || id == "move") {
        copyMove(id == "move");
        return;
    }
    if (id == "delete") {
        removeSelection();
        return;
    }
    if (id == "rename") {
        renameSelection();
        return;
    }
    if (id == "create-file" || id == "create-folder") {
        createItem(id == "create-folder");
        return;
    }
    if (id == "info") {
        properties();
        return;
    }
    if (id == "formats") {
        showFormatCatalog(this);
        return;
    }
    if (id == "sfx") {
        createSfx();
        return;
    }
    if (id == "edit-save") {
        checkEditedFiles();
        return;
    }
    if (id == "options") {
        showSettings(this);
        applySettings();
        updateActions();
        return;
    }
    if (id == "two-panels") {
        right_->setVisible(actions_[id]->isChecked());
        if (right_->isHidden())
            active_ = left_;
        QSettings().setValue("view/dual", !right_->isHidden());
        updateActions();
        return;
    }
    if (id == "root") {
        active_->navigate(QDir::rootPath());
        return;
    }
    if (id == "up") {
        checkEditedFiles();
        if (!jobs_->busy())
            active_->up();
        return;
    }
    if (id == "refresh") {
        active_->refresh();
        return;
    }
    if (id == "address") {
        bool ok;
        auto p = QInputDialog::getText(this, tr("转到目录"), tr("本地路径"), QLineEdit::Normal,
                                       active_->localDirectory(), &ok);
        if (ok)
            active_->navigate(p);
        return;
    }
    if (id == "flat") {
        active_->setFlat(actions_[id]->isChecked());
        return;
    }
    if (id == "show-hidden") {
        bool on = actions_[id]->isChecked();
        QSettings().setValue("view/hidden", on);
        for (auto* p : {left_, right_})
            if (auto* m = qobject_cast<QFileSystemModel*>(p->view()->model()))
                m->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System |
                             (on ? QDir::Hidden : QDir::Filter(0)));
        return;
    }
    if (id == "select-all") {
        active_->selectAll();
        return;
    }
    if (id == "deselect-all") {
        active_->view()->clearSelection();
        return;
    }
    if (id == "invert-selection") {
        active_->invertSelection();
        return;
    }
    if (id == "select-mask" || id == "deselect-mask" || id == "select-type" || id == "deselect-type") {
        selectMask(id.startsWith("select"), id.endsWith("type"));
        return;
    }
    if (id == "toolbar-text") {
        toolbar_->setToolButtonStyle(actions_[id]->isChecked() ? Qt::ToolButtonTextUnderIcon
                                                               : Qt::ToolButtonIconOnly);
        return;
    }
    if (id == "toolbar-large") {
        toolbar_->setIconSize(actions_[id]->isChecked() ? QSize(28, 28) : QSize(18, 18));
        return;
    }
    if (id.startsWith("view-")) {
        active_->setViewMode(id.mid(5));
        return;
    }
    if (id.startsWith("sort-")) {
        int c = id.mid(5).toInt();
        if (active_->inArchive()) {
            if (c == 2)
                c = 3;
            else if (c == 3)
                c = 4;
        }
        active_->view()->sortByColumn(c, Qt::AscendingOrder);
        return;
    }
    if (id.startsWith("favorite-set:")) {
        auto n = id.mid(13);
        QSettings().setValue("favorites/" + n, active_->location());
        actions_["favorite:" + n]->setText(n + " — " + active_->location());
        return;
    }
    if (id.startsWith("favorite:")) {
        auto path = QSettings().value("favorites/" + id.mid(9)).toString();
        if (!path.isEmpty())
            openPath(path);
        return;
    }
    if (id == "verify-manifest") {
        const auto selected = active_->selectedFiles();
        auto path = selected.size() == 1 ? selected.first() : QString{};
        if (path.isEmpty())
            path = QFileDialog::getOpenFileName(this, tr("选择摘要清单"), active_->location());
        if (!path.isEmpty())
            verifyHashManifest(this, jobs_, path);
        return;
    }
    if (id.startsWith("hash:")) {
        QStringList algorithms;
        if (id == "hash:*" || id == "hash:choose") {
            ArchiveEngine e;
            for (const auto& h : e.hashers())
                algorithms << QString::fromStdString(h.name);
            if (id == "hash:choose")
                algorithms = chooseHashAlgorithms(this, algorithms);
        } else
            algorithms << id.mid(5);
        hashes(algorithms);
        return;
    }
    if (id == "split") {
        splitFile();
        return;
    }
    if (id == "combine") {
        combineFiles();
        return;
    }
    if (id == "clipboard-copy" || id == "clipboard-cut") {
        if (active_->inArchive()) {
            if (id == "clipboard-copy")
                copyToClipboard();
            return;
        }
        auto* mime = new QMimeData;
        QList<QUrl> urls;
        for (const auto& p : active_->selectedFiles())
            urls << QUrl::fromLocalFile(p);
        mime->setUrls(urls);
        if (id == "clipboard-cut")
            mime->setData("application/x-m7arc-cut", "1");
        QApplication::clipboard()->setMimeData(mime);
        return;
    }
    if (id == "clipboard-paste") {
        pasteFromClipboard();
        return;
    }
    if (id == "link") {
        auto files = active_->selectedFiles();
        if (files.size() != 1)
            return;
        bool ok;
        auto type = QInputDialog::getItem(this, tr("创建链接"), tr("类型"),
                                          {tr("符号链接"), tr("硬链接（文件）")}, 0, false, &ok);
        if (!ok)
            return;
        auto dest = QFileDialog::getSaveFileName(this, tr("链接位置"), files.first() + " link");
        if (dest.isEmpty())
            return;
        int result =
            type.startsWith(tr("符号"))
                ? ::symlink(QFile::encodeName(files.first()).constData(), QFile::encodeName(dest).constData())
                : ::link(QFile::encodeName(files.first()).constData(), QFile::encodeName(dest).constData());
        if (result != 0)
            QMessageBox::warning(this, tr("创建链接失败"), tr("目标已存在或文件系统不支持此链接。"));
        active_->refresh();
        return;
    }
    if (id == "diff") {
        auto files = active_->selectedFiles();
        if (files.size() == 2)
            launchFile(QSettings().value("tools/diff").toString(), files);
        return;
    }
    if (id == "open" || id == "open-inside" || id == "open-outside" || id == "view-file" ||
        id == "edit-file" || id.startsWith("open-as:")) {
        if (active_->inArchive()) {
            auto selected = active_->selectedArchiveRows();
            if (!selected.isEmpty()) {
                auto row = selected.first();
                if (row.directory)
                    active_->navigate(active_->archivePath() + "/" + row.path);
                else
                    openMember(active_, row,
                               id == "view-file"   ? QSettings().value("tools/viewer").toString()
                               : id == "edit-file" ? QSettings().value("tools/editor").toString()
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
        if (id == "open-inside" || id.startsWith("open-as:") || (id == "open" && isArchiveName(path)))
            openArchive(active_, path, id.startsWith("open-as:") ? id.mid(8) : QString{});
        else
            launchFile(id == "edit-file"   ? QSettings().value("tools/editor").toString()
                       : id == "view-file" ? QSettings().value("tools/viewer").toString()
                                           : QString{},
                       {path});
        return;
    }
    if (id == "benchmark") {
        showBenchmark(this, jobs_);
        return;
    }
    if (id == "comment") {
        editComment();
        return;
    }
    if (id == "streams") {
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
        return;
    }
    if (id == "temp-cleanup") {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("临时文件"));
        auto* l = new QVBoxLayout(&dialog);
        QStringList dirs;
        for (const auto& p : temporary_)
            dirs << p->path();
        auto* text =
            new QPlainTextEdit(dirs.isEmpty() ? tr("本次会话没有临时文件。") : dirs.join('\n'), &dialog);
        text->setReadOnly(true);
        l->addWidget(text);
        auto* b = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        l->addWidget(b);
        connect(b, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(650, 300);
        dialog.exec();
        return;
    }
    if (id == "help") {
        showHelp();
        return;
    }
    if (id == "about") {
        QMessageBox::about(
            this, tr("关于 M7Arc"),
            tr("M7Arc %1\n\n面向 macOS 的开源归档与文件管理器\n7-Zip 引擎 %2\nQt %3\n\n7-Zip © Igor "
               "Pavlov\n第三方许可随应用提供。\n当前发行包未经 Developer ID 签名和公证。")
                .arg(QStringLiteral(M7ARC_VERSION), QString::fromStdString(ArchiveEngine::version()),
                     QString::fromLatin1(qVersion())));
        return;
    }
    if (id == "exit")
        close();
}
void MainWindow::checkEditedFiles() {
    if (checkingEdits_ || jobs_->busy() || QApplication::activeModalWidget())
        return;
    auto found = std::find_if(edits_.begin(), edits_.end(), [](const auto& s) { return s->changed(); });
    if (found == edits_.end())
        return;
    auto session = *found;
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
    const bool saving = prompt.clickedButton() == save;
    const auto sessions = edits_;
    QStringList affected{session->localPath};
    QString parentPath = session->archivePath;
    for (;;) {
        auto parent = std::find_if(edits_.begin(), edits_.end(),
                                   [&](const auto& s) { return s->localPath == parentPath; });
        if (parent == edits_.end())
            break;
        affected << (*parent)->localPath;
        parentPath = (*parent)->archivePath;
    }
    jobs_->run(
        saving ? tr("保存归档内编辑") : tr("恢复归档内文件"),
        [session, sessions, saving](ArchiveEngine& e, const Callbacks& cb) {
            if (saving)
                return saveEditedMember(e, *session, sessions, cb);
            QFileInfo archive(session->archivePath);
            if (archive.size() != session->archiveSize || archive.lastModified() != session->archiveModified)
                throw EngineError("The archive changed; reopen it before restoring the edited member");
            auto snapshot = e.list(session->archivePath.toStdString(), {}, cb);
            auto entry = std::find_if(snapshot.entries.begin(), snapshot.entries.end(), [&](const auto& v) {
                return v.index == session->index && QString::fromStdString(v.path) == session->memberPath;
            });
            if (entry == snapshot.entries.end())
                throw EngineError("The original archive member no longer exists");
            QString root = session->localPath;
            root.chop(session->memberPath.size());
            ExtractOptions options;
            options.overwrite = Overwrite::Replace;
            return e.extract(session->archivePath.toStdString(), root.toStdString(), {entry->index}, options,
                             cb);
        },
        [this, session, affected, saving](const OperationResult& r) {
            checkingEdits_ = false;
            if (r.ok()) {
                for (const auto& s : edits_)
                    if ((saving && affected.contains(s->localPath)) || s == session) {
                        s->remember();
                        s->rememberArchive();
                    }
                if (active_->inArchive())
                    active_->refresh();
            } else
                for (const auto& temp : temporary_)
                    if (session->localPath.startsWith(temp->path() + "/"))
                        temp->setAutoRemove(false);
        });
}
void MainWindow::transferToPane(FilePane* source, FilePane* target, bool move, bool confirm) {
    if (!verifyArchiveSelection(source) || !verifyArchiveSelection(target))
        return;
    const bool fromArchive = source->inArchive(), toArchive = target->inArchive();
    const auto sourceFiles = source->selectedFiles();
    const auto sourceRows = source->selectedArchiveRows();
    auto ids = indices(source->selectedArchiveIndexes());
    if (fromArchive ? ids.empty() : sourceFiles.isEmpty())
        return;
    if (toArchive && !writable_.value(target->archivePath())) {
        QMessageBox::information(this, tr("目标归档只读"), tr("此归档可以浏览和提取，无法写入。"));
        return;
    }
    if (move && fromArchive && !writable_.value(source->archivePath()))
        return;
    if (fromArchive && toArchive && source->archivePath() == target->archivePath()) {
        QMessageBox::information(this, tr("同一归档"),
                                 tr("请使用重命名调整成员路径，或将文件复制到另一归档。"));
        return;
    }
    const auto sourceArchive = source->archivePath(), targetArchive = target->archivePath(),
               targetDirectory = target->localDirectory(), prefix = target->archiveDirectory();
    const auto targetFormat = target->archiveFormat().toLower();
    if (confirm && QMessageBox::question(this, move ? tr("移动所选项目") : tr("复制所选项目"),
                                         tr("目标：%1").arg(target->location())) != QMessageBox::Yes)
        return;
    auto temp = std::make_shared<QTemporaryDir>(
        QSettings().value("folders/temp", QDir::tempPath()).toString() + "/M7Arc-Transfer-XXXXXX");
    jobs_->run(
        move ? tr("移动项目") : tr("复制项目"),
        [sourceFiles, sourceRows, ids, sourceArchive, targetArchive, targetDirectory, prefix, targetFormat,
         fromArchive, toArchive, move, temp](ArchiveEngine& e, const Callbacks& cb) {
            OperationResult result;
            QStringList inputs = sourceFiles;
            if (fromArchive) {
                if (!temp->isValid())
                    throw EngineError("Unable to create temporary directory");
                result = e.extract(sourceArchive.toStdString(), temp->path().toStdString(), ids, {}, cb);
                if (!result.ok())
                    return result;
                inputs.clear();
                for (const auto& row : sourceRows) {
                    QString path = row.path;
                    while (path.endsWith('/'))
                        path.chop(1);
                    inputs << QDir(temp->path()).filePath(path);
                }
            }
            if (toArchive) {
                CreateOptions options;
                options.format = targetFormat.toStdString();
                options.archivePrefix = prefix.toStdString();
                result = e.create(strings(inputs), targetArchive.toStdString(), options, cb);
            } else
                result = transfer(inputs, targetDirectory, false, cb);
            if (!result.ok() || !move)
                return result;
            if (fromArchive && !toArchive && result.completedItems != quint64(inputs.size())) {
                result.errors.push_back(QObject::tr("部分项目已跳过；源归档成员全部保留。").toStdString());
                return result;
            }
            if (fromArchive)
                return e.remove(sourceArchive.toStdString(), ids, {}, cb);
            for (const auto& path : sourceFiles)
                if (!removePath(path))
                    result.errors.push_back(
                        QObject::tr("已写入目标，但无法删除源：%1").arg(path).toStdString());
            return result;
        },
        [this](const OperationResult& r) {
            if (r.ok()) {
                refreshPanes();
                QTimer::singleShot(300, this, &MainWindow::checkEditedFiles);
            }
        });
}
void MainWindow::createSfx() {
    showSfxCreation(this, active_->selectedFiles(), active_->localDirectory(), jobs_,
                    [this] { refreshPanes(); });
}
void MainWindow::showHelp() {
    m7arc::ui::showHelp(this);
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (std::any_of(edits_.begin(), edits_.end(), [](const auto& s) { return s->changed(); })) {
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
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("pane/right", right_->localDirectory());
    s.setValue("view/dual", !right_->isHidden());
    event->accept();
}
} // namespace m7arc::ui
