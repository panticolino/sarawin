#ifndef SARA_AUDIO_CROSSFADER_H
#define SARA_AUDIO_CROSSFADER_H

#include "audio/audio_pipeline.h"
#include <QObject>
#include <QTimer>

namespace sara {

/**
 * Crossfader y fader de audio.
 *
 * Tres modos de operación:
 *
 * 1. fadeIn(pipeline)  — Sube el volumen de 0 a targetVolume (inicio de pista)
 * 2. fadeOut(pipeline)  — Baja el volumen de actual a 0 (fin de pista)
 * 3. execute(from, to) — Crossfade: baja from mientras sube to (música ↔ publicidad)
 *
 * Usa rampas de volumen con timer preciso para transiciones suaves.
 */
class Crossfader : public QObject
{
    Q_OBJECT

public:
    explicit Crossfader(QObject* parent = nullptr);
    ~Crossfader() override;

    /// Duración del crossfade/fade en milisegundos
    void setDurationMs(int ms) { durationMs_ = ms; }
    int durationMs() const { return durationMs_; }

    /// Volumen objetivo al que llega el fade-in (volumen master)
    void setTargetVolume(double vol) { targetVolume_ = vol; }
    double targetVolume() const { return targetVolume_; }

    /// Fade-in: sube volumen de 0 a targetVolume
    void fadeIn(AudioPipeline* pipeline);

    /// Fade-out: baja volumen de actual a 0
    void fadeOut(AudioPipeline* pipeline);

    /// Crossfade entre dos pipelines (from baja, to sube)
    void execute(AudioPipeline* from, AudioPipeline* to);

    /// ¿Hay un fade en curso?
    bool isActive() const { return active_; }

    /// Cancelar fade en curso
    void cancel();

signals:
    void finished();
    void fadeOutFinished();   // Específico: el fade-out terminó (hora de poner la siguiente pista)

private:
    enum class Mode { None, FadeIn, FadeOut, Crossfade };

    void tick();

    int    durationMs_   = 3000;
    double targetVolume_ = 0.75;
    bool   active_       = false;
    Mode   mode_         = Mode::None;

    AudioPipeline* from_ = nullptr;
    AudioPipeline* to_   = nullptr;

    double fromStartVol_ = 1.0;
    int    elapsed_      = 0;
    int    stepMs_       = 50;

    QTimer* timer_ = nullptr;
};

} // namespace sara

#endif
