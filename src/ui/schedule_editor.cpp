#include "ui/schedule_editor.h"
#include "data/schedule_repository.h"
#include "util/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QIcon>
#include <QFileInfo>
#include <QShortcut>
#include <QKeySequence>

namespace sara {

// ── Constructores ────────────────────────────────────────

ScheduleEditor::ScheduleEditor(ScheduleRepository* repo, QWidget* parent)
    : QDialog(parent)
    , repo_(repo)
    , isNew_(true)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle(tr("Nueva Programación"));
    setMinimumSize(560, 480);
    setupUI();
}

ScheduleEditor::ScheduleEditor(ScheduleRepository* repo, const QString& scheduleId,
                                 QWidget* parent)
    : QDialog(parent)
    , repo_(repo)
    , scheduleId_(scheduleId)
    , isNew_(false)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle(tr("Editar Programación"));
    setMinimumSize(560, 480);
    setupUI();
    loadSchedule();
}

// ── UI ───────────────────────────────────────────────────

void ScheduleEditor::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 16, 20, 16);

    // Nombre
    auto* nameRow = new QHBoxLayout();
    auto* nameLabel = new QLabel(tr("Nombre:"));
    nameLabel->setStyleSheet("font-weight: 600;");
    nameEdit_ = new QLineEdit();
    nameEdit_->setPlaceholderText(tr("Ej: Mañanas de salsa"));
    nameEdit_->setMinimumHeight(32);
    nameRow->addWidget(nameLabel);
    nameRow->addWidget(nameEdit_, 1);
    mainLayout->addLayout(nameRow);

    // Descripción
    auto* descLabel = new QLabel(
        tr("Defina los elementos que componen esta programación. Se reproducirán en ciclo durante el tiempo asignado.")
    );
    descLabel->setStyleSheet("font-size: 12px;");
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Contenido principal: lista + botones laterales
    auto* contentRow = new QHBoxLayout();

    // Lista de elementos
    elementList_ = new QListWidget();
    elementList_->setDragDropMode(QAbstractItemView::InternalMove);
    elementList_->setMinimumHeight(200);

    // Tecla Suprimir para quitar elemento seleccionado
    auto* delShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), elementList_);
    delShortcut->setContext(Qt::WidgetShortcut);
    connect(delShortcut, &QShortcut::activated, this, &ScheduleEditor::removeElement);

    contentRow->addWidget(elementList_, 1);

    // Botones laterales
    auto* sideButtons = new QVBoxLayout();
    sideButtons->setSpacing(6);

    addFolderBtn_ = new QPushButton(QIcon(":/icons/folder.svg"), " Carpeta");
    addFolderBtn_->setToolTip(tr("Agregar carpeta (selección aleatoria)"));

    addFileBtn_ = new QPushButton(QIcon(":/icons/music.svg"), " Archivo");
    addFileBtn_->setToolTip(tr("Agregar archivo específico"));

    addTimeBtn_ = new QPushButton(QIcon(":/icons/clock.svg"), " Hora");
    addTimeBtn_->setToolTip(tr("Agregar locución de hora"));

    auto* sep1 = new QFrame();
    sep1->setFrameShape(QFrame::HLine);
    sep1->setStyleSheet("color: rgba(255,255,255,0.1);");

    moveUpBtn_ = new QPushButton(QIcon(":/icons/chevron-left.svg"), "");
    moveUpBtn_->setToolTip(tr("Mover arriba"));
    moveUpBtn_->setFixedSize(32, 32);
    moveUpBtn_->setProperty("class", "secondaryButton");

    moveDownBtn_ = new QPushButton(QIcon(":/icons/chevron-right.svg"), "");
    moveDownBtn_->setToolTip(tr("Mover abajo"));
    moveDownBtn_->setFixedSize(32, 32);
    moveDownBtn_->setProperty("class", "secondaryButton");

    removeBtn_ = new QPushButton(QIcon(":/icons/x.svg"), "");
    removeBtn_->setToolTip(tr("Eliminar elemento seleccionado"));
    removeBtn_->setFixedSize(32, 32);
    removeBtn_->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.15); color: #ef4444; "
        "border: 1px solid rgba(239,68,68,0.3); border-radius: 6px; }"
        "QPushButton:hover { background: rgba(239,68,68,0.25); }"
    );

    sideButtons->addWidget(addFolderBtn_);
    sideButtons->addWidget(addFileBtn_);
    sideButtons->addWidget(addTimeBtn_);
    sideButtons->addWidget(sep1);

    auto* moveRow = new QHBoxLayout();
    moveRow->addWidget(moveUpBtn_);
    moveRow->addWidget(moveDownBtn_);
    sideButtons->addLayout(moveRow);

    sideButtons->addWidget(removeBtn_);
    sideButtons->addStretch();
    contentRow->addLayout(sideButtons);

    mainLayout->addLayout(contentRow, 1);

    // Botones de acción
    auto* actionRow = new QHBoxLayout();
    actionRow->addStretch();

    cancelBtn_ = new QPushButton(tr("Cancelar"));
    cancelBtn_->setProperty("class", "secondaryButton");
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);

    saveBtn_ = new QPushButton(QIcon(":/icons/save.svg"), " Guardar");
    connect(saveBtn_, &QPushButton::clicked, this, &ScheduleEditor::onSave);

    actionRow->addWidget(cancelBtn_);
    actionRow->addWidget(saveBtn_);
    mainLayout->addLayout(actionRow);

    // Conexiones
    connect(addFolderBtn_, &QPushButton::clicked, this, &ScheduleEditor::addFolder);
    connect(addFileBtn_,   &QPushButton::clicked, this, &ScheduleEditor::addFile);
    connect(addTimeBtn_,   &QPushButton::clicked, this, &ScheduleEditor::addTimeAnnounce);
    connect(removeBtn_,    &QPushButton::clicked, this, &ScheduleEditor::removeElement);
    connect(moveUpBtn_,    &QPushButton::clicked, this, &ScheduleEditor::moveUp);
    connect(moveDownBtn_,  &QPushButton::clicked, this, &ScheduleEditor::moveDown);
}

