#include "ui/schedule_column.h"
#include "ui/vumeter_widget.h"
#include "ui/metatag_editor.h"
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include "audio/audio_pipeline.h"
#include "util/file_scanner.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QIcon>
#include <QFileInfo>
#include <QFrame>
#include <QDateTime>
#include <QApplication>
#include <QPalette>
#include <QFont>
#include <QPainter>
#include <QKeyEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

namespace sara {

// Helper: tintar ícono SVG a color
static QIcon tintSvg(const QString& path, const QColor& color)
{
    QPixmap pix = QIcon(path).pixmap(64, 64);
    QPainter p(&pix);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pix.rect(), color);
    p.end();
    return QIcon(pix);
}

// ══════════════════════════════════════════════════════════
// PlaylistDelegate — pinta cada item como fila multi-columna
// ══════════════════════════════════════════════════════════

void PlaylistDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRect rect = option.rect;
    int row = index.row();
    bool isFirst = (row == 0);
    bool selected = option.state & QStyle::State_Selected;

    // Fondo
    QColor bg;
    if (selected) {
        bg = QColor(102, 126, 234, 90);
    } else if (isFirst) {
        bg = QColor(102, 126, 234, 50);
    } else if (row % 2 == 0) {
        bg = QColor(0, 0, 0, 12);
    } else {
        bg = QColor(0, 0, 0, 4);
    }
    painter->fillRect(rect, bg);

    // Datos del item
    QString title = index.data(Qt::DisplayRole).toString();
    QString duration = index.data(DurationMsRole).toLongLong() > 0
        ? formatDuration(index.data(DurationMsRole).toLongLong()) : QString::fromUtf8("—");
    QString startTime = index.data(StartTimeRole).toString();
    bool hasPisador = index.data(HasPisadorRole).toBool();

    // Color de texto según el TEMA (claro u oscuro), tomado de la paleta del
    // widget. Antes estaba fijo en blanco y, en modo claro, las filas con fondo
    // claro quedaban invisibles. Ahora se adapta: oscuro en tema claro, claro
    // en tema oscuro.
    QColor textColor = option.palette.color(QPalette::Active, QPalette::Text);
    QColor dimColor  = textColor;
    dimColor.setAlpha(170);  // duración/hora un poco más tenues pero legibles

    int x = rect.left() + 6;
    int y = rect.top();
    int h = rect.height();

    // [🎤] Pisador indicator
    if (hasPisador) {
        QFont pisFont = option.font;
        pisFont.setPixelSize(14);
        painter->setFont(pisFont);
        painter->setPen(QColor("#f59e0b"));
        painter->drawText(QRect(x, y, 18, h), Qt::AlignCenter, QString::fromUtf8("🎤 "));
        x += 18;
    }

    // Título
    QFont titleFont = option.font;
    titleFont.setPixelSize(15);
    painter->setFont(titleFont);
    painter->setPen(textColor);

    int rightW = 130;
    int titleW = rect.right() - x - rightW;
    if (titleW > 0) {
        QString elidedTitle = painter->fontMetrics().elidedText(title, Qt::ElideRight, titleW);
        painter->drawText(QRect(x, y, titleW, h), Qt::AlignVCenter | Qt::AlignLeft, elidedTitle);
    }

    // Duración
    painter->setPen(dimColor);
    QFont monoFont = option.font;
    monoFont.setPixelSize(15);
    painter->setFont(monoFont);
    int durX = rect.right() - 125;
    painter->drawText(QRect(durX, y, 55, h), Qt::AlignVCenter | Qt::AlignRight, duration);

    // Hora de inicio
    int startX = rect.right() - 62;
    painter->drawText(QRect(startX, y, 58, h), Qt::AlignVCenter | Qt::AlignRight, startTime);

    painter->restore();
}

QSize PlaylistDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return QSize(200, 34);
}

// Helper estático para formatear duración
QString PlaylistDelegate::formatDuration(int64_t ms)
{
    if (ms <= 0) return QString::fromUtf8("—");
    int totalSec = static_cast<int>(ms / 1000);
    int min = totalSec / 60;
    int sec = totalSec % 60;
    return QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}

// ══════════════════════════════════════════════════════════
// ScheduleColumn
// ══════════════════════════════════════════════════════════

