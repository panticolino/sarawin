#ifndef SARA_AUDIO_ENGINE_H
#define SARA_AUDIO_ENGINE_H

#include "audio/audio_pipeline.h"
#include "core/types.h"
#include <QObject>
#include <QVector>
#include <memory>

namespace sara {

/**
 * Motor de audio: gestiona los 3 pipelines de GStreamer.
 *
 * - Main:       Programación Principal (música continua)
 * - Events:     Publicidad/Eventos (interrumpe Main)
 * - InstantPlay: Asistente en Vivo (independiente)
 *
 * Inicializa GStreamer y crea los pipelines con el dispositivo de audio correcto.
 * En Fase 0 solo está activo el pipeline Main.
 */
class AudioEngine : public QObject
{
    Q_OBJECT

public:
    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    /// Inicializar GStreamer y crear pipelines.
    /// mainDevice: dispositivo para Main + Events (aire)
    /// cueDevice: dispositivo para preescucha CUE. Vacío = no usar.
    /// instantDevice: dispositivo para InstantPlay. Vacío = mismo que main.
    bool initialize(const QString& mainDevice = "default",
                    const QString& cueDevice = "",
                    const QString& instantDevice = "");

    /// Habilitar ReplayGain (llamar ANTES de initialize)
    void setReplayGainEnabled(bool enabled) { replayGainEnabled_ = enabled; }

    /// Habilitar procesamiento (EQ + Compresor) en pipelines Main (llamar ANTES de initialize)
    void setProcessingEnabled(bool enabled) { processingEnabled_ = enabled; }

    /// Aplicar EQ/compresor a los pipelines Main/MainAlt/Events
    void setEqBands(const QVector<double>& gains);
    void setCompressorEnabled(bool enabled);
    void setCompressorThreshold(double db);
    void setCompressorRatio(double ratio);

    /// Cambiar el dispositivo de un pipeline en caliente
    bool changeDevice(PipelineId pipeline, const QString& device);

    /// Obtener el dispositivo actual de un pipeline
    QString deviceForPipeline(PipelineId pipeline) const;

    /// Acceso a cada pipeline
    AudioPipeline* mainPipeline()    const { return main_.get(); }
    AudioPipeline* mainAltPipeline() const { return mainAlt_.get(); }
    AudioPipeline* eventsPipeline()  const { return events_.get(); }
    AudioPipeline* instantPipeline() const { return instant_.get(); }
    AudioPipeline* cuePipeline()     const { return cue_.get(); }
    AudioPipeline* pisadorPipeline() const { return pisador_.get(); }

    /// Deck A/B para crossfade solapado
    AudioPipeline* activeDeck() const { return deckIsA_ ? main_.get() : mainAlt_.get(); }
    AudioPipeline* standbyDeck() const { return deckIsA_ ? mainAlt_.get() : main_.get(); }
    void swapDecks() { deckIsA_ = !deckIsA_; }

    /// Volumen master (afecta a Main y Events, no a InstantPlay)
    void setMasterVolume(double volume);
    double masterVolume() const { return masterVolume_; }

    /// ¿GStreamer inicializado correctamente?
    bool isInitialized() const { return initialized_; }

    /// Información de un dispositivo de audio
    struct AudioDeviceInfo {
        QString displayName;  // "PCM2902 Audio Codec Estéreo analógico"
        QString deviceId;     // ID real para PulseAudio/ALSA (ej: "alsa_output.usb-...")
    };

    /// Listar dispositivos de audio disponibles
    static QStringList availableAudioDevices();
    static QVector<AudioDeviceInfo> availableAudioDevicesDetailed();
    static QVector<AudioDeviceInfo> availableInputSources();

signals:
    /// Reenviadas desde los pipelines para conveniencia
    void mainTrackFinished();
    void mainAltTrackFinished();
    void eventsTrackFinished();
    void instantTrackFinished();

private:
    std::unique_ptr<AudioPipeline> main_;
    std::unique_ptr<AudioPipeline> mainAlt_;
    std::unique_ptr<AudioPipeline> events_;
    std::unique_ptr<AudioPipeline> instant_;
    std::unique_ptr<AudioPipeline> cue_;
    std::unique_ptr<AudioPipeline> pisador_;

    double masterVolume_ = 0.75;
    bool   initialized_  = false;
    bool   deckIsA_      = true;
    bool   replayGainEnabled_ = false;
    bool   processingEnabled_ = false;

    // Dispositivo asignado a cada pipeline
    QString mainDevice_;
    QString eventsDevice_;
    QString instantDevice_;
    QString cueDevice_;
};

} // namespace sara

#endif // SARA_AUDIO_ENGINE_H
