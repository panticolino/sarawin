#include "core/track_selector.h"
#include "data/database.h"
#include "util/file_scanner.h"
#include "util/logger.h"

#include <QDateTime>
#include <QFileInfo>
#include <QMap>
#include <QRandomGenerator>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <algorithm>

namespace sara {

TrackSelector::TrackSelector(QObject* parent)
    : QObject(parent)
{
}

const QStringList& TrackSelector::filesForFolder(const QString& folderPath)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool sameFolder = (folderPath == cachedFolder_);
    const bool fresh = !cachedFiles_.isEmpty()
                    && sameFolder
                    && (nowMs - cacheTimeMs_) < static_cast<qint64>(cacheTtlSec_) * 1000;

    if (fresh) {
        return cachedFiles_;  // Reutilizar: NO tocar el disco de red
    }

    // (Re)escanear una sola vez. Esto sigue corriendo en el hilo principal, así
    // que la PRIMERA vez (al arrancar) puede tardar un instante en un disco de
    // red; pero ya no se repite en cada cambio de canción.
    cachedFiles_  = FileScanner::scanFolder(folderPath, true);
    cachedFolder_ = folderPath;
    cacheTimeMs_  = nowMs;
    LOG_INFO("[TrackSelector] Carpeta escaneada: {} ({} archivos) — en caché por {}s",
             folderPath.toStdString(), cachedFiles_.size(), cacheTtlSec_);
    return cachedFiles_;
}

QString TrackSelector::selectFromFolder(const QString& folderPath, const QString& source)
{
    const QStringList& allFiles = filesForFolder(folderPath);
    if (allFiles.isEmpty()) {
        LOG_WARN("[TrackSelector] No hay archivos de audio en: {}", folderPath.toStdString());
        return {};
    }

    // Filtrar: excluir reproducidas recientemente Y las que ya están en cola
    QStringList available;
    for (const auto& f : allFiles) {
        if (!wasRecentlyPlayed(f) && !pendingTracks_.contains(f)) {
            available << f;
        }
    }

    QString selected;

    if (!available.isEmpty()) {
        // Caso normal: hay pistas frescas disponibles
        if (noRepeatArtistTracks_ > 0 && available.size() > 1) {
            // Anti-repetición por artista: intentar hasta 15 veces
            for (int attempt = 0; attempt < 15; ++attempt) {
                int idx = QRandomGenerator::global()->bounded(available.size());
                QString candidate = available[idx];
                QString artist = readArtist(candidate);

                if (artist.isEmpty() || !recentArtists_.contains(artist, Qt::CaseInsensitive)) {
                    selected = candidate;
                    break;
                }

                if (attempt == 14) {
                    // Fallback: usar la última selección aunque repita artista
                    selected = candidate;
                    LOG_INFO("[TrackSelector] Anti-repetición artista agotada, usando: {}",
                             QFileInfo(candidate).fileName().toStdString());
                }
            }
        } else {
            int idx = QRandomGenerator::global()->bounded(available.size());
            selected = available[idx];
        }

        LOG_INFO("[TrackSelector] Seleccionado: {} (de {} disponibles / {} total / {} en cola, fuente: {})",
                  QFileInfo(selected).fileName().toStdString(),
                  available.size(), allFiles.size(), pendingTracks_.size(),
                  source.isEmpty() ? "sin fuente" : source.toStdString());
    } else {
        // Pool agotado: todas reproducidas recientemente o en cola.
        // Elegir la que fue reproducida hace MÁS TIEMPO (excluyendo las de cola).
        QStringList candidates;
        for (const auto& f : allFiles) {
            if (!pendingTracks_.contains(f)) {
                candidates << f;
            }
        }

        if (candidates.isEmpty()) {
            // Incluso todas en cola (cola > total de pistas). Usar todas.
            candidates = allFiles;
        }

        selected = selectOldestPlayed(candidates);

        LOG_INFO("[TrackSelector] Pool agotado, seleccionada más antigua: {} (fuente: {})",
                  QFileInfo(selected).fileName().toStdString(),
                  source.isEmpty() ? "sin fuente" : source.toStdString());
    }

    // Marcar como pendiente automáticamente
    if (!selected.isEmpty()) {
        pendingTracks_.insert(selected);

        // Registrar artista al SELECCIONAR (no al reproducir)
        // para que la cola completa se considere en anti-repetición
        if (noRepeatArtistTracks_ > 0) {
            QString artist = readArtist(selected);
            if (!artist.isEmpty()) {
                recordArtist(artist);
            }
        }
    }

    return selected;
}

