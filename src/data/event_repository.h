#ifndef SARA_DATA_EVENT_REPOSITORY_H
#define SARA_DATA_EVENT_REPOSITORY_H

#include "core/types.h"
#include <QVector>
#include <QString>
#include <optional>

namespace sara {

class Database;

/**
 * Repositorio de eventos (Publicidad/Eventos).
 * CRUD sobre las tablas events, event_elements y event_slots.
 */
class EventRepository
{
public:
    explicit EventRepository(Database* db);

    // ── CRUD de Eventos ──────────────────────────────
    QString create(const QString& name);
    std::optional<Event> getById(const QString& id);
    QVector<Event> getAll();
    bool update(const Event& event);
    bool remove(const QString& id);
    QString duplicate(const QString& sourceId, const QString& newName);

    // ── Elementos ────────────────────────────────────
    QVector<EventElement> getElements(const QString& eventId);
    QVector<EventElement> getValidElements(const QString& eventId);  // Solo vigentes hoy
    bool updateElementVigency(int elementId, const QDate& validFrom, const QDate& validUntil);
    bool updateElementValidFrom(int elementId, const QDate& validFrom);
    bool updateElementValidUntil(int elementId, const QDate& validUntil);
    bool setElements(const QString& eventId, const QVector<EventElement>& elements);

    // ── Slots horarios ───────────────────────────────
    QVector<EventSlot> getSlots(const QString& eventId);
    QVector<EventSlot> getSlotsForDay(const QString& day);
    QVector<EventSlot> getSlotsForDayAndTime(const QString& day, const QTime& time);

    /// Obtener todos los eventos programados para un día+hora, ordenados por prioridad desc
    QVector<Event> getEventsAt(const QString& day, const QTime& time);

    bool addSlot(const QString& eventId, const QString& day, const QTime& triggerTime);
    bool removeSlot(int slotId);
    bool setSlotEnabled(int slotId, bool enabled);

    // ── Asignación inversa: agregar audio a múltiples eventos ──
    bool addElementToEvents(const QStringList& eventIds, const EventElement& element);
    bool removeElementFromEvents(const QStringList& eventIds, const QString& filePath);

    // ── Búsqueda ─────────────────────────────────────
    /// Buscar eventos que contengan un archivo específico
    QVector<Event> findEventsContaining(const QString& filePath);

    // ── Eventos con ventana de espera ────────────────
    /// Obtener eventos no-persistentes que están dentro de su ventana de espera
    /// (scheduled_time + maxWaitMinutes > now)
    struct PendingWindowEvent {
        Event event;
        QTime scheduledTime;
        int   minutesLate;
    };
    QVector<PendingWindowEvent> getEventsInWindow(const QString& day, const QTime& now);

    // ── Eventos vencidos ─────────────────────────────
    struct ExpiredEventInfo {
        int     id;
        QString eventId;
        QString eventName;
        QString scheduledTime;
        QString expiredAt;
        QString reason;
    };
    void recordExpired(const QString& eventId, const QString& eventName,
                       const QTime& scheduledTime, const QString& reason = "timeout");
    QVector<ExpiredEventInfo> getExpiredEvents(int limit = 50);
    void clearExpiredEvents();
    void removeExpiredEvent(const QString& eventId);

    // ── Registro de eventos emitidos (persiste entre reinicios) ──
    void recordPlayed(const QString& eventId, const QTime& slotTime);
    bool wasPlayedToday(const QString& eventId, const QTime& slotTime);
    void cleanOldPlayedEvents();  // Limpia registros de días anteriores

    // ── Resumen semanal ──────────────────────────────
    struct WeeklySummaryItem {
        QString eventId;
        QString eventName;
        QString day;
        QTime   triggerTime;
        int     elementCount;
        int     priority;
    };
    QVector<WeeklySummaryItem> getWeeklySummary();

private:
    QString generateId();
    Database* db_;
};

} // namespace sara

#endif
