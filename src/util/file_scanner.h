#ifndef SARA_UTIL_FILE_SCANNER_H
#define SARA_UTIL_FILE_SCANNER_H

#include <QStringList>
#include <QString>

namespace sara {

/**
 * Escanea carpetas recursivamente buscando archivos de audio.
 */
class FileScanner
{
public:
    /// Extensiones de audio soportadas por GStreamer (con plugins estándar)
    static const QStringList& supportedExtensions();

    /// Escanea una carpeta (y subcarpetas) devolviendo rutas absolutas de archivos de audio
    static QStringList scanFolder(const QString& folderPath, bool recursive = true);

    /// Verifica si un archivo tiene extensión de audio soportada
    static bool isAudioFile(const QString& filePath);
};

} // namespace sara

#endif // SARA_UTIL_FILE_SCANNER_H
