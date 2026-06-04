#include "audio/audio_pipeline.h"
#include "util/logger.h"

#include <QTimer>
#include <QFileInfo>
#include <QUrl>
#include <gst/gst.h>
#include <cmath>

namespace sara {

// ── Constructor / Destructor ─────────────────────────────

AudioPipeline::AudioPipeline(PipelineId id, QObject* parent)
    : QObject(parent)
    , id_(id)
{
}

AudioPipeline::~AudioPipeline()
{
    cleanup();
    LOG_DEBUG("[AudioPipeline:{}] Destruido", static_cast<int>(id_));
}

void AudioPipeline::cleanup()
{
    stop();
    // Detener el sondeo del bus y soltar la referencia antes de destruir el pipeline
    if (busPollTimer_) {
        busPollTimer_->stop();
        busPollTimer_->deleteLater();
        busPollTimer_ = nullptr;
    }
    if (bus_) {
        gst_object_unref(bus_);
        bus_ = nullptr;
    }
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    // volumeElement_ and levelElement_ are owned by the pipeline bin, no unref needed
    volumeElement_ = nullptr;
    levelElement_ = nullptr;
    levelPostElement_ = nullptr;
    equalizerElement_ = nullptr;
    compGainElement_ = nullptr;
    silenceConsecutiveMs_ = 0;
    cachedDurationMs_ = 0;
    lastAudioTimestamp_ = 0;
    lastLevelReceivedMs_ = 0;
    silenceWatchdogFired_ = false;
    sinkInitWatchdogFired_ = false;
    lastObservedPositionMs_ = -1;
    lastPositionChangeMs_ = 0;
    stallWatchdogFired_ = false;
    initialized_ = false;
}

// ── Inicialización ───────────────────────────────────────

bool AudioPipeline::initialize(const QString& audioDevice)
{
    if (initialized_) return true;

    // Usar playbin: maneja decodificación, demux, etc. automáticamente
    QString name = QString("pipeline_%1").arg(static_cast<int>(id_));
    pipeline_ = gst_element_factory_make("playbin", name.toUtf8().constData());

    if (!pipeline_) {
        LOG_ERROR("[AudioPipeline:{}] No se pudo crear playbin", static_cast<int>(id_));
        return false;
    }

    // Configurar audio sink con dispositivo específico.
    // Usamos un bin personalizado: audioconvert → volume → sink.
    // El elemento volume interno NO afecta PulseAudio (solo playbin->volume lo
    // hace), así fade-in/fade-out no quedan recordados por el servidor de
    // sonido entre reproducciones.
    //
    // Selección de sink:
    //   - Siempre `pulsesink` como primera opción. En sistemas con PipeWire,
    //     el shim `pipewire-pulse` provee la API PulseAudio y los nombres de
    //     dispositivo coinciden con los que devuelve `pactl list sinks`.
    //     Esto garantiza que cuando el operador elige una tarjeta en Settings,
    //     el `device=...` que mandamos a `pulsesink` referencia exactamente
    //     la tarjeta que vio el usuario.
    //   - Fallback a `autoaudiosink` / `alsasink` si el sistema no tiene ni
    //     PulseAudio ni PipeWire-pulse instalados (raro en distros target,
    //     pero red de seguridad).
    //
    // NOTA HISTÓRICA: en fase 9 se intentó preferir `pipewiresink` cuando
    // PipeWire estaba activo, con la intención de reducir latencia. La idea
    // se revirtió porque pipewiresink usa nombres de dispositivo nativos de
    // PipeWire (números enteros, paths) que NO coinciden con los nombres
    // PulseAudio-style que devuelve pactl. Resultado: el operador elegía una
    // tarjeta y el audio salía por otra, o cambiar tarjeta no surtía efecto.
    // Además se observó SIGSEGV al abrir Settings durante reproducción en
    // Debian 12 (interacción mala entre GstDeviceMonitor y pipewiresink ya
    // creado en el playbin).
    // El shim pulse-pipewire provee toda la funcionalidad necesaria con la
    // misma robustez del código previo a fase 9.
    GstElement* actualSink = nullptr;

#ifdef _WIN32
    // ── Windows: salida por WASAPI (sistema de audio nativo de Windows) ──
    // Fallback a directsoundsink y, por último, autoaudiosink (que deja a
    // GStreamer elegir el mejor sink disponible).
    if (audioDevice.isEmpty() || audioDevice == "default") {
        actualSink = gst_element_factory_make("wasapisink", "actual_sink");
        if (!actualSink)
            actualSink = gst_element_factory_make("directsoundsink", "actual_sink");
        if (!actualSink)
            actualSink = gst_element_factory_make("autoaudiosink", "actual_sink");
        if (actualSink)
            LOG_INFO("[AudioPipeline:{}] salida de audio Windows (default)",
                     static_cast<int>(id_));
    } else {
        actualSink = gst_element_factory_make("wasapisink", "actual_sink");
        if (actualSink) {
            g_object_set(actualSink, "device",
                         audioDevice.toUtf8().constData(), nullptr);
            LOG_INFO("[AudioPipeline:{}] wasapisink configurado con device='{}'",
                     static_cast<int>(id_), audioDevice.toStdString());
        } else {
            actualSink = gst_element_factory_make("autoaudiosink", "actual_sink");
            if (actualSink)
                LOG_WARN("[AudioPipeline:{}] wasapisink no disponible, "
                         "usando autoaudiosink como fallback",
                         static_cast<int>(id_));
        }
    }
#else
    if (audioDevice.isEmpty() || audioDevice == "default") {
        // Sin dispositivo específico: PulseAudio (o pipewire-pulse) elige
        // el default del sistema.
        actualSink = gst_element_factory_make("pulsesink", "actual_sink");
        if (!actualSink) {
            // pulsesink no disponible: fallback a autoaudiosink (GStreamer
            // elige el mejor sink que encuentre — alsasink, osssink, etc.)
            actualSink = gst_element_factory_make("autoaudiosink", "actual_sink");
            if (actualSink) {
                LOG_WARN("[AudioPipeline:{}] pulsesink no disponible, "
                         "usando autoaudiosink como fallback",
                         static_cast<int>(id_));
            }
        }
    } else {
        // Dispositivo específico: pulsesink lo acepta tal cual viene de
        // pactl. En sistemas PipeWire, pipewire-pulse expone los mismos
        // nombres por compatibilidad.
        actualSink = gst_element_factory_make("pulsesink", "actual_sink");
        if (actualSink) {
            g_object_set(actualSink, "device",
                         audioDevice.toUtf8().constData(), nullptr);
            LOG_INFO("[AudioPipeline:{}] pulsesink configurado con device='{}'",
                     static_cast<int>(id_), audioDevice.toStdString());
        } else {
            // Sin PulseAudio ni shim pulse-pipewire: alsasink directo
            actualSink = gst_element_factory_make("alsasink", "actual_sink");
            if (actualSink) {
                g_object_set(actualSink, "device",
                             audioDevice.toUtf8().constData(), nullptr);
                LOG_INFO("[AudioPipeline:{}] alsasink configurado con device='{}'",
                         static_cast<int>(id_), audioDevice.toStdString());
            }
        }
    }
#endif // _WIN32

    // Crear bin: audioconvert → [rgvolume] → level → volume → sink
    // rgvolume: normalización automática por ReplayGain tags
    // equalizer: ecualizador de 10 bandas
    // audiodynamic: compresor/limiter
    // level: detecta silencio prolongado para auto-skip + VU meter
    GstElement* audioBin = gst_bin_new("audio_bin");
    GstElement* convert = gst_element_factory_make("audioconvert", "bin_convert");
    GstElement* rgvolume = replayGainEnabled_
        ? gst_element_factory_make("rgvolume", "bin_rgvolume") : nullptr;

    // EQ y compresor (solo si processingEnabled_)
    if (processingEnabled_) {
        equalizerElement_ = gst_element_factory_make("equalizer-10bands", "bin_eq");
        compGainElement_ = gst_element_factory_make("volume", "bin_comp_gain");
        if (equalizerElement_) {
            for (int i = 0; i < 10; ++i) {
                QString prop = QString("band%1").arg(i);
                g_object_set(equalizerElement_, prop.toUtf8().constData(), 0.0, nullptr);
            }
        }
        if (compGainElement_) {
            // Compresor apagado: ganancia unitaria (1.0 = 0 dB)
            g_object_set(compGainElement_, "volume", 1.0, nullptr);
        }
    }

    levelElement_ = gst_element_factory_make("level", "bin_level");
    levelPostElement_ = gst_element_factory_make("level", "bin_level_post");
    volumeElement_ = gst_element_factory_make("volume", "bin_volume");

    if (audioBin && convert && levelElement_ && volumeElement_ && actualSink) {
        // Configurar level: enviar mensajes cada 100ms para VU meter responsive
        g_object_set(levelElement_, "post-messages", TRUE, nullptr);
        g_object_set(levelElement_, "interval", (guint64)100000000, nullptr);

        // Defecto 1.7: el level POST-fader mide lo que efectivamente sale
        // por la tarjeta (refleja crossfade y volumen master). Es el que
        // usamos para el VU meter al aire. El level PRE-fader sigue siendo
        // la fuente del watchdog de silencio (detecta archivos rotos
        // aunque el operador haga mute manual o estemos en crossfade).
        if (levelPostElement_) {
            g_object_set(levelPostElement_, "post-messages", TRUE, nullptr);
            g_object_set(levelPostElement_, "interval", (guint64)100000000, nullptr);
        }

        // CRÍTICO: reenviar mensajes de elementos internos al bus del playbin
        g_object_set(audioBin, "message-forward", TRUE, nullptr);

        // Construir cadena dinámica:
        //   convert → [rg] → [eq] → [comp] → level → volume → level_post → sink
        QVector<GstElement*> chain;
        chain.append(convert);
        if (rgvolume) {
            g_object_set(rgvolume, "fallback-gain", -6.0, nullptr);
            chain.append(rgvolume);
        }
        if (equalizerElement_) chain.append(equalizerElement_);
        if (compGainElement_) chain.append(compGainElement_);
        chain.append(levelElement_);
        chain.append(volumeElement_);
        if (levelPostElement_) chain.append(levelPostElement_);
        chain.append(actualSink);

        // Agregar todos al bin
        for (auto* el : chain) gst_bin_add(GST_BIN(audioBin), el);

        // Enlazar secuencialmente
        for (int i = 0; i < chain.size() - 1; ++i) {
            gst_element_link(chain[i], chain[i + 1]);
        }

        // Log de la cadena
        QStringList chainNames;
        for (auto* el : chain) chainNames << gst_element_get_name(el);
        LOG_DEBUG("[AudioPipeline:{}] Audio bin: {}",
                 static_cast<int>(id_), chainNames.join(" → ").toStdString());

        // Ghost pad: el bin expone el pad de entrada del audioconvert
        GstPad* sinkPad = gst_element_get_static_pad(convert, "sink");
        gst_element_add_pad(audioBin, gst_ghost_pad_new("sink", sinkPad));
        gst_object_unref(sinkPad);

        g_object_set(pipeline_, "audio-sink", audioBin, nullptr);
        g_object_set(volumeElement_, "volume", volume_, nullptr);
    } else if (audioBin && convert && volumeElement_ && actualSink) {
        // Fallback sin level (level no disponible)
        LOG_WARN("[AudioPipeline:{}] Elemento 'level' no disponible, sin detección de silencio",
                 static_cast<int>(id_));
        if (levelElement_) { gst_object_unref(levelElement_); levelElement_ = nullptr; }
        if (levelPostElement_) { gst_object_unref(levelPostElement_); levelPostElement_ = nullptr; }
        gst_bin_add_many(GST_BIN(audioBin), convert, volumeElement_, actualSink, nullptr);
        gst_element_link_many(convert, volumeElement_, actualSink, nullptr);

        GstPad* sinkPad = gst_element_get_static_pad(convert, "sink");
        gst_element_add_pad(audioBin, gst_ghost_pad_new("sink", sinkPad));
        gst_object_unref(sinkPad);

        g_object_set(pipeline_, "audio-sink", audioBin, nullptr);
        g_object_set(volumeElement_, "volume", volume_, nullptr);
    } else {
        // Fallback: usar el sink directo sin bin de volumen
        LOG_WARN("[AudioPipeline:{}] No se pudo crear audio bin, usando sink directo",
                 static_cast<int>(id_));
        if (audioBin) gst_object_unref(audioBin);
        if (convert) gst_object_unref(convert);
        if (volumeElement_) { gst_object_unref(volumeElement_); volumeElement_ = nullptr; }
        if (levelPostElement_) { gst_object_unref(levelPostElement_); levelPostElement_ = nullptr; }
        if (actualSink) {
            g_object_set(pipeline_, "audio-sink", actualSink, nullptr);
        }
    }

    // Desactivar video (somos audio-only)
    GstElement* fakeSink = gst_element_factory_make("fakesink", "fake_video");
    if (fakeSink) {
        g_object_set(pipeline_, "video-sink", fakeSink, nullptr);
    }

    // Volumen inicial: playbin volume queda en 1.0 (PulseAudio ve volumen fijo)
    // El fade se controla con volumeElement_ interno (invisible para PA)
    g_object_set(pipeline_, "volume", 1.0, nullptr);

    // Defecto 1.11: la señal about-to-finish se eliminó. No estaba en uso
    // (el crossfade se gestiona por polling del fadeMonitor en MainWindow)
    // y mantenerla conectada solo agregaba ruido y latencia ínfima sin
    // beneficio real. Si se necesita gapless puro en el futuro, se vuelve
    // a conectar entonces.

    // Configurar bus de mensajes.
    //
    // IMPORTANTE (defecto Windows): gst_bus_add_watch() entrega los mensajes a
    // través del main loop de GLib. En Linux, Qt itera ese main loop (usa el
    // dispatcher de glib), así que los mensajes llegan. En WINDOWS, Qt usa su
    // propio dispatcher y NUNCA itera el main loop de GLib: los mensajes del
    // bus (level/VU, EOS de fin de pista, tags, errores, cambios de estado)
    // jamás se entregaban. Eso causaba VU congelado, línea de tiempo quieta,
    // metadatos sin actualizar y —en modo manual— que la pista no avanzara al
    // terminar (nunca llegaba el EOS).
    //
    // Solución multiplataforma: NO usar el watch de GLib; vaciar el bus
    // nosotros mismos con un QTimer (Qt lo ejecuta en todos los sistemas).
    bus_ = gst_element_get_bus(pipeline_);
    busPollTimer_ = new QTimer(this);
    busPollTimer_->setInterval(50);
    connect(busPollTimer_, &QTimer::timeout, this, [this]() {
        if (!bus_) return;
        GstMessage* m = nullptr;
        while ((m = gst_bus_pop(bus_)) != nullptr) {
            // Reutilizamos el mismo handler que usaba el watch. Con gst_bus_pop
            // el mensaje es nuestro, así que lo liberamos tras procesarlo.
            onBusMessage(bus_, m, this);
            gst_message_unref(m);
        }
    });
    busPollTimer_->start();

    // Timer de posición (emite ~4 veces por segundo) — usa método de instancia
    // para que la lógica de los watchdogs sea fácilmente testeable / lineable.
    positionTimer_ = new QTimer(this);
    positionTimer_->setInterval(250);
    connect(positionTimer_, &QTimer::timeout, this, &AudioPipeline::positionTick);

    initialized_ = true;
    LOG_INFO("[AudioPipeline:{}] Inicializado (device: {})",
             static_cast<int>(id_), audioDevice.toStdString());
    return true;
}

// ── Control de reproducción ──────────────────────────────

bool AudioPipeline::play(const QString& uri)
{
    if (!initialized_) {
        LOG_ERROR("[AudioPipeline:{}] No inicializado", static_cast<int>(id_));
        return false;
    }

    // Detener lo que esté sonando
    if (state_ != PlaybackState::Stopped) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }

