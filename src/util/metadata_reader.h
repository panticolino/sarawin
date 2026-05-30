#ifndef SARA_UTIL_METADATA_READER_H
#define SARA_UTIL_METADATA_READER_H

#include "core/types.h"
#include <QString>

namespace sara {

/**
 * Lee metadata (duración, título, artista) de archivos de audio
 * usando GStreamer Discoverer (gst-pbutils).
 *
 * Es una operación síncrona y relativamente rápida (~50ms por archivo).
 * No reproduce el archivo, solo analiza los headers.
 */
class MetadataReader
{
public:
    MetadataReader();
    ~MetadataReader();

    /// Leer metadata de un archivo. Retorna duración en ms (0 si falla).
    TrackMetadata read(const QString& filePath);

    /// Solo obtener la duración en ms (más rápido si solo se necesita eso)
    int64_t getDurationMs(const QString& filePath);

private:
    // Timeout para el discoverer (en nanosegundos)
    static constexpr int64_t DISCOVER_TIMEOUT_NS = 5LL * 1000000000LL; // 5 segundos
};

} // namespace sara

#endif // SARA_UTIL_METADATA_READER_H
