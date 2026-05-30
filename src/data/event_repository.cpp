#include "data/event_repository.h"
#include "data/database.h"
#include "util/logger.h"

#include <QUuid>
#include <QDateTime>

namespace sara {

EventRepository::EventRepository(Database* db)
    : db_(db)
{
}

// ══════════════════════════════════════════════════════════
// CRUD de Eventos
// ══════════════════════════════════════════════════════════

QString EventRepository::create(const QString& name)
{
    QString id = generateId();
    auto q = db_->execPrepared(
        "INSERT INTO events (id, name) VALUES (?, ?)",
        {id, name}
    );
    if (!q) return {};
    LOG_INFO("[EventRepo] Evento creado: {} ({})", name.toStdString(), id.toStdString());
    return id;
}

std::optional<Event> EventRepository::getById(const QString& id)
{
    auto q = db_->execPrepared(
        "SELECT id, name, persistent, immediate, priority, "
        "use_ad_announce, max_wait_minutes, valid_from, valid_until, created_at "
        "FROM events WHERE id = ?",
        {id}
    );
    if (!q || !q->next()) return std::nullopt;

    Event e;
    e.id             = q->value(0).toString();
    e.name           = q->value(1).toString();
    e.persistent     = q->value(2).toBool();
    e.immediate      = q->value(3).toBool();
    e.priority       = q->value(4).toInt();
    e.useAdAnnounce  = q->value(5).toBool();
    e.maxWaitMinutes = q->value(6).toInt();
    e.validFrom      = QDate::fromString(q->value(7).toString(), Qt::ISODate);
    e.validUntil     = QDate::fromString(q->value(8).toString(), Qt::ISODate);
    e.createdAt      = QDateTime::fromString(q->value(9).toString(), Qt::ISODate);
    e.elements       = getElements(id);

    return e;
}

QVector<Event> EventRepository::getAll()
{
    QVector<Event> result;
    auto q = db_->execPrepared(
        "SELECT id, name, persistent, immediate, priority, "
        "use_ad_announce, max_wait_minutes, valid_from, valid_until, created_at "
        "FROM events ORDER BY name",
        {}
    );
    if (!q) return result;

    while (q->next()) {
        Event e;
        e.id             = q->value(0).toString();
        e.name           = q->value(1).toString();
        e.persistent     = q->value(2).toBool();
        e.immediate      = q->value(3).toBool();
        e.priority       = q->value(4).toInt();
        e.useAdAnnounce  = q->value(5).toBool();
        e.maxWaitMinutes = q->value(6).toInt();
        e.validFrom      = QDate::fromString(q->value(7).toString(), Qt::ISODate);
        e.validUntil     = QDate::fromString(q->value(8).toString(), Qt::ISODate);
        e.createdAt      = QDateTime::fromString(q->value(9).toString(), Qt::ISODate);
        result.append(e);
    }
    return result;
}

bool EventRepository::update(const Event& event)
{
    auto q = db_->execPrepared(
        "UPDATE events SET name=?, persistent=?, immediate=?, priority=?, "
        "use_ad_announce=?, max_wait_minutes=?, valid_from=?, valid_until=? WHERE id=?",
        {event.name, event.persistent ? 1 : 0, event.immediate ? 1 : 0,
         event.priority, event.useAdAnnounce ? 1 : 0, event.maxWaitMinutes,
         event.validFrom.isValid() ? event.validFrom.toString(Qt::ISODate) : QVariant(),
         event.validUntil.isValid() ? event.validUntil.toString(Qt::ISODate) : QVariant(),
         event.id}
    );
    return q.has_value();
}

bool EventRepository::remove(const QString& id)
{
    auto q = db_->execPrepared("DELETE FROM events WHERE id = ?", {id});
    if (q) LOG_INFO("[EventRepo] Evento eliminado: {}", id.toStdString());
    return q.has_value();
}

QString EventRepository::duplicate(const QString& sourceId, const QString& newName)
{
    auto source = getById(sourceId);
    if (!source) return {};

    QString newId = create(newName);
    if (newId.isEmpty()) return {};

    // Copiar propiedades
    Event newEvent = *source;
    newEvent.id = newId;
    newEvent.name = newName;
    update(newEvent);

    // Copiar elementos
    setElements(newId, source->elements);

    // Copiar slots
    auto eventSlots = getSlots(sourceId);
    for (const auto& slot : eventSlots) {
        addSlot(newId, slot.day, slot.triggerTime);
    }

    return newId;
}

// ══════════════════════════════════════════════════════════
// Elementos
// ══════════════════════════════════════════════════════════

QVector<EventElement> EventRepository::getElements(const QString& eventId)
{
    QVector<EventElement> result;
    auto q = db_->execPrepared(
        "SELECT id, event_id, position, type, path, display_name, "
        "valid_from, valid_until, duration_ms "
        "FROM event_elements WHERE event_id = ? ORDER BY position",
        {eventId}
    );
    if (!q) return result;

    while (q->next()) {
        EventElement e;
        e.id          = q->value(0).toInt();
        e.eventId     = q->value(1).toString();
        e.position    = q->value(2).toInt();
        e.type        = elementTypeFromString(q->value(3).toString());
        e.path        = q->value(4).toString();
        e.displayName = q->value(5).toString();
        e.validFrom   = QDate::fromString(q->value(6).toString(), Qt::ISODate);
        e.validUntil  = QDate::fromString(q->value(7).toString(), Qt::ISODate);
        e.durationMs  = q->value(8).toLongLong();
        result.append(e);
    }
    return result;
}

QVector<EventElement> EventRepository::getValidElements(const QString& eventId)
{
    auto all = getElements(eventId);
    QVector<EventElement> valid;
    for (const auto& e : all) {
        if (e.isValidToday()) {
            valid.append(e);
        }
    }
    return valid;
}

bool EventRepository::updateElementVigency(int elementId,
                                            const QDate& validFrom, const QDate& validUntil)
{
    auto q = db_->execPrepared(
        "UPDATE event_elements SET valid_from=?, valid_until=? WHERE id=?",
        {validFrom.isValid() ? validFrom.toString(Qt::ISODate) : QVariant(),
         validUntil.isValid() ? validUntil.toString(Qt::ISODate) : QVariant(),
         elementId});
    return q.has_value();
}

bool EventRepository::updateElementValidFrom(int elementId, const QDate& validFrom)
{
    auto q = db_->execPrepared(
        "UPDATE event_elements SET valid_from=? WHERE id=?",
        {validFrom.isValid() ? validFrom.toString(Qt::ISODate) : QVariant(), elementId});
    return q.has_value();
}

bool EventRepository::updateElementValidUntil(int elementId, const QDate& validUntil)
{
    auto q = db_->execPrepared(
        "UPDATE event_elements SET valid_until=? WHERE id=?",
        {validUntil.isValid() ? validUntil.toString(Qt::ISODate) : QVariant(), elementId});
    return q.has_value();
}

bool EventRepository::setElements(const QString& eventId,
                                   const QVector<EventElement>& elements)
{
    return db_->transaction([&]() {
        db_->execPrepared("DELETE FROM event_elements WHERE event_id = ?", {eventId});

        for (int i = 0; i < elements.size(); ++i) {
            const auto& e = elements[i];
            db_->execPrepared(
                "INSERT INTO event_elements "
                "(event_id, position, type, path, display_name, valid_from, valid_until, duration_ms) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                {eventId, i, elementTypeToString(e.type), e.path, e.displayName,
                 e.validFrom.isValid() ? e.validFrom.toString(Qt::ISODate) : QVariant(),
                 e.validUntil.isValid() ? e.validUntil.toString(Qt::ISODate) : QVariant(),
                 static_cast<qlonglong>(e.durationMs)}
            );
        }
        return true;
    });
}

