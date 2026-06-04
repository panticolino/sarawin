#include "audio/recording_manager.h"
#include "util/logger.h"

#include <QDir>
#include <QFileInfo>
#include <QTime>

namespace sara {

RecordingManager::RecordingManager(QObject* parent)
    : QObject(parent)
{
    segmentTimer_ = new QTimer(this);
    segmentTimer_->setSingleShot(true);
    connect(segmentTimer_, &QTimer::timeout, this, [this]() {
        if (!recording_) return;
        // Segmentar: cerrar archivo actual y abrir nuevo
        LOG_INFO("[Recording] Segmento por hora: creando nuevo archivo");
        QString oldFile = currentFile_;
        destroyPipeline();
        emit segmentCreated(oldFile);
        createNewSegment();
    });
}

RecordingManager::~RecordingManager()
{
    if (recording_) stopRecording();
}

void RecordingManager::setOutputFolder(const QString& folder) { outputFolder_ = folder; }
void RecordingManager::setFormat(const QString& format) { format_ = format; }
void RecordingManager::setBitrate(int kbps) { bitrate_ = kbps; }
void RecordingManager::setSegmentByHour(bool enabled) { segmentByHour_ = enabled; }
void RecordingManager::setMainDevice(const QString& deviceId) { mainDevice_ = deviceId; }

QString RecordingManager::monitorDevice() const
{
    // Formato "source:nombre_pulseaudio" → usar directamente como source
    if (mainDevice_.startsWith("source:")) {
        return mainDevice_.mid(7);  // Quitar prefijo "source:"
    }
    // Formato legacy "input:nombre" → usar directamente
    if (mainDevice_.startsWith("input:")) {
        return mainDevice_.mid(6);
    }
    // PulseAudio: el monitor source de un sink es "sink_name.monitor"
    if (mainDevice_.isEmpty() || mainDevice_ == "default") {
        return "@DEFAULT_MONITOR@";
    }
    return mainDevice_ + ".monitor";
}

QString RecordingManager::generateFileName() const
{
    QDateTime now = QDateTime::currentDateTime();
    QString dateStr = now.toString("yyyy-MM-dd_HH-00");
    return QDir(outputFolder_).filePath(
        QString("sara_rec_%1.%2").arg(dateStr, format_));
}

bool RecordingManager::startRecording()
{
    if (recording_) {
        LOG_WARN("[Recording] Ya está grabando");
        return false;
    }

    // Verificar carpeta
    if (outputFolder_.isEmpty()) {
        emit errorOccurred(tr("No se configuró carpeta de grabación"));
        return false;
    }

    QDir dir(outputFolder_);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            emit errorOccurred(tr("No se pudo crear la carpeta: %1").arg(outputFolder_));
            return false;
        }
    }

    recordingStart_ = QDateTime::currentDateTime();
    createNewSegment();
    return recording_;
}