    // Incrementar generación: cualquier lambda diferido de handleEos /
    // handleError programado por la reproducción anterior debe quedar
    // invalidado. Esto evita que un set_state(NULL) tardío mate la nueva
    // reproducción que estamos arrancando ahora.
    ++playGeneration_;
    teardownPending_ = false;

    // Convertir ruta local a URI si es necesario
    currentUri_ = pathToUri(uri);

    int64_t now = QDateTime::currentMSecsSinceEpoch();
    silenceConsecutiveMs_ = 0;
    cachedDurationMs_ = 0;
    lastAudioTimestamp_ = now;
    lastLevelReceivedMs_ = 0;            // 0 = aún no llegó ningún level — habilita watchdog inicial
    playbackStartMs_ = now;
    silenceWatchdogFired_ = false;
    sinkInitWatchdogFired_ = false;
    lastObservedPositionMs_ = -1;
    lastPositionChangeMs_ = now;
    stallWatchdogFired_ = false;

    LOG_INFO("[AudioPipeline:{}] Play: {}", static_cast<int>(id_),
             QFileInfo(uri).fileName().toStdString());

    g_object_set(pipeline_, "uri", currentUri_.toUtf8().constData(), nullptr);

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("[AudioPipeline:{}] Error al iniciar reproducción de {}",
                  static_cast<int>(id_), uri.toStdString());
        state_ = PlaybackState::Stopped;

