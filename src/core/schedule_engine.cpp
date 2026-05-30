#include "core/schedule_engine.h"
#include "data/schedule_repository.h"
#include "core/track_selector.h"
#include "core/time_announcer.h"
#include "util/metadata_reader.h"
#include "util/logger.h"

#include <QDate>
#include <QFileInfo>

namespace sara {

static const QStringList DAY_NAMES = {
    "Lunes", "Martes", "Miércoles", "Jueves",
    "Viernes", "Sábado", "Domingo"
};

ScheduleEngine::ScheduleEngine(QObject* parent)
    : QObject(parent)
{
    checkTimer_ = new QTimer(this);
    checkTimer_->setInterval(30000);  // Verificar cada 30 segundos
    connect(checkTimer_, &QTimer::timeout, this, &ScheduleEngine::checkSchedule);
}

ScheduleEngine::~ScheduleEngine()
{
    stop();
}

void ScheduleEngine::start()
{
    if (running_) return;
    running_ = true;

    // Verificación inicial
    checkSchedule();

    checkTimer_->start();
    LOG_INFO("[ScheduleEngine] Motor iniciado");
}

void ScheduleEngine::stop()
{
    if (!running_) return;
    running_ = false;
    checkTimer_->stop();
    LOG_INFO("[ScheduleEngine] Motor detenido");
}

// ══════════════════════════════════════════════════════════
// Verificación periódica de programación
// ══════════════════════════════════════════════════════════

void ScheduleEngine::checkSchedule()
{
    if (!repo_) return;

    auto slot = findActiveSlot();

    if (slot.has_value()) {
        // Hay un slot activo
        if (slot->scheduleId != activeScheduleId_) {
            // ¡Cambio de bloque!
            QString prevName = activeScheduleName_;
            activeScheduleId_ = slot->scheduleId;
            activeScheduleName_ = slot->scheduleName;
            currentSlotEnd_ = slot->slotEnd;
            cyclePosition_ = 0;  // Reiniciar ciclo al cambiar de bloque

            bool wasFallback = inFallback_;
            inFallback_ = false;

            LOG_INFO("[ScheduleEngine] Bloque activo: {} (hasta {})",
                     activeScheduleName_.toStdString(),
                     currentSlotEnd_.toString("HH:mm").toStdString());

            emit activeScheduleChanged(activeScheduleName_);

            if (wasFallback) {
                emit scheduleActivated(activeScheduleName_);
            }
        } else {
            // Mismo bloque, actualizar hora de fin (puede haber cambiado
            // si el operador editó la grilla en caliente)
            currentSlotEnd_ = slot->slotEnd;
        }
    } else {
        // No hay slot activo → fallback
        if (!inFallback_) {
            LOG_INFO("[ScheduleEngine] Sin programación activa, entrando en fallback");
            activeScheduleId_.clear();
            activeScheduleName_.clear();
            cyclePosition_ = 0;
            inFallback_ = true;
            emit activeScheduleChanged("");
            emit fallbackActivated();
        }
    }
}

// ══════════════════════════════════════════════════════════
// Resolución de pistas
// ══════════════════════════════════════════════════════════

void ScheduleEngine::requestNextTrack()
{
    auto resolved = resolveNextTrack();
    if (resolved) {
        emit nextTrackReady(
            resolved->filePath,
            resolved->displayName,
            resolved->source,
            resolved->durationMs
        );
    }
}

std::optional<ScheduleEngine::ResolvedTrack> ScheduleEngine::resolveNextTrack()
{
    if (!repo_ || !selector_) return std::nullopt;

    // ── Archivos pendientes de locución de hora ──────
    // Si hay archivos en la cola de locución, devolver el siguiente
    if (!pendingAnnouncementFiles_.isEmpty()) {
        QString file = pendingAnnouncementFiles_.takeFirst();
        ResolvedTrack rt;
        rt.filePath = file;
        rt.displayName = "Locución de hora";
        rt.source = inFallback_ ? "fallback" : "schedule:" + activeScheduleName_;
        if (reader_) {
            rt.durationMs = reader_->getDurationMs(file);
        } else {
            rt.durationMs = 0;
        }
        return rt;
    }

    // Primero: verificar si seguimos en el mismo slot o cambiamos
    checkSchedule();

    // ── Modo Fallback ────────────────────────────────
    if (inFallback_) {
        if (fallbackFolder_.isEmpty()) {
            LOG_WARN("[ScheduleEngine] Sin carpeta de respaldo configurada");
            return std::nullopt;
        }

        QString track = selector_->selectFromFolder(fallbackFolder_, "fallback");
        if (track.isEmpty()) return std::nullopt;

        ResolvedTrack rt;
        rt.filePath = track;
        rt.source = "fallback";

        // Leer metadata
        if (reader_) {
            auto meta = reader_->read(track);
            rt.durationMs = meta.durationMs;
            if (!meta.title.isEmpty()) {
                rt.displayName = meta.title;
                if (!meta.artist.isEmpty())
                    rt.displayName += " — " + meta.artist;
            } else {
                rt.displayName = QFileInfo(track).completeBaseName();
            }
        } else {
            rt.displayName = QFileInfo(track).completeBaseName();
            rt.durationMs = 0;
        }

        return rt;
    }

    // ── Modo Programación ────────────────────────────
    auto slot = findActiveSlot();
    if (!slot || slot->elements.isEmpty()) {
        // Sin elementos en este slot, fallback temporal
        if (!fallbackFolder_.isEmpty()) {
            inFallback_ = true;
            return resolveNextTrack();
        }
        return std::nullopt;
    }

    // Verificar frontera horaria: ¿estamos cerca del fin del slot?
    QTime now = QTime::currentTime();
    if (now >= currentSlotEnd_) {
        // Ya pasamos el fin del slot → forzar re-check
        LOG_INFO("[ScheduleEngine] Frontera horaria alcanzada ({}), recalculando...",
                 currentSlotEnd_.toString("HH:mm").toStdString());
        checkSchedule();
        // Recursión segura: checkSchedule cambió el estado
        return resolveNextTrack();
    }

    // Obtener el elemento actual del ciclo, con fallback si no se resuelve.
    // Intentamos hasta N veces (tamaño de la programación) antes de rendirse.
    int maxAttempts = slot->elements.size();

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        if (cyclePosition_ >= slot->elements.size()) {
            cyclePosition_ = 0;
        }

        const ScheduleElement& element = slot->elements[cyclePosition_];
        advanceCyclePosition();

        QString filePath = resolveElement(element);
        if (filePath.isEmpty()) {
            LOG_WARN("[ScheduleEngine] Elemento no resoluble: {} ({}), saltando...",
                     element.displayName.toStdString(),
                     elementTypeToString(element.type).toStdString());
            continue;  // Intentar siguiente elemento
        }

        // Éxito: construir resultado
        ResolvedTrack rt;
        rt.filePath = filePath;
        rt.source = "schedule:" + activeScheduleName_;

        // Marcador especial de locución de hora
        if (filePath == "__SARA_TIME_ANNOUNCE__") {
            rt.displayName = "🕐 Locución de hora";
            rt.durationMs = 0;
            return rt;
        }

        if (reader_) {
            auto meta = reader_->read(filePath);
            rt.durationMs = meta.durationMs;
            if (!meta.title.isEmpty()) {
                rt.displayName = meta.title;
                if (!meta.artist.isEmpty())
                    rt.displayName += " — " + meta.artist;
            } else {
                rt.displayName = QFileInfo(filePath).completeBaseName();
            }
        } else {
            rt.displayName = QFileInfo(filePath).completeBaseName();
            rt.durationMs = 0;
        }

        return rt;
    }

