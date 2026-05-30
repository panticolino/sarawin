#include "ui/weekly_grid.h"
#include "data/schedule_repository.h"
#include "util/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QScrollBar>
#include <QIcon>
#include <QApplication>

namespace sara {

static const QStringList DAYS = {
    "Lunes", "Martes", "Miércoles", "Jueves",
    "Viernes", "Sábado", "Domingo"
};

WeeklyGrid::WeeklyGrid(ScheduleRepository* repo, QWidget* parent)
    : QDialog(parent)
    , repo_(repo)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle(tr("Grilla Semanal de Programación"));
    setMinimumSize(900, 620);
    resize(1050, 680);

    // Paleta de colores para distinguir programaciones
    palette_ = {
        QColor(102, 126, 234),  // Azul (primario SARA)
        QColor(34, 197, 94),    // Verde
        QColor(245, 158, 11),   // Ámbar
        QColor(239, 68, 68),    // Rojo
        QColor(139, 92, 246),   // Violeta
        QColor(6, 182, 212),    // Cyan
        QColor(236, 72, 153),   // Rosa
        QColor(234, 179, 8),    // Amarillo
        QColor(20, 184, 166),   // Teal
        QColor(249, 115, 22),   // Naranja
    };

    setupUI();
    loadGrid();
}

void WeeklyGrid::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 12, 16, 12);

    // ── Título y descripción ─────────────────────────
    auto* titleLabel = new QLabel(tr("Grilla Semanal"));
    titleLabel->setStyleSheet("font-size: 18px; font-weight: 700;");

    auto* descLabel = new QLabel(
        tr("Haga clic en una celda para asignar una programación. Clic derecho para quitar. Las franjas vacías usan la carpeta de respaldo.")
    );
    descLabel->setStyleSheet("font-size: 12px;");
    descLabel->setWordWrap(true);

    layout->addWidget(titleLabel);
    layout->addWidget(descLabel);

    // ── Barra de herramientas ────────────────────────
    auto* toolbar = new QHBoxLayout();

    dayFilter_ = new QComboBox();
    dayFilter_->addItem(tr("Vista: Semana completa"));
    for (const auto& d : DAYS) dayFilter_->addItem(tr("Ver ") + d);
    dayFilter_->setMinimumWidth(180);
    connect(dayFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WeeklyGrid::onDayFilterChanged);

    bulkAssignBtn_ = new QPushButton(QIcon(":/icons/plus.svg"), " Asignar en bloque");
    bulkAssignBtn_->setToolTip(tr("Asignar una programación a múltiples franjas"));
    connect(bulkAssignBtn_, &QPushButton::clicked, this, &WeeklyGrid::onBulkAssign);

    clearDayBtn_ = new QPushButton(QIcon(":/icons/x.svg"), " Limpiar");
    clearDayBtn_->setToolTip(tr("Quitar asignaciones del día seleccionado (o toda la semana)"));
    clearDayBtn_->setProperty("class", "secondaryButton");
    connect(clearDayBtn_, &QPushButton::clicked, this, &WeeklyGrid::onClearDay);

    infoLabel_ = new QLabel();
    infoLabel_->setStyleSheet("font-size: 11px;");

    toolbar->addWidget(dayFilter_);
    toolbar->addWidget(bulkAssignBtn_);
    toolbar->addWidget(clearDayBtn_);
    toolbar->addStretch();
    toolbar->addWidget(infoLabel_);

    layout->addLayout(toolbar);

    // ── Grilla ───────────────────────────────────────
    grid_ = new QTableWidget(ROWS, COLS);
    grid_->setHorizontalHeaderLabels(DAYS);
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid_->setShowGrid(true);
    grid_->setAlternatingRowColors(false);

    // Labels de hora en filas
    QStringList rowLabels;
    for (int r = 0; r < ROWS; ++r) {
        rowLabels << timeLabel(r);
    }
    grid_->setVerticalHeaderLabels(rowLabels);

    // Tamaños
    grid_->verticalHeader()->setDefaultSectionSize(26);
    grid_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // Inicializar celdas vacías
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            auto* item = new QTableWidgetItem("");
            item->setTextAlignment(Qt::AlignCenter);
            grid_->setItem(r, c, item);
        }
    }

    // Context menu
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(grid_, &QTableWidget::customContextMenuRequested,
            this, &WeeklyGrid::onCellRightClicked);
    connect(grid_, &QTableWidget::cellClicked,
            this, &WeeklyGrid::onCellClicked);

    layout->addWidget(grid_, 1);

    // ── Botón cerrar ─────────────────────────────────
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Cerrar"));
    closeBtn->setProperty("class", "secondaryButton");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomRow->addWidget(closeBtn);
    layout->addLayout(bottomRow);

    // Scroll a hora actual
    int currentRow = timeToRow(QTime::currentTime());
    int scrollTo = qMax(0, currentRow - 4);
    QMetaObject::invokeMethod(this, [this, scrollTo]() {
        grid_->verticalScrollBar()->setValue(
            grid_->verticalScrollBar()->maximum() * scrollTo / ROWS
        );
    }, Qt::QueuedConnection);
}