        // Auto-skip para pipelines de reproducción de programación.
        // Events queda fuera: el EventDispatcher gestiona reintentos.
        if (id_ == PipelineId::Main || id_ == PipelineId::MainAlt ||
            id_ == PipelineId::Pisador) {
            LOG_WARN("[AudioPipeline:{}] Auto-skip: archivo no reproducible",
                     static_cast<int>(id_));
            QTimer::singleShot(200, this, [this]() {
                emit trackFinished();
            });
        } else {
            emit stateChanged(state_);
            emit errorOccurred(QString("No se pudo reproducir: %1").arg(uri));
        }
        return false;
    }

    startPositionTimer();
    return true;
}

void AudioPipeline::pause()
{
    if (!pipeline_ || state_ != PlaybackState::Playing) return;

    gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    stopPositionTimer();
    state_ = PlaybackState::Paused;
    emit stateChanged(state_);
    LOG_DEBUG("[AudioPipeline:{}] Pausado", static_cast<int>(id_));
}

void AudioPipeline::resume()
{
    if (!pipeline_ || state_ != PlaybackState::Paused) return;

    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    startPositionTimer();
    state_ = PlaybackState::Playing;

    // Resetear watchdogs: la pausa no cuenta como silencio
    int64_t now = QDateTime::currentMSecsSinceEpoch();
    lastAudioTimestamp_ = now;
    lastLevelReceivedMs_ = now;          // ya hubo audio antes; no aplica watchdog inicial
    playbackStartMs_ = now;
    silenceWatchdogFired_ = false;
    silenceConsecutiveMs_ = 0;
    sinkInitWatchdogFired_ = false;
    lastObservedPositionMs_ = -1;
    lastPositionChangeMs_ = now;
    stallWatchdogFired_ = false;

    emit stateChanged(state_);
    LOG_DEBUG("[AudioPipeline:{}] Reanudado", static_cast<int>(id_));
}

