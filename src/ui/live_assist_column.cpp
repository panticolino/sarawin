#include "ui/live_assist_column.h"
#include "data/instant_play_repo.h"
#include "data/stream_preset_repo.h"
#include "audio/audio_pipeline.h"
#include "util/file_scanner.h"
#include "util/metadata_reader.h"
#include "ui/metatag_editor.h"
#include "util/logger.h"
#include <taglib/fileref.h>
#include <taglib/tag.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QLabel>
#include <QIcon>
#include <QHeaderView>
#include <QDir>
#include <QDirIterator>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QMenu>
#include <QFileDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QLocale>
#include <QShortcut>
#include <QKeySequence>
#include <QFrame>
#include <QItemSelectionModel>
#include <QPainter>

namespace sara {

// Helper: tintar ícono SVG a color
static QIcon tintNavIcon(const QString& path, const QColor& color)
{
    QPixmap pix = QIcon(path).pixmap(64, 64);
    QPainter painter(&pix);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pix.rect(), color);
    painter.end();
    return QIcon(pix);
}

static const QString NAV_ACTIVE_STYLE =
    "QPushButton { background: rgba(102,126,234,0.25); color: #ffffff; "
    "border: 1px solid rgba(102,126,234,0.4); border-radius: 6px; "
    "font-size: 14px; font-weight: 600; padding: 7px 12px; }"
    "QPushButton:hover { background: rgba(102,126,234,0.35); }";

static const QString NAV_INACTIVE_STYLE =
    "QPushButton { background: transparent; color: #667eea; "
    "border: 1px solid rgba(102,126,234,0.15); border-radius: 6px; "
    "font-size: 14px; font-weight: 500; padding: 7px 12px; }"
    "QPushButton:hover { background: rgba(102,126,234,0.12); }";

// Estilos de botones
static const QString BTN_EMPTY_STYLE =
    "QPushButton { background: rgba(255,255,255,0.04); color: #606080; "
    "border: 1px solid rgba(255,255,255,0.06); border-radius: 8px; "
    "font-size: 12px; font-weight: 600; text-align: center; padding: 4px; }"
    "QPushButton:hover { background: rgba(255,255,255,0.08); }";

static const QString BTN_ASSIGNED_STYLE =
    "QPushButton { background: rgba(102,126,234,0.12); color: #c0c8f0; "
    "border: 1px solid rgba(102,126,234,0.25); border-radius: 8px; "
    "font-size: 12px; font-weight: 600; text-align: center; padding: 4px; }"
    "QPushButton:hover { background: rgba(102,126,234,0.22); }";

static const QString BTN_ACTIVE_STYLE =
    "QPushButton { background: rgba(34,197,94,0.2); color: #4ade80; "
    "border: 2px solid rgba(34,197,94,0.5); border-radius: 8px; "
    "font-size: 12px; font-weight: 700; text-align: center; padding: 4px; }"
    "QPushButton:hover { background: rgba(34,197,94,0.3); }";

LiveAssistColumn::LiveAssistColumn(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Cabecera con color ───────────────────────────
    auto* headerBar = new QWidget();
    headerBar->setFixedHeight(28);
    headerBar->setStyleSheet(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #764ba2, stop:1 #667eea);"
        "border-radius: 4px 4px 0 0;");
    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(10, 0, 10, 0);
    auto* headerTitle = new QLabel(tr("ASISTENTE EN VIVO"));
    headerTitle->setStyleSheet(
        "font-size: 12px; font-weight: 700; letter-spacing: 1px; "
        "color: white; background: transparent;");
    headerLayout->addWidget(headerTitle);
    headerLayout->addStretch();
    layout->addWidget(headerBar);

    // La botonera (instant play) ya NO va arriba: ahora vive dentro del
    // explorador, en la pestaña "Botonera" (se construye en setupFileExplorer).
    // El explorador ocupa toda la altura de la columna.
    auto* explorerWidget = new QWidget();
    setupFileExplorer(explorerWidget);
    layout->addWidget(explorerWidget, 1);

    // Atajos de teclado F1-F12
    for (int i = 0; i < 12; ++i) {
        auto* shortcut = new QShortcut(QKeySequence(Qt::Key_F1 + i), this);
        connect(shortcut, &QShortcut::activated, this, [this, i]() {
            playSlot(i);
        });
    }

    // Timer para actualizar progreso
    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(250);
    connect(progressTimer_, &QTimer::timeout, this, &LiveAssistColumn::updateProgress);
}

void LiveAssistColumn::setInstantPlayRepo(InstantPlayRepo* repo)
{
    repo_ = repo;
    presetCombo_->clear();
    auto presets = repo_->getAllPresets();
    for (const auto& p : presets) {
        presetCombo_->addItem(p.name, p.id);
    }
    if (presets.isEmpty()) {
        QString id = repo_->ensureDefaultPreset();
        presetCombo_->addItem(tr("Predeterminado"), id);
    }
    if (presetCombo_->count() > 0) {
        currentPresetId_ = presetCombo_->currentData().toString();
        refreshButtons();
    }
}

void LiveAssistColumn::setInstantPipeline(AudioPipeline* pipeline)
{
    pipeline_ = pipeline;
    if (pipeline_) {
        connect(pipeline_, &AudioPipeline::trackFinished,
                this, &LiveAssistColumn::onInstantTrackFinished);
    }
}

void LiveAssistColumn::setMusicFolder(const QString& path)
{
    musicFolder_ = path;
    if (navMusicBtn_) {
        navMusicBtn_->setEnabled(!path.isEmpty());
        navMusicBtn_->setToolTip(path.isEmpty() ? tr("Sin carpeta alternativa") : path);
    }
}

void LiveAssistColumn::setRadioFolder(const QString& path)
{
    radioFolder_ = path;
    if (navRadioBtn_) {
        navRadioBtn_->setEnabled(!path.isEmpty());
        navRadioBtn_->setToolTip(path.isEmpty() ? tr("Sin carpeta de radio configurada") : path);
    }
}

// ══════════════════════════════════════════════════════════
// Setup UI
// ══════════════════════════════════════════════════════════

