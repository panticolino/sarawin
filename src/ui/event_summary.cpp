#include "ui/event_summary.h"
#include "data/event_repository.h"
#include "util/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QIcon>
#include <QGroupBox>
#include <QMenu>
#include <QMessageBox>
#include <QDate>
#include <QDateEdit>
#include <QCheckBox>
#include <QFormLayout>
#include <QDialogButtonBox>

namespace sara {

static const QStringList DAYS = {
    "Lunes", "Martes", "Miércoles", "Jueves",
    "Viernes", "Sábado", "Domingo"
};

EventSummaryDialog::EventSummaryDialog(EventRepository* repo, QWidget* parent)
    : QDialog(parent), repo_(repo)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle(tr("Resumen Semanal de Eventos"));
    setMinimumSize(850, 520);
    resize(950, 600);
    setupUI();
    loadData();
    applyFilters();
}

void EventSummaryDialog::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 12, 16, 12);

    auto* titleLabel = new QLabel(tr("Resumen Semanal de Eventos"));
    titleLabel->setStyleSheet("font-size: 18px; font-weight: 700;");
    layout->addWidget(titleLabel);

    auto* descLabel = new QLabel(
        tr("Vista completa de todos los eventos programados. "
           "Use los filtros para encontrar audios caducados, "
           "próximos a vencer, o buscar contenidos específicos."));
    descLabel->setStyleSheet("font-size: 12px;");
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // ── Barra de filtros ─────────────────────────────
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);

    auto* searchIcon = new QLabel();
    searchIcon->setPixmap(QIcon(":/icons/search.svg").pixmap(16, 16));
    filterRow->addWidget(searchIcon);

    searchEdit_ = new QLineEdit();
    searchEdit_->setPlaceholderText(tr("Buscar por nombre de evento o contenido..."));
    searchEdit_->setClearButtonEnabled(true);
    connect(searchEdit_, &QLineEdit::textChanged,
            this, &EventSummaryDialog::onFilterChanged);
    filterRow->addWidget(searchEdit_, 1);

    dayFilter_ = new QComboBox();
    dayFilter_->addItem(tr("Todos los días"));
    for (const auto& d : DAYS) dayFilter_->addItem(d);
    dayFilter_->setFixedWidth(130);
    connect(dayFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EventSummaryDialog::onFilterChanged);
    filterRow->addWidget(dayFilter_);

    vigencyFilter_ = new QComboBox();
    vigencyFilter_->addItem(tr("Cualquier vigencia"));
    vigencyFilter_->addItem(tr("Caducados"));
    vigencyFilter_->addItem(tr("Próximos a caducar (7 días)"));
    vigencyFilter_->addItem(tr("Sin vigencia definida"));
    vigencyFilter_->addItem(tr("Vigentes"));
    vigencyFilter_->setFixedWidth(190);
    connect(vigencyFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EventSummaryDialog::onFilterChanged);
    filterRow->addWidget(vigencyFilter_);

    modeCombo_ = new QComboBox();
    modeCombo_->addItem(tr("Vista por horario"));
    modeCombo_->addItem(tr("Vista por contenido"));
    modeCombo_->setFixedWidth(150);
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EventSummaryDialog::onFilterChanged);
    filterRow->addWidget(modeCombo_);

    layout->addLayout(filterRow);

    // ── Tabla ────────────────────────────────────────
    table_ = new QTableWidget();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->verticalHeader()->setVisible(false);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->setDefaultSectionSize(26);

    layout->addWidget(table_, 1);

    // Doble clic → editar evento
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0) return;
        QString eventId;
        int mode = modeCombo_->currentIndex();
        if (mode == 0) {
            auto* item = table_->item(row, 2);
            if (item) eventId = item->data(Qt::UserRole).toString();
        } else {
            auto* item = table_->item(row, 1);
            if (item) {
                QString eventName = item->text();
                for (const auto& r : allRows_) {
                    if (r.eventName == eventName) { eventId = r.eventId; break; }
                }
            }
        }
        if (!eventId.isEmpty()) {
            emit editEventRequested(eventId);
            loadData();
            applyFilters();
        }
    });

    // Context menu
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        int row = table_->rowAt(pos.y());
        if (row < 0) return;

        QString eventId, eventName;
        int mode = modeCombo_->currentIndex();
        if (mode == 0) {
            auto* item = table_->item(row, 2);
            if (item) { eventId = item->data(Qt::UserRole).toString(); eventName = item->text(); }
        } else {
            auto* item = table_->item(row, 1);
            if (item) { eventName = item->text(); }
            for (const auto& r : allRows_) {
                if (r.eventName == eventName) { eventId = r.eventId; break; }
            }
        }
        if (eventId.isEmpty()) return;

        QMenu menu(this);
        auto* editAction = menu.addAction(QIcon(":/icons/settings.svg"), tr("Editar evento"));
        auto* duplicateAction = menu.addAction(QIcon(":/icons/list.svg"), tr("Duplicar evento"));
        menu.addSeparator();
        auto* deleteAction = menu.addAction(QIcon(":/icons/x.svg"), tr("Eliminar evento"));

        auto* result = menu.exec(table_->viewport()->mapToGlobal(pos));
        if (result == editAction) {
            emit editEventRequested(eventId);
            loadData(); applyFilters();
        } else if (result == duplicateAction) {
            emit duplicateEventRequested(eventId, eventName);
            loadData(); applyFilters();
        } else if (result == deleteAction) {
            auto confirm = QMessageBox::question(
                this, tr("Eliminar evento"),
                tr("Eliminar el evento \"%1\"?\n\nSe eliminarán todos sus contenidos y horarios.\n"
                   "Esta acción no se puede deshacer.").arg(eventName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (confirm == QMessageBox::Yes) {
                emit deleteEventRequested(eventId);
                loadData(); applyFilters();
            }
        }

        // Edición masiva de vigencia (vista por contenido)
        if (modeCombo_->currentIndex() == 1) {
            auto selected = table_->selectionModel()->selectedRows();
            if (selected.size() > 1) {
                // Ya se maneja con el botón, pero se puede añadir al menú si hay selección múltiple
            }
        }
    });

    // Barra de acciones
    auto* actionRow = new QHBoxLayout();

    auto* bulkEditBtn = new QPushButton(QIcon(":/icons/clock.svg"), tr("Editar vigencia seleccionados"));
    bulkEditBtn->setFixedHeight(28);
    bulkEditBtn->setProperty("class", "secondaryButton");
    bulkEditBtn->setToolTip(tr("Cambiar las fechas de vigencia de todos los contenidos seleccionados"));
    connect(bulkEditBtn, &QPushButton::clicked, this, [this]() {
        auto selected = table_->selectionModel()->selectedRows();
        if (selected.isEmpty()) {
            QMessageBox::information(this, tr("Selección vacía"),
                tr("Seleccione uno o más contenidos en la tabla.\n"
                   "Use Ctrl+Clic o Shift+Clic para selección múltiple."));
            return;
        }

        // Recoger IDs de elementos seleccionados
        QVector<int> elementIds;
        QStringList names;
        for (const auto& idx : selected) {
            auto* item = table_->item(idx.row(), 0);
            if (!item) continue;
            int elemId = item->data(Qt::UserRole + 1).toInt();
            if (elemId > 0) {
                elementIds.append(elemId);
                QString n = item->text();
                if (!names.contains(n)) names << n;
            }
        }
        if (elementIds.isEmpty()) {
            QMessageBox::information(this, tr("Sin datos"),
                tr("No se encontraron contenidos editables en la selección."));
            return;
        }

        bulkEditVigency(elementIds, names);
    });
    actionRow->addWidget(bulkEditBtn);

    auto* bulkDeleteBtn = new QPushButton(QIcon(":/icons/x.svg"), tr("Eliminar seleccionados"));
    bulkDeleteBtn->setFixedHeight(28);
    bulkDeleteBtn->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.15); color: #ef4444; "
        "border: 1px solid rgba(239,68,68,0.3); border-radius: 6px; padding: 4px 10px; }"
        "QPushButton:hover { background: rgba(239,68,68,0.25); }");
    bulkDeleteBtn->setToolTip(tr("Eliminar los eventos seleccionados"));
    connect(bulkDeleteBtn, &QPushButton::clicked, this, [this]() {
        auto selected = table_->selectionModel()->selectedRows();
        if (selected.isEmpty()) {
            QMessageBox::information(this, tr("Selección vacía"),
                tr("Seleccione uno o más eventos en la tabla."));
            return;
        }

        // Recoger eventIds únicos
        QSet<QString> eventIds;
        QStringList eventNames;
        int mode = modeCombo_->currentIndex();
        for (const auto& idx : selected) {
            QString eventId;
            QString eventName;
            if (mode == 0) {
                auto* item = table_->item(idx.row(), 2);
                if (item) { eventId = item->data(Qt::UserRole).toString(); eventName = item->text(); }
            } else {
                auto* item = table_->item(idx.row(), 1);
                if (item) { eventName = item->text(); }
                for (const auto& r : allRows_) {
                    if (r.eventName == eventName) { eventId = r.eventId; break; }
                }
            }
            if (!eventId.isEmpty() && !eventIds.contains(eventId)) {
                eventIds.insert(eventId);
                eventNames << eventName;
            }
        }
        if (eventIds.isEmpty()) return;

        QStringList preview = eventNames.mid(0, 5);
        if (eventNames.size() > 5)
            preview << tr("... y %1 más").arg(eventNames.size() - 5);

        auto confirm = QMessageBox::question(this, tr("Eliminar eventos"),
            tr("¿Eliminar %1 evento(s)?\n\n%2\n\n"
               "Se eliminarán todos sus contenidos y horarios.\n"
               "Esta acción no se puede deshacer.")
                .arg(eventIds.size()).arg(preview.join("\n")),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirm != QMessageBox::Yes) return;

        for (const auto& id : eventIds) {
            emit deleteEventRequested(id);
        }
        loadData();
        applyFilters();
    });
    actionRow->addWidget(bulkDeleteBtn);

    actionRow->addStretch();

    // Status
    statusLabel_ = new QLabel();
    statusLabel_->setStyleSheet("font-size: 11px;");
    actionRow->addWidget(statusLabel_);
    layout->addLayout(actionRow);

    // Cerrar
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Cerrar"));
    closeBtn->setProperty("class", "secondaryButton");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}