ScheduleColumn::ScheduleColumn(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void ScheduleColumn::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Cabecera con color ───────────────────────────
    auto* headerBar = new QWidget();
    headerBar->setFixedHeight(28);
    headerBar->setStyleSheet(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #667eea, stop:1 #764ba2);"
        "border-radius: 4px 4px 0 0;");
    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(10, 0, 10, 0);
    auto* headerTitle = new QLabel(tr("PROGRAMACIÓN MUSICAL"));
    headerTitle->setStyleSheet(
        "font-size: 11px; font-weight: 700; letter-spacing: 1px; "
        "color: white; background: transparent;");
    headerLayout->addWidget(headerTitle);
    headerLayout->addStretch();
    layout->addWidget(headerBar);

    // ── Contenido principal ──────────────────────────
    auto* content = new QWidget();
    content->setObjectName("col1Content");
    auto* gl = new QVBoxLayout(content);
    gl->setSpacing(4);
    gl->setContentsMargins(8, 8, 8, 6);

    // ══════════════════════════════════════════════════
    // Cabecera "AL AIRE"
    // ══════════════════════════════════════════════════

    auto* airFrame = new QWidget();
    auto* airLayout = new QVBoxLayout(airFrame);
    airLayout->setContentsMargins(12, 8, 12, 8);
    airLayout->setSpacing(6);

    auto* topRow = new QHBoxLayout();

    onAirLabel_ = new QLabel(tr("● AL AIRE"));
    onAirLabel_->setStyleSheet("font-size: 11px; font-weight: 800; color: #22c55e; "
                               "letter-spacing: 1px;");

    sourceLabel_ = new QLabel(tr("Alternativa"));
    sourceLabel_->setProperty("class", "sourceLabel");

    topRow->addWidget(onAirLabel_);
    topRow->addSpacing(6);
    topRow->addWidget(sourceLabel_);
    topRow->addStretch();

    // Transport buttons
    auto makeTransBtn = [](const QString& icon, int sz, const QColor& iconColor,
                           const QString& bg, const QString& hov) {
        auto* b = new QPushButton(tintSvg(icon, iconColor), "");
        b->setFixedSize(sz, sz);
        b->setIconSize(QSize(sz/2, sz/2));
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString(
            "QPushButton { background: %1; border: 1px solid rgba(255, 255, 255, 0); border-radius: %2px; "
            "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }"
            "QPushButton:hover { background: %3; }"
        ).arg(bg).arg(sz/2).arg(hov));
        return b;
    };

    auto* playBtn = makeTransBtn(":/icons/play.svg", 42, Qt::white,
        "rgba(34,197,94,0.6)", "rgba(34,197,94,0.9)");
    playBtn->setToolTip(tr("Reproducir"));
    connect(playBtn, &QPushButton::clicked, this, &ScheduleColumn::playClicked);

    auto* pauseBtn = makeTransBtn(":/icons/pause.svg", 42, Qt::white,
        "rgba(253,161,8,0.6)", "rgba(253,161,8,0.9)");
    pauseBtn->setToolTip(tr("Pausar"));
    connect(pauseBtn, &QPushButton::clicked, this, &ScheduleColumn::pauseClicked);

    auto* stopBtn = makeTransBtn(":/icons/square.svg", 42, Qt::white,
        "rgba(239,68,68,0.6)", "rgba(239,68,68,0.9)");
    stopBtn->setToolTip(tr("Detener"));
    connect(stopBtn, &QPushButton::clicked, this, &ScheduleColumn::stopClicked);

    auto* skipBtn = makeTransBtn(":/icons/skip-forward.svg", 42, Qt::white,
        "rgba(102,126,234,0.6)", "rgba(102,126,234,0.9)");
    skipBtn->setToolTip(tr("Siguiente"));
    connect(skipBtn, &QPushButton::clicked, this, &ScheduleColumn::skipClicked);

    stopAtEndButton_ = makeTransBtn(":/icons/circle-stop.svg", 42, Qt::white,
        "rgba(239,68,68,0.6)", "rgba(239,68,68,0.9)");
    stopAtEndButton_->setToolTip(tr("Parar al terminar"));
    stopAtEndButton_->setCheckable(true);
    connect(stopAtEndButton_, &QPushButton::clicked, this, &ScheduleColumn::stopAtEndClicked);

    loopButton_ = makeTransBtn(":/icons/repeat.svg", 42, Qt::white,
        "rgba(118,75,162,0.6)", "rgba(118,75,162,0.9)");
    loopButton_->setToolTip(tr("Repetir en bucle"));
    loopButton_->setCheckable(true);
    connect(loopButton_, &QPushButton::toggled, this, [this](bool on) {
        setLoopActive(on);
        emit loopToggled(on);
    });

    loopBlinkTimer_ = new QTimer(this);
    loopBlinkTimer_->setInterval(800);
    connect(loopBlinkTimer_, &QTimer::timeout, this, [this]() {
        if (!loopActive_) return;
        static bool bright = false;
        bright = !bright;
        loopButton_->setStyleSheet(bright
            ? "QPushButton { background: rgba(118,75,162,0.9); border: 1px solid rgba(118,75,162,1); border-radius: 21px; "
              "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }"
            : "QPushButton { background: rgba(118,75,162,0.3); border: 1px solid rgba(118,75,162,1); border-radius: 21px; "
              "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }");
    });

    stopAtEndBlinkTimer_ = new QTimer(this);
    stopAtEndBlinkTimer_->setInterval(800);
    connect(stopAtEndBlinkTimer_, &QTimer::timeout, this, [this]() {
        if (!stopAtEndActive_) return;
        static bool bright = false;
        bright = !bright;
        stopAtEndButton_->setStyleSheet(bright
            ? "QPushButton { background: rgba(239,68,68,0.9); border: 1px solid rgba(239,68,68,1); border-radius: 21px; "
              "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }"
            : "QPushButton { background: rgba(239,68,68,0.3); border: 1px solid rgba(239,68,68,1); border-radius: 21px; "
              "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }");
    });

    topRow->addWidget(playBtn);
    topRow->addWidget(pauseBtn);
    topRow->addWidget(stopBtn);
    topRow->addWidget(stopAtEndButton_);
    topRow->addWidget(skipBtn);
    topRow->addWidget(loopButton_);

    currentTrackLabel_ = new QLabel(tr("Sin reproducción"));
    currentTrackLabel_->setProperty("class", "trackLabel");
    currentTrackLabel_->setWordWrap(true);
    currentTrackLabel_->setContentsMargins(0, 7, 0, 7);

    auto* timeRow = new QHBoxLayout();
    timeRow->setContentsMargins(0, 4, 0, 4);

    elapsedLabel_ = new QLabel("00:00");
    elapsedLabel_->setProperty("class", "timeLabel");
    elapsedLabel_->setStyleSheet(
        "font-size: 22px; font-weight: 300; color: #FFFFFF; "
        "font-family: 'Ubuntu Mono', 'Courier New', monospace;"
    );

    progressBar_ = new QSlider(Qt::Horizontal);
    progressBar_->setRange(0, 1000);
    progressBar_->setValue(0);
    progressBar_->setFixedHeight(22);
    progressBar_->setToolTip(tr("Clic para avanzar/retroceder"));
    connect(progressBar_, &QSlider::sliderPressed, this, [this]() {
        seekingByUser_ = true;
    });
    connect(progressBar_, &QSlider::sliderReleased, this, [this]() {
        seekingByUser_ = false;
        if (pipeline_ && currentDurationMs_ > 0) {
            int64_t seekMs = (progressBar_->value() * currentDurationMs_) / 1000;
            emit seekRequested(seekMs);
        }
    });

    remainingLabel_ = new QLabel("00:00");
    remainingLabel_->setProperty("class", "dimLabel");
    remainingLabel_->setStyleSheet(
        "font-size: 22px; font-weight: 300; color: #FFFFFF; "
        "font-family: 'Ubuntu Mono', 'Courier New', monospace;"
    );
    remainingLabel_->setAlignment(Qt::AlignRight);

    timeRow->addWidget(elapsedLabel_);
    timeRow->addWidget(progressBar_, 1);
    timeRow->addWidget(remainingLabel_);

    airLayout->addLayout(topRow);
    airLayout->addWidget(currentTrackLabel_);
    airLayout->addLayout(timeRow);

    // VU Meter
    vuMeter_ = new VuMeterWidget(this);
    vuMeter_->setFixedHeight(28);
    airLayout->addWidget(vuMeter_);

    gl->addWidget(airFrame);

    // ══════════════════════════════════════════════════
    // Lista de playlist (QListWidget con drag & drop)
    // ══════════════════════════════════════════════════

    auto* queueLabel = new QLabel(tr("COLA DE REPRODUCCIÓN"));
    queueLabel->setProperty("class", "dimLabel");
    queueLabel->setStyleSheet(
        "font-size: 12px; font-weight: 700; "
        "letter-spacing: 1px; padding-top: 5px;padding-bottom: 6px;"
    );

    playlistList_ = new QListWidget();
    playlistList_->setSelectionMode(QAbstractItemView::SingleSelection);
    playlistList_->setFocusPolicy(Qt::StrongFocus);
    playlistList_->setItemDelegate(new PlaylistDelegate(playlistList_));

    // Drag & drop: DragDrop para reordenar Y aceptar drops externos
    // QListWidget (a diferencia de QTableWidget) mueve items completos
    playlistList_->setDragEnabled(true);
    playlistList_->setAcceptDrops(true);
    playlistList_->viewport()->setAcceptDrops(true);
    playlistList_->setDragDropMode(QAbstractItemView::DragDrop);
    playlistList_->setDefaultDropAction(Qt::MoveAction);
    playlistList_->setDropIndicatorShown(true);

    // Actualizar colores y tiempos al reordenar por drag
    connect(playlistList_->model(), &QAbstractItemModel::rowsInserted,
            this, [this]() { updateRowColors(); recalcStartTimes(); });
    connect(playlistList_->model(), &QAbstractItemModel::rowsRemoved,
            this, [this]() { updateRowColors(); recalcStartTimes(); });

    // Context menu
    playlistList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(playlistList_, &QListWidget::customContextMenuRequested,
            this, &ScheduleColumn::showContextMenu);

    // Doble clic: NO reproduce al instante. Marca la pista como SIGUIENTE,
    // moviéndola al tope de la cola (que es lo próximo que va a sonar).
    connect(playlistList_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
        int row = playlistList_->row(item);
        if (row > 0) {
            QListWidgetItem* moved = playlistList_->takeItem(row);
            playlistList_->insertItem(0, moved);
            playlistList_->setCurrentItem(moved);
        }
    });

    // Tecla Suprimir + drops externos
    playlistList_->installEventFilter(this);
    playlistList_->viewport()->installEventFilter(this);

    // Recalcular tiempos cuando el modelo cambia (drag & drop reorder)
    connect(playlistList_->model(), &QAbstractItemModel::rowsInserted,
            this, [this]() { recalcStartTimes(); });
    connect(playlistList_->model(), &QAbstractItemModel::rowsMoved,
            this, [this]() { recalcStartTimes(); });

    gl->addWidget(queueLabel);
    gl->addWidget(playlistList_, 1);

    layout->addWidget(content, 1);

    // ── Barra inferior unificada ─────────────────────
    auto* bottomBar = new QWidget();
    bottomBar->setFixedHeight(42);
    bottomBar->setStyleSheet(
        "background: rgba(102,126,234,0.1); "
        "border-top: 1px solid rgba(102,126,234,0.3);");
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(8, 4, 8, 4);
    bottomLayout->setSpacing(5);

    auto makeBottomBtn = [](const QString& icon, const QString& text) {
        auto* btn = new QPushButton(tintSvg(icon, Qt::white), "   " + text);
        btn->setFixedHeight(32);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { background: rgba(102,126,234,0.3); color: #c0c8f0; "
            "border: 1px solid rgba(102,126,234,0.2); border-radius: 6px; "
            "padding: 6px 10px; font-size: 14px; }"
            "QPushButton:hover { background: rgba(102,126,234,0.6); color: #e0e0f0; }");
        return btn;
    };

    auto* clearQueueBtn = makeBottomBtn(":/icons/x.svg", tr("Vaciar"));
    clearQueueBtn->setToolTip(tr("Vaciar cola de reproducción"));
    connect(clearQueueBtn, &QPushButton::clicked, this, [this]() {
        emit clearQueueRequested();
    });

    auto* regenerateBtn = makeBottomBtn(":/icons/shuffle.svg", tr("Regenerar"));
    regenerateBtn->setToolTip(tr("Regenerar cola desde la programación activa"));
    connect(regenerateBtn, &QPushButton::clicked, this, [this]() {
        emit regenerateRequested();
    });

    schedBtn_ = makeBottomBtn(":/icons/list.svg", tr("Programar"));
    schedBtn_->setToolTip(tr("Gestionar programaciones"));
    connect(schedBtn_, &QPushButton::clicked, this, &ScheduleColumn::manageSchedulesClicked);

    gridBtn_ = makeBottomBtn(":/icons/clock.svg", tr("Grilla"));
    gridBtn_->setToolTip(tr("Grilla semanal de programaciones"));
    connect(gridBtn_, &QPushButton::clicked, this, &ScheduleColumn::weeklyGridClicked);

    auto* listBtn = makeBottomBtn(":/icons/scroll-text.svg", tr("Listas"));
    listBtn->setToolTip(tr("Gestionar listas musicales"));
    connect(listBtn, &QPushButton::clicked, this, &ScheduleColumn::listManagerClicked);

    bottomLayout->addWidget(clearQueueBtn);
    bottomLayout->addWidget(regenerateBtn);
    bottomLayout->addWidget(schedBtn_);
    bottomLayout->addWidget(gridBtn_);
    bottomLayout->addWidget(listBtn);

    layout->addWidget(bottomBar);

    // ── Context menu setup ───────────────────────────
    contextMenu_ = new QMenu(this);
}

