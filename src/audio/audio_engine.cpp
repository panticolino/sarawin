#include "audio/audio_engine.h"
#include "util/logger.h"

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <QProcess>

namespace sara {

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine()
{
    // Los unique_ptr destruyen los pipelines automáticamente
    // antes de que GStreamer se deinicialice
    main_.reset();
    mainAlt_.reset();
    events_.reset();
    instant_.reset();
    cue_.reset();

    LOG_INFO("[AudioEngine] Destruido");
}

bool AudioEngine::initialize(const QString& mainDevice, const QString& cueDevice,
                              const QString& instantDevice)
{
    if (initialized_) return true;

    // Inicializar GStreamer (es seguro llamar múltiples veces)
    GError* err = nullptr;
    if (!gst_init_check(nullptr, nullptr, &err)) {
        QString errorMsg = err ? QString::fromUtf8(err->message) : "unknown";
        if (err) g_error_free(err);
        LOG_ERROR("[AudioEngine] Error inicializando GStreamer: {}", errorMsg.toStdString());
        return false;
    }

    LOG_INFO("[AudioEngine] GStreamer {} inicializado", gst_version_string());

    // Resolver dispositivos
    mainDevice_ = mainDevice.isEmpty() ? "default" : mainDevice;
    eventsDevice_ = mainDevice_;  // Events siempre va por la misma tarjeta que Main (aire)
    instantDevice_ = instantDevice.isEmpty() ? mainDevice_ : instantDevice;

    // Crear Pipeline 1: Programación Principal - Deck A
    main_ = std::make_unique<AudioPipeline>(PipelineId::Main, this);
    main_->setReplayGainEnabled(replayGainEnabled_);
    main_->setProcessingEnabled(processingEnabled_);
    if (!main_->initialize(mainDevice_)) {
        LOG_ERROR("[AudioEngine] Error creando pipeline Main");
        return false;
    }

    // Crear Pipeline 1B: Programación Principal - Deck B (crossfade solapado)
    mainAlt_ = std::make_unique<AudioPipeline>(PipelineId::MainAlt, this);
    mainAlt_->setReplayGainEnabled(replayGainEnabled_);
    mainAlt_->setProcessingEnabled(processingEnabled_);
    if (!mainAlt_->initialize(mainDevice_)) {
        LOG_ERROR("[AudioEngine] Error creando pipeline MainAlt");
        return false;
    }

    // Crear Pipeline 2: Publicidad/Eventos (misma tarjeta que Main)
    events_ = std::make_unique<AudioPipeline>(PipelineId::Events, this);
    events_->setReplayGainEnabled(replayGainEnabled_);
    events_->setProcessingEnabled(processingEnabled_);
    if (!events_->initialize(eventsDevice_)) {
        LOG_ERROR("[AudioEngine] Error creando pipeline Events");
        return false;
    }

    // Crear Pipeline 3: Instant Play / Asistente en Vivo
    instant_ = std::make_unique<AudioPipeline>(PipelineId::InstantPlay, this);
    if (!instant_->initialize(instantDevice_)) {
        LOG_ERROR("[AudioEngine] Error creando pipeline InstantPlay");
        return false;
    }

    // Crear Pipeline 4: CUE / Preescucha
    cueDevice_ = cueDevice.isEmpty() ? mainDevice_ : cueDevice;
    cue_ = std::make_unique<AudioPipeline>(PipelineId::Cue, this);
    if (!cue_->initialize(cueDevice_)) {
        LOG_WARN("[AudioEngine] CUE pipeline no disponible, preescucha desactivada");
        cue_.reset();
    }

    // Crear Pipeline 5: Pisadores (siempre en Main, con EQ/compresor)
    pisador_ = std::make_unique<AudioPipeline>(PipelineId::Pisador, this);
    pisador_->setProcessingEnabled(processingEnabled_);
    if (!pisador_->initialize(mainDevice_)) {
        LOG_WARN("[AudioEngine] Pisador pipeline no disponible");
        pisador_.reset();
    }

    // Reenviar señales de trackFinished
    connect(main_.get(), &AudioPipeline::trackFinished,
            this, &AudioEngine::mainTrackFinished);
    connect(mainAlt_.get(), &AudioPipeline::trackFinished,
            this, &AudioEngine::mainAltTrackFinished);
    connect(events_.get(), &AudioPipeline::trackFinished,
            this, &AudioEngine::eventsTrackFinished);
    connect(instant_.get(), &AudioPipeline::trackFinished,
            this, &AudioEngine::instantTrackFinished);

    // Aplicar volumen inicial
    setMasterVolume(masterVolume_);

    initialized_ = true;
    LOG_INFO("[AudioEngine] 5 pipelines creados (main: {}, events: {}, instant: {}, cue: {}, pisador: main)",
             mainDevice_.toStdString(),
             eventsDevice_.toStdString(),
             instantDevice_.toStdString(),
             cueDevice_.toStdString());
    return true;
}

bool AudioEngine::changeDevice(PipelineId pipeline, const QString& device)
{
    AudioPipeline* pipe = nullptr;
    switch (pipeline) {
        case PipelineId::Main:        pipe = main_.get(); break;
        case PipelineId::MainAlt:     pipe = mainAlt_.get(); break;
        case PipelineId::Events:      pipe = events_.get(); break;
        case PipelineId::InstantPlay: pipe = instant_.get(); break;
        case PipelineId::Cue:         pipe = cue_.get(); break;
        case PipelineId::Pisador:     pipe = pisador_.get(); break;
    }
    if (!pipe) return false;

    // Detener y reinicializar con nuevo dispositivo
    pipe->stop();

    // Recrear el pipeline con el nuevo dispositivo
    pipe->cleanup();
    if (!pipe->initialize(device)) {
        LOG_ERROR("[AudioEngine] Error cambiando dispositivo a: {}", device.toStdString());
        return false;
    }

    // Actualizar tracking
    switch (pipeline) {
        case PipelineId::Main:        mainDevice_ = device; break;
        case PipelineId::MainAlt:     /* same device as Main */ break;
        case PipelineId::Events:      eventsDevice_ = device; break;
        case PipelineId::InstantPlay: instantDevice_ = device; break;
        case PipelineId::Cue:         cueDevice_ = device; break;
        case PipelineId::Pisador:     /* same device as Main */ break;
    }

    LOG_INFO("[AudioEngine] Dispositivo cambiado: pipeline {} → {}",
             static_cast<int>(pipeline), device.toStdString());
    return true;
}

QString AudioEngine::deviceForPipeline(PipelineId pipeline) const
{
    switch (pipeline) {
        case PipelineId::Main:        return mainDevice_;
        case PipelineId::MainAlt:     return mainDevice_;
        case PipelineId::Events:      return eventsDevice_;
        case PipelineId::InstantPlay: return instantDevice_;
        case PipelineId::Cue:         return cueDevice_;
        case PipelineId::Pisador:     return mainDevice_;
    }
    return {};
}

void AudioEngine::setMasterVolume(double volume)
{
    masterVolume_ = qBound(0.0, volume, 1.0);
    if (main_)    main_->setVolume(masterVolume_);
    if (mainAlt_) mainAlt_->setVolume(masterVolume_);
    if (events_)  events_->setVolume(masterVolume_);
    if (pisador_) pisador_->setVolume(masterVolume_);
}

QVector<AudioEngine::AudioDeviceInfo> AudioEngine::availableAudioDevicesDetailed()
{
    QVector<AudioDeviceInfo> devices;

    // Asegurar que GStreamer está inicializado
    GError* initErr = nullptr;
    if (!gst_init_check(nullptr, nullptr, &initErr)) {
        if (initErr) g_error_free(initErr);
        LOG_WARN("[AudioEngine] GStreamer no disponible para listar dispositivos");
        return devices;
    }

    GstDeviceMonitor* monitor = gst_device_monitor_new();
    gst_device_monitor_add_filter(monitor, "Audio/Sink", nullptr);

    if (!gst_device_monitor_start(monitor)) {
        LOG_WARN("[AudioEngine] No se pudo iniciar device monitor");
        gst_object_unref(monitor);
        return devices;
    }

    GList* devList = gst_device_monitor_get_devices(monitor);
    for (GList* it = devList; it != nullptr; it = it->next) {
        GstDevice* dev = GST_DEVICE(it->data);
        gchar* displayName = gst_device_get_display_name(dev);

        GstStructure* props = gst_device_get_properties(dev);
        QString deviceId;

        if (props) {
            // Intentar múltiples propiedades para obtener el ID real del dispositivo
            // PulseAudio usa "node.name" o "device.name" según la versión
            const gchar* propNames[] = {
                "node.name",        // PipeWire / PulseAudio nuevo
                "device.name",      // PulseAudio clásico
                "object.path",      // PipeWire
                "device.bus_path",  // Fallback
                nullptr
            };

            for (int i = 0; propNames[i]; ++i) {
                const gchar* val = gst_structure_get_string(props, propNames[i]);
                if (val && val[0] != '\0') {
                    deviceId = QString::fromUtf8(val);
                    LOG_DEBUG("[AudioEngine] Device '{}' → {} = {}",
                              displayName ? displayName : "?", propNames[i], val);
                    break;
                }
            }

            // Debug: volcar todas las propiedades si no encontramos ID
            if (deviceId.isEmpty()) {
                gchar* str = gst_structure_to_string(props);
                LOG_DEBUG("[AudioEngine] Props de '{}': {}", 
                          displayName ? displayName : "?", str ? str : "null");
                if (str) g_free(str);
            }

            gst_structure_free(props);
        }

        if (displayName) {
            AudioDeviceInfo info;
            info.displayName = QString::fromUtf8(displayName);
            info.deviceId = deviceId.isEmpty() ? info.displayName : deviceId;
            devices.append(info);

            LOG_INFO("[AudioEngine] Dispositivo: '{}' (id: '{}')",
                     info.displayName.toStdString(), info.deviceId.toStdString());

            g_free(displayName);
        }

        gst_object_unref(dev);
    }
    g_list_free(devList);

    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);