void AudioPipeline::stop()
{
    if (!pipeline_) return;

    // Invalidar lambdas diferidos pendientes (mismo motivo que en play())
    ++playGeneration_;
    teardownPending_ = false;

    gst_element_set_state(pipeline_, GST_STATE_NULL);
    stopPositionTimer();
    state_ = PlaybackState::Stopped;
    currentUri_.clear();
    emit stateChanged(state_);
    LOG_DEBUG("[AudioPipeline:{}] Detenido", static_cast<int>(id_));
}

void AudioPipeline::seek(int64_t positionMs)
{
    if (!pipeline_ || state_ == PlaybackState::Stopped) return;

    gint64 posNs = positionMs * GST_MSECOND;
    gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        posNs);
    LOG_DEBUG("[AudioPipeline:{}] Seek a {}ms", static_cast<int>(id_), positionMs);
}

// ── Volumen ──────────────────────────────────────────────

void AudioPipeline::setVolume(double volume)
{
    volume_ = qBound(0.0, volume, 1.0);
    if (volumeElement_) {
        g_object_set(volumeElement_, "volume", volume_, nullptr);
    } else if (pipeline_) {
        // Fallback: usar playbin volume (afecta PulseAudio)
        g_object_set(pipeline_, "volume", volume_, nullptr);
    }
}

double AudioPipeline::volume() const
{
    return volume_;
}

void AudioPipeline::setSilenceDetection(bool enabled, int thresholdMs, double levelDb)
{
    silenceDetectionEnabled_ = enabled;
    silenceThresholdMs_ = thresholdMs;
    silenceLevelDb_ = levelDb;
    LOG_DEBUG("[AudioPipeline:{}] Silencio: {} (umbral: {}ms, nivel: {} dB)",
             static_cast<int>(id_),
             enabled ? "ON" : "OFF",
             thresholdMs, levelDb);
}

// ── Posición / Duración ──────────────────────────────────

int64_t AudioPipeline::positionMs() const
{
    if (!pipeline_ || state_ == PlaybackState::Stopped) return 0;

    gint64 pos = 0;
    if (gst_element_query_position(pipeline_, GST_FORMAT_TIME, &pos)) {
        return pos / GST_MSECOND;
    }
    return 0;
}

int64_t AudioPipeline::durationMs() const
{
    if (!pipeline_) return cachedDurationMs_;

    gint64 dur = 0;
    if (gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &dur) && dur > 0) {
        cachedDurationMs_ = dur / GST_MSECOND;
        return cachedDurationMs_;
    }
    // GStreamer retorna 0 después de EOS — usar el valor cacheado
    return cachedDurationMs_;
}

// ── Callbacks de GStreamer ────────────────────────────────

gboolean AudioPipeline::onBusMessage(GstBus* /*bus*/, GstMessage* msg, gpointer data)
{
    auto* self = static_cast<AudioPipeline*>(data);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        self->handleEos();
        break;

    case GST_MESSAGE_ERROR:
        self->handleError(msg);
        break;

    case GST_MESSAGE_WARNING: {
        GError* warn = nullptr;
        gchar* debugInfo = nullptr;
        gst_message_parse_warning(msg, &warn, &debugInfo);
        LOG_WARN("[AudioPipeline:{}] Warning GStreamer: {} — Debug: {}",
                 static_cast<int>(self->id_),
                 warn ? warn->message : "unknown",
                 debugInfo ? debugInfo : "none");
        if (warn) g_error_free(warn);
        g_free(debugInfo);
        break;
    }

    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline_)) {
            self->handleStateChange(msg);
        }
        break;

    case GST_MESSAGE_TAG:
        self->handleTag(msg);
        break;

    case GST_MESSAGE_BUFFERING: {
        gint percent = 0;
        gst_message_parse_buffering(msg, &percent);
        if (percent < 100 && self->state_ != PlaybackState::Buffering) {
            self->state_ = PlaybackState::Buffering;
            emit self->stateChanged(self->state_);
        }
        break;
    }

    case GST_MESSAGE_ELEMENT:
    {
        const GstStructure* s = gst_message_get_structure(msg);
        if (s && self->levelElement_) {
            const gchar* name = gst_structure_get_name(s);
            if (name) {
                // Defecto 1.7: discriminamos entre level pre-fader y post-fader
                // mirando el GstObject que originó el mensaje. El despacho a
                // handleLevel pasa un flag `isPostFader` para que la función
                // sepa si debe alimentar el VU meter (post) o el watchdog +
                // compresor (pre).
                auto dispatch = [self](GstMessage* lm) {
                    GstObject* src = GST_MESSAGE_SRC(lm);
                    bool isPost = (src != nullptr
                        && self->levelPostElement_ != nullptr
                        && src == GST_OBJECT(self->levelPostElement_));
                    self->handleLevel(lm, isPost);
                };

                if (strcmp(name, "level") == 0) {
                    dispatch(msg);
                } else if (strcmp(name, "GstBinForwarded") == 0) {
                    GstMessage* fwdMsg = nullptr;
                    gst_structure_get(s, "message", GST_TYPE_MESSAGE, &fwdMsg, nullptr);
                    if (fwdMsg) {
                        const GstStructure* fs = gst_message_get_structure(fwdMsg);
                        if (fs && strcmp(gst_structure_get_name(fs), "level") == 0) {
                            dispatch(fwdMsg);
                        }
                        gst_message_unref(fwdMsg);
                    }
                }
            }
        }
        break;
    }

    default:
        break;
    }

    return TRUE;  // Mantener el watch
}

