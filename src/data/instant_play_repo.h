#ifndef SARA_DATA_INSTANT_PLAY_REPO_H
#define SARA_DATA_INSTANT_PLAY_REPO_H

#include <QString>
#include <QVector>
#include <optional>

namespace sara {

class Database;

struct InstantSlot {
    int     slotNumber = -1;  // 0-11
    QString filePath;
    QString displayName;
};

struct InstantPreset {
    QString id;
    QString name;
    QVector<InstantSlot> buttonSlots;  // 12 slots
};

/**
 * Repositorio de presets de Instant Play.
 * Cada preset contiene 12 slots (F1-F12) con audio asignado.
 */
class InstantPlayRepo
{
public:
    explicit InstantPlayRepo(Database* db);

    // Presets
    QString createPreset(const QString& name);
    QVector<InstantPreset> getAllPresets();
    std::optional<InstantPreset> getPreset(const QString& id);
    bool renamePreset(const QString& id, const QString& newName);
    bool deletePreset(const QString& id);
    QString duplicatePreset(const QString& sourceId, const QString& newName);

    // Slots individuales
    bool setSlot(const QString& presetId, int slotNumber,
                 const QString& filePath, const QString& displayName);
    bool clearSlot(const QString& presetId, int slotNumber);
    std::optional<InstantSlot> getSlot(const QString& presetId, int slotNumber);

    /// Asegurar que existe el preset "default", crearlo si no
    QString ensureDefaultPreset();

private:
    QString generateId();
    Database* db_;
};

} // namespace sara

#endif
