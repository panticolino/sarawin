#include "util/file_scanner.h"
#include "util/logger.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <algorithm>

namespace sara {

const QStringList& FileScanner::supportedExtensions()
{
    static const QStringList exts = {
        "mp3", "ogg", "oga", "flac", "wav", "aac", "m4a",
        "wma", "opus", "aiff", "aif", "mp4", "m4b", "webm"
    };
    return exts;
}

QStringList FileScanner::scanFolder(const QString& folderPath, bool recursive)
{
    QStringList results;
    QDir dir(folderPath);

    if (!dir.exists()) {
        LOG_WARN("[FileScanner] Carpeta no encontrada: {}", folderPath.toStdString());
        return results;
    }

    // Construir filtros por extensión
    QStringList filters;
    for (const auto& ext : supportedExtensions()) {
        filters << ("*." + ext);
    }

    auto flags = recursive
        ? QDirIterator::Subdirectories
        : QDirIterator::NoIteratorFlags;

    QDirIterator it(folderPath, filters, QDir::Files | QDir::Readable, flags);

    while (it.hasNext()) {
        results << it.next();
    }

    // Ordenar para reproducibilidad (luego se puede mezclar)
    std::sort(results.begin(), results.end());

    LOG_INFO("[FileScanner] {} archivos de audio encontrados en {}",
             results.size(), folderPath.toStdString());

    return results;
}

bool FileScanner::isAudioFile(const QString& filePath)
{
    QString ext = QFileInfo(filePath).suffix().toLower();
    return supportedExtensions().contains(ext);
}

} // namespace sara