void LiveAssistColumn::setupInstantPlay(QWidget* container)
{
    auto* gl = new QVBoxLayout(container);
    gl->setContentsMargins(8, 8, 8, 6);
    gl->setSpacing(4);

    // ══════════════════════════════════════════════════
    // Cabecera estilo unificado: AL AIRE + Preset + controles
    // ══════════════════════════════════════════════════
    auto* airFrame = new QWidget();
    auto* airLayout = new QVBoxLayout(airFrame);
    airLayout->setContentsMargins(12, 8, 12, 8);
    airLayout->setSpacing(6);

    auto* topRow = new QHBoxLayout();

    onAirLabel_ = new QLabel(tr("● PARADO"));
    onAirLabel_->setStyleSheet(
        "font-size: 11px; font-weight: 800; color: #ef4444; "
        "letter-spacing: 1px;");

    presetCombo_ = new QComboBox();
    presetCombo_->setMinimumWidth(100);
    presetCombo_->setStyleSheet(
        "font-size: 13px; padding: 8px 8px; "
        "border: 1px solid rgba(255,255,255,0.1); border-radius: 4px;");
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LiveAssistColumn::onPresetChanged);

    manageBtn_ = new QPushButton(tintNavIcon(":/icons/settings.svg", Qt::white), "");
    manageBtn_->setFixedSize(42, 42);
    manageBtn_->setCursor(Qt::PointingHandCursor);
    manageBtn_->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #667eea, stop:1 #764ba2); border: 1px solid rgba(255, 255, 255, 0); border-radius: 21px; "
        "margin: 0;  padding: 21px; min-height: 0px; min-width: 0px; }"
        "QPushButton:hover {background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #667eea, stop:1 #764ba2); }");
    manageBtn_->setToolTip(tr("Gestionar presets"));
    connect(manageBtn_, &QPushButton::clicked, this, &LiveAssistColumn::onManagePresets);

    // Transport: Stop + Loop (circulares)
    stopBtn_ = new QPushButton(tintNavIcon(":/icons/square.svg", Qt::white), "");
    stopBtn_->setFixedSize(42, 42);
    stopBtn_->setCursor(Qt::PointingHandCursor);
    stopBtn_->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.6); border: 1px solid rgba(255, 255, 255, 0); border-radius: 21px; "
        "margin:0; padding: 21px; min-height: 0px; min-width: 0px; }"
        "QPushButton:hover { background: rgba(239,68,68,0.9); }");
    stopBtn_->setToolTip(tr("Detener reproducción"));
    connect(stopBtn_, &QPushButton::clicked, this, &LiveAssistColumn::stopPlayback);

    loopCheck_ = new QCheckBox();
    loopCheck_->setVisible(false);

    loopBtn_ = new QPushButton(tintNavIcon(":/icons/repeat.svg", Qt::white), "");
    loopBtn_->setFixedSize(42, 42);
    loopBtn_->setCursor(Qt::PointingHandCursor);
    loopBtn_->setCheckable(true);
    loopBtn_->setStyleSheet(
        "QPushButton { background: rgba(118,75,162,0.6); border: 1px solid rgba(255, 255, 255, 0); border-radius: 21px; "
        "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }"
        "QPushButton:hover { background: rgba(118,75,162,0.9); }");
    loopBtn_->setToolTip(tr("Repetir en bucle"));
    connect(loopBtn_, &QPushButton::toggled, this, [this](bool on) {
        loopCheck_->setChecked(on);
        loopActive_ = on;
        if (on) {
            loopBlinkTimer_->start();
        } else {
            loopBlinkTimer_->stop();
            loopBtn_->setStyleSheet(
                "QPushButton { background: rgba(118,75,162,0.6); border: 1px solid rgba(255,255,255,0); border-radius: 21px; "
                "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }"
                "QPushButton:hover { background: rgba(118,75,162,0.9); }");
        }
    });

    loopBlinkTimer_ = new QTimer(this);
    loopBlinkTimer_->setInterval(800);
    connect(loopBlinkTimer_, &QTimer::timeout, this, [this]() {
        if (!loopActive_) return;
        static bool bright = false;
        bright = !bright;
        loopBtn_->setStyleSheet(bright
            ? "QPushButton { background: rgba(118,75,162,0.9); border: 1px solid rgba(118,75,162,1); border-radius: 21px; "
              "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }"
            : "QPushButton { background: rgba(118,75,162,0.3); border: 1px solid rgba(118,75,162,1); border-radius: 21px; "
              "margin: 0; padding: 21px; min-height: 0px; min-width: 0px; }");
    });

    topRow->addWidget(onAirLabel_);
    topRow->addSpacing(6);
    topRow->addWidget(presetCombo_, 1);
    topRow->addWidget(manageBtn_);
    topRow->addWidget(stopBtn_);
    topRow->addWidget(loopBtn_);

    // Track name
    nowPlayingLabel_ = new QLabel(tr("Sin reproducción"));
    nowPlayingLabel_->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #e0e0f0; margin:8px 0 6px 0;");
    nowPlayingLabel_->setWordWrap(true);
    nowPlayingLabel_->setContentsMargins(0, 7, 0, 7);

    // Time row
    auto* timeRow = new QHBoxLayout();
    timeRow->setContentsMargins(0, 4, 0, 4);

    timeLabel_ = new QLabel("00:00");
    timeLabel_->setStyleSheet(
        "font-size: 22px; font-weight: 300; "
        "font-family: 'Ubuntu Mono', 'Courier New', monospace;");

    progressBar_ = new QProgressBar();
    progressBar_->setRange(0, 1000);
    progressBar_->setValue(0);
    progressBar_->setFixedHeight(10);
    progressBar_->setFixedHeight(12);
    progressBar_->setTextVisible(false);
    progressBar_->setStyleSheet(
        "QProgressBar { background: rgba(255,255,255,0.05); border: none; border-radius: 5px; }"
        "QProgressBar::chunk { background: #667eea; border-radius: 5px; }");

    remainLabel_ = new QLabel("-00:00");
    remainLabel_->setStyleSheet(
        "font-size: 22px; font-weight: 300; "
        "font-family: 'Ubuntu Mono', 'Courier New', monospace;");
    remainLabel_->setAlignment(Qt::AlignRight);

    timeRow->addWidget(timeLabel_);
    timeRow->addWidget(progressBar_, 1);
    timeRow->addWidget(remainLabel_);

    airLayout->addLayout(topRow);
    airLayout->addWidget(nowPlayingLabel_);
    airLayout->addLayout(timeRow);
    gl->addWidget(airFrame);

    // ══════════════════════════════════════════════════
    // Grilla 3x4 de botones grandes
    // ══════════════════════════════════════════════════
    auto* grid = new QGridLayout();
    grid->setSpacing(4);

    for (int i = 0; i < 12; ++i) {
        auto* btn = new QPushButton(QString("F%1").arg(i + 1));
        btn->setMinimumHeight(48);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        btn->setStyleSheet(BTN_EMPTY_STYLE);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        btn->setAcceptDrops(true);
        btn->installEventFilter(this);

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onButtonClicked(i);
        });
        connect(btn, &QPushButton::customContextMenuRequested,
                this, [this, i, btn](const QPoint& pos) {
            onButtonRightClicked(i, btn->mapToGlobal(pos));
        });

        instantButtons_.append(btn);
        grid->addWidget(btn, i / 3, i % 3);
    }

    gl->addLayout(grid, 1);
}

// ══════════════════════════════════════════════════════════
// Botones
// ══════════════════════════════════════════════════════════

void LiveAssistColumn::refreshButtons()
{
    if (!repo_ || currentPresetId_.isEmpty()) return;
    auto preset = repo_->getPreset(currentPresetId_);
    if (!preset) return;

    for (int i = 0; i < 12 && i < preset->buttonSlots.size(); ++i) {
        const auto& slot = preset->buttonSlots[i];
        if (slot.filePath.isEmpty()) {
            QString label = QString("F%1\n—").arg(i + 1);
            instantButtons_[i]->setText(label);
            instantButtons_[i]->setToolTip(
                QString("F%1 — vacío\nClic derecho para asignar audio").arg(i + 1));
            if (i != activeSlot_) {
                instantButtons_[i]->setStyleSheet(BTN_EMPTY_STYLE);
            }
        } else {
            QString name = slot.displayName.isEmpty()
                ? QFileInfo(slot.filePath).completeBaseName()
                : slot.displayName;
            // Truncar para el botón
            QString shortName = name.length() > 16 ? name.left(15) + "…" : name;
            instantButtons_[i]->setText(QString("F%1\n%2").arg(i + 1).arg(shortName));
            instantButtons_[i]->setToolTip(
                QString("F%1 — %2\n%3\nClic para reproducir").arg(i + 1).arg(name, slot.filePath));
            if (i != activeSlot_) {
                instantButtons_[i]->setStyleSheet(BTN_ASSIGNED_STYLE);
            }
        }
    }
}