// ══════════════════════════════════════════════════════════
// API pública
// ══════════════════════════════════════════════════════════

void ScheduleColumn::setMusicPipeline(AudioPipeline* pipeline)
{
    pipeline_ = pipeline;
    if (!pipeline_) return;

    connect(pipeline_, &AudioPipeline::positionUpdated,
            this, &ScheduleColumn::onPositionUpdated);
    connect(pipeline_, &AudioPipeline::metadataReceived,
            this, &ScheduleColumn::onMetadataReceived);
}

void ScheduleColumn::setCurrentTrack(const QString& name, const QString& source)
{
    currentTrackLabel_->setText(name.isEmpty() ? tr("Sin reproducción") : name);

    if (source.isEmpty()) {
        sourceLabel_->setVisible(false);
    } else {
        sourceLabel_->setText(source);
        sourceLabel_->setVisible(true);
    }

    stopStreamTimer();
    elapsedLabel_->setText("00:00");
    remainingLabel_->setText("00:00");
    progressBar_->setValue(0);
    currentDurationMs_ = 0;
    currentPositionMs_ = 0;

    recalcStartTimes();
}

void ScheduleColumn::startStreamTimer(int64_t durationMs)
{
    stopStreamTimer();
    streamMode_ = true;
    streamDurationMs_ = durationMs;
    streamElapsedMs_ = 0;

    if (!streamTimer_) {
        streamTimer_ = new QTimer(this);
        streamTimer_->setInterval(500);
        connect(streamTimer_, &QTimer::timeout, this, [this]() {
            streamElapsedMs_ += 500;

            elapsedLabel_->setText(formatTime(streamElapsedMs_));

            if (streamDurationMs_ > 0) {
                // Cuenta regresiva
                int64_t remaining = streamDurationMs_ - streamElapsedMs_;
                if (remaining < 0) remaining = 0;
                QTime endTime = QTime::currentTime().addMSecs(remaining);
                remainingLabel_->setText(
                    "-" + formatTime(remaining) + " / " + endTime.toString("HH:mm"));

                int progress = static_cast<int>((streamElapsedMs_ * 1000) / streamDurationMs_);
                progressBar_->setValue(qMin(progress, 1000));

                // Rojo si quedan menos de 15 seg
                if (remaining > 0 && remaining < 15000) {
                    remainingLabel_->setStyleSheet(
                        "font-size: 22px; font-weight: 300; color: #ef4444; "
                        "font-family: 'Ubuntu Mono', 'Courier New', monospace;");
                } else {
                    remainingLabel_->setStyleSheet(
                        "font-size: 22px; font-weight: 300; "
                        "font-family: 'Ubuntu Mono', 'Courier New', monospace;");
                }

                currentDurationMs_ = streamDurationMs_;
                currentPositionMs_ = streamElapsedMs_;
            } else {
                // Sin límite: cuenta ascendente, mostrar VIVO
                remainingLabel_->setText(tr("VIVO"));
                remainingLabel_->setStyleSheet(
                    "font-size: 22px; font-weight: 300; color: #22c55e; "
                    "font-family: 'Ubuntu Mono', 'Courier New', monospace;");
                progressBar_->setValue(0);

                currentDurationMs_ = 0;
                currentPositionMs_ = streamElapsedMs_;
            }
            recalcStartTimes();
        });
    }
    streamTimer_->start();
}

