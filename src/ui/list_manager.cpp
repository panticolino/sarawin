#include "ui/list_manager.h"
#include "data/database.h"
#include "util/logger.h"
#include "util/metadata_reader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QIcon>
#include <QFileInfo>
#include <QUuid>
#include <QPushButton>
#include <QMenu>

namespace sara {

ListManager::ListManager(Database* db, MetadataReader* reader, QWidget* parent)
    : QDialog(parent), db_(db), reader_(reader)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle(tr("Listas Guardadas"));
    setMinimumSize(720, 460);
    resize(780, 500);
    setupUI();
    refreshLists();
}

void ListManager::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 12, 16, 12);

    auto* titleLabel = new QLabel(tr("Listas Guardadas"));
    titleLabel->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(titleLabel);

    auto* descLabel = new QLabel(
        tr("Cree listas de canciones para cargar en la cola de reproducción. Seleccione una lista para ver y editar su contenido."));
    descLabel->setStyleSheet("font-size: 12px;");
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // ══ Layout horizontal ════════════════════════════
    auto* columns = new QHBoxLayout();
    columns->setSpacing(10);

    // ── Izquierda: lista de listas ───────────────────
    auto* leftGroup = new QGroupBox(tr("Listas"));
    auto* leftLayout = new QVBoxLayout(leftGroup);
    leftLayout->setSpacing(4);

    listWidget_ = new QListWidget();
    connect(listWidget_, &QListWidget::currentRowChanged,
            this, [this](int) { onListSelected(); });
    leftLayout->addWidget(listWidget_, 1);

    auto* leftBtnRow = new QHBoxLayout();
    leftBtnRow->setSpacing(3);

    auto* newBtn = new QPushButton(QIcon(":/icons/plus.svg"), "");
    newBtn->setFixedSize(28, 28);
    newBtn->setToolTip(tr("Nueva lista"));
    connect(newBtn, &QPushButton::clicked, this, &ListManager::onNew);

    auto* renameBtn = new QPushButton(QIcon(":/icons/settings.svg"), "");
    renameBtn->setFixedSize(28, 28);
    renameBtn->setToolTip(tr("Renombrar lista"));
    connect(renameBtn, &QPushButton::clicked, this, &ListManager::onRename);

    deleteListBtn_ = new QPushButton(QIcon(":/icons/x.svg"), "");
    deleteListBtn_->setFixedSize(28, 28);
    deleteListBtn_->setToolTip(tr("Eliminar lista"));
    deleteListBtn_->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.15); color: #ef4444; "
        "border: 1px solid rgba(239,68,68,0.3); border-radius: 6px; }"
        "QPushButton:hover { background: rgba(239,68,68,0.25); }");
    connect(deleteListBtn_, &QPushButton::clicked, this, &ListManager::onDelete);

    leftBtnRow->addWidget(newBtn);
    leftBtnRow->addWidget(renameBtn);
    leftBtnRow->addStretch();
    leftBtnRow->addWidget(deleteListBtn_);
    leftLayout->addLayout(leftBtnRow);

    listCountLabel_ = new QLabel();
    listCountLabel_->setStyleSheet("font-size: 10px;");
    leftLayout->addWidget(listCountLabel_);

    columns->addWidget(leftGroup, 3);

    // ── Derecha: contenido de la lista ───────────────
    auto* rightGroup = new QGroupBox(tr("Contenido"));
    auto* rightLayout = new QVBoxLayout(rightGroup);
    rightLayout->setSpacing(4);

    trackWidget_ = new QListWidget();
    trackWidget_->setDragDropMode(QAbstractItemView::InternalMove);
    // Guardar orden al soltar drag & drop
    connect(trackWidget_->model(), &QAbstractItemModel::rowsMoved,
            this, &ListManager::syncTracksAfterDrop);
    rightLayout->addWidget(trackWidget_, 1);

    auto* rightBtnRow = new QHBoxLayout();
    rightBtnRow->setSpacing(3);

    auto* addTracksBtn = new QPushButton(QIcon(":/icons/plus.svg"), " Agregar");
    addTracksBtn->setFixedHeight(26);
    connect(addTracksBtn, &QPushButton::clicked, this, &ListManager::onAddTracks);

    cueBtn_ = new QPushButton(QIcon(":/icons/volume-2.svg"), "");
    cueBtn_->setFixedSize(26, 26);
    cueBtn_->setToolTip(tr("Preescuchar pista seleccionada (CUE)"));
    cueBtn_->setProperty("class", "secondaryButton");
    connect(cueBtn_, &QPushButton::clicked, this, [this]() {
        auto* item = trackWidget_->currentItem();
        if (!item) return;
        QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty()) return;

        if (cuePlaying_) {
            emit cueStopRequested();
            cuePlaying_ = false;
            cueBtn_->setIcon(QIcon(":/icons/volume-2.svg"));
            cueBtn_->setToolTip(tr("Preescuchar pista seleccionada (CUE)"));
        } else {
            emit cuePreviewRequested(path);
            cuePlaying_ = true;
            cueBtn_->setIcon(QIcon(":/icons/square.svg"));
            cueBtn_->setToolTip(tr("Detener preescucha"));
        }
    });

    // Resetear botón CUE al cambiar de pista
    connect(trackWidget_, &QListWidget::currentRowChanged, this, [this]() {
        if (cuePlaying_) {
            emit cueStopRequested();
            cuePlaying_ = false;
            cueBtn_->setIcon(QIcon(":/icons/volume-2.svg"));
            cueBtn_->setToolTip(tr("Preescuchar pista seleccionada (CUE)"));
        }
    });

    auto* moveUpBtn = new QPushButton(QIcon(":/icons/chevron-left.svg"), "");
    moveUpBtn->setFixedSize(26, 26);
    moveUpBtn->setToolTip(tr("Subir"));
    moveUpBtn->setProperty("class", "secondaryButton");
    connect(moveUpBtn, &QPushButton::clicked, this, &ListManager::onMoveUp);

    auto* moveDownBtn = new QPushButton(QIcon(":/icons/chevron-right.svg"), "");
    moveDownBtn->setFixedSize(26, 26);
    moveDownBtn->setToolTip(tr("Bajar"));
    moveDownBtn->setProperty("class", "secondaryButton");
    connect(moveDownBtn, &QPushButton::clicked, this, &ListManager::onMoveDown);

    auto* removeTrackBtn = new QPushButton(QIcon(":/icons/x.svg"), "");
    removeTrackBtn->setFixedSize(26, 26);
    removeTrackBtn->setToolTip(tr("Quitar pista seleccionada"));
    removeTrackBtn->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.15); color: #ef4444; "
        "border: 1px solid rgba(239,68,68,0.3); border-radius: 6px; }"
        "QPushButton:hover { background: rgba(239,68,68,0.25); }");
    connect(removeTrackBtn, &QPushButton::clicked, this, &ListManager::onRemoveTrack);

    rightBtnRow->addWidget(addTracksBtn);
    rightBtnRow->addWidget(cueBtn_);
    rightBtnRow->addStretch();
    rightBtnRow->addWidget(moveUpBtn);
    rightBtnRow->addWidget(moveDownBtn);
    rightBtnRow->addWidget(removeTrackBtn);
    rightLayout->addLayout(rightBtnRow);

    // Menú contextual en tracks
    trackWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(trackWidget_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = trackWidget_->itemAt(pos);
        if (!item) return;
        QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty()) return;

        QMenu menu(this);

        auto* cueAction = menu.addAction(QIcon(":/icons/volume-2.svg"), tr("Preescuchar (CUE)"));
        menu.addSeparator();
        auto* removeAction = menu.addAction(QIcon(":/icons/x.svg"), tr("Quitar de la lista"));
        menu.addSeparator();

        auto* pisadorMenu = menu.addMenu(QIcon(":/icons/music.svg"), tr("Pisador"));
        auto* pisSpecific = pisadorMenu->addAction(tr("Asignar pisador específico..."));
        auto* pisRandom = pisadorMenu->addAction(tr("Asignar aleatorio (de carpeta)"));
        auto* pisTime = pisadorMenu->addAction(tr("Pisar con locución de hora"));
        pisadorMenu->addSeparator();
        auto* pisNone = pisadorMenu->addAction(tr("No pisar nunca esta pista"));
        auto* pisRemove = pisadorMenu->addAction(tr("Quitar asignación individual"));

        auto* result = menu.exec(trackWidget_->viewport()->mapToGlobal(pos));
        if (result == cueAction) {
            emit cuePreviewRequested(path);
        } else if (result == removeAction) {
            onRemoveTrack();
        } else if (result == pisSpecific) {
            emit pisadorAssignRequested(path, "specific");
        } else if (result == pisRandom) {
            emit pisadorAssignRequested(path, "random");
        } else if (result == pisTime) {
            emit pisadorAssignRequested(path, "time");
        } else if (result == pisNone) {
            emit pisadorAssignRequested(path, "none");
        } else if (result == pisRemove) {
            emit pisadorAssignRequested(path, "remove");
        }
    });

    trackCountLabel_ = new QLabel();
    trackCountLabel_->setStyleSheet("font-size: 10px;");
    rightLayout->addWidget(trackCountLabel_);

    columns->addWidget(rightGroup, 5);

    layout->addLayout(columns, 1);

    // ── Botones de acción (pie) ──────────────────────
    auto* actionRow = new QHBoxLayout();

    auto* loadBtn = new QPushButton(QIcon(":/icons/play.svg"), " Cargar en cola");
    loadBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #22c55e, stop:1 #16a34a); color: white; border: none; "
        "border-radius: 6px; font-weight: 600; padding: 8px 16px; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #34d369, stop:1 #22c55e); }");
    connect(loadBtn, &QPushButton::clicked, this, &ListManager::onLoad);

    actionRow->addWidget(loadBtn);
    actionRow->addStretch();

    auto* closeBtn = new QPushButton(tr("Cerrar"));
    closeBtn->setProperty("class", "secondaryButton");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    actionRow->addWidget(closeBtn);

    layout->addLayout(actionRow);
}