    LOG_INFO("[AudioEngine] {} dispositivos de audio encontrados", devices.size());
    return devices;
}

QVector<AudioEngine::AudioDeviceInfo> AudioEngine::availableInputSources()
{
    QVector<AudioDeviceInfo> devices;

    // Defecto 1.10: timeout reducido a 1500ms (antes 5000ms).
    // Si pactl no responde en 1.5s es porque pulseaudio/pipewire-pulse
    // está colgado o no instalado — caemos al fallback de GstDeviceMonitor
    // sin congelar la GUI durante 5 segundos enteros.
#ifndef _WIN32
    QProcess pactl;
    pactl.start("pactl", {"list", "sources"});

    bool pactlOk = pactl.waitForFinished(1500);
    if (pactlOk && pactl.exitCode() == 0) {
        QString output = QString::fromUtf8(pactl.readAllStandardOutput());
        QStringList lines = output.split('\n');

        QString currentName;
        for (const auto& line : lines) {
            QString trimmed = line.trimmed();

            // Nuevo bloque Source → resetear
            if (trimmed.startsWith("Source #")) {
                currentName.clear();
            }
            else if (trimmed.startsWith("Name:") || trimmed.startsWith("Nombre:")) {
                int colonPos = trimmed.indexOf(':');
                currentName = trimmed.mid(colonPos + 1).trimmed();
            }
            else if ((trimmed.startsWith("Description:") || trimmed.startsWith("Descripción:"))
                     && !currentName.isEmpty()) {
                int colonPos = trimmed.indexOf(':');
                QString desc = trimmed.mid(colonPos + 1).trimmed();

                AudioDeviceInfo info;
                info.deviceId = currentName;
                info.displayName = desc.isEmpty() ? currentName : desc;
                devices.append(info);

                LOG_INFO("[AudioEngine] Fuente: '{}' (id: '{}')",
                         info.displayName.toStdString(), info.deviceId.toStdString());
                currentName.clear();
            }
        }

        LOG_INFO("[AudioEngine] {} fuentes encontradas via pactl", devices.size());
    } else {
        // Si pactl no terminó dentro del timeout, matarlo para que no quede
        // huérfano consumiendo CPU
        if (!pactlOk) {
            pactl.kill();
            pactl.waitForFinished(500);
            LOG_WARN("[AudioEngine] pactl no respondió en 1500ms (timeout), "
                     "usando GstDeviceMonitor");
        } else {
            LOG_WARN("[AudioEngine] pactl falló (exit={}, stderr={}), usando GstDeviceMonitor",
                     pactl.exitCode(),
                     QString::fromUtf8(pactl.readAllStandardError()).toStdString());
        }
    }
#endif // _WIN32

    // Fallback: GstDeviceMonitor si pactl no funcionó
    if (devices.isEmpty()) {
        GError* initErr = nullptr;
        if (gst_init_check(nullptr, nullptr, &initErr)) {
            GstDeviceMonitor* monitor = gst_device_monitor_new();
            gst_device_monitor_add_filter(monitor, "Audio/Source", nullptr);
            if (gst_device_monitor_start(monitor)) {
                GList* devList = gst_device_monitor_get_devices(monitor);
                for (GList* it = devList; it; it = it->next) {
                    GstDevice* dev = GST_DEVICE(it->data);
                    gchar* dn = gst_device_get_display_name(dev);
                    GstStructure* props = gst_device_get_properties(dev);
                    QString deviceId;
                    if (props) {
                        const gchar* propNames[] = {"node.name", "device.name", nullptr};
                        for (int i = 0; propNames[i]; ++i) {
                            const gchar* val = gst_structure_get_string(props, propNames[i]);
                            if (val && val[0]) { deviceId = QString::fromUtf8(val); break; }
                        }
                        gst_structure_free(props);
                    }
                    if (dn) {
                        AudioDeviceInfo info;
                        info.displayName = QString::fromUtf8(dn);
                        info.deviceId = deviceId.isEmpty() ? info.displayName : deviceId;
                        devices.append(info);
                        g_free(dn);
                    }
                    gst_object_unref(dev);
                }
                g_list_free(devList);
                gst_device_monitor_stop(monitor);
            }
            gst_object_unref(monitor);
        } else {
            if (initErr) g_error_free(initErr);
        }
        LOG_INFO("[AudioEngine] GstDeviceMonitor fallback: {} fuentes", devices.size());
    }

    return devices;
}