void ScheduleColumn::stopStreamTimer()
{
    streamMode_ = false;
    if (streamTimer_) streamTimer_->stop();
}

void ScheduleColumn::setLoopActive(bool on)
{
    loopActive_ = on;
    if (on) {
        loopButton_->setChecked(true);
        loopBlinkTimer_->start();
    } else {
        loopButton_->setChecked(false);
        loopBlinkTimer_->stop();
        // Restaurar estilo inicial exacto
        loopButton_->setStyleSheet(
            "QPushButton { background: rgba(118,75,162,0.6); border: 1px solid rgba(255,255,255,0); border-radius: 21px; "
            "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }"
            "QPushButton:hover { background: rgba(118,75,162,0.9); }");
    }
}

void ScheduleColumn::setStopAtEndActive(bool on)
{
    stopAtEndActive_ = on;
    stopAtEndButton_->blockSignals(true);
    stopAtEndButton_->setChecked(on);
    stopAtEndButton_->blockSignals(false);
    if (on) {
        stopAtEndBlinkTimer_->start();
    } else {
        stopAtEndBlinkTimer_->stop();
        // Restaurar estilo inicial exacto
        stopAtEndButton_->setStyleSheet(
            "QPushButton { background: rgba(239,68,68,0.6); border: 1px solid rgba(255,255,255,0); border-radius: 21px; "
            "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }"
            "QPushButton:hover { background: rgba(239,68,68,0.9); }");
    }
}

