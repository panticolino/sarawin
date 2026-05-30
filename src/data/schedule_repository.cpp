#include "data/schedule_repository.h"
#include "data/database.h"
#include "util/logger.h"

#include <QUuid>
#include <QDateTime>

namespace sara {

ScheduleRepository::ScheduleRepository(Database* db)
    : db_(db)
{
}

// ══════════════════════════════════════════════════════════
// CRUD de Programaciones
// ══════════════════════════════════════════════════════════

QString ScheduleRepository::create(const QString& name)
{
    QString id = generateId();

    auto q = db_->execPrepared(
        "INSERT INTO schedules (id, name) VALUES (?, ?)",
        {id, name}
    );

    if (!q) {
        LOG_ERROR("[ScheduleRepo] Error creando programación: {}", name.toStdString());
        return {};
    }

    LOG_INFO("[ScheduleRepo] Programación creada: {} ({})", name.toStdString(), id.toStdString());
    return id;
}

std::optional<Schedule> ScheduleRepository::getById(const QString& id)
{
    auto q = db_->execPrepared(
        "SELECT id, name, created_at, updated_at FROM schedules WHERE id = ?",
        {id}
    );

    if (!q || !q->next()) return std::nullopt;

    Schedule s;
    s.id        = q->value(0).toString();
    s.name      = q->value(1).toString();
    s.createdAt = QDateTime::fromString(q->value(2).toString(), Qt::ISODate);
    s.updatedAt = QDateTime::fromString(q->value(3).toString(), Qt::ISODate);
    s.elements  = getElements(id);

    return s;
}

QVector<Schedule> ScheduleRepository::getAll()
{
    QVector<Schedule> result;

    auto q = db_->execPrepared(
        "SELECT id, name, created_at, updated_at FROM schedules ORDER BY name",
        {}
    );

    if (!q) return result;

    while (q->next()) {
        Schedule s;
        s.id        = q->value(0).toString();
        s.name      = q->value(1).toString();
        s.createdAt = QDateTime::fromString(q->value(2).toString(), Qt::ISODate);
        s.updatedAt = QDateTime::fromString(q->value(3).toString(), Qt::ISODate);
        result.append(s);
    }

    return result;
}

bool ScheduleRepository::rename(const QString& id, const QString& newName)
{
    auto q = db_->execPrepared(
        "UPDATE schedules SET name = ?, updated_at = datetime('now', 'localtime') WHERE id = ?",
        {newName, id}
    );
    return q.has_value();
}

bool ScheduleRepository::remove(const QString& id)
{
    // CASCADE borra elementos y slots automáticamente
    auto q = db_->execPrepared("DELETE FROM schedules WHERE id = ?", {id});
    if (q) {
        LOG_INFO("[ScheduleRepo] Programación eliminada: {}", id.toStdString());
    }
    return q.has_value();
}

QString ScheduleRepository::duplicate(const QString& sourceId, const QString& newName)
{
    auto source = getById(sourceId);
    if (!source) return {};

    QString newId = create(newName);
    if (newId.isEmpty()) return {};

    setElements(newId, source->elements);

    // Copiar slots horarios
    auto sourceSlots = getSlots(sourceId);
    for (const auto& slot : sourceSlots) {
        assignSlot(newId, slot.day, slot.startTime, slot.endTime);
    }

    LOG_INFO("[ScheduleRepo] Programación duplicada: {} → {}",
             source->name.toStdString(), newName.toStdString());
    return newId;
}

// ══════════════════════════════════════════════════════════
// Elementos
// ══════════════════════════════════════════════════════════

QVector<ScheduleElement> ScheduleRepository::getElements(const QString& scheduleId)
{
    QVector<ScheduleElement> result;

    auto q = db_->execPrepared(
        "SELECT id, schedule_id, position, type, path, display_name "
        "FROM schedule_elements WHERE schedule_id = ? ORDER BY position",
        {scheduleId}
    );

    if (!q) return result;

    while (q->next()) {
        ScheduleElement e;
        e.id          = q->value(0).toInt();
        e.scheduleId  = q->value(1).toString();
        e.position    = q->value(2).toInt();
        e.type        = elementTypeFromString(q->value(3).toString());
        e.path        = q->value(4).toString();
        e.displayName = q->value(5).toString();
        result.append(e);
    }

    return result;
}

bool ScheduleRepository::setElements(const QString& scheduleId,
                                      const QVector<ScheduleElement>& elements)
{
    return db_->transaction([&]() {
        // Borrar elementos existentes
        db_->execPrepared(
            "DELETE FROM schedule_elements WHERE schedule_id = ?",
            {scheduleId}
        );

        // Insertar nuevos en orden
        for (int i = 0; i < elements.size(); ++i) {
            const auto& e = elements[i];
            auto q = db_->execPrepared(
                "INSERT INTO schedule_elements "
                "(schedule_id, position, type, path, display_name) "
                "VALUES (?, ?, ?, ?, ?)",
                {scheduleId, i, elementTypeToString(e.type), e.path, e.displayName}
            );
            if (!q) return false;
        }

        // Actualizar timestamp
        db_->execPrepared(
            "UPDATE schedules SET updated_at = datetime('now', 'localtime') WHERE id = ?",
            {scheduleId}
        );

        return true;
    });
}

bool ScheduleRepository::addElement(const QString& scheduleId, const ScheduleElement& element)
{
    // Obtener posición máxima actual
    auto q = db_->execPrepared(
        "SELECT COALESCE(MAX(position), -1) FROM schedule_elements WHERE schedule_id = ?",
        {scheduleId}
    );

    int nextPos = 0;
    if (q && q->next()) {
        nextPos = q->value(0).toInt() + 1;
    }

    auto r = db_->execPrepared(
        "INSERT INTO schedule_elements "
        "(schedule_id, position, type, path, display_name) "
        "VALUES (?, ?, ?, ?, ?)",
        {scheduleId, nextPos, elementTypeToString(element.type),
         element.path, element.displayName}
    );

    return r.has_value();
}

bool ScheduleRepository::removeElement(const QString& scheduleId, int position)
{
    return db_->transaction([&]() {
        db_->execPrepared(
            "DELETE FROM schedule_elements WHERE schedule_id = ? AND position = ?",
            {scheduleId, position}
        );

        // Reordenar posiciones
        db_->execPrepared(
            "UPDATE schedule_elements SET position = position - 1 "
            "WHERE schedule_id = ? AND position > ?",
            {scheduleId, position}
        );

        return true;
    });
}

bool ScheduleRepository::moveElement(const QString& scheduleId, int fromPos, int toPos)
{
    if (fromPos == toPos) return true;

    auto elements = getElements(scheduleId);
    if (fromPos < 0 || fromPos >= elements.size() ||
        toPos < 0   || toPos >= elements.size()) {
        return false;
    }

    // Reordenar en memoria
    auto elem = elements.takeAt(fromPos);
    elements.insert(toPos, elem);

    // Guardar de nuevo
    return setElements(scheduleId, elements);
}

// ══════════════════════════════════════════════════════════
// Slots horarios
// ══════════════════════════════════════════════════════════

QVector<ScheduleSlot> ScheduleRepository::getSlots(const QString& scheduleId)
{
    QVector<ScheduleSlot> result;

    auto q = db_->execPrepared(
        "SELECT id, schedule_id, day, start_time, end_time "
        "FROM schedule_slots WHERE schedule_id = ? ORDER BY day, start_time",
        {scheduleId}
    );

    if (!q) return result;

    while (q->next()) {
        ScheduleSlot s;
        s.id         = q->value(0).toInt();
        s.scheduleId = q->value(1).toString();
        s.day        = q->value(2).toString();
        s.startTime  = QTime::fromString(q->value(3).toString(), "HH:mm");
        s.endTime    = QTime::fromString(q->value(4).toString(), "HH:mm");
        result.append(s);
    }

    return result;
}

QVector<ScheduleSlot> ScheduleRepository::getSlotsForDay(const QString& day)
{
    QVector<ScheduleSlot> result;

    auto q = db_->execPrepared(
        "SELECT ss.id, ss.schedule_id, ss.day, ss.start_time, ss.end_time "
        "FROM schedule_slots ss "
        "WHERE ss.day = ? ORDER BY ss.start_time",
        {day}
    );

    if (!q) return result;

    while (q->next()) {
        ScheduleSlot s;
        s.id         = q->value(0).toInt();
        s.scheduleId = q->value(1).toString();
        s.day        = q->value(2).toString();
        s.startTime  = QTime::fromString(q->value(3).toString(), "HH:mm");
        s.endTime    = QTime::fromString(q->value(4).toString(), "HH:mm");
        result.append(s);
    }

    return result;
}

std::optional<ScheduleSlot> ScheduleRepository::getActiveSlot(const QString& day, const QTime& time)
{
    auto q = db_->execPrepared(
        "SELECT id, schedule_id, day, start_time, end_time "
        "FROM schedule_slots "
        "WHERE day = ? AND start_time <= ? AND end_time > ? "
        "ORDER BY start_time DESC LIMIT 1",
        {day, time.toString("HH:mm"), time.toString("HH:mm")}
    );

    if (!q || !q->next()) return std::nullopt;

    ScheduleSlot s;
    s.id         = q->value(0).toInt();
    s.scheduleId = q->value(1).toString();
    s.day        = q->value(2).toString();
    s.startTime  = QTime::fromString(q->value(3).toString(), "HH:mm");
    s.endTime    = QTime::fromString(q->value(4).toString(), "HH:mm");

    return s;
}

bool ScheduleRepository::assignSlot(const QString& scheduleId, const QString& day,
                                     const QTime& startTime, const QTime& endTime)
{
    return db_->transaction([&]() {
        // Eliminar slot previo en esa franja (si existe)
        db_->execPrepared(
            "DELETE FROM schedule_slots WHERE day = ? AND start_time = ?",
            {day, startTime.toString("HH:mm")}
        );

        // Insertar nuevo
        auto q = db_->execPrepared(
            "INSERT INTO schedule_slots (schedule_id, day, start_time, end_time) "
            "VALUES (?, ?, ?, ?)",
            {scheduleId, day, startTime.toString("HH:mm"), endTime.toString("HH:mm")}
        );

        return q.has_value();
    });
}

bool ScheduleRepository::bulkAssignSlots(const QString& scheduleId,
                                          const QStringList& days,
                                          const QTime& startTime, const QTime& endTime)
{
    // Convertir a "slot index" (0-47) para evitar problemas de QTime wrap en medianoche
    int startSlot = startTime.hour() * 2 + (startTime.minute() >= 30 ? 1 : 0);
    int endSlot   = endTime.hour() * 2 + (endTime.minute() >= 30 ? 1 : 0);

    // Si endTime es 00:00 (medianoche / "24:00"), significa fin del día = slot 48
    if (endTime == QTime(0, 0) || endSlot <= startSlot) {
        endSlot = 48;
    }

    // Sanity check
    if (startSlot >= endSlot || startSlot < 0 || endSlot > 48) {
        LOG_WARN("[ScheduleRepo] bulkAssign: rango inválido slots {}-{}", startSlot, endSlot);
        return false;
    }

    // Una sola transacción para todas las franjas
    return db_->transaction([&]() {
        for (int s = startSlot; s < endSlot; ++s) {
            int hour = s / 2;
            int minute = (s % 2) * 30;
            QTime t(hour, minute);
            QTime slotEnd = (s + 1 < 48)
                ? QTime(((s + 1) / 2), ((s + 1) % 2) * 30)
                : QTime(23, 59);

            QString tStr = t.toString("HH:mm");
            QString slotEndStr = slotEnd.toString("HH:mm");

            for (const auto& day : days) {
                db_->execPrepared(
                    "DELETE FROM schedule_slots WHERE day = ? AND start_time = ?",
                    {day, tStr}
                );
                db_->execPrepared(
                    "INSERT INTO schedule_slots (schedule_id, day, start_time, end_time) "
                    "VALUES (?, ?, ?, ?)",
                    {scheduleId, day, tStr, slotEndStr}
                );
            }
        }
        return true;
    });
}

bool ScheduleRepository::removeSlot(const QString& day, const QTime& startTime)
{
    auto q = db_->execPrepared(
        "DELETE FROM schedule_slots WHERE day = ? AND start_time = ?",
        {day, startTime.toString("HH:mm")}
    );
    return q.has_value();
}

bool ScheduleRepository::clearDay(const QString& day)
{
    auto q = db_->execPrepared(
        "DELETE FROM schedule_slots WHERE day = ?",
        {day}
    );
    return q.has_value();
}

// ══════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════

QString ScheduleRepository::generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

} // namespace sara
