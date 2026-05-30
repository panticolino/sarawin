#ifndef SARA_CORE_TYPES_H
#define SARA_CORE_TYPES_H

#include <QString>
#include <QStringList>
#include <QTime>
#include <QDate>
#include <QDateTime>
#include <QVector>
#include <cstdint>

namespace sara {

// ── Versión ──────────────────────────────────────────────
constexpr const char* VERSION = "0.1.0";
constexpr const char* APP_NAME = "SARA Libre";

// ── Audio pipeline identifiers ───────────────────────────
enum class PipelineId {
    Main,       // Programación Principal - Deck A (columna 1)
    MainAlt,    // Programación Principal - Deck B (crossfade solapado)
    Events,     // Publicidad/Eventos (columna 2)
    InstantPlay, // Asistente en Vivo (columna 3)
    Cue,        // Preescucha por auriculares
    Pisador     // Pisadores (siempre en Main)
};

// ── Roles de usuario ──────────────────────────────────
enum class UserRole {
    Operation = 0,      // Solo opera el aire
    Programming = 1,    // Programa eventos y contenidos
    Admin = 2           // Acceso total + gestión de personal
};

// ── Vistas ────────────────────────────────────────────
enum class ViewMode {
    Operation = 0,      // Interfaz simplificada
    Full = 1            // Ver todo
};

// ── Estado de un pipeline ────────────────────────────────
enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Buffering,
    Error
};

// ── Tipo de elemento en una programación ─────────────────
enum class ElementType {
    Folder,         // Carpeta: selección aleatoria
    File,           // Archivo específico
    TimeAnnounce,   // Locución de hora
    Stream          // URL de streaming
};

inline QString elementTypeToString(ElementType t) {
    switch (t) {
        case ElementType::Folder:       return QStringLiteral("folder");
        case ElementType::File:         return QStringLiteral("file");
        case ElementType::TimeAnnounce: return QStringLiteral("time_announce");
        case ElementType::Stream:       return QStringLiteral("stream");
    }
    return {};
}

inline ElementType elementTypeFromString(const QString& s) {
    if (s == "folder")        return ElementType::Folder;
    if (s == "file")          return ElementType::File;
    if (s == "time_announce") return ElementType::TimeAnnounce;
    if (s == "stream")        return ElementType::Stream;
    return ElementType::File;
}

// ── Elemento de programación ─────────────────────────────
struct ScheduleElement {
    int         id = 0;
    QString     scheduleId;
    int         position = 0;
    ElementType type = ElementType::Folder;
    QString     path;
    QString     displayName;
};

// ── Programación (bloque musical) ────────────────────────
struct Schedule {
    QString                  id;
    QString                  name;
    QVector<ScheduleElement> elements;
    QDateTime                createdAt;
    QDateTime                updatedAt;
};

// ── Slot horario de programación ─────────────────────────
struct ScheduleSlot {
    int     id = 0;
    QString scheduleId;
    QString day;        // "Lunes", "Martes", etc.
    QTime   startTime;
    QTime   endTime;
};

// ── Prioridad de evento (1-10, donde 1 es máxima) ───────
// Ya no usamos enum, es un int directamente

// ── Elemento de evento (con fechas de vigencia) ──────────
struct EventElement {
    int         id = 0;
    QString     eventId;
    int         position = 0;
    ElementType type = ElementType::File;
    QString     path;
    QString     displayName;
    QDate       validFrom;    // Vacío = sin restricción de inicio
    QDate       validUntil;   // Vacío = sin restricción de fin
    int64_t     durationMs = 0; // Para streams: duración de conexión en ms (0 = sin límite)

    /// ¿Está vigente hoy?
    bool isValidToday() const {
        QDate today = QDate::currentDate();
        if (validFrom.isValid() && today < validFrom) return false;
        if (validUntil.isValid() && today > validUntil) return false;
        return true;
    }
};

// ── Evento (Publicidad) ──────────────────────────────────
struct Event {
    QString       id;
    QString       name;
    bool          persistent = true;
    bool          immediate  = false;   // false = retardado
    int           priority   = 5;       // 1-10 (1 = máxima)
    bool          useAdAnnounce = false;
    int           maxWaitMinutes = 0;   // 0 = sin espera (solo hora exacta)
    QDate         validFrom;
    QDate         validUntil;
    QVector<EventElement> elements;
    QDateTime     createdAt;

    /// ¿Está vigente hoy?
    bool isValidToday() const {
        QDate today = QDate::currentDate();
        if (validFrom.isValid() && today < validFrom) return false;
        if (validUntil.isValid() && today > validUntil) return false;
        return true;
    }
};