void EventSummaryDialog::loadData()
{
    allRows_.clear();

    auto summary = repo_->getWeeklySummary();
    // Cache de eventos para no consultar repetidamente
    QMap<QString, Event> eventCache;

    for (const auto& item : summary) {
        SummaryRow row;
        row.eventId      = item.eventId;
        row.eventName    = item.eventName;
        row.day          = item.day;
        row.triggerTime  = item.triggerTime;
        row.priority     = item.priority;
        row.elementCount = item.elementCount;

        // Cargar vigencia del evento
        if (!eventCache.contains(item.eventId)) {
            auto ev = repo_->getById(item.eventId);
            if (ev) eventCache[item.eventId] = *ev;
        }
        if (eventCache.contains(item.eventId)) {
            row.eventValidFrom = eventCache[item.eventId].validFrom;
            row.eventValidUntil = eventCache[item.eventId].validUntil;
        }

        // Cargar elementos con vigencia
        auto elements = repo_->getElements(item.eventId);
        for (const auto& e : elements) {
            row.contentNames << e.displayName;
            row.elementVigencies.append({e.id, e.displayName, e.validFrom, e.validUntil});
        }

        allRows_.append(row);
    }
}

void EventSummaryDialog::onFilterChanged()
{
    applyFilters();
}

// Helpers para vigencia
static bool isExpired(const QDate& validUntil) {
    return validUntil.isValid() && validUntil < QDate::currentDate();
}