void AudioPipeline::handleEos()
{
    LOG_INFO("[AudioPipeline:{}] Pista terminada: {}",
             static_cast<int>(id_),
             QFileInfo(QUrl(currentUri_).toLocalFile()).fileName().toStdString());

    // Si handleError ya programó un teardown para este mismo fallo, no
    // programar otro. GStreamer puede emitir Error+EOS en cascada para
    // un único fallo HTTP (observado en logs de stream cortado): sin esta
    // guarda, el segundo lambda generaba un emit trackFinished tardío
    // que MainWindow logueaba como "tardío del deck standby, ignorando" —
    // pero antes de ese emit, su set_state(NULL) mataba la canción que
    // MainWindow ya había arrancado.
    if (teardownPending_) {
        LOG_DEBUG("[AudioPipeline:{}] EOS deduplicado (teardown ya pendiente)",
                  static_cast<int>(id_));
        return;
    }
    teardownPending_ = true;

    stopPositionTimer();

    // Cambiar el estado lógico inmediatamente para que las queries de
    // posición/duración no encuentren el pipeline en estado inconsistente.
    state_ = PlaybackState::Stopped;

    // Defecto crítico (locución de minutos): set_state(NULL) y emit
    // trackFinished DEBEN ejecutarse en el MISMO slot diferido y EN ESTE
    // ORDEN.
    //
    // Defecto fase 8c: doble protección con playGeneration_ — si entre
    // el momento en que programamos el lambda y el momento en que se
    // ejecuta alguien llamó play() o stop(), abortamos sin tocar nada.
    GstElement* p = pipeline_;
    quint64 gen = playGeneration_;
    QTimer::singleShot(0, this, [this, p, gen]() {
        if (playGeneration_ != gen) {
            LOG_DEBUG("[AudioPipeline:{}] Teardown EOS abortado (play() corrió en medio)",
                      static_cast<int>(id_));
            return;
        }
        teardownPending_ = false;
        if (pipeline_ == p && p) {
            gst_element_set_state(p, GST_STATE_NULL);
        }
        emit stateChanged(state_);
        emit trackFinished();
    });
}

void AudioPipeline::handleError(GstMessage* msg)
{
    GError* err = nullptr;
    gchar* debugInfo = nullptr;
    gst_message_parse_error(msg, &err, &debugInfo);

    QString errorMsg = err ? QString::fromUtf8(err->message) : QStringLiteral("unknown");
    QString currentFile = currentUri_;

    LOG_ERROR("[AudioPipeline:{}] Error GStreamer: {} — Debug: {}",
              static_cast<int>(id_),
              errorMsg.toStdString(),
              debugInfo ? debugInfo : "none");

    if (!currentFile.isEmpty()) {
        LOG_ERROR("[AudioPipeline:{}] Archivo problemático: {}",
                  static_cast<int>(id_), currentFile.toStdString());
    }

    if (err) g_error_free(err);
    g_free(debugInfo);

    // Deduplicar Error+EOS para un mismo fallo (ver comentario en handleEos)
    if (teardownPending_) {
        LOG_DEBUG("[AudioPipeline:{}] Error deduplicado (teardown ya pendiente)",
                  static_cast<int>(id_));
        return;
    }
    teardownPending_ = true;

    stopPositionTimer();
    state_ = PlaybackState::Error;

    // ──────────────────────────────────────────────────────────
    // Decisión: ¿auto-skip o delegar al caller?
    //
    // Auto-skip sirve para pipelines de programación normal Y SOLO PARA
    // ARCHIVOS LOCALES. Si el archivo está roto, va a seguir roto: tiene
    // sentido saltar al siguiente.
    //
    // STREAMS (http://, https://) son distintos: una desconexión casi
    // siempre es transitoria (corte breve de internet, hipo del servidor).
    // Saltar a la siguiente pista pierde información — lo correcto es darle
    // al caller la oportunidad de reintentar, igual que ya hace el
    // EventDispatcher para sus streams.
    //
    // Para esos casos delegamos en el caller (MainWindow / EventDispatcher)
    // emitiendo errorOccurred. Si el caller no maneja el error, el watchdog
    // de silencio de 15s lo capturará igual; pero el caller TIENE el contexto
    // para hacer reintentos inteligentes y decidir cuándo realmente saltar.
    //
    // NOTA: los pipelines Events / Cue / InstantPlay tampoco hacen auto-skip
    // por motivos similares (sus callers manejan el flujo). Pisador SÍ
    // mantiene auto-skip porque sus archivos son siempre locales y un fallo
    // ahí debe avanzar inmediatamente.
    // ──────────────────────────────────────────────────────────
    bool isStream = currentFile.startsWith(QStringLiteral("http://"))
                 || currentFile.startsWith(QStringLiteral("https://"));

    bool autoSkipPipeline =
        (id_ == PipelineId::Main || id_ == PipelineId::MainAlt ||
         id_ == PipelineId::Pisador) && !isStream;

    if (autoSkipPipeline) {
        LOG_WARN("[AudioPipeline:{}] Auto-skip: avanzando a la siguiente pista",
                 static_cast<int>(id_));
        state_ = PlaybackState::Stopped;

        // Mismo patrón que handleEos: set_state(NULL) y trackFinished en el
        // mismo slot diferido, en orden. Captura playGeneration_ para que el
        // lambda se aborte si play()/stop() invalidaron este teardown
        // entre que se programó y se ejecuta (ver detalles en handleEos).
        //
        // El delay de 200ms anterior se REDUCE A 0: la guarda de
        // playGeneration_ hace innecesario el cushion (si llega otro play()
        // en medio el lambda se aborta limpio). El delay viejo era
        // justamente la ventana donde el bug del log fase 8c se manifestaba.
        GstElement* p = pipeline_;
        quint64 gen = playGeneration_;
        QTimer::singleShot(0, this, [this, p, gen]() {
            if (playGeneration_ != gen) {
                LOG_DEBUG("[AudioPipeline:{}] Teardown error abortado "
                          "(play() corrió en medio)", static_cast<int>(id_));
                return;
            }
            teardownPending_ = false;
            if (pipeline_ == p && p) {
                gst_element_set_state(p, GST_STATE_NULL);
            }
            emit stateChanged(state_);
            emit trackFinished();
        });
    } else {
        // Pipelines de preview/CUE/Events O streams en cualquier pipeline:
        // el caller maneja el error como prefiera (típicamente reintenta
        // antes de saltar). Igual diferimos el set_state(NULL) para no
        // llamarlo desde el bus watch.
        if (isStream) {
            LOG_INFO("[AudioPipeline:{}] Stream falló, delegando al caller "
                     "(no auto-skip)", static_cast<int>(id_));
        }
        GstElement* p = pipeline_;
        quint64 gen = playGeneration_;
        QTimer::singleShot(0, this, [this, p, gen, errorMsg]() {
            if (playGeneration_ != gen) {
                LOG_DEBUG("[AudioPipeline:{}] Teardown error abortado "
                          "(play() corrió en medio)", static_cast<int>(id_));
                return;
            }
            teardownPending_ = false;
            if (pipeline_ == p && p) {
                gst_element_set_state(p, GST_STATE_NULL);
            }
            emit stateChanged(state_);
            emit errorOccurred(errorMsg);
        });
    }
}