// ── Slot horario de evento ───────────────────────────────
struct EventSlot {
    int     id = 0;
    QString eventId;
    QString day;
    QTime   triggerTime;
    bool    enabled = true;
};

// ── Registro de reproducción ─────────────────────────────
struct PlayRecord {
    int64_t   id = 0;
    QString   filePath;
    QDateTime playedAt;
    QString   source;       // "schedule:Mañanas", "event:Publicidad14h", "fallback"
    int64_t   durationMs = 0;
    bool      completed = true;
};

// ── Configuración de la aplicación ───────────────────────
struct AppConfig {
    // Radio
    QString radioName    = "Mi Radio";
    QString radioSlogan;
    QString radioFrequency;
    QString radioCity;
    QString radioCountry;

    // Interfaz
    QString language = "auto";       // "auto", "es", "pt_BR", "en"
    QString theme    = "auto";       // "auto", "dark", "light"
    int     fontSize = 0;            // 0 = normal, -1 = pequeño, +1 = grande

    // Audio
    QString mainAudioDevice  = "default";
    QString cueAudioDevice;             // vacío = sin preescucha
    QString instantAudioDevice;         // vacío = mismo que main
    QString radioFolder;                // Carpeta raíz de contenidos de la radio
    QString fallbackFolder;
    int     crossfadeMs      = 3000;
    bool    crossfadeEnabled = true;
    int     fadeOutMs         = 500;
    bool    fadeOutEnabled    = true;
    int     noRepeatHours    = 3;
    int     noRepeatArtistTracks = 0;  // 0 = deshabilitado

    // Detección de silencio
    bool    silenceDetectionEnabled = true;
    int     silenceThresholdSecs    = 15;
    int     silenceLevelDb          = -50;

    // Normalización de audio
    bool    replayGainEnabled = false;

    // VU Meter
    bool    vuMeterEnabled = true;

    // Ecualizador + Compresor
    bool    eqEnabled = false;
    QVector<double> eqBands = {0,0,0,0,0,0,0,0,0,0};  // 10 bandas, dB
    bool    compressorEnabled = false;
    double  compressorThresholdDb = -10.0;  // dB
    double  compressorRatio = 4.0;          // 4:1
    QString eqPresetName;                   // Nombre del preset activo (vacío = Personalizado)

    // Pisadores
    bool    pisadorEnabled = false;
    QString pisadorFolder;                // Carpeta con archivos de pisador
    int     pisadorFrequency = 1;         // 0=todos, 1=intercalado 1no/1sí, 2=2no/1sí...
    QStringList pisadorExcludedFolders;   // Carpetas cuyos archivos no se pisan
    double  pisadorDuckLevel = 0.25;      // Nivel del duck (0.0-1.0, default 25%)
    int     pisadorDelaySecs = 3;         // Segundos antes de que entre el pisador

    // Locuciones de hora
    QString hoursFolder;
    QString minutesFolder;
    QString prefixFile;
    QString suffixFile;
    bool    use24h = true;

    // Locuciones de espacio publicitario
    QString adIntroFile;    // "Inicio de espacio publicitario"
    QString adOutroFile;    // "Fin de espacio publicitario"

    // Grabación testigo
    QString recordingFolder;            // Carpeta para guardar grabaciones
    QString recordingFormat = "mp3";    // "mp3" o "ogg"
    int     recordingBitrate = 96;      // 64, 96, 128
    bool    recordingSegmentByHour = true;
    QString recordingDevice;               // Dispositivo fuente para grabar (vacío = Main)

    // Archivo "sonando ahora" para Butt / streaming
    bool    nowPlayingEnabled = false;
    QString nowPlayingFile;                 // Ruta al archivo .txt

    // Arranque
    // 0 = Inicia tal como se cerró (default)
    // 1 = Siempre inicia en AUTO
    // 2 = Siempre inicia en Manual
    int  startupMode = 0;
    int  defaultVolume = 95;        // Volumen inicial (0-100)

    // Hablar encima (talkover)
    int  talkoverLevel = 25;        // Nivel de duck al hablar (0-100)

    // Backup automático
    bool    backupEnabled = true;
    int     backupIntervalHours = 24;   // 6, 12, 24, 48
    int     backupMaxCount = 7;         // Cantidad de backups a mantener
};

// ── Señal de posición de audio ───────────────────────────
struct AudioPosition {
    PipelineId pipeline;
    int64_t    positionMs = 0;
    int64_t    durationMs = 0;
};

// ── Información de metadata ──────────────────────────────
struct TrackMetadata {
    QString title;
    QString artist;
    QString album;
    int64_t durationMs = 0;
    QString filePath;
};

} // namespace sara

#endif // SARA_CORE_TYPES_H
