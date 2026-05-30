#include "ui/event_editor.h"
#include "data/event_repository.h"
#include "data/stream_preset_repo.h"
#include "util/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QIcon>
#include <QFileInfo>
#include <QFrame>
#include <QSplitter>
#include <QShortcut>
#include <QKeySequence>
#include <QMenu>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QScrollArea>

namespace sara {

static const QStringList DAYS = {
    "Lunes", "Martes", "Miércoles", "Jueves",
    "Viernes", "Sábado", "Domingo"
};

EventEditor::EventEditor(EventRepository* repo, QWidget* parent)
    : QDialog(parent), repo_(repo), isNew_(true)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle(tr("Nuevo Evento / Publicidad"));
    setMinimumSize(880, 520);
    resize(920, 560);
    setupUI();
}

EventEditor::EventEditor(EventRepository* repo, const QString& eventId, QWidget* parent)
    : QDialog(parent), repo_(repo), eventId_(eventId), isNew_(false)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle(tr("Editar Evento / Publicidad"));
    setMinimumSize(880, 520);
    resize(920, 560);
    setupUI();
    loadEvent();
}

void EventEditor::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(16, 12, 16, 12);

    // ══ Layout horizontal: dos columnas ══════════════
    auto* columns = new QHBoxLayout();
    columns->setSpacing(12);

    // ── COLUMNA IZQUIERDA: Info + Opciones + Horarios (scrollable) ──
    auto* leftScroll = new QScrollArea();
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    auto* leftWidget = new QWidget();
    auto* leftCol = new QVBoxLayout(leftWidget);
    leftCol->setSpacing(8);

    // Info
    auto* infoGroup = new QGroupBox(tr("Información"));
    auto* infoLayout = new QFormLayout(infoGroup);
    infoLayout->setSpacing(6);

    nameEdit_ = new QLineEdit();
    nameEdit_->setPlaceholderText(tr("Ej: Publicidad Matutina..."));
    infoLayout->addRow(tr("Nombre:"), nameEdit_);

    prioritySpin_ = new QSpinBox();
    prioritySpin_->setRange(1, 10);
    prioritySpin_->setValue(5);
    prioritySpin_->setToolTip(
        tr("Resuelve conflictos cuando hay varios eventos a la misma hora.\nSe reproducen en orden: primero el de mayor prioridad (1)."));

    auto* priRow = new QHBoxLayout();
    priRow->setSpacing(8);
    priRow->addWidget(prioritySpin_);
    auto* priHelp = new QLabel(tr("1 = sale primero · 10 = sale último"));
    priHelp->setStyleSheet("font-size: 11px;");
    priRow->addWidget(priHelp);
    priRow->addStretch();
    infoLayout->addRow(tr("Prioridad:"), priRow);

    leftCol->addWidget(infoGroup);

    // Opciones
    auto* optGroup = new QGroupBox(tr("Opciones"));
    auto* optLayout = new QVBoxLayout(optGroup);
    optLayout->setSpacing(4);

    persistentCheck_ = new QCheckBox(tr("Persistente (obligatorio al aire)"));
    persistentCheck_->setChecked(true);
    persistentCheck_->setToolTip(
        tr("Si hubo corte de luz o modo manual, se reproduce al retomar automático."));

    immediateCheck_ = new QCheckBox(tr("Inmediato (crossfade)"));
    immediateCheck_->setToolTip(tr("Retardado = espera fin de canción. Inmediato = crossfade."));

    optLayout->addWidget(persistentCheck_);

    // Tiempo máximo de espera (solo visible cuando no es persistente)
    auto* waitRow = new QHBoxLayout();
    maxWaitCheck_ = new QCheckBox(tr("Tiempo máximo de espera:"));
    maxWaitCheck_->setToolTip(
        tr("Si la hora programada ya pasó, el evento se reproduce igualmente "
           "si no superó este tiempo. Si se supera, se marca como vencido."));
    maxWaitSpin_ = new QSpinBox();
    maxWaitSpin_->setRange(1, 120);
    maxWaitSpin_->setValue(15);
    maxWaitSpin_->setSuffix(tr(" minutos"));
    maxWaitSpin_->setFixedWidth(120);
    waitRow->addWidget(maxWaitCheck_);
    waitRow->addWidget(maxWaitSpin_);
    waitRow->addStretch();
    optLayout->addLayout(waitRow);

    optLayout->addWidget(immediateCheck_);

    // Habilitar/deshabilitar según persistente
    auto updateWaitVisibility = [this]() {
        bool showWait = !persistentCheck_->isChecked();
        maxWaitCheck_->setVisible(showWait);
        maxWaitSpin_->setVisible(showWait);
        if (!showWait) {
            maxWaitCheck_->setChecked(false);
        }
        maxWaitSpin_->setEnabled(maxWaitCheck_->isChecked());
    };
    connect(persistentCheck_, &QCheckBox::toggled, this, updateWaitVisibility);
    connect(maxWaitCheck_, &QCheckBox::toggled, this, [this]() {
        maxWaitSpin_->setEnabled(maxWaitCheck_->isChecked());
    });
    updateWaitVisibility();

    adAnnounceCheck_ = new QCheckBox(tr("Incluir locución inicio/fin de espacio publicitario"));
    adAnnounceCheck_->setToolTip(
        tr("Al inicio del bloque se reproducirá el audio de apertura,\ny al final el de cierre. Configure los archivos en Configuración."));
    optLayout->addWidget(adAnnounceCheck_);

    // Vigencia
    auto* dateRow = new QHBoxLayout();
    blockDatesCheck_ = new QCheckBox(tr("Vigencia:"));

    blockFromDate_ = new QDateEdit(QDate::currentDate());
    blockFromDate_->setCalendarPopup(true);
    blockFromDate_->setDisplayFormat("dd/MM/yy");
    blockFromDate_->setEnabled(false);
    blockFromDate_->setFixedWidth(95);

    blockUntilDate_ = new QDateEdit(QDate::currentDate().addMonths(1));
    blockUntilDate_->setCalendarPopup(true);
    blockUntilDate_->setDisplayFormat("dd/MM/yy");
    blockUntilDate_->setEnabled(false);
    blockUntilDate_->setFixedWidth(95);

    connect(blockDatesCheck_, &QCheckBox::toggled, blockFromDate_, &QDateEdit::setEnabled);
    connect(blockDatesCheck_, &QCheckBox::toggled, blockUntilDate_, &QDateEdit::setEnabled);

    dateRow->addWidget(blockDatesCheck_);
    dateRow->addWidget(blockFromDate_);
    dateRow->addWidget(new QLabel("→"));
    dateRow->addWidget(blockUntilDate_);
    dateRow->addStretch();
    optLayout->addLayout(dateRow);

    leftCol->addWidget(optGroup);

    // Programación horaria
    auto* schedGroup = new QGroupBox(tr("Horarios"));
    auto* schedLayout = new QVBoxLayout(schedGroup);
    schedLayout->setSpacing(4);

    auto* addSlotRow = new QHBoxLayout();
    addSlotRow->setSpacing(4);

    slotDayCombo_ = new QComboBox();
    for (const auto& d : DAYS) slotDayCombo_->addItem(d);
    slotDayCombo_->addItem(tr("L a V"));
    slotDayCombo_->addItem(tr("Semana"));
    slotDayCombo_->setFixedWidth(90);

    auto* selectHoursBtn = new QPushButton(QIcon(":/icons/clock.svg"), tr(" Horas..."));
    selectHoursBtn->setFixedHeight(28);
    selectHoursBtn->setToolTip(tr("Seleccionar horas para agregar"));
    connect(selectHoursBtn, &QPushButton::clicked, this, &EventEditor::openHourSelector);

    auto* editSlotBtn = new QPushButton(QIcon(":/icons/settings.svg"), "");
    editSlotBtn->setFixedSize(28, 28);
    editSlotBtn->setToolTip(tr("Editar horario seleccionado"));
    connect(editSlotBtn, &QPushButton::clicked, this, [this]() {
        int row = slotList_->currentRow();
        if (row >= 0 && row < eventSlots_.size()) editSlot(row);
    });

    auto* removeSlotBtn = new QPushButton(QIcon(":/icons/x.svg"), "");
    removeSlotBtn->setFixedSize(28, 28);
    removeSlotBtn->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.15); color: #ef4444; "
        "border: 1px solid rgba(239,68,68,0.3); border-radius: 6px; }"
        "QPushButton:hover { background: rgba(239,68,68,0.25); }"
    );
    connect(removeSlotBtn, &QPushButton::clicked, this, &EventEditor::removeSlotAction);

    addSlotRow->addWidget(slotDayCombo_);
    addSlotRow->addWidget(selectHoursBtn);
    addSlotRow->addWidget(editSlotBtn);
    addSlotRow->addWidget(removeSlotBtn);

    slotList_ = new QListWidget();
    slotList_->setMaximumHeight(120);

    // Doble clic para editar horario
    connect(slotList_, &QListWidget::itemDoubleClicked, this, [this]() {
        int row = slotList_->currentRow();
        if (row >= 0 && row < eventSlots_.size()) editSlot(row);
    });

    // Tecla Suprimir para quitar horario seleccionado
    auto* delSlotShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), slotList_);
    delSlotShortcut->setContext(Qt::WidgetShortcut);
    connect(delSlotShortcut, &QShortcut::activated, this, &EventEditor::removeSlotAction);

    schedLayout->addLayout(addSlotRow);
    schedLayout->addWidget(slotList_, 1);

    leftCol->addWidget(schedGroup, 1);

    // ── COLUMNA DERECHA: Contenido del bloque ────────
    auto* rightCol = new QVBoxLayout();
    rightCol->setSpacing(8);

    auto* audioGroup = new QGroupBox(tr("Contenido del Bloque"));
    auto* audioLayout = new QVBoxLayout(audioGroup);
    audioLayout->setSpacing(4);

    // Botones de agregar
    auto* audioBtnRow = new QHBoxLayout();
    audioBtnRow->setSpacing(4);

    auto* addFileBtn = new QPushButton(QIcon(":/icons/music.svg"), " Archivo");
    addFileBtn->setFixedHeight(26);
    auto* addFolderBtn = new QPushButton(QIcon(":/icons/folder.svg"), " Carpeta");
    addFolderBtn->setFixedHeight(26);
    auto* addTimeBtn = new QPushButton(QIcon(":/icons/clock.svg"), " Hora");
    addTimeBtn->setFixedHeight(26);
    auto* addStreamBtn = new QPushButton(QIcon(":/icons/radio.svg"), " Stream");
    addStreamBtn->setFixedHeight(26);

    connect(addFileBtn,   &QPushButton::clicked, this, &EventEditor::addFile);
    connect(addFolderBtn, &QPushButton::clicked, this, &EventEditor::addFolder);
    connect(addTimeBtn,   &QPushButton::clicked, this, &EventEditor::addTimeAnnounce);
    connect(addStreamBtn, &QPushButton::clicked, this, &EventEditor::addStream);

    audioBtnRow->addWidget(addFileBtn);
    audioBtnRow->addWidget(addFolderBtn);
    audioBtnRow->addWidget(addTimeBtn);
    audioBtnRow->addWidget(addStreamBtn);

    // Lista de elementos
    elementList_ = new QListWidget();
    elementList_->setDragDropMode(QAbstractItemView::InternalMove);
    connect(elementList_, &QListWidget::currentRowChanged, this, [this]() {
        // Resetear botón de preescucha al cambiar de elemento
        if (cuePlaying_) {
            emit cueStopRequested();
            cuePlaying_ = false;
            cueBtn_->setText(" Preescuchar");
            cueBtn_->setIcon(QIcon(":/icons/volume-2.svg"));
        }
    });

    // Tecla Suprimir para quitar elemento seleccionado
    auto* delElemShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), elementList_);
    delElemShortcut->setContext(Qt::WidgetShortcut);
    connect(delElemShortcut, &QShortcut::activated, this, &EventEditor::removeElement);

    // Doble clic para editar streams
    connect(elementList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < elements_.size() && elements_[idx].type == ElementType::Stream) {
            editStreamElement(idx);
        }
    });

    // Menú contextual en elementos
    elementList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(elementList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int row = elementList_->row(elementList_->itemAt(pos));
        if (row < 0 || row >= elements_.size()) return;

        int idx = elementList_->item(row)->data(Qt::UserRole).toInt();
        const auto& elem = elements_[idx];

        QMenu menu(this);

        if (elem.type == ElementType::Stream) {
            auto* editAction = menu.addAction(QIcon(":/icons/settings.svg"), "Editar streaming...");
            connect(editAction, &QAction::triggered, this, [this, idx]() {
                editStreamElement(idx);
            });
            menu.addSeparator();
        }

        auto* removeAction = menu.addAction(QIcon(":/icons/x.svg"), "Quitar");
        connect(removeAction, &QAction::triggered, this, [this, row]() {
            if (row >= 0 && row < elements_.size()) {
                elements_.removeAt(row);
                refreshElementList();
            }
        });

        menu.exec(elementList_->viewport()->mapToGlobal(pos));
    });

    // Botones de gestión de elementos
    auto* elemActionRow = new QHBoxLayout();
    auto* editDatesBtn = new QPushButton(QIcon(":/icons/clock.svg"), " Vigencia");
    editDatesBtn->setProperty("class", "secondaryButton");
    editDatesBtn->setFixedHeight(26);
    editDatesBtn->setToolTip(tr("Editar fechas de vigencia del audio seleccionado"));
    connect(editDatesBtn, &QPushButton::clicked, this, &EventEditor::editElementDates);

    cueBtn_ = new QPushButton(QIcon(":/icons/volume-2.svg"), " Preescuchar");
    cueBtn_->setFixedHeight(26);
    cueBtn_->setProperty("class", "secondaryButton");
    cueBtn_->setToolTip(tr("Preescuchar el audio seleccionado por la tarjeta CUE"));
    connect(cueBtn_, &QPushButton::clicked, this, [this]() {
        int row = elementList_->currentRow();
        if (row < 0 || row >= elements_.size()) return;
        const auto& elem = elements_[row];
        if (elem.path.isEmpty()) return;

        if (cuePlaying_) {
            // Stop
            emit cueStopRequested();
            cuePlaying_ = false;
            cueBtn_->setText(" Preescuchar");
            cueBtn_->setIcon(QIcon(":/icons/volume-2.svg"));
        } else {
            // Play
            emit cuePreviewRequested(elem.path);
            cuePlaying_ = true;
            cueBtn_->setText(" Detener");
            cueBtn_->setIcon(QIcon(":/icons/square.svg"));
        }
    });

    auto* removeBtn = new QPushButton(QIcon(":/icons/x.svg"), " Quitar");
    removeBtn->setFixedHeight(26);
    removeBtn->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.15); color: #ef4444; "
        "border: 1px solid rgba(239,68,68,0.3); border-radius: 6px; "
        "font-weight: 600; padding: 4px 10px; }"
        "QPushButton:hover { background: rgba(239,68,68,0.25); }"
    );
    connect(removeBtn, &QPushButton::clicked, this, &EventEditor::removeElement);

    auto* removeAllBtn = new QPushButton(QIcon(":/icons/x.svg"), " Quitar de todos");
    removeAllBtn->setFixedHeight(26);
    removeAllBtn->setToolTip(tr("Quitar este audio de TODOS los eventos donde esté programado"));
    removeAllBtn->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.1); color: #f87171; "
        "border: 1px solid rgba(239,68,68,0.2); border-radius: 6px; "
        "font-weight: 600; padding: 4px 10px; font-size: 11px; }"
        "QPushButton:hover { background: rgba(239,68,68,0.2); }"
    );
    connect(removeAllBtn, &QPushButton::clicked, this, &EventEditor::removeFromAllEvents);

    elemActionRow->addWidget(editDatesBtn);
    elemActionRow->addWidget(cueBtn_);
    elemActionRow->addStretch();
    elemActionRow->addWidget(removeAllBtn);
    elemActionRow->addWidget(removeBtn);

    audioLayout->addLayout(audioBtnRow);
    audioLayout->addWidget(elementList_, 1);
    audioLayout->addLayout(elemActionRow);

    rightCol->addWidget(audioGroup, 1);

    // ══ Unir columnas ════════════════════════════════
    leftScroll->setWidget(leftWidget);
    columns->addWidget(leftScroll, 4);
    columns->addLayout(rightCol, 6);

    mainLayout->addLayout(columns, 1);

    // ══ Botones de acción (pie) ══════════════════════
    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: rgba(0,0,0,0.1);");
    mainLayout->addWidget(sep);

    auto* actionRow = new QHBoxLayout();
    actionRow->addStretch();

    cancelBtn_ = new QPushButton(QIcon(":/icons/x.svg"), " Cancelar");
    cancelBtn_->setProperty("class", "secondaryButton");
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);

    saveBtn_ = new QPushButton(QIcon(":/icons/check.svg"), " Crear Bloque");
    connect(saveBtn_, &QPushButton::clicked, this, &EventEditor::onSave);

    actionRow->addWidget(saveBtn_);
    actionRow->addWidget(cancelBtn_);
    mainLayout->addLayout(actionRow);
}

