#ifndef SARA_UI_MAIN_WINDOW_H
#define SARA_UI_MAIN_WINDOW_H

#include "core/types.h"
#include "data/user_manager.h"
#include <QMainWindow>
#include <QTimer>
#include <memory>
#include <functional>

namespace sara {

class AudioEngine;
class Database;
class ConfigManager;
class TrackSelector;
class MetadataReader;
class ScheduleRepository;
class ScheduleEngine;
class TimeAnnouncer;
class EventRepository;
class EventDispatcher;
class InstantPlayRepo;
class AuditManager;
class Crossfader;
class StateManager;
class PisadorManager;
class BackupManager;
class StreamPresetRepo;
class RecordingManager;
class HeaderWidget;
class ScheduleColumn;
class EventsColumn;
class LiveAssistColumn;

/**
 * Ventana principal de SARA Libre.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AudioEngine* audio, Database* db,
                        ConfigManager* config, UserManager* userMgr,
                        const UserInfo& currentUser,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onPlay();
    void onPause();
    void onStop();
    void onVolumeChanged(int percent);
    void onAutoModeToggled(bool on);
    void onMainTrackFinished();
    void openSettings();

private:
    void setupUI();
    void loadTheme();
    void playNextTrack();
    void performFadeOut(std::function<void()> onComplete);
    void fillQueue();
    void openScheduleManager();
    void openWeeklyGrid();
    void openListManager();
    void openNewEvent();
    void openEditEvent(const QString& eventId);
    void openInverseAssign();
    void openEventSummary();
    void openAuditDashboard();
    void playTimeAnnouncement();
    void cuePreview(const QString& filePath);
    void handlePisadorAssign(const QString& filePath, const QString& type);
    void cueStop();
    // Detiene cualquier timer de reintento/duración/estabilización de
    // stream y resetea los flags. Llamado al detener stream (Stop manual,
    // closeEvent, paso a una pista que no es stream, agotamiento de
    // reintentos). Mantiene la lógica en un solo lugar.
    void resetStreamRetryState();
    void closeEvent(QCloseEvent* event) override;

    // Componentes de UI
    HeaderWidget*     header_;
    ScheduleColumn*   scheduleCol_;
    EventsColumn*     eventsCol_;
    LiveAssistColumn* liveAssistCol_;

    // Servicios (no owned)
    AudioEngine*   audio_;
    Database*      db_;
    ConfigManager* config_;

    // Owned
    std::unique_ptr<TrackSelector> trackSelector_;
    std::unique_ptr<MetadataReader> metadataReader_;
    std::unique_ptr<ScheduleRepository> scheduleRepo_;
    std::unique_ptr<ScheduleEngine> scheduleEngine_;
    std::unique_ptr<TimeAnnouncer> timeAnnouncer_;
    std::unique_ptr<EventRepository> eventRepo_;
    std::unique_ptr<EventDispatcher> eventDispatcher_;
    std::unique_ptr<InstantPlayRepo> instantRepo_;
    std::unique_ptr<AuditManager> auditManager_;
    std::unique_ptr<Crossfader> crossfader_;
    std::unique_ptr<StateManager> stateManager_;
    std::unique_ptr<PisadorManager> pisadorManager_;
    std::unique_ptr<BackupManager> backupManager_;
    std::unique_ptr<StreamPresetRepo> streamRepo_;
    std::unique_ptr<RecordingManager> recordingManager_;

    QTimer* fadeMonitor_ = nullptr;
    QTimer* streamDurationTimer_ = nullptr;  // Timer para limitar duración de streams
    QTimer* streamRetryTimer_ = nullptr;     // Timer para reintentos de stream
    QTimer* streamStabilizedTimer_ = nullptr; // 5s post-play → marca conexión estabilizada
    QTimer* fadeOutTimer_ = nullptr;         // Timer para fade-out manual (defecto 1.9: evitar acumulación)
    int     streamRetryCount_ = 0;
    QString streamRetryUrl_;
    QString streamRetryName_;
    // True una vez que el stream se considera "estabilizado" (5s sin
    // fallos desde el play). Cuando un fallo ocurre con este flag en
    // false, cuenta contra streamRetryCount_; cuando ocurre con flag en
    // true, es desconexión en medio y se reinicia el contador (igual que
    // hace EventDispatcher para sus streams).
    bool    streamConnected_ = false;
    static constexpr int STREAM_MAX_RETRIES = 3;
    static constexpr int STREAM_RETRY_DELAY_MS = 3000;
    static constexpr int STREAM_STABILIZE_MS = 5000;
    bool fadeOutTriggered_ = false;
    bool crossfading_ = false;  // True durante crossfade solapado
    // Defecto 1.2: durante secuencia de locución de hora, onMainTrackFinished
    // debe encadenar archivos en lugar de avanzar la cola normal. Antes se
    // resolvía con disconnect/reconnect, frágil ante onStop() en medio.
    bool inAnnouncementSequence_ = false;
    QString cueCurrentFile_;  // Archivo en preescucha CUE actual

    bool autoMode_ = true;
    bool stopAtEnd_ = false;
    bool loopMode_ = false;
    QString currentMusicFolder_;
    QStringList announcementQueue_;

    // User management
    UserManager*  userMgr_ = nullptr;
    UserInfo      currentUser_;
    ViewMode      currentView_ = ViewMode::Full;

    void applyPermissions();
    void applyViewMode(ViewMode mode);
    void showUserMenu();
    void openEqCompressor();
    void applyEqCompressorConfig();
    void updateNowPlayingFile(const QString& title, const QString& source);
};

} // namespace sara

#endif
