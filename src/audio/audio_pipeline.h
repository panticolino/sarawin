#ifndef SARA_AUDIO_PIPELINE_H
#define SARA_AUDIO_PIPELINE_H

#include "core/types.h"
#include <QObject>
#include <QString>
#include <QTimer>
#include <QDateTime>
#include <gst/gst.h>
#include <memory>

namespace sara {

/**
 * Pipeline de audio individual usando GStreamer.
 *
 * Estructura del pipeline:
 *   uridecodebin → audioconvert → audioresample → volume → audiosink
 *
 * Soporta:
 *  - Reproducción de archivos locales y streams HTTP
 *  - Control de volumen
 *  - Posición y duración
 *  - Señal about-to-finish para gapless playback
 *  - Señales de metadata (tags)
 *  - Recuperación ante errores (archivo corrupto)
 *  - Watchdogs múltiples:
 *      · Silencio prolongado (audio bajo umbral durante N seg)
 *      · Sink no responde (no llegan mensajes level tras play)
 *      · Pipeline estancado (posición no avanza pese a estar Playing)
 */
class AudioPipeline : public QObject
{
    Q_OBJECT

public:
    explicit AudioPipeline(PipelineId id, QObject* parent = nullptr);
    ~AudioPipeline() override;

    // No copiable
    AudioPipeline(const AudioPipeline&) = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;

    /// Inicializar el pipeline de GStreamer. Retorna false si falla.
    bool initialize(const QString& audioDevice = "default");

    /// Limpiar el pipeline para poder reinicializar con otro dispositivo
    void cleanup();

    /// Reproducir un archivo o URL
    bool play(const QString& uri);

    /// Pausar
    void pause();

    /// Reanudar
    void resume();

    /// Detener
    void stop();

    /// Saltar a una posición (en milisegundos)
    void seek(int64_t positionMs);

    /// Ajustar volumen (0.0 a 1.0)
    void setVolume(double volume);
    double volume() const;

    /// Estado actual
    PlaybackState state() const { return state_; }
    bool isPlaying() const { return state_ == PlaybackState::Playing; }

    /// Posición actual en ms
    int64_t positionMs() const;

    /// Duración total en ms
    int64_t durationMs() const;

    /// URI actualmente cargado
    QString currentUri() const { return currentUri_; }

    /// Identificador del pipeline
    PipelineId id() const { return id_; }

    /// Configurar detección de silencio
    void setSilenceDetection(bool enabled, int thresholdMs, double levelDb);

    /// Habilitar/deshabilitar ReplayGain (requiere reinicializar pipeline)
    void setReplayGainEnabled(bool enabled) { replayGainEnabled_ = enabled; }

    /// Habilitar procesamiento de audio (EQ + Compresor, llamar ANTES de initialize)
    void setProcessingEnabled(bool enabled) { processingEnabled_ = enabled; }

    /// Ajustar bandas del ecualizador (ganancia en dB, -24 a +12)
    void setEqBand(int band, double gainDb);
    void setEqBands(const QVector<double>& gains);

    /// Ajustar compresor
    void setCompressorEnabled(bool enabled);
    void setCompressorThreshold(double db);
    void setCompressorRatio(double ratio);

signals:
    /// Cambio de estado
    void stateChanged(sara::PlaybackState newState);

    /// La pista actual terminó (natural end-of-stream)
    void trackFinished();

    /// Posición actualizada (emitida ~4 veces por segundo)
    void positionUpdated(int64_t positionMs, int64_t durationMs);

    /// Se leyeron tags de metadata
    void metadataReceived(const sara::TrackMetadata& meta);

    /// Error en reproducción
    void errorOccurred(const QString& message);

    /// Silencio prolongado detectado (incluye sink no responde, posición estancada)
    void silenceDetected();

    /// Niveles de audio actualizados (POST-fader, lo que va al aire)
    /// para VU meter. Defecto 1.7: la medición refleja el efecto del
    /// crossfade y del fader maestro.
    void levelUpdated(double leftDb, double rightDb);

private:
    // Callbacks de GStreamer (funciones estáticas que llaman a métodos de instancia)
    static void onPadAdded(GstElement* src, GstPad* newPad, gpointer data);
    static gboolean onBusMessage(GstBus* bus, GstMessage* msg, gpointer data);

    // Procesamiento de mensajes del bus
    void handleEos();
    void handleError(GstMessage* msg);
    void handleStateChange(GstMessage* msg);
    void handleTag(GstMessage* msg);
    // handleLevel ahora discrimina entre level pre-fader (watchdog de
    // silencio + compresor) y level post-fader (VU meter al aire).
    // Defecto 1.7.
    void handleLevel(GstMessage* msg, bool isPostFader);

