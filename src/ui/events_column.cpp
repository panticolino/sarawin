#include "ui/events_column.h"
#include "data/event_repository.h"
#include "audio/audio_pipeline.h"
#include "ui/vumeter_widget.h"
#include "util/metadata_reader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QIcon>
#include <QDate>
#include <QTime>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>

namespace sara {

static QIcon tintSvg(const QString& path, const QColor& color)
{
    QPixmap pix = QIcon(path).pixmap(64, 64);
    QPainter p(&pix);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pix.rect(), color);
    p.end();
    return QIcon(pix);
}

static const QStringList DAYS = {
    "Lunes", "Martes", "Miércoles", "Jueves",
    "Viernes", "Sábado", "Domingo"
};

EventsColumn::EventsColumn(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void EventsColumn::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Cabecera con color ───────────────────────────
    auto* headerBar = new QWidget();
    headerBar->setFixedHeight(28);
    headerBar->setStyleSheet(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #22c55e, stop:1 #16a34a);"
        "border-radius: 4px 4px 0 0;");
    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(10, 0, 10, 0);
    auto* headerTitle = new QLabel(tr("PUBLICIDAD / EVENTOS"));
    headerTitle->setStyleSheet(
        "font-size: 11px; font-weight: 700; letter-spacing: 1px; "
        "color: white; background: transparent;");
    headerLayout->addWidget(headerTitle);
    headerLayout->addStretch();
    layout->addWidget(headerBar);

    // ── Mini Player ─────────────────────────────────
    auto* playerFrame = new QWidget();
    playerFrame->setStyleSheet(
        "background: none;"
        "border-bottom: none");
    auto* playerLayout = new QVBoxLayout(playerFrame);
    playerLayout->setContentsMargins(12, 8, 12, 8);
    playerLayout->setSpacing(6);

    auto* playerTopRow = new QHBoxLayout();
    playerStatusLabel_ = new QLabel(tr("● PARADO"));
    playerStatusLabel_->setStyleSheet(
        "font-size: 11px; font-weight: 800; color: #ef4444; padding-top: 8px;" 
        "letter-spacing: 1px; background: transparent;");

    playerEventLabel_ = new QLabel();
   playerEventLabel_->setProperty("class", "sourceLabel");

 /*   playerEventLabel_->setStyleSheet(
        "font-size: 11px; font-weight: 600; color: rgba(255,255,255,0.5); "
        "background: transparent; padding: 1px 6px; "
        "border: 1px solid rgba(255,255,255,0.1); border-radius: 4px;");   */
    playerEventLabel_->setVisible(false);

    playerTopRow->addWidget(playerStatusLabel_);
    playerTopRow->addSpacing(6);
    playerTopRow->addWidget(playerEventLabel_);
    playerTopRow->addStretch();

    // Transport buttons
    auto makeMiniBtn = [](const QString& icon, int sz, const QColor& iconColor,
                          const QString& bg, const QString& hov) {
        auto* b = new QPushButton(tintSvg(icon, iconColor), "");
        b->setFixedSize(sz, sz);
        b->setIconSize(QSize(sz/2, sz/2));
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString(
            "QPushButton { background: %1; border: 1px solid rgba(255, 255, 255, 0); border-radius: %2px; "
            "margin: 5px 1px 0 1px; padding: 20px; min-height: 0px; min-width: 0px; }"
            "QPushButton:hover { background: %3; }"
        ).arg(bg).arg(sz/2).arg(hov));
        return b;
    };

    auto* evPlayBtn = makeMiniBtn(":/icons/play.svg", 42, Qt::white,
        "rgba(34,197,94,0.6)", "rgba(34,197,94,0.9)");
    evPlayBtn->setToolTip(tr("Reproducir evento seleccionado"));
    connect(evPlayBtn, &QPushButton::clicked, this, [this]() {
        QString eventId;
        int tabIdx = tabs_->currentIndex();

        if (tabIdx == 0) {
            // Pestaña Día
            auto* item = dayTree_->currentItem();
            if (item) {
                auto* parent = item->parent() ? item->parent() : item;
                eventId = parent->data(1, Qt::UserRole).toString();
            }
        } else if (tabIdx == 1) {
            // Pestaña Semana
            auto* item = weekTree_->currentItem();
            if (item) {
                auto* parent = item->parent() ? item->parent() : item;
                eventId = parent->data(2, Qt::UserRole).toString();
            }
        } else if (tabIdx == 2) {
            // Pestaña No emitidos
            auto* item = expiredTree_->currentItem();
            if (item) {
                auto* parent = item->parent() ? item->parent() : item;
                eventId = parent->data(0, Qt::UserRole).toString();
            }
        }

        if (!eventId.isEmpty()) {
            emit playEventNow(eventId);
        }
    });

    auto* evStopBtn = makeMiniBtn(":/icons/square.svg", 42, Qt::white,
        "rgba(239,68,68,0.6)", "rgba(239,68,68,0.9)");
    evStopBtn->setToolTip(tr("Detener evento"));
    connect(evStopBtn, &QPushButton::clicked, this, &EventsColumn::stopEventNow);

    playerTopRow->addWidget(evPlayBtn);
    playerTopRow->addWidget(evStopBtn);

    playerLayout->addLayout(playerTopRow);

    playerTrackLabel_ = new QLabel(tr("Sin reproducción"));
    playerTrackLabel_->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #e0e0f0; background: transparent; margin:10px 0 6px 0;");
    playerTrackLabel_->setWordWrap(true);
    playerTrackLabel_->setContentsMargins(0, 7, 0, 7);
    playerLayout->addWidget(playerTrackLabel_);

    auto* playerTimeRow = new QHBoxLayout();
    playerTimeRow->setContentsMargins(0, 4, 0, 4);

    playerElapsedLabel_ = new QLabel("00:00");
    playerElapsedLabel_->setStyleSheet(
        "font-size: 22px; font-weight: 300; background: transparent; "
        "font-family: 'Ubuntu Mono', 'Courier New', monospace;");

    playerProgress_ = new QSlider(Qt::Horizontal);
    playerProgress_->setRange(0, 1000);
    playerProgress_->setValue(0);
    playerProgress_->setFixedHeight(22);
    playerProgress_->setEnabled(false);

    playerRemainingLabel_ = new QLabel("-00:00");
    playerRemainingLabel_->setStyleSheet(
        "font-size: 22px; font-weight: 300; background: transparent; "
        "font-family: 'Ubuntu Mono', 'Courier New', monospace;");
    playerRemainingLabel_->setAlignment(Qt::AlignRight);

    playerTimeRow->addWidget(playerElapsedLabel_);
    playerTimeRow->addWidget(playerProgress_, 1);
    playerTimeRow->addWidget(playerRemainingLabel_);
    playerLayout->addLayout(playerTimeRow);

    // VU Meter
    vuMeter_ = new VuMeterWidget(this);
    vuMeter_->setFixedHeight(28);
    playerLayout->addWidget(vuMeter_);

    layout->addWidget(playerFrame);

    // ── Contenido principal ──────────────────────────
    auto* content = new QWidget();
    content->setObjectName("col2Content");
    auto* gl = new QVBoxLayout(content);
    gl->setSpacing(4);
    gl->setContentsMargins(8, 8, 8, 6);

    // Pestañas
    tabs_ = new QTabWidget();

    // ── Pestaña: Hoy ─────────────────────────────────
    auto* dayWidget = new QWidget();
    auto* dayLayout = new QVBoxLayout(dayWidget);
    dayLayout->setContentsMargins(0, 4, 0, 0);

    dayInfoLabel_ = new QLabel();
    dayInfoLabel_->setStyleSheet("font-size: 12px; padding: 3px 0;");
    dayLayout->addWidget(dayInfoLabel_);

    dayTree_ = new QTreeWidget();
    dayTree_->setHeaderLabels({tr("Hora"), tr("Evento"), tr("Duración"), tr("Pri"), tr("Estado")});
    dayTree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    dayTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    dayTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    dayTree_->setFocusPolicy(Qt::NoFocus);
    dayTree_->setRootIsDecorated(true);
    dayTree_->setAnimated(true);
    dayTree_->setIndentation(16);

    dayTree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    dayTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    dayTree_->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    dayTree_->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    dayTree_->header()->setSectionResizeMode(4, QHeaderView::Interactive);
    dayTree_->setColumnWidth(0, 65);
    dayTree_->setColumnWidth(2, 55);
    dayTree_->setColumnWidth(3, 32);
    dayTree_->setColumnWidth(4, 70);

    // Context menu en dayTree
    dayTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dayTree_, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        auto* treeItem = dayTree_->itemAt(pos);
        if (!treeItem) return;

        // Si es hijo, usar el parent
        auto* parentItem = treeItem->parent() ? treeItem->parent() : treeItem;
        QString eventId = parentItem->data(1, Qt::UserRole).toString();
        QString eventName = parentItem->text(1);
        if (eventId.isEmpty()) return;

        QMenu menu(this);
        auto* playAction = menu.addAction(tintSvg(":/icons/play.svg", Qt::white), tr("Reproducir ahora"));
        QAction* editAction = nullptr;
        QAction* duplicateAction = nullptr;
        QAction* deleteAction = nullptr;
        if (programmingVisible_) {
            editAction = menu.addAction(tintSvg(":/icons/settings.svg", Qt::white),  tr("Editar evento"));
            duplicateAction = menu.addAction(tintSvg(":/icons/list.svg", Qt::white), tr("Duplicar evento"));
            menu.addSeparator();
            deleteAction = menu.addAction(tintSvg(":/icons/x.svg", Qt::white), tr("Eliminar evento"));
        }

        auto* result = menu.exec(dayTree_->viewport()->mapToGlobal(pos));
        if (!result) return;
        if (result == playAction) {
            emit playEventClicked(eventId);
        } else if (editAction && result == editAction) {
            emit editEventClicked(eventId);
        } else if (duplicateAction && result == duplicateAction) {
            emit duplicateEventClicked(eventId, eventName);
        } else if (deleteAction && result == deleteAction) {
            auto confirm = QMessageBox::question(
                this, tr("Eliminar evento"),
                tr("¿Eliminar el evento \"%1\"?\n\nSe eliminarán todos sus contenidos y horarios.\n"
                   "Esta acción no se puede deshacer.").arg(eventName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (confirm == QMessageBox::Yes) {
                emit deleteEventClicked(eventId);
            }
        }
    });

    // Doble clic para editar
    connect(dayTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!programmingVisible_) return;
        auto* parentItem = item->parent() ? item->parent() : item;
        QString eventId = parentItem->data(1, Qt::UserRole).toString();
        if (!eventId.isEmpty()) emit editEventClicked(eventId);
    });

    dayLayout->addWidget(dayTree_, 1);

    // ── Pestaña: Semana ──────────────────────────────
    auto* weekWidget = new QWidget();
    auto* weekLayout = new QVBoxLayout(weekWidget);
    weekLayout->setContentsMargins(0, 4, 0, 0);

    weekTree_ = new QTreeWidget();
    weekTree_->setHeaderLabels({tr("Día"), tr("Hora"), tr("Evento"), tr("Duración"), tr("Pri")});
    weekTree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    weekTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    weekTree_->setFocusPolicy(Qt::NoFocus);
    weekTree_->setRootIsDecorated(true);
    weekTree_->setAnimated(true);
    weekTree_->setIndentation(16);
    weekTree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    weekTree_->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    weekTree_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    weekTree_->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    weekTree_->header()->setSectionResizeMode(4, QHeaderView::Interactive);
    weekTree_->setColumnWidth(0, 75);
    weekTree_->setColumnWidth(1, 55);
    weekTree_->setColumnWidth(3, 55);
    weekTree_->setColumnWidth(4, 32);

    weekTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(weekTree_, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        auto* treeItem = weekTree_->itemAt(pos);
        if (!treeItem) return;
        auto* parentItem = treeItem->parent() ? treeItem->parent() : treeItem;
        QString eventId = parentItem->data(2, Qt::UserRole).toString();
        QString eventName = parentItem->text(2);
        if (eventId.isEmpty()) return;

        QMenu menu(this);
        if (!programmingVisible_) return;  // Operación: sin menú en semana
        auto* editAction = menu.addAction(tr("Editar evento"));
        auto* duplicateAction = menu.addAction(tr("Duplicar evento"));
        menu.addSeparator();
        auto* deleteAction = menu.addAction(tr("Eliminar evento"));
        auto* result = menu.exec(weekTree_->viewport()->mapToGlobal(pos));
        if (result == editAction) emit editEventClicked(eventId);
        else if (result == duplicateAction) emit duplicateEventClicked(eventId, eventName);
        else if (result == deleteAction) {
            if (QMessageBox::question(this, tr("Eliminar"),
                    tr("¿Eliminar \"%1\"?").arg(eventName),
                    QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes)
                emit deleteEventClicked(eventId);
        }
    });
    connect(weekTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!programmingVisible_) return;
        auto* p = item->parent() ? item->parent() : item;
        QString id = p->data(2, Qt::UserRole).toString();
        if (!id.isEmpty()) emit editEventClicked(id);
    });

    weekLayout->addWidget(weekTree_, 1);

    // ── Pestaña: No emitidos ──────────────────────────────
    auto* expiredWidget = new QWidget();
    auto* expiredLayout = new QVBoxLayout(expiredWidget);
    expiredLayout->setContentsMargins(0, 4, 0, 0);

    auto* expiredDesc = new QLabel(tr("Eventos que no salieron al aire por vencimiento o corte de programación."));
    expiredDesc->setProperty("class", "dimLabel");
    expiredDesc->setWordWrap(true);
    expiredLayout->addWidget(expiredDesc);

    expiredTree_ = new QTreeWidget();
    expiredTree_->setHeaderLabels({tr("Evento"), tr("Hora"), tr("Fecha"), tr("Motivo")});
    expiredTree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    expiredTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    expiredTree_->setFocusPolicy(Qt::NoFocus);
    expiredTree_->setRootIsDecorated(true);
    expiredTree_->setAnimated(true);
    expiredTree_->setIndentation(16);
    expiredTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    expiredTree_->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    expiredTree_->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    expiredTree_->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    expiredTree_->setColumnWidth(1, 55);
    expiredTree_->setColumnWidth(2, 90);
    expiredTree_->setColumnWidth(3, 70);
    expiredLayout->addWidget(expiredTree_, 1);

    // Agregar pestañas
    QString todayName = currentDayName();
    tabs_->addTab(dayWidget, todayName);
    tabs_->addTab(weekWidget, tr("Semana"));
    tabs_->addTab(expiredWidget, tr("No emitidos"));

    gl->addWidget(tabs_, 1);
    layout->addWidget(content, 1);

    // ── Barra inferior unificada ─────────────────────
    auto* bottomBar = new QWidget();
    bottomBar->setFixedHeight(42);
    bottomBar->setStyleSheet(
        "background: rgba(34,197,94,0.1); "
        "border-top: 1px solid rgba(34,197,94,0.3);");
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(8, 4, 8, 4);
    bottomLayout->setSpacing(5);

    auto makeBottomBtn = [](const QString& icon, const QString& text) {
        auto* btn = new QPushButton(tintSvg(icon, Qt::white), "   " + text);
        btn->setFixedHeight(32);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { background: rgba(34,197,94,0.3); color: #b0d0b0; "
            "border: 1px solid rgba(34,197,94,0.2); border-radius: 6px; "
            "padding: 6px 10px; font-size: 14px; }"
            "QPushButton:hover { background: rgba(34,197,94,0.6); color: #e0f0e0; }");
        return btn;
    };

    clearExpBtn_ = makeBottomBtn(":/icons/x.svg", tr("Limpiar"));
    clearExpBtn_->setObjectName("clearExpBtn");
    clearExpBtn_->setToolTip(tr("Limpiar eventos no emitidos"));
    clearExpBtn_->setVisible(false);  // Solo visible en pestaña No emitidos
    connect(clearExpBtn_, &QPushButton::clicked, this, [this]() {
        if (repo_) { repo_->clearExpiredEvents(); refreshExpiredView(); }
    });

    newEventBtn_ = makeBottomBtn(":/icons/plus.svg", tr("Nuevo"));
    newEventBtn_->setToolTip(tr("Crear nuevo evento o bloque publicitario"));
    connect(newEventBtn_, &QPushButton::clicked, this, &EventsColumn::newEventClicked);

    assignBtn_ = makeBottomBtn(":/icons/music.svg", tr("Asignar"));
    assignBtn_->setToolTip(tr("Agregar audio a múltiples eventos"));
    connect(assignBtn_, &QPushButton::clicked, this, &EventsColumn::inverseAssignClicked);

    summaryBtn_ = makeBottomBtn(":/icons/list.svg", tr("Resumen"));
    summaryBtn_->setToolTip(tr("Resumen semanal de eventos"));
    connect(summaryBtn_, &QPushButton::clicked, this, &EventsColumn::summaryClicked);

    bottomLayout->addWidget(clearExpBtn_);
    bottomLayout->addWidget(newEventBtn_);
    bottomLayout->addWidget(assignBtn_);
    bottomLayout->addWidget(summaryBtn_);

    layout->addWidget(bottomBar);

    // Limpiar solo visible en pestaña No emitidos (index 2)
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int idx) {
        if (clearExpBtn_) clearExpBtn_->setVisible(idx == 2 && programmingVisible_);
    });
}

