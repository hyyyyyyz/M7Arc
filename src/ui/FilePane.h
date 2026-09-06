#pragma once
#include "ArchiveModel.h"
#include <QWidget>
#include <functional>
class QFileSystemModel;
class QTreeView;
class QListView;
class QStackedWidget;
class QComboBox;
class QLabel;
namespace m7arc::ui {
class FilePane final : public QWidget {
    Q_OBJECT
  public:
    explicit FilePane(QWidget* parent = nullptr);
    void navigate(const QString& path);
    void showArchive(const QString& file, QList<ArchiveRow> entries, const QString& format,
                     bool nested = false, QList<ArchiveProperty> properties = {}, QStringList volumes = {});
    QList<ArchiveContainer> archiveContainers() const;
    void leaveArchive();
    void refresh();
    void up();
    QString location() const;
    QString localDirectory() const;
    QString archivePath() const {
        return archivePath_;
    }
    QString archiveDirectory() const {
        return archive_->directory();
    }
    QString archiveFormat() const {
        return archiveFormat_;
    }
    bool archiveUnchanged() const;
    bool inArchive() const {
        return !archivePath_.isEmpty();
    }
    QStringList selectedFiles() const;
    QList<quint32> selectedArchiveIndexes() const;
    QList<ArchiveRow> selectedArchiveRows() const;
    QTreeView* view() const {
        return view_;
    }
    ArchiveModel* archiveModel() const {
        return archive_;
    }
    void selectAll();
    void selectMask(const QString& mask, bool selected);
    void invertSelection();
    void setFlat(bool);
    void setViewMode(const QString& mode);
    void applySettings();
    void setLeaveGuard(std::function<bool()> guard) {
        leaveGuard_ = std::move(guard);
    }
  signals:
    void activated(FilePane* pane);
    void openRequested(const QString& file);
    void archiveMemberRequested(const ArchiveRow& entry);
    void archiveRefreshRequested(const QString& file);
    void selectionChanged();
    void locationChanged();
    void contextMenuRequested(const QPoint& global);
    void filesDropped(const QStringList& paths, bool move);
    void archiveDropped(const QString& archive, bool move);
    void archiveDragRequested(QWidget* source);

  protected:
    bool eventFilter(QObject* object, QEvent* event) override;

  private:
    void updateStatus();
    void updateAddress();
    QFileSystemModel* files_;
    ArchiveModel* archive_;
    QTreeView* view_;
    QListView* icons_;
    QStackedWidget* stack_;
    QComboBox* address_;
    QList<QLabel*> status_;
    struct ArchiveLocation {
        QString path, format, directory;
        QList<ArchiveRow> entries;
        qint64 size;
        QDateTime modified;
        QList<ArchiveProperty> properties;
        QStringList volumes;
    };
    QList<ArchiveLocation> parents_;
    std::function<bool()> leaveGuard_;
    QString localPath_, archivePath_, archiveFormat_;
    qint64 archiveSize_ = 0;
    QDateTime archiveModified_;
    QList<ArchiveProperty> archiveProperties_;
    QStringList archiveVolumes_;
    QPoint dragStart_;
    bool dragStarted_ = false;
};
} // namespace m7arc::ui