void EventEditor::loadEvent()
{
    auto event = repo_->getById(eventId_);
    if (!event) return;

    nameEdit_->setText(event->name);
    prioritySpin_->setValue(event->priority);
    persistentCheck_->setChecked(event->persistent);
    immediateCheck_->setChecked(event->immediate);
    adAnnounceCheck_->setChecked(event->useAdAnnounce);

    // Tiempo de espera
    if (event->maxWaitMinutes > 0) {
        maxWaitCheck_->setChecked(true);
        maxWaitSpin_->setValue(event->maxWaitMinutes);
    } else {
        maxWaitCheck_->setChecked(false);
    }

    if (event->validFrom.isValid() || event->validUntil.isValid()) {
        blockDatesCheck_->setChecked(true);
        if (event->validFrom.isValid()) blockFromDate_->setDate(event->validFrom);
        if (event->validUntil.isValid()) blockUntilDate_->setDate(event->validUntil);
    }

    elements_ = event->elements;
    eventSlots_ = repo_->getSlots(eventId_);

    refreshElementList();
    refreshSlotList();
    saveBtn_->setText(" Guardar Cambios");
}

// ── Agregar elementos ────────────────────────────────────

void EventEditor::addFile()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Seleccionar archivo(s) de audio"), QString(),
        "Audio (*.mp3 *.ogg *.flac *.wav *.aac *.m4a *.opus);;Todos (*)"
    );
    for (const auto& file : files) {
        EventElement e;
        e.type = ElementType::File;
        e.path = file;
        e.displayName = QFileInfo(file).completeBaseName();
        elements_.append(e);
    }
    refreshElementList();
}