// ══════════════════════════════════════════════════════════
// Slots horarios
// ══════════════════════════════════════════════════════════

QVector<EventSlot> EventRepository::getSlots(const QString& eventId)
{
    QVector<EventSlot> result;
    auto q = db_->execPrepared(
        "SELECT id, event_id, day, trigger_time, enabled "
        "FROM event_slots WHERE event_id = ? ORDER BY day, trigger_time",
        {eventId}
    );
    if (!q) return result;

    while (q->next()) {
        EventSlot s;
        s.id          = q->value(0).toInt();
        s.eventId     = q->value(1).toString();
        s.day         = q->value(2).toString();
        s.triggerTime = QTime::fromString(q->value(3).toString(), "HH:mm");
        s.enabled     = q->value(4).toBool();
        result.append(s);
    }
    return result;
}

QVector<EventSlot> EventRepository::getSlotsForDay(const QString& day)
{
    QVector<EventSlot> result;
    auto q = db_->execPrepared(
        "SELECT es.id, es.event_id, es.day, es.trigger_time, es.enabled "
        "FROM event_slots es "
        "JOIN events e ON es.event_id = e.id "
        "WHERE es.day = ? AND es.enabled = 1 "
        "ORDER BY es.trigger_time, e.priority ASC",
        {day}
    );
    if (!q) return result;

    while (q->next()) {
        EventSlot s;
        s.id          = q->value(0).toInt();
        s.eventId     = q->value(1).toString();
        s.day         = q->value(2).toString();
        s.triggerTime = QTime::fromString(q->value(3).toString(), "HH:mm");
        s.enabled     = q->value(4).toBool();
        result.append(s);
    }
    return result;
}