void ScheduleColumn::addToQueue(const QString& displayName, const QString& filePath,
                                 int64_t durationMs)
{
    auto* item = new QListWidgetItem(displayName);
    item->setData(FilePathRole, filePath);
    item->setData(DurationMsRole, static_cast<qlonglong>(durationMs));
    item->setData(StartTimeRole, "—");
    item->setData(HasPisadorRole, false);
    item->setToolTip(filePath);
    playlistList_->addItem(item);

    updateRowColors();
    recalcStartTimes();
}

void ScheduleColumn::insertInQueue(int position, const QString& displayName,
                                    const QString& filePath, int64_t durationMs)
{
    if (position < 0 || position > playlistList_->count()) {
        position = playlistList_->count();
    }

    auto* item = new QListWidgetItem(displayName);
    item->setData(FilePathRole, filePath);
    item->setData(DurationMsRole, static_cast<qlonglong>(durationMs));
    item->setData(StartTimeRole, "—");
    item->setData(HasPisadorRole, false);
    item->setToolTip(filePath);
    playlistList_->insertItem(position, item);

    updateRowColors();
    recalcStartTimes();
}

void ScheduleColumn::removeFromQueue(int row)
{
    if (row >= 0 && row < playlistList_->count()) {
        delete playlistList_->takeItem(row);
        updateRowColors();
        recalcStartTimes();
    }
}