void AudioPipeline::handleStateChange(GstMessage* msg)
{
    GstState oldState, newState, pending;
    gst_message_parse_state_changed(msg, &oldState, &newState, &pending);

    if (newState == GST_STATE_PLAYING && state_ != PlaybackState::Playing) {
        state_ = PlaybackState::Playing;
        emit stateChanged(state_);
    } else if (newState == GST_STATE_PAUSED && state_ == PlaybackState::Buffering) {
        // Buffering terminado, reanudar
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    }
}

void AudioPipeline::handleTag(GstMessage* msg)
{
    GstTagList* tags = nullptr;
    gst_message_parse_tag(msg, &tags);

    if (!tags) return;

    TrackMetadata meta;
    meta.filePath = QUrl(currentUri_).toLocalFile();

    gchar* str = nullptr;
    if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &str)) {
        meta.title = QString::fromUtf8(str);
        g_free(str);
    }
    if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &str)) {
        meta.artist = QString::fromUtf8(str);
        g_free(str);
    }
    if (gst_tag_list_get_string(tags, GST_TAG_ALBUM, &str)) {
        meta.album = QString::fromUtf8(str);
        g_free(str);
    }
    meta.durationMs = durationMs();

    gst_tag_list_unref(tags);

    if (!meta.title.isEmpty() || !meta.artist.isEmpty()) {
        emit metadataReceived(meta);
    }
}