QVector<EventSlot> EventRepository::getSlotsForDayAndTime(const QString& day, const QTime& time)
{
    QVector<EventSlot> result;
    auto q = db_->execPrepared(
        "SELECT es.id, es.event_id, es.day, es.trigger_time, es.enabled "
        "FROM event_slots es "
        "JOIN events e ON es.event_id = e.id "
        "WHERE es.day = ? AND es.trigger_time = ? AND es.enabled = 1 "
        "ORDER BY e.priority ASC",
        {day, time.toString("HH:mm")}
    );
    if (!q) return result;

    while (q->next()) {
        EventSlot s;
        s.id          = q->value(0).toInt();
        s.eventId     = q->value(1).toString();
        s.day         = q->value(2).toString();
        s.triggerTime = QTime::fromString(q->value(3).toString(), "HH:mm");
        s.enabled     = q->value(4).toBool();
        result.append(s);
    }
    return result;
}

QVector<Event> EventRepository::getEventsAt(const QString& day, const QTime& time)
{
    QVector<Event> result;
    auto eventSlots = getSlotsForDayAndTime(day, time);

    for (const auto& slot : eventSlots) {
        auto event = getById(slot.eventId);
        if (event && event->isValidToday()) {
            result.append(*event);
        }
    }
    return result;
}

bool EventRepository::addSlot(const QString& eventId, const QString& day, const QTime& triggerTime)
{
    auto q = db_->execPrepared(
        "INSERT INTO event_slots (event_id, day, trigger_time) VALUES (?, ?, ?)",
        {eventId, day, triggerTime.toString("HH:mm")}
    );
    return q.has_value();
}

bool EventRepository::removeSlot(int slotId)
{
    auto q = db_->execPrepared("DELETE FROM event_slots WHERE id = ?", {slotId});
    return q.has_value();
}

bool EventRepository::setSlotEnabled(int slotId, bool enabled)
{
    auto q = db_->execPrepared(
        "UPDATE event_slots SET enabled = ? WHERE id = ?",
        {enabled ? 1 : 0, slotId}
    );
    return q.has_value();
}

// ══════════════════════════════════════════════════════════
// Asignación inversa
// ══════════════════════════════════════════════════════════