void EventEditor::addFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Seleccionar carpeta"));
    if (dir.isEmpty()) return;

    EventElement e;
    e.type = ElementType::Folder;
    e.path = dir;
    e.displayName = QFileInfo(dir).fileName();
    elements_.append(e);
    refreshElementList();
}

void EventEditor::addTimeAnnounce()
{
    EventElement e;
    e.type = ElementType::TimeAnnounce;
    e.displayName = tr("Locución de hora");
    elements_.append(e);
    refreshElementList();
}

void EventEditor::addStream()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Agregar Streaming"));
    dlg.setMinimumWidth(500);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel(tr("Agregar fuente de Streaming"));
    titleLabel->setStyleSheet("font-size: 14px; font-weight: 700;");
    layout->addWidget(titleLabel);

    auto* descLabel = new QLabel(
        tr("Ingrese la URL de un streaming Icecast o Shoutcast (HTTP/HTTPS). El streaming se reproducirá durante el tiempo indicado y luego SARA continuará con la programación."));
    descLabel->setStyleSheet("font-size: 11px;");
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // Presets guardados
    QComboBox* presetCombo = nullptr;
    if (streamRepo_) {
        auto presets = streamRepo_->getAll();
        if (!presets.isEmpty()) {
            auto* presetGroup = new QGroupBox(tr("Streams guardados"));
            auto* presetLayout = new QHBoxLayout(presetGroup);
            presetCombo = new QComboBox();
            presetCombo->addItem(tr("— Seleccionar un preset guardado —"), -1);
            for (const auto& p : presets) {
                presetCombo->addItem(
                    QString("%1 (%2)").arg(p.name, p.url), p.id);
            }
            presetLayout->addWidget(presetCombo, 1);
            layout->addWidget(presetGroup);
        }
    }

    // URL
    auto* urlGroup = new QGroupBox(tr("Dirección del streaming"));
    auto* urlLayout = new QVBoxLayout(urlGroup);

    auto* urlEdit = new QLineEdit();
    urlEdit->setPlaceholderText("http://stream.ejemplo.com:8000/radio  o  https://...");
    urlEdit->setMinimumHeight(30);
    urlLayout->addWidget(urlEdit);

    // Botón CUE preview
    auto* cueRow = new QHBoxLayout();
    auto* cueBtn = new QPushButton(QIcon(":/icons/volume-2.svg"), " Preescuchar (CUE)");
    cueBtn->setFixedHeight(28);
    cueBtn->setProperty("class", "secondaryButton");
    bool cuePlaying = false;

    connect(cueBtn, &QPushButton::clicked, this, [this, &dlg, &cuePlaying, cueBtn, urlEdit]() {
        QString url = urlEdit->text().trimmed();
        if (url.isEmpty()) return;

        if (cuePlaying) {
            emit cueStopRequested();
            cuePlaying = false;
            cueBtn->setText(" Preescuchar (CUE)");
            cueBtn->setIcon(QIcon(":/icons/volume-2.svg"));
        } else {
            emit cuePreviewRequested(url);
            cuePlaying = true;
            cueBtn->setText(" Detener preescucha");
            cueBtn->setIcon(QIcon(":/icons/square.svg"));
        }
    });

    auto* cueHelp = new QLabel(tr("Pruebe la conexión antes de agregar"));
    cueHelp->setStyleSheet("font-size: 10px;");
    cueRow->addWidget(cueBtn);
    cueRow->addWidget(cueHelp);
    cueRow->addStretch();
    urlLayout->addLayout(cueRow);

    layout->addWidget(urlGroup);

    // Nombre y duración
    auto* infoGroup = new QGroupBox(tr("Configuración"));
    auto* infoForm = new QFormLayout(infoGroup);
    infoForm->setSpacing(8);

    auto* nameEdit = new QLineEdit(tr("Streaming"));
    nameEdit->setPlaceholderText(tr("Nombre para identificar en la lista"));
    infoForm->addRow(tr("Nombre:"), nameEdit);

    // Conectar preset combo si existe
    if (presetCombo && streamRepo_) {
        connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                &dlg, [presetCombo, urlEdit, nameEdit, this](int idx) {
            int id = presetCombo->itemData(idx).toInt();
            if (id > 0 && streamRepo_) {
                auto preset = streamRepo_->getById(id);
                urlEdit->setText(preset.url);
                nameEdit->setText(preset.name);
            }
        });
    }

    // Duración
    auto* durRow = new QHBoxLayout();
    auto* durMinSpin = new QSpinBox();
    durMinSpin->setRange(0, 480);  // hasta 8 horas
    durMinSpin->setValue(30);
    durMinSpin->setSuffix(" min");
    durMinSpin->setFixedWidth(90);

    auto* durSecSpin = new QSpinBox();
    durSecSpin->setRange(0, 59);
    durSecSpin->setValue(0);
    durSecSpin->setSuffix(" seg");
    durSecSpin->setFixedWidth(90);

    durRow->addWidget(durMinSpin);
    durRow->addWidget(durSecSpin);
    durRow->addStretch();
    infoForm->addRow(tr("Duración:"), durRow);

    auto* durHelp = new QLabel(
        tr("Tiempo que el streaming permanecerá al aire. Al cumplirse, SARA retoma la programación automática."));
    durHelp->setStyleSheet("font-size: 10px;");
    durHelp->setWordWrap(true);
    infoForm->addRow("", durHelp);

    layout->addWidget(infoGroup);

    // Botones
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(tr("Cancelar"));
    cancelBtn->setProperty("class", "secondaryButton");
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    auto* addBtn = new QPushButton(QIcon(":/icons/plus.svg"), " Agregar");
    connect(addBtn, &QPushButton::clicked, &dlg, [&dlg, urlEdit]() {
        if (urlEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, tr("URL requerida"),
                tr("Ingrese la dirección del streaming."));
            return;
        }
        dlg.accept();
    });

    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(addBtn);
    layout->addLayout(btnRow);

    // Ejecutar diálogo
    if (dlg.exec() != QDialog::Accepted) {
        // Detener CUE si estaba sonando
        if (cuePlaying) emit cueStopRequested();
        return;
    }

    // Detener CUE
    if (cuePlaying) emit cueStopRequested();

    // Calcular duración en ms
    int64_t durationMs = (durMinSpin->value() * 60 + durSecSpin->value()) * 1000LL;

    EventElement e;
    e.type = ElementType::Stream;
    e.path = urlEdit->text().trimmed();
    // Auto-completar protocolo si falta
    if (!e.path.contains("://")) {
        e.path = "http://" + e.path;
    }
    e.displayName = nameEdit->text().trimmed().isEmpty() ? tr("Streaming") : nameEdit->text().trimmed();
    e.durationMs = durationMs;
    elements_.append(e);

    // Guardar como preset si es nuevo
    if (streamRepo_) {
        streamRepo_->add(e.displayName, e.path);
    }

    refreshElementList();
}

