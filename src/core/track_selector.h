#ifndef SARA_CORE_TRACK_SELECTOR_H
#define SARA_CORE_TRACK_SELECTOR_H

#include "core/types.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>

namespace sara {

class Database;

/**
 * Selecciona pistas de audio desde carpetas, evitando repeticiones recientes.
 * Consulta la tabla play_history de la BD para saber qué se reprodujo.
 */
class TrackSelector : public QObject
{
    Q_OBJECT

public:
    explicit TrackSelector(QObject* parent = nullptr);

    void setDatabase(Database* db) { db_ = db; }
    void setNoRepeatHours(int hours) { noRepeatHours_ = hours; }
    void setNoRepeatArtistTracks(int tracks) { noRepeatArtistTracks_ = tracks; }

    /// Seleccionar una pista aleatoria de una carpeta, sin repetir en N horas
    /// ni repetir pistas que ya están en la cola pendiente.
    QString selectFromFolder(const QString& folderPath, const QString& source = {});

    /// Registrar que una pista fue reproducida (se llama al terminar)
    void recordPlay(const QString& filePath, const QString& source, int64_t durationMs = 0);

    /// Registrar el artista de la pista que se va a reproducir
    void recordArtist(const QString& artist);

    /// Obtener el artista de un archivo de audio (via TagLib)
    static QString readArtist(const QString& filePath);

    /// Verificar si una pista fue reproducida recientemente
    bool wasRecentlyPlayed(const QString& filePath) const;

    /// Marcar una pista como "en cola" (excluida de futuras selecciones)
    void markAsPending(const QString& filePath);

    /// Quitar una pista del set de pendientes (cuando se reproduce o se elimina de la cola)
    void unmarkPending(const QString& filePath);

    /// Limpiar todas las pistas pendientes
    void clearPending();

private:
    /// Cuando todas las pistas fueron reproducidas, elegir la más antigua
    QString selectOldestPlayed(const QStringList& tracks) const;

    Database* db_ = nullptr;
    int noRepeatHours_ = 3;
    int noRepeatArtistTracks_ = 0;  // 0 = disabled
    QSet<QString> pendingTracks_;
    QStringList recentArtists_;     // Últimos N artistas reproducidos
};

} // namespace sara

#endif // SARA_CORE_TRACK_SELECTOR_H