void EventsColumn::setEventRepository(EventRepository* repo)
{
    repo_ = repo;
    refresh();
}

void EventsColumn::refresh()
{
    refreshDayView();
    refreshWeekView();
    refreshExpiredView();
}

void EventsColumn::refreshDayView()
{
    dayTree_->clear();
    if (!repo_) return;

    QString today = currentDayName();
    auto daySlotList = repo_->getSlotsForDay(today);

    QTime now = QTime::currentTime();
    int count = 0;

    for (const auto& slot : daySlotList) {
        auto event = repo_->getById(slot.eventId);
        if (!event || !event->isValidToday()) continue;

        // Calcular duración total del evento
        auto elements = repo_->getValidElements(event->id);
        int64_t totalDurMs = 0;
        for (const auto& el : elements) {
            totalDurMs += getElementDuration(el);
        }
        QString durStr = totalDurMs > 0 ? formatTime(totalDurMs) : "--:--";

        // Estado
        QString estado;
        QColor statusColor;
        if (slot.triggerTime <= now) {
            estado = tr("Pasado");
            statusColor = QColor(112, 112, 144);
        } else {
            estado = tr("Pendiente");
            statusColor = QColor("#f59e0b");
        }

        // Item padre (evento)
        auto* parent = new QTreeWidgetItem(dayTree_);
        parent->setText(0, slot.triggerTime.toString(tr("HH:mm")));
        parent->setText(1, event->name);
        parent->setText(2, durStr);
        parent->setText(3, QString::number(event->priority));
        parent->setText(4, estado);
        parent->setData(1, Qt::UserRole, event->id);
        parent->setTextAlignment(0, Qt::AlignCenter);
        parent->setTextAlignment(2, Qt::AlignCenter);
        parent->setTextAlignment(3, Qt::AlignCenter);
        parent->setTextAlignment(4, Qt::AlignCenter);
        parent->setForeground(4, statusColor);

        // Fondo según proximidad
        if (slot.triggerTime > now && slot.triggerTime < now.addSecs(3600)) {
            QColor highlight(245, 158, 11, 20);
            for (int c = 0; c < 5; ++c) parent->setBackground(c, highlight);
        }

        // Items hijos (elementos del evento)
        for (const auto& el : elements) {
            auto* child = new QTreeWidgetItem(parent);
            child->setText(0, "");
            QString prefix = QString::fromUtf8("  ↳ ");
            child->setText(1, prefix + el.displayName);
            int64_t elDur = getElementDuration(el);
            child->setText(2, elDur > 0 ? formatTime(elDur) : "--:--");
            child->setTextAlignment(2, Qt::AlignCenter);
            child->setForeground(1, QColor(160, 160, 190));
            child->setForeground(2, QColor(140, 140, 170));
        }

        ++count;
    }

    dayInfoLabel_->setText(tr("%1 — %2 evento(s) programado(s)")
        .arg(today).arg(count));
}

