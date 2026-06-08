#include "ui/main_window.h"
#include "ui/header_widget.h"
#include "ui/schedule_column.h"
#include "ui/events_column.h"
#include "ui/live_assist_column.h"
#include "ui/settings_dialog.h"
#include "audio/audio_engine.h"
#include "audio/audio_pipeline.h"
#include "audio/crossfader.h"
#include "data/database.h"
#include "data/config_manager.h"
#include "core/track_selector.h"
#include "core/schedule_engine.h"
#include "core/time_announcer.h"
#include "core/event_dispatcher.h"
#include "data/schedule_repository.h"
#include "data/event_repository.h"
#include "data/instant_play_repo.h"
#include "data/stream_preset_repo.h"
#include "data/audit_manager.h"
#include "data/state_manager.h"
#include "core/pisador_manager.h"
#include "data/backup_manager.h"
#include "audio/recording_manager.h"
#include "data/user_manager.h"
#include "ui/login_dialog.h"
#include "ui/eq_compressor_dialog.h"
#include "ui/schedule_manager.h"
#include "ui/weekly_grid.h"
#include "ui/list_manager.h"
#include "ui/event_editor.h"
#include "ui/inverse_assign.h"
#include "ui/event_summary.h"
#include "ui/audit_dashboard.h"
#include "ui/vumeter_widget.h"
#include "util/logger.h"
#include "util/metadata_reader.h"
#include "util/qt_connect_helpers.h"

#include <QSplitter>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QApplication>
#include <QScreen>
#include <QPalette>
#include <QUrl>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QTextStream>
#include <QDir>
#include <QMenu>
#include <QCursor>
#include <QPointer>