// ══════════════════════════════════════════════════════════
// Cargar / actualizar grilla
// ══════════════════════════════════════════════════════════

void WeeklyGrid::loadGrid()
{
    // Limpiar todo
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            clearCell(r, c);
        }
    }

    int totalAssigned = 0;

    // Cargar slots desde BD
    for (int c = 0; c < COLS; ++c) {
        auto daySlots = repo_->getSlotsForDay(DAYS[c]);

        for (const auto& slot : daySlots) {
            // Obtener nombre de la programación
            auto sched = repo_->getById(slot.scheduleId);
            if (!sched) continue;

            int startRow = timeToRow(slot.startTime);
            int endRow = timeToRow(slot.endTime);

            for (int r = startRow; r < endRow && r < ROWS; ++r) {
                updateCell(r, c, slot.scheduleId, sched->name);
                ++totalAssigned;
            }
        }
    }

    infoLabel_->setText(
        QString("%1 franjas asignadas").arg(totalAssigned)
    );
}

void WeeklyGrid::updateCell(int row, int col, const QString& scheduleId,
                             const QString& scheduleName)
{
    auto* item = grid_->item(row, col);
    if (!item) return;

    QColor color = scheduleColor(scheduleId);

    item->setText(scheduleName);
    item->setData(Qt::UserRole, scheduleId);
    item->setBackground(QColor(color.red(), color.green(), color.blue(), 50));
    item->setForeground(color.lighter(140));
    item->setToolTip(QString("%1\n%2 %3")
        .arg(scheduleName, DAYS[col], timeLabel(row)));

    QFont f = item->font();
    f.setPointSize(9);
    f.setBold(true);
    item->setFont(f);
}

void WeeklyGrid::clearCell(int row, int col)
{
    auto* item = grid_->item(row, col);
    if (!item) return;

    item->setText("");
    item->setData(Qt::UserRole, QString());
    item->setBackground(palette().color(QPalette::Base));
    item->setForeground(palette().color(QPalette::PlaceholderText));
    item->setToolTip("");
}

// ══════════════════════════════════════════════════════════
// Interacción
// ══════════════════════════════════════════════════════════

void WeeklyGrid::onCellClicked(int row, int col)
{
    // Mostrar lista de programaciones para asignar
    auto schedules = repo_->getAll();
    if (schedules.isEmpty()) {
        QMessageBox::information(this, tr("Sin programaciones"),
            tr("No hay programaciones creadas.\n\nPrimero cree una programación desde el botón \")Programar\" en la columna de Programación Principal."));
        return;
    }

    QStringList items;
    for (const auto& s : schedules) {
        items << s.name;
    }

    bool ok = false;
    QString selected = QInputDialog::getItem(
        this,
        QString("Asignar programación — %1 %2").arg(DAYS[col], timeLabel(row)),
        tr("Seleccione una programación:"),
        items, 0, false, &ok
    );

    if (!ok || selected.isEmpty()) return;

    // Buscar el ID de la programación seleccionada
    QString scheduleId;
    for (const auto& s : schedules) {
        if (s.name == selected) {
            scheduleId = s.id;
            break;
        }
    }

    if (scheduleId.isEmpty()) return;

    // Calcular la franja: esta celda = 30 minutos
    QTime startTime = rowToTime(row);
    QTime endTime = startTime.addSecs(30 * 60);

    // Verificar si ya hay una asignación en esta celda y es la misma programación
    auto* item = grid_->item(row, col);
    QString existingId = item ? item->data(Qt::UserRole).toString() : QString();

    if (existingId == scheduleId) return;  // Ya asignada

    // Asignar en BD
    repo_->assignSlot(scheduleId, DAYS[col], startTime, endTime);

    // Actualizar visual
    updateCell(row, col, scheduleId, selected);

    LOG_INFO("[WeeklyGrid] Asignado: {} → {} {} - {}",
             selected.toStdString(), DAYS[col].toStdString(),
             startTime.toString("HH:mm").toStdString(),
             endTime.toString("HH:mm").toStdString());

    loadGrid();  // Recargar para actualizar contador
}

