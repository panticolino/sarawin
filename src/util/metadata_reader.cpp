#include "util/metadata_reader.h"
#include "util/logger.h"

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <QUrl>
#include <QFileInfo>

namespace sara {

MetadataReader::MetadataReader()
{
    // GStreamer debe estar inicializado antes de usar Discoverer.
    // AudioEngine ya lo hace, pero por seguridad:
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
}

MetadataReader::~MetadataReader() = default;

TrackMetadata MetadataReader::read(const QString& filePath)
{
    TrackMetadata meta;
    meta.filePath = filePath;

    GError* err = nullptr;
    GstDiscoverer* discoverer = gst_discoverer_new(DISCOVER_TIMEOUT_NS, &err);
    if (!discoverer) {
        LOG_WARN("[MetadataReader] No se pudo crear discoverer: {}",
                 err ? err->message : "unknown");
        if (err) g_error_free(err);
        return meta;
    }

    // Convertir ruta a URI
    QString uri = QUrl::fromLocalFile(filePath).toString();
    GstDiscovererInfo* info = gst_discoverer_discover_uri(
        discoverer, uri.toUtf8().constData(), &err);

    if (!info || err) {
        LOG_WARN("[MetadataReader] Error analizando {}: {}",
                 QFileInfo(filePath).fileName().toStdString(),
                 err ? err->message : "unknown");
        if (err) g_error_free(err);
        if (info) gst_discoverer_info_unref(info);
        g_object_unref(discoverer);
        return meta;
    }

    GstDiscovererResult result = gst_discoverer_info_get_result(info);
    if (result != GST_DISCOVERER_OK) {
        LOG_WARN("[MetadataReader] Archivo no reconocido: {}",
                 QFileInfo(filePath).fileName().toStdString());
        gst_discoverer_info_unref(info);
        g_object_unref(discoverer);
        return meta;
    }

    // Duración
    GstClockTime duration = gst_discoverer_info_get_duration(info);
    if (GST_CLOCK_TIME_IS_VALID(duration)) {
        meta.durationMs = static_cast<int64_t>(duration / GST_MSECOND);
    }

    // Tags
    const GstTagList* tags = gst_discoverer_info_get_tags(info);
    if (tags) {
        gchar* str = nullptr;

        if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &str)) {
            meta.title = QString::fromUtf8(str);
            g_free(str);
        }
        if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &str)) {
            meta.artist = QString::fromUtf8(str);
            g_free(str);
        }
        if (gst_tag_list_get_string(tags, GST_TAG_ALBUM, &str)) {
            meta.album = QString::fromUtf8(str);
            g_free(str);
        }
    }

    gst_discoverer_info_unref(info);
    g_object_unref(discoverer);

    return meta;
}

int64_t MetadataReader::getDurationMs(const QString& filePath)
{
    // Versión simplificada que solo obtiene duración
    GError* err = nullptr;
    GstDiscoverer* discoverer = gst_discoverer_new(DISCOVER_TIMEOUT_NS, &err);
    if (!discoverer) {
        if (err) g_error_free(err);
        return 0;
    }

    QString uri = QUrl::fromLocalFile(filePath).toString();
    GstDiscovererInfo* info = gst_discoverer_discover_uri(
        discoverer, uri.toUtf8().constData(), &err);

    int64_t durationMs = 0;

    if (info && !err) {
        GstDiscovererResult result = gst_discoverer_info_get_result(info);
        if (result == GST_DISCOVERER_OK) {
            GstClockTime duration = gst_discoverer_info_get_duration(info);
            if (GST_CLOCK_TIME_IS_VALID(duration)) {
                durationMs = static_cast<int64_t>(duration / GST_MSECOND);
            }
        }
    }

    if (err) g_error_free(err);
    if (info) gst_discoverer_info_unref(info);
    g_object_unref(discoverer);

    return durationMs;
}

} // namespace sara