namespace sara {

MainWindow::MainWindow(AudioEngine* audio, Database* db,
                       ConfigManager* config, UserManager* userMgr,
                       const UserInfo& currentUser, QWidget* parent)
    : QMainWindow(parent)
    , audio_(audio)
    , db_(db)
    , config_(config)
    , userMgr_(userMgr)
    , currentUser_(currentUser)
{
    setWindowTitle(QString("SARA Libre — %1").arg(config_->config().radioName));
    // Adaptar tamaño a la pantalla disponible
    QScreen* screen = QApplication::primaryScreen();
    QRect screenGeom = screen ? screen->availableGeometry() : QRect(0, 0, 1600, 900);
    int screenW = screenGeom.width();
    int screenH = screenGeom.height();

    // Mínimo razonable para que la UI funcione
    setMinimumSize(std::min(1024, screenW), std::min(600, screenH));

    // Tamaño inicial al 92% de la pantalla
    int w = static_cast<int>(screenW * 0.92);
    int h = static_cast<int>(screenH * 0.92);
    resize(w, h);
    // Centrar en pantalla
    move(screenGeom.x() + (screenW - w) / 2,
         screenGeom.y() + (screenH - h) / 2);

    // Track selector
    trackSelector_ = std::make_unique<TrackSelector>(this);
    trackSelector_->setDatabase(db_);
    trackSelector_->setNoRepeatHours(config_->config().noRepeatHours);
    trackSelector_->setNoRepeatArtistTracks(config_->config().noRepeatArtistTracks);

    // Metadata reader (duraciones reales)
    metadataReader_ = std::make_unique<MetadataReader>();

    // Time announcer (locución de hora)
    timeAnnouncer_ = std::make_unique<TimeAnnouncer>(this);
    timeAnnouncer_->configure(config_->config());

    // Schedule repository
    scheduleRepo_ = std::make_unique<ScheduleRepository>(db_);

    // Event repository
    eventRepo_ = std::make_unique<EventRepository>(db_);

    // Instant play repo
    instantRepo_ = std::make_unique<InstantPlayRepo>(db_);

    // Stream presets
    streamRepo_ = std::make_unique<StreamPresetRepo>(db_);

    // Audit manager
    auditManager_ = std::make_unique<AuditManager>(db_);

    // Crossfader
    crossfader_ = std::make_unique<Crossfader>(this);
    crossfader_->setDurationMs(config_->config().crossfadeMs);
    crossfader_->setTargetVolume(0.75);

    // Monitor de crossfade: revisa cada 500ms si la pista está por terminar
    // Cuando detecta que faltan ~crossfadeMs, carga la siguiente pista en el deck
    // standby y ejecuta un crossfade solapado real (ambos suenan al mismo tiempo)
    fadeMonitor_ = new QTimer(this);
    fadeMonitor_->setInterval(500);
    connect(fadeMonitor_, &QTimer::timeout, this, [this]() {
        if (!config_->config().crossfadeEnabled || fadeOutTriggered_ || crossfading_) return;
        if (eventDispatcher_->isEventPlaying()) return;
        if (!autoMode_) return;

        // No hacer crossfade si el operador pidió bucle o parar al terminar
        if (loopMode_ || stopAtEnd_) return;

        auto* active = audio_->activeDeck();
        if (!active || !active->isPlaying()) return;

        int64_t pos = active->positionMs();
        int64_t dur = active->durationMs();
        int fadeMs = config_->config().crossfadeMs;

        // Si estamos dentro del rango de crossfade y la pista es suficientemente larga
        if (dur > fadeMs * 2 && pos > 0 && (dur - pos) <= fadeMs) {
            fadeOutTriggered_ = true;

            if (eventDispatcher_->isWaitingForMain()) return;

            if (scheduleCol_->queueSize() == 0) return;
            QString nextTrack = scheduleCol_->getTrackAt(0);
            QString nextName = scheduleCol_->getTrackNameAt(0);
            if (nextTrack.isEmpty()) return;

            // Si la siguiente pista es una locución de hora, no hacer crossfade.
            if (nextTrack == "__SARA_TIME_ANNOUNCE__") {
                fadeOutTriggered_ = false;
                return;
            }

            // Si la siguiente pista es un stream, no hacer crossfade.
            // playNextTrack() maneja los timers, reintentos y display.
            if (nextTrack.startsWith("http://") || nextTrack.startsWith("https://")) {
                fadeOutTriggered_ = false;
                return;
            }

            scheduleCol_->removeFromQueue(0);

            QString sourceKey = scheduleEngine_->isFallback()
                ? "fallback" : "schedule:" + scheduleEngine_->activeScheduleName();
            QString sourceDisplay = scheduleEngine_->isFallback()
                ? tr("Alternativa") : scheduleEngine_->activeScheduleName();

            // Crossfade asimétrico:
            // - La pista actual hace fade-out durante fadeMs completo
            // - A la mitad del fade-out, entra la nueva pista a volumen completo (sin fade-in)
            // Así el golpe musical de la nueva canción no se pierde

            crossfading_ = true;

            // 1. Iniciar fade-out de la pista actual
            crossfader_->fadeOut(active);

            // 2. Timer para arrancar la nueva pista a la mitad del fade-out
            int entryDelay = fadeMs / 2;
            QTimer::singleShot(entryDelay, this, [this, nextTrack, nextName, sourceKey, sourceDisplay]() {
                auto* standby = audio_->standbyDeck();
                standby->setVolume(audio_->masterVolume());
                standby->play(nextTrack);

                scheduleCol_->setCurrentTrack(nextName, sourceDisplay);
                auditManager_->recordPlay(nextTrack, nextName, sourceKey, 0, "main");
                updateNowPlayingFile(nextName, sourceDisplay);

                LOG_INFO("[MainWindow] Nueva pista entra a volumen completo: {} [{}]",
                         nextName.toStdString(), sourceDisplay.toStdString());

                // Pisador durante crossfade
                if (pisadorManager_) {
                    pisadorManager_->onTrackStarted(nextTrack, standby);
                }
            });

            // 3. Al completar el fade-out, swap decks y restaurar volumen
            //
            // Nuevo contrato del Crossfader (defecto 1.1): al terminar fadeOut
            // el pipeline queda en volumen 0. ANTES de cualquier próxima
            // reproducción debemos:
            //   a) Detener el deck (que ahora pasa a ser "standby")
            //   b) Restaurar el volumen al masterVolume para no salir mudo
            //      la próxima vez que ese deck reciba play().
            connectOnce(crossfader_.get(), &Crossfader::fadeOutFinished,
                        this, [this]() {
                QTimer::singleShot(0, this, [this]() {
                    auto* oldDeck = audio_->activeDeck();  // El que terminó de fade-out
                    // Detener primero (con volumen aún en 0 = sin click),
                    // luego restaurar volumen para próximas reproducciones
                    oldDeck->stop();
                    oldDeck->setVolume(audio_->masterVolume());

                    audio_->swapDecks();
                    QTimer::singleShot(200, this, [this]() {
                        crossfading_ = false;
                        fadeOutTriggered_ = false;
                    });
                    LOG_INFO("[MainWindow] Crossfade asimétrico completado");
                    if (autoMode_) fillQueue();
                });
            });

            LOG_INFO("[MainWindow] Crossfade asimétrico iniciado (fade-out {}ms, entrada a {}ms): {}",
                     fadeMs, entryDelay,
                     QFileInfo(active->currentUri()).fileName().toStdString());
        }
    });
    fadeMonitor_->start();

    // Event dispatcher (ejecuta eventos automáticamente)
    eventDispatcher_ = std::make_unique<EventDispatcher>(this);
    eventDispatcher_->setEventRepository(eventRepo_.get());
    eventDispatcher_->setAudioEngine(audio_);
    eventDispatcher_->setCrossfader(crossfader_.get());
    eventDispatcher_->setFadeOutMs(config_->config().fadeOutMs);
    eventDispatcher_->setFadeOutEnabled(config_->config().fadeOutEnabled);
    eventDispatcher_->setTrackSelector(trackSelector_.get());
    eventDispatcher_->setMetadataReader(metadataReader_.get());
    eventDispatcher_->setTimeAnnouncer(timeAnnouncer_.get());
    eventDispatcher_->setAuditManager(auditManager_.get());
    eventDispatcher_->setAdIntroFile(config_->config().adIntroFile);
    eventDispatcher_->setAdOutroFile(config_->config().adOutroFile);

    // Señales del dispatcher de eventos
    connect(eventDispatcher_.get(), &EventDispatcher::eventStarted,
            this, [this](const QString& name) {
        LOG_INFO("[MainWindow] Evento iniciado: {}", name.toStdString());
        eventsCol_->onEventStarted(name);
    });

    connect(eventDispatcher_.get(), &EventDispatcher::eventFinished,
            this, [this](const QString& name) {
        LOG_INFO("[MainWindow] Evento finalizado: {}", name.toStdString());
        eventsCol_->onEventFinished(name);
        eventsCol_->refresh();
    });

    connect(eventDispatcher_.get(), &EventDispatcher::elementPlaying,
            this, [this](const QString& elementName, const QString& eventName) {
        eventsCol_->onElementPlaying(elementName, eventName);
    });

    connect(eventDispatcher_.get(), &EventDispatcher::streamElementStarted,
            this, [this](int64_t durationMs) {
        eventsCol_->startStreamTimer(durationMs);
    });

    connect(eventDispatcher_.get(), &EventDispatcher::eventExpired,
            this, [this]() {
        eventsCol_->refresh();
    });

    connect(eventDispatcher_.get(), &EventDispatcher::requestMainPause,
            this, [this]() {
        // Evento retardado: pausar la programación principal
        // (no detener, para que pueda reanudar)
    });

    connect(eventDispatcher_.get(), &EventDispatcher::requestMainResume,
            this, [this]() {
        // Evento terminó: reanudar la música
        if (autoMode_) {
            fillQueue();
            playNextTrack();
        }
    });

    // Schedule engine (cerebro de la programación)
    scheduleEngine_ = std::make_unique<ScheduleEngine>(this);
    scheduleEngine_->setDatabase(db_);
    scheduleEngine_->setScheduleRepository(scheduleRepo_.get());
    scheduleEngine_->setTrackSelector(trackSelector_.get());
    scheduleEngine_->setMetadataReader(metadataReader_.get());
    scheduleEngine_->setTimeAnnouncer(timeAnnouncer_.get());
    scheduleEngine_->setFallbackFolder(config_->config().fallbackFolder);

    // Señales del motor de programación
    connect(scheduleEngine_.get(), &ScheduleEngine::activeScheduleChanged,
            this, [this](const QString& name) {
        if (name.isEmpty()) {
            scheduleCol_->setCurrentTrack("", "");
            statusBar()->showMessage(
                QString("SARA Libre v%1 — Modo Automático (Respaldo)").arg(VERSION));
        } else {
            statusBar()->showMessage(
                QString("SARA Libre v%1 — %2").arg(VERSION, name));
        }
        // Limpiar cola y rellenar con pistas del nuevo bloque
        scheduleCol_->clearQueue();
        trackSelector_->clearPending();
        if (autoMode_) fillQueue();
    });

    connect(scheduleEngine_.get(), &ScheduleEngine::fallbackActivated,
            this, [this]() {
        LOG_INFO("[MainWindow] Fallback activado");
    });

    connect(scheduleEngine_.get(), &ScheduleEngine::scheduleActivated,
            this, [this](const QString& name) {
        LOG_INFO("[MainWindow] Programación activada: {}", name.toStdString());
    });

    // Carpeta de música actual (solo para fallback directo, el engine la maneja)
    currentMusicFolder_ = config_->config().fallbackFolder;

    loadTheme();
    setupUI();

    // Conexiones con el motor de audio
    connect(audio_, &AudioEngine::mainTrackFinished,
            this, &MainWindow::onMainTrackFinished);
    connect(audio_, &AudioEngine::mainAltTrackFinished,
            this, &MainWindow::onMainTrackFinished);

    // Detección de silencio: skip automático si una pista tiene silencio prolongado
    auto onSilence = [this]() {
        if (!autoMode_ || crossfading_ || fadeOutTriggered_) return;
        LOG_WARN("[MainWindow] Silencio prolongado detectado, saltando a siguiente pista");
        auto* deck = audio_->activeDeck();
        deck->stop();
        playNextTrack();
    };
    connect(audio_->mainPipeline(), &AudioPipeline::silenceDetected, this, onSilence);
    connect(audio_->mainAltPipeline(), &AudioPipeline::silenceDetected, this, onSilence);

    // Aplicar configuración de detección de silencio
    auto applySilenceConfig = [this]() {
        const auto& cfg = config_->config();
        int thresholdMs = cfg.silenceThresholdSecs * 1000;
        double levelDb = static_cast<double>(cfg.silenceLevelDb);
        audio_->mainPipeline()->setSilenceDetection(cfg.silenceDetectionEnabled, thresholdMs, levelDb);
        audio_->mainAltPipeline()->setSilenceDetection(cfg.silenceDetectionEnabled, thresholdMs, levelDb);
    };
    applySilenceConfig();

    // ── Guardado periódico de estado (recuperación tras crash) ──
    stateManager_ = std::make_unique<StateManager>(this);

    // Alimentar estado cada vez que el timer de posición actualiza (~4/s, pero el save es cada 30s)
    connect(audio_->mainPipeline(), &AudioPipeline::positionUpdated,
            this, [this](int64_t pos, int64_t) {
        stateManager_->setCurrentTrack(
            audio_->activeDeck()->currentUri(), 
            scheduleEngine_->isFallback() ? "fallback" : scheduleEngine_->activeScheduleName(),
            pos);
    });
    connect(audio_->mainAltPipeline(), &AudioPipeline::positionUpdated,
            this, [this](int64_t pos, int64_t) {
        stateManager_->setCurrentTrack(
            audio_->activeDeck()->currentUri(),
            scheduleEngine_->isFallback() ? "fallback" : scheduleEngine_->activeScheduleName(),
            pos);
    });

    // Verificar si hay un estado reciente (crash recovery)
    bool hasRecentState = stateManager_->hasRecentState();
    bool lastAutoMode = true;

    if (hasRecentState) {
        auto state = stateManager_->loadState();
        lastAutoMode = state.autoMode;
        if (state.valid && !state.queueTracks.isEmpty()) {
            LOG_INFO("[MainWindow] Estado reciente detectado ({}), recuperando...",
                     state.savedAt.toString("HH:mm:ss").toStdString());

            // Restaurar volumen
            audio_->setMasterVolume(state.volume);

            // Restaurar cola
            for (int i = 0; i < state.queueTracks.size(); ++i) {
                QString name = (i < state.queueNames.size()) ? state.queueNames[i] 
                    : QFileInfo(state.queueTracks[i]).completeBaseName();
                int64_t dur = (i < state.queueDurations.size()) ? state.queueDurations[i] : 0;
                scheduleCol_->addToQueue(name, state.queueTracks[i], dur);
            }
        }
    }

    // Determinar modo de inicio
    bool shouldStartAuto = false;
    switch (config_->config().startupMode) {
    case 1:  shouldStartAuto = true; break;   // Siempre AUTO
    case 2:  shouldStartAuto = false; break;  // Siempre Manual
    default: // 0: Recordar último estado
        shouldStartAuto = hasRecentState ? lastAutoMode : true;
        break;
    }
    LOG_INFO("[MainWindow] Modo de inicio: {} (config={}, último={})",
             shouldStartAuto ? "AUTO" : "Manual",
             config_->config().startupMode,
             lastAutoMode ? "AUTO" : "Manual");

    // Arrancar motor de programación si corresponde
    if (shouldStartAuto && !currentMusicFolder_.isEmpty()) {
        autoMode_ = true;
        scheduleEngine_->start();
        eventDispatcher_->start();
        stateManager_->setAutoMode(true);
        QMetaObject::invokeMethod(this, &MainWindow::playNextTrack, Qt::QueuedConnection);
    }
    header_->setAutoMode(shouldStartAuto);

    stateManager_->start();

    // ── Pisadores ────────────────────────────────────
    pisadorManager_ = std::make_unique<PisadorManager>(this);
    pisadorManager_->setDatabase(db_);
    pisadorManager_->setInstantPipeline(audio_->pisadorPipeline());
    pisadorManager_->setTimeAnnouncer(timeAnnouncer_.get());
    {
        const auto& c = config_->config();
        pisadorManager_->setEnabled(c.pisadorEnabled);
        pisadorManager_->setFolder(c.pisadorFolder);
        pisadorManager_->setFrequency(c.pisadorFrequency);
        pisadorManager_->setExcludedFolders(c.pisadorExcludedFolders);
        pisadorManager_->setDuckLevel(c.pisadorDuckLevel);
        pisadorManager_->setDelaySecs(c.pisadorDelaySecs);
    }

    // (VU meter visibilidad y pisadores se configuran después de crear las columnas)

    // ── Backup automático ────────────────────────────
    backupManager_ = std::make_unique<BackupManager>(this);
    backupManager_->setDatabasePath(db_->databasePath());
    backupManager_->setConfigPath(config_->configFilePath());
    backupManager_->setEnabled(config_->config().backupEnabled);
    backupManager_->setIntervalHours(config_->config().backupIntervalHours);
    backupManager_->setMaxBackups(config_->config().backupMaxCount);
    backupManager_->start();

    // ── Grabación testigo ────────────────────────────
    recordingManager_ = std::make_unique<RecordingManager>(this);
    {
        const auto& c = config_->config();
        recordingManager_->setOutputFolder(c.recordingFolder);
        recordingManager_->setFormat(c.recordingFormat);
        recordingManager_->setBitrate(c.recordingBitrate);
        recordingManager_->setSegmentByHour(c.recordingSegmentByHour);
        recordingManager_->setMainDevice(c.recordingDevice.isEmpty() ? c.mainAudioDevice : c.recordingDevice);
    }
    connect(recordingManager_.get(), &RecordingManager::errorOccurred,
            this, [this](const QString& msg) {
        LOG_ERROR("[Recording] {}", msg.toStdString());
        header_->setRecordingActive(false);
        statusBar()->showMessage(tr("Error de grabación: ") + msg, 5000);
    });
    connect(recordingManager_.get(), &RecordingManager::segmentCreated,
            this, [](const QString& path) {
        LOG_INFO("[Recording] Segmento completado: {}", path.toStdString());
    });

    LOG_INFO("[MainWindow] Ventana principal creada");

    // ── Permisos y vista ─────────────────────────────
    if (currentUser_.id > 0) {
        header_->setCurrentUser(currentUser_.displayName, currentUser_.role);
        currentView_ = currentUser_.defaultView;
    } else {
        header_->setCurrentUser("", UserRole::Operation);
        currentView_ = ViewMode::Operation;
    }
    applyPermissions();
    applyViewMode(currentView_);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUI()
{
    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── Header ───────────────────────────────────────
    header_ = new HeaderWidget(this);
    header_->setRadioName(config_->config().radioName);
    header_->setAudioEngine(audio_);
    header_->setAutoMode(true);  // Se actualiza en el constructor

    // Header controls (volume, auto mode, utilities)
    connect(header_, &HeaderWidget::volumeChanged,  this, &MainWindow::onVolumeChanged);
    connect(header_, &HeaderWidget::talkoverChanged, this, [this](bool on) {
        LOG_INFO("[MainWindow] Hablar encima: {}", on ? "ON" : "OFF");
    });
    connect(header_, &HeaderWidget::autoModeToggled, this, &MainWindow::onAutoModeToggled);
    connect(header_, &HeaderWidget::settingsClicked, this, &MainWindow::openSettings);
    connect(header_, &HeaderWidget::auditClicked, this, &MainWindow::openAuditDashboard);
    connect(header_, &HeaderWidget::clockClicked, this, &MainWindow::playTimeAnnouncement);
    connect(header_, &HeaderWidget::recordToggled, this, [this](bool on) {
        if (on) {
            if (config_->config().recordingFolder.isEmpty()) {
                header_->setRecordingActive(false);
                statusBar()->showMessage(
                    tr("Configure la carpeta de grabación en Opciones > Modo Automático"), 5000);
                return;
            }
            if (recordingManager_->startRecording()) {
                statusBar()->showMessage(
                    tr("● Grabación iniciada: ") + recordingManager_->currentFile(), 3000);
            } else {
                header_->setRecordingActive(false);
            }
        } else {
            recordingManager_->stopRecording();
            statusBar()->showMessage(tr("Grabación detenida"), 3000);
        }
    });

    connect(header_, &HeaderWidget::userMenuClicked, this, &MainWindow::showUserMenu);
    connect(header_, &HeaderWidget::viewToggled, this, [this]() {
        ViewMode newMode = (currentView_ == ViewMode::Full)
            ? ViewMode::Operation : ViewMode::Full;
        applyViewMode(newMode);
    });

    connect(header_, &HeaderWidget::eqClicked, this, &MainWindow::openEqCompressor);

    // Aplicar EQ/compresor desde configuración
    applyEqCompressorConfig();

    // Aplicar volumen inicial y nivel de talkover desde configuración
    header_->setDefaultVolume(config_->config().defaultVolume);
    header_->setTalkoverLevel(config_->config().talkoverLevel);
    audio_->setMasterVolume(config_->config().defaultVolume / 100.0);

    mainLayout->addWidget(header_);

    // ── Columnas ─────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setContentsMargins(8, 8, 8, 8);
    splitter->setHandleWidth(5);
    splitter->setChildrenCollapsible(true);

    // Columna 1: Programación Principal
    scheduleCol_ = new ScheduleColumn(this);
    scheduleCol_->setMusicPipeline(audio_->mainPipeline());
    // También conectar el deck alterno para que la UI muestre posición durante crossfade
    connect(audio_->mainAltPipeline(), &AudioPipeline::positionUpdated,
            scheduleCol_, &ScheduleColumn::onPositionUpdated);
    connect(scheduleCol_, &ScheduleColumn::skipRequested,
            this, [this](int row) {
                // Desmarcar las pistas que vamos a saltar
                for (int i = 0; i < row; ++i) {
                    QString path = scheduleCol_->getTrackAt(0);
                    if (!path.isEmpty()) trackSelector_->unmarkPending(path);
                    scheduleCol_->removeFromQueue(0);
                }

                performFadeOut([this]() {
                    playNextTrack();
                });
            });
    connect(scheduleCol_, &ScheduleColumn::removeRequested,
            this, [this](const QString& filePath) {
                if (!filePath.isEmpty()) trackSelector_->unmarkPending(filePath);
                if (autoMode_) fillQueue();
            });
    connect(scheduleCol_, &ScheduleColumn::manageSchedulesClicked,
            this, &MainWindow::openScheduleManager);
    connect(scheduleCol_, &ScheduleColumn::weeklyGridClicked,
            this, &MainWindow::openWeeklyGrid);
    connect(scheduleCol_, &ScheduleColumn::listManagerClicked,
            this, &MainWindow::openListManager);
    connect(scheduleCol_, &ScheduleColumn::cuePreviewRequested,
            this, &MainWindow::cuePreview);

    // Pisadores individuales desde el menú contextual de la cola
    connect(scheduleCol_, &ScheduleColumn::pisadorAssignRequested,
            this, &MainWindow::handlePisadorAssign);

    // Seek bar
    connect(scheduleCol_, &ScheduleColumn::seekRequested,
            this, [this](int64_t posMs) {
        audio_->activeDeck()->seek(posMs);
    });

    // Vaciar cola
    connect(scheduleCol_, &ScheduleColumn::clearQueueRequested,
            this, [this]() {
        scheduleCol_->clearQueue();
        trackSelector_->clearPending();
        LOG_INFO("[MainWindow] Cola vaciada por el operador");
    });

    // Regenerar cola
    connect(scheduleCol_, &ScheduleColumn::regenerateRequested,
            this, [this]() {
        scheduleCol_->clearQueue();
        trackSelector_->clearPending();
        fillQueue();
        LOG_INFO("[MainWindow] Cola regenerada");
        statusBar()->showMessage(tr("Cola regenerada"), 3000);
    });

    // Archivos arrastrados desde el explorador
    connect(scheduleCol_, &ScheduleColumn::filesDropped,
            this, [this](const QStringList& paths, int insertAt) {
        int pos = insertAt;
        for (const auto& path : paths) {
            auto meta = metadataReader_->read(path);
            QString displayName;
            if (!meta.title.isEmpty()) {
                displayName = meta.title;
                if (!meta.artist.isEmpty())
                    displayName += QString::fromUtf8(" — ") + meta.artist;
            } else {
                displayName = QFileInfo(path).completeBaseName();
            }
            scheduleCol_->insertInQueue(pos, displayName, path, meta.durationMs);
            trackSelector_->markAsPending(path);
            pos++;
        }
    });

    // (scheduleCol_ se agrega al splitter más abajo: primero Publicidad/Eventos,
    //  luego Programación Musical.)

    // Columna 2: Publicidad/Eventos
    eventsCol_ = new EventsColumn(this);
    eventsCol_->setEventRepository(eventRepo_.get());
    eventsCol_->setEventsPipeline(audio_->eventsPipeline());
    eventsCol_->setMetadataReader(metadataReader_.get());
    connect(eventsCol_, &EventsColumn::newEventClicked,
            this, &MainWindow::openNewEvent);
    connect(eventsCol_, &EventsColumn::editEventClicked,
            this, &MainWindow::openEditEvent);
    connect(eventsCol_, &EventsColumn::deleteEventClicked,
            this, [this](const QString& eventId) {
        eventRepo_->remove(eventId);
        eventsCol_->refresh();
    });
    connect(eventsCol_, &EventsColumn::duplicateEventClicked,
            this, [this](const QString& eventId, const QString& eventName) {
        bool ok = false;
        QString newName = QInputDialog::getText(this, tr("Duplicar evento"),
            tr("Nombre para la copia:"), QLineEdit::Normal,
            eventName + " (copia)", &ok);
        if (ok && !newName.trimmed().isEmpty()) {
            eventRepo_->duplicate(eventId, newName.trimmed());
            eventsCol_->refresh();
        }
    });
    connect(eventsCol_, &EventsColumn::playEventClicked,
            this, [this](const QString& eventId) {
        eventDispatcher_->playEventManually(eventId);
    });
    connect(eventsCol_, &EventsColumn::playExpiredEvent,
            this, [this](const QString& eventId) {
        eventDispatcher_->playEventManually(eventId);
        eventRepo_->removeExpiredEvent(eventId);
        eventsCol_->refresh();
    });
    connect(eventsCol_, &EventsColumn::inverseAssignClicked,
            this, &MainWindow::openInverseAssign);
    connect(eventsCol_, &EventsColumn::summaryClicked,
            this, &MainWindow::openEventSummary);

    // Events column transport
    connect(eventsCol_, &EventsColumn::playEventNow, this, [this](const QString& eventId) {
        if (eventId.isEmpty()) return;
        eventDispatcher_->playEventManually(eventId);
    });
    connect(eventsCol_, &EventsColumn::stopEventNow, this, [this]() {
        if (eventDispatcher_->isEventPlaying()) {
            eventDispatcher_->forceStopEvent();
        }
    });

    // Orden de columnas (izquierda → derecha): Publicidad/Eventos primero,
    // luego Programación Musical. Por eso eventos se agrega ANTES que schedule.
    splitter->addWidget(eventsCol_);    // Columna 1 (izquierda): Publicidad/Eventos
    splitter->addWidget(scheduleCol_);  // Columna 2: Programación Musical

    // Columna 3: Asistente en Vivo + Explorador
    liveAssistCol_ = new LiveAssistColumn(this);
    liveAssistCol_->setInstantPlayRepo(instantRepo_.get());
    liveAssistCol_->setInstantPipeline(audio_->instantPipeline());
    liveAssistCol_->setMusicFolder(config_->config().fallbackFolder);
    liveAssistCol_->setRadioFolder(config_->config().radioFolder);
    liveAssistCol_->setStreamPresetRepo(streamRepo_.get());
    liveAssistCol_->setMetadataReader(metadataReader_.get());
    connect(liveAssistCol_, &LiveAssistColumn::fileDoubleClicked,
        this, [this](const QString& path) {
            if (!autoMode_) {
                auto* deck = audio_->activeDeck();
                deck->play(path);
                scheduleCol_->setCurrentTrack(QFileInfo(path).completeBaseName(), tr("Manual"));
            } else {
                // En modo automático, reproducir en InstantPlay (no interrumpe)
                audio_->instantPipeline()->play(path);
            }
        });
    connect(liveAssistCol_, &LiveAssistColumn::addToQueueRequested,
        this, [this](const QString& path) {
            auto meta = metadataReader_->read(path);
            QString displayName;
            if (!meta.title.isEmpty()) {
                displayName = meta.title;
                if (!meta.artist.isEmpty()) displayName += " — " + meta.artist;
            } else {
                displayName = QFileInfo(path).completeBaseName();
            }
            scheduleCol_->addToQueue(displayName, path, meta.durationMs);
            trackSelector_->markAsPending(path);
        });
    connect(liveAssistCol_, &LiveAssistColumn::cuePreviewRequested,
            this, &MainWindow::cuePreview);
    connect(liveAssistCol_, &LiveAssistColumn::pisadorAssignRequested,
            this, &MainWindow::handlePisadorAssign);
    connect(liveAssistCol_, &LiveAssistColumn::streamToQueueRequested,
            this, [this](const QString& url, const QString& name, int64_t durationMs) {
        QString displayName = QString::fromUtf8("📡 ") + name;
        // Streams usan la URL como filePath — GStreamer puede reproducir URLs directamente
        scheduleCol_->addToQueue(displayName, url, durationMs);
        LOG_INFO("[MainWindow] Stream agregado a cola: {} ({}ms)", name.toStdString(), durationMs);
    });
    splitter->addWidget(liveAssistCol_);

    // Mínimos para que las columnas no desaparezcan
    scheduleCol_->setMinimumWidth(250);
    eventsCol_->setMinimumWidth(0);   // Colapsable
    liveAssistCol_->setMinimumWidth(250);

    // ── Transport controls (schedule column) ─────────
    connect(scheduleCol_, &ScheduleColumn::playClicked,    this, &MainWindow::onPlay);
    connect(scheduleCol_, &ScheduleColumn::pauseClicked,   this, &MainWindow::onPause);
    connect(scheduleCol_, &ScheduleColumn::stopClicked,    this, &MainWindow::onStop);
    connect(scheduleCol_, &ScheduleColumn::skipClicked,    this, [this]() {
        stopAtEnd_ = false;
        scheduleCol_->setStopAtEndActive(false);
        resetStreamRetryState();
        scheduleCol_->stopStreamTimer();
        disconnect(audio_->activeDeck(), &AudioPipeline::errorOccurred, this, nullptr);
        performFadeOut([this]() {
            playNextTrack();
        });
    });
    connect(scheduleCol_, &ScheduleColumn::stopAtEndClicked, this, [this]() {
        stopAtEnd_ = !stopAtEnd_;
        scheduleCol_->setStopAtEndActive(stopAtEnd_);
        if (stopAtEnd_ && loopMode_) {
            loopMode_ = false;
            scheduleCol_->setLoopActive(false);
        }
        LOG_INFO("[MainWindow] Parar al terminar: {}", stopAtEnd_ ? "ON" : "OFF");
    });
    connect(scheduleCol_, &ScheduleColumn::loopToggled, this, [this](bool on) {
        loopMode_ = on;
        if (loopMode_ && stopAtEnd_) {
            stopAtEnd_ = false;
            scheduleCol_->setStopAtEndActive(false);
        }
        LOG_INFO("[MainWindow] Bucle: {}", loopMode_ ? "ON" : "OFF");
    });

    // ── VU Meters: conectar a pipelines ──────────────
    auto* schedVu = scheduleCol_->vuMeter();
    connect(audio_->mainPipeline(), &AudioPipeline::levelUpdated,
            schedVu, &VuMeterWidget::setLevels);
    connect(audio_->mainAltPipeline(), &AudioPipeline::levelUpdated,
            schedVu, &VuMeterWidget::setLevels);

    auto* eventsVu = eventsCol_->vuMeter();
    connect(audio_->eventsPipeline(), &AudioPipeline::levelUpdated,
            eventsVu, &VuMeterWidget::setLevels);

    // VU Meter visibilidad
    bool vuEnabled = config_->config().vuMeterEnabled;
    scheduleCol_->vuMeter()->setVisible(vuEnabled);
    eventsCol_->vuMeter()->setVisible(vuEnabled);

    // Indicador visual de pisadores en la cola
    scheduleCol_->setPisadorChecker([this](const QString& fp) {
        return pisadorManager_ && pisadorManager_->hasPisadorIndicator(fp);
    });

    // Proporciones de las columnas: 40% - 30% - 30%
    splitter->setStretchFactor(0, 4);  // Publicidad/Eventos: ~36%
    splitter->setStretchFactor(1, 4);  // Programación Musical: ~36%
    splitter->setStretchFactor(2, 3);  // Asistente: ~27%

    // Forzar proporciones iniciales después del layout
    QTimer::singleShot(0, this, [splitter]() {
        int w = splitter->width();
        if (w > 0) {
            splitter->setSizes({w * 4 / 11, w * 4 / 11, w * 3 / 11});
        }
    });

    mainLayout->addWidget(splitter, 1);

    setCentralWidget(centralWidget);

    // ── Status bar ───────────────────────────────────
    statusBar()->showMessage(
        tr("SARA Libre v%1 — %2")
            .arg(VERSION)
            .arg(autoMode_ ? "Modo Automático" : "Modo Manual")
    );
}

void MainWindow::loadTheme()
{
    const auto& cfg = config_->config();

    // Determinar tema
    QString themeFile;
    bool isDark;
    if (cfg.theme == "light") {
        themeFile = ":/style/light.qss";
        isDark = false;
    } else if (cfg.theme == "dark") {
        themeFile = ":/style/dark.qss";
        isDark = true;
    } else {
        // Auto: detectar tema del sistema
        QPalette sysPal = QApplication::palette();
        int bgLightness = sysPal.color(QPalette::Window).lightness();
        isDark = (bgLightness <= 128);
        themeFile = isDark ? ":/style/dark.qss" : ":/style/light.qss";
    }

    QFile styleFile(themeFile);
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString css = styleFile.readAll();
        styleFile.close();

        // Ajustar tamaño de fuente global
        if (cfg.fontSize != 0) {
            int baseSize = 13 + (cfg.fontSize * 2);  // -1→11, 0→13, +1→15
            css.replace("font-size: 13px;",
                       QString("font-size: %1px;").arg(baseSize));
        }

        qApp->setStyleSheet(css);
    } else {
        LOG_WARN("[MainWindow] No se pudo cargar el tema: {}", themeFile.toStdString());
    }

    // Configurar paleta global para que los widgets nativos y los iconos
    // hereden colores correctos según el tema
    QPalette pal = qApp->palette();
    if (isDark) {
        pal.setColor(QPalette::Window, QColor("#12121f"));
        pal.setColor(QPalette::WindowText, QColor("#e0e0f0"));
        pal.setColor(QPalette::Base, QColor("#1a1a30"));
        pal.setColor(QPalette::AlternateBase, QColor("#22223a"));
        pal.setColor(QPalette::Text, QColor("#e0e0f0"));
        pal.setColor(QPalette::Button, QColor("#2a2a45"));
        pal.setColor(QPalette::ButtonText, QColor("#e0e0f0"));
        pal.setColor(QPalette::Highlight, QColor("#667eea"));
        pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        pal.setColor(QPalette::ToolTipBase, QColor("#1e1e38"));
        pal.setColor(QPalette::ToolTipText, QColor("#e0e0f0"));
        pal.setColor(QPalette::PlaceholderText, QColor("#707090"));
    } else {
        pal.setColor(QPalette::Window, QColor("#f0f1f5"));
        pal.setColor(QPalette::WindowText, QColor("#1a1a2e"));
        pal.setColor(QPalette::Base, QColor("#ffffff"));
        pal.setColor(QPalette::AlternateBase, QColor("#f5f5fa"));
        pal.setColor(QPalette::Text, QColor("#1a1a2e"));
        pal.setColor(QPalette::Button, QColor("#e0e2f0"));
        pal.setColor(QPalette::ButtonText, QColor("#1a1a2e"));
        pal.setColor(QPalette::Highlight, QColor("#667eea"));
        pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        pal.setColor(QPalette::ToolTipBase, QColor("#ffffff"));
        pal.setColor(QPalette::ToolTipText, QColor("#1a1a2e"));
        pal.setColor(QPalette::PlaceholderText, QColor("#8080a0"));
    }
    qApp->setPalette(pal);

    // Forzar actualización de todos los widgets (header views, tables, etc.)
    for (auto* widget : qApp->allWidgets()) {
        widget->setPalette(pal);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }

    LOG_INFO("[MainWindow] Tema: {} (fuente: {}px)",
             isDark ? "oscuro" : "claro", 13 + cfg.fontSize * 2);
}

// ── Slots de control ─────────────────────────────────────

void MainWindow::onPlay()
{
    auto* deck = audio_->activeDeck();
    if (deck->state() == PlaybackState::Paused) {
        deck->resume();
    } else if (deck->state() == PlaybackState::Stopped) {
        playNextTrack();
    }
}

void MainWindow::onPause()
{
    auto* deck = audio_->activeDeck();
    if (deck->state() == PlaybackState::Paused) {
        deck->resume();   // segundo toque del mismo botón: continúa la reproducción
    } else {
        deck->pause();
    }
}

void MainWindow::onStop()
{
    // Resetear flags de modo manual
    if (loopMode_) {
        loopMode_ = false;
        scheduleCol_->setLoopActive(false);
    }
    if (stopAtEnd_) {
        stopAtEnd_ = false;
        scheduleCol_->setStopAtEndActive(false);
    }

    // Defecto 1.2: cancelar cualquier secuencia de locución en curso
    inAnnouncementSequence_ = false;
    announcementQueue_.clear();

    // Limpiar estado de reintentos de stream
    resetStreamRetryState();
    scheduleCol_->stopStreamTimer();
    disconnect(audio_->activeDeck(), &AudioPipeline::errorOccurred, this, nullptr);
    disconnect(audio_->standbyDeck(), &AudioPipeline::errorOccurred, this, nullptr);

    // Si hay un evento/streaming sonando, forzar su detención
    if (eventDispatcher_->isEventPlaying() || eventDispatcher_->isWaitingForMain()) {
        eventDispatcher_->forceStopEvent();
        // También detener los decks principales si estaban sonando
        audio_->activeDeck()->setVolume(audio_->masterVolume());
        audio_->standbyDeck()->setVolume(audio_->masterVolume());
        audio_->activeDeck()->stop();
        audio_->standbyDeck()->stop();
        scheduleCol_->setCurrentTrack("", "");
        fadeOutTriggered_ = false;
        crossfading_ = false;

        // Si estamos en modo automático, retomar la programación
        if (autoMode_) {
            fillQueue();
            playNextTrack();
        }
        return;
    }

    auto* standby = audio_->standbyDeck();

    performFadeOut([this, standby]() {
        standby->setVolume(audio_->masterVolume());
        standby->stop();
        scheduleCol_->setCurrentTrack("", "");
        fadeOutTriggered_ = false;
        crossfading_ = false;
    });
}

void MainWindow::performFadeOut(std::function<void()> onComplete)
{
    auto* deck = audio_->activeDeck();
    const auto& cfg = config_->config();

    // Defecto 1.9: si ya hay un fadeTimer activo (Stop seguido por otro
    // Stop), cancelarlo. Antes se creaban múltiples timers que se pisaban
    // el volumen mutuamente.
    if (fadeOutTimer_ && fadeOutTimer_->isActive()) {
        fadeOutTimer_->stop();
        fadeOutTimer_->deleteLater();
        fadeOutTimer_ = nullptr;
    }

    if (!cfg.fadeOutEnabled || !deck->isPlaying() || cfg.fadeOutMs < 50) {
        // Sin fade: ejecutar inmediatamente
        if (deck->isPlaying()) {
            deck->stop();
            deck->setVolume(audio_->masterVolume());
        }
        if (onComplete) onComplete();
        return;
    }

    double startVol = deck->volume();
    int durationMs = cfg.fadeOutMs;
    int stepMs = 30;
    int totalSteps = durationMs / stepMs;
    if (totalSteps < 2) totalSteps = 2;

    fadeOutTimer_ = new QTimer(this);
    int step = 0;
    QPointer<QTimer> safeTimer = fadeOutTimer_;
    connect(fadeOutTimer_, &QTimer::timeout, this,
            [this, deck, startVol, totalSteps, stepMs, step, safeTimer,
             onComplete = std::move(onComplete)]() mutable {
        // Si el timer fue reemplazado por otro performFadeOut, salir
        if (safeTimer != fadeOutTimer_) return;
        step++;
        double t = static_cast<double>(step) / totalSteps;
        deck->setVolume(startVol * (1.0 - t));
        if (step >= totalSteps) {
            fadeOutTimer_->stop();
            fadeOutTimer_->deleteLater();
            fadeOutTimer_ = nullptr;
            deck->stop();
            deck->setVolume(audio_->masterVolume());
            if (onComplete) onComplete();
        }
    });
    fadeOutTimer_->start(stepMs);
}

void MainWindow::onVolumeChanged(int percent)
{
    audio_->setMasterVolume(percent / 100.0);
    if (stateManager_) stateManager_->setVolume(percent / 100.0);
}

void MainWindow::onAutoModeToggled(bool on)
{
    autoMode_ = on;
    if (stateManager_) stateManager_->setAutoMode(on);
    LOG_INFO("[MainWindow] Modo: {}", on ? "Automático" : "Manual");

    if (autoMode_) {
        scheduleEngine_->start();
        eventDispatcher_->start();
        statusBar()->showMessage(
            QString("SARA Libre v%1 — Modo Automático").arg(VERSION));
        if (!audio_->activeDeck()->isPlaying()) {
            playNextTrack();
        }
    } else {
        scheduleEngine_->stop();
        eventDispatcher_->stop();
        statusBar()->showMessage(
            QString("SARA Libre v%1 — Modo Manual").arg(VERSION));
    }
}

void MainWindow::onMainTrackFinished()
{
    auto* active = audio_->activeDeck();
    auto* standby = audio_->standbyDeck();
    LOG_INFO("[MainWindow] trackFinished recibido (crossfading={}, active={}, standby={})",
             crossfading_ ? "true" : "false",
             QFileInfo(QUrl(active->currentUri()).toLocalFile()).fileName().toStdString(),
             QFileInfo(QUrl(standby->currentUri()).toLocalFile()).fileName().toStdString());

    // Defecto 1.2: si estamos reproduciendo una secuencia de locución de
    // hora, encadenar el siguiente archivo en el mismo deck.
    if (inAnnouncementSequence_) {
        if (!announcementQueue_.isEmpty()) {
            QString next = announcementQueue_.takeFirst();
            active->play(next);
            LOG_INFO("[MainWindow] Locución encadenada: {}",
                     QFileInfo(next).fileName().toStdString());
            return;
        }
        // Secuencia terminada — limpiar flag y continuar con la cola normal
        inAnnouncementSequence_ = false;
        LOG_INFO("[MainWindow] Locución de hora completada, continuando");
        // Caer al flujo normal (siguiente pista, eventos pendientes, etc.)
    }

    // Si estamos en medio de un crossfade solapado, esta señal viene del
    // deck que estaba sonando y terminó naturalmente → registrar finalización
    if (crossfading_) {
        // Registrar la pista que terminó (el deck viejo)
        auto* oldDeck = audio_->activeDeck();  // Aún no se hizo swapDecks
        QString uri = oldDeck->currentUri();
        if (!uri.isEmpty()) {
            // Streams (http://) → toLocalFile() retorna string vacía;
            // usamos el URI completo para mantener el registro consistente.
            QString filePath = QUrl(uri).toLocalFile();
            if (filePath.isEmpty()) filePath = uri;
            auditManager_->recordFinish(filePath, oldDeck->durationMs());
            trackSelector_->unmarkPending(filePath);
        }
        return;
    }

    // Protección contra trackFinished tardío del deck viejo después de crossfade.
    // Si el deck activo ya está reproduciendo algo, es un finish del standby → ignorar.
    if (active->isPlaying() && standby->state() == PlaybackState::Stopped) {
        LOG_INFO("[MainWindow] trackFinished tardío del deck standby, ignorando");
        // Registrar finalización del standby
        QString uri = standby->currentUri();
        if (!uri.isEmpty()) {
            QString filePath = QUrl(uri).toLocalFile();
            if (filePath.isEmpty()) filePath = uri;
            auditManager_->recordFinish(filePath, standby->durationMs());
            trackSelector_->unmarkPending(filePath);
        }
        return;
    }

    // Cancelar cualquier fade en curso y restaurar volumen
    if (crossfader_->isActive()) {
        crossfader_->cancel();
    }
    fadeOutTriggered_ = false;
    audio_->activeDeck()->setVolume(audio_->masterVolume());
    scheduleCol_->stopStreamTimer();
    resetStreamRetryState();
    disconnect(active, &AudioPipeline::errorOccurred, this, nullptr);

    // Registrar la pista que terminó
    QString uri = active->currentUri();
    if (!uri.isEmpty()) {
        // Para streams (http://, https://) toLocalFile() devuelve string
        // vacía. En ese caso usamos el URI completo como file_path para que
        // la auditoría tenga registro y no caiga el INSERT por NOT NULL.
        QString filePath = QUrl(uri).toLocalFile();
        if (filePath.isEmpty()) filePath = uri;

        QString sourceKey = scheduleEngine_->isFallback()
            ? "fallback"
            : "schedule:" + scheduleEngine_->activeScheduleName();
        trackSelector_->recordPlay(filePath, sourceKey, active->durationMs());
        trackSelector_->unmarkPending(filePath);
        auditManager_->recordFinish(filePath, active->durationMs());

        // Bucle: repetir la misma pista (solo aplica a archivos locales)
        if (loopMode_ && QFileInfo::exists(filePath)) {
            LOG_INFO("[MainWindow] Bucle: repitiendo {}", QFileInfo(filePath).fileName().toStdString());
            active->play(filePath);
            return;
        }
    }

    // Parar al terminar: no reproducir siguiente
    if (stopAtEnd_) {
        stopAtEnd_ = false;
        scheduleCol_->setStopAtEndActive(false);
        LOG_INFO("[MainWindow] Parar al terminar: detenido");
        statusBar()->showMessage(tr("Reproducción detenida (parar al terminar)"), 5000);
        return;
    }

    // Si el dispatcher de eventos está esperando, notificarle primero
    if (eventDispatcher_->notifyMainTrackFinished()) {
        return;  // El dispatcher tomó el control
    }

    // Reproducir siguiente en modo automático
    if (autoMode_ && !eventDispatcher_->isEventPlaying()) {
        playNextTrack();
    }
}

void MainWindow::playNextTrack()
{
    // Si la cola está vacía, rellenarla (solo en modo automático)
    if (scheduleCol_->queueSize() == 0 && autoMode_) {
        fillQueue();
    }

    if (scheduleCol_->queueSize() == 0) {
        LOG_WARN("[MainWindow] No se encontraron archivos de audio");
        return;
    }

    // Tomar la primera pista DE LA COLA (fila 0)
    QString track = scheduleCol_->getTrackAt(0);
    QString displayName = scheduleCol_->getTrackNameAt(0);
    int64_t queuedDurationMs = scheduleCol_->getTrackDurationAt(0);

    if (track.isEmpty()) {
        LOG_WARN("[MainWindow] Pista vacía en fila 0 de la cola");
        return;
    }

    // Reset de estado de reintento de stream (si la pista anterior fue
    // un stream y aún quedaba un timer pendiente, lo cancelamos antes de
    // arrancar la siguiente).
    resetStreamRetryState();

    // Quitar de la cola la pista que vamos a reproducir
    scheduleCol_->removeFromQueue(0);

    // ── Marcador de locución de hora ─────────────────
    // La hora se resuelve AHORA, al momento de reproducir, no cuando se llenó la cola
    if (track == "__SARA_TIME_ANNOUNCE__") {
        if (!timeAnnouncer_->isConfigured()) {
            LOG_WARN("[MainWindow] Locución de hora en cola pero no configurada, saltando");
            // Defecto 1.3: usar invokeMethod en lugar de recursión directa
            // para evitar consumir stack si toda la cola son marcadores.
            QMetaObject::invokeMethod(this, &MainWindow::playNextTrack,
                                      Qt::QueuedConnection);
            return;
        }

        QStringList files = timeAnnouncer_->generateForCurrentTime();
        if (files.isEmpty()) {
            QMetaObject::invokeMethod(this, &MainWindow::playNextTrack,
                                      Qt::QueuedConnection);
            return;
        }

        // Reproducir la secuencia completa en el deck activo
        auto* deck = audio_->activeDeck();
        fadeOutTriggered_ = false;
        crossfading_ = false;

        announcementQueue_ = files;
        announcementQueue_.removeFirst();

        // Defecto 1.2: NO desconectar onMainTrackFinished. En su lugar,
        // marcamos un flag que onMainTrackFinished interpreta para encadenar
        // los archivos de la locución. Antes se hacía disconnect/reconnect y,
        // si el operador presionaba Stop o cambiaba a manual durante la
        // locución, las conexiones quedaban rotas para el resto de la sesión
        // y la radio nunca volvía a avanzar al final de pista.
        inAnnouncementSequence_ = true;

        deck->setVolume(audio_->masterVolume());
        deck->play(files.first());
        scheduleCol_->setCurrentTrack(tr("Locución de hora"), "Hora");

        LOG_INFO("[MainWindow] Locución de hora en programación: {} archivos", files.size());
        if (autoMode_) fillQueue();
        return;
    }

    // Determinar la fuente (interna en inglés para BD, traducida para UI)
    QString sourceKey = scheduleEngine_->isFallback()
        ? "fallback" : "schedule:" + scheduleEngine_->activeScheduleName();
    QString sourceDisplay = scheduleEngine_->isFallback()
        ? tr("Alternativa") : scheduleEngine_->activeScheduleName();

    // Reproducir en el deck activo (sin fade-in, el crossfade solapado
    // se maneja automáticamente por el fadeMonitor antes del fin de pista)
    auto* deck = audio_->activeDeck();
    fadeOutTriggered_ = false;
    crossfading_ = false;
    deck->setVolume(audio_->masterVolume());

    // Si es un stream, configurar timer ANTES de play()
    // (GStreamer emite positionUpdated inmediatamente con duraciones basura)
    bool isStream = track.startsWith("http://") || track.startsWith("https://");
    if (isStream) {
        scheduleCol_->setCurrentTrack(displayName, tr("Stream"));
        scheduleCol_->startStreamTimer(queuedDurationMs);

        if (queuedDurationMs > 0) {
            if (!streamDurationTimer_) {
                streamDurationTimer_ = new QTimer(this);
                streamDurationTimer_->setSingleShot(true);
                connect(streamDurationTimer_, &QTimer::timeout, this, [this]() {
                    LOG_INFO("[MainWindow] Stream: tiempo cumplido, avanzando");
                    scheduleCol_->stopStreamTimer();
                    auto* d = audio_->activeDeck();
                    d->stop();
                    playNextTrack();
                });
            }
            streamDurationTimer_->start(static_cast<int>(queuedDurationMs));
            LOG_INFO("[MainWindow] Stream con timer: {} seg", queuedDurationMs / 1000);
        }
    } else {
        scheduleCol_->setCurrentTrack(displayName, sourceDisplay);
    }

    deck->play(track);

    // Stream: conectar manejo de errores con reintentos.
    //
    // Este flujo replica el del EventDispatcher para mantener consistencia:
    //   - 3 intentos × 3s de delay (no inmediato)
    //   - Distinción entre "conexión inicial fallida" (cuenta contra el
    //     límite) y "desconexión en medio" (reinicia contador, porque suele
    //     ser un corte transitorio de internet)
    //   - Idempotencia: GStreamer puede emitir Error+EOS para un mismo
    //     fallo, pero AudioPipeline ya deduplica eso (fase 8d teardownPending_)
    //
    // Esto NO se ejecutaba antes porque AudioPipeline::handleError hacía
    // auto-skip en pipelines Main/MainAlt, emitiendo trackFinished en vez
    // de errorOccurred. Ahora handleError detecta streams (URI http://) y
    // delega al caller — éste handler de errorOccurred se ejecuta como
    // corresponde.
    if (isStream) {
        streamRetryCount_ = 0;
        streamRetryUrl_ = track;
        streamRetryName_ = displayName;
        streamConnected_ = false;

        // Marcar conexión estabilizada después de STREAM_STABILIZE_MS.
        // Si llega un error antes, sigue contando como "intento fallido"
        // (incrementa streamRetryCount_); si llega después, se reinicia el
        // contador para tratarlo como desconexión transitoria.
        if (!streamStabilizedTimer_) {
            streamStabilizedTimer_ = new QTimer(this);
            streamStabilizedTimer_->setSingleShot(true);
            connect(streamStabilizedTimer_, &QTimer::timeout, this, [this]() {
                streamConnected_ = true;
                LOG_DEBUG("[MainWindow] Stream estabilizado: {}",
                          streamRetryUrl_.toStdString());
            });
        }
        streamStabilizedTimer_->stop();
        streamStabilizedTimer_->start(STREAM_STABILIZE_MS);

        // Desconectar handler anterior si existía
        disconnect(deck, &AudioPipeline::errorOccurred, this, nullptr);
        connect(deck, &AudioPipeline::errorOccurred, this, [this](const QString& msg) {
            // Cancelar timer de estabilización si estaba pendiente
            if (streamStabilizedTimer_ && streamStabilizedTimer_->isActive()) {
                streamStabilizedTimer_->stop();
            }

            // Si la conexión estaba estabilizada (>5s sonando) es una
            // desconexión transitoria → reiniciamos contador para darle
            // 3 oportunidades nuevas. Si no, cuenta contra el límite.
            if (streamConnected_) {
                LOG_WARN("[MainWindow] Stream desconectado en medio de transmisión: {}",
                         msg.toStdString());
                streamRetryCount_ = 0;
                streamConnected_ = false;
            } else {
                LOG_WARN("[MainWindow] Stream error: {}", msg.toStdString());
            }

            streamRetryCount_++;
            if (streamRetryCount_ <= STREAM_MAX_RETRIES) {
                LOG_INFO("[MainWindow] Reintento {}/{} en {} seg...",
                         streamRetryCount_, STREAM_MAX_RETRIES,
                         STREAM_RETRY_DELAY_MS / 1000);
                statusBar()->showMessage(
                    tr("Stream: reintentando conexión (%1/%2)...")
                        .arg(streamRetryCount_).arg(STREAM_MAX_RETRIES), 5000);

                if (!streamRetryTimer_) {
                    streamRetryTimer_ = new QTimer(this);
                    streamRetryTimer_->setSingleShot(true);
                    connect(streamRetryTimer_, &QTimer::timeout, this, [this]() {
                        auto* d = audio_->activeDeck();
                        // Re-armar el timer de estabilización para este intento
                        if (streamStabilizedTimer_) {
                            streamStabilizedTimer_->stop();
                            streamStabilizedTimer_->start(STREAM_STABILIZE_MS);
                        }
                        streamConnected_ = false;
                        d->play(streamRetryUrl_);
                        LOG_INFO("[MainWindow] Reintentando stream: {}",
                                 streamRetryUrl_.toStdString());
                    });
                }
                streamRetryTimer_->start(STREAM_RETRY_DELAY_MS);
            } else {
                LOG_ERROR("[MainWindow] Stream falló tras {} reintentos, avanzando",
                          STREAM_MAX_RETRIES);
                statusBar()->showMessage(
                    tr("Stream no disponible, continuando programación"), 5000);

                // Desconectar handler de error
                auto* d = audio_->activeDeck();
                disconnect(d, &AudioPipeline::errorOccurred, this, nullptr);

                // Limpiar estado de stream
                scheduleCol_->stopStreamTimer();
                if (streamDurationTimer_ && streamDurationTimer_->isActive())
                    streamDurationTimer_->stop();
                if (streamStabilizedTimer_ && streamStabilizedTimer_->isActive())
                    streamStabilizedTimer_->stop();
                streamConnected_ = false;

                d->stop();
                playNextTrack();
            }
        });
    }

    // Auditoría: registrar inicio de reproducción
    auditManager_->recordPlay(track, displayName, sourceKey, 0, "main");

    // Actualizar archivo "sonando ahora" para Butt / streaming
    updateNowPlayingFile(displayName, sourceDisplay);

    LOG_INFO("[MainWindow] Reproduciendo: {} [{}]",
             displayName.toStdString(), sourceDisplay.toStdString());

    // Pisador: verificar si esta pista debe ser pisada
    if (pisadorManager_) {
        pisadorManager_->onTrackStarted(track, deck);
    }

    // Rellenar la cola para mantener siempre pistas adelante (solo en auto)
    if (autoMode_) fillQueue();
}

void MainWindow::fillQueue()
{
    // Mantener al menos 8 pistas en cola
    static const int TARGET_QUEUE_SIZE = 8;

    while (scheduleCol_->queueSize() < TARGET_QUEUE_SIZE) {
        // El ScheduleEngine decide de dónde viene la pista
        // (programación activa o fallback)
        auto resolved = scheduleEngine_->resolveNextTrack();
        if (!resolved) break;

        scheduleCol_->addToQueue(
            resolved->displayName,
            resolved->filePath,
            resolved->durationMs
        );
    }

    // Sincronizar cola con el StateManager
    if (stateManager_) {
        QStringList tracks, names;
        QList<int64_t> durations;
        for (int i = 0; i < scheduleCol_->queueSize(); ++i) {
            tracks << scheduleCol_->getTrackAt(i);
            names << scheduleCol_->getTrackNameAt(i);
            durations << 0;  // Duración no es crítica para recovery
        }
        stateManager_->setQueue(tracks, names, durations);
        stateManager_->setActiveSchedule(
            scheduleEngine_->isFallback() ? "fallback" : scheduleEngine_->activeScheduleName());
    }
}

void MainWindow::openScheduleManager()
{
    ScheduleManager mgr(scheduleRepo_.get(), this);
    mgr.exec();
    // Re-check en caso de que la programación haya cambiado
    if (autoMode_ && scheduleEngine_->isRunning()) {
        scheduleEngine_->checkSchedule();
    }
}

void MainWindow::openWeeklyGrid()
{
    WeeklyGrid grid(scheduleRepo_.get(), this);
    grid.exec();
    if (autoMode_ && scheduleEngine_->isRunning()) {
        scheduleEngine_->checkSchedule();
    }
}

void MainWindow::playTimeAnnouncement()
{
    if (!timeAnnouncer_->isConfigured()) {
        QMessageBox::information(this, tr("Locución de hora"),
            tr("Las carpetas de locución de hora no están configuradas.\n\nVaya a Configuración para establecer las carpetas de horas y minutos."));
        return;
    }

    QStringList files = timeAnnouncer_->generateForCurrentTime();
    if (files.isEmpty()) {
        QMessageBox::warning(this, tr("Locución de hora"),
            tr("No se encontraron archivos para la hora actual."));
        return;
    }

    // Reproducir la secuencia en el pipeline de InstantPlay
    auto* pipeline = audio_->instantPipeline();

    // Guardar la cola de archivos pendientes
    announcementQueue_ = files;
    announcementQueue_.removeFirst();

    // Desconectar conexiones anteriores de MainWindow con este pipeline
    disconnect(pipeline, &AudioPipeline::trackFinished, this, nullptr);

    // Conectar la reproducción encadenada con DirectConnection para que
    // el siguiente archivo arranque ANTES de que LiveAssistColumn reciba la señal
    connect(pipeline, &AudioPipeline::trackFinished, this, [this]() {
        if (!announcementQueue_.isEmpty()) {
            QString next = announcementQueue_.takeFirst();
            LOG_INFO("[MainWindow] Locución encadenada: {}", QFileInfo(next).fileName().toStdString());
            audio_->instantPipeline()->play(next);
        } else {
            // Secuencia terminada, desconectar
            disconnect(audio_->instantPipeline(), &AudioPipeline::trackFinished, this, nullptr);
        }
    }, Qt::DirectConnection);

    // Reproducir el primer archivo
    pipeline->play(files.first());

    LOG_INFO("[MainWindow] Reproduciendo locución de hora: {} archivos", files.size());
}

void MainWindow::openNewEvent()
{
    EventEditor editor(eventRepo_.get(), this);
    editor.setStreamPresetRepo(streamRepo_.get());
    connect(&editor, &EventEditor::cuePreviewRequested, this, &MainWindow::cuePreview);
    connect(&editor, &EventEditor::cueStopRequested, this, &MainWindow::cueStop);
    if (editor.exec() == QDialog::Accepted) {
        eventsCol_->refresh();
    }
    cueStop();
}

void MainWindow::openEditEvent(const QString& eventId)
{
    EventEditor editor(eventRepo_.get(), eventId, this);
    editor.setStreamPresetRepo(streamRepo_.get());
    connect(&editor, &EventEditor::cuePreviewRequested, this, &MainWindow::cuePreview);
    connect(&editor, &EventEditor::cueStopRequested, this, &MainWindow::cueStop);
    if (editor.exec() == QDialog::Accepted) {
        eventsCol_->refresh();
    }
    cueStop();  // Detener preescucha al cerrar el editor
}

void MainWindow::openInverseAssign()
{
    InverseAssignDialog dlg(eventRepo_.get(), this);
    dlg.exec();
    eventsCol_->refresh();
}

void MainWindow::openEventSummary()
{
    EventSummaryDialog dlg(eventRepo_.get(), this);
    connect(&dlg, &EventSummaryDialog::editEventRequested,
            this, [this, &dlg](const QString& eventId) {
        EventEditor editor(eventRepo_.get(), eventId, &dlg);
        editor.setStreamPresetRepo(streamRepo_.get());
        connect(&editor, &EventEditor::cuePreviewRequested, this, &MainWindow::cuePreview);
        connect(&editor, &EventEditor::cueStopRequested, this, &MainWindow::cueStop);
        if (editor.exec() == QDialog::Accepted) {
            // Reload summary data
        }
        cueStop();
    });
    connect(&dlg, &EventSummaryDialog::deleteEventRequested,
            this, [this](const QString& eventId) {
        eventRepo_->remove(eventId);
        eventsCol_->refresh();
    });
    connect(&dlg, &EventSummaryDialog::duplicateEventRequested,
            this, [this](const QString& eventId, const QString& eventName) {
        bool ok = false;
        QString newName = QInputDialog::getText(this, tr("Duplicar evento"),
            tr("Nombre para la copia:"), QLineEdit::Normal,
            eventName + " (copia)", &ok);
        if (ok && !newName.trimmed().isEmpty()) {
            eventRepo_->duplicate(eventId, newName.trimmed());
            eventsCol_->refresh();
        }
    });
    dlg.exec();
    eventsCol_->refresh();
}

void MainWindow::openAuditDashboard()
{
    AuditDashboard dlg(auditManager_.get(), this);
    dlg.setOperationMode(currentUser_.role == UserRole::Operation);
    dlg.exec();
}

void MainWindow::cuePreview(const QString& filePath)
{
    auto* cue = audio_->cuePipeline();
    if (!cue) {
        LOG_WARN("[MainWindow] Pipeline CUE no disponible");
        return;
    }

    // Vacío o mismo archivo = detener
    if (filePath.isEmpty() || (!cueCurrentFile_.isEmpty() && cue->isPlaying())) {
        cueStop();
        return;
    }

    cue->play(filePath);
    cueCurrentFile_ = filePath;
    LOG_INFO("[MainWindow] CUE preescucha: {}", QFileInfo(filePath).fileName().toStdString());
}

void MainWindow::cueStop()
{
    auto* cue = audio_->cuePipeline();
    if (cue) cue->stop();
    cueCurrentFile_.clear();
}

void MainWindow::resetStreamRetryState()
{
    if (streamDurationTimer_ && streamDurationTimer_->isActive())
        streamDurationTimer_->stop();
    if (streamRetryTimer_ && streamRetryTimer_->isActive())
        streamRetryTimer_->stop();
    if (streamStabilizedTimer_ && streamStabilizedTimer_->isActive())
        streamStabilizedTimer_->stop();
    streamConnected_ = false;
    streamRetryCount_ = 0;
}

void MainWindow::handlePisadorAssign(const QString& filePath, const QString& type)
{
    if (type == "remove") {
        pisadorManager_->removeAssignment(filePath);
    } else if (type == "none") {
        pisadorManager_->setAssignment(filePath, "none");
    } else if (type == "specific") {
        QString pisadorFile = QFileDialog::getOpenFileName(this,
            tr("Seleccionar archivo de pisador"), config_->config().pisadorFolder,
            tr("Audio (*.mp3 *.ogg *.flac *.wav *.m4a)"));
        if (pisadorFile.isEmpty()) return;

        if (pisadorManager_->isExcluded(filePath)) {
            auto answer = QMessageBox::question(this, tr("Conflicto de exclusión"),
                tr("Esta pista está en una carpeta marcada como \"no pisar nunca\".\n"
                   "¿Desea asignar el pisador de todas formas?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) return;
        }
        pisadorManager_->setAssignment(filePath, "specific", pisadorFile);
    } else if (type == "random") {
        if (pisadorManager_->isExcluded(filePath)) {
            auto answer = QMessageBox::question(this, tr("Conflicto de exclusión"),
                tr("Esta pista está en una carpeta marcada como \"no pisar nunca\".\n"
                   "¿Desea asignar el pisador de todas formas?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) return;
        }
        pisadorManager_->setAssignment(filePath, "random");
    } else if (type == "time") {
        pisadorManager_->setAssignment(filePath, "time");
    }
    scheduleCol_->updateRowColors();
}

void MainWindow::openListManager()
{
    ListManager mgr(db_, metadataReader_.get(), this);
    mgr.setOperationMode(currentUser_.role == UserRole::Operation ||
                          currentView_ == ViewMode::Operation);
    connect(&mgr, &ListManager::cuePreviewRequested, this, &MainWindow::cuePreview);
    connect(&mgr, &ListManager::cueStopRequested, this, &MainWindow::cueStop);
    connect(&mgr, &ListManager::pisadorAssignRequested, this, &MainWindow::handlePisadorAssign);
    mgr.exec();
    cueStop();  // Detener preescucha al cerrar

    // Si el operador cargó una lista, insertarla en la cola
    if (mgr.wasLoaded() && !mgr.selectedTracks().isEmpty()) {
        // Limpiar cola actual y cargar la lista
        scheduleCol_->clearQueue();
        trackSelector_->clearPending();

        for (const auto& track : mgr.selectedTracks()) {
            auto meta = metadataReader_->read(track);
            QString displayName;
            if (!meta.title.isEmpty()) {
                displayName = meta.title;
                if (!meta.artist.isEmpty())
                    displayName += " — " + meta.artist;
            } else {
                displayName = QFileInfo(track).completeBaseName();
            }
            scheduleCol_->addToQueue(displayName, track, meta.durationMs);
            trackSelector_->markAsPending(track);
        }

        scheduleCol_->setCurrentTrack("", tr("Lista: ") + mgr.selectedListName());
        LOG_INFO("[MainWindow] Lista cargada en cola: {} ({} pistas)",
                 mgr.selectedListName().toStdString(),
                 mgr.selectedTracks().size());
    }
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(config_->config(), false, this);
    dlg.setBackupManager(backupManager_.get());
    dlg.setUserManager(userMgr_);
    dlg.setUserRole(currentUser_.id > 0 ? currentUser_.role : UserRole::Admin);
    if (dlg.exec() == QDialog::Accepted) {
        config_->config() = dlg.result();
        config_->save();

        // Aplicar cambios
        header_->setRadioName(config_->config().radioName);
        setWindowTitle(QString("SARA Libre — %1").arg(config_->config().radioName));
        header_->setTalkoverLevel(config_->config().talkoverLevel);
        currentMusicFolder_ = config_->config().fallbackFolder;
        trackSelector_->setNoRepeatHours(config_->config().noRepeatHours);
        trackSelector_->setNoRepeatArtistTracks(config_->config().noRepeatArtistTracks);
        scheduleEngine_->setFallbackFolder(config_->config().fallbackFolder);
        timeAnnouncer_->configure(config_->config());
        eventDispatcher_->setAdIntroFile(config_->config().adIntroFile);
        eventDispatcher_->setAdOutroFile(config_->config().adOutroFile);
        liveAssistCol_->setMusicFolder(config_->config().fallbackFolder);
        liveAssistCol_->setRadioFolder(config_->config().radioFolder);

        // Aplicar cambios de tarjeta de audio (si cambiaron)
        const auto& cfg = config_->config();
        QString mainDev = cfg.mainAudioDevice.isEmpty() ? "default" : cfg.mainAudioDevice;
        QString instantDev = cfg.instantAudioDevice.isEmpty() ? mainDev : cfg.instantAudioDevice;

        if (mainDev != audio_->deviceForPipeline(PipelineId::Main)) {
            audio_->changeDevice(PipelineId::Main, mainDev);
            audio_->changeDevice(PipelineId::MainAlt, mainDev);
            audio_->changeDevice(PipelineId::Events, mainDev);
        }
        if (instantDev != audio_->deviceForPipeline(PipelineId::InstantPlay)) {
            audio_->changeDevice(PipelineId::InstantPlay, instantDev);
        }
        QString cueDev = cfg.cueAudioDevice.isEmpty() ? mainDev : cfg.cueAudioDevice;
        if (cueDev != audio_->deviceForPipeline(PipelineId::Cue)) {
            audio_->changeDevice(PipelineId::Cue, cueDev);
        }

        // Crossfade
        crossfader_->setDurationMs(cfg.crossfadeMs);
        crossfader_->setTargetVolume(audio_->masterVolume());

        // Fade-Out
        eventDispatcher_->setFadeOutMs(cfg.fadeOutMs);
        eventDispatcher_->setFadeOutEnabled(cfg.fadeOutEnabled);

        // Grabación testigo (aplicar cambios sin detener grabación activa)
        recordingManager_->setOutputFolder(cfg.recordingFolder);
        recordingManager_->setFormat(cfg.recordingFormat);
        recordingManager_->setBitrate(cfg.recordingBitrate);
        recordingManager_->setSegmentByHour(cfg.recordingSegmentByHour);
        recordingManager_->setMainDevice(cfg.recordingDevice.isEmpty() ? cfg.mainAudioDevice : cfg.recordingDevice);

        // Detección de silencio
        int silThreshMs = cfg.silenceThresholdSecs * 1000;
        double silLevelDb = static_cast<double>(cfg.silenceLevelDb);
        audio_->mainPipeline()->setSilenceDetection(cfg.silenceDetectionEnabled, silThreshMs, silLevelDb);
        audio_->mainAltPipeline()->setSilenceDetection(cfg.silenceDetectionEnabled, silThreshMs, silLevelDb);

        // Aplicar tema e interfaz en caliente
        loadTheme();

        // VU Meter visibilidad
        scheduleCol_->vuMeter()->setVisible(cfg.vuMeterEnabled);
        eventsCol_->vuMeter()->setVisible(cfg.vuMeterEnabled);

        // Pisadores
        pisadorManager_->setEnabled(cfg.pisadorEnabled);
        pisadorManager_->setFolder(cfg.pisadorFolder);
        pisadorManager_->setFrequency(cfg.pisadorFrequency);
        pisadorManager_->setExcludedFolders(cfg.pisadorExcludedFolders);
        pisadorManager_->setDuckLevel(cfg.pisadorDuckLevel);
        pisadorManager_->setDelaySecs(cfg.pisadorDelaySecs);

        // Backup
        backupManager_->setEnabled(cfg.backupEnabled);
        backupManager_->setIntervalHours(cfg.backupIntervalHours);
        backupManager_->setMaxBackups(cfg.backupMaxCount);

        LOG_INFO("[MainWindow] Configuración actualizada");
    }
}

// ══════════════════════════════════════════════════════════
// Permisos y vistas
// ══════════════════════════════════════════════════════════

void MainWindow::applyPermissions()
{
    UserRole role = currentUser_.id > 0 ? currentUser_.role : UserRole::Operation;
    bool isOp   = (role == UserRole::Operation);
    bool isAdmin = (role == UserRole::Admin);
    bool noSession = (currentUser_.id == 0);

    // Si no hay sesión, deshabilitar todo excepto la visualización
    if (noSession) {
        // Desactivar interacción — la música sigue sonando
        scheduleCol_->setEnabled(false);
        eventsCol_->setEnabled(false);
        liveAssistCol_->setEnabled(false);
        header_->setControlsEnabled(false);
        return;
    }

    // Con sesión activa, habilitar todo y luego restringir según rol
    scheduleCol_->setEnabled(true);
    eventsCol_->setEnabled(true);
    liveAssistCol_->setEnabled(true);
    header_->setControlsEnabled(true);

    // Operación: ocultar botones de programación
    scheduleCol_->setProgrammingVisible(!isOp);
    eventsCol_->setProgrammingVisible(!isOp);
    liveAssistCol_->setOperationMode(isOp);

    // Vista toggle: solo Programación y Admin pueden cambiar
    header_->setViewToggleEnabled(!isOp);

    // Settings: botón siempre visible, pero las pestañas se filtran según rol
    // (se aplica al abrir Settings en openSettings())

    LOG_INFO("[MainWindow] Permisos aplicados: {} ({})",
             currentUser_.displayName.toStdString(),
             isAdmin ? "Admin" : isOp ? "Operación" : "Programación");
}

void MainWindow::applyViewMode(ViewMode mode)
{
    currentView_ = mode;
    bool full = (mode == ViewMode::Full);

    // En modo operación: ocultar botones de programación
    scheduleCol_->setProgrammingVisible(full);
    eventsCol_->setProgrammingVisible(full);
    liveAssistCol_->setOperationMode(!full);

    header_->setViewMode(mode);
}

void MainWindow::showUserMenu()
{
    QMenu menu(this);

    if (currentUser_.id > 0) {
        auto* profileAction = menu.addAction(tr("Editar perfil"));
        connect(profileAction, &QAction::triggered, this, [this]() {
            // Diálogo simple de edición de perfil
            bool ok;
            QString newName = QInputDialog::getText(this, tr("Editar perfil"),
                tr("Nombre visible:"), QLineEdit::Normal, currentUser_.displayName, &ok);
            if (ok && !newName.trimmed().isEmpty()) {
                userMgr_->updateProfile(currentUser_.id, newName.trimmed(), currentView_);
                currentUser_.displayName = newName.trimmed();
                header_->setCurrentUser(currentUser_.displayName, currentUser_.role);
            }
        });

        auto* changePinAction = menu.addAction(tr("Cambiar PIN"));
        connect(changePinAction, &QAction::triggered, this, [this]() {
            QString oldPin = QInputDialog::getText(this, tr("Cambiar PIN"),
                tr("PIN actual:"), QLineEdit::Password);
            if (oldPin.isEmpty()) return;

            auto verify = userMgr_->authenticate(currentUser_.username, oldPin);
            if (!verify) {
                QMessageBox::warning(this, tr("Error"), tr("PIN actual incorrecto"));
                return;
            }

            QString newPin = QInputDialog::getText(this, tr("Cambiar PIN"),
                tr("Nuevo PIN:"), QLineEdit::Password);
            if (!newPin.isEmpty()) {
                userMgr_->changePin(currentUser_.id, newPin);
                QMessageBox::information(this, tr("PIN actualizado"),
                    tr("Su PIN fue cambiado correctamente."));
            }
        });

        auto* recoveryAction = menu.addAction(tr("Descargar archivo de recuperación"));
        connect(recoveryAction, &QAction::triggered, this, [this]() {
            QString token = userMgr_->generateRecoveryToken(currentUser_.id);
            QString path = QFileDialog::getSaveFileName(this,
                tr("Guardar archivo de recuperación"),
                QDir::homePath() + QString("/sara_recovery_%1.key").arg(currentUser_.username),
                tr("Archivo de recuperación (*.key)"));
            if (!path.isEmpty()) {
                QFile file(path);
                if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&file);
                    out << token;
                    file.close();
                    QMessageBox::information(this, tr("Guardado"),
                        tr("Archivo de recuperación guardado en:\n%1").arg(path));
                }
            }
        });

        menu.addSeparator();

        auto* switchAction = menu.addAction(tr("Cambiar de usuario/a"));
        connect(switchAction, &QAction::triggered, this, [this]() {
            LoginDialog dlg(userMgr_, this);
            if (dlg.exec() == QDialog::Accepted) {
                currentUser_ = dlg.authenticatedUser();
                header_->setCurrentUser(currentUser_.displayName, currentUser_.role);
                currentView_ = currentUser_.defaultView;
                applyPermissions();
                applyViewMode(currentView_);
            }
        });

        auto* logoutAction = menu.addAction(tr("Cerrar sesión"));
        connect(logoutAction, &QAction::triggered, this, [this]() {
            currentUser_ = {};
            header_->setCurrentUser("", UserRole::Operation);
            currentView_ = ViewMode::Operation;
            applyPermissions();
            applyViewMode(currentView_);
        });
    } else {
        auto* loginAction = menu.addAction(tr("Iniciar sesión"));
        connect(loginAction, &QAction::triggered, this, [this]() {
            LoginDialog dlg(userMgr_, this);
            if (dlg.exec() == QDialog::Accepted) {
                currentUser_ = dlg.authenticatedUser();
                header_->setCurrentUser(currentUser_.displayName, currentUser_.role);
                currentView_ = currentUser_.defaultView;
                applyPermissions();
                applyViewMode(currentView_);
            }
        });
    }

    menu.exec(QCursor::pos());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    auto result = QMessageBox::question(
        this, tr("Cerrar SARA Libre"),
        tr("¿Está seguro de que desea cerrar SARA Libre?\n\nSi hay una emisión en curso, la radio quedará en silencio."),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (result != QMessageBox::Yes) {
        event->ignore();
        return;
    }

    // Guardar configuración y detener
    if (recordingManager_ && recordingManager_->isRecording()) {
        recordingManager_->stopRecording();
    }
    config_->save();
    stateManager_->stop();
    stateManager_->clearState();  // Cierre limpio: no recuperar al próximo inicio
    scheduleEngine_->stop();
    eventDispatcher_->stop();
    audio_->mainPipeline()->stop();
    audio_->mainAltPipeline()->stop();
    audio_->eventsPipeline()->stop();
    LOG_INFO("[MainWindow] Cerrando SARA Libre");
    event->accept();
}

void MainWindow::applyEqCompressorConfig()
{
    const auto& cfg = config_->config();

    if (cfg.eqEnabled) {
        audio_->setEqBands(cfg.eqBands);
    }

    if (cfg.compressorEnabled) {
        audio_->setCompressorEnabled(true);
        audio_->setCompressorThreshold(cfg.compressorThresholdDb);
        audio_->setCompressorRatio(cfg.compressorRatio);
    } else {
        audio_->setCompressorEnabled(false);
    }

    LOG_DEBUG("[MainWindow] EQ: {}, Compresor: {}",
              cfg.eqEnabled ? "ON" : "OFF",
              cfg.compressorEnabled ? "ON" : "OFF");
}

void MainWindow::openEqCompressor()
{
    EqCompressorDialog dlg(config_->config(), this);

    connect(&dlg, &EqCompressorDialog::parametersChanged, this, [&dlg, this]() {
        // Aplicar en tiempo real
        if (dlg.eqEnabled()) {
            audio_->setEqBands(dlg.eqBands());
        } else {
            audio_->setEqBands({0,0,0,0,0,0,0,0,0,0});
        }
        if (dlg.compressorEnabled()) {
            audio_->setCompressorEnabled(true);
            audio_->setCompressorThreshold(dlg.compressorThresholdDb());
            audio_->setCompressorRatio(dlg.compressorRatio());
        } else {
            audio_->setCompressorEnabled(false);
        }
    });

    if (dlg.exec() == QDialog::Accepted) {
        // Guardar en config
        config_->config().eqEnabled = dlg.eqEnabled();
        config_->config().eqBands = dlg.eqBands();
        config_->config().eqPresetName = dlg.currentPresetName();
        config_->config().compressorEnabled = dlg.compressorEnabled();
        config_->config().compressorThresholdDb = dlg.compressorThresholdDb();
        config_->config().compressorRatio = dlg.compressorRatio();
        config_->save();
    } else {
        // Si canceló, restaurar valores previos
        applyEqCompressorConfig();
    }
}

void MainWindow::updateNowPlayingFile(const QString& title, const QString& source)
{
    const auto& cfg = config_->config();
    if (!cfg.nowPlayingEnabled || cfg.nowPlayingFile.isEmpty()) return;

    QFile file(cfg.nowPlayingFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        LOG_WARN("[NowPlaying] No se pudo escribir: {}", cfg.nowPlayingFile.toStdString());
        return;
    }

    QTextStream out(&file);
    // Qt6 defaults to UTF-8

    QString text = title.trimmed();
    if (text.isEmpty()) {
        text = tr("Sin información");
    }

    // Formato: Título - Fuente (compatible con Butt)
    if (!source.isEmpty() && source != tr("Alternativa")) {
        out << text << " - " << source;
    } else {
        out << text;
    }
    out << "\n";
    file.close();
}

} // namespace sara