void LiveAssistColumn::setActiveButton(int slot)
{
    // Desactivar el anterior
    if (activeSlot_ >= 0 && activeSlot_ < instantButtons_.size()) {
        auto slotData = repo_ ? repo_->getSlot(currentPresetId_, activeSlot_) : std::nullopt;
        instantButtons_[activeSlot_]->setStyleSheet(
            (slotData && !slotData->filePath.isEmpty()) ? BTN_ASSIGNED_STYLE : BTN_EMPTY_STYLE);
    }
    activeSlot_ = slot;
    // Activar el nuevo
    if (slot >= 0 && slot < instantButtons_.size()) {
        instantButtons_[slot]->setStyleSheet(BTN_ACTIVE_STYLE);
    }
}

void LiveAssistColumn::onButtonClicked(int slot)
{
    playSlot(slot);
}

void LiveAssistColumn::playSlot(int slot)
{
    if (!repo_ || !pipeline_ || currentPresetId_.isEmpty()) return;

    auto slotData = repo_->getSlot(currentPresetId_, slot);
    if (!slotData || slotData->filePath.isEmpty()) {
        LOG_INFO("[LiveAssist] F{} vacío", slot + 1);
        return;
    }

    if (!QFileInfo::exists(slotData->filePath)) {
        LOG_WARN("[LiveAssist] Archivo no encontrado: {}", slotData->filePath.toStdString());
        return;
    }

    pipeline_->play(slotData->filePath);
    setActiveButton(slot);

    QString name = slotData->displayName.isEmpty()
        ? QFileInfo(slotData->filePath).completeBaseName()
        : slotData->displayName;
    nowPlayingLabel_->setText(name);
    nowPlayingLabel_->setStyleSheet(
        "font-size: 13px; font-weight: 600; color: #e0e0f0;");

    if (onAirLabel_) {
        onAirLabel_->setText(tr("● AL AIRE"));
        onAirLabel_->setStyleSheet(
            "font-size: 11px; font-weight: 800; color: #22c55e; letter-spacing: 1px;");
    }

    progressTimer_->start();
    LOG_INFO("[LiveAssist] F{} → {}", slot + 1, name.toStdString());
    emit instantPlayClicked(slot);
}

void LiveAssistColumn::stopPlayback()
{
    if (pipeline_) pipeline_->stop();
    setActiveButton(-1);
    progressBar_->setValue(0);
    timeLabel_->setText("00:00");
    if (remainLabel_) remainLabel_->setText("-00:00");
    nowPlayingLabel_->setText(tr("Sin reproducción"));
    nowPlayingLabel_->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #e0e0f0;");
    if (onAirLabel_) {
        onAirLabel_->setText(tr("● PARADO"));
        onAirLabel_->setStyleSheet(
            "font-size: 11px; font-weight: 800; color: #ef4444; letter-spacing: 1px;");
    }
    progressTimer_->stop();
}

void LiveAssistColumn::updateProgress()
{
    if (!pipeline_ || !pipeline_->isPlaying()) return;

    int64_t pos = pipeline_->positionMs();
    int64_t dur = pipeline_->durationMs();

    if (dur > 0) {
        progressBar_->setValue(static_cast<int>((pos * 1000) / dur));
    }

    auto fmt = [](int64_t ms) -> QString {
        int secs = static_cast<int>(ms / 1000);
        return QString("%1:%2")
            .arg(secs / 60, 2, 10, QChar('0'))
            .arg(secs % 60, 2, 10, QChar('0'));
    };

    timeLabel_->setText(fmt(pos));

    if (remainLabel_ && dur > 0) {
        int64_t remaining = dur - pos;
        if (remaining < 0) remaining = 0;
        remainLabel_->setText("-" + fmt(remaining));
    }
}

void LiveAssistColumn::onInstantTrackFinished()
{
    // Si el pipeline sigue reproduciendo (ej: locución de hora encadenó otro archivo), no interferir
    if (pipeline_ && pipeline_->isPlaying()) return;

    if (loopCheck_->isChecked() && activeSlot_ >= 0) {
        // Reproducir de nuevo en bucle
        playSlot(activeSlot_);
    } else {
        stopPlayback();
    }
}

void LiveAssistColumn::onButtonRightClicked(int slot, const QPoint& globalPos)
{
    QMenu menu(this);

    auto* assignAction = menu.addAction(tintNavIcon(":/icons/music.svg", Qt::white),
        QString("Asignar audio a F%1").arg(slot + 1));

    bool hasAudio = false;
    QString slotFilePath;
    if (repo_ && !currentPresetId_.isEmpty()) {
        auto slotData = repo_->getSlot(currentPresetId_, slot);
        hasAudio = slotData && !slotData->filePath.isEmpty();
        if (hasAudio) slotFilePath = slotData->filePath;
    }

    QAction* cueAction = nullptr;
    QAction* clearAction = nullptr;
    if (hasAudio) {
        cueAction = menu.addAction(tintNavIcon(":/icons/volume-2.svg", Qt::white),
            QString("Preescuchar F%1 (CUE)").arg(slot + 1));
        menu.addSeparator();
        clearAction = menu.addAction(tintNavIcon(":/icons/x.svg", Qt::white),
            QString("Limpiar F%1").arg(slot + 1));
    }

    auto* result = menu.exec(globalPos);
    if (result == assignAction) {
        QString file = QFileDialog::getOpenFileName(
            this, QString("Asignar audio a F%1").arg(slot + 1), QString(),
            "Audio (*.mp3 *.ogg *.flac *.wav *.aac *.m4a *.opus);;Todos (*)");
        if (!file.isEmpty() && repo_) {
            repo_->setSlot(currentPresetId_, slot, file,
                           QFileInfo(file).completeBaseName());
            refreshButtons();
        }
    } else if (result == cueAction && !slotFilePath.isEmpty()) {
        emit cuePreviewRequested(slotFilePath);
    } else if (result == clearAction && repo_) {
        repo_->clearSlot(currentPresetId_, slot);
        refreshButtons();
    }
}

// ══════════════════════════════════════════════════════════
// Presets
// ══════════════════════════════════════════════════════════

void LiveAssistColumn::onPresetChanged(int index)
{
    if (index < 0) return;
    currentPresetId_ = presetCombo_->currentData().toString();
    refreshButtons();
}