void AudioPipeline::handleLevel(GstMessage* msg, bool isPostFader)
{
    // Permitir durante Playing y Buffering (streams emiten audio mientras bufferean)
    if (state_ != PlaybackState::Playing && state_ != PlaybackState::Buffering) return;

    const GstStructure* s = gst_message_get_structure(msg);
    if (!s) return;

    const GValue* rmsValue = gst_structure_get_value(s, "rms");
    if (!rmsValue) return;

    // Extraer niveles por canal
    double channels[2] = {-100.0, -100.0};  // L, R
    int channelCount = 0;

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    if (GST_VALUE_HOLDS_LIST(rmsValue)) {
        guint n = gst_value_list_get_size(rmsValue);
        for (guint i = 0; i < n && i < 2; ++i) {
            const GValue* val = gst_value_list_get_value(rmsValue, i);
            if (val && G_VALUE_TYPE(val) == G_TYPE_DOUBLE) {
                channels[i] = g_value_get_double(val);
                channelCount++;
            }
        }
    } else if (G_VALUE_TYPE(rmsValue) == G_TYPE_VALUE_ARRAY) {
        GValueArray* arr = (GValueArray*)g_value_get_boxed(rmsValue);
        if (arr) {
            for (guint i = 0; i < arr->n_values && i < 2; ++i) {
                GValue* val = g_value_array_get_nth(arr, i);
                if (val && G_VALUE_TYPE(val) == G_TYPE_DOUBLE) {
                    channels[i] = g_value_get_double(val);
                    channelCount++;
                }
            }
        }
    } else if (G_VALUE_TYPE(rmsValue) == G_TYPE_DOUBLE) {
        channels[0] = channels[1] = g_value_get_double(rmsValue);
        channelCount = 1;
    } else {
        return;
    }
G_GNUC_END_IGNORE_DEPRECATIONS

    if (channelCount == 0) return;
    if (channelCount == 1) channels[1] = channels[0];

    double maxRms = qMax(channels[0], channels[1]);

    // ═══════════════════════════════════════════════════════════
    // ANTES de discriminar pre/post-fader: cualquier mensaje level
    // (pre o post) significa que el pipeline está vivo y procesando
    // audio. El watchdog de "sin mensajes level" debe contar ambas
    // fuentes — si solo cuenta una, basta con que esa fuente se demore
    // un instante en arrancar para que el watchdog dispare un falso
    // positivo (bug confirmado en log: 15s justos después del play).
    //
    // Solo aplica a Main/MainAlt (los otros pipelines no usan watchdog).
    // ═══════════════════════════════════════════════════════════
    if ((id_ == PipelineId::Main || id_ == PipelineId::MainAlt)
        && silenceDetectionEnabled_) {
        lastLevelReceivedMs_ = QDateTime::currentMSecsSinceEpoch();
    }

    // ═══════════════════════════════════════════════════════════
    // RUTA POST-FADER: solo alimenta el VU meter al aire.
    // Defecto 1.7: ahora la medición refleja crossfade y volumen master.
    // ═══════════════════════════════════════════════════════════
    if (isPostFader) {
        emit levelUpdated(channels[0], channels[1]);
        return;
    }

    // ═══════════════════════════════════════════════════════════
    // RUTA PRE-FADER: compresor en software + watchdog de silencio
    // por nivel. El compresor debe medir antes del fader para que su
    // umbral no se vea afectado por el volumen master. El watchdog
    // por nivel (no el de "sin mensajes") mide pre-fader para detectar
    // archivos rotos aunque el fader esté en mute o haciendo crossfade.
    // ═══════════════════════════════════════════════════════════

    // ── Compresor en software ──────────────────────────
    if (compressorActive_ && compGainElement_) {
        // También leer peak para compresión más precisa
        const GValue* peakValue = gst_structure_get_value(s, "peak");
        double peakDb = maxRms;  // Fallback a RMS
        if (peakValue) {
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            if (GST_VALUE_HOLDS_LIST(peakValue)) {
                double maxPeak = -100.0;
                guint n = gst_value_list_get_size(peakValue);
                for (guint i = 0; i < n && i < 2; ++i) {
                    const GValue* val = gst_value_list_get_value(peakValue, i);
                    if (val && G_VALUE_TYPE(val) == G_TYPE_DOUBLE)
                        maxPeak = qMax(maxPeak, g_value_get_double(val));
                }
                peakDb = maxPeak;
            } else if (G_VALUE_TYPE(peakValue) == G_TYPE_VALUE_ARRAY) {
                GValueArray* arr = (GValueArray*)g_value_get_boxed(peakValue);
                double maxPeak = -100.0;
                if (arr) {
                    for (guint i = 0; i < arr->n_values && i < 2; ++i) {
                        GValue* val = g_value_array_get_nth(arr, i);
                        if (val && G_VALUE_TYPE(val) == G_TYPE_DOUBLE)
                            maxPeak = qMax(maxPeak, g_value_get_double(val));
                    }
                }
                peakDb = maxPeak;
            } else if (G_VALUE_TYPE(peakValue) == G_TYPE_DOUBLE) {
                peakDb = g_value_get_double(peakValue);
            }
G_GNUC_END_IGNORE_DEPRECATIONS
        }

        // Calcular reducción de ganancia (en dB)
        // Compensar: estimamos el nivel de entrada sumando la reducción actual
        // (topología "reconstructed input" — estable sin oscilaciones)
        double inputEstimate = peakDb + currentGainReduction_;
        double targetReduction = 0.0;
        if (inputEstimate > compThresholdDb_) {
            double excess = inputEstimate - compThresholdDb_;
            targetReduction = excess * (1.0 - 1.0 / compRatio_);
        }

        // Smoothing: attack rápido (~20ms), release lento (~300ms)
        // Con updates cada 100ms: attack ~instantáneo, release ~3 updates
        const double attackCoeff = 0.9;   // Rápido: llega al 90% del target en 1 update
        const double releaseCoeff = 0.15; // Lento: libera ~15% por update

        if (targetReduction > currentGainReduction_) {
            // Attack: subir reducción rápido
            currentGainReduction_ += (targetReduction - currentGainReduction_) * attackCoeff;
        } else {
            // Release: bajar reducción lento
            currentGainReduction_ += (targetReduction - currentGainReduction_) * releaseCoeff;
        }

        // Limitar reducción máxima a 30 dB
        currentGainReduction_ = qBound(0.0, currentGainReduction_, 30.0);

        // Aplicar: convertir dB de reducción a factor lineal
        double gainLinear = pow(10.0, -currentGainReduction_ / 20.0);
        g_object_set(compGainElement_, "volume", gainLinear, nullptr);
    }

    // Si no hay un level post-fader disponible (fallback de initialize),
    // emitimos el VU desde aquí (mismo comportamiento previo, mejor que nada)
    if (!levelPostElement_) {
        emit levelUpdated(channels[0], channels[1]);
    }

    // Detección de silencio por nivel: solo en pipelines principales
    if (id_ != PipelineId::Main && id_ != PipelineId::MainAlt) return;
    if (!silenceDetectionEnabled_) return;

    // (lastLevelReceivedMs_ ya fue actualizado al entrar a handleLevel,
    // antes de la rama post-fader, para que ambas fuentes alimenten el
    // watchdog de "sin mensajes level".)
    int64_t now = QDateTime::currentMSecsSinceEpoch();

    // Si hay audio real (por encima del umbral), marcar el timestamp
    if (maxRms >= silenceLevelDb_) {
        lastAudioTimestamp_ = now;
        silenceConsecutiveMs_ = 0;
    }
    // Si el level reporta silencio, verificar con timestamp real
    else if (lastAudioTimestamp_ > 0) {
        int64_t silentMs = now - lastAudioTimestamp_;
        if (silentMs >= silenceThresholdMs_ && silenceConsecutiveMs_ == 0) {
            silenceConsecutiveMs_ = silentMs;  // Marcar como ya disparado
            LOG_WARN("[AudioPipeline:{}] Silencio detectado por nivel ({} ms, {:.1f} dB): {}",
                     static_cast<int>(id_), silentMs, maxRms,
                     QFileInfo(QUrl(currentUri_).toLocalFile()).fileName().toStdString());
            emit silenceDetected();
        }
    }
}

// ── Position tick: posición + tres watchdogs ─────────────
//
// Se ejecuta cada 250ms mientras el pipeline está reproduciendo.
//   1. Emite positionUpdated para la UI.
//   2. Watchdog de sink inicial: si pasaron SINK_INIT_TIMEOUT_MS desde
//      play() y NO se recibió ningún mensaje level, el sink probablemente
//      está colgado (USB desconectado, pulseaudio caído, etc.).
//   3. Watchdog de silencio total: si pasaron silenceThresholdMs sin
//      mensajes level (ej: silencio digital puro = level no emite).
//   4. Watchdog de posición estancada: si la posición no avanza durante
//      POSITION_STALL_THRESHOLD_MS pese a estar Playing, el pipeline se
//      colgó (clásico de USB suspendido por powersave).
//
// Solo aplica a Main/MainAlt — los otros pipelines no necesitan auto-skip
// agresivo.