void TrackSelector::markAsPending(const QString& filePath)
{
    pendingTracks_.insert(filePath);
}

void TrackSelector::unmarkPending(const QString& filePath)
{
    pendingTracks_.remove(filePath);
}

void TrackSelector::clearPending()
{
    pendingTracks_.clear();
}

QString TrackSelector::selectOldestPlayed(const QStringList& tracks) const
{
    if (!db_ || tracks.isEmpty()) {
        // Sin BD, fallback a aleatorio
        return tracks[QRandomGenerator::global()->bounded(tracks.size())];
    }

    // Buscar la fecha de última reproducción de cada pista.
    // Las que nunca se reprodujeron (no están en play_history) son las más "viejas".
    QString oldest;
    QDateTime oldestTime = QDateTime::currentDateTime();

    // Para evitar N queries individuales, hacemos una sola con todas las pistas
    // que SÍ tienen historial, y luego comparamos.
    // Primero: buscar las que tienen historial reciente
    QMap<QString, QDateTime> lastPlayTimes;

    auto q = db_->execPrepared(
        "SELECT file_path, MAX(played_at) as last_play "
        "FROM play_history "
        "GROUP BY file_path "
        "ORDER BY last_play ASC",
        {}
    );

    if (q) {
        while (q->next()) {
            QString path = q->value(0).toString();
            QDateTime lastPlay = QDateTime::fromString(q->value(1).toString(), Qt::ISODate);
            lastPlayTimes[path] = lastPlay;
        }
    }

    // Recorrer las pistas y encontrar la más vieja
    QStringList neverPlayed;

    for (const auto& track : tracks) {
        if (!lastPlayTimes.contains(track)) {
            // Nunca reproducida: candidata perfecta
            neverPlayed << track;
        } else {
            QDateTime lastPlay = lastPlayTimes[track];
            if (lastPlay < oldestTime) {
                oldestTime = lastPlay;
                oldest = track;
            }
        }
    }

    // Si hay pistas que nunca se reprodujeron, elegir aleatoriamente entre ellas
    if (!neverPlayed.isEmpty()) {
        int idx = QRandomGenerator::global()->bounded(neverPlayed.size());
        return neverPlayed[idx];
    }

    // Si todas se reprodujeron, devolver la más antigua
    if (!oldest.isEmpty()) {
        return oldest;
    }

    // Fallback último: aleatorio
    return tracks[QRandomGenerator::global()->bounded(tracks.size())];
}

void TrackSelector::recordPlay(const QString& filePath, const QString& source, int64_t durationMs)
{
    if (!db_) return;

    // Defensa: la columna file_path tiene NOT NULL. En vez de fallar el INSERT
    // con un error en logs (que ensucia el output y puede asustar al operador),
    // rechazamos silenciosamente. Quien quiera registrar un stream debe pasar
    // su URL como filePath.
    if (filePath.isEmpty()) {
        LOG_DEBUG("[TrackSelector] recordPlay omitido (filePath vacío)");
        return;
    }

    db_->execPrepared(
        "INSERT INTO play_history (file_path, played_at, source, duration_ms) "
        "VALUES (?, datetime('now', 'localtime'), ?, ?)",
        {filePath, source, static_cast<qlonglong>(durationMs)}
    );
}

void TrackSelector::recordArtist(const QString& artist)
{
    if (artist.isEmpty() || noRepeatArtistTracks_ <= 0) return;

    recentArtists_.prepend(artist);

    // Mantener solo los últimos N artistas
    while (recentArtists_.size() > noRepeatArtistTracks_) {
        recentArtists_.removeLast();
    }

    LOG_INFO("[TrackSelector] Artista registrado: {} (historial: {})",
             artist.toStdString(), recentArtists_.size());
}

QString TrackSelector::readArtist(const QString& filePath)
{
    TagLib::FileRef f(filePath.toUtf8().constData());
    if (f.isNull() || !f.tag()) return {};

    QString artist = QString::fromStdString(f.tag()->artist().to8Bit(true)).trimmed();
    return artist;
}

bool TrackSelector::wasRecentlyPlayed(const QString& filePath) const
{
    if (!db_ || noRepeatHours_ <= 0) return false;

    QDateTime cutoff = QDateTime::currentDateTime().addSecs(-noRepeatHours_ * 3600);

    auto q = db_->execPrepared(
        "SELECT COUNT(*) FROM play_history "
        "WHERE file_path = ? AND played_at > ?",
        {filePath, cutoff.toString(Qt::ISODate)}
    );

    if (q && q->next()) {
        return q->value(0).toInt() > 0;
    }
    return false;
}

} // namespace sara
