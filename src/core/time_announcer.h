#ifndef SARA_CORE_TIME_ANNOUNCER_H
#define SARA_CORE_TIME_ANNOUNCER_H

#include "core/types.h"
#include <QObject>
#include <QStringList>

namespace sara {

/**
 * Generador de locuciones de hora.
 *
 * Compone una secuencia de archivos de audio para anunciar la hora actual.
 * Estructura de carpetas esperada:
 *
 *   horas/    → 00.mp3, 01.mp3, ... 23.mp3  (o 1.mp3 ... 12.mp3 si 12h)
 *   minutos/  → 00.mp3, 01.mp3, ... 59.mp3
 *
 * La secuencia resultante es:
 *   [prefix] + hora + [separator] + minutos + [suffix]
 *
 * Donde prefix, separator y suffix son archivos opcionales (ej: "Son las...",
 * "horas con", "minutos").
 */
class TimeAnnouncer : public QObject
{
    Q_OBJECT

public:
    explicit TimeAnnouncer(QObject* parent = nullptr);

    // Configuración
    void setHoursFolder(const QString& path)   { hoursFolder_ = path; }
    void setMinutesFolder(const QString& path)  { minutesFolder_ = path; }
    void setPrefixFile(const QString& path)     { prefixFile_ = path; }
    void setSuffixFile(const QString& path)     { suffixFile_ = path; }
    void setUse24h(bool use24h)                 { use24h_ = use24h; }

    /// Aplicar configuración desde AppConfig
    void configure(const AppConfig& config);

    /// Generar la lista de archivos para la hora actual
    QStringList generateForCurrentTime() const;

    /// Generar la lista de archivos para una hora específica
    QStringList generateForTime(int hour, int minute) const;

    /// ¿Está configurado correctamente para funcionar?
    bool isConfigured() const;

private:
    /// Buscar archivo de hora (prueba varias extensiones y formatos de nombre)
    QString findHourFile(int hour) const;

    /// Buscar archivo de minuto
    QString findMinuteFile(int minute) const;

    /// Buscar un archivo por nombre base en una carpeta (prueba extensiones)
    QString findAudioFile(const QString& folder, const QString& baseName) const;

    QString hoursFolder_;
    QString minutesFolder_;
    QString prefixFile_;
    QString suffixFile_;
    bool    use24h_ = true;
};

} // namespace sara

#endif