bool EventRepository::addElementToEvents(const QStringList& eventIds,
                                          const EventElement& element)
{
    return db_->transaction([&]() {
        for (const auto& eventId : eventIds) {
            // Obtener posición máxima actual
            auto q = db_->execPrepared(
                "SELECT COALESCE(MAX(position), -1) FROM event_elements WHERE event_id = ?",
                {eventId}
            );
            int nextPos = 0;
            if (q && q->next()) nextPos = q->value(0).toInt() + 1;

            db_->execPrepared(
                "INSERT INTO event_elements "
                "(event_id, position, type, path, display_name, valid_from, valid_until, duration_ms) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                {eventId, nextPos, elementTypeToString(element.type),
                 element.path, element.displayName,
                 element.validFrom.isValid() ? element.validFrom.toString(Qt::ISODate) : QVariant(),
                 element.validUntil.isValid() ? element.validUntil.toString(Qt::ISODate) : QVariant(),
                 static_cast<qlonglong>(element.durationMs)}
            );
        }
        return true;
    });
}

bool EventRepository::removeElementFromEvents(const QStringList& eventIds,
                                               const QString& filePath)
{
    return db_->transaction([&]() {
        for (const auto& eventId : eventIds) {
            db_->execPrepared(
                "DELETE FROM event_elements WHERE event_id = ? AND path = ?",
                {eventId, filePath}
            );
            // Reordenar posiciones
            auto elems = getElements(eventId);
            for (int i = 0; i < elems.size(); ++i) {
                db_->execPrepared(
                    "UPDATE event_elements SET position = ? WHERE id = ?",
                    {i, elems[i].id}
                );
            }
        }
        return true;
    });
}

// ══════════════════════════════════════════════════════════
// Búsqueda
// ══════════════════════════════════════════════════════════

QVector<Event> EventRepository::findEventsContaining(const QString& filePath)
{
    QVector<Event> result;
    auto q = db_->execPrepared(
        "SELECT DISTINCT e.id FROM events e "
        "JOIN event_elements ee ON e.id = ee.event_id "
        "WHERE ee.path = ?",
        {filePath}
    );
    if (!q) return result;

    while (q->next()) {
        auto event = getById(q->value(0).toString());
        if (event) result.append(*event);
    }
    return result;
}

// ══════════════════════════════════════════════════════════
// Resumen semanal
// ══════════════════════════════════════════════════════════

QVector<EventRepository::WeeklySummaryItem> EventRepository::getWeeklySummary()
{
    QVector<WeeklySummaryItem> result;
    auto q = db_->execPrepared(
        "SELECT e.id, e.name, es.day, es.trigger_time, e.priority, "
        "(SELECT COUNT(*) FROM event_elements ee WHERE ee.event_id = e.id) as elem_count "
        "FROM events e "
        "JOIN event_slots es ON e.id = es.event_id "
        "WHERE es.enabled = 1 "
        "ORDER BY CASE es.day "
        "  WHEN 'Lunes' THEN 1 WHEN 'Martes' THEN 2 "
        "  WHEN 'Miércoles' THEN 3 WHEN 'Jueves' THEN 4 "
        "  WHEN 'Viernes' THEN 5 WHEN 'Sábado' THEN 6 "
        "  WHEN 'Domingo' THEN 7 END, es.trigger_time",
        {}
    );
    if (!q) return result;

    while (q->next()) {
        WeeklySummaryItem item;
        item.eventId      = q->value(0).toString();
        item.eventName    = q->value(1).toString();
        item.day          = q->value(2).toString();
        item.triggerTime  = QTime::fromString(q->value(3).toString(), "HH:mm");
        item.priority     = q->value(4).toInt();
        item.elementCount = q->value(5).toInt();
        result.append(item);
    }
    return result;
}

// ══════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════

QString EventRepository::generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

// ══════════════════════════════════════════════════════════
// Eventos con ventana de espera
// ══════════════════════════════════════════════════════════