void LiveAssistColumn::onManagePresets()
{
    QMenu menu(this);

    auto* newAction = menu.addAction(tintNavIcon(":/icons/plus.svg", Qt::white), "Nuevo preset");
    auto* renameAction = menu.addAction(tintNavIcon(":/icons/settings.svg", Qt::white), "Renombrar preset actual");
    auto* duplicateAction = menu.addAction(tintNavIcon(":/icons/list.svg", Qt::white), "Duplicar preset actual");
    QAction* deleteAction = nullptr;
    if (!operationMode_) {
        menu.addSeparator();
        deleteAction = menu.addAction(tintNavIcon(":/icons/x.svg", Qt::white), "Eliminar preset actual");
    }

    auto* result = menu.exec(manageBtn_->mapToGlobal(QPoint(0, manageBtn_->height())));

    if (result == newAction) {
        bool ok = false;
        QString name = QInputDialog::getText(this, tr("Nuevo preset"),
            tr("Nombre:"), QLineEdit::Normal, "", &ok);
        if (ok && !name.trimmed().isEmpty() && repo_) {
            QString id = repo_->createPreset(name.trimmed());
            if (!id.isEmpty()) {
                presetCombo_->addItem(name.trimmed(), id);
                presetCombo_->setCurrentIndex(presetCombo_->count() - 1);
            }
        }
    } else if (result == renameAction) {
        bool ok = false;
        QString name = QInputDialog::getText(this, tr("Renombrar preset"),
            tr("Nuevo nombre:"), QLineEdit::Normal, presetCombo_->currentText(), &ok);
        if (ok && !name.trimmed().isEmpty() && repo_) {
            repo_->renamePreset(currentPresetId_, name.trimmed());
            presetCombo_->setItemText(presetCombo_->currentIndex(), name.trimmed());
        }
    } else if (result == duplicateAction) {
        bool ok = false;
        QString name = QInputDialog::getText(this, tr("Duplicar preset"),
            tr("Nombre de la copia:"), QLineEdit::Normal,
            presetCombo_->currentText() + " (copia)", &ok);
        if (ok && !name.trimmed().isEmpty() && repo_) {
            QString id = repo_->duplicatePreset(currentPresetId_, name.trimmed());
            if (!id.isEmpty()) {
                presetCombo_->addItem(name.trimmed(), id);
                presetCombo_->setCurrentIndex(presetCombo_->count() - 1);
            }
        }
    } else if (deleteAction && result == deleteAction) {
        if (presetCombo_->count() <= 1) {
            QMessageBox::information(this, tr("No se puede"),
                tr("Debe existir al menos un preset."));
            return;
        }
        auto confirm = QMessageBox::question(this, tr("Eliminar preset"),
            QString("¿Eliminar \"%1\"?").arg(presetCombo_->currentText()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirm == QMessageBox::Yes && repo_) {
            repo_->deletePreset(currentPresetId_);
            presetCombo_->removeItem(presetCombo_->currentIndex());
        }
    }
}

// ══════════════════════════════════════════════════════════
// Explorador
// ══════════════════════════════════════════════════════════

void LiveAssistColumn::setupFileExplorer(QWidget* container)
{
    auto* outerLayout = new QVBoxLayout(container);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // (Sin sub-título "EXPLORADOR DE ARCHIVOS": se quitó para ganar espacio.)

    auto* explorerContent = new QWidget();
    auto* gl = new QVBoxLayout(explorerContent);
    gl->setSpacing(4);
    gl->setContentsMargins(6, 8, 6, 4);

    // ── Barra de navegación rápida ───────────────────
    auto* navRow = new QHBoxLayout();
    navRow->setSpacing(4);

    auto setActiveNav = [this](QPushButton* active) {
        for (auto* btn : {navRadioBtn_, navBotoneraBtn_, navMusicBtn_, navHomeBtn_, navStreamsBtn_}) {
            if (btn) btn->setStyleSheet(NAV_INACTIVE_STYLE);
        }
        if (active) active->setStyleSheet(NAV_ACTIVE_STYLE);
    };

    // Radio
    navRadioBtn_ = new QPushButton(tintNavIcon(":/icons/radio-tower.svg", Qt::white), tr(" Radio"));
    navRadioBtn_->setFixedHeight(28);
    navRadioBtn_->setCursor(Qt::PointingHandCursor);
    navRadioBtn_->setStyleSheet(NAV_INACTIVE_STYLE);
    navRadioBtn_->setEnabled(false);
    connect(navRadioBtn_, &QPushButton::clicked, this, [this, setActiveNav]() {
        showFileView();
        if (!radioFolder_.isEmpty() && QDir(radioFolder_).exists()) {
            fileTree_->setRootIndex(fileModel_->index(radioFolder_));
        }
        setActiveNav(navRadioBtn_);
    });

    // Botonera (instant play / F1-F12) — pestaña entre Radio y Alternativa
    navBotoneraBtn_ = new QPushButton(tintNavIcon(":/icons/zap.svg", Qt::white), tr(" Botonera"));
    navBotoneraBtn_->setFixedHeight(28);
    navBotoneraBtn_->setCursor(Qt::PointingHandCursor);
    navBotoneraBtn_->setStyleSheet(NAV_INACTIVE_STYLE);
    navBotoneraBtn_->setToolTip(tr("Botonera de reproducción instantánea (F1–F12)"));
    navBotoneraBtn_->setAcceptDrops(true);     // permite arrastrar canciones sobre la pestaña
    navBotoneraBtn_->installEventFilter(this);  // para cambiar de vista al arrastrar encima
    connect(navBotoneraBtn_, &QPushButton::clicked, this, [this, setActiveNav]() {
        showInstantView();
        setActiveNav(navBotoneraBtn_);
    });

    // Alternativa
    navMusicBtn_ = new QPushButton(tintNavIcon(":/icons/music.svg", Qt::white), tr(" Alternativa"));
    navMusicBtn_->setFixedHeight(28);
    navMusicBtn_->setCursor(Qt::PointingHandCursor);
    navMusicBtn_->setStyleSheet(NAV_INACTIVE_STYLE);
    navMusicBtn_->setEnabled(false);
    connect(navMusicBtn_, &QPushButton::clicked, this, [this, setActiveNav]() {
        showFileView();
        if (!musicFolder_.isEmpty() && QDir(musicFolder_).exists()) {
            fileTree_->setRootIndex(fileModel_->index(musicFolder_));
        }
        setActiveNav(navMusicBtn_);
    });

    // Inicio
    navHomeBtn_ = new QPushButton(tintNavIcon(":/icons/folder.svg", Qt::white), tr(" Inicio"));
    navHomeBtn_->setFixedHeight(28);
    navHomeBtn_->setCursor(Qt::PointingHandCursor);
    navHomeBtn_->setStyleSheet(NAV_ACTIVE_STYLE);  // Default active
    navHomeBtn_->setToolTip(QDir::homePath());
    connect(navHomeBtn_, &QPushButton::clicked, this, [this, setActiveNav]() {
        showFileView();
        fileTree_->setRootIndex(fileModel_->index(QDir::homePath()));
        setActiveNav(navHomeBtn_);
    });

    auto* navUpBtn = new QPushButton(tintNavIcon(":/icons/chevron-left.svg", Qt::white), "");
    navUpBtn->setFixedSize(28, 28);
    navUpBtn->setCursor(Qt::PointingHandCursor);
    navUpBtn->setStyleSheet(NAV_INACTIVE_STYLE);
    navUpBtn->setToolTip(tr("Carpeta superior"));
    connect(navUpBtn, &QPushButton::clicked, this, [this]() {
        QModelIndex current = fileTree_->rootIndex();
        QModelIndex parent = current.parent();
        if (parent.isValid()) {
            fileTree_->setRootIndex(parent);
        }
    });

    navRow->addWidget(navRadioBtn_);
    navRow->addWidget(navBotoneraBtn_);
    navRow->addWidget(navMusicBtn_);
    navRow->addWidget(navHomeBtn_);

    // Streams
    navStreamsBtn_ = new QPushButton(tintNavIcon(":/icons/radio.svg", Qt::white), tr(" Stream"));
    navStreamsBtn_->setFixedHeight(28);
    navStreamsBtn_->setCursor(Qt::PointingHandCursor);
    navStreamsBtn_->setStyleSheet(NAV_INACTIVE_STYLE);
    navStreamsBtn_->setToolTip(tr("Fuentes de streaming guardadas"));
    connect(navStreamsBtn_, &QPushButton::clicked, this, [this, setActiveNav]() {
        if (streamList_ && streamList_->isVisible()) {
            showFileView();
            setActiveNav(navHomeBtn_);
        } else {
            showStreamView();
            setActiveNav(navStreamsBtn_);
        }
    });
    navRow->addWidget(navStreamsBtn_);

    navRow->addStretch();
    navRow->addWidget(navUpBtn);
    gl->addLayout(navRow);

    gl->addSpacing(6);  // Separación entre tabs y buscador

    // ── Barra de búsqueda ────────────────────────────
    auto* searchRow = new QHBoxLayout();
    searchRow->setSpacing(3);

    auto* searchIcon = new QLabel();
    searchIcon->setObjectName("searchIcon");
    searchIcon->setPixmap(tintNavIcon(":/icons/search.svg", QColor("#667eea")).pixmap(16, 16));

    searchEdit_ = new QLineEdit();
    searchEdit_->setPlaceholderText(tr("Buscar archivo de audio..."));
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setFixedHeight(30);
    searchEdit_->setStyleSheet(
        "QLineEdit { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.1); "
        "border-radius: 6px; padding: 4px 10px; font-size: 12px; }"
        "QLineEdit:focus { border-color: rgba(102,126,234,0.5); }");

    searchRow->addWidget(searchIcon);
    searchRow->addWidget(searchEdit_, 1);
    gl->addLayout(searchRow);
    gl->addSpacing(6);  // Separación entre buscador y carpetas

    // Debounce: esperar 300ms después de dejar de escribir
    searchDebounce_ = new QTimer(this);
    searchDebounce_->setSingleShot(true);
    searchDebounce_->setInterval(300);
    connect(searchEdit_, &QLineEdit::textChanged, this, [this](const QString&) {
        searchDebounce_->start();
    });
    connect(searchDebounce_, &QTimer::timeout, this, [this]() {
        QString query = searchEdit_->text().trimmed();
        if (query.length() >= 2) {
            performSearch(query);
        } else {
            // Volver al árbol
            searchResults_->hide();
            fileTree_->show();
        }
    });

    // ── Lista de resultados de búsqueda (oculta por defecto) ─
    searchResults_ = new DraggableListWidget();
    searchResults_->setVisible(false);
    searchResults_->setDragEnabled(true);
    searchResults_->setStyleSheet(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 4px 8px; border-radius: 4px; color: #c0c8e0; }"
        "QListWidget::item:selected { background: rgba(102,126,234,0.2); }"
        "QListWidget::item:hover { background: rgba(255,255,255,0.05); }");

    // Doble clic en resultado
    connect(searchResults_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) emit fileDoubleClicked(path);
    });

    // Clic derecho en resultado
    searchResults_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(searchResults_, &QListWidget::customContextMenuRequested,
            this, &LiveAssistColumn::onSearchResultContextMenu);

    gl->addWidget(searchResults_, 1);

    // ── Árbol de archivos ────────────────────────────
    fileModel_ = new QFileSystemModel(this);
    fileModel_->setRootPath(QDir::homePath());
    QStringList filters;
    for (const auto& ext : FileScanner::supportedExtensions()) {
        filters << ("*." + ext);
    }
    fileModel_->setNameFilters(filters);
    fileModel_->setNameFilterDisables(false);

    fileTree_ = new QTreeView();
    fileTree_->setModel(fileModel_);
    fileTree_->setRootIndex(fileModel_->index(QDir::homePath()));
    fileTree_->setColumnHidden(1, true);
    fileTree_->setColumnHidden(2, true);
    fileTree_->setColumnHidden(3, true);
    fileTree_->header()->setStretchLastSection(true);
    fileTree_->setHeaderHidden(true);
    fileTree_->setAnimated(true);
    fileTree_->setDragEnabled(true);

    // Doble clic
    connect(fileTree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        QString path = fileModel_->filePath(idx);
        if (FileScanner::isAudioFile(path)) {
            emit fileDoubleClicked(path);
        }
    });

    // Selección → mostrar info
    connect(fileTree_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
        if (!current.isValid()) {
            fileInfoLabel_->setText("");
            return;
        }
        QString path = fileModel_->filePath(current);
        if (FileScanner::isAudioFile(path)) {
            QFileInfo fi(path);
            QString sizeStr = QLocale().formattedDataSize(fi.size());
            QString ext = fi.suffix().toUpper();
            QString durStr;
            if (metadataReader_) {
                int64_t durMs = metadataReader_->getDurationMs(path);
                if (durMs > 0) {
                    int secs = static_cast<int>(durMs / 1000);
                    durStr = QString(" · %1:%2")
                        .arg(secs / 60, 2, 10, QChar('0'))
                        .arg(secs % 60, 2, 10, QChar('0'));
                }
            }
            fileInfoLabel_->setText(
                QString("%1 · %2 · %3%4").arg(fi.completeBaseName(), ext, sizeStr, durStr));
        } else {
            fileInfoLabel_->setText(fileModel_->fileName(current));
        }
    });

    // Clic derecho
    fileTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fileTree_, &QTreeView::customContextMenuRequested,
            this, &LiveAssistColumn::onExplorerContextMenu);

    gl->addWidget(fileTree_, 1);

    // ── Lista de streams (oculta por defecto) ────────
    streamList_ = new QListWidget();
    streamList_->setVisible(false);
    streamList_->setContextMenuPolicy(Qt::CustomContextMenu);
    streamList_->setStyleSheet(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 6px 8px; border-radius: 4px; }"
        "QListWidget::item:selected { background: rgba(102,126,234,0.2); }"
        "QListWidget::item:hover { background: rgba(255,255,255,0.05); }");
    connect(streamList_, &QListWidget::customContextMenuRequested,
            this, &LiveAssistColumn::onStreamContextMenu);
    connect(streamList_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
        QString url = item->data(Qt::UserRole).toString();
        QString name = item->data(Qt::UserRole + 2).toString();
        if (!url.isEmpty() && !name.isEmpty()) {
            addStreamToQueue(url, name);
        } else {
            openAddStreamDialog();
        }
    });
    gl->addWidget(streamList_, 1);

    // ── Botonera (instant play) como vista alternativa del explorador ──
    // Antes era una sección fija arriba; ahora se muestra al activar la
    // pestaña "Botonera" (oculta por defecto).
    instantContainer_ = new QWidget();
    setupInstantPlay(instantContainer_);
    instantContainer_->setVisible(false);
    gl->addWidget(instantContainer_, 1);

    // Botón agregar stream (oculto por defecto, aparece con la vista streams)
    auto* addStreamBtn = new QPushButton(tintNavIcon(":/icons/plus.svg", Qt::white), tr("Agregar stream"));
    addStreamBtn->setObjectName("addStreamBtn");
    addStreamBtn->setFixedHeight(26);
    addStreamBtn->setVisible(false);
    connect(addStreamBtn, &QPushButton::clicked, this, &LiveAssistColumn::openAddStreamDialog);
    gl->addWidget(addStreamBtn);

    // ── Info del archivo seleccionado ─────────────────
    fileInfoLabel_ = new QLabel();
    fileInfoLabel_->setStyleSheet(
        "font-size: 14px; padding: 4px 4px;");
    fileInfoLabel_->setMaximumHeight(20);
    gl->addWidget(fileInfoLabel_);

    outerLayout->addWidget(explorerContent, 1);
}