void ScheduleEditor::loadSchedule()
{
    auto sched = repo_->getById(scheduleId_);
    if (!sched) return;

    nameEdit_->setText(sched->name);
    elements_ = sched->elements;
    refreshList();
}

// ── Agregar elementos ────────────────────────────────────

void ScheduleEditor::addFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Seleccionar carpeta de música"));
    if (dir.isEmpty()) return;

    ScheduleElement e;
    e.type = ElementType::Folder;
    e.path = dir;
    e.displayName = QFileInfo(dir).fileName();
    if (e.displayName.isEmpty()) e.displayName = dir;

    elements_.append(e);
    refreshList();
}

void ScheduleEditor::addFile()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Seleccionar archivo(s) de audio"), QString(),
        "Audio (*.mp3 *.ogg *.flac *.wav *.aac *.m4a *.opus);;Todos (*)"
    );

    for (const auto& file : files) {
        ScheduleElement e;
        e.type = ElementType::File;
        e.path = file;
        e.displayName = QFileInfo(file).completeBaseName();
        elements_.append(e);
    }

    refreshList();
}

void ScheduleEditor::addTimeAnnounce()
{
    ScheduleElement e;
    e.type = ElementType::TimeAnnounce;
    e.displayName = tr("Locución de hora");
    elements_.append(e);
    refreshList();
}

// ── Manipulación ─────────────────────────────────────────

void ScheduleEditor::removeElement()
{
    int row = elementList_->currentRow();
    if (row < 0 || row >= elements_.size()) return;

    elements_.removeAt(row);
    refreshList();
}

void ScheduleEditor::moveUp()
{
    int row = elementList_->currentRow();
    if (row <= 0) return;

    elements_.swapItemsAt(row, row - 1);
    refreshList();
    elementList_->setCurrentRow(row - 1);
}

void ScheduleEditor::moveDown()
{
    int row = elementList_->currentRow();
    if (row < 0 || row >= elements_.size() - 1) return;

    elements_.swapItemsAt(row, row + 1);
    refreshList();
    elementList_->setCurrentRow(row + 1);
}

// ── Guardar ──────────────────────────────────────────────

void ScheduleEditor::onSave()
{
    QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Nombre requerido"),
            tr("Ingrese un nombre para la programación."));
        nameEdit_->setFocus();
        return;
    }

    if (elements_.isEmpty()) {
        QMessageBox::warning(this, tr("Sin elementos"),
            tr("Agregue al menos un elemento a la programación."));
        return;
    }

    if (isNew_) {
        scheduleId_ = repo_->create(name);
        if (scheduleId_.isEmpty()) {
            QMessageBox::critical(this, tr("Error"), "No se pudo crear la programación.");
            return;
        }
    } else {
        repo_->rename(scheduleId_, name);
    }

    // Sincronizar el orden visual (drag & drop) con el vector interno
    syncElementsFromList();

    repo_->setElements(scheduleId_, elements_);

    LOG_INFO("[ScheduleEditor] Guardada: {} ({} elementos)",
             name.toStdString(), elements_.size());
    accept();
}

// ── Helpers ──────────────────────────────────────────────

void ScheduleEditor::refreshList()
{
    elementList_->clear();

    for (int i = 0; i < elements_.size(); ++i) {
        const auto& e = elements_[i];
        QString label = QString("%1. [%2] %3")
            .arg(i + 1)
            .arg(elementTypeLabel(e.type))
            .arg(e.displayName);

        auto* item = new QListWidgetItem(label);
        item->setToolTip(e.path.isEmpty() ? e.displayName : e.path);
        item->setData(Qt::UserRole, i);  // Índice original para sync de drag & drop

        // Icono según tipo
        QString iconName = elementIcon(e.type);
        if (!iconName.isEmpty()) {
            item->setIcon(QIcon(iconName));
        }

        elementList_->addItem(item);
    }
}

QString ScheduleEditor::elementIcon(ElementType type) const
{
    switch (type) {
        case ElementType::Folder:       return ":/icons/folder.svg";
        case ElementType::File:         return ":/icons/music.svg";
        case ElementType::TimeAnnounce: return ":/icons/clock.svg";
        case ElementType::Stream:       return ":/icons/radio.svg";
    }
    return {};
}

QString ScheduleEditor::elementTypeLabel(ElementType type) const
{
    switch (type) {
        case ElementType::Folder:       return tr("Carpeta");
        case ElementType::File:         return tr("Archivo");
        case ElementType::TimeAnnounce: return tr("Hora");
        case ElementType::Stream:       return tr("Stream");
    }
    return "?";
}

void ScheduleEditor::syncElementsFromList()
{
    QVector<ScheduleElement> reordered;
    for (int i = 0; i < elementList_->count(); ++i) {
        auto* item = elementList_->item(i);
        int origIndex = item->data(Qt::UserRole).toInt();
        if (origIndex >= 0 && origIndex < elements_.size()) {
            reordered.append(elements_[origIndex]);
        }
    }
    if (reordered.size() == elements_.size()) {
        elements_ = reordered;
    }
}

} // namespace sara
