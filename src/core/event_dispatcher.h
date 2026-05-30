#ifndef SARA_CORE_EVENT_DISPATCHER_H
#define SARA_CORE_EVENT_DISPATCHER_H

#include "core/types.h"
#include <QObject>
#include <QTimer>
#include <QTime>
#include <QDate>
#include <QSet>
#include <QQueue>

namespace sara {

class EventRepository;
class AudioEngine;
class Crossfader;
class TrackSelector;
class MetadataReader;
class TimeAnnouncer;
class AuditManager;

/**
 * Dispatcher de Publicidad/Eventos.
 *
 * Cada 15 segundos verifica si hay un evento programado para este minuto.
 * Cuando lo hay:
 *   - RETARDADO: espera señal trackFinished del pipeline Main, luego reproduce
 *   - INMEDIATO: hace crossfade del Main al Events pipeline
 *
 * Al finalizar todos los archivos del evento:
 *   - Retorna al pipeline Main (crossfade inverso si fue inmediato)
 *   - Emite eventFinished()
 *
 * Prioridad 1-10 resuelve conflictos cuando hay múltiples eventos
 * programados a la misma hora.
 */
class EventDispatcher : public QObject
{
    Q_OBJECT

public:
    explicit EventDispatcher(QObject* parent = nullptr);
    ~EventDispatcher() override;

    // Dependencias
    void setEventRepository(EventRepository* repo) { repo_ = repo; }
    void setAudioEngine(AudioEngine* engine) { engine_ = engine; }
    void setCrossfader(Crossfader* fader) { fader_ = fader; }
    void setFadeOutMs(int ms) { fadeOutMs_ = ms; }
    void setFadeOutEnabled(bool on) { fadeOutEnabled_ = on; }
    void setTrackSelector(TrackSelector* selector) { selector_ = selector; }
    void setMetadataReader(MetadataReader* reader) { reader_ = reader; }
    void setTimeAnnouncer(TimeAnnouncer* announcer) { announcer_ = announcer; }
    void setAuditManager(AuditManager* audit) { audit_ = audit; }
    void setAdIntroFile(const QString& path) { adIntroFile_ = path; }
    void setAdOutroFile(const QString& path) { adOutroFile_ = path; }

    /// Iniciar el dispatcher
    void start();

    /// Detener
    void stop();

    bool isRunning() const { return running_; }

    /// ¿Hay un evento reproduciéndose ahora?
    bool isEventPlaying() const { return eventPlaying_; }

    /// ¿Está esperando que termine la pista actual para lanzar un evento retardado?
    bool isWaitingForMain() const { return waitingForMainToFinish_; }

    /// Nombre del evento en reproducción
    QString currentEventName() const { return currentEventName_; }

    /// Reproducir un evento manualmente (para modo manual)
    void playEventManually(const QString& eventId);

    /// Notificar que la pista principal terminó (llamado por MainWindow)
    /// Retorna true si el dispatcher tomó el control
    bool notifyMainTrackFinished();

    /// Forzar detención del evento en curso (Stop general)
    void forceStopEvent();

signals:
    /// Un evento comenzó a reproducirse
    void eventStarted(const QString& eventName);

    /// Un elemento individual comenzó a reproducirse
    void elementPlaying(const QString& elementName, const QString& eventName);

    /// Un stream comenzó como parte de un evento (durationMs = 0 si sin límite)
    void streamElementStarted(int64_t durationMs);

    /// Un evento terminó
    void eventFinished(const QString& eventName);

    /// Se necesita pausar el pipeline Main (para evento retardado)
    void requestMainPause();

    /// Se puede reanudar el pipeline Main
    void requestMainResume();