void WeeklyGrid::onCellRightClicked(const QPoint& pos)
{
    int row = grid_->rowAt(pos.y());
    int col = grid_->columnAt(pos.x());
    if (row < 0 || col < 0) return;

    auto* item = grid_->item(row, col);
    if (!item) return;

    QString scheduleId = item->data(Qt::UserRole).toString();
    if (scheduleId.isEmpty()) return;  // Celda vacía

    QString scheduleName = item->text();
    QTime slotTime = rowToTime(row);
    QString timeStr = slotTime.toString(tr("HH:mm"));

    QMenu menu(this);

    auto* removeDayAction = menu.addAction(
        QIcon(":/icons/x.svg"),
        tr("Quitar \"%1\" del %2 a las %3")
            .arg(scheduleName, DAYS[col], timeStr));

    menu.addSeparator();

    auto* removeWeekAction = menu.addAction(
        QIcon(":/icons/x.svg"),
        tr("Quitar \"%1\" de las %2 toda la semana")
            .arg(scheduleName, timeStr));

    auto* result = menu.exec(grid_->viewport()->mapToGlobal(pos));

    if (result == removeDayAction) {
        // Quitar solo de este día
        repo_->removeSlot(DAYS[col], slotTime);
        LOG_INFO("[WeeklyGrid] Slot removido: {} {} {}",
                 scheduleName.toStdString(),
                 DAYS[col].toStdString(), timeStr.toStdString());
        loadGrid();

    } else if (result == removeWeekAction) {
        // Quitar de todos los días a esta hora
        for (const auto& day : DAYS) {
            // Solo quitar si es la misma programación
            auto activeSlot = repo_->getActiveSlot(day, slotTime);
            if (activeSlot && activeSlot->scheduleId == scheduleId) {
                repo_->removeSlot(day, slotTime);
            }
        }
        LOG_INFO("[WeeklyGrid] Slot removido toda la semana: {} {}",
                 scheduleName.toStdString(), timeStr.toStdString());
        loadGrid();
    }
}

void WeeklyGrid::onDayFilterChanged(int index)
{
    if (index == 0) {
        // Mostrar todos
        for (int c = 0; c < COLS; ++c) {
            grid_->setColumnHidden(c, false);
        }
    } else {
        for (int c = 0; c < COLS; ++c) {
            grid_->setColumnHidden(c, c != (index - 1));
        }
    }
}

void WeeklyGrid::onBulkAssign()
{
    auto schedules = repo_->getAll();
    if (schedules.isEmpty()) {
        QMessageBox::information(this, tr("Sin programaciones"),
            tr("No hay programaciones creadas."));
        return;
    }

    // Paso 1: Elegir programación
    QStringList items;
    for (const auto& s : schedules) {
        items << s.name;
    }

    bool ok = false;
    QString selected = QInputDialog::getItem(
        this, tr("Asignar en bloque — Paso 1: Programación"),
        tr("Seleccione la programación:"), items, 0, false, &ok);
    if (!ok) return;

    QString scheduleId;
    for (const auto& s : schedules) {
        if (s.name == selected) { scheduleId = s.id; break; }
    }
    if (scheduleId.isEmpty()) return;

    // Paso 2: Elegir días
    QStringList dayOptions = {tr("Lunes a Viernes"), "Sábado y Domingo", "Toda la semana"};
    for (const auto& d : DAYS) dayOptions << d;

    QString selectedDays = QInputDialog::getItem(
        this, tr("Asignar en bloque — Paso 2: Días"),
        tr("¿Qué días?"), dayOptions, 0, false, &ok);
    if (!ok) return;

    QStringList targetDays;
    if (selectedDays == tr("Lunes a Viernes")) {
        targetDays = DAYS.mid(0, 5);
    } else if (selectedDays == tr("Sábado y Domingo")) {
        targetDays = DAYS.mid(5, 2);
    } else if (selectedDays == tr("Toda la semana")) {
        targetDays = DAYS;
    } else {
        targetDays << selectedDays;
    }

    // Paso 3: Elegir horario de inicio
    QStringList allTimeOptions;
    for (int h = 0; h < 24; ++h) {
        allTimeOptions << QString("%1:00").arg(h, 2, 10, QChar('0'));
        allTimeOptions << QString("%1:30").arg(h, 2, 10, QChar('0'));
    }

    QString startStr = QInputDialog::getItem(
        this, tr("Asignar en bloque — Paso 3: Hora de inicio"),
        tr("Hora de inicio:"), allTimeOptions, 12, false, &ok);
    if (!ok) return;

    // Filtrar opciones de fin: solo horarios posteriores al inicio
    int startIdx = allTimeOptions.indexOf(startStr);
    QStringList endTimeOptions;
    for (int i = startIdx + 1; i < allTimeOptions.size(); ++i) {
        endTimeOptions << allTimeOptions[i];
    }
    // Agregar 00:00 del día siguiente como opción final (= hasta medianoche)
    if (!endTimeOptions.contains("00:00")) {
        endTimeOptions << "24:00";
    }

    if (endTimeOptions.isEmpty()) {
        QMessageBox::warning(this, tr("Horario inválido"),
            tr("No hay franjas disponibles después de la hora seleccionada."));
        return;
    }

    // Pre-seleccionado: primera opción (la franja inmediatamente siguiente)
    QString endStr = QInputDialog::getItem(
        this, tr("Asignar en bloque — Paso 3: Hora de fin"),
        QString("Hora de fin (inicio: %1):").arg(startStr),
        endTimeOptions, 0, false, &ok);
    if (!ok) return;

    QTime startTime = QTime::fromString(startStr, tr("HH:mm"));
    QTime endTime;
    if (endStr == "24:00") {
        endTime = QTime(0, 0);  // Medianoche: bulkAssignSlots lo interpreta como slot 48
    } else {
        endTime = QTime::fromString(endStr, tr("HH:mm"));
    }

    if (endStr != "24:00" && startTime >= endTime) {
        QMessageBox::warning(this, tr("Horario inválido"),
            tr("La hora de fin debe ser posterior a la de inicio."));
        return;
    }

    // Asignar en una sola transacción SQLite (rápido incluso con muchas franjas)
    QApplication::setOverrideCursor(Qt::WaitCursor);

    bool ok2 = repo_->bulkAssignSlots(scheduleId, targetDays, startTime, endTime);

    QApplication::restoreOverrideCursor();

    if (!ok2) {
        QMessageBox::critical(this, tr("Error"), "No se pudieron asignar las franjas.");
        return;
    }

    // Calcular cantidad para el mensaje (integer math, no QTime loops)
    int startSlot = startTime.hour() * 2 + (startTime.minute() >= 30 ? 1 : 0);
    int endSlot = (endStr == "24:00") ? 48
                  : endTime.hour() * 2 + (endTime.minute() >= 30 ? 1 : 0);
    int slotCount = (endSlot - startSlot) * targetDays.size();

    LOG_INFO("[WeeklyGrid] Asignación en bloque: {} → {} día(s), {} - {} ({} franjas)",
             selected.toStdString(), targetDays.size(),
             startTime.toString("HH:mm").toStdString(),
             endTime.toString("HH:mm").toStdString(), slotCount);

    loadGrid();

    QMessageBox::information(this, tr("Asignación completada"),
        QString("Se asignó \"%1\" en %2 franja(s).").arg(selected).arg(slotCount));
}