QStringList AudioEngine::availableAudioDevices()
{
    QStringList list;
    auto detailed = availableAudioDevicesDetailed();
    for (const auto& d : detailed) {
        list << d.displayName;
    }
    return list;
}

void AudioEngine::setEqBands(const QVector<double>& gains)
{
    if (main_) main_->setEqBands(gains);
    if (mainAlt_) mainAlt_->setEqBands(gains);
    if (events_) events_->setEqBands(gains);
    if (pisador_) pisador_->setEqBands(gains);
}

void AudioEngine::setCompressorEnabled(bool enabled)
{
    if (main_) main_->setCompressorEnabled(enabled);
    if (mainAlt_) mainAlt_->setCompressorEnabled(enabled);
    if (events_) events_->setCompressorEnabled(enabled);
    if (pisador_) pisador_->setCompressorEnabled(enabled);
}

void AudioEngine::setCompressorThreshold(double db)
{
    if (main_) main_->setCompressorThreshold(db);
    if (mainAlt_) mainAlt_->setCompressorThreshold(db);
    if (events_) events_->setCompressorThreshold(db);
    if (pisador_) pisador_->setCompressorThreshold(db);
}

void AudioEngine::setCompressorRatio(double ratio)
{
    if (main_) main_->setCompressorRatio(ratio);
    if (mainAlt_) mainAlt_->setCompressorRatio(ratio);
    if (events_) events_->setCompressorRatio(ratio);
    if (pisador_) pisador_->setCompressorRatio(ratio);
}

} // namespace sara