static bool isExpiringSoon(const QDate& validUntil, int days = 7) {
    if (!validUntil.isValid()) return false;
    QDate today = QDate::currentDate();
    return validUntil >= today && validUntil <= today.addDays(days);
}

static bool hasNoDate(const QDate& validFrom, const QDate& validUntil) {
    return !validFrom.isValid() && !validUntil.isValid();
}

static bool isActive(const QDate& validFrom, const QDate& validUntil) {
    QDate today = QDate::currentDate();
    if (validFrom.isValid() && today < validFrom) return false;
    if (validUntil.isValid() && today > validUntil) return false;
    return true;
}

static QString vigencyText(const QDate& validFrom, const QDate& validUntil) {
    if (!validFrom.isValid() && !validUntil.isValid()) return "";
    QDate today = QDate::currentDate();
    if (validUntil.isValid() && validUntil < today) {
        int daysAgo = validUntil.daysTo(today);
        return QString("Caducado hace %1 día(s)").arg(daysAgo);
    }
    if (validUntil.isValid()) {
        int daysLeft = today.daysTo(validUntil);
        if (daysLeft <= 7) return QString("Caduca en %1 día(s)").arg(daysLeft);
        return QString("Hasta %1").arg(validUntil.toString("dd/MM/yyyy"));
    }
    if (validFrom.isValid()) {
        return QString("Desde %1").arg(validFrom.toString("dd/MM/yyyy"));
    }
    return "";
}

