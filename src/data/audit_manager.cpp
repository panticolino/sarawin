#include "data/audit_manager.h"
#include "data/database.h"
#include "util/logger.h"

#include <QFile>
#include <QTextStream>
#include <QUuid>

namespace sara {

AuditManager::AuditManager(Database* db) : db_(db) {}

// ══════════════════════════════════════════════════════════
// Registro
// ══════════════════════════════════════════════════════════

void AuditManager::recordPlay(const QString& filePath, const QString& displayName,
                               const QString& source, int64_t durationMs,
                               const QString& pipeline)
{
    db_->execPrepared(
        "INSERT INTO play_history (file_path, display_name, played_at, source, duration_ms, pipeline, completed) "
        "VALUES (?, ?, datetime('now', 'localtime'), ?, ?, ?, 0)",
        {filePath, displayName, source, static_cast<qlonglong>(durationMs), pipeline}
    );
}

void AuditManager::recordFinish(const QString& filePath, int64_t durationMs)
{
    db_->execPrepared(
        "UPDATE play_history SET completed = 1, "
        "finished_at = datetime('now', 'localtime'), "
        "duration_ms = ? "
        "WHERE id = (SELECT id FROM play_history "
        "WHERE file_path = ? AND completed = 0 ORDER BY id DESC LIMIT 1)",
        {static_cast<qlonglong>(durationMs), filePath}
    );
}

// ══════════════════════════════════════════════════════════
// Historial
// ══════════════════════════════════════════════════════════

QVector<AuditManager::HistoryEntry> AuditManager::getHistory(
    const QDate& from, const QDate& to,
    const QString& sourceFilter, const QString& pipelineFilter, int limit)
{
    QString sql =
        "SELECT id, file_path, display_name, played_at, finished_at, "
        "source, duration_ms, completed, pipeline "
        "FROM play_history WHERE played_at >= ? AND played_at < ?";

    QVariantList bindings;
    bindings << from.toString(Qt::ISODate)
             << to.addDays(1).toString(Qt::ISODate);

    if (!sourceFilter.isEmpty()) {
        if (sourceFilter == "fallback") {
            sql += " AND source = 'fallback'";
        } else {
            sql += " AND source LIKE ?";
            bindings << ("%" + sourceFilter + "%");
        }
    }
    if (!pipelineFilter.isEmpty()) {
        sql += " AND pipeline = ?";
        bindings << pipelineFilter;
    }

    sql += " ORDER BY played_at DESC LIMIT ?";
    bindings << limit;

    QVector<HistoryEntry> result;
    auto q = db_->execPrepared(sql, bindings);
    if (!q) return result;

    while (q->next()) {
        HistoryEntry e;
        e.id          = q->value(0).toLongLong();
        e.filePath    = q->value(1).toString();
        e.displayName = q->value(2).toString();
        e.playedAt    = QDateTime::fromString(q->value(3).toString(), Qt::ISODate);
        e.finishedAt  = QDateTime::fromString(q->value(4).toString(), Qt::ISODate);
        e.source      = q->value(5).toString();
        e.durationMs  = q->value(6).toLongLong();
        e.completed   = q->value(7).toBool();
        e.pipeline    = q->value(8).toString();
        result.append(e);
    }
    return result;
}

// ══════════════════════════════════════════════════════════
// Estadísticas
// ══════════════════════════════════════════════════════════

QVector<AuditManager::TopTrack> AuditManager::getTopTracks(
    const QDate& from, const QDate& to, int limit)
{
    QVector<TopTrack> result;
    auto q = db_->execPrepared(
        "SELECT COALESCE(display_name, file_path), file_path, COUNT(*) as cnt, "
        "SUM(duration_ms) as total_dur "
        "FROM play_history WHERE played_at >= ? AND played_at < ? "
        "GROUP BY file_path ORDER BY cnt DESC LIMIT ?",
        {from.toString(Qt::ISODate), to.addDays(1).toString(Qt::ISODate), limit}
    );
    if (!q) return result;

    while (q->next()) {
        TopTrack t;
        t.displayName    = q->value(0).toString();
        t.filePath       = q->value(1).toString();
        t.playCount      = q->value(2).toInt();
        t.totalDurationMs = q->value(3).toLongLong();
        result.append(t);
    }
    return result;
}

QVector<AuditManager::HourStat> AuditManager::getHourDistribution(
    const QDate& from, const QDate& to)
{
    QVector<HourStat> result(24);
    for (int i = 0; i < 24; ++i) {
        result[i].hour = i;
        result[i].playCount = 0;
    }

    auto q = db_->execPrepared(
        "SELECT CAST(strftime('%H', played_at) AS INTEGER) as hr, COUNT(*) "
        "FROM play_history WHERE played_at >= ? AND played_at < ? "
        "GROUP BY hr",
        {from.toString(Qt::ISODate), to.addDays(1).toString(Qt::ISODate)}
    );
    if (!q) return result;

    while (q->next()) {
        int hour = q->value(0).toInt();
        if (hour >= 0 && hour < 24) {
            result[hour].playCount = q->value(1).toInt();
        }
    }
    return result;
}

QVector<AuditManager::SourceStat> AuditManager::getSourceDistribution(
    const QDate& from, const QDate& to)
{
    QVector<SourceStat> result;
    auto q = db_->execPrepared(
        "SELECT COALESCE(source, 'desconocido'), COUNT(*) "
        "FROM play_history WHERE played_at >= ? AND played_at < ? "
        "GROUP BY source ORDER BY COUNT(*) DESC",
        {from.toString(Qt::ISODate), to.addDays(1).toString(Qt::ISODate)}
    );
    if (!q) return result;

    while (q->next()) {
        SourceStat s;
        s.source    = q->value(0).toString();
        s.playCount = q->value(1).toInt();
        result.append(s);
    }
    return result;
}

int AuditManager::getTotalPlays(const QDate& from, const QDate& to)
{
    auto q = db_->execPrepared(
        "SELECT COUNT(*) FROM play_history WHERE played_at >= ? AND played_at < ?",
        {from.toString(Qt::ISODate), to.addDays(1).toString(Qt::ISODate)}
    );
    if (q && q->next()) return q->value(0).toInt();
    return 0;
}

int64_t AuditManager::getTotalDurationMs(const QDate& from, const QDate& to)
{
    auto q = db_->execPrepared(
        "SELECT COALESCE(SUM(duration_ms), 0) FROM play_history "
        "WHERE played_at >= ? AND played_at < ?",
        {from.toString(Qt::ISODate), to.addDays(1).toString(Qt::ISODate)}
    );
    if (q && q->next()) return q->value(0).toLongLong();
    return 0;
}

// ══════════════════════════════════════════════════════════
// Exportación CSV
// ══════════════════════════════════════════════════════════

bool AuditManager::exportCSV(const QString& filePath, const QDate& from,
                              const QDate& to, const QString& sourceFilter)
{
    auto history = getHistory(from, to, sourceFilter, {}, 100000);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR("[Audit] No se pudo crear CSV: {}", filePath.toStdString());
        return false;
    }