void EventsColumn::refreshWeekView()
{
    weekTree_->clear();
    if (!repo_) return;

    auto summary = repo_->getWeeklySummary();
    for (const auto& item : summary) {
        auto elements = repo_->getValidElements(item.eventId);
        int64_t totalDurMs = 0;
        for (const auto& el : elements) totalDurMs += getElementDuration(el);

        auto* parent = new QTreeWidgetItem(weekTree_);
        parent->setText(0, item.day);
        parent->setText(1, item.triggerTime.toString(tr("HH:mm")));
        parent->setText(2, item.eventName);
        parent->setText(3, totalDurMs > 0 ? formatTime(totalDurMs) : "--:--");
        parent->setText(4, QString::number(item.priority));
        parent->setData(2, Qt::UserRole, item.eventId);
        parent->setTextAlignment(1, Qt::AlignCenter);
        parent->setTextAlignment(3, Qt::AlignCenter);
        parent->setTextAlignment(4, Qt::AlignCenter);

        for (const auto& el : elements) {
            auto* child = new QTreeWidgetItem(parent);
            child->setText(2, QString::fromUtf8("  ↳ ") + el.displayName);
            child->setText(3, getElementDuration(el) > 0 ? formatTime(getElementDuration(el)) : "--:--");
            child->setTextAlignment(3, Qt::AlignCenter);
            child->setForeground(2, QColor(160, 160, 190));
            child->setForeground(3, QColor(140, 140, 170));
        }
    }
}