void ScheduleColumn::clearQueue()
{
    playlistList_->clear();
}

int ScheduleColumn::queueSize() const
{
    return playlistList_->count();
}

QString ScheduleColumn::getTrackAt(int row) const
{
    if (row < 0 || row >= playlistList_->count()) return {};
    auto* item = playlistList_->item(row);
    return item ? item->data(FilePathRole).toString() : QString();
}

QString ScheduleColumn::getTrackNameAt(int row) const
{
    if (row < 0 || row >= playlistList_->count()) return {};
    auto* item = playlistList_->item(row);
    return item ? item->text() : QString();
}

int64_t ScheduleColumn::getTrackDurationAt(int row) const
{
    if (row < 0 || row >= playlistList_->count()) return 0;
    auto* item = playlistList_->item(row);
    return item ? item->data(DurationMsRole).toLongLong() : 0;
}

// ══════════════════════════════════════════════════════════
// Slots privados
// ══════════════════════════════════════════════════════════

void ScheduleColumn::onPositionUpdated(int64_t posMs, int64_t durMs)
{
    // En modo stream, el timer propio maneja el display
    if (streamMode_) return;

    currentDurationMs_ = durMs;
    currentPositionMs_ = posMs;

    if (durMs > 0 && !seekingByUser_) {
        progressBar_->setValue(static_cast<int>((posMs * 1000) / durMs));
    }

    elapsedLabel_->setText(formatTime(posMs));

    int64_t remaining = durMs - posMs;
    QTime endTime = QTime::currentTime().addMSecs(remaining > 0 ? remaining : 0);
    QString remainText = "-" + formatTime(remaining > 0 ? remaining : 0)
                         + " / " + endTime.toString("HH:mm");
    remainingLabel_->setText(remainText);

    if (remaining > 0 && remaining < 15000) {
        remainingLabel_->setStyleSheet(
            "font-size: 22px; font-weight: 300; color: #ef4444; "
            "font-family: 'Ubuntu Mono', 'Courier New', monospace;"
        );
    } else {
        remainingLabel_->setStyleSheet(
            "font-size: 22px; font-weight: 300; "
            "font-family: 'Ubuntu Mono', 'Courier New', monospace;"
        );
    }
}

void ScheduleColumn::onMetadataReceived(const TrackMetadata& meta)
{
    QString display;
    if (!meta.title.isEmpty()) {
        display = meta.title;
        if (!meta.artist.isEmpty()) {
            display += QString::fromUtf8(" — ") + meta.artist;
        }
    } else {
        display = QFileInfo(meta.filePath).completeBaseName();
    }
    currentTrackLabel_->setText(display);

    if (meta.durationMs > 0) {
        currentDurationMs_ = meta.durationMs;
        recalcStartTimes();
    }
}