void EventSummaryDialog::applyFilters()
{
    QString search = searchEdit_->text().trimmed().toLower();
    int dayIdx = dayFilter_->currentIndex();
    int mode = modeCombo_->currentIndex();
    int vigency = vigencyFilter_->currentIndex();

    if (mode == 0) {
        // ── Vista por horario ────────────────────────
        table_->setColumnCount(6);
        table_->setHorizontalHeaderLabels({tr("Día"), tr("Hora"), tr("Evento"),
                                            tr("Pri"), tr("Vigencia"), tr("Contenido")});
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
        table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
        table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
        table_->setColumnWidth(0, 80);
        table_->setColumnWidth(1, 50);
        table_->setColumnWidth(2, 150);
        table_->setColumnWidth(3, 32);
        table_->setColumnWidth(4, 150);

        table_->setRowCount(0);
        int count = 0;
        int expiredCount = 0;

        for (const auto& row : allRows_) {
            if (dayIdx > 0 && row.day != DAYS[dayIdx - 1]) continue;

            // Filtro de vigencia (sobre el evento)
            bool eventExpired = isExpired(row.eventValidUntil);
            bool eventExpiring = isExpiringSoon(row.eventValidUntil);
            bool eventNoDate = hasNoDate(row.eventValidFrom, row.eventValidUntil);
            bool eventActive = isActive(row.eventValidFrom, row.eventValidUntil);

            // Tambien verificar si algún elemento está caducado
            bool hasExpiredElement = false;
            bool hasExpiringElement = false;
            for (const auto& ev : row.elementVigencies) {
                if (isExpired(ev.validUntil)) hasExpiredElement = true;
                if (isExpiringSoon(ev.validUntil)) hasExpiringElement = true;
            }

            if (vigency == VigencyExpired && !eventExpired && !hasExpiredElement) continue;
            if (vigency == VigencyExpiringSoon && !eventExpiring && !hasExpiringElement) continue;
            if (vigency == VigencyNoDate && !eventNoDate) continue;
            if (vigency == VigencyActive && !eventActive) continue;

            // Filtro de búsqueda
            if (!search.isEmpty()) {
                bool match = row.eventName.toLower().contains(search);
                if (!match) {
                    for (const auto& name : row.contentNames) {
                        if (name.toLower().contains(search)) { match = true; break; }
                    }
                }
                if (!match) continue;
            }

            int r = table_->rowCount();
            table_->insertRow(r);

            auto* dayItem = new QTableWidgetItem(row.day);
            table_->setItem(r, 0, dayItem);

            auto* timeItem = new QTableWidgetItem(row.triggerTime.toString(tr("HH:mm")));
            timeItem->setTextAlignment(Qt::AlignCenter);
            table_->setItem(r, 1, timeItem);

            auto* nameItem = new QTableWidgetItem(row.eventName);
            nameItem->setData(Qt::UserRole, row.eventId);
            table_->setItem(r, 2, nameItem);

            auto* priItem = new QTableWidgetItem(QString::number(row.priority));
            priItem->setTextAlignment(Qt::AlignCenter);
            table_->setItem(r, 3, priItem);

            // Vigencia con color
            QString vigText = vigencyText(row.eventValidFrom, row.eventValidUntil);
            auto* vigItem = new QTableWidgetItem(vigText);
            if (eventExpired) {
                vigItem->setForeground(QColor("#ef4444"));
                expiredCount++;
            } else if (eventExpiring) {
                vigItem->setForeground(QColor("#f59e0b"));
            } else if (eventNoDate) {
                vigItem->setForeground(palette().color(QPalette::PlaceholderText));
                vigItem->setText(tr("Sin fecha"));
            } else {
                vigItem->setForeground(QColor("#22c55e"));
            }
            table_->setItem(r, 4, vigItem);

            // Contenido con indicadores de vigencia por elemento
            QStringList contentParts;
            for (const auto& ev : row.elementVigencies) {
                QString prefix;
                if (isExpired(ev.validUntil)) {
                    prefix = QString::fromUtf8("⛔ ");
                    expiredCount++;
                } else if (isExpiringSoon(ev.validUntil)) {
                    prefix = QString::fromUtf8("⚠ ");
                }
                contentParts << prefix + ev.name;
            }
            QString contentStr = contentParts.join(", ");
            if (contentStr.length() > 80) contentStr = contentStr.left(77) + "...";
            auto* contentItem = new QTableWidgetItem(contentStr);
            contentItem->setForeground(palette().color(QPalette::PlaceholderText));

            // Tooltip detallado con vigencia de cada elemento
            QStringList tipParts;
            for (const auto& ev : row.elementVigencies) {
                QString vt = vigencyText(ev.validFrom, ev.validUntil);
                tipParts << ev.name + (vt.isEmpty() ? "" : " [" + vt + "]");
            }
            contentItem->setToolTip(tipParts.join("\n"));
            table_->setItem(r, 5, contentItem);

            // Fondo rojo suave si el evento está caducado
            if (eventExpired) {
                for (int c = 0; c < 6; ++c) {
                    if (auto* item = table_->item(r, c))
                        item->setBackground(QColor(239, 68, 68, 20));
                }
            } else if (eventExpiring) {
                for (int c = 0; c < 6; ++c) {
                    if (auto* item = table_->item(r, c))
                        item->setBackground(QColor(245, 158, 11, 15));
                }
            }

            // Resaltar si el buscador coincide con contenido
            if (!search.isEmpty()) {
                for (const auto& name : row.contentNames) {
                    if (name.toLower().contains(search)) {
                        for (int c = 0; c < 6; ++c) {
                            if (auto* item = table_->item(r, c))
                                item->setBackground(QColor(245, 158, 11, 25));
                        }
                        break;
                    }
                }
            }

            ++count;
        }

        QString status = tr("%1 entrada(s) mostrada(s)").arg(count);
        if (expiredCount > 0)
            status += tr(" — %1 caducado(s)").arg(expiredCount);
        statusLabel_->setText(status);

    } else {
        // ── Vista por contenido ──────────────────────
        table_->setColumnCount(5);
        table_->setHorizontalHeaderLabels({tr("Contenido"), tr("Evento"),
                                            tr("Vigencia audio"), tr("Día"), tr("Hora")});
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
        table_->setColumnWidth(1, 140);
        table_->setColumnWidth(2, 150);
        table_->setColumnWidth(3, 80);
        table_->setColumnWidth(4, 50);

        table_->setRowCount(0);
        int count = 0;
        int expiredCount = 0;

        for (const auto& row : allRows_) {
            if (dayIdx > 0 && row.day != DAYS[dayIdx - 1]) continue;

            for (const auto& ev : row.elementVigencies) {
                bool elExpired = isExpired(ev.validUntil);
                bool elExpiring = isExpiringSoon(ev.validUntil);
                bool elNoDate = hasNoDate(ev.validFrom, ev.validUntil);
                bool elActive = isActive(ev.validFrom, ev.validUntil);

                if (vigency == VigencyExpired && !elExpired) continue;
                if (vigency == VigencyExpiringSoon && !elExpiring) continue;
                if (vigency == VigencyNoDate && !elNoDate) continue;
                if (vigency == VigencyActive && !elActive) continue;

                if (!search.isEmpty() &&
                    !ev.name.toLower().contains(search) &&
                    !row.eventName.toLower().contains(search)) continue;

                int r = table_->rowCount();
                table_->insertRow(r);

                auto* contentItem = new QTableWidgetItem(ev.name);
                contentItem->setData(Qt::UserRole + 1, ev.id);
                table_->setItem(r, 0, contentItem);

                auto* nameItem = new QTableWidgetItem(row.eventName);
                table_->setItem(r, 1, nameItem);

                // Vigencia del audio
                QString vt = vigencyText(ev.validFrom, ev.validUntil);
                auto* vigItem = new QTableWidgetItem(vt.isEmpty() ? tr("Sin fecha") : vt);
                if (elExpired) {
                    vigItem->setForeground(QColor("#ef4444"));
                    expiredCount++;
                } else if (elExpiring) {
                    vigItem->setForeground(QColor("#f59e0b"));
                } else if (elNoDate) {
                    vigItem->setForeground(palette().color(QPalette::PlaceholderText));
                } else {
                    vigItem->setForeground(QColor("#22c55e"));
                }
                table_->setItem(r, 2, vigItem);

                auto* dayItem = new QTableWidgetItem(row.day);
                table_->setItem(r, 3, dayItem);

                auto* timeItem = new QTableWidgetItem(row.triggerTime.toString(tr("HH:mm")));
                timeItem->setTextAlignment(Qt::AlignCenter);
                table_->setItem(r, 4, timeItem);

                // Fondo para caducados
                if (elExpired) {
                    for (int c = 0; c < 5; ++c) {
                        if (auto* item = table_->item(r, c))
                            item->setBackground(QColor(239, 68, 68, 20));
                    }
                } else if (elExpiring) {
                    for (int c = 0; c < 5; ++c) {
                        if (auto* item = table_->item(r, c))
                            item->setBackground(QColor(245, 158, 11, 15));
                    }
                }

                if (!search.isEmpty() && ev.name.toLower().contains(search)) {
                    for (int c = 0; c < 5; ++c) {
                        if (auto* item = table_->item(r, c))
                            item->setBackground(QColor(245, 158, 11, 25));
                    }
                }

                ++count;
            }
        }

        QString status = tr("%1 entrada(s) mostrada(s)").arg(count);
        if (expiredCount > 0)
            status += tr(" — %1 caducado(s)").arg(expiredCount);
        statusLabel_->setText(status);
    }
}

