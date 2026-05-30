#include "data/state_manager.h"
#include "util/logger.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace sara {

StateManager::StateManager(QObject* parent)
    : QObject(parent)
{
    timer_ = new QTimer(this);
    timer_->setInterval(30000);  // 30 segundos
    connect(timer_, &QTimer::timeout, this, &StateManager::saveNow);
}

void StateManager::start()
{
    timer_->start();
    LOG_INFO("[StateManager] Guardado periódico iniciado (cada 30s)");
}

void StateManager::stop()
{
    timer_->stop();
}

QString StateManager::stateFilePath() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                        + "/saralibre";
    QDir().mkpath(configDir);
    return configDir + "/state.json";
}

void StateManager::saveNow()
{
    QJsonObject root;
    root["auto_mode"] = autoMode_;
    root["current_track"] = currentTrack_;
    root["current_source"] = currentSource_;
    root["position_ms"] = static_cast<qint64>(positionMs_);
    root["active_schedule"] = activeSchedule_;
    root["volume"] = volume_;
    root["saved_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // Cola de reproducción
    QJsonArray tracks, names, durations;
    for (const auto& t : queueTracks_) tracks.append(t);
    for (const auto& n : queueNames_) names.append(n);
    for (auto d : queueDurations_) durations.append(static_cast<qint64>(d));

    root["queue_tracks"] = tracks;
    root["queue_names"] = names;
    root["queue_durations"] = durations;

    QFile file(stateFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.close();
    }
}

StateManager::SavedState StateManager::loadState()
{
    SavedState state;

    QFile file(stateFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return state;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return state;
    }

    QJsonObject root = doc.object();

    state.autoMode = root["auto_mode"].toBool(true);
    state.currentTrack = root["current_track"].toString();
    state.currentSource = root["current_source"].toString();
    state.positionMs = root["position_ms"].toInteger(0);
    state.activeSchedule = root["active_schedule"].toString();
    state.volume = root["volume"].toDouble(0.75);
    state.savedAt = QDateTime::fromString(root["saved_at"].toString(), Qt::ISODate);

    // Cola
    QJsonArray tracks = root["queue_tracks"].toArray();
    QJsonArray names = root["queue_names"].toArray();
    QJsonArray durations = root["queue_durations"].toArray();

    for (const auto& v : tracks) state.queueTracks << v.toString();
    for (const auto& v : names) state.queueNames << v.toString();
    for (const auto& v : durations) state.queueDurations << v.toInteger(0);

    state.valid = !state.savedAt.isNull();

    LOG_INFO("[StateManager] Estado cargado: {} (guardado: {})",
             state.valid ? "válido" : "inválido",
             state.savedAt.toString("dd/MM HH:mm:ss").toStdString());

    return state;
}

bool StateManager::hasRecentState()
{
    auto state = loadState();
    if (!state.valid) return false;

    // Estado con menos de 5 minutos de antigüedad
    int secsAgo = state.savedAt.secsTo(QDateTime::currentDateTime());
    return secsAgo >= 0 && secsAgo < 300;
}

void StateManager::clearState()
{
    QFile::remove(stateFilePath());
    LOG_INFO("[StateManager] Estado limpiado (cierre limpio)");
}

} // namespace sara
