#ifndef SARA_DATA_STATE_MANAGER_H
#define SARA_DATA_STATE_MANAGER_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QDateTime>

namespace sara {

/**
 * Guardado periódico de estado para recuperación tras crash.
 *
 * Cada 30 segundos guarda en ~/.config/saralibre/state.json:
 * - Modo (automático/manual)
 * - Pista actual y posición
 * - Cola de reproducción
 * - Programación activa
 * - Timestamp del guardado
 *
 * Al iniciar, si el estado tiene menos de 5 minutos de antigüedad,
 * SARA puede retomar la programación automáticamente.
 */
class StateManager : public QObject
{
    Q_OBJECT

public:
    explicit StateManager(QObject* parent = nullptr);

    struct SavedState {
        bool        valid = false;
        bool        autoMode = true;
        QString     currentTrack;
        QString     currentSource;
        int64_t     positionMs = 0;
        QStringList queueTracks;
        QStringList queueNames;
        QList<int64_t> queueDurations;
        QString     activeSchedule;
        QDateTime   savedAt;
        double      volume = 0.75;
    };

    /// Iniciar guardado periódico (cada 30 segundos)
    void start();
    void stop();

    /// Guardar estado inmediatamente
    void saveNow();

    /// Cargar el último estado guardado
    SavedState loadState();

    /// Eliminar el archivo de estado (al cerrar limpiamente)
    void clearState();

    /// ¿Hay un estado reciente (menos de 5 minutos)?
    bool hasRecentState();

    // Setters para el estado actual
    void setAutoMode(bool on) { autoMode_ = on; }
    void setCurrentTrack(const QString& track, const QString& source, int64_t posMs) {
        currentTrack_ = track; currentSource_ = source; positionMs_ = posMs;
    }
    void setQueue(const QStringList& tracks, const QStringList& names,
                  const QList<int64_t>& durations) {
        queueTracks_ = tracks; queueNames_ = names; queueDurations_ = durations;
    }
    void setActiveSchedule(const QString& name) { activeSchedule_ = name; }
    void setVolume(double vol) { volume_ = vol; }

private:
    QString stateFilePath() const;

    QTimer* timer_ = nullptr;

    bool        autoMode_ = true;
    QString     currentTrack_;
    QString     currentSource_;
    int64_t     positionMs_ = 0;
    QStringList queueTracks_;
    QStringList queueNames_;
    QList<int64_t> queueDurations_;
    QString     activeSchedule_;
    double      volume_ = 0.75;
};

} // namespace sara

#endif
