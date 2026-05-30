#include "data/instant_play_repo.h"
#include "data/database.h"
#include "util/logger.h"

#include <QUuid>

namespace sara {

InstantPlayRepo::InstantPlayRepo(Database* db) : db_(db) {}

QString InstantPlayRepo::createPreset(const QString& name)
{
    QString id = generateId();
    auto q = db_->execPrepared(
        "INSERT INTO instant_play_presets (id, name) VALUES (?, ?)",
        {id, name});
    if (!q) return {};

    // Crear 12 slots vacíos
    for (int i = 0; i < 12; ++i) {
        db_->execPrepared(
            "INSERT INTO instant_play_slots (preset_id, slot_number) VALUES (?, ?)",
            {id, i});
    }
    return id;
}

QVector<InstantPreset> InstantPlayRepo::getAllPresets()
{
    QVector<InstantPreset> result;
    auto q = db_->execPrepared(
        "SELECT id, name FROM instant_play_presets ORDER BY name", {});
    if (!q) return result;

    while (q->next()) {
        InstantPreset p;
        p.id = q->value(0).toString();
        p.name = q->value(1).toString();
        result.append(p);
    }
    return result;
}

std::optional<InstantPreset> InstantPlayRepo::getPreset(const QString& id)
{
    auto q = db_->execPrepared(
        "SELECT id, name FROM instant_play_presets WHERE id = ?", {id});
    if (!q || !q->next()) return std::nullopt;

    InstantPreset p;
    p.id = q->value(0).toString();
    p.name = q->value(1).toString();

    // Cargar slots
    auto sq = db_->execPrepared(
        "SELECT slot_number, file_path, display_name "
        "FROM instant_play_slots WHERE preset_id = ? ORDER BY slot_number",
        {id});
    if (sq) {
        while (sq->next()) {
            InstantSlot s;
            s.slotNumber  = sq->value(0).toInt();
            s.filePath    = sq->value(1).toString();
            s.displayName = sq->value(2).toString();
            p.buttonSlots.append(s);
        }
    }

    // Asegurar 12 slots
    while (p.buttonSlots.size() < 12) {
        InstantSlot empty;
        empty.slotNumber = p.buttonSlots.size();
        p.buttonSlots.append(empty);
    }

    return p;
}

bool InstantPlayRepo::renamePreset(const QString& id, const QString& newName)
{
    auto q = db_->execPrepared(
        "UPDATE instant_play_presets SET name = ? WHERE id = ?",
        {newName, id});
    return q.has_value();
}

bool InstantPlayRepo::deletePreset(const QString& id)
{
    auto q = db_->execPrepared(
        "DELETE FROM instant_play_presets WHERE id = ?", {id});
    return q.has_value();
}

QString InstantPlayRepo::duplicatePreset(const QString& sourceId, const QString& newName)
{
    auto source = getPreset(sourceId);
    if (!source) return {};

    QString newId = createPreset(newName);
    if (newId.isEmpty()) return {};

    for (const auto& slot : source->buttonSlots) {
        if (!slot.filePath.isEmpty()) {
            setSlot(newId, slot.slotNumber, slot.filePath, slot.displayName);
        }
    }
    return newId;
}

bool InstantPlayRepo::setSlot(const QString& presetId, int slotNumber,
                               const QString& filePath, const QString& displayName)
{
    auto q = db_->execPrepared(
        "UPDATE instant_play_slots SET file_path = ?, display_name = ? "
        "WHERE preset_id = ? AND slot_number = ?",
        {filePath, displayName, presetId, slotNumber});

    if (!q) {
        // Slot row might not exist, insert it
        q = db_->execPrepared(
            "INSERT OR REPLACE INTO instant_play_slots "
            "(preset_id, slot_number, file_path, display_name) VALUES (?, ?, ?, ?)",
            {presetId, slotNumber, filePath, displayName});
    }
    return q.has_value();
}

bool InstantPlayRepo::clearSlot(const QString& presetId, int slotNumber)
{
    auto q = db_->execPrepared(
        "UPDATE instant_play_slots SET file_path = NULL, display_name = NULL "
        "WHERE preset_id = ? AND slot_number = ?",
        {presetId, slotNumber});
    return q.has_value();
}

std::optional<InstantSlot> InstantPlayRepo::getSlot(const QString& presetId, int slotNumber)
{
    auto q = db_->execPrepared(
        "SELECT slot_number, file_path, display_name "
        "FROM instant_play_slots WHERE preset_id = ? AND slot_number = ?",
        {presetId, slotNumber});
    if (!q || !q->next()) return std::nullopt;

    InstantSlot s;
    s.slotNumber  = q->value(0).toInt();
    s.filePath    = q->value(1).toString();
    s.displayName = q->value(2).toString();
    return s;
}

QString InstantPlayRepo::ensureDefaultPreset()
{
    auto allPresets = getAllPresets();
    if (!allPresets.isEmpty()) return allPresets.first().id;

    return createPreset("Predeterminado");
}

QString InstantPlayRepo::generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

} // namespace sara