    /// Un evento venció (no se emitió)
    void eventExpired();

private slots:
    void checkEvents();
    void onEventTrackFinished();

private:
    void triggerEvent(const Event& event);
    void triggerEvent(const Event& event, const QTime& scheduledTime);
    void recoverPersistentEvents();
    void playNextEventElement();
    QString resolveEventElement(const EventElement& element);
    void finishEvent();
    void attemptStreamPlay(const EventElement& element, const QString& url);
    // Punto único de gestión de fallo de stream. Centraliza la lógica que
    // antes estaba replicada en tres handlers distintos (error inicial,
    // error post-estabilización, onEventTrackFinished). Diseñado para ser
    // idempotente: si se llama varias veces casi a la vez (porque GStreamer
    // emite múltiples errores para el mismo fallo de conexión, o porque
    // errorOccurred y trackFinished llegan juntos), las llamadas
    // subsecuentes son ignoradas.
    void handleStreamFailure(const QString& reason);
    QString currentDayName() const;

    EventRepository* repo_ = nullptr;
    AudioEngine*     engine_ = nullptr;
    Crossfader*      fader_ = nullptr;
    int              fadeOutMs_ = 500;
    bool             fadeOutEnabled_ = true;
    TrackSelector*   selector_ = nullptr;
    MetadataReader*  reader_ = nullptr;
    TimeAnnouncer*   announcer_ = nullptr;
    AuditManager*    audit_ = nullptr;
    QString          adIntroFile_;
    QString          adOutroFile_;

    bool running_ = false;
    bool eventPlaying_ = false;
    bool waitingForMainToFinish_ = false;

    // Evento actual en reproducción
    QString currentEventId_;
    QString currentEventName_;
    QVector<EventElement> currentElements_;
    int currentElementIndex_ = 0;
    bool currentImmediate_ = false;
    bool currentUseAdAnnounce_ = false;

    // Para no disparar el mismo evento dos veces en el mismo minuto
    QSet<QString> triggeredThisMinute_;
    QTime lastCheckMinute_;

    // Eventos ya emitidos hoy (para no repetir persistentes en recovery)
    QSet<QString> playedToday_;  // key: eventId@HH:mm
    QDate playedTrackingDate_;

    // Eventos que ya se registraron como vencidos hoy
    QSet<QString> expiredToday_;

    // Cola de eventos pendientes con su hora programada
    struct QueuedEvent {
        Event event;
        QTime scheduledTime;
    };
    QQueue<QueuedEvent> pendingEventQueue_;
    QTime currentEventScheduledTime_;  // Hora programada del evento en curso

    QTimer* checkTimer_ = nullptr;
    QTimer* streamTimer_ = nullptr;      // Timer para limitar duración de streams
    QTimer* streamRetryTimer_ = nullptr;  // Timer para reintentos de conexión
    int     streamRetryCount_ = 0;        // Contador de reintentos (max 3)
    QString streamRetryUrl_;              // URL del stream a reintentar
    EventElement streamRetryElement_;     // Elemento stream para reintentar

    // Flag de reentrada para gestión de fallo de stream.
    // Activo desde el momento en que handleStreamFailure() empieza a
    // procesar un fallo, hasta que se programa el próximo intento (o se
    // decide saltar el elemento). Sirve para que las múltiples emisiones
    // de errorOccurred + trackFinished que GStreamer puede generar para
    // un único fallo de conexión HTTP no se traduzcan en múltiples
    // reconexiones en cascada.
    bool    streamReconnecting_ = false;

    // True una vez que el stream se considera "estabilizado" (5s sin
    // fallos desde el play()). Cuando un fallo ocurre con este flag en
    // false, se cuenta contra streamRetryCount_ (límite a 3). Cuando
    // ocurre con el flag en true, es una desconexión en medio de
    // transmisión y se reinicia el contador.
    bool    streamConnected_ = false;
    QTimer* streamStabilizedTimer_ = nullptr;  // 5s post-play → marca estabilizado

    static constexpr int STREAM_MAX_RETRIES = 3;
    static constexpr int STREAM_RETRY_DELAY_MS = 3000;  // 3 segundos entre reintentos
    static constexpr int STREAM_STABILIZE_MS = 5000;    // ms para considerar estable
};

} // namespace sara

#endif