void EventEditor::editStreamElement(int index)
{
    if (index < 0 || index >= elements_.size()) return;
    auto& elem = elements_[index];
    if (elem.type != ElementType::Stream) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Editar Streaming"));
    dlg.setMinimumWidth(500);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Editar Streaming"));
    title->setStyleSheet("font-size: 14px; font-weight: 700;");
    layout->addWidget(title);

    // URL
    auto* urlGroup = new QGroupBox(tr("Dirección del streaming"));
    auto* urlLayout = new QVBoxLayout(urlGroup);

    auto* urlEdit = new QLineEdit(elem.path);
    urlEdit->setMinimumHeight(30);
    urlLayout->addWidget(urlEdit);

    // CUE preview
    auto* cueRow = new QHBoxLayout();
    auto* cueBtn = new QPushButton(QIcon(":/icons/volume-2.svg"), " Preescuchar (CUE)");
    cueBtn->setFixedHeight(28);
    cueBtn->setProperty("class", "secondaryButton");
    bool cuePlaying = false;

    connect(cueBtn, &QPushButton::clicked, this, [this, &cuePlaying, cueBtn, urlEdit]() {
        QString url = urlEdit->text().trimmed();
        if (url.isEmpty()) return;
        if (!url.contains("://")) url = "http://" + url;

        if (cuePlaying) {
            emit cueStopRequested();
            cuePlaying = false;
            cueBtn->setText(" Preescuchar (CUE)");
            cueBtn->setIcon(QIcon(":/icons/volume-2.svg"));
        } else {
            emit cuePreviewRequested(url);
            cuePlaying = true;
            cueBtn->setText(" Detener preescucha");
            cueBtn->setIcon(QIcon(":/icons/square.svg"));
        }
    });

    auto* cueHelp = new QLabel(tr("Pruebe la conexión antes de guardar"));
    cueHelp->setStyleSheet("font-size: 10px;");
    cueRow->addWidget(cueBtn);
    cueRow->addWidget(cueHelp);
    cueRow->addStretch();
    urlLayout->addLayout(cueRow);
    layout->addWidget(urlGroup);

    // Nombre y duración
    auto* infoGroup = new QGroupBox(tr("Configuración"));
    auto* infoForm = new QFormLayout(infoGroup);
    infoForm->setSpacing(8);

    auto* nameEdit = new QLineEdit(elem.displayName);
    infoForm->addRow(tr("Nombre:"), nameEdit);

    int totalSecs = static_cast<int>(elem.durationMs / 1000);
    auto* durRow = new QHBoxLayout();
    auto* durMinSpin = new QSpinBox();
    durMinSpin->setRange(0, 480);
    durMinSpin->setValue(totalSecs / 60);
    durMinSpin->setSuffix(" min");
    durMinSpin->setFixedWidth(90);

    auto* durSecSpin = new QSpinBox();
    durSecSpin->setRange(0, 59);
    durSecSpin->setValue(totalSecs % 60);
    durSecSpin->setSuffix(" seg");
    durSecSpin->setFixedWidth(90);

    durRow->addWidget(durMinSpin);
    durRow->addWidget(durSecSpin);
    durRow->addStretch();
    infoForm->addRow(tr("Duración:"), durRow);
    layout->addWidget(infoGroup);

    // Botones
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(tr("Cancelar"));
    cancelBtn->setProperty("class", "secondaryButton");
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    auto* saveBtn = new QPushButton(QIcon(":/icons/save.svg"), " Guardar");
    connect(saveBtn, &QPushButton::clicked, &dlg, [&dlg, urlEdit]() {
        if (urlEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, tr("URL requerida"), "Ingrese la dirección del streaming.");
            return;
        }
        dlg.accept();
    });

    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    layout->addLayout(btnRow);

    if (dlg.exec() != QDialog::Accepted) {
        if (cuePlaying) emit cueStopRequested();
        return;
    }
    if (cuePlaying) emit cueStopRequested();

    // Actualizar elemento
    elem.path = urlEdit->text().trimmed();
    if (!elem.path.contains("://")) elem.path = "http://" + elem.path;
    elem.displayName = nameEdit->text().trimmed().isEmpty() ? tr("Streaming") : nameEdit->text().trimmed();
    elem.durationMs = (durMinSpin->value() * 60 + durSecSpin->value()) * 1000LL;
    refreshElementList();
}

