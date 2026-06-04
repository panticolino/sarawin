#ifndef SARA_AUDIO_RECORDING_MANAGER_H
#define SARA_AUDIO_RECORDING_MANAGER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QDateTime>
#include <gst/gst.h>

namespace sara {

/**
 * Grabación testigo: captura el audio que sale al aire y lo guarda
 * en archivos segmentados por hora.
 *
 * Usa el monitor source de PulseAudio para capturar exactamente
 * lo que se emite por la tarjeta de audio principal (música + eventos
 * + instant play, todo mezclado).
 *
 * Pipeline GStreamer:
 *   pulsesrc (monitor) → audioconvert → audioresample
 *     → lamemp3enc/vorbisenc → filesink
 */
class RecordingManager : public QObject
{
    Q_OBJECT

public:
    explicit RecordingManager(QObject* parent = nullptr);
    ~RecordingManager() override;

    /// Configurar
    void setOutputFolder(const QString& folder);
    void setFormat(const QString& format);  // "mp3" o "ogg"
    void setBitrate(int kbps);              // 64, 96, 128
    void setSegmentByHour(bool enabled);
    void setMainDevice(const QString& deviceId);

    /// Control
    bool startRecording();
    void stopRecording();
    bool isRecording() const { return recording_; }

    /// Info
    QString currentFile() const { return currentFile_; }
    int64_t elapsedMs() const;

signals:
    void recordingStarted(const QString& filePath);
    void recordingStopped(const QString& filePath);
    void segmentCreated(const QString& filePath);
    void errorOccurred(const QString& message);

private:
    void createNewSegment();
    void destroyPipeline();
    QString generateFileName() const;
    QString monitorDevice() const;

    GstElement* pipeline_ = nullptr;
    GstBus*     bus_ = nullptr;          // ver startRecording (mismo motivo Windows que AudioPipeline)
    QTimer*     busPollTimer_ = nullptr;

    QString outputFolder_;
    QString format_ = "mp3";
    int     bitrate_ = 96;
    bool    segmentByHour_ = true;
    QString mainDevice_;
    QString currentFile_;
    bool    recording_ = false;

    QTimer* segmentTimer_ = nullptr;
    QDateTime recordingStart_;
};

} // namespace sara

#endif