// ══════════════════════════════════════════════════════════
// Acciones de listas
// ══════════════════════════════════════════════════════════

void ListManager::onNew()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Nueva lista"),
        tr("Nombre de la lista:"), QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QString id = createList(name.trimmed());
    if (!id.isEmpty()) {
        refreshLists();
        // Seleccionar la nueva
        for (int i = 0; i < listWidget_->count(); ++i) {
            if (listWidget_->item(i)->data(Qt::UserRole).toString() == id) {
                listWidget_->setCurrentRow(i);
                break;
            }
        }
    }
}

void ListManager::onRename()
{
    QString id = currentListId();
    if (id.isEmpty()) return;

    auto* item = listWidget_->currentItem();
    QString oldName = item->text().split("  ").first();

    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Renombrar lista"),
        tr("Nuevo nombre:"), QLineEdit::Normal, oldName, &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    db_->execPrepared("UPDATE saved_lists SET name = ? WHERE id = ?",
                      {name.trimmed(), id});
    refreshLists();
}

void ListManager::onDelete()
{
    QString id = currentListId();
    if (id.isEmpty()) return;

    auto* item = listWidget_->currentItem();
    auto confirm = QMessageBox::question(this, tr("Eliminar lista"),
        QString("¿Eliminar \"%1\"?").arg(item->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm == QMessageBox::Yes) {
        db_->execPrepared("DELETE FROM saved_lists WHERE id = ?", {id});
        refreshLists();
        trackWidget_->clear();
        trackCountLabel_->clear();
    }
}

void ListManager::onLoad()
{
    QString id = currentListId();
    if (id.isEmpty()) return;

    selectedTracks_ = getListTracks(id);
    if (selectedTracks_.isEmpty()) {
        QMessageBox::information(this, tr("Lista vacía"),
            tr("Esta lista no tiene pistas."));
        return;
    }

    auto* item = listWidget_->currentItem();
    selectedListName_ = item->text().split("  ").first();
    loaded_ = true;
    accept();
}

void ListManager::onListSelected()
{
    refreshTracks();
}

// ══════════════════════════════════════════════════════════
// Acciones de tracks
// ══════════════════════════════════════════════════════════

void ListManager::onAddTracks()
{
    QString id = currentListId();
    if (id.isEmpty()) {
        QMessageBox::information(this, tr("Sin lista"),
            tr("Seleccione o cree una lista primero."));
        return;
    }

    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Agregar archivos"), QString(),
        "Audio (*.mp3 *.ogg *.flac *.wav *.aac *.m4a *.opus);;Todos (*)");
    if (files.isEmpty()) return;

    QStringList current = getListTracks(id);
    current.append(files);
    setListTracks(id, current);
    refreshTracks();
    refreshLists();  // Actualizar conteo
}