    // Timer de posición (incluye los tres watchdogs)
    void startPositionTimer();
    void stopPositionTimer();
    void positionTick();

    // Convierte ruta local a URI de GStreamer
    static QString pathToUri(const QString& path);

    PipelineId    id_;
    PlaybackState state_ = PlaybackState::Stopped;
    QString       currentUri_;
    double        volume_ = 0.75;

    // Elementos de GStreamer
    GstElement* pipeline_ = nullptr;
    GstElement* volumeElement_ = nullptr;
    GstElement* levelElement_ = nullptr;       // PRE-fader: watchdog silencio + compresor
    GstElement* levelPostElement_ = nullptr;   // POST-fader: VU meter al aire (defecto 1.7)

    // Generación de "vida" del pipeline. Cada vez que play() arranca una
    // nueva reproducción, se incrementa este contador. Los lambdas diferidos
    // de handleEos / handleError capturan el valor que tenían al programarse;
    // si el valor actual difiere cuando el lambda finalmente se ejecuta,
    // significa que entre tanto se llamó play() de nuevo y por lo tanto NO
    // deben tocar el pipeline (set_state(NULL) ni emit trackFinished).
    //
    // Este es el fix al bug observado en log fase 8c: tras un fallo de stream,
    // el handleError programaba un singleShot(200) con set_state(NULL); en
    // los 200ms intermedios, MainWindow ya había llamado play() de la
    // siguiente canción, y el set_state(NULL) diferido la mataba.
    quint64 playGeneration_ = 0;

    // Flag de deduplicación de teardown. Cuando GStreamer emite Error
    // seguido de EOS para un mismo fallo (típico en streams HTTP que se
    // cortan), handleError() y handleEos() se ejecutarían los dos y
    // programarían dos lambdas de teardown idénticos. El segundo emit
    // trackFinished generaba el mensaje "tardío del deck standby, ignorando"
    // visto en el log fase 8c. Con este flag, solo el primer handler que
    // entre programa el teardown.
    bool teardownPending_ = false;

    // Timer Qt para posición + watchdogs
    QTimer* positionTimer_ = nullptr;

    // Bus de GStreamer + timer que lo vacía nosotros mismos. Necesario porque
    // gst_bus_add_watch() depende del main loop de GLib, que Qt NO ejecuta en
    // Windows (sí en Linux). Ver initialize() en el .cpp.
    GstBus* bus_ = nullptr;
    QTimer* busPollTimer_ = nullptr;

    // ── Detección de silencio (umbral de nivel sostenido) ──
    bool    silenceDetectionEnabled_ = true;
    int     silenceConsecutiveMs_ = 0;
    int     silenceThresholdMs_ = 15000;
    double  silenceLevelDb_ = -50.0;
    int64_t lastAudioTimestamp_ = 0;     // Último ms con audio por encima del umbral
    int64_t lastLevelReceivedMs_ = 0;    // Último ms que llegó un mensaje level (cualquier nivel)
    int64_t playbackStartMs_ = 0;        // Timestamp de inicio de reproducción (ms epoch)
    bool    silenceWatchdogFired_ = false; // Evitar múltiples disparos

    // ── Watchdog de sink inicial (defecto 1.8) ──
    // Si tras N ms desde play() NO se recibió ningún mensaje level
    // y el estado es Playing, el sink probablemente está colgado
    // (USB desconectado, pulseaudio caído, etc.).
    static constexpr int SINK_INIT_TIMEOUT_MS = 5000;
    bool    sinkInitWatchdogFired_ = false;

    // ── Watchdog de posición estancada (defecto 1.12) ──
    // Si la posición no avanza durante M ms pese a estar Playing,
    // GStreamer se quedó colgado (clásico de USB suspendido).
    static constexpr int POSITION_STALL_THRESHOLD_MS = 4000;
    int64_t lastObservedPositionMs_ = -1;     // -1 = no leída aún
    int64_t lastPositionChangeMs_ = 0;        // ms epoch del último avance
    bool    stallWatchdogFired_ = false;

    // Duración cacheada (GStreamer retorna 0 después de EOS)
    mutable int64_t cachedDurationMs_ = 0;

    // ReplayGain
    bool replayGainEnabled_ = false;

    // Procesamiento de audio (EQ + Compresor)
    bool processingEnabled_ = false;
    GstElement* equalizerElement_ = nullptr;
    GstElement* compGainElement_ = nullptr;   // Volume element para gain reduction del compresor
    bool compressorActive_ = false;
    double compThresholdDb_ = -10.0;
    double compRatio_ = 4.0;
    double currentGainReduction_ = 0.0;  // dB de reducción actual (smoothed)

    bool initialized_ = false;
};

} // namespace sara

#endif // SARA_AUDIO_PIPELINE_H