void EventSummaryDialog::bulkEditVigency(const QVector<int>& elementIds,
                                          const QStringList& names)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Editar vigencia en lote"));
    dlg.setMinimumWidth(450);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Editar vigencia de %1 contenido(s)").arg(elementIds.size()));
    title->setStyleSheet("font-size: 14px; font-weight: 700;");
    layout->addWidget(title);

    // Mostrar nombres afectados (máx 5)
    QStringList preview = names.mid(0, 5);
    if (names.size() > 5) preview << tr("... y %1 más").arg(names.size() - 5);
    auto* namesLabel = new QLabel(preview.join("\n"));
    namesLabel->setStyleSheet("font-size: 11px; padding: 4px 8px; "
                               "background: rgba(102,126,234,0.1); border-radius: 6px;");
    namesLabel->setWordWrap(true);
    layout->addWidget(namesLabel);

    // Fechas
    auto* formGroup = new QGroupBox(tr("Nueva vigencia"));
    auto* form = new QFormLayout(formGroup);
    form->setSpacing(8);

    auto* fromCheck = new QCheckBox(tr("Establecer fecha de inicio"));
    auto* fromDate = new QDateEdit(QDate::currentDate());
    fromDate->setCalendarPopup(true);
    fromDate->setDisplayFormat("dd/MM/yyyy");
    fromDate->setEnabled(false);
    connect(fromCheck, &QCheckBox::toggled, fromDate, &QDateEdit::setEnabled);
    form->addRow(fromCheck, fromDate);

    auto* untilCheck = new QCheckBox(tr("Establecer fecha de fin"));
    untilCheck->setChecked(true);
    auto* untilDate = new QDateEdit(QDate::currentDate().addMonths(1));
    untilDate->setCalendarPopup(true);
    untilDate->setDisplayFormat("dd/MM/yyyy");
    connect(untilCheck, &QCheckBox::toggled, untilDate, &QDateEdit::setEnabled);
    form->addRow(untilCheck, untilDate);

    auto* clearCheck = new QCheckBox(tr("Quitar vigencia (sin fecha)"));
    connect(clearCheck, &QCheckBox::toggled, this, [fromCheck, untilCheck](bool on) {
        if (on) {
            fromCheck->setChecked(false);
            fromCheck->setEnabled(false);
            untilCheck->setChecked(false);
            untilCheck->setEnabled(false);
        } else {
            fromCheck->setEnabled(true);
            untilCheck->setEnabled(true);
        }
    });
    form->addRow("", clearCheck);

    auto* hint = new QLabel(
        tr("Solo se modificarán las fechas marcadas.\n"
           "Las fechas no marcadas se mantienen sin cambios."));
    hint->setStyleSheet("font-size: 10px; color: #888;");
    hint->setWordWrap(true);
    form->addRow("", hint);

    layout->addWidget(formGroup);

    // Botones
    auto* btns = new QDialogButtonBox();
    auto* cancelBtn = btns->addButton(QDialogButtonBox::Cancel);
    cancelBtn->setText(tr("Cancelar"));
    auto* applyBtn = btns->addButton(tr("Aplicar a %1 contenido(s)").arg(elementIds.size()),
                                      QDialogButtonBox::AcceptRole);
    applyBtn->setProperty("class", "primaryButton");
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    // Aplicar cambios
    int updated = 0;
    for (int id : elementIds) {
        QDate newFrom, newUntil;

        if (clearCheck->isChecked()) {
            // Quitar vigencia: fechas inválidas
            repo_->updateElementVigency(id, QDate(), QDate());
            updated++;
        } else {
            // Leer fechas actuales si no se van a cambiar
            // (updateElementVigency sobreescribe ambas, así que leemos las actuales)
            // Para simplificar: si un checkbox está marcado, usar la nueva fecha
            // Si no, mantener la actual → necesitamos leer la actual
            // Pero como tenemos el ID, podemos hacer la query con COALESCE
            // Más simple: solo actualizar los campos marcados con queries separadas
            if (fromCheck->isChecked()) {
                newFrom = fromDate->date();
            }
            if (untilCheck->isChecked()) {
                newUntil = untilDate->date();
            }

            // Actualizar solo los campos que cambiaron
            if (fromCheck->isChecked() && untilCheck->isChecked()) {
                repo_->updateElementVigency(id, newFrom, newUntil);
            } else if (fromCheck->isChecked()) {
                repo_->updateElementValidFrom(id, newFrom);
            } else if (untilCheck->isChecked()) {
                repo_->updateElementValidUntil(id, newUntil);
            }
            updated++;
        }
    }

    QMessageBox::information(this, tr("Vigencia actualizada"),
        tr("Se actualizaron %1 contenido(s).").arg(updated));

    loadData();
    applyFilters();
}

} // namespace sara
