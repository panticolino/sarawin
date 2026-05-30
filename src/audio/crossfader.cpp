#include "audio/crossfader.h"
#include "util/logger.h"

#include <algorithm>
#include <cmath>

namespace sara {

// ──────────────────────────────────────────────────────────────────
// Contrato con el caller:
//
//   fadeIn(p):      al terminar p->volume() == fromStartVol_ (= targetVolume_).
//                   El pipeline ya está reproduciendo y queda en su nivel final.
//
//   fadeOut(p):     al terminar p->volume() == 0. Emite fadeOutFinished().
//                   El caller DEBE detener el pipeline (stop/pause) y restaurar
//                   el volumen ANTES de la próxima reproducción. Esto evita el
//                   "pico" audible que se producía si el volumen se restauraba
//                   antes de que el caller corte la salida.
//
//   execute(f, t):  al terminar f está STOP-eado y su volumen restaurado a
//                   fromStartVol_ (listo para próxima reproducción); t queda
//                   sonando al volumen pleno (fromStartVol_). El stop() se
//                   ejecuta CON el volumen aún en 0 para no producir click.
// ──────────────────────────────────────────────────────────────────

Crossfader::Crossfader(QObject* parent)
    : QObject(parent)
{
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &Crossfader::tick);
}

Crossfader::~Crossfader()
{
    cancel();
}

// ══════════════════════════════════════════════════════════
// Fade-in: 0 → targetVolume sobre un solo pipeline
// ══════════════════════════════════════════════════════════

void Crossfader::fadeIn(AudioPipeline* pipeline)
{
    if (active_) cancel();
    if (!pipeline) return;

    if (durationMs_ <= 0) {
        pipeline->setVolume(targetVolume_);
        return;
    }

    to_ = pipeline;
    from_ = nullptr;
    fromStartVol_ = targetVolume_;
    elapsed_ = 0;
    mode_ = Mode::FadeIn;
    active_ = true;

    pipeline->setVolume(0.0);
    LOG_INFO("[Crossfader] Fade-in {}ms -> vol {:.2f}", durationMs_, targetVolume_);
    timer_->start(stepMs_);
}

// ══════════════════════════════════════════════════════════
// Fade-out: actual → 0 sobre un solo pipeline
//   AL TERMINAR: volumen queda en 0. Caller debe detener el pipeline
//   y restaurar el volumen antes del próximo play.
// ══════════════════════════════════════════════════════════

void Crossfader::fadeOut(AudioPipeline* pipeline)
{
    if (active_) cancel();
    if (!pipeline) return;

    if (durationMs_ <= 0) {
        pipeline->setVolume(0.0);
        emit fadeOutFinished();
        emit finished();
        return;
    }

    from_ = pipeline;
    to_ = nullptr;
    fromStartVol_ = pipeline->volume();
    elapsed_ = 0;
    mode_ = Mode::FadeOut;
    active_ = true;

    LOG_INFO("[Crossfader] Fade-out {}ms desde vol {:.2f}", durationMs_, fromStartVol_);
    timer_->start(stepMs_);
}

// ══════════════════════════════════════════════════════════
// Crossfade: from baja mientras to sube
// ══════════════════════════════════════════════════════════

void Crossfader::execute(AudioPipeline* from, AudioPipeline* to)
{
    if (active_) cancel();

    if (!from || !to) {
        LOG_WARN("[Crossfader] Pipelines nulos, cancelando");
        return;
    }

    from_ = from;
    to_   = to;
    fromStartVol_ = from->volume();
    elapsed_ = 0;
    mode_ = Mode::Crossfade;

    if (durationMs_ <= 0) {
        // Corte directo: bajar volumen ANTES de stop() para evitar click,
        // luego restaurar el volumen del 'from' para próximas reproducciones.
        from_->setVolume(0.0);
        from_->stop();
        from_->setVolume(fromStartVol_);
        to_->setVolume(fromStartVol_);
        LOG_INFO("[Crossfader] Corte directo (duracion 0)");
        active_ = false;
        mode_ = Mode::None;
        from_ = nullptr;
        to_ = nullptr;
        emit finished();
        return;
    }

    to_->setVolume(0.0);
    active_ = true;

    LOG_INFO("[Crossfader] Crossfade {}ms", durationMs_);
    timer_->start(stepMs_);
}

void Crossfader::cancel()
{
    if (timer_->isActive()) {
        timer_->stop();
    }
    active_ = false;
    mode_ = Mode::None;
    from_ = nullptr;
    to_ = nullptr;
}

// ══════════════════════════════════════════════════════════
// Tick: avanza el fade según el modo
// ══════════════════════════════════════════════════════════

void Crossfader::tick()
{
    elapsed_ += stepMs_;
    double progress = std::min(1.0, static_cast<double>(elapsed_) / durationMs_);

    // Curva S suave (smoothstep) para transiciones más naturales
    // f(x) = 3x^2 - 2x^3
    double smooth = progress * progress * (3.0 - 2.0 * progress);

    switch (mode_) {
    case Mode::FadeIn:
        if (to_) to_->setVolume(fromStartVol_ * smooth);
        break;

    case Mode::FadeOut:
        if (from_) from_->setVolume(fromStartVol_ * (1.0 - smooth));
        break;

    case Mode::Crossfade:
        if (from_) from_->setVolume(fromStartVol_ * (1.0 - smooth));
        if (to_)   to_->setVolume(fromStartVol_ * smooth);
        break;

    case Mode::None:
        break;
    }

    if (elapsed_ >= durationMs_) {
        timer_->stop();

        // Capturar referencias antes de limpiar el estado, para que
        // los signals se emitan con el estado interno ya consistente
        // (evita reentrada de cancel() desde slots conectados).
        Mode completedMode = mode_;
        AudioPipeline* fromPipe = from_;
        AudioPipeline* toPipe = to_;
        double startVol = fromStartVol_;

        active_ = false;
        mode_ = Mode::None;
        from_ = nullptr;
        to_ = nullptr;

        switch (completedMode) {
        case Mode::FadeIn:
            // Asegurar volumen final exacto
            if (toPipe) toPipe->setVolume(startVol);
            break;

        case Mode::FadeOut:
            // CONTRATO: dejamos el pipeline en volumen 0.
            // El caller (slot conectado a fadeOutFinished) es responsable
            // de detenerlo y restaurar el volumen antes del próximo play.
            // Esto evita el "pico" que se producía al restaurar el volumen
            // antes del stop() del caller.
            if (fromPipe) fromPipe->setVolume(0.0);
            break;

        case Mode::Crossfade:
            // El 'from' hizo fade a 0 — detenerlo CON el volumen en 0
            // para evitar click, y luego restaurar el volumen para la
            // próxima vez que se use este pipeline.
            if (fromPipe) {
                fromPipe->stop();
                fromPipe->setVolume(startVol);
            }
            // El 'to' ya está al volumen pleno por el último tick;
            // forzamos el valor exacto.
            if (toPipe) toPipe->setVolume(startVol);
            break;

        case Mode::None:
            break;
        }

        if (completedMode == Mode::FadeOut) {
            emit fadeOutFinished();
        }
        emit finished();
    }
}

} // namespace sara