QString EventsColumn::currentDayName() const
{
    int dow = QDate::currentDate().dayOfWeek();
    if (dow >= 1 && dow <= 7) return DAYS[dow - 1];
    return DAYS[0];
}

void EventsColumn::refreshExpiredView()
{
    if (!repo_ || !expiredTree_) return;

    expiredTree_->clear();

    auto expired = repo_->getExpiredEvents(50);
    for (const auto& info : expired) {
        auto* parent = new QTreeWidgetItem(expiredTree_);
        parent->setText(0, info.eventName);
        parent->setData(0, Qt::UserRole, info.eventId);
        parent->setText(1, info.scheduledTime);
        parent->setText(2, info.expiredAt);
        QString reason = info.reason == "timeout"
            ? tr("Tiempo agotado") : tr("Sin espera");
        parent->setText(3, reason);
        parent->setTextAlignment(1, Qt::AlignCenter);
        parent->setTextAlignment(2, Qt::AlignCenter);
        parent->setTextAlignment(3, Qt::AlignCenter);

        // Elementos del evento
        auto elements = repo_->getElements(info.eventId);
        for (const auto& el : elements) {
            auto* child = new QTreeWidgetItem(parent);
            child->setText(0, QString::fromUtf8("  ↳ ") + el.displayName);
            child->setText(1, getElementDuration(el) > 0 ? formatTime(getElementDuration(el)) : "--:--");
            child->setTextAlignment(1, Qt::AlignCenter);
            child->setForeground(0, QColor(160, 160, 190));
            child->setForeground(1, QColor(140, 140, 170));
        }
    }

    if (!expired.isEmpty()) {
        tabs_->setTabText(2, tr("No emitidos (%1)").arg(expired.size()));
    } else {
        tabs_->setTabText(2, tr("No emitidos"));
    }
}

