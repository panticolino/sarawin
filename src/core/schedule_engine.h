#ifndef SARA_CORE_SCHEDULE_ENGINE_H
#define SARA_CORE_SCHEDULE_ENGINE_H

#include "core/types.h"
#include <QObject>
#include <QTimer>
#include <QTime>
#include <optional>

namespace sara {

class Database;
class ScheduleRepository;
class TrackSelector;
class MetadataReader;
class TimeAnnouncer;

/**
 * Motor de programación principal.
 *
 * Conecta la grilla semanal con la reproducción real:
 * - Cada minuto verifica qué slot de la grilla está activo
 * - Cicla por los elementos de la programación asignada
 * - Detecta fronteras horarias y cambia de bloque
 * - Cae a fallback cuando no hay programación
 *
 * Flujo:
 *   MainWindow llama requestNextTrack() → el motor resuelve
 *   qué debe sonar → emite nextTrackReady() con el archivo.
 */
class ScheduleEngine : public QObject
{
    Q_OBJECT

public:
    explicit ScheduleEngine(QObject* parent = nullptr);
    ~ScheduleEngine() override;

    // Dependencias (inyectadas por MainWindow)
    void setDatabase(Database* db) { db_ = db; }
    void setScheduleRepository(ScheduleRepository* repo) { repo_ = repo; }
    void setTrackSelector(TrackSelector* selector) { selector_ = selector; }
    void setMetadataReader(MetadataReader* reader) { reader_ = reader; }
    void setTimeAnnouncer(TimeAnnouncer* announcer) { announcer_ = announcer; }
    void setFallbackFolder(const QString& folder) { fallbackFolder_ = folder; }

    /// Iniciar el motor
    void start();

    /// Detener
    void stop();

    /// ¿Está activo?
    bool isRunning() const { return running_; }

    /// Solicitar la siguiente pista. El motor la resuelve y emite nextTrackReady().
    void requestNextTrack();

    /// Nombre del bloque activo actualmente (vacío si fallback)
    QString activeScheduleName() const { return activeScheduleName_; }

    /// ¿Está en modo fallback?
    bool isFallback() const { return inFallback_; }

    /// Obtener info de la pista resuelta (para agregar a la cola con metadata)
    struct ResolvedTrack {
        QString filePath;
        QString displayName;
        QString source;      // "schedule:Mañanas de salsa" o "fallback"
        int64_t durationMs;
    };

    /// Resolver la siguiente pista sin emitir señal (para pre-llenar cola)
    std::optional<ResolvedTrack> resolveNextTrack();

    /// Forzar re-verificación de programación (útil tras editar la grilla)
    void checkSchedule();

signals:
    /// Pista lista para reproducir
    void nextTrackReady(const QString& filePath, const QString& displayName,
                        const QString& source, qint64 durationMs);

    /// Cambió el bloque activo
    void activeScheduleChanged(const QString& scheduleName);

    /// Entró en modo fallback (no hay programación)
    void fallbackActivated();

    /// Salió de fallback (hay programación activa)
    void scheduleActivated(const QString& scheduleName);

private:
    // Determinar qué slot está activo ahora
    struct ActiveSlotInfo {
        QString scheduleId;
        QString scheduleName;
        QTime   slotStart;
        QTime   slotEnd;
        QVector<ScheduleElement> elements;
    };

    std::optional<ActiveSlotInfo> findActiveSlot();

    // Resolver un elemento de programación a un archivo concreto
    QString resolveElement(const ScheduleElement& element);

    // Avanzar al siguiente elemento en el ciclo
    void advanceCyclePosition();

    // Helpers
    QString currentDayName() const;

    // Dependencias
    Database*           db_ = nullptr;
    ScheduleRepository* repo_ = nullptr;
    TrackSelector*      selector_ = nullptr;
    MetadataReader*     reader_ = nullptr;
    TimeAnnouncer*      announcer_ = nullptr;

    // Estado
    bool    running_ = false;
    bool    inFallback_ = true;
    QString activeScheduleId_;
    QString activeScheduleName_;
    int     cyclePosition_ = 0;
    QTime   currentSlotEnd_;

    // Cola de archivos pendientes de locución de hora
    QStringList pendingAnnouncementFiles_;

    QString fallbackFolder_;

    // Timer de verificación periódica
    QTimer* checkTimer_ = nullptr;
};

} // namespace sara

#endif