QVector<EventRepository::PendingWindowEvent> EventRepository::getEventsInWindow(
    const QString& day, const QTime& now)
{
    QVector<PendingWindowEvent> result;

    // Obtener todos los slots del dia
    auto daySlots = getSlotsForDay(day);

    for (const auto& slot : daySlots) {
        // Solo slots habilitados cuya hora ya paso
        if (!slot.enabled) continue;
        if (slot.triggerTime >= now) continue;  // No es hora aun

        auto event = getById(slot.eventId);
        if (!event || !event->isValidToday()) continue;

        // Solo eventos no-persistentes con maxWaitMinutes > 0
        if (event->persistent) continue;
        if (event->maxWaitMinutes <= 0) continue;

        // Verificar que está dentro de la ventana de espera
        int minutesLate = slot.triggerTime.secsTo(now) / 60;
        if (minutesLate > event->maxWaitMinutes) continue;

        // Verificar que tiene elementos vigentes
        auto elements = getValidElements(event->id);
        if (elements.isEmpty()) continue;

        PendingWindowEvent pwe;
        pwe.event = *event;
        pwe.scheduledTime = slot.triggerTime;
        pwe.minutesLate = minutesLate;
        result.append(pwe);
    }

    // Ordenar por prioridad descendente
    std::sort(result.begin(), result.end(),
              [](const PendingWindowEvent& a, const PendingWindowEvent& b) {
        return a.event.priority < b.event.priority;
    });

    return result;
}

// ══════════════════════════════════════════════════════════
// Eventos vencidos
// ══════════════════════════════════════════════════════════

void EventRepository::recordExpired(const QString& eventId, const QString& eventName,
                                     const QTime& scheduledTime, const QString& reason)
{
    // Evitar duplicados: verificar si ya existe para este evento+hora+fecha
    auto check = db_->execPrepared(
        "SELECT COUNT(*) FROM expired_events "
        "WHERE event_id=? AND scheduled_time=? AND date(expired_at)=date('now','localtime')",
        {eventId, scheduledTime.toString("HH:mm")});
    if (check && check->next() && check->value(0).toInt() > 0) return;

    db_->execPrepared(
        "INSERT INTO expired_events (event_id, event_name, scheduled_time, reason) "
        "VALUES (?, ?, ?, ?)",
        {eventId, eventName, scheduledTime.toString("HH:mm"), reason});
}

QVector<EventRepository::ExpiredEventInfo> EventRepository::getExpiredEvents(int limit)
{
    QVector<ExpiredEventInfo> result;
    auto q = db_->execPrepared(
        "SELECT id, event_id, event_name, scheduled_time, expired_at, reason "
        "FROM expired_events ORDER BY expired_at DESC LIMIT ?",
        {limit});
    if (!q) return result;

    while (q->next()) {
        ExpiredEventInfo info;
        info.id            = q->value(0).toInt();
        info.eventId       = q->value(1).toString();
        info.eventName     = q->value(2).toString();
        info.scheduledTime = q->value(3).toString();
        info.expiredAt     = q->value(4).toString();
        info.reason        = q->value(5).toString();
        result.append(info);
    }
    return result;
}

void EventRepository::clearExpiredEvents()
{
    db_->execPrepared("DELETE FROM expired_events", {});
}

void EventRepository::removeExpiredEvent(const QString& eventId)
{
    db_->execPrepared("DELETE FROM expired_events WHERE event_id=?", {eventId});
}

// ══════════════════════════════════════════════════════════
// Registro de eventos emitidos
// ══════════════════════════════════════════════════════════

void EventRepository::recordPlayed(const QString& eventId, const QTime& slotTime)
{
    db_->execPrepared(
        "INSERT OR IGNORE INTO played_events (event_id, slot_time) VALUES (?, ?)",
        {eventId, slotTime.toString("HH:mm")});
}

bool EventRepository::wasPlayedToday(const QString& eventId, const QTime& slotTime)
{
    auto q = db_->execPrepared(
        "SELECT COUNT(*) FROM played_events "
        "WHERE event_id=? AND slot_time=? AND play_date=date('now','localtime')",
        {eventId, slotTime.toString("HH:mm")});
    return q && q->next() && q->value(0).toInt() > 0;
}

void EventRepository::cleanOldPlayedEvents()
{
    db_->execPrepared(
        "DELETE FROM played_events WHERE play_date < date('now','localtime')", {});
}

} // namespace sara
