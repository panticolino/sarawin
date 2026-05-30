#include "core/time_announcer.h"
#include "util/logger.h"

#include <QDir>
#include <QFileInfo>
#include <QTime>

namespace sara {

static const QStringList AUDIO_EXTENSIONS = {
    "mp3", "ogg", "flac", "wav", "aac", "m4a", "opus"
};

TimeAnnouncer::TimeAnnouncer(QObject* parent)
    : QObject(parent)
{
}

void TimeAnnouncer::configure(const AppConfig& config)
{
    hoursFolder_   = config.hoursFolder;
    minutesFolder_ = config.minutesFolder;
    prefixFile_    = config.prefixFile;
    suffixFile_    = config.suffixFile;
    use24h_        = config.use24h;
}

bool TimeAnnouncer::isConfigured() const
{
    return !hoursFolder_.isEmpty()
        && !minutesFolder_.isEmpty()
        && QDir(hoursFolder_).exists()
        && QDir(minutesFolder_).exists();
}

QStringList TimeAnnouncer::generateForCurrentTime() const
{
    QTime now = QTime::currentTime();
    return generateForTime(now.hour(), now.minute());
}

QStringList TimeAnnouncer::generateForTime(int hour, int minute) const
{
    QStringList files;

    if (!isConfigured()) {
        LOG_WARN("[TimeAnnouncer] No configurado (carpetas de horas/minutos vacías)");
        return files;
    }

    // 1. Prefijo (opcional): "Son las..."
    if (!prefixFile_.isEmpty() && QFileInfo::exists(prefixFile_)) {
        files << prefixFile_;
    }

    // 2. Archivo de hora
    int displayHour = hour;
    if (!use24h_) {
        displayHour = hour % 12;
        if (displayHour == 0) displayHour = 12;
    }

    QString hourFile = findHourFile(displayHour);
    if (!hourFile.isEmpty()) {
        files << hourFile;
        LOG_INFO("[TimeAnnouncer] Hora {}: {}", displayHour, hourFile.toStdString());
    } else {
        LOG_WARN("[TimeAnnouncer] Archivo de hora no encontrado para: {} en {}",
                 displayHour, hoursFolder_.toStdString());
    }

    // 3. Archivo de minutos (si no es :00)
    if (minute > 0) {
        QString minuteFile = findMinuteFile(minute);
        if (!minuteFile.isEmpty()) {
            files << minuteFile;
            LOG_INFO("[TimeAnnouncer] Minuto {}: {}", minute, minuteFile.toStdString());
        } else {
            LOG_WARN("[TimeAnnouncer] Archivo de minuto no encontrado para: {} en {}",
                     minute, minutesFolder_.toStdString());
        }
    }

    // 4. Sufijo (opcional): "...de la tarde"
    if (!suffixFile_.isEmpty() && QFileInfo::exists(suffixFile_)) {
        files << suffixFile_;
    }

    LOG_INFO("[TimeAnnouncer] Hora {}:{:02d} → {} archivos: [{}]",
             hour, minute, files.size(),
             files.join(", ").toStdString());

    return files;
}

QString TimeAnnouncer::findHourFile(int hour) const
{
    // Intentar con distintos formatos de nombre:
    // "14", "14h", "02", "2"
    QStringList candidates;
    candidates << QString::number(hour);                               // "14"
    candidates << QString("%1").arg(hour, 2, 10, QChar('0'));         // "02"
    candidates << QString::number(hour) + "h";                        // "14h"
    candidates << QString("%1h").arg(hour, 2, 10, QChar('0'));        // "02h"

    for (const auto& name : candidates) {
        QString found = findAudioFile(hoursFolder_, name);
        if (!found.isEmpty()) return found;
    }
    return {};
}

QString TimeAnnouncer::findMinuteFile(int minute) const
{
    QStringList candidates;
    candidates << QString::number(minute);                             // "5"
    candidates << QString("%1").arg(minute, 2, 10, QChar('0'));       // "05"
    candidates << QString::number(minute) + "m";                      // "5m"
    candidates << QString("%1m").arg(minute, 2, 10, QChar('0'));      // "05m"

    for (const auto& name : candidates) {
        QString found = findAudioFile(minutesFolder_, name);
        if (!found.isEmpty()) return found;
    }
    return {};
}

QString TimeAnnouncer::findAudioFile(const QString& folder, const QString& baseName) const
{
    for (const auto& ext : AUDIO_EXTENSIONS) {
        QString path = folder + "/" + baseName + "." + ext;
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

} // namespace sara