void ListManager::onRemoveTrack()
{
    QString id = currentListId();
    if (id.isEmpty()) return;

    int row = trackWidget_->currentRow();
    if (row < 0) return;

    QStringList tracks = getListTracks(id);
    if (row < tracks.size()) {
        tracks.removeAt(row);
        setListTracks(id, tracks);
        refreshTracks();
        refreshLists();
    }
}

void ListManager::onMoveUp()
{
    QString id = currentListId();
    if (id.isEmpty()) return;

    int row = trackWidget_->currentRow();
    if (row <= 0) return;

    QStringList tracks = getListTracks(id);
    if (row < tracks.size()) {
        tracks.swapItemsAt(row, row - 1);
        setListTracks(id, tracks);
        refreshTracks();
        trackWidget_->setCurrentRow(row - 1);
    }
}

void ListManager::onMoveDown()
{
    QString id = currentListId();
    if (id.isEmpty()) return;

    int row = trackWidget_->currentRow();
    QStringList tracks = getListTracks(id);
    if (row < 0 || row >= tracks.size() - 1) return;

    tracks.swapItemsAt(row, row + 1);
    setListTracks(id, tracks);
    refreshTracks();
    trackWidget_->setCurrentRow(row + 1);
}

// ══════════════════════════════════════════════════════════
// Refresh
// ══════════════════════════════════════════════════════════

