#ifndef SARA_CORE_PISADOR_MANAGER_H
#define SARA_CORE_PISADOR_MANAGER_H

#include "core/types.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace sara {

class AudioPipeline;
class Database;
class TimeAnnouncer;

/**
 * Gestor de pisadores — reproduce jingles/IDs encima de la música
 * con ducking automático del volumen principal.
 *
 * Tipos de asignación individual:
 *  - "specific": pisador concreto (ruta)
 *  - "random":   pisador aleatorio de la carpeta general
 *  - "time":     locución de hora completa
 *  - "none":     excluir esta pista de pisadores generales
 */
class PisadorManager : public QObject
{
    Q_OBJECT

public:
    explicit PisadorManager(QObject* parent = nullptr);

    void setDatabase(Database* db) { db_ = db; }
    void setInstantPipeline(AudioPipeline* pip) { instantPipeline_ = pip; }
    void setTimeAnnouncer(TimeAnnouncer* ann) { announcer_ = ann; }

    void setEnabled(bool on) { enabled_ = on; }
    void setFolder(const QString& folder) { folder_ = folder; }
    void setFrequency(int freq) { frequency_ = freq; }
    void setExcludedFolders(const QStringList& folders) { excludedFolders_ = folders; }
    void setDuckLevel(double level) { duckLevel_ = level; }
    void setDelaySecs(int secs) { delaySecs_ = secs; }

    /// Llamar cuando una pista comienza a reproducirse.
    void onTrackStarted(const QString& filePath, AudioPipeline* mainDeck);

    // ── Pisadores individuales ─────────────────────────
    struct Assignment {
        QString type;   // "specific", "random", "time", "none"
        QString path;
    };

    Assignment getAssignment(const QString& filePath);
    void setAssignment(const QString& filePath, const QString& type,
                       const QString& pisadorPath = {});
    void removeAssignment(const QString& filePath);
    bool hasAssignment(const QString& filePath);
    bool isExcluded(const QString& filePath);

    /// ¿Esta pista tiene un pisador asignado individualmente
    /// o será pisada por la regla general?
    bool hasPisadorIndicator(const QString& filePath);

signals:
    void duckingStarted(const QString& pisadorName);
    void duckingFinished();

private:
    QString resolvePisador(const QString& filePath);
    QString randomPisador();
    void executePisador(const QString& pisadorFile, AudioPipeline* mainDeck);
    void playNextTimeFile();
    void restoreVolume();

    Database*       db_ = nullptr;
    AudioPipeline*  instantPipeline_ = nullptr;
    TimeAnnouncer*  announcer_ = nullptr;

    bool        enabled_ = false;
    QString     folder_;
    int         frequency_ = 1;
    QStringList excludedFolders_;
    double      duckLevel_ = 0.25;
    int         delaySecs_ = 3;

    int         trackCounter_ = 0;
    AudioPipeline* duckedDeck_ = nullptr;
    double      originalVolume_ = 1.0;
    QStringList pendingTimeFiles_;
    QMetaObject::Connection timeConn_;

    // Defecto 1.13: timer persistente para fade-in al restaurar volumen.
    // Antes se hacía new QTimer(this) cada vez, lo que causaba leak si el
    // deck se detenía durante el fade (el timer huérfano persistía hasta
    // la destrucción del PisadorManager — habitualmente el final de la app).
    QTimer*     restoreFadeTimer_ = nullptr;
    int         restoreFadeStep_ = 0;
    double      restoreFadeStartVol_ = 0.0;
    double      restoreFadeTargetVol_ = 0.0;
};

} // namespace sara

#endif