    QTextStream out(&file);
    // BOM para Excel
    out << "\xEF\xBB\xBF";
    // Header
    out << "Fecha,Hora,Título,Archivo,Fuente,Duración(s),Pipeline,Completado\n";

    for (const auto& e : history) {
        QString durSecs = QString::number(e.durationMs / 1000.0, 'f', 1);
        // Escapar comillas en campos
        QString name = e.displayName;
        name.replace('"', "\"\"");
        QString path = e.filePath;
        path.replace('"', "\"\"");

        out << e.playedAt.date().toString("dd/MM/yyyy") << ","
            << e.playedAt.time().toString("HH:mm:ss") << ","
            << "\"" << name << "\","
            << "\"" << path << "\","
            << "\"" << e.source << "\","
            << durSecs << ","
            << e.pipeline << ","
            << (e.completed ? "Sí" : "No") << "\n";
    }

    file.close();
    LOG_INFO("[Audit] CSV exportado: {} ({} registros)", filePath.toStdString(), history.size());
    return true;
}

// ══════════════════════════════════════════════════════════
// Monitores de auditoría
// ══════════════════════════════════════════════════════════

QVector<AuditManager::AuditMonitor> AuditManager::getMonitors()
{
    QVector<AuditMonitor> result;
    auto q = db_->execPrepared("SELECT id, name FROM audit_monitors ORDER BY name", {});
    if (!q) return result;

    while (q->next()) {
        AuditMonitor m;
        m.id = q->value(0).toString();
        m.name = q->value(1).toString();

        // Cargar carpetas
        auto fq = db_->execPrepared(
            "SELECT folder_path FROM audit_monitor_folders WHERE monitor_id = ?",
            {m.id});
        if (fq) {
            while (fq->next()) {
                m.folders << fq->value(0).toString();
            }
        }
        result.append(m);
    }
    return result;
}

