#include "FilePane.h"
#include "app/Settings.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
namespace m7arc::ui {
FilePane::FilePane(QWidget* p)
    : QWidget(p), files_(new QFileSystemModel(this)), archive_(new ArchiveModel(this)),
      view_(new QTreeView(this)), icons_(new QListView(this)), stack_(new QStackedWidget(this)),
      address_(new QComboBox(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 4);
    layout->setSpacing(7);
    auto* bar = new QHBoxLayout;
    bar->setSpacing(6);
    auto* upButton = new QToolButton(this);
    upButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    upButton->setToolTip(tr("上一级（Backspace）"));
    address_->setEditable(true);
    address_->setInsertPolicy(QComboBox::NoInsert);
    address_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* columns = new QToolButton(this);
    columns->setText("≡");
    columns->setToolTip(tr("显示列"));
    bar->addWidget(upButton);
    bar->addWidget(address_, 1);
    bar->addWidget(columns);
    layout->addLayout(bar);
    files_->setReadOnly(true);
    files_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    files_->setRootPath(QDir::rootPath());
    view_->setModel(files_);
    view_->setRootIsDecorated(false);
    view_->setItemsExpandable(false);
    view_->setAlternatingRowColors(true);
    view_->setUniformRowHeights(true);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSortingEnabled(true);
    view_->sortByColumn(0, Qt::AscendingOrder);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->header()->setStretchLastSection(false);
    view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    view_->setColumnWidth(1, 105);
    view_->setColumnWidth(2, 90);
    view_->setColumnWidth(3, 165);
    stack_->addWidget(view_);
    stack_->addWidget(icons_);
    layout->addWidget(stack_, 1);
    icons_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    icons_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    icons_->setContextMenuPolicy(Qt::CustomContextMenu);
    icons_->setUniformItemSizes(true);
    icons_->setLayoutMode(QListView::Batched);
    icons_->setBatchSize(256);
    for (auto* v : QList<QAbstractItemView*>{view_, icons_}) {
        v->setDragEnabled(true);
        v->setAcceptDrops(true);
        v->setDragDropMode(QAbstractItemView::DragDrop);
        v->setDefaultDropAction(Qt::CopyAction);
        v->viewport()->setAcceptDrops(true);
        v->installEventFilter(this);
        v->viewport()->installEventFilter(this);
    }
    connect(icons_, &QListView::doubleClicked, view_, &QTreeView::doubleClicked);
    connect(icons_, &QListView::customContextMenuRequested, this, [this](const QPoint& p) {
        emit activated(this);
        emit contextMenuRequested(icons_->viewport()->mapToGlobal(p));
    });
    auto* statusBar = new QHBoxLayout;
    statusBar->setSpacing(10);
    for (int n = 0; n < 4; ++n) {
        auto* s = new QLabel(this);
        s->setTextInteractionFlags(Qt::TextSelectableByMouse);
        status_.append(s);
        statusBar->addWidget(s, n == 0 ? 1 : 0);
    }
    layout->addLayout(statusBar);
    view_->installEventFilter(this);
    view_->viewport()->installEventFilter(this);
    address_->lineEdit()->installEventFilter(this);
    connect(upButton, &QToolButton::clicked, this, &FilePane::up);
    connect(address_->lineEdit(), &QLineEdit::returnPressed, this,
            [this] { navigate(address_->currentText()); });
    connect(address_, &QComboBox::textActivated, this, &FilePane::navigate);
    connect(view_, &QTreeView::customContextMenuRequested, this, [this](const QPoint& p) {
        emit activated(this);
        emit contextMenuRequested(view_->viewport()->mapToGlobal(p));
    });
    connect(view_, &QTreeView::doubleClicked, this, [this](const QModelIndex& i) {
        if (inArchive()) {
            const auto e = archive_->entry(i.row());
            if (e.directory) {
                archive_->setDirectory(e.path);
                updateAddress();
                updateStatus();
            } else
                emit archiveMemberRequested(e);
        } else {
            auto info = files_->fileInfo(i);
            if (info.isDir())
                navigate(info.absoluteFilePath());
            else
                emit openRequested(info.absoluteFilePath());
        }
    });
    connect(files_, &QFileSystemModel::directoryLoaded, this, [this] { updateStatus(); });
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        updateStatus();
        emit selectionChanged();
    });
    connect(columns, &QToolButton::clicked, this, [this, columns] {
        QMenu menu;
        for (int c = 1; c < view_->model()->columnCount(); ++c) {
            auto* a = menu.addAction(view_->model()->headerData(c, Qt::Horizontal).toString());
            a->setCheckable(true);
            a->setChecked(!view_->isColumnHidden(c));
            connect(a, &QAction::toggled, this, [this, c](bool on) { view_->setColumnHidden(c, !on); });
        }
        menu.exec(columns->mapToGlobal(QPoint(0, columns->height())));
    });
    applySettings();
    navigate(QDir::homePath());
}
bool FilePane::eventFilter(QObject* object, QEvent* e) {
    const bool viewport = object == view_->viewport() || object == icons_->viewport();
    if (viewport && e->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(e);
        dragStart_ = mouse->position().toPoint();
        dragStarted_ = false;
    }
    if (viewport && inArchive() && e->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(e);
        if (mouse->buttons().testFlag(Qt::LeftButton) && !dragStarted_ &&
            (mouse->position().toPoint() - dragStart_).manhattanLength() >=
                QApplication::startDragDistance() &&
            !selectedArchiveRows().isEmpty()) {
            dragStarted_ = true;
            emit archiveDragRequested(qobject_cast<QWidget*>(object));
            return true;
        }
    }
    if (e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove) {
        auto* d = static_cast<QDragMoveEvent*>(e);
        if (d->mimeData()->hasUrls() || d->mimeData()->hasFormat("application/x-m7arc-members")) {
            d->acceptProposedAction();
            return true;
        }
    }
    if (e->type() == QEvent::Drop) {
        auto* d = static_cast<QDropEvent*>(e);
        bool move = d->proposedAction() == Qt::MoveAction;
        if (d->mimeData()->hasUrls()) {
            QStringList paths;
            for (const auto& u : d->mimeData()->urls())
                if (u.isLocalFile())
                    paths << u.toLocalFile();
            emit filesDropped(paths, move);
            d->acceptProposedAction();
            return true;
        }
        if (d->mimeData()->hasFormat("application/x-m7arc-members")) {
            auto data = QJsonDocument::fromJson(d->mimeData()->data("application/x-m7arc-members")).object();
            emit archiveDropped(data["archive"].toString(), move);
            d->acceptProposedAction();
            return true;
        }
    }
    if (e->type() == QEvent::FocusIn || e->type() == QEvent::MouseButtonPress)
        emit activated(this);
    if (e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Backspace && (view_->hasFocus() || icons_->hasFocus())) {
            up();
            return true;
        }
        if ((k->key() == Qt::Key_Enter || k->key() == Qt::Key_Return) &&
            (view_->hasFocus() || icons_->hasFocus())) {
            if (view_->currentIndex().isValid())
                emit view_->doubleClicked(view_->currentIndex());
            return true;
        }
    }
    return false;
}
void FilePane::navigate(const QString& path) {
    QString p = path;
    if (inArchive() && p.startsWith(archivePath_ + "/")) {
        archive_->setDirectory(p.mid(archivePath_.size() + 1));
        updateAddress();
        updateStatus();
        emit locationChanged();
        return;
    }
    if (p.startsWith('~'))
        p.replace(0, 1, QDir::homePath());
    QFileInfo info(p);
    if (info.isFile()) {
        emit openRequested(info.absoluteFilePath());
        return;
    }
    if (!info.isDir()) {
        address_->setCurrentText(location());
        return;
    }
    if (inArchive() && leaveGuard_ && !leaveGuard_())
        return;
    localPath_ = info.absoluteFilePath();
    parents_.clear();
    archivePath_.clear();
    archiveFormat_.clear();
    view_->setModel(files_);
    view_->setRootIndex(files_->index(localPath_));
    view_->setCurrentIndex({});
    view_->clearSelection();
    icons_->setModel(files_);
    icons_->setRootIndex(files_->index(localPath_));
    icons_->setSelectionModel(view_->selectionModel());
    view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    disconnect(view_->selectionModel(), nullptr, this, nullptr);
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        updateStatus();
        emit selectionChanged();
    });
    updateAddress();
    updateStatus();
    emit locationChanged();
}
void FilePane::showArchive(const QString& file, QList<ArchiveRow> e, const QString& format, bool nested,
                           QList<ArchiveProperty> properties, QStringList volumes) {
    const auto directory = file == archivePath_ ? archive_->directory() : QString{};
    if (!nested && file != archivePath_)
        parents_.clear();
    if (nested && inArchive() && file != archivePath_)
        parents_.append({archivePath_, archiveFormat_, archive_->directory(), archive_->allEntries(),
                         archiveSize_, archiveModified_, archiveProperties_, archiveVolumes_});
    archivePath_ = file;
    archiveSize_ = QFileInfo(file).size();
    archiveModified_ = QFileInfo(file).lastModified();
    archiveFormat_ = format;
    archiveProperties_ = std::move(properties);
    archiveVolumes_ = std::move(volumes);
    archive_->setArchivePath(file);
    archive_->setEntries(std::move(e), directory);
    view_->setModel(archive_);
    view_->setRootIndex({});
    icons_->setModel(archive_);
    icons_->setRootIndex({});
    icons_->setSelectionModel(view_->selectionModel());
    view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 9; ++c)
        view_->setColumnWidth(c, c == 4 ? 165 : 100);
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        updateStatus();
        emit selectionChanged();
    });
    updateAddress();
    updateStatus();
    emit locationChanged();
}
void FilePane::leaveArchive() {
    navigate(localPath_);
}
QList<ArchiveContainer> FilePane::archiveContainers() const {
    QList<ArchiveContainer> containers;
    if (!inArchive())
        return containers;
    for (const auto& parent : parents_)
        containers.append({parent.path, parent.format, parent.properties, parent.volumes});
    containers.append({archivePath_, archiveFormat_, archiveProperties_, archiveVolumes_});
    return containers;
}
bool FilePane::archiveUnchanged() const {
    QFileInfo f(archivePath_);
    return !inArchive() || (f.exists() && f.size() == archiveSize_ && f.lastModified() == archiveModified_);
}
QString FilePane::location() const {
    return inArchive() ? archivePath_ + "/" + archive_->directory() : localPath_;
}
QString FilePane::localDirectory() const {
    return localPath_;
}
QStringList FilePane::selectedFiles() const {
    QStringList paths;
    if (inArchive())
        return paths;
    for (const auto& i : view_->selectionModel()->selectedRows())
        paths.append(files_->filePath(i));
    return paths;
}
QList<quint32> FilePane::selectedArchiveIndexes() const {
    return inArchive() ? archive_->indexesForRows(view_->selectionModel()->selectedRows()) : QList<quint32>{};
}
QList<ArchiveRow> FilePane::selectedArchiveRows() const {
    QList<ArchiveRow> e;
    if (inArchive())
        for (const auto& i : view_->selectionModel()->selectedRows())
            e.append(archive_->entry(i.row()));
    return e;
}
void FilePane::up() {
    if (inArchive()) {
        auto p = archive_->directory();
        if (p.isEmpty()) {
            if (leaveGuard_ && !leaveGuard_())
                return;
            if (!parents_.isEmpty()) {
                auto parent = parents_.takeLast();
                archivePath_ = parent.path;
                archiveSize_ = parent.size;
                archiveModified_ = parent.modified;
                archiveFormat_ = parent.format;
                archiveProperties_ = parent.properties;
                archiveVolumes_ = parent.volumes;
                archive_->setArchivePath(parent.path);
                archive_->setEntries(parent.entries, parent.directory);
                updateAddress();
                updateStatus();
                emit locationChanged();
                if (!archiveUnchanged())
                    emit archiveRefreshRequested(archivePath_);
            } else
                leaveArchive();
            return;
        }
        p = p.chopped(1);
        auto slash = p.lastIndexOf('/');
        archive_->setDirectory(slash < 0 ? QString{} : p.left(slash));
        updateAddress();
        updateStatus();
    } else
        navigate(QFileInfo(localPath_).dir().absolutePath());
}
void FilePane::refresh() {
    if (inArchive())
        emit archiveRefreshRequested(archivePath_);
    else {
        files_->setRootPath(localPath_);
        view_->setRootIndex(files_->index(localPath_));
        updateStatus();
    }
}
void FilePane::updateAddress() {
    const auto p = location();
    if (address_->findText(p) < 0)
        address_->insertItem(0, p);
    while (address_->count() > 30)
        address_->removeItem(address_->count() - 1);
    address_->setCurrentText(p);
}
void FilePane::updateStatus() {
    auto rows = view_->selectionModel()->selectedRows();
    quint64 total = 0;
    QString size, date;
    if (inArchive()) {
        for (const auto& i : rows)
            total += archive_->entry(i.row()).size;
        if (view_->currentIndex().isValid()) {
            auto e = archive_->entry(view_->currentIndex().row());
            size = QLocale().formattedDataSize(e.size);
            date = e.modified.toLocalTime().toString("MM-dd HH:mm");
        }
    } else {
        for (const auto& i : rows)
            total += files_->size(i);
        auto f = files_->fileInfo(view_->currentIndex());
        if (f.exists()) {
            size = QLocale().formattedDataSize(f.size());
            date = f.lastModified().toString("MM-dd HH:mm");
        }
    }
    status_[0]->setText(tr("%1 / %2 项").arg(rows.size()).arg(view_->model()->rowCount(view_->rootIndex())));
    status_[1]->setText(QLocale().formattedDataSize(total));
    status_[2]->setText(size);
    status_[3]->setText(date);
}
void FilePane::selectAll() {
    if (stack_->currentWidget() == icons_)
        icons_->selectAll();
    else
        view_->selectAll();
}
void FilePane::invertSelection() {
    for (int r = 0; r < view_->model()->rowCount(view_->rootIndex()); ++r)
        view_->selectionModel()->select(view_->model()->index(r, 0, view_->rootIndex()),
                                        QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
}
void FilePane::selectMask(const QString& mask, bool on) {
    QRegularExpression re(QRegularExpression::wildcardToRegularExpression(mask),
                          QRegularExpression::CaseInsensitiveOption);
    for (int r = 0; r < view_->model()->rowCount(view_->rootIndex()); ++r) {
        auto i = view_->model()->index(r, 0, view_->rootIndex());
        if (re.match(i.data().toString()).hasMatch())
            view_->selectionModel()->select(
                i, (on ? QItemSelectionModel::Select : QItemSelectionModel::Deselect) |
                       QItemSelectionModel::Rows);
    }
}
void FilePane::setFlat(bool on) {
    if (inArchive())
        archive_->setFlat(on);
}
void FilePane::applySettings() {
    const app::Settings settings;
    files_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System |
                      (settings.showHidden() ? QDir::Hidden : QDir::Filter(0)));
    files_->setOption(QFileSystemModel::DontWatchForChanges, !settings.autoRefresh());
    view_->setAlternatingRowColors(settings.alternatingRows());
}
void FilePane::setViewMode(const QString& mode) {
    if (mode == "details") {
        stack_->setCurrentWidget(view_);
        return;
    }
    bool large = mode == "large";
    icons_->setViewMode(mode == "list" ? QListView::ListMode : QListView::IconMode);
    icons_->setResizeMode(QListView::Adjust);
    icons_->setMovement(QListView::Static);
    icons_->setIconSize(large ? QSize(48, 48) : QSize(20, 20));
    icons_->setGridSize(large ? QSize(128, 90) : mode == "list" ? QSize{} : QSize(180, 38));
    icons_->setWordWrap(large);
    stack_->setCurrentWidget(icons_);
    icons_->setFocus();
}
} // namespace m7arc::ui