void EventsColumn::setEventsPipeline(AudioPipeline* pipeline)
{
    eventsPipeline_ = pipeline;
    if (!pipeline) return;

    connect(pipeline, &AudioPipeline::positionUpdated,
            this, &EventsColumn::onPositionUpdated);
}

void EventsColumn::onEventStarted(const QString& eventName)
{
    playerStatusLabel_->setText(tr("● AL AIRE"));
    playerStatusLabel_->setStyleSheet(
        "font-size: 11px; font-weight: 800; color: #22c55e; "
        "letter-spacing: 1px; background: transparent;");
    playerEventLabel_->setText(eventName);
    playerEventLabel_->setVisible(true);
    playerTrackLabel_->setText("...");
    playerElapsedLabel_->setText("00:00");
    playerRemainingLabel_->setText("-00:00");
    playerProgress_->setValue(0);
}

void EventsColumn::onEventFinished(const QString& /*eventName*/)
{
    stopStreamTimer();
    playerStatusLabel_->setText(tr("● PARADO"));
    playerStatusLabel_->setStyleSheet(
        "font-size: 11px; font-weight: 800; color: #ef4444; "
        "letter-spacing: 1px; background: transparent;");
    playerEventLabel_->setVisible(false);
    playerTrackLabel_->setText(tr("Sin reproducción"));
    playerElapsedLabel_->setText("00:00");
    playerRemainingLabel_->setText("-00:00");
    playerRemainingLabel_->setStyleSheet(
        "font-size: 22px; font-weight: 300; background: transparent; "
        "font-family: 'Ubuntu Mono', 'Courier New', monospace;");
    playerProgress_->setValue(0);
}