void LiveAssistColumn::onExplorerContextMenu(const QPoint& pos)
{
    auto idx = fileTree_->indexAt(pos);
    if (!idx.isValid()) return;

    QString path = fileModel_->filePath(idx);
    if (!FileScanner::isAudioFile(path)) return;

    QMenu menu(this);

    auto* cueAction = menu.addAction(tintNavIcon(":/icons/volume-2.svg", Qt::white), tr("Preescuchar (CUE)"));
    auto* queueAction = menu.addAction(tintNavIcon(":/icons/list.svg", Qt::white), tr("Agregar a la cola"));
    menu.addSeparator();

    auto* assignMenu = menu.addMenu(tintNavIcon(":/icons/plus.svg", Qt::white), tr("Asignar a botón"));
    QVector<QAction*> slotActions;
    for (int i = 0; i < 12; ++i) {
        slotActions.append(assignMenu->addAction(QString("F%1").arg(i + 1)));
    }

    // Pisador
    menu.addSeparator();
    auto* editTagsAction = menu.addAction(tintNavIcon(":/icons/disc-2.svg", Qt::white), tr("Editar metatags"));
    menu.addSeparator();
    auto* pisadorMenu = menu.addMenu(tintNavIcon(":/icons/music.svg", Qt::white), tr("Pisador"));
    auto* pisSpecific = pisadorMenu->addAction(tr("Asignar pisador específico..."));
    auto* pisRandom = pisadorMenu->addAction(tr("Asignar aleatorio (de carpeta)"));
    auto* pisTime = pisadorMenu->addAction(tr("Pisar con locución de hora"));
    pisadorMenu->addSeparator();
    auto* pisNone = pisadorMenu->addAction(tr("No pisar nunca esta pista"));
    auto* pisRemove = pisadorMenu->addAction(tr("Quitar asignación individual"));

    auto* result = menu.exec(fileTree_->viewport()->mapToGlobal(pos));
    if (result == cueAction) {
        emit cuePreviewRequested(path);
    } else if (result == queueAction) {
        emit addToQueueRequested(path);
    } else if (result == editTagsAction) {
        auto* editor = new MetatagEditor(path, this);
        if (editor->exec() == QDialog::Accepted && editor->wasModified()) {
            TagLib::FileRef f(path.toUtf8().constData());
            if (!f.isNull() && f.tag()) {
                QString title = QString::fromStdString(f.tag()->title().to8Bit(true));
                QString artist = QString::fromStdString(f.tag()->artist().to8Bit(true));
                QFileInfo fi(path);
                QString info = fi.completeBaseName();
                if (!title.isEmpty()) info = title;
                if (!artist.isEmpty()) info += QString::fromUtf8(" — ") + artist;
                info += QString(" · %1").arg(fi.suffix().toUpper());
                if (f.audioProperties()) {
                    int secs = f.audioProperties()->lengthInSeconds();
                    info += QString(" (%1:%2)")
                        .arg(secs / 60, 2, 10, QChar('0'))
                        .arg(secs % 60, 2, 10, QChar('0'));
                }
                fileInfoLabel_->setText(info);
            }
        }
        delete editor;
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
    } else {
        for (int i = 0; i < slotActions.size(); ++i) {
            if (result == slotActions[i] && repo_) {
                repo_->setSlot(currentPresetId_, i, path,
                               QFileInfo(path).completeBaseName());
                refreshButtons();
                break;
            }
        }
    }
}

