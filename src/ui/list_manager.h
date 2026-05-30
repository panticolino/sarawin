#ifndef SARA_UI_LIST_MANAGER_H
#define SARA_UI_LIST_MANAGER_H

#include "core/types.h"
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>

namespace sara {

class Database;
class MetadataReader;

/**
 * Gestor de listas guardadas (rediseñado).
 */
class ListManager : public QDialog
{
    Q_OBJECT

public:
    explicit ListManager(Database* db, MetadataReader* reader = nullptr,
                         QWidget* parent = nullptr);

    QStringList selectedTracks() const { return selectedTracks_; }
    QString selectedListName() const { return selectedListName_; }
    bool wasLoaded() const { return loaded_; }

signals:
    void cuePreviewRequested(const QString& filePath);
    void pisadorAssignRequested(const QString& filePath, const QString& type);
    void cueStopRequested();

private slots:
    void onNew();
    void onRename();
    void onDelete();
    void onLoad();
    void onListSelected();
    void onAddTracks();
    void onRemoveTrack();
    void onMoveUp();
    void onMoveDown();
    void refreshLists();
    void refreshTracks();
    void syncTracksAfterDrop();

private:
    void setupUI();
    QString createList(const QString& name);
    bool setListTracks(const QString& listId, const QStringList& tracks);
    QStringList getListTracks(const QString& listId);
    QString currentListId() const;
    QString formatDuration(int64_t ms) const;

    Database*       db_;
    MetadataReader* reader_;

    // Izquierda: listas
    QListWidget* listWidget_;
    QLabel*      listCountLabel_;

    // Derecha: contenido
    QListWidget* trackWidget_;
    QLabel*      trackCountLabel_;

    QStringList selectedTracks_;
    QString selectedListName_;
    bool loaded_ = false;
    bool cuePlaying_ = false;

    QPushButton* cueBtn_ = nullptr;
    QPushButton* deleteListBtn_ = nullptr;
    bool operationMode_ = false;

public:
    void setOperationMode(bool on) { operationMode_ = on; if (deleteListBtn_) deleteListBtn_->setVisible(!on); }
};

} // namespace sara

#endif