void EventsColumn::onElementPlaying(const QString& elementName, const QString& eventName)
{
    stopStreamTimer();  // Stop previous stream timer if any
    playerEventLabel_->setText(eventName);
    playerTrackLabel_->setText(elementName);
    playerElapsedLabel_->setText("00:00");
    playerRemainingLabel_->setText("-00:00");
    playerRemainingLabel_->setStyleSheet(
        "font-size: 22px; font-weight: 300; background: transparent; "
        "font-family: 'Ubuntu Mono', 'Courier New', monospace;");
    playerProgress_->setValue(0);
}

void EventsColumn::onPositionUpdated(int64_t posMs, int64_t durMs)
{
    if (streamMode_) return;  // Stream uses its own timer
    if (durMs <= 0) return;

    playerElapsedLabel_->setText(formatTime(posMs));

    int64_t remaining = durMs - posMs;
    if (remaining < 0) remaining = 0;
    QTime endTime = QTime::currentTime().addMSecs(remaining);
    playerRemainingLabel_->setText(
        "-" + formatTime(remaining) + " / " + endTime.toString("HH:mm"));

    playerProgress_->setValue(static_cast<int>((posMs * 1000) / durMs));

    if (remaining > 0 && remaining < 15000) {
        playerRemainingLabel_->setStyleSheet(
            "font-size: 22px; font-weight: 300; color: #ef4444; "
            "background: transparent; "
            "font-family: 'Ubuntu Mono', 'Courier New', monospace;");
    } else {
        playerRemainingLabel_->setStyleSheet(
            "font-size: 22px; font-weight: 300; background: transparent; "
            "font-family: 'Ubuntu Mono', 'Courier New', monospace;");
    }
}