// ══════════════════════════════════════════════════════════
// Búsqueda de archivos
// ══════════════════════════════════════════════════════════

void LiveAssistColumn::performSearch(const QString& query)
{
    searchResults_->clear();
    fileTree_->hide();
    searchResults_->show();

    // Construir lista de raíces de búsqueda:
    // 1. Home del usuario (siempre)
    // 2. Carpeta de música configurada (si es diferente del home)
    // 3. Volúmenes montados (/media, /mnt, /run/media) — discos externos, USB, NAS
    QStringList searchRoots;
    searchRoots << QDir::homePath();

    // Si la carpeta de música está fuera del home, agregarla
    if (!musicFolder_.isEmpty() && !musicFolder_.startsWith(QDir::homePath())) {
        searchRoots << musicFolder_;
    }

    // Detectar volúmenes montados (segundo disco, USB, NAS)
    QStringList mountDirs = {"/media", "/mnt", "/run/media"};
    for (const auto& mountBase : mountDirs) {
        QDir dir(mountBase);
        if (!dir.exists()) continue;

        // /media/usuario/DISCO, /mnt/datos, /run/media/usuario/USB
        for (const auto& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (entry.isReadable()) {
                // Si es /media o /run/media, buscar un nivel más (usuario/disco)
                if (mountBase == "/media" || mountBase == "/run/media") {
                    QDir subDir(entry.filePath());
                    for (const auto& sub : subDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                        if (sub.isReadable()) searchRoots << sub.filePath();
                    }
                } else {
                    searchRoots << entry.filePath();
                }
            }
        }
    }

    // Eliminar duplicados
    searchRoots.removeDuplicates();

    QStringList nameFilters;
    for (const auto& ext : FileScanner::supportedExtensions()) {
        nameFilters << ("*." + ext);
    }

    QString queryLower = query.toLower();
    int count = 0;
    static const int MAX_RESULTS = 100;

    for (const auto& root : searchRoots) {
        if (count >= MAX_RESULTS) break;
        if (!QDir(root).exists()) continue;

        QDirIterator it(root, nameFilters,
                        QDir::Files, QDirIterator::Subdirectories);

        while (it.hasNext() && count < MAX_RESULTS) {
            it.next();
            QString fileName = it.fileName();

            if (fileName.toLower().contains(queryLower)) {
                QString filePath = it.filePath();
                QFileInfo fi(filePath);

                // Duración
                QString durStr;
                if (metadataReader_) {
                    int64_t durMs = metadataReader_->getDurationMs(filePath);
                    if (durMs > 0) {
                        int secs = static_cast<int>(durMs / 1000);
                        durStr = QString("  [%1:%2]")
                            .arg(secs / 60, 2, 10, QChar('0'))
                            .arg(secs % 60, 2, 10, QChar('0'));
                    }
                }

                QString parentDir = fi.dir().dirName();
                QString label = QString("%1%2  (%3)")
                    .arg(fi.completeBaseName(), durStr, parentDir);

                auto* item = new QListWidgetItem(
                    tintNavIcon(":/icons/music.svg", Qt::white), label);
                item->setData(Qt::UserRole, filePath);
                item->setToolTip(filePath);
                searchResults_->addItem(item);
                ++count;
            }
        }
    }

    if (count == 0) {
        auto* empty = new QListWidgetItem(tr("Sin resultados para \"%1\"").arg(query));
        empty->setForeground(palette().color(QPalette::PlaceholderText));
        empty->setFlags(empty->flags() & ~Qt::ItemIsSelectable);
        searchResults_->addItem(empty);
    }

    // Mostrar dónde buscó
    QString rootsInfo = searchRoots.join(", ");
    fileInfoLabel_->setText(
        count >= MAX_RESULTS
            ? QString("%1+ resultados (máx. %1)").arg(MAX_RESULTS)
            : QString("%1 resultado(s)").arg(count));
    fileInfoLabel_->setToolTip(tr("Buscando en: ") + rootsInfo);
}