void ScheduleColumn::showContextMenu(const QPoint& pos)
{
    auto* clickedItem = playlistList_->itemAt(pos);
    if (!clickedItem) return;
    int row = playlistList_->row(clickedItem);
    if (row < 0) return;

    contextMenu_->clear();

    QString filePath = clickedItem->data(FilePathRole).toString();

    auto* skipAction = contextMenu_->addAction(
        tintSvg(":/icons/skip-forward.svg", Qt::white), tr("Saltar a esta pista"));
    connect(skipAction, &QAction::triggered, this, [this, row]() {
        emit skipRequested(row);
    });

    auto* cueAction = contextMenu_->addAction(
        tintSvg(":/icons/volume-2.svg", Qt::white), tr("Preescuchar (CUE)"));
    connect(cueAction, &QAction::triggered, this, [this, filePath]() {
        emit cuePreviewRequested(filePath);
    });

    contextMenu_->addSeparator();

    auto* removeAction = contextMenu_->addAction(
        tintSvg(":/icons/x.svg", Qt::white), tr("Eliminar de la cola"));
    connect(removeAction, &QAction::triggered, this, [this, row]() {
        QString fp = getTrackAt(row);
        removeFromQueue(row);
        emit removeRequested(fp);
    });

    contextMenu_->addSeparator();

    if (row > 0) {
        auto* moveFirstAction = contextMenu_->addAction(
            tintSvg(":/icons/chevron-left.svg", Qt::white), tr("Mover primera"));
        connect(moveFirstAction, &QAction::triggered, this, [this, row]() {
            auto* item = playlistList_->takeItem(row);
            if (item) {
                playlistList_->insertItem(0, item);
                playlistList_->setCurrentRow(0);
                updateRowColors();
                recalcStartTimes();
            }
        });

        auto* moveUpAction = contextMenu_->addAction(tr("Mover arriba"));
        connect(moveUpAction, &QAction::triggered, this, [this, row]() {
            if (row > 0) {
                auto* item = playlistList_->takeItem(row);
                if (item) {
                    playlistList_->insertItem(row - 1, item);
                    playlistList_->setCurrentRow(row - 1);
                    recalcStartTimes();
                }
            }
        });
    }

    if (row < playlistList_->count() - 1) {
        auto* moveDownAction = contextMenu_->addAction(tr("Mover abajo"));
        connect(moveDownAction, &QAction::triggered, this, [this, row]() {
            if (row < playlistList_->count() - 1) {
                auto* item = playlistList_->takeItem(row);
                if (item) {
                    playlistList_->insertItem(row + 1, item);
                    playlistList_->setCurrentRow(row + 1);
                    recalcStartTimes();
                }
            }
        });
    }

    contextMenu_->addSeparator();

    // Editar metatags
    auto* editTagsAction = contextMenu_->addAction(
        tintSvg(":/icons/disc-2.svg", Qt::white), tr("Editar metatags"));
    connect(editTagsAction, &QAction::triggered, this, [this, filePath, row]() {
        if (filePath.startsWith("http") || filePath.startsWith("__SARA")) return;
        auto* editor = new MetatagEditor(filePath, this);
        if (editor->exec() == QDialog::Accepted && editor->wasModified()) {
            // Refrescar el nombre en la cola con los nuevos tags
            TagLib::FileRef f(filePath.toUtf8().constData());
            if (!f.isNull() && f.tag()) {
                QString title = QString::fromStdString(f.tag()->title().to8Bit(true));
                QString artist = QString::fromStdString(f.tag()->artist().to8Bit(true));
                QString displayName;
                if (!title.isEmpty() && !artist.isEmpty()) {
                    displayName = title + QString::fromUtf8(" — ") + artist;
                } else if (!title.isEmpty()) {
                    displayName = title;
                } else {
                    displayName = QFileInfo(filePath).completeBaseName();
                }
                auto* item = playlistList_->item(row);
                if (item) {
                    item->setText(displayName);
                    playlistList_->update();
                }
            }
        }
        delete editor;
    });

    contextMenu_->addSeparator();

    // Pisador
    auto* pisadorMenu = contextMenu_->addMenu(tintSvg(":/icons/music.svg", Qt::white), tr("Pisador"));

    auto* pisadorSpecific = pisadorMenu->addAction(tr("Asignar pisador específico..."));
    connect(pisadorSpecific, &QAction::triggered, this, [this, filePath]() {
        emit pisadorAssignRequested(filePath, "specific");
    });

    auto* pisadorRandom = pisadorMenu->addAction(tr("Asignar aleatorio (de carpeta)"));
    connect(pisadorRandom, &QAction::triggered, this, [this, filePath]() {
        emit pisadorAssignRequested(filePath, "random");
    });

    auto* pisadorTime = pisadorMenu->addAction(tr("Pisar con locución de hora"));
    connect(pisadorTime, &QAction::triggered, this, [this, filePath]() {
        emit pisadorAssignRequested(filePath, "time");
    });

    pisadorMenu->addSeparator();

    auto* pisadorNone = pisadorMenu->addAction(tr("No pisar nunca esta pista"));
    connect(pisadorNone, &QAction::triggered, this, [this, filePath]() {
        emit pisadorAssignRequested(filePath, "none");
    });

    auto* pisadorRemove = pisadorMenu->addAction(tr("Quitar asignación individual"));
    connect(pisadorRemove, &QAction::triggered, this, [this, filePath]() {
        emit pisadorAssignRequested(filePath, "remove");
    });

    contextMenu_->popup(playlistList_->viewport()->mapToGlobal(pos));
}