void EventEditor::removeElement()
{
    int row = elementList_->currentRow();
    if (row >= 0 && row < elements_.size()) {
        elements_.removeAt(row);
        refreshElementList();
    }
}

void EventEditor::removeFromAllEvents()
{
    int row = elementList_->currentRow();
    if (row < 0 || row >= elements_.size()) return;

    const auto& elem = elements_[row];
    if (elem.path.isEmpty()) {
        QMessageBox::information(this, tr("No aplicable"),
            tr("Esta función solo aplica a archivos con ruta definida."));
        return;
    }

    // Buscar en cuántos eventos está este archivo
    auto eventsWithFile = repo_->findEventsContaining(elem.path);
    if (eventsWithFile.isEmpty()) {
        QMessageBox::information(this, tr("Sin coincidencias"),
            tr("Este archivo no se encontró en ningún otro evento."));
        return;
    }

    // Listar los eventos afectados
    QStringList eventNames;
    QStringList eventIds;
    for (const auto& ev : eventsWithFile) {
        eventNames << QString("• %1").arg(ev.name);
        eventIds << ev.id;
    }

    auto confirm = QMessageBox::question(
        this, tr("Quitar de todos los eventos"),
        QString(tr("¿Quitar \")%1\" de %2 evento(s)?\n\n%3\n\nEsta acción no se puede deshacer."))
            .arg(elem.displayName)
            .arg(eventsWithFile.size())
            .arg(eventNames.join("\n")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (confirm != QMessageBox::Yes) return;

    repo_->removeElementFromEvents(eventIds, elem.path);

    // También quitar del listado local
    elements_.removeAt(row);
    refreshElementList();

    LOG_INFO("[EventEditor] '{}' quitado de {} evento(s)",
             elem.displayName.toStdString(), eventsWithFile.size());

    QMessageBox::information(this, tr("Completado"),
        tr("Se quitó \"%1\" de %2 evento(s).")
            .arg(elem.displayName).arg(eventsWithFile.size()));
}

void EventEditor::editElementDates()
{
    int row = elementList_->currentRow();
    if (row < 0 || row >= elements_.size()) return;

    auto& elem = elements_[row];

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Vigencia: ") + elem.displayName);
    dlg.setMinimumWidth(340);

    auto* lay = new QVBoxLayout(&dlg);
    lay->setSpacing(8);

    auto* desc = new QLabel(
        tr("Fuera de estas fechas, el audio no se reproducirá."));
    desc->setStyleSheet("font-size: 12px;");
    desc->setWordWrap(true);
    lay->addWidget(desc);

    auto* enableCheck = new QCheckBox(tr("Activar vigencia"));
    enableCheck->setChecked(elem.validFrom.isValid() || elem.validUntil.isValid());
    lay->addWidget(enableCheck);

    auto* fromDate = new QDateEdit(
        elem.validFrom.isValid() ? elem.validFrom : QDate::currentDate());
    fromDate->setCalendarPopup(true);
    fromDate->setDisplayFormat("dd/MM/yyyy");
    fromDate->setEnabled(enableCheck->isChecked());

    auto* untilDate = new QDateEdit(
        elem.validUntil.isValid() ? elem.validUntil : QDate::currentDate().addMonths(1));
    untilDate->setCalendarPopup(true);
    untilDate->setDisplayFormat("dd/MM/yyyy");
    untilDate->setEnabled(enableCheck->isChecked());

    connect(enableCheck, &QCheckBox::toggled, fromDate, &QDateEdit::setEnabled);
    connect(enableCheck, &QCheckBox::toggled, untilDate, &QDateEdit::setEnabled);

    auto* form = new QFormLayout();
    form->addRow(tr("Desde:"), fromDate);
    form->addRow(tr("Hasta:"), untilDate);
    lay->addLayout(form);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* okBtn = new QPushButton(tr("Aceptar"));
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* cBtn = new QPushButton(tr("Cancelar"));
    cBtn->setProperty("class", "secondaryButton");
    connect(cBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnRow->addWidget(okBtn);
    btnRow->addWidget(cBtn);
    lay->addLayout(btnRow);

    if (dlg.exec() == QDialog::Accepted) {
        if (enableCheck->isChecked()) {
            elem.validFrom = fromDate->date();
            elem.validUntil = untilDate->date();
        } else {
            elem.validFrom = QDate();
            elem.validUntil = QDate();
        }
        refreshElementList();
    }
}

// ── Slots horarios ───────────────────────────────────────

void EventEditor::openHourSelector()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Horas del evento"));
    dlg.setMinimumWidth(320);

    auto* layout = new QVBoxLayout(&dlg);

    auto* desc = new QLabel(tr("Seleccione las horas a las que se emitirá este evento:"));
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // Grilla 6x4 de checkboxes (0-23)
    auto* grid = new QGridLayout();
    grid->setSpacing(4);
    QVector<QCheckBox*> hourChecks(24);
    for (int h = 0; h < 24; ++h) {
        hourChecks[h] = new QCheckBox(QString("%1").arg(h, 2, 10, QChar('0')));
        int col = h / 8;  // 3 columnas de 8 horas
        int row = h % 8;
        grid->addWidget(hourChecks[h], row, col);
    }
    layout->addLayout(grid);

    // Botones Todas / Ninguna
    auto* selRow = new QHBoxLayout();
    auto* allBtn = new QPushButton(tr("Todas"));
    allBtn->setFixedHeight(26);
    connect(allBtn, &QPushButton::clicked, &dlg, [&hourChecks]() {
        for (auto* cb : hourChecks) cb->setChecked(true);
    });
    auto* noneBtn = new QPushButton(tr("Ninguna"));
    noneBtn->setFixedHeight(26);
    connect(noneBtn, &QPushButton::clicked, &dlg, [&hourChecks]() {
        for (auto* cb : hourChecks) cb->setChecked(false);
    });
    selRow->addWidget(allBtn);
    selRow->addWidget(noneBtn);
    selRow->addStretch();
    layout->addLayout(selRow);

    // Minutos
    auto* minRow = new QHBoxLayout();
    minRow->addWidget(new QLabel(tr("Minutos:")));
    auto* minSpin = new QSpinBox();
    minSpin->setRange(0, 59);
    minSpin->setValue(0);
    minSpin->setSuffix(tr(" min"));
    minSpin->setFixedWidth(90);
    minRow->addWidget(minSpin);
    minRow->addStretch();
    layout->addLayout(minRow);

    // Botones
    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    // Recoger horas seleccionadas
    QVector<int> selectedHours;
    for (int h = 0; h < 24; ++h) {
        if (hourChecks[h]->isChecked()) selectedHours.append(h);
    }
    if (selectedHours.isEmpty()) return;

    int minutes = minSpin->value();

    // Obtener días
    QString dayOption = slotDayCombo_->currentText();
    QStringList targetDays;
    if (dayOption == tr("L a V")) {
        targetDays = DAYS.mid(0, 5);
    } else if (dayOption == tr("Semana")) {
        targetDays = DAYS;
    } else {
        targetDays << dayOption;
    }

    // Crear slots
    for (const auto& day : targetDays) {
        for (int hour : selectedHours) {
            QTime time(hour, minutes);
            bool exists = false;
            for (const auto& s : eventSlots_) {
                if (s.day == day && s.triggerTime == time) { exists = true; break; }
            }
            if (!exists) {
                EventSlot ns;
                ns.eventId = eventId_;
                ns.day = day;
                ns.triggerTime = time;
                ns.enabled = true;
                eventSlots_.append(ns);
            }
        }
    }
    refreshSlotList();
}

void EventEditor::addSlot()
{
    openHourSelector();
}

void EventEditor::editSlot(int row)
{
    if (row < 0 || row >= eventSlots_.size()) return;

    auto& slot = eventSlots_[row];

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Editar horario"));
    dlg.setMinimumWidth(300);

    auto* layout = new QFormLayout(&dlg);
    layout->setSpacing(8);

    auto* dayCombo = new QComboBox();
    for (const auto& d : DAYS) dayCombo->addItem(d);
    dayCombo->setCurrentText(slot.day);
    layout->addRow(tr("Día:"), dayCombo);

    auto* timeEdit = new QTimeEdit(slot.triggerTime);
    timeEdit->setDisplayFormat(tr("HH:mm"));
    layout->addRow(tr("Hora:"), timeEdit);

    auto* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(btns);

    if (dlg.exec() == QDialog::Accepted) {
        slot.day = dayCombo->currentText();
        slot.triggerTime = timeEdit->time();
        refreshSlotList();
    }
}

void EventEditor::removeSlotAction()
{
    int row = slotList_->currentRow();
    if (row >= 0 && row < eventSlots_.size()) {
        eventSlots_.removeAt(row);
        refreshSlotList();
    }
}

// ── Guardar ──────────────────────────────────────────────

void EventEditor::onSave()
{
    QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Nombre requerido"),
            tr("Ingrese un nombre para el evento."));
        nameEdit_->setFocus();
        return;
    }

    if (eventSlots_.isEmpty()) {
        QMessageBox::warning(this, tr("Sin horario"),
            tr("Debe asignar al menos un día y hora al evento.\n\nUse el selector de día/hora y el botón + para agregar horarios."));
        return;
    }

    if (isNew_) {
        eventId_ = repo_->create(name);
        if (eventId_.isEmpty()) {
            QMessageBox::critical(this, tr("Error"), "No se pudo crear el evento.");
            return;
        }
    }

    Event event;
    event.id = eventId_;
    event.name = name;
    event.priority = prioritySpin_->value();
    event.persistent = persistentCheck_->isChecked();
    event.immediate = immediateCheck_->isChecked();
    event.useAdAnnounce = adAnnounceCheck_->isChecked();
    event.maxWaitMinutes = (!event.persistent && maxWaitCheck_->isChecked())
                           ? maxWaitSpin_->value() : 0;

    if (blockDatesCheck_->isChecked()) {
        event.validFrom = blockFromDate_->date();
        event.validUntil = blockUntilDate_->date();
    }

    // Sincronizar el orden visual (drag & drop) con el vector interno
    syncElementsFromList();

    repo_->update(event);
    repo_->setElements(eventId_, elements_);

    // Guardar slots
    auto existingSlotList = repo_->getSlots(eventId_);
    for (const auto& s : existingSlotList) repo_->removeSlot(s.id);
    for (const auto& s : eventSlots_) repo_->addSlot(eventId_, s.day, s.triggerTime);

    LOG_INFO("[EventEditor] Guardado: {} ({} elem, {} horarios, pri {})",
             name.toStdString(), elements_.size(), eventSlots_.size(), event.priority);
    accept();
}