void LiveAssistColumn::onSearchResultContextMenu(const QPoint& pos)
{
    auto* item = searchResults_->itemAt(pos);
    if (!item) return;

    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    QMenu menu(this);

    auto* cueAction = menu.addAction(tintNavIcon(":/icons/volume-2.svg", Qt::white), tr("Preescuchar (CUE)"));
    auto* queueAction = menu.addAction(tintNavIcon(":/icons/list.svg", Qt::white), tr("Agregar a la cola"));
    menu.addSeparator();

    auto* assignMenu = menu.addMenu(tintNavIcon(":/icons/plus.svg", Qt::white), tr("Asignar a botón"));
    QVector<QAction*> slotActions;
    for (int i = 0; i < 12; ++i) {
        slotActions.append(assignMenu->addAction(QString("F%1").arg(i + 1)));
    }

    menu.addSeparator();
    auto* editTagsAction = menu.addAction(tintNavIcon(":/icons/disc-2.svg", Qt::white), tr("Editar metatags"));
    menu.addSeparator();
    auto* pisadorMenu = menu.addMenu(tintNavIcon(":/icons/music.svg", Qt::white), tr("Pisador"));
    auto* pisSpecific = pisadorMenu->addAction(tr("Asignar pisador específico..."));
    auto* pisRandom = pisadorMenu->addAction(tr("Asignar aleatorio (de carpeta)"));
    auto* pisTime = pisadorMenu->addAction(tr("Pisar con locución de hora"));
    pisadorMenu->addSeparator();
    auto* pisNone = pisadorMenu->addAction(tr("No pisar nunca esta pista"));
    auto* pisRemove = pisadorMenu->addAction(tr("Quitar asignación individual"));

    auto* result = menu.exec(searchResults_->viewport()->mapToGlobal(pos));
    if (result == cueAction) {
        emit cuePreviewRequested(path);
    } else if (result == queueAction) {
        emit addToQueueRequested(path);
    } else if (result == editTagsAction) {
        auto* editor = new MetatagEditor(path, this);
        if (editor->exec() == QDialog::Accepted && editor->wasModified()) {
            TagLib::FileRef f(path.toUtf8().constData());
            if (!f.isNull() && f.tag()) {
                QString title = QString::fromStdString(f.tag()->title().to8Bit(true));
                QString artist = QString::fromStdString(f.tag()->artist().to8Bit(true));
                QFileInfo fi(path);
                QString info = fi.completeBaseName();
                if (!title.isEmpty()) info = title;
                if (!artist.isEmpty()) info += QString::fromUtf8(" — ") + artist;
                info += QString(" · %1").arg(fi.suffix().toUpper());
                if (f.audioProperties()) {
                    int secs = f.audioProperties()->lengthInSeconds();
                    info += QString(" (%1:%2)")
                        .arg(secs / 60, 2, 10, QChar('0'))
                        .arg(secs % 60, 2, 10, QChar('0'));
                }
                fileInfoLabel_->setText(info);
            }
        }
        delete editor;
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
    } else {
        for (int i = 0; i < slotActions.size(); ++i) {
            if (result == slotActions[i] && repo_) {
                repo_->setSlot(currentPresetId_, i, path,
                               QFileInfo(path).completeBaseName());
                refreshButtons();
                break;
            }
        }
    }
}

// ══════════════════════════════════════════════════════════
// Streams
// ══════════════════════════════════════════════════════════

void LiveAssistColumn::setStreamPresetRepo(StreamPresetRepo* repo)
{
    streamRepo_ = repo;
    refreshStreamList();
}

void LiveAssistColumn::refreshStreamList()
{
    if (!streamList_ || !streamRepo_) return;

    streamList_->clear();
    auto presets = streamRepo_->getAll();
    for (const auto& p : presets) {
        auto* item = new QListWidgetItem(
            tintNavIcon(":/icons/radio.svg", Qt::white),
            QString("%1\n%2").arg(p.name, p.url));
        item->setData(Qt::UserRole, p.url);
        item->setData(Qt::UserRole + 1, p.id);
        item->setData(Qt::UserRole + 2, p.name);
        streamList_->addItem(item);
    }

    if (presets.isEmpty()) {
        auto* hint = new QListWidgetItem(
            tr("No hay streams guardados.\n"
               "Use el botón + de abajo o clic derecho para agregar."));
        hint->setFlags(Qt::NoItemFlags);
        streamList_->addItem(hint);
    }
}

void LiveAssistColumn::showStreamView()
{
    if (instantContainer_) instantContainer_->hide();
    fileTree_->hide();
    searchResults_->hide();
    searchEdit_->hide();
    if (auto* si = findChild<QLabel*>("searchIcon")) si->hide();
    streamList_->show();
    if (auto* btn = findChild<QPushButton*>("addStreamBtn"))
        btn->show();
    refreshStreamList();
}

void LiveAssistColumn::showFileView()
{
    if (instantContainer_) instantContainer_->hide();
    streamList_->hide();
    if (auto* btn = findChild<QPushButton*>("addStreamBtn"))
        btn->hide();
    searchEdit_->show();
    if (auto* si = findChild<QLabel*>("searchIcon")) si->show();
    fileTree_->show();
}

void LiveAssistColumn::showInstantView()
{
    // Mostrar la botonera; ocultar todo lo demás del explorador.
    fileTree_->hide();
    searchResults_->hide();
    searchEdit_->hide();
    if (auto* si = findChild<QLabel*>("searchIcon")) si->hide();
    streamList_->hide();
    if (auto* btn = findChild<QPushButton*>("addStreamBtn")) btn->hide();
    if (instantContainer_) instantContainer_->show();
}

void LiveAssistColumn::onStreamContextMenu(const QPoint& pos)
{
    QMenu menu(this);

    auto* addAction = menu.addAction(tintNavIcon(":/icons/plus.svg", Qt::white), tr("Agregar stream nuevo..."));

    QAction* queueAction = nullptr;
    QAction* cueAction = nullptr;
    QAction* editAction = nullptr;
    QAction* removeAction = nullptr;
    QString url, name;
    int id = -1;

    auto* item = streamList_->itemAt(pos);
    if (item) {
        url = item->data(Qt::UserRole).toString();
        name = item->data(Qt::UserRole + 2).toString();
        id = item->data(Qt::UserRole + 1).toInt();
    }

    if (!url.isEmpty() && id > 0) {
        menu.addSeparator();
        queueAction = menu.addAction(tintNavIcon(":/icons/list.svg", Qt::white), tr("Agregar a la cola..."));
        cueAction = menu.addAction(tintNavIcon(":/icons/volume-2.svg", Qt::white), tr("Preescuchar (CUE)"));
    }

    // Opción para detener CUE si está sonando
    QAction* cueStopAction = nullptr;
    cueStopAction = menu.addAction(tintNavIcon(":/icons/square.svg", Qt::white), tr("Detener preescucha"));

    if (!url.isEmpty() && id > 0) {
        menu.addSeparator();
        editAction = menu.addAction(tintNavIcon(":/icons/settings.svg", Qt::white), tr("Editar..."));
        removeAction = menu.addAction(tintNavIcon(":/icons/x.svg", Qt::white), tr("Eliminar"));
    }

    auto* result = menu.exec(streamList_->viewport()->mapToGlobal(pos));
    if (!result) return;

    if (result == addAction) {
        openAddStreamDialog();
    } else if (result == queueAction) {
        addStreamToQueue(url, name);
    } else if (result == cueAction) {
        emit cuePreviewRequested(url);
    } else if (result == cueStopAction) {
        emit cuePreviewRequested("");
    } else if (result == editAction) {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Editar Stream"));
        dlg.setMinimumWidth(400);
        auto* layout = new QFormLayout(&dlg);
        auto* nameEdit = new QLineEdit(name);
        auto* urlEdit = new QLineEdit(url);
        layout->addRow(tr("Nombre:"), nameEdit);
        layout->addRow(tr("URL:"), urlEdit);
        auto* btns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addRow(btns);

        if (dlg.exec() == QDialog::Accepted && streamRepo_) {
            streamRepo_->update(id, nameEdit->text().trimmed(),
                                urlEdit->text().trimmed());
            refreshStreamList();
        }
    } else if (result == removeAction) {
        auto r = QMessageBox::question(this, tr("Eliminar stream"),
            tr("Eliminar \"%1\"?").arg(name));
        if (r == QMessageBox::Yes && streamRepo_) {
            streamRepo_->remove(id);
            refreshStreamList();
        }
    }
}