// ══════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════

void ScheduleColumn::updateRowColors()
{
    for (int row = 0; row < playlistList_->count(); ++row) {
        auto* item = playlistList_->item(row);
        if (!item) continue;

        // Actualizar indicador de pisador
        QString filePath = item->data(FilePathRole).toString();
        bool hasPis = hasPisador_ && !filePath.isEmpty() && hasPisador_(filePath);
        item->setData(HasPisadorRole, hasPis);
    }
    playlistList_->viewport()->update();
}

void ScheduleColumn::recalcStartTimes()
{
    QTime currentTime = QTime::currentTime();
    int64_t remainingMs = currentDurationMs_ - currentPositionMs_;
    if (remainingMs < 0) remainingMs = 0;

    QTime nextStart = currentTime.addMSecs(remainingMs);

    for (int row = 0; row < playlistList_->count(); ++row) {
        auto* item = playlistList_->item(row);
        if (!item) continue;

        item->setData(StartTimeRole, formatClock(nextStart));

        int64_t durMs = item->data(DurationMsRole).toLongLong();
        if (durMs > 0) {
            nextStart = nextStart.addMSecs(durMs);
        } else {
            nextStart = nextStart.addMSecs(180000);
        }
    }
    playlistList_->viewport()->update();
}

QString ScheduleColumn::formatTime(int64_t ms) const
{
    if (ms < 0) ms = 0;
    int totalSec = static_cast<int>(ms / 1000);
    int min = totalSec / 60;
    int sec = totalSec % 60;
    return QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}

QString ScheduleColumn::formatClock(const QTime& t) const
{
    return t.toString(tr("HH:mm"));
}

bool ScheduleColumn::eventFilter(QObject* obj, QEvent* event)
{
    // Keyboard: Delete/Backspace
    if (obj == playlistList_ && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) {
            int row = playlistList_->currentRow();
            if (row >= 0) {
                QString filePath = getTrackAt(row);
                removeFromQueue(row);
                emit removeRequested(filePath);
            }
            return true;
        }
    }

    // Drop externo: aceptar archivos de audio desde el explorador
    if (obj == playlistList_->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            auto* de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasUrls()) {
                de->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::DragMove) {
            auto* de = static_cast<QDragMoveEvent*>(event);
            if (de->mimeData()->hasUrls()) {
                de->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto* de = static_cast<QDropEvent*>(event);
            if (de->mimeData()->hasUrls()) {
                // Determinar posición de inserción
                int insertAt = playlistList_->count(); // Por defecto al final
                auto* itemAt = playlistList_->itemAt(de->position().toPoint());
                if (itemAt) {
                    insertAt = playlistList_->row(itemAt);
                }

                QStringList paths;
                for (const auto& url : de->mimeData()->urls()) {
                    QString path = url.toLocalFile();
                    if (!path.isEmpty() && FileScanner::isAudioFile(path)) {
                        paths << path;
                    }
                }
                if (!paths.isEmpty()) {
                    emit filesDropped(paths, insertAt);
                }
                de->acceptProposedAction();
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ScheduleColumn::setProgrammingVisible(bool visible)
{
    if (schedBtn_) schedBtn_->setVisible(visible);
    if (gridBtn_) gridBtn_->setVisible(visible);
}

} // namespace sara
