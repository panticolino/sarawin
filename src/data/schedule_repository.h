#ifndef SARA_DATA_SCHEDULE_REPOSITORY_H
#define SARA_DATA_SCHEDULE_REPOSITORY_H

#include "core/types.h"
#include <QVector>
#include <QString>

namespace sara {

class Database;

/**
 * Repositorio de programaciones musicales.
 * CRUD sobre las tablas schedules, schedule_elements y schedule_slots.
 */
class ScheduleRepository
{
public:
    explicit ScheduleRepository(Database* db);

    // ── CRUD de Programaciones ───────────────────────
    /// Crear nueva programación, retorna su ID
    QString create(const QString& name);

    /// Obtener una programación por ID (con sus elementos)
    std::optional<Schedule> getById(const QString& id);

    /// Obtener todas las programaciones (sin elementos, solo nombre e ID)
    QVector<Schedule> getAll();

    /// Renombrar una programación
    bool rename(const QString& id, const QString& newName);

    /// Eliminar una programación (cascada borra elementos y slots)
    bool remove(const QString& id);

    /// Duplicar una programación con nuevo nombre
    QString duplicate(const QString& sourceId, const QString& newName);

    // ── Elementos de una programación ────────────────
    /// Obtener elementos ordenados por posición
    QVector<ScheduleElement> getElements(const QString& scheduleId);

    /// Reemplazar todos los elementos de una programación
    bool setElements(const QString& scheduleId,
                     const QVector<ScheduleElement>& elements);

    /// Agregar un elemento al final
    bool addElement(const QString& scheduleId, const ScheduleElement& element);

    /// Eliminar un elemento por posición
    bool removeElement(const QString& scheduleId, int position);

    /// Mover un elemento de una posición a otra
    bool moveElement(const QString& scheduleId, int fromPos, int toPos);

    // ── Slots horarios ───────────────────────────────
    /// Obtener todos los slots de una programación
    QVector<ScheduleSlot> getSlots(const QString& scheduleId);

    /// Obtener todos los slots para un día dado
    QVector<ScheduleSlot> getSlotsForDay(const QString& day);

    /// Obtener el slot activo en un momento dado (día + hora)
    std::optional<ScheduleSlot> getActiveSlot(const QString& day, const QTime& time);

    /// Asignar una programación a un slot horario
    bool assignSlot(const QString& scheduleId, const QString& day,
                    const QTime& startTime, const QTime& endTime);

    /// Asignar en bloque: múltiples franjas en una sola transacción (rendimiento)
    bool bulkAssignSlots(const QString& scheduleId,
                         const QStringList& days,
                         const QTime& startTime, const QTime& endTime);

    /// Eliminar un slot horario
    bool removeSlot(const QString& day, const QTime& startTime);

    /// Limpiar todos los slots de un día
    bool clearDay(const QString& day);

private:
    QString generateId();
    Database* db_;
};

} // namespace sara

#endif
