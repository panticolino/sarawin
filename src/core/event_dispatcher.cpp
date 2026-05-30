#include "core/event_dispatcher.h"
#include "data/event_repository.h"
#include "audio/audio_engine.h"
#include "audio/audio_pipeline.h"
#include "audio/crossfader.h"
#include "core/track_selector.h"
#include "core/time_announcer.h"
#include "data/audit_manager.h"
#include "util/metadata_reader.h"
#include "util/file_scanner.h"
#include "util/logger.h"
#include "util/qt_connect_helpers.h"

#include <QDate>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include <QRandomGenerator>

namespace sara {

static const QStringList DAY_NAMES = {
    "Lunes", "Martes", "Miércoles", "Jueves",
    "Viernes", "Sábado", "Domingo"
};

EventDispatcher::EventDispatcher(QObject* parent)
    : QObject(parent)
{
    checkTimer_ = new QTimer(this);
    checkTimer_->setInterval(15000);  // Verificar cada 15 segundos
    connect(checkTimer_, &QTimer::timeout, this, &EventDispatcher::checkEvents);
}

EventDispatcher::~EventDispatcher()
{
    stop();
}

void EventDispatcher::start()
{
    if (running_) return;
    running_ = true;

    // Limpiar registros de días anteriores
    if (repo_) repo_->cleanOldPlayedEvents();

    // Resetear tracking diario en memoria
    expiredToday_.clear();
    playedTrackingDate_ = QDate::currentDate();

    // Recuperar eventos persistentes que no se emitieron (ej: tras corte de luz)
    recoverPersistentEvents();

    checkTimer_->start();
    LOG_INFO("[EventDispatcher] Iniciado");
}

void EventDispatcher::stop()
{
    if (!running_) return;
    running_ = false;
    checkTimer_->stop();
    if (streamTimer_) streamTimer_->stop();
    if (streamRetryTimer_) streamRetryTimer_->stop();
    pendingEventQueue_.clear();
    LOG_INFO("[EventDispatcher] Detenido");
}

// ══════════════════════════════════════════════════════════
// Verificación periódica
// ══════════════════════════════════════════════════════════

void EventDispatcher::checkEvents()
{
    if (!repo_ || !engine_ || eventPlaying_ || waitingForMainToFinish_) return;

    // Si hay eventos en cola de un ciclo anterior, despacharlos primero
    if (!pendingEventQueue_.isEmpty()) {
        QueuedEvent first = pendingEventQueue_.dequeue();
        LOG_INFO("[EventDispatcher] Despachando evento pendiente en cola: {}",
                 first.event.name.toStdString());
        triggerEvent(first.event, first.scheduledTime);
        return;
    }

    QString today = currentDayName();
    QTime now = QTime::currentTime();
    QTime currentMinute(now.hour(), now.minute());

    if (currentMinute != lastCheckMinute_) {
        triggeredThisMinute_.clear();
        lastCheckMinute_ = currentMinute;
    }

    // Resetear tracking si cambio de dia
    if (playedTrackingDate_ != QDate::currentDate()) {
        playedToday_.clear();
        expiredToday_.clear();
        playedTrackingDate_ = QDate::currentDate();
    }

    // ═══════════════════════════════════════════════════
    // 1. Buscar eventos para este minuto exacto
    //    Todos los tipos se encolan (persistente, con/sin maxWait)
    // ═══════════════════════════════════════════════════
    auto events = repo_->getEventsAt(today, currentMinute);
    for (const auto& event : events) {
        if (triggeredThisMinute_.contains(event.id)) continue;
        if (repo_->wasPlayedToday(event.id, currentMinute)) continue;
        if (!event.isValidToday()) continue;

        auto validElements = repo_->getValidElements(event.id);
        if (validElements.isEmpty()) {
            triggeredThisMinute_.insert(event.id);
            continue;
        }

        triggeredThisMinute_.insert(event.id);
        pendingEventQueue_.enqueue({event, currentMinute});
    }

    // ═══════════════════════════════════════════════════
    // 2. Detectar eventos vencidos (no-persistentes con maxWait que superaron su ventana)
    //    Los persistentes y los sin maxWait NO se marcan como vencidos aqui
    // ═══════════════════════════════════════════════════
    auto allDaySlots = repo_->getSlotsForDay(today);
    for (const auto& slot : allDaySlots) {
        if (!slot.enabled) continue;
        if (slot.triggerTime >= now) continue;

        auto event = repo_->getById(slot.eventId);
        if (!event || !event->isValidToday()) continue;
        if (event->persistent) continue;  // Persistentes nunca vencen

        QString expKey = event->id + "@" + slot.triggerTime.toString("HH:mm");
        if (expiredToday_.contains(expKey)) continue;
        if (repo_->wasPlayedToday(event->id, slot.triggerTime)) continue;

        int minutesLate = slot.triggerTime.secsTo(now) / 60;

        // Solo marcar vencidos los que tienen maxWait y lo superaron
        if (event->maxWaitMinutes > 0 && minutesLate > event->maxWaitMinutes) {
            repo_->recordExpired(event->id, event->name, slot.triggerTime, "timeout");
            expiredToday_.insert(expKey);
            LOG_INFO("[EventDispatcher] Evento '{}' vencido ({} min > {} max)",
                     event->name.toStdString(), minutesLate, event->maxWaitMinutes);
            emit eventExpired();
        }
        // Los no-persistentes sin maxWait NO se marcan vencidos aqui.
        // Se emitieron a su hora exacta o no se emitieron.
        // Solo se registran como vencidos si no fueron encolados a su hora
        // (ej: SARA no estaba corriendo). Eso lo maneja recoverPersistentEvents.
    }

    // ═══════════════════════════════════════════════════
    // Disparar el primer evento de la cola
    // ═══════════════════════════════════════════════════
    if (pendingEventQueue_.isEmpty()) return;

    LOG_INFO("[EventDispatcher] {} evento(s) encolado(s)",
             pendingEventQueue_.size());

    QueuedEvent first = pendingEventQueue_.dequeue();
    triggerEvent(first.event, first.scheduledTime);
}

// ══════════════════════════════════════════════════════════
// Recuperacion de eventos persistentes (tras corte de luz)
// ══════════════════════════════════════════════════════════

void EventDispatcher::recoverPersistentEvents()
{
    if (!repo_) return;

    QString today = currentDayName();
    QTime now = QTime::currentTime();

    LOG_INFO("[EventDispatcher] Recuperación de persistentes: día={}, hora={}",
             today.toStdString(), now.toString("HH:mm").toStdString());

    auto allDaySlots = repo_->getSlotsForDay(today);
    LOG_INFO("[EventDispatcher] Slots del día: {}", allDaySlots.size());

    QVector<QueuedEvent> toRecover;

    for (const auto& slot : allDaySlots) {
        if (!slot.enabled) continue;
        if (slot.triggerTime >= now) continue;

        auto event = repo_->getById(slot.eventId);
        if (!event || !event->isValidToday()) {
            LOG_DEBUG("[EventDispatcher] Slot {} descartado: evento inválido o no vigente hoy",
                      slot.eventId.toStdString());
            continue;
        }
        if (!event->persistent) {
            LOG_DEBUG("[EventDispatcher] Slot '{}' a las {} NO es persistente, saltando",
                      event->name.toStdString(),
                      slot.triggerTime.toString("HH:mm").toStdString());
            continue;
        }

        QString key = event->id + "@" + slot.triggerTime.toString("HH:mm");
        if (repo_->wasPlayedToday(event->id, slot.triggerTime)) {
            LOG_DEBUG("[EventDispatcher] Evento '{}' a las {} ya emitido hoy (BD), saltando",
                      event->name.toStdString(),
                      slot.triggerTime.toString("HH:mm").toStdString());
            continue;
        }

        auto validElements = repo_->getValidElements(event->id);
        if (validElements.isEmpty()) {
            LOG_DEBUG("[EventDispatcher] Evento '{}' sin elementos válidos",
                      event->name.toStdString());
            continue;
        }

        toRecover.append({*event, slot.triggerTime});
        LOG_INFO("[EventDispatcher] Recuperando evento persistente: '{}' (programado {})",
                 event->name.toStdString(),
                 slot.triggerTime.toString("HH:mm").toStdString());
    }

    // Ordenar por prioridad descendente
    std::sort(toRecover.begin(), toRecover.end(),
              [](const QueuedEvent& a, const QueuedEvent& b) {
        return a.event.priority < b.event.priority;
    });

    for (const auto& qe : toRecover) {
        pendingEventQueue_.enqueue(qe);
    }

    if (!toRecover.isEmpty()) {
        LOG_INFO("[EventDispatcher] {} evento(s) persistente(s) recuperados para emitir",
                 toRecover.size());
        // Disparar el primero con un breve retraso para que MainWindow termine de iniciar
        QTimer::singleShot(2000, this, [this]() {
            if (pendingEventQueue_.isEmpty() || eventPlaying_) return;
            QueuedEvent first = pendingEventQueue_.dequeue();
            LOG_INFO("[EventDispatcher] Emitiendo evento persistente recuperado: {}",
                     first.event.name.toStdString());
            triggerEvent(first.event, first.scheduledTime);
        });
    }
}

// ══════════════════════════════════════════════════════════
// Disparo de evento
// ══════════════════════════════════════════════════════════

void EventDispatcher::triggerEvent(const Event& event)
{
    triggerEvent(event, QTime::currentTime());
}

void EventDispatcher::triggerEvent(const Event& event, const QTime& scheduledTime)
{
    LOG_INFO("[EventDispatcher] Disparando evento: {} (prioridad {}, {})",
             event.name.toStdString(), event.priority,
             event.immediate ? "inmediato" : "retardado");

    currentEventId_ = event.id;
    currentEventName_ = event.name;
    currentElements_ = repo_->getValidElements(event.id);
    currentElementIndex_ = 0;
    currentImmediate_ = event.immediate;
    currentUseAdAnnounce_ = event.useAdAnnounce;
    currentEventScheduledTime_ = scheduledTime;

    // Si tiene locución de inicio/fin, inyectarla en la lista de elementos
    if (event.useAdAnnounce) {
        if (!adIntroFile_.isEmpty() && QFileInfo::exists(adIntroFile_)) {
            EventElement intro;
            intro.type = ElementType::File;
            intro.path = adIntroFile_;
            intro.displayName = "Inicio espacio publicitario";
            currentElements_.prepend(intro);
        }
        if (!adOutroFile_.isEmpty() && QFileInfo::exists(adOutroFile_)) {
            EventElement outro;
            outro.type = ElementType::File;
            outro.path = adOutroFile_;
            outro.displayName = "Fin espacio publicitario";
            currentElements_.append(outro);
        }
    }

    // Conectar señal de fin de pista del pipeline de eventos
    auto* eventsPipeline = engine_->eventsPipeline();
    disconnect(eventsPipeline, &AudioPipeline::trackFinished, this, nullptr);
    connect(eventsPipeline, &AudioPipeline::trackFinished,
            this, &EventDispatcher::onEventTrackFinished);

    if (event.immediate) {
        // INMEDIATO: fade-out música → reproducir evento sin fade-in
        eventPlaying_ = true;

        // Marcar como emitido hoy
        QString playedKey = currentEventId_ + "@" + currentEventScheduledTime_.toString("HH:mm");
        playedToday_.insert(playedKey);
        if (repo_) repo_->recordPlayed(currentEventId_, currentEventScheduledTime_);

        emit eventStarted(currentEventName_);

        auto* activeDeck = engine_->activeDeck();
        if (fadeOutEnabled_ && fader_ && activeDeck->isPlaying()) {
            // Guardar duración original del crossfader y usar fadeOutMs
            int originalMs = fader_->durationMs();
            fader_->setDurationMs(fadeOutMs_);

            // connectOnce: equivalente a Qt::SingleShotConnection (Qt 6.6+),
            // pero compatible con Qt 6.4 (Debian 12 / Devuan 5).
            connectOnce(fader_, &Crossfader::fadeOutFinished,
                        this, [this, activeDeck, originalMs]() {
                // Restaurar duración original del crossfader
                fader_->setDurationMs(originalMs);
                // El crossfader dejó volumen=0; restaurar al volumen master
                // ANTES del pause/resume (contrato de fadeOut).
                activeDeck->pause();
                activeDeck->setVolume(engine_->masterVolume());
                QTimer::singleShot(0, this, [this]() {
                    playNextEventElement();
                });
            });
            fader_->fadeOut(activeDeck);
        } else {
            if (activeDeck->isPlaying()) activeDeck->pause();
            playNextEventElement();
        }
    } else {
        // RETARDADO: esperar a que termine la pista actual en Main
        auto* activeDeck = engine_->activeDeck();
        if (!activeDeck->isPlaying()) {
            // Main ya esta detenido, reproducir inmediatamente
            eventPlaying_ = true;
            QString playedKey = currentEventId_ + "@" + currentEventScheduledTime_.toString("HH:mm");
            playedToday_.insert(playedKey);
            if (repo_) repo_->recordPlayed(currentEventId_, currentEventScheduledTime_);
            emit eventStarted(currentEventName_);
            playNextEventElement();
        } else {
            // Marcar que estamos esperando; MainWindow llamará
            // notifyMainTrackFinished() cuando la pista termine
            waitingForMainToFinish_ = true;
            LOG_INFO("[EventDispatcher] Esperando fin de pista actual para evento '{}'",
                     currentEventName_.toStdString());
        }
    }
}

void EventDispatcher::playEventManually(const QString& eventId)
{
    if (eventPlaying_) {
        LOG_WARN("[EventDispatcher] Ya hay un evento en reproducción");
        return;
    }

    auto event = repo_->getById(eventId);
    if (!event) return;

    // Forzar como inmediato en modo manual
    Event manualEvent = *event;
    manualEvent.immediate = true;
    triggerEvent(manualEvent);
}

bool EventDispatcher::notifyMainTrackFinished()
{
    if (!waitingForMainToFinish_) return false;

    waitingForMainToFinish_ = false;

    // Verificar si el evento retardado aun es valido (maxWait check)
    if (!currentEventId_.isEmpty() && repo_) {
        auto event = repo_->getById(currentEventId_);
        if (event && !event->persistent && event->maxWaitMinutes > 0) {
            QTime now = QTime::currentTime();
            int minutesLate = currentEventScheduledTime_.secsTo(now) / 60;
            if (minutesLate > event->maxWaitMinutes) {
                repo_->recordExpired(event->id, event->name,
                                     currentEventScheduledTime_, "timeout");
                QString key = event->id + "@" + currentEventScheduledTime_.toString("HH:mm");
                expiredToday_.insert(key);

                LOG_INFO("[EventDispatcher] Evento retardado \'{}\' vencido ({} min > {} max)",
                         event->name.toStdString(), minutesLate, event->maxWaitMinutes);
                emit eventExpired();

                currentEventId_.clear();
                currentEventName_.clear();

                if (!pendingEventQueue_.isEmpty()) {
                    QueuedEvent next = pendingEventQueue_.dequeue();
                    triggerEvent(next.event, next.scheduledTime);
                    return true;
                }
                return false;
            }
        }
    }

    eventPlaying_ = true;

    // Marcar como emitido hoy
    QString playedKey = currentEventId_ + "@" + currentEventScheduledTime_.toString("HH:mm");
    playedToday_.insert(playedKey);
    if (repo_) repo_->recordPlayed(currentEventId_, currentEventScheduledTime_);

    LOG_INFO("[EventDispatcher] Pista termino, arrancando evento retardado: {}",
             currentEventName_.toStdString());

    emit eventStarted(currentEventName_);
    playNextEventElement();

    return true;
}

void EventDispatcher::forceStopEvent()
{
    if (!eventPlaying_ && !waitingForMainToFinish_) return;

    LOG_INFO("[EventDispatcher] Forzando detención del evento: {}",
             currentEventName_.toStdString());

    // Detener stream timer
    if (streamTimer_ && streamTimer_->isActive()) {
        streamTimer_->stop();
    }
    if (streamRetryTimer_ && streamRetryTimer_->isActive()) {
        streamRetryTimer_->stop();
    }

    // Detener pipeline de eventos
    if (engine_) {
        auto* eventsPipeline = engine_->eventsPipeline();
        if (eventsPipeline) {
            disconnect(eventsPipeline, &AudioPipeline::trackFinished, this, nullptr);
            disconnect(eventsPipeline, &AudioPipeline::errorOccurred, this, nullptr);
            eventsPipeline->stop();
        }
    }

    // Limpiar todo el estado
    QString finishedName = currentEventName_;
    eventPlaying_ = false;
    waitingForMainToFinish_ = false;
    streamReconnecting_ = false;
    streamConnected_ = false;
    currentEventId_.clear();
    currentEventName_.clear();
    currentElements_.clear();
    currentElementIndex_ = 0;
    currentImmediate_ = false;
    pendingEventQueue_.clear();

    emit eventFinished(finishedName);
}

// ══════════════════════════════════════════════════════════
// Reproducción de elementos del evento
// ══════════════════════════════════════════════════════════

void EventDispatcher::playNextEventElement()
{
    if (currentElementIndex_ >= currentElements_.size()) {
        finishEvent();
        return;
    }

    // COPIA, no referencia: resolveEventElement() puede insertar elementos
    // en currentElements_ (locuciones de hora), invalidando cualquier referencia
    const auto element = currentElements_[currentElementIndex_];
    currentElementIndex_++;

    // Detener timer de stream anterior si existe
    if (streamTimer_) {
        streamTimer_->stop();
    }
    if (streamStabilizedTimer_) {
        streamStabilizedTimer_->stop();
    }
    // Avanzar elemento implica abandonar cualquier reconexión anterior
    streamReconnecting_ = false;
    streamConnected_ = false;

    QString filePath = resolveEventElement(element);

    if (filePath.isEmpty()) {
        LOG_WARN("[EventDispatcher] Elemento no resoluble: {}, saltando",
                 element.displayName.toStdString());
        playNextEventElement();
        return;
    }

    auto* eventsPipeline = engine_->eventsPipeline();

    // Si es un stream, usar el sistema de reintentos
    if (element.type == ElementType::Stream) {
        streamRetryCount_ = 0;
        streamRetryElement_ = element;
        streamRetryUrl_ = filePath;
        attemptStreamPlay(element, filePath);
        return;
    }

    eventsPipeline->play(filePath);

    // Registrar en auditoría
    if (audit_) {
        QString displayName = element.displayName.isEmpty()
            ? QFileInfo(filePath).completeBaseName() : element.displayName;
        audit_->recordPlay(filePath, displayName, "event:" + currentEventName_, 0, "events");
    }

    LOG_INFO("[EventDispatcher] Reproduciendo: {} [{}/{}]",
             QFileInfo(filePath).fileName().toStdString(),
             currentElementIndex_, currentElements_.size());

    QString elName = element.displayName.isEmpty()
        ? QFileInfo(filePath).completeBaseName() : element.displayName;
    emit elementPlaying(elName, currentEventName_);
}

void EventDispatcher::attemptStreamPlay(const EventElement& element, const QString& url)
{
    streamRetryCount_++;

    LOG_INFO("[EventDispatcher] Stream intento {}/{}: {} → {}",
             streamRetryCount_, STREAM_MAX_RETRIES,
             element.displayName.toStdString(), url.toStdString());

    auto* eventsPipeline = engine_->eventsPipeline();

    // Estado al iniciar un nuevo intento:
    //   - reconnecting = false: estamos intentando, no recuperando todavía
    //   - connected = false: el stream aún no se estabilizó
    streamReconnecting_ = false;
    streamConnected_ = false;

    // Detener cualquier timer de estabilización previo (de un intento anterior)
    if (streamStabilizedTimer_ && streamStabilizedTimer_->isActive()) {
        streamStabilizedTimer_->stop();
    }

    // ── Único handler de error ────────────────────────────────
    // Tanto si el fallo es durante la conexión inicial como en medio de
    // transmisión, el flujo es idéntico: handleStreamFailure() decide.
    // Esto reemplaza los dos handlers anteriores (uno para conexión inicial,
    // otro para reconexión post-estabilización) que tenían lógica casi
    // duplicada y dejaban una ventana de race condition al cambiar entre
    // uno y otro tras los 5s.
    disconnect(eventsPipeline, &AudioPipeline::errorOccurred, this, nullptr);
    connect(eventsPipeline, &AudioPipeline::errorOccurred, this,
            [this](const QString& msg) {
        handleStreamFailure(msg);
    });

    bool ok = eventsPipeline->play(url);

    if (ok) {
        QString elName = element.displayName.isEmpty()
            ? QFileInfo(url).completeBaseName() : element.displayName;
        emit elementPlaying(elName, currentEventName_);
        emit streamElementStarted(element.durationMs);

        // Marcar conexión estabilizada después de STREAM_STABILIZE_MS.
        // Si llega un error antes, sigue contando como "intento fallido"
        // (incrementa streamRetryCount_); si llega después, se reinicia
        // el contador para reconexiones de stream caído en medio.
        if (!streamStabilizedTimer_) {
            streamStabilizedTimer_ = new QTimer(this);
            streamStabilizedTimer_->setSingleShot(true);
            connect(streamStabilizedTimer_, &QTimer::timeout, this, [this]() {
                if (eventPlaying_) {
                    streamConnected_ = true;
                    LOG_DEBUG("[EventDispatcher] Stream estabilizado");
                }
            });
        }
        streamStabilizedTimer_->start(STREAM_STABILIZE_MS);

        // Iniciar timer de duración si aplica
        if (element.durationMs > 0) {
            if (!streamTimer_) {
                streamTimer_ = new QTimer(this);
                streamTimer_->setSingleShot(true);
                connect(streamTimer_, &QTimer::timeout, this, [this]() {
                    LOG_INFO("[EventDispatcher] Stream finalizado por duración");
                    auto* ep = engine_->eventsPipeline();
                    // Registrar finalización en auditoría
                    if (audit_) {
                        QString uri = ep->currentUri();
                        QString filePath = QUrl(uri).toLocalFile();
                        if (filePath.isEmpty()) filePath = uri;
                        audit_->recordFinish(filePath, ep->durationMs());
                    }
                    ep->stop();
                    playNextEventElement();
                });
            }
            streamTimer_->start(static_cast<int>(element.durationMs));
        }

        LOG_INFO("[EventDispatcher] Stream conectado: {} (duración: {}s) [{}/{}]",
                 element.displayName.toStdString(),
                 element.durationMs / 1000,
                 currentElementIndex_, currentElements_.size());

        // Registrar en auditoría
        if (audit_) {
            audit_->recordPlay(url, element.displayName,
                              "event:" + currentEventName_, element.durationMs, "events");
        }
    } else {
        // Fallo inmediato (play() retornó false). Procesar como cualquier
        // otro fallo; handleStreamFailure decide reintentar o saltar.
        LOG_WARN("[EventDispatcher] Stream fallo inmediato (intento {}/{})",
                 streamRetryCount_, STREAM_MAX_RETRIES);
        handleStreamFailure(QStringLiteral("play() returned false"));
    }
}

// ──────────────────────────────────────────────────────────
// Punto único de gestión de fallo de stream.
//
// Es el ÚNICO lugar donde se decide qué hacer cuando algo va mal:
// - Se llama desde el handler de errorOccurred del pipeline
// - Se llama desde onEventTrackFinished cuando hay timer activo
//   (significa que el stream terminó antes de tiempo)
// - Se llama desde attemptStreamPlay si play() retorna false
//
// Idempotente vía streamReconnecting_: si se llama varias veces casi
// simultáneamente para el mismo fallo (GStreamer emite a veces 2+
// mensajes de error para una sola desconexión HTTP), las llamadas
// posteriores son ignoradas.
// ──────────────────────────────────────────────────────────

void EventDispatcher::handleStreamFailure(const QString& reason)
{
    if (!eventPlaying_) return;

    // Idempotencia: si ya estamos procesando un fallo, ignorar el resto
    // de la cascada. El primer error programó (o programará) el retry o
    // el skip; las emisiones subsecuentes son ruido.
    if (streamReconnecting_) {
        LOG_DEBUG("[EventDispatcher] Fallo de stream ignorado (ya en gestión): {}",
                  reason.toStdString());
        return;
    }
    streamReconnecting_ = true;

    auto* ep = engine_->eventsPipeline();

    // Desconectar el handler de error AHORA — con streamReconnecting_=true
    // los errores residuales serían ignorados de todas formas, pero
    // desconectar evita el sobrecosto del Qt connection.
    disconnect(ep, &AudioPipeline::errorOccurred, this, nullptr);

    // Cancelar timer de estabilización (si estaba pendiente)
    if (streamStabilizedTimer_ && streamStabilizedTimer_->isActive()) {
        streamStabilizedTimer_->stop();
    }

    // Detener pipeline de inmediato para liberar el socket HTTP
    ep->stop();

    // Determinar tiempo restante del stream (si aplica)
    int64_t remainingMs = 0;
    bool hadStreamTimer = (streamTimer_ && streamTimer_->isActive());
    if (hadStreamTimer) {
        remainingMs = static_cast<int64_t>(streamTimer_->remainingTime());
        streamTimer_->stop();
    }

    // ── Caso A: el stream estaba estabilizado (>5s sonando) ──
    // → Es una desconexión en medio de transmisión. Reiniciar contador
    //   de reintentos (queremos darle 3 oportunidades nuevas).
    if (streamConnected_) {
        LOG_WARN("[EventDispatcher] Stream desconectado en medio de transmisión "
                 "(restaban {}s): {}",
                 remainingMs / 1000, reason.toStdString());

        if (remainingMs > 5000) {
            streamRetryElement_.durationMs = remainingMs;
            streamRetryCount_ = 0;
            streamConnected_ = false;
            // Programar reintento con DELAY (no inmediato).
            // Esto es crítico: sin el delay, GStreamer y libsoup intentan
            // reconectar demasiado rápido y agotan recursos.
            if (!streamRetryTimer_) {
                streamRetryTimer_ = new QTimer(this);
                streamRetryTimer_->setSingleShot(true);
                connect(streamRetryTimer_, &QTimer::timeout, this, [this]() {
                    attemptStreamPlay(streamRetryElement_, streamRetryUrl_);
                });
            }
            LOG_INFO("[EventDispatcher] Reintentando en {}ms...", STREAM_RETRY_DELAY_MS);
            streamRetryTimer_->start(STREAM_RETRY_DELAY_MS);
        } else {
            LOG_INFO("[EventDispatcher] Stream: quedan menos de 5s, continuando");
            streamReconnecting_ = false;
            playNextEventElement();
        }
        return;
    }

    // ── Caso B: el stream NO se llegó a estabilizar ──
    // → Conexión fallida desde el inicio. Contar contra el límite.
    LOG_WARN("[EventDispatcher] Stream error (intento {}/{}): {}",
             streamRetryCount_, STREAM_MAX_RETRIES, reason.toStdString());

    if (streamRetryCount_ < STREAM_MAX_RETRIES) {
        // Reintento normal con delay
        if (!streamRetryTimer_) {
            streamRetryTimer_ = new QTimer(this);
            streamRetryTimer_->setSingleShot(true);
            connect(streamRetryTimer_, &QTimer::timeout, this, [this]() {
                attemptStreamPlay(streamRetryElement_, streamRetryUrl_);
            });
        }
        LOG_INFO("[EventDispatcher] Reintentando en {}ms...", STREAM_RETRY_DELAY_MS);
        streamRetryTimer_->start(STREAM_RETRY_DELAY_MS);
    } else {
        // Agotados los reintentos: saltar al siguiente elemento del evento
        LOG_ERROR("[EventDispatcher] Stream falló después de {} intentos, saltando: {}",
                  STREAM_MAX_RETRIES, streamRetryElement_.displayName.toStdString());
        streamReconnecting_ = false;
        playNextEventElement();
    }
}

void EventDispatcher::onEventTrackFinished()
{
    if (!eventPlaying_) return;

    // Si ya hay una gestión de fallo en curso (errorOccurred llegó primero),
    // ignorar — handleStreamFailure ya está actuando.
    if (streamReconnecting_) {
        LOG_DEBUG("[EventDispatcher] onEventTrackFinished ignorado (gestión de fallo en curso)");
        return;
    }

    // Registrar finalización en auditoría
    if (audit_ && engine_) {
        auto* ep = engine_->eventsPipeline();
        QString uri = ep->currentUri();
        if (!uri.isEmpty()) {
            QString filePath = QUrl(uri).toLocalFile();
            if (filePath.isEmpty()) filePath = uri;  // Para streams (no es archivo local)
            audit_->recordFinish(filePath, ep->durationMs());
        }
    }

    // Si el stream timer está activo, el stream se cortó ANTES de cumplir
    // su duración programada. Delegar al gestor de fallos central, que
    // decidirá según streamConnected_ si reintenta o salta.
    if (streamTimer_ && streamTimer_->isActive()) {
        handleStreamFailure(QStringLiteral("trackFinished antes del timer"));
        return;
    }

    playNextEventElement();
}

QString EventDispatcher::resolveEventElement(const EventElement& element)
{
    switch (element.type) {
    case ElementType::File:
        if (QFileInfo::exists(element.path)) return element.path;
        LOG_WARN("[EventDispatcher] Archivo no encontrado: {}", element.path.toStdString());
        return {};

    case ElementType::Folder:
        if (selector_ && !element.path.isEmpty()) {
            return selector_->selectFromFolder(element.path, "event:" + currentEventName_);
        }
        return {};

    case ElementType::TimeAnnounce:
        if (announcer_ && announcer_->isConfigured()) {
            auto files = announcer_->generateForCurrentTime();
            if (!files.isEmpty()) {
                // Insertar los archivos restantes (minutos, sufijo) como elementos
                // temporales en la posición actual de la cola
                for (int i = files.size() - 1; i >= 1; --i) {
                    EventElement extra;
                    extra.type = ElementType::File;
                    extra.path = files[i];
                    extra.displayName = "Locución de hora (cont.)";
                    currentElements_.insert(currentElementIndex_, extra);
                }
                LOG_INFO("[EventDispatcher] Locución de hora: {} archivos insertados",
                         files.size());
                return files.first();
            }
        }
        return {};

    case ElementType::Stream:
        if (!element.path.isEmpty()) return element.path;
        return {};
    }

    return {};
}

// ══════════════════════════════════════════════════════════
// Finalización del evento
// ══════════════════════════════════════════════════════════

void EventDispatcher::finishEvent()
{
    LOG_INFO("[EventDispatcher] Evento finalizado: {}", currentEventName_.toStdString());

    auto* eventsPipeline = engine_->eventsPipeline();
    eventsPipeline->stop();

    // Detener timer de stream si estaba activo
    if (streamTimer_) streamTimer_->stop();
    if (streamRetryTimer_) streamRetryTimer_->stop();
    if (streamStabilizedTimer_) streamStabilizedTimer_->stop();
    streamReconnecting_ = false;
    streamConnected_ = false;

    // Desconectar señales del pipeline de eventos
    disconnect(eventsPipeline, &AudioPipeline::trackFinished, this, nullptr);
    disconnect(eventsPipeline, &AudioPipeline::errorOccurred, this, nullptr);

    QString finishedName = currentEventName_;

    // Limpiar estado del evento actual
    currentEventId_.clear();
    currentEventName_.clear();
    currentElements_.clear();
    currentElementIndex_ = 0;
    currentImmediate_ = false;

    // ¿Hay más eventos en la cola? Si sí, reproducir el siguiente
    // SIN emitir eventFinished ni cambiar eventPlaying_
    if (!pendingEventQueue_.isEmpty()) {
        QueuedEvent next = pendingEventQueue_.dequeue();
        LOG_INFO("[EventDispatcher] Siguiente evento en cola: {} (prioridad {}, quedan {})",
                 next.event.name.toStdString(), next.event.priority, pendingEventQueue_.size());

        triggerEvent(next.event, next.scheduledTime);
        return;
    }

    // Cola vacía → ahora sí terminamos de verdad
    eventPlaying_ = false;

    // Retornar al Main — sin crossfade (los eventos terminan limpiamente)
    emit requestMainResume();

    emit eventFinished(finishedName);
}

// ══════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════

QString EventDispatcher::currentDayName() const
{
    int dow = QDate::currentDate().dayOfWeek();
    if (dow >= 1 && dow <= 7) return DAY_NAMES[dow - 1];
    return DAY_NAMES[0];
}

} // namespace sara
