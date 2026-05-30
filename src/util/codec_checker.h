#ifndef SARA_UTIL_CODEC_CHECKER_H
#define SARA_UTIL_CODEC_CHECKER_H

#include <QString>
#include <QStringList>

namespace sara {

struct CodecInfo {
    QString name;           // "MP3", "AAC", etc.
    QString gstElement;     // Nombre del elemento GStreamer a buscar
    QString extensions;     // ".mp3", ".m4a, .aac"
    bool    critical;       // Si es crítico para el funcionamiento
    bool    found = false;
};

struct CodecCheckResult {
    QList<CodecInfo> codecs;
    QStringList missingCritical;
    QStringList missingOptional;
    QString installCommand;    // apt install ...
    bool allCriticalFound = true;
};

/// Verifica los codecs de GStreamer disponibles.
/// Llamar DESPUÉS de gst_init().
CodecCheckResult checkCodecs();

/// Genera un mensaje legible con el resultado.
QString formatCodecReport(const CodecCheckResult& result);

} // namespace sara

#endif