void LiveAssistColumn::openAddStreamDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Agregar Streaming"));
    dlg.setMinimumWidth(420);

    auto* layout = new QVBoxLayout(&dlg);

    // Sección URL
    auto* urlGroup = new QGroupBox(tr("Dirección del streaming"));
    auto* urlLayout = new QVBoxLayout(urlGroup);

    auto* urlEdit = new QLineEdit();
    urlEdit->setPlaceholderText("http://stream.ejemplo.com:8000/radio  o  https://...");
    urlLayout->addWidget(urlEdit);

    auto* cueRow = new QHBoxLayout();
    auto* cueBtn = new QPushButton(tintNavIcon(":/icons/volume-2.svg", Qt::white), tr("Preescuchar (CUE)"));
    cueBtn->setFixedHeight(26);
    cueBtn->setCheckable(true);
    connect(cueBtn, &QPushButton::toggled, this, [this, cueBtn, urlEdit](bool on) {
        if (on) {
            QString url = urlEdit->text().trimmed();
            if (url.isEmpty()) { cueBtn->setChecked(false); return; }
            emit cuePreviewRequested(url);
            cueBtn->setText(tr("Detener preescucha"));
            cueBtn->setIcon(tintNavIcon(":/icons/square.svg", Qt::white));
        } else {
            emit cuePreviewRequested("");  // Signal vacía = detener
            cueBtn->setText(tr("Preescuchar (CUE)"));
            cueBtn->setIcon(tintNavIcon(":/icons/volume-2.svg", Qt::white));
        }
    });
    auto* cueHint = new QLabel(tr("Pruebe la conexión antes de agregar"));
    cueHint->setStyleSheet("font-size: 11px; color: #888;");
    cueRow->addWidget(cueBtn);
    cueRow->addWidget(cueHint);
    cueRow->addStretch();
    urlLayout->addLayout(cueRow);
    layout->addWidget(urlGroup);

    // Sección configuración
    auto* cfgGroup = new QGroupBox(tr("Configuración"));
    auto* cfgForm = new QFormLayout(cfgGroup);

    auto* nameEdit = new QLineEdit(tr("Streaming"));
    cfgForm->addRow(tr("Nombre:"), nameEdit);

    layout->addWidget(cfgGroup);

    // Botones
    auto* btns = new QDialogButtonBox();
    auto* cancelBtn = btns->addButton(QDialogButtonBox::Cancel);
    cancelBtn->setText(tr("Cancelar"));
    auto* addBtn = btns->addButton(tr("+ Agregar"), QDialogButtonBox::AcceptRole);
    addBtn->setProperty("class", "primaryButton");
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btns);

    if (dlg.exec() == QDialog::Accepted && streamRepo_) {
        emit cuePreviewRequested("");  // Detener CUE al cerrar
        QString url = urlEdit->text().trimmed();
        QString name = nameEdit->text().trimmed();
        if (url.isEmpty()) return;
        if (name.isEmpty()) name = "Streaming";

        streamRepo_->add(name, url);
        refreshStreamList();
    } else {
        emit cuePreviewRequested("");  // Detener CUE al cancelar
    }
}

void LiveAssistColumn::addStreamToQueue(const QString& url, const QString& name)
{
    // Diálogo para seleccionar duración
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Agregar Stream a la Cola"));
    dlg.setMinimumWidth(350);

    auto* layout = new QVBoxLayout(&dlg);

    auto* info = new QLabel(tr("Stream: <b>%1</b>").arg(name));
    layout->addWidget(info);

    auto* durGroup = new QGroupBox(tr("Duración"));
    auto* durLayout = new QHBoxLayout(durGroup);

    auto* minSpin = new QSpinBox();
    minSpin->setRange(0, 480);
    minSpin->setValue(30);
    minSpin->setSuffix(tr(" min"));
    minSpin->setFixedWidth(100);

    auto* secSpin = new QSpinBox();
    secSpin->setRange(0, 59);
    secSpin->setValue(0);
    secSpin->setSuffix(tr(" seg"));
    secSpin->setFixedWidth(100);

    durLayout->addWidget(minSpin);
    durLayout->addWidget(secSpin);
    durLayout->addStretch();
    layout->addWidget(durGroup);

    auto* hint = new QLabel(
        tr("Tiempo que el streaming permanecerá al aire. "
           "Al cumplirse, SARA retoma la programación automática.\n"
           "Ponga 0 min y 0 seg para corte manual (botón Siguiente)."));
    hint->setWordWrap(true);
    hint->setStyleSheet("font-size: 11px; color: #888;");
    layout->addWidget(hint);

    auto* btns = new QDialogButtonBox();
    auto* cancelBtn = btns->addButton(QDialogButtonBox::Cancel);
    cancelBtn->setText(tr("Cancelar"));
    auto* addBtn = btns->addButton(tr("Agregar a la cola"), QDialogButtonBox::AcceptRole);
    addBtn->setProperty("class", "primaryButton");
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(btns);

    if (dlg.exec() == QDialog::Accepted) {
        int64_t durationMs = (minSpin->value() * 60 + secSpin->value()) * 1000LL;
        // 0 = sin limite (corte manual)
        emit streamToQueueRequested(url, name, durationMs);
    }
}

bool LiveAssistColumn::eventFilter(QObject* obj, QEvent* event)
{
    // Spring-load: al arrastrar un archivo sobre la pestaña "Botonera",
    // cambiar automáticamente a la vista de botonera para poder soltarlo en un
    // botón (estando en otra pestaña no se ven los botones).
    if (obj == navBotoneraBtn_ &&
        (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto* de = static_cast<QDragMoveEvent*>(event);
        if (de->mimeData()->hasUrls()) {
            showInstantView();
            for (auto* b : {navRadioBtn_, navBotoneraBtn_, navMusicBtn_, navHomeBtn_, navStreamsBtn_})
                if (b) b->setStyleSheet(NAV_INACTIVE_STYLE);
            navBotoneraBtn_->setStyleSheet(NAV_ACTIVE_STYLE);
            de->acceptProposedAction();
            return true;
        }
    }

    // Drag & drop de archivos de audio sobre los botones F1-F12
    for (int i = 0; i < instantButtons_.size(); ++i) {
        if (obj != instantButtons_[i]) continue;

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
            if (de->mimeData()->hasUrls() && repo_) {
                for (const auto& url : de->mimeData()->urls()) {
                    QString path = url.toLocalFile();
                    if (!path.isEmpty() && FileScanner::isAudioFile(path)) {
                        // Si el botón ya tiene una canción, pedir confirmación
                        // antes de pisarla.
                        if (repo_->getSlot(currentPresetId_, i)) {
                            auto resp = QMessageBox::question(this,
                                tr("Reemplazar botón"),
                                tr("El botón F%1 ya tiene una canción asignada.\n"
                                   "¿Querés reemplazarla por la nueva?").arg(i + 1),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                            if (resp != QMessageBox::Yes) {
                                de->acceptProposedAction();
                                return true;   // cancelado: no se pisa
                            }
                        }
                        repo_->setSlot(currentPresetId_, i, path,
                                       QFileInfo(path).completeBaseName());
                        refreshButtons();
                        break;  // Solo un archivo por botón
                    }
                }
                de->acceptProposedAction();
                return true;
            }
        }
        break;
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace sara
