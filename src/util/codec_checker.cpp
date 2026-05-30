#include "util/codec_checker.h"
#include "util/logger.h"
#include <gst/gst.h>
#include <QFile>
#include <QRegularExpression>

namespace sara {

static bool hasGstElement(const char* name)
{
    GstElementFactory* factory = gst_element_factory_find(name);
    if (factory) {
        gst_object_unref(factory);
        return true;
    }
    return false;
}

// Buscar cualquiera de los elementos alternativos (separados por |)
static bool hasAnyGstElement(const QString& elements)
{
    for (const auto& el : elements.split('|')) {
        if (hasGstElement(el.trimmed().toUtf8().constData())) {
            return true;
        }
    }
    return false;
}

CodecCheckResult checkCodecs()
{
    CodecCheckResult result;

    // Definir codecs a verificar
    // gstElement puede ser "a|b|c" para buscar alternativas
    QList<CodecInfo> codecs = {
        // Críticos (formatos muy comunes en radio)
        {"MP3",     "mpg123audiodec|avdec_mp3|mad",         ".mp3",         true},
        {"AAC/M4A/MP4", "avdec_aac|faad",                  ".m4a, .aac, .mp4", true},
        {"OGG Vorbis", "vorbisdec",                         ".ogg",         true},
        {"WAV/PCM", "wavparse",                             ".wav",         true},
        {"FLAC",    "flacdec",                              ".flac",        true},

        // Importantes pero no críticos
        {"Opus",    "opusdec",                              ".opus",        false},
        {"WMA",     "avdec_wmav2|avdec_wmav1",              ".wma",         false},

        // Infraestructura (siempre deberían estar)
        {"Audioconvert", "audioconvert",                    "(interno)",    true},
        {"Volume",       "volume",                          "(interno)",    true},
        {"Level",        "level",                           "(VU meter)",   true},
        // Salida de audio: pulsesink. En sistemas PipeWire modernos viene
        // provisto por el shim pipewire-pulse, así que el chequeo funciona
        // tanto en PulseAudio puro como en PipeWire-con-shim. Esto permite
        // que el operador elija tarjetas con los nombres que ve en pactl
        // (mismos en ambos servidores), evitando inconsistencias.
        {"PulseAudio",   "pulsesink",                       "(salida)",     true},
        {"Playbin",      "playbin",                         "(reproductor)",true},
    };

    QStringList missingPackages;

    for (auto& c : codecs) {
        c.found = hasAnyGstElement(c.gstElement);

        if (c.found) {
            LOG_INFO("[CodecCheck] ✓ {} ({}) — disponible", 
                     c.name.toStdString(), c.extensions.toStdString());
        } else {
            if (c.critical) {
                result.missingCritical << c.name;
                LOG_WARN("[CodecCheck] ✗ {} ({}) — NO ENCONTRADO (crítico)", 
                         c.name.toStdString(), c.extensions.toStdString());
            } else {
                result.missingOptional << c.name;
                LOG_WARN("[CodecCheck] ✗ {} ({}) — no encontrado (opcional)", 
                         c.name.toStdString(), c.extensions.toStdString());
            }
        }
    }

    result.codecs = codecs;
    result.allCriticalFound = result.missingCritical.isEmpty();

    // Detectar distribución y sugerir paquetes
    QString distro;
    QFile osRelease("/etc/os-release");
    if (osRelease.open(QIODevice::ReadOnly)) {
        QString content = osRelease.readAll();
        osRelease.close();
        if (content.contains("Ubuntu") || content.contains("Debian") || 
            content.contains("Mint") || content.contains("Pop!_OS")) {
            distro = "debian";
        } else if (content.contains("Fedora") || content.contains("Red Hat") ||
                   content.contains("CentOS") || content.contains("Rocky")) {
            distro = "fedora";
        } else if (content.contains("Arch") || content.contains("Manjaro") ||
                   content.contains("EndeavourOS")) {
            distro = "arch";
        } else if (content.contains("openSUSE") || content.contains("SUSE")) {
            distro = "suse";
        }

        // Extraer nombre legible
        QRegularExpression re("PRETTY_NAME=\"([^\"]+)\"");
        auto match = re.match(content);
        if (match.hasMatch()) {
            LOG_INFO("[CodecCheck] Distribución: {}", match.captured(1).toStdString());
        }
    }

    // Generar comando de instalación
    if (!result.allCriticalFound || !result.missingOptional.isEmpty()) {
        if (distro == "debian") {
            result.installCommand =
                "sudo apt install gstreamer1.0-libav gstreamer1.0-plugins-good "
                "gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly "
                "gstreamer1.0-pulseaudio";
        } else if (distro == "fedora") {
            result.installCommand =
                "sudo dnf install gstreamer1-libav gstreamer1-plugins-good "
                "gstreamer1-plugins-bad-free gstreamer1-plugins-ugly-free "
                "gstreamer1-plugin-pulseaudio";
        } else if (distro == "arch") {
            result.installCommand =
                "sudo pacman -S gst-libav gst-plugins-good gst-plugins-bad "
                "gst-plugins-ugly gst-plugin-pulse";
        } else if (distro == "suse") {
            result.installCommand =
                "sudo zypper install gstreamer-plugins-libav "
                "gstreamer-plugins-good gstreamer-plugins-bad "
                "gstreamer-plugins-ugly";
        } else {
            result.installCommand =
                "Instalar: gstreamer-libav, gstreamer-plugins-good, "
                "gstreamer-plugins-bad, gstreamer-plugins-ugly, "
                "gstreamer-pulseaudio (nombres varían según distribución)";
        }
    }

    LOG_INFO("[CodecCheck] Resultado: {} críticos OK, {} faltantes, {} opcionales faltantes",
             codecs.size() - result.missingCritical.size() - result.missingOptional.size(),
             result.missingCritical.size(), result.missingOptional.size());

    return result;
}

QString formatCodecReport(const CodecCheckResult& result)
{
    QString msg;

    if (!result.allCriticalFound) {
        msg += "⚠ SARA Libre no podrá reproducir algunos formatos de audio.\n\n";
        msg += "Codecs faltantes (críticos):\n";
        for (const auto& name : result.missingCritical) {
            msg += "  ✗ " + name + "\n";
        }
        msg += "\n";
    }

    if (!result.missingOptional.isEmpty()) {
        msg += "Codecs opcionales no disponibles:\n";
        for (const auto& name : result.missingOptional) {
            msg += "  ○ " + name + "\n";
        }
        msg += "\n";
    }

    if (!result.installCommand.isEmpty()) {
        msg += "Para instalar los codecs necesarios, ejecute en una terminal:\n\n";
        msg += result.installCommand + "\n\n";
        msg += "Después reinicie SARA Libre.";
    }

    return msg;
}

} // namespace sara
