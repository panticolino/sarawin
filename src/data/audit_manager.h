#ifndef SARA_DATA_AUDIT_MANAGER_H
#define SARA_DATA_AUDIT_MANAGER_H

#include "core/types.h"
#include <QObject>
#include <QVector>
#include <QDate>
#include <QTime>
#include <QStringList>
#include <QPair>

namespace sara {

class Database;

/**
 * Gestor de auditoría — consultas sobre el historial de reproducción.
 * Filtros por rango de fechas, fuente, pipeline.
 * Estadísticas y exportación CSV.
 */
class AuditManager
{
public:
    explicit AuditManager(Database* db);

    // ── Registro ─────────────────────────────────────
    void recordPlay(const QString& filePath, const QString& displayName,
                    const QString& source, int64_t durationMs,
                    const QString& pipeline = "main");
    void recordFinish(const QString& filePath, int64_t durationMs = 0);

    // ── Historial ────────────────────────────────────
    struct HistoryEntry {
        int64_t   id;
        QString   filePath;
        QString   displayName;
        QDateTime playedAt;
        QDateTime finishedAt;
        QString   source;
        int64_t   durationMs;
        bool      completed;
        QString   pipeline;
    };

    QVector<HistoryEntry> getHistory(const QDate& from, const QDate& to,
                                      const QString& sourceFilter = {},
                                      const QString& pipelineFilter = {},
                                      int limit = 500);

    // ── Estadísticas ─────────────────────────────────
    struct TopTrack {
        QString displayName;
        QString filePath;
        int     playCount;
        int64_t totalDurationMs;
    };

    struct HourStat {
        int hour;       // 0-23
        int playCount;
    };

    struct SourceStat {
        QString source;
        int     playCount;
    };

    QVector<TopTrack>   getTopTracks(const QDate& from, const QDate& to, int limit = 10);
    QVector<HourStat>   getHourDistribution(const QDate& from, const QDate& to);
    QVector<SourceStat> getSourceDistribution(const QDate& from, const QDate& to);
    int                 getTotalPlays(const QDate& from, const QDate& to);
    int64_t             getTotalDurationMs(const QDate& from, const QDate& to);

    // ── Exportación CSV ──────────────────────────────
    bool exportCSV(const QString& filePath, const QDate& from, const QDate& to,
                   const QString& sourceFilter = {});

    // ── Monitores de auditoría ────────────────────────
    struct AuditMonitor {
        QString     id;
        QString     name;
        QStringList folders;
    };

    QVector<AuditMonitor> getMonitors();
    QString addMonitor(const QString& name, const QStringList& folders);
    void removeMonitor(const QString& id);
    void updateMonitor(const QString& id, const QString& name, const QStringList& folders);

    /// Porcentaje de reproducción de un monitor en un rango de fechas
    struct MonitorStats {
        int totalPlays = 0;       // Total de reproducciones (programación + respaldo)
        int monitorPlays = 0;     // Reproducciones que coinciden con el monitor
        double percentage = 0.0;  // monitorPlays / totalPlays * 100
    };
    MonitorStats getMonitorStats(const QString& monitorId, const QDate& from, const QDate& to);

    /// Verificar si un archivo pertenece a algún monitor
    bool fileMatchesMonitor(const QString& filePath, const QStringList& folders);

private:
    Database* db_;
};

} // namespace sara

#endif
