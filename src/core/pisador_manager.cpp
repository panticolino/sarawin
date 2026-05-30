#include "core/pisador_manager.h"
#include "audio/audio_pipeline.h"
#include "core/time_announcer.h"
#include "data/database.h"
#include "util/logger.h"
#include "util/file_scanner.h"
#include "util/qt_connect_helpers.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QRandomGenerator>

namespace sara {

PisadorManager::PisadorManager(QObject* parent)
    : QObject(parent)
{
}

void PisadorManager::onTrackStarted(const QString& filePath, AudioPipeline* mainDeck)
{
    if (!enabled_ || !instantPipeline_ || filePath.isEmpty()) return;

    // 1. Verificar asignación individual (tiene prioridad absoluta)
    auto assignment = getAssignment(filePath);
    if (!assignment.type.isEmpty()) {
        if (assignment.type == "none") {
            // Excluida individualmente: contar pero no pisar
            trackCounter_++;
            return;
        }
        // Individual asignado: pisar sin importar carpeta excluida
        // (el usuario ya confirmó el conflicto al asignar)
        QString pisadorFile;
        if (assignment.type == "specific" && !assignment.path.isEmpty()) {
            pisadorFile = assignment.path;
        } else if (assignment.type == "random") {
            pisadorFile = randomPisador();
        } else if (assignment.type == "time") {
            pisadorFile = "__PISADOR_TIME__";
        }
        if (!pisadorFile.isEmpty()) {
            int delayMs = delaySecs_ * 1000;
            QTimer::singleShot(delayMs, this, [this, pisadorFile, mainDeck]() {
                if (mainDeck->isPlaying()) {
                    executePisador(pisadorFile, mainDeck);
                }
            });
        }
        trackCounter_++;
        return;
    }

    // 2. Verificar exclusión por carpeta (solo para pisadores generales)
    if (isExcluded(filePath)) {
        LOG_DEBUG("[Pisador] Pista excluida por carpeta: {}",
                  QFileInfo(filePath).fileName().toStdString());
        trackCounter_++;
        return;
    }

    // 3. Pisador general: verificar frecuencia
    //    -1 = Solo manual (no pisar con generales)
    if (frequency_ < 0) return;

    trackCounter_++;
    if (frequency_ > 0) {
        if ((trackCounter_ % (frequency_ + 1)) != 0) {
            return;  // No toca pisar en este ciclo
        }
    }

    // Elegir pisador aleatorio
    QString pisadorFile = randomPisador();
    if (pisadorFile.isEmpty()) return;

    int delayMs = delaySecs_ * 1000;
    QTimer::singleShot(delayMs, this, [this, pisadorFile, mainDeck]() {
        if (mainDeck->isPlaying()) {
            executePisador(pisadorFile, mainDeck);
        }
    });
}

QString PisadorManager::resolvePisador(const QString& filePath)
{
    Q_UNUSED(filePath);
    return randomPisador();
}

QString PisadorManager::randomPisador()
{
    if (folder_.isEmpty() || !QDir(folder_).exists()) return {};

    QStringList files = FileScanner::scanFolder(folder_);
    if (files.isEmpty()) return {};

    int idx = QRandomGenerator::global()->bounded(files.size());
    return files[idx];
}

void PisadorManager::executePisador(const QString& pisadorFile, AudioPipeline* mainDeck)
{
    if (!mainDeck || !instantPipeline_) return;

    // Marcador especial: locución de hora completa
    if (pisadorFile == "__PISADOR_TIME__" && announcer_) {
        playNextTimeFile();  // Inicia la secuencia
        // Pero primero: duckear
        duckedDeck_ = mainDeck;
        originalVolume_ = mainDeck->volume();
        mainDeck->setVolume(originalVolume_ * duckLevel_);
        emit duckingStarted(tr("Locución de hora"));

        pendingTimeFiles_ = announcer_->generateForCurrentTime();
        if (pendingTimeFiles_.isEmpty()) {
            restoreVolume();
            return;
        }

        LOG_INFO("[Pisador] Duck {:.0f}% → locución de hora ({} archivos)",
                 duckLevel_ * 100, pendingTimeFiles_.size());

        // Reproducir primer archivo
        QString first = pendingTimeFiles_.takeFirst();
        instantPipeline_->play(first);

        // Conectar para reproducir los siguientes al terminar cada uno
        timeConn_ = connect(instantPipeline_, &AudioPipeline::trackFinished,
                            this, &PisadorManager::playNextTimeFile);
        return;
    }

    if (!QFileInfo::exists(pisadorFile)) {
        LOG_WARN("[Pisador] Archivo no encontrado: {}", pisadorFile.toStdString());
        return;
    }

    // Duckear
    duckedDeck_ = mainDeck;
    originalVolume_ = mainDeck->volume();
    mainDeck->setVolume(originalVolume_ * duckLevel_);

    LOG_INFO("[Pisador] Duck {:.0f}% → reproduciendo: {}",
             duckLevel_ * 100, QFileInfo(pisadorFile).fileName().toStdString());

    emit duckingStarted(QFileInfo(pisadorFile).completeBaseName());

    // Reproducir pisador en InstantPlay
    instantPipeline_->play(pisadorFile);

    // Cuando termine, restaurar volumen
    connectOnce(instantPipeline_, &AudioPipeline::trackFinished,
                this, [this]() {
        restoreVolume();
    });
}

void PisadorManager::playNextTimeFile()
{
    if (pendingTimeFiles_.isEmpty()) {
        // Todos los archivos reproducidos
        disconnect(timeConn_);
        restoreVolume();
        return;
    }

    QString next = pendingTimeFiles_.takeFirst();
    if (QFileInfo::exists(next)) {
        instantPipeline_->play(next);
    } else {
        // Skip archivo faltante, intentar siguiente
        playNextTimeFile();
    }
}

void PisadorManager::restoreVolume()
{
    if (!duckedDeck_) {
        emit duckingFinished();
        return;
    }

    // Defecto 1.13: usar un timer persistente en lugar de crear uno nuevo
    // cada vez. Si el deck se detiene durante el fade, el timer simplemente
    // se detiene en lugar de quedar huérfano consumiendo memoria.
    if (!restoreFadeTimer_) {
        restoreFadeTimer_ = new QTimer(this);
        connect(restoreFadeTimer_, &QTimer::timeout, this, [this]() {
            const int totalSteps = 20;
            restoreFadeStep_++;
            double t = static_cast<double>(restoreFadeStep_) / totalSteps;
            double vol = restoreFadeStartVol_
                + (restoreFadeTargetVol_ - restoreFadeStartVol_) * t;
            if (duckedDeck_) duckedDeck_->setVolume(vol);

            if (restoreFadeStep_ >= totalSteps) {
                restoreFadeTimer_->stop();
                duckedDeck_ = nullptr;
                emit duckingFinished();
                LOG_DEBUG("[Pisador] Volumen restaurado");
            }
        });
    }

    // Si el timer ya estaba corriendo (otra restauración a medio camino),
    // simplemente reiniciar — los nuevos parámetros sobreescriben.
    restoreFadeTimer_->stop();
    restoreFadeStep_ = 0;
    restoreFadeStartVol_ = duckedDeck_->volume();
    restoreFadeTargetVol_ = originalVolume_;

    // Fade-in en 500ms (20 pasos de 25ms)
    restoreFadeTimer_->start(25);
}

// ══════════════════════════════════════════════════════════
// Pisadores individuales (base de datos)
// ══════════════════════════════════════════════════════════

PisadorManager::Assignment PisadorManager::getAssignment(const QString& filePath)
{
    Assignment a;
    if (!db_) return a;

    auto q = db_->execPrepared(
        "SELECT pisador_type, pisador_path FROM pisador_assignments WHERE file_path = ?",
        {filePath});
    if (q && q->next()) {
        a.type = q->value(0).toString();
        a.path = q->value(1).toString();
    }
    return a;
}

void PisadorManager::setAssignment(const QString& filePath, const QString& type,
                                    const QString& pisadorPath)
{
    if (!db_) return;
    db_->execPrepared(
        "INSERT OR REPLACE INTO pisador_assignments (file_path, pisador_type, pisador_path) "
        "VALUES (?, ?, ?)",
        {filePath, type, pisadorPath});
    LOG_INFO("[Pisador] Asignación: {} → {} ({})",
             QFileInfo(filePath).fileName().toStdString(),
             type.toStdString(),
             pisadorPath.isEmpty() ? "-" : QFileInfo(pisadorPath).fileName().toStdString());
}

void PisadorManager::removeAssignment(const QString& filePath)
{
    if (!db_) return;
    db_->execPrepared("DELETE FROM pisador_assignments WHERE file_path = ?", {filePath});
}

bool PisadorManager::hasAssignment(const QString& filePath)
{
    if (!db_) return false;
    auto q = db_->execPrepared(
        "SELECT 1 FROM pisador_assignments WHERE file_path = ? LIMIT 1",
        {filePath});
    return q && q->next();
}

bool PisadorManager::isExcluded(const QString& filePath)
{
    for (const auto& folder : excludedFolders_) {
        if (filePath.startsWith(folder)) return true;
    }
    return false;
}

bool PisadorManager::hasPisadorIndicator(const QString& filePath)
{
    if (!enabled_) return false;

    // Solo mostrar indicador para asignaciones individuales.
    // Los pisadores generales dependen del contador de frecuencia
    // en tiempo real y no se pueden predecir de antemano.
    auto a = getAssignment(filePath);
    if (!a.type.isEmpty() && a.type != "none") {
        return true;
    }
    return false;
}

} // namespace sara