// ── Refresh ──────────────────────────────────────────────

void EventEditor::refreshElementList()
{
    elementList_->clear();
    for (int i = 0; i < elements_.size(); ++i) {
        const auto& e = elements_[i];
        QString typeLabel;
        QString iconPath;
        switch (e.type) {
            case ElementType::Folder:       typeLabel = "Carpeta"; iconPath = ":/icons/folder.svg"; break;
            case ElementType::File:         typeLabel = "Archivo"; iconPath = ":/icons/music.svg"; break;
            case ElementType::TimeAnnounce: typeLabel = "Hora";    iconPath = ":/icons/clock.svg"; break;
            case ElementType::Stream:       typeLabel = "Stream";  iconPath = ":/icons/radio.svg"; break;
        }

        QString label = QString("%1. [%2] %3").arg(i + 1).arg(typeLabel, e.displayName);

        // Mostrar duración para streams
        if (e.type == ElementType::Stream && e.durationMs > 0) {
            int totalSecs = static_cast<int>(e.durationMs / 1000);
            int mins = totalSecs / 60;
            int secs = totalSecs % 60;
            label += QString("  [%1:%2]")
                .arg(mins).arg(secs, 2, 10, QChar('0'));
        }

        if (e.validFrom.isValid() || e.validUntil.isValid()) {
            label += QString("  %1→%2")
                .arg(e.validFrom.isValid() ? e.validFrom.toString("dd/MM") : "...")
                .arg(e.validUntil.isValid() ? e.validUntil.toString("dd/MM") : "...");
        }

        auto* item = new QListWidgetItem(QIcon(iconPath), label);
        item->setToolTip(e.path.isEmpty() ? e.displayName : e.path);
        item->setData(Qt::UserRole, i);  // Índice original para sync de drag & drop
        elementList_->addItem(item);
    }
}

void EventEditor::refreshSlotList()
{
    slotList_->clear();
    std::sort(eventSlots_.begin(), eventSlots_.end(),
        [](const EventSlot& a, const EventSlot& b) {
            static const QStringList order = {
                "Lunes","Martes","Miércoles","Jueves","Viernes","Sábado","Domingo"
            };
            if (a.day != b.day) return order.indexOf(a.day) < order.indexOf(b.day);
            return a.triggerTime < b.triggerTime;
        });

    for (const auto& s : eventSlots_) {
        slotList_->addItem(QString("%1  %2").arg(s.day, s.triggerTime.toString("HH:mm")));
    }
}

void EventEditor::syncElementsFromList()
{
    // Reconstruir elements_ según el orden visual del QListWidget
    // (el drag & drop puede haber cambiado el orden de los items)
    QVector<EventElement> reordered;
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