void WeeklyGrid::onClearDay()
{
    int dayIdx = dayFilter_->currentIndex();

    if (dayIdx == 0) {
        // Vista semana completa → ofrecer limpiar toda la semana
        auto result = QMessageBox::question(
            this, tr("Limpiar toda la semana"),
            tr("¿Quitar TODAS las programaciones de TODA la semana?\n\nEsta acción no se puede deshacer."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No
        );

        if (result == QMessageBox::Yes) {
            for (const auto& day : DAYS) {
                repo_->clearDay(day);
            }
            loadGrid();
            LOG_INFO("[WeeklyGrid] Toda la semana limpiada");
        }
        return;
    }

    QString day = DAYS[dayIdx - 1];

    auto result = QMessageBox::question(
        this, tr("Limpiar día"),
        QString("¿Quitar todas las programaciones del %1?").arg(day),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No
    );

    if (result == QMessageBox::Yes) {
        repo_->clearDay(day);
        loadGrid();
        LOG_INFO("[WeeklyGrid] Día limpiado: {}", day.toStdString());
    }
}

// ══════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════

QString WeeklyGrid::dayName(int col) const
{
    return (col >= 0 && col < DAYS.size()) ? DAYS[col] : "";
}

int WeeklyGrid::dayColumn(const QString& day) const
{
    return DAYS.indexOf(day);
}

QTime WeeklyGrid::rowToTime(int row) const
{
    int hour = row / 2;
    int min = (row % 2) * 30;
    return QTime(hour, min);
}

int WeeklyGrid::timeToRow(const QTime& time) const
{
    return time.hour() * 2 + (time.minute() >= 30 ? 1 : 0);
}

QColor WeeklyGrid::scheduleColor(const QString& scheduleId) const
{
    if (colorMap_.contains(scheduleId)) {
        return colorMap_[scheduleId];
    }

    // Asignar siguiente color de la paleta
    QColor color = palette_[nextColor_ % palette_.size()];
    colorMap_[scheduleId] = color;
    nextColor_++;
    return color;
}

QString WeeklyGrid::timeLabel(int row) const
{
    QTime t = rowToTime(row);
    return t.toString(tr("HH:mm"));
}

} // namespace sara