void EventsColumn::startStreamTimer(int64_t durationMs)
{
    streamMode_ = true;
    streamDurationMs_ = durationMs;
    streamElapsedMs_ = 0;

    if (!streamTimer_) {
        streamTimer_ = new QTimer(this);
        streamTimer_->setInterval(500);
        connect(streamTimer_, &QTimer::timeout, this, [this]() {
            streamElapsedMs_ += 500;

            playerElapsedLabel_->setText(formatTime(streamElapsedMs_));

            if (streamDurationMs_ > 0) {
                // Con duración: cuenta regresiva
                int64_t remaining = streamDurationMs_ - streamElapsedMs_;
                if (remaining < 0) remaining = 0;
                QTime endTime = QTime::currentTime().addMSecs(remaining);
                playerRemainingLabel_->setText(
                    "-" + formatTime(remaining) + " / " + endTime.toString("HH:mm"));
                playerProgress_->setValue(
                    static_cast<int>((streamElapsedMs_ * 1000) / streamDurationMs_));

                if (remaining > 0 && remaining < 15000) {
                    playerRemainingLabel_->setStyleSheet(
                        "font-size: 22px; font-weight: 300; color: #ef4444; "
                        "background: transparent; "
                        "font-family: 'Ubuntu Mono', 'Courier New', monospace;");
                }
            } else {
                // Sin duración: cronómetro ascendente + VIVO
                playerRemainingLabel_->setText(
                    "<span style='color:#22c55e; font-weight:700;'>VIVO</span>");
                playerProgress_->setValue(0);
            }
        });
    }
    streamTimer_->start();
}

void EventsColumn::stopStreamTimer()
{
    streamMode_ = false;
    if (streamTimer_) streamTimer_->stop();
}

QString EventsColumn::formatTime(int64_t ms) const
{
    if (ms < 0) ms = 0;
    int totalSecs = static_cast<int>(ms / 1000);
    int mins = totalSecs / 60;
    int secs = totalSecs % 60;
    return QString("%1:%2")
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

int64_t EventsColumn::getElementDuration(const EventElement& el) const
{
    // Si ya tiene duración almacenada (streams o previamente calculada)
    if (el.durationMs > 0) return el.durationMs;
    // Para archivos, leer del metadata reader
    if (metadataReader_ && el.type == ElementType::File && !el.path.isEmpty()) {
        return metadataReader_->getDurationMs(el.path);
    }
    return 0;
}

void EventsColumn::setProgrammingVisible(bool visible)
{
    programmingVisible_ = visible;
    if (newEventBtn_) newEventBtn_->setVisible(visible);
    if (assignBtn_) assignBtn_->setVisible(visible);
    if (summaryBtn_) summaryBtn_->setVisible(visible);
    if (clearExpBtn_) clearExpBtn_->setVisible(visible && tabs_ && tabs_->currentIndex() == 2);
}

} // namespace sara