QString AuditManager::addMonitor(const QString& name, const QStringList& folders)
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    db_->transaction([&]() {
        db_->execPrepared("INSERT INTO audit_monitors (id, name) VALUES (?, ?)",
                          {id, name});
        for (const auto& folder : folders) {
            db_->execPrepared(
                "INSERT INTO audit_monitor_folders (monitor_id, folder_path) VALUES (?, ?)",
                {id, folder});
        }
        return true;
    });
    LOG_INFO("[Audit] Monitor creado: {} ({} carpetas)", name.toStdString(), folders.size());
    return id;
}

void AuditManager::removeMonitor(const QString& id)
{
    db_->execPrepared("DELETE FROM audit_monitors WHERE id = ?", {id});
}

void AuditManager::updateMonitor(const QString& id, const QString& name,
                                  const QStringList& folders)
{
    db_->transaction([&]() {
        db_->execPrepared("UPDATE audit_monitors SET name = ? WHERE id = ?", {name, id});
        db_->execPrepared("DELETE FROM audit_monitor_folders WHERE monitor_id = ?", {id});
        for (const auto& folder : folders) {
            db_->execPrepared(
                "INSERT INTO audit_monitor_folders (monitor_id, folder_path) VALUES (?, ?)",
                {id, folder});
        }
        return true;
    });
}

bool AuditManager::fileMatchesMonitor(const QString& filePath, const QStringList& folders)
{
    for (const auto& folder : folders) {
        if (filePath.startsWith(folder)) return true;
    }
    return false;
}

AuditManager::MonitorStats AuditManager::getMonitorStats(const QString& monitorId,
                                                          const QDate& from, const QDate& to)
{
    MonitorStats stats;

    // Obtener carpetas del monitor
    QStringList folders;
    auto fq = db_->execPrepared(
        "SELECT folder_path FROM audit_monitor_folders WHERE monitor_id = ?",
        {monitorId});
    if (fq) {
        while (fq->next()) folders << fq->value(0).toString();
    }
    if (folders.isEmpty()) return stats;

    // Obtener todas las reproducciones musicales (excluir eventos)
    auto q = db_->execPrepared(
        "SELECT file_path FROM play_history "
        "WHERE played_at >= ? AND played_at < ? "
        "AND (source LIKE 'schedule:%' OR source = 'fallback')",
        {from.toString(Qt::ISODate),
         to.addDays(1).toString(Qt::ISODate)});

    if (!q) return stats;

    while (q->next()) {
        stats.totalPlays++;
        QString fp = q->value(0).toString();
        if (fileMatchesMonitor(fp, folders)) {
            stats.monitorPlays++;
        }
    }

    if (stats.totalPlays > 0) {
        stats.percentage = (static_cast<double>(stats.monitorPlays) / stats.totalPlays) * 100.0;
    }

    return stats;
}

} // namespace sara