    // Todos los elementos fallaron → fallback
    LOG_WARN("[ScheduleEngine] Todos los elementos fallaron, usando fallback");
    if (!fallbackFolder_.isEmpty()) {
        inFallback_ = true;
        return resolveNextTrack();
    }
    return std::nullopt;
}

// ══════════════════════════════════════════════════════════
// Resolver elementos
// ══════════════════════════════════════════════════════════

QString ScheduleEngine::resolveElement(const ScheduleElement& element)
{
    switch (element.type) {
    case ElementType::Folder: {
        // Selección aleatoria con anti-repetición
        if (element.path.isEmpty()) return {};
        QString source = "schedule:" + activeScheduleName_;
        return selector_->selectFromFolder(element.path, source);
    }

    case ElementType::File: {
        // Archivo específico: verificar que existe
        if (QFileInfo::exists(element.path)) {
            return element.path;
        }
        LOG_WARN("[ScheduleEngine] Archivo no encontrado: {}", element.path.toStdString());
        return {};
    }

    case ElementType::TimeAnnounce: {
        // Devolver un marcador especial. La hora se resolverá al momento
        // de reproducir, no ahora (para que diga la hora correcta).
        if (announcer_ && announcer_->isConfigured()) {
            return "__SARA_TIME_ANNOUNCE__";
        }
        LOG_WARN("[ScheduleEngine] Locución de hora no configurada, saltando");
        return {};
    }

    case ElementType::Stream: {
        // Stream URL — se usa en Publicidad/Eventos, no aquí
        // Pero si alguien lo configura, intentar
        if (!element.path.isEmpty()) {
            return element.path;
        }
        return {};
    }
    }

    return {};
}

void ScheduleEngine::advanceCyclePosition()
{
    cyclePosition_++;
    // El wrap a 0 se hace en resolveNextTrack al verificar >= size
}

// ══════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════

std::optional<ScheduleEngine::ActiveSlotInfo> ScheduleEngine::findActiveSlot()
{
    if (!repo_) return std::nullopt;

    QString today = currentDayName();
    QTime now = QTime::currentTime();

    auto slot = repo_->getActiveSlot(today, now);
    if (!slot) return std::nullopt;

    auto sched = repo_->getById(slot->scheduleId);
    if (!sched) return std::nullopt;

    ActiveSlotInfo info;
    info.scheduleId = slot->scheduleId;
    info.scheduleName = sched->name;
    info.slotStart = slot->startTime;
    info.slotEnd = slot->endTime;
    info.elements = sched->elements;

    return info;
}

QString ScheduleEngine::currentDayName() const
{
    // Qt: Monday=1 ... Sunday=7
    int dow = QDate::currentDate().dayOfWeek();
    if (dow >= 1 && dow <= 7) {
        return DAY_NAMES[dow - 1];
    }
    return DAY_NAMES[0];
}

} // namespace sara