void ListManager::refreshLists()
{
    QString prevId = currentListId();
    listWidget_->clear();

    auto q = db_->execPrepared(
        "SELECT sl.id, sl.name, COUNT(slt.id) as track_count "
        "FROM saved_lists sl "
        "LEFT JOIN saved_list_tracks slt ON sl.id = slt.list_id GROUP BY sl.id ORDER BY sl.name", {});

    int count = 0;
    int selectRow = -1;
    if (q) {
        while (q->next()) {
            QString id = q->value(0).toString();
            QString name = q->value(1).toString();
            int tracks = q->value(2).toInt();

            QString label = QString("%1  (%2 pistas)").arg(name).arg(tracks);
            auto* item = new QListWidgetItem(QIcon(":/icons/list.svg"), label);
            item->setData(Qt::UserRole, id);
            listWidget_->addItem(item);

            if (id == prevId) selectRow = count;
            ++count;
        }
    }

    listCountLabel_->setText(QString("%1 lista(s)").arg(count));

    if (selectRow >= 0) {
        listWidget_->setCurrentRow(selectRow);
    } else if (count > 0) {
        listWidget_->setCurrentRow(0);
    }
}

void ListManager::refreshTracks()
{
    trackWidget_->clear();
    trackCountLabel_->clear();

    QString id = currentListId();
    if (id.isEmpty()) return;

    QStringList tracks = getListTracks(id);
    int64_t totalDurationMs = 0;

    for (int i = 0; i < tracks.size(); ++i) {
        QFileInfo fi(tracks[i]);
        QString label = QString("%1. %2").arg(i + 1).arg(fi.completeBaseName());

        // Obtener duración si hay MetadataReader
        if (reader_) {
            int64_t durMs = reader_->getDurationMs(tracks[i]);
            if (durMs > 0) {
                totalDurationMs += durMs;
                label += QString("  [%1]").arg(formatDuration(durMs));
            }
        }

        auto* item = new QListWidgetItem(QIcon(":/icons/music.svg"), label);
        item->setToolTip(tracks[i]);
        item->setData(Qt::UserRole, tracks[i]);
        trackWidget_->addItem(item);
    }

    QString info = QString("%1 pista(s)").arg(tracks.size());
    if (totalDurationMs > 0) {
        info += QString("  —  Duración total: %1").arg(formatDuration(totalDurationMs));
    }
    trackCountLabel_->setText(info);
}

// ══════════════════════════════════════════════════════════
// CRUD helpers
// ══════════════════════════════════════════════════════════

QString ListManager::currentListId() const
{
    auto* item = listWidget_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString ListManager::createList(const QString& name)
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    auto q = db_->execPrepared(
        "INSERT INTO saved_lists (id, name) VALUES (?, ?)", {id, name});
    return q ? id : QString();
}

bool ListManager::setListTracks(const QString& listId, const QStringList& tracks)
{
    return db_->transaction([&]() {
        db_->execPrepared(
            "DELETE FROM saved_list_tracks WHERE list_id = ?", {listId});
        for (int i = 0; i < tracks.size(); ++i) {
            db_->execPrepared(
                "INSERT INTO saved_list_tracks (list_id, position, file_path, display_name) "
                "VALUES (?, ?, ?, ?)",
                {listId, i, tracks[i], QFileInfo(tracks[i]).completeBaseName()});
        }
        return true;
    });
}

QStringList ListManager::getListTracks(const QString& listId)
{
    QStringList result;
    auto q = db_->execPrepared(
        "SELECT file_path FROM saved_list_tracks "
        "WHERE list_id = ? ORDER BY position", {listId});
    if (q) {
        while (q->next()) result << q->value(0).toString();
    }
    return result;
}

void ListManager::syncTracksAfterDrop()
{
    QString id = currentListId();
    if (id.isEmpty()) return;

    // Leer el orden visual actual del QListWidget
    QStringList reorderedTracks;
    for (int i = 0; i < trackWidget_->count(); ++i) {
        auto* item = trackWidget_->item(i);
        reorderedTracks << item->data(Qt::UserRole).toString();
    }

    // Guardar en BD
    setListTracks(id, reorderedTracks);

    // Refrescar para actualizar números y duración
    refreshTracks();
    refreshLists();  // Actualizar conteo en la lista izquierda
}

QString ListManager::formatDuration(int64_t ms) const
{
    if (ms <= 0) return "0:00";
    int64_t totalSecs = ms / 1000;
    int hours = static_cast<int>(totalSecs / 3600);
    int mins = static_cast<int>((totalSecs % 3600) / 60);
    int secs = static_cast<int>(totalSecs % 60);

    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours)
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    }
    return QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
}

} // namespace sara