void AudioPipeline::positionTick()
{
    if (state_ != PlaybackState::Playing) return;

    int64_t pos = positionMs();
    int64_t dur = durationMs();
    emit positionUpdated(pos, dur);

    // Solo aplicamos los watchdogs a los decks principales
    if (id_ != PipelineId::Main && id_ != PipelineId::MainAlt) return;
    if (!silenceDetectionEnabled_) return;

    int64_t now = QDateTime::currentMSecsSinceEpoch();

    // ── Watchdog 1: sink inicial no responde ──
    // Si pasaron N ms desde play() y NO llegó ningún mensaje level,
    // el sink está colgado. Más rápido que esperar al watchdog de silencio.
    if (!sinkInitWatchdogFired_ && lastLevelReceivedMs_ == 0 && playbackStartMs_ > 0) {
        int64_t sincePlay = now - playbackStartMs_;
        if (sincePlay >= SINK_INIT_TIMEOUT_MS) {
            sinkInitWatchdogFired_ = true;
            silenceWatchdogFired_ = true;  // No re-disparar el otro watchdog
            LOG_WARN("[AudioPipeline:{}] Watchdog sink inicial: sin mensajes level "
                     "tras {}ms — el sink no responde: {}",
                     static_cast<int>(id_), sincePlay,
                     QFileInfo(QUrl(currentUri_).toLocalFile()).fileName().toStdString());
            emit silenceDetected();
            return;
        }
    }

    // ── Watchdog 2: silencio total (ningún level por mucho tiempo) ──
    // GStreamer no envía mensajes level cuando los samples son
    // exactamente 0 (silencio digital puro). handleLevel no se ejecuta
    // → este watchdog detecta esa ausencia total.
    if (!silenceWatchdogFired_ && lastLevelReceivedMs_ > 0) {
        int64_t noLevelMs = now - lastLevelReceivedMs_;
        if (noLevelMs >= silenceThresholdMs_) {
            silenceWatchdogFired_ = true;
            LOG_WARN("[AudioPipeline:{}] Watchdog silencio: {}ms sin mensajes level: {}",
                     static_cast<int>(id_), noLevelMs,
                     QFileInfo(QUrl(currentUri_).toLocalFile()).fileName().toStdString());
            emit silenceDetected();
            return;
        }
    }

    // ── Watchdog 3: posición estancada (pipeline colgado) ──
    // GStreamer puede quedarse en GST_STATE_PLAYING reportando posición
    // congelada cuando el sink se suspende (USB powersave, ALSA xrun,
    // pulseaudio cliente desconectado). El estado lógico dice "tocando"
    // pero no avanza nada.
    if (!stallWatchdogFired_ && pos > 0) {
        if (lastObservedPositionMs_ < 0) {
            lastObservedPositionMs_ = pos;
            lastPositionChangeMs_ = now;
        } else if (pos != lastObservedPositionMs_) {
            // La posición avanzó (o retrocedió por seek) — todo bien
            lastObservedPositionMs_ = pos;
            lastPositionChangeMs_ = now;
        } else {
            // Posición congelada
            int64_t stallMs = now - lastPositionChangeMs_;
            if (stallMs >= POSITION_STALL_THRESHOLD_MS) {
                stallWatchdogFired_ = true;
                LOG_WARN("[AudioPipeline:{}] Watchdog posición estancada: "
                         "{}ms sin avance en pos={}ms: {}",
                         static_cast<int>(id_), stallMs, pos,
                         QFileInfo(QUrl(currentUri_).toLocalFile()).fileName().toStdString());
                emit silenceDetected();
                return;
            }
        }
    }
}

// ── Timer de posición ────────────────────────────────────

void AudioPipeline::startPositionTimer()
{
    if (positionTimer_ && !positionTimer_->isActive()) {
        positionTimer_->start();
    }
}

void AudioPipeline::stopPositionTimer()
{
    if (positionTimer_ && positionTimer_->isActive()) {
        positionTimer_->stop();
    }
}

// ── Utilidades ───────────────────────────────────────────

QString AudioPipeline::pathToUri(const QString& path)
{
    // Si ya es un URI (http://, https://, file://, etc.), retornar como está
    if (path.contains("://")) {
        return path;
    }

    // Detectar URLs sin protocolo (ej: "radios.ejemplo.com/stream")
    // Si tiene un punto y no empieza con / ni con letra de unidad, es probable URL
    if (path.contains('.') && !path.startsWith('/') && !path.startsWith("~/")
        && !(path.length() > 2 && path[1] == ':')) {
        LOG_INFO("[AudioPipeline] URL sin protocolo detectada, agregando http://: {}",
                 path.toStdString());
        return "http://" + path;
    }

    // Convertir ruta local a file:// URI
    return QUrl::fromLocalFile(path).toString();
}

// ── Ecualizador y Compresor ──────────────────────────────

void AudioPipeline::setEqBand(int band, double gainDb)
{
    if (!equalizerElement_ || band < 0 || band > 9) return;
    gainDb = qBound(-24.0, gainDb, 12.0);
    QString prop = QString("band%1").arg(band);
    g_object_set(equalizerElement_, prop.toUtf8().constData(), gainDb, nullptr);
}

void AudioPipeline::setEqBands(const QVector<double>& gains)
{
    if (!equalizerElement_) return;
    for (int i = 0; i < qMin(gains.size(), 10); ++i) {
        setEqBand(i, gains[i]);
    }
}

void AudioPipeline::setCompressorEnabled(bool enabled)
{
    compressorActive_ = enabled;
    if (!enabled && compGainElement_) {
        // Restaurar ganancia unitaria
        g_object_set(compGainElement_, "volume", 1.0, nullptr);
        currentGainReduction_ = 0.0;
        LOG_DEBUG("[AudioPipeline:{}] Compresor desactivado", static_cast<int>(id_));
    }
    if (enabled) {
        LOG_DEBUG("[AudioPipeline:{}] Compresor activado: threshold={:.1f} dB, ratio={:.1f}:1",
                 static_cast<int>(id_), compThresholdDb_, compRatio_);
    }
}

void AudioPipeline::setCompressorThreshold(double db)
{
    compThresholdDb_ = qBound(-40.0, db, 0.0);
}

void AudioPipeline::setCompressorRatio(double ratio)
{
    compRatio_ = qBound(1.0, ratio, 20.0);
}

} // namespace sara