void RecordingManager::createNewSegment()
{
    // Generar nombre de archivo
    currentFile_ = generateFileName();

    // Si el archivo ya existe (reinicio dentro de la misma hora), añadir sufijo
    if (QFileInfo::exists(currentFile_)) {
        QString base = currentFile_;
        base.chop(format_.length() + 1);  // Remove .ext
        int n = 1;
        while (QFileInfo::exists(currentFile_)) {
            currentFile_ = QString("%1_%2.%3").arg(base).arg(n++).arg(format_);
        }
    }

    // Construir pipeline string
    QString monitor = monitorDevice();
    QString pipelineStr;

    if (format_ == "ogg") {
        int bitrateBps = bitrate_ * 1000;  // vorbisenc usa bps
        pipelineStr = QString(
            "pulsesrc device=\"%1\" ! audioconvert ! audioresample ! "
            "vorbisenc bitrate=%2 ! oggmux ! filesink location=\"%3\"")
            .arg(monitor).arg(bitrateBps).arg(currentFile_);
    } else {
        // MP3 por defecto
        pipelineStr = QString(
            "pulsesrc device=\"%1\" ! audioconvert ! audioresample ! "
            "lamemp3enc target=1 bitrate=%2 cbr=true ! filesink location=\"%3\"")
            .arg(monitor).arg(bitrate_).arg(currentFile_);
    }

    LOG_INFO("[Recording] Pipeline: {}", pipelineStr.toStdString());

    GError* err = nullptr;
    pipeline_ = gst_parse_launch(pipelineStr.toUtf8().constData(), &err);

    if (!pipeline_ || err) {
        QString msg = err ? QString::fromUtf8(err->message) : "Error desconocido";
        if (err) g_error_free(err);
        LOG_ERROR("[Recording] Error creando pipeline: {}", msg.toStdString());
        emit errorOccurred(tr("Error al iniciar grabación: %1").arg(msg));
        recording_ = false;
        return;
    }

    // Bus para detectar errores.
    // Igual que en AudioPipeline: el watch de GLib no se entrega con Qt en
    // Windows, así que sondeamos el bus con un QTimer (funciona en todos los SO).
    bus_ = gst_element_get_bus(pipeline_);
    busPollTimer_ = new QTimer(this);
    busPollTimer_->setInterval(100);
    connect(busPollTimer_, &QTimer::timeout, this, [this]() {
        if (!bus_) return;
        GstMessage* msg = nullptr;
        while ((msg = gst_bus_pop(bus_)) != nullptr) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(msg, &err, &debug);
                QString errorMsg = err ? QString::fromUtf8(err->message) : "Error desconocido";
                LOG_ERROR("[Recording] Error: {} — {}", errorMsg.toStdString(),
                          debug ? debug : "");
                if (err) g_error_free(err);
                if (debug) g_free(debug);
                emit errorOccurred(errorMsg);
            }
            gst_message_unref(msg);
        }
    });
    busPollTimer_->start();

    // Iniciar
    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_ERROR("[Recording] No se pudo iniciar el pipeline");
        destroyPipeline();
        emit errorOccurred(tr("Error al iniciar grabación"));
        recording_ = false;
        return;
    }

    recording_ = true;
    LOG_INFO("[Recording] Grabación iniciada: {}", currentFile_.toStdString());
    emit recordingStarted(currentFile_);

    // Programar segmento por hora
    if (segmentByHour_) {
        QTime now = QTime::currentTime();
        // Milisegundos hasta el inicio de la próxima hora
        int msToNextHour = ((59 - now.minute()) * 60 + (60 - now.second())) * 1000;
        if (msToNextHour < 5000) msToNextHour += 3600000;  // Si faltan <5s, esperar 1h más
        segmentTimer_->start(msToNextHour);
        LOG_INFO("[Recording] Próximo segmento en {}ms ({})",
                 msToNextHour,
                 QTime::currentTime().addMSecs(msToNextHour).toString("HH:mm").toStdString());
    }
}

void RecordingManager::stopRecording()
{
    if (!recording_) return;

    segmentTimer_->stop();
    QString lastFile = currentFile_;
    destroyPipeline();
    recording_ = false;

    LOG_INFO("[Recording] Grabación detenida: {}", lastFile.toStdString());
    emit recordingStopped(lastFile);
}

void RecordingManager::destroyPipeline()
{
    if (!pipeline_) return;

    // Detener el sondeo del bus antes de tocar el pipeline
    if (busPollTimer_) {
        busPollTimer_->stop();
        busPollTimer_->deleteLater();
        busPollTimer_ = nullptr;
    }

    GstBus* bus = bus_ ? bus_ : gst_element_get_bus(pipeline_);

    // Enviar EOS para cerrar correctamente el archivo (cabeceras MP3/Ogg, etc.)
    gst_element_send_event(pipeline_, gst_event_new_eos());

    // Esperar brevemente a que el pipeline procese el EOS y cierre el archivo
    if (bus) {
        gst_bus_timed_pop_filtered(bus, 500 * GST_MSECOND, GST_MESSAGE_EOS);
    }

    gst_element_set_state(pipeline_, GST_STATE_NULL);

    if (bus) {
        gst_object_unref(bus);
    }
    bus_ = nullptr;

    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
}

int64_t RecordingManager::elapsedMs() const
{
    if (!recording_) return 0;
    return recordingStart_.msecsTo(QDateTime::currentDateTime());
}

} // namespace sara
