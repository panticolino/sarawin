#ifndef SARA_UI_SCHEDULE_COLUMN_H
#define SARA_UI_SCHEDULE_COLUMN_H

#include "core/types.h"
#include <QWidget>
#include <QLabel>
#include <QListWidget>
#include <QSlider>
#include <QPushButton>
#include <QMenu>
#include <QTime>
#include <QTimer>
#include <QStyledItemDelegate>
#include <functional>

namespace sara {

class AudioPipeline;
class VuMeterWidget;

// Roles para datos de cada item
enum PlaylistRole {
    FilePathRole  = Qt::UserRole,
    DurationMsRole = Qt::UserRole + 1,
    StartTimeRole  = Qt::UserRole + 2,
    HasPisadorRole = Qt::UserRole + 3,
};

/// Delegate que pinta cada item como una fila de tabla:
///  [▶] [🎤] Título — Artista          Duración  HH:mm
class PlaylistDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    static QString formatDuration(int64_t ms);
};

class ScheduleColumn : public QWidget
{
    Q_OBJECT

public:
    explicit ScheduleColumn(QWidget* parent = nullptr);

    void setMusicPipeline(AudioPipeline* pipeline);
    void setCurrentTrack(const QString& name, const QString& source);
    void startStreamTimer(int64_t durationMs);  // 0 = sin límite (cuenta ascendente)
    void stopStreamTimer();

    VuMeterWidget* vuMeter() { return vuMeter_; }
    void setLoopActive(bool on);
    void setStopAtEndActive(bool on);
    void setProgrammingVisible(bool visible);

    void addToQueue(const QString& displayName, const QString& filePath,
                    int64_t durationMs = 0);
    void insertInQueue(int position, const QString& displayName,
                       const QString& filePath, int64_t durationMs = 0);
    void removeFromQueue(int row);
    void clearQueue();

    QString getTrackAt(int row) const;
    QString getTrackNameAt(int row) const;
    int64_t getTrackDurationAt(int row) const;
    int queueSize() const;

    void updateRowColors();

    void setPisadorChecker(std::function<bool(const QString&)> checker) {
        hasPisador_ = checker;
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

signals:
    void playClicked();
    void pauseClicked();
    void stopClicked();
    void skipClicked();
    void stopAtEndClicked();
    void loopToggled(bool on);
    void skipRequested(int row);
    void removeRequested(const QString& filePath);
    void trackDoubleClicked(const QString& filePath);
    void cuePreviewRequested(const QString& filePath);
    void pisadorAssignRequested(const QString& filePath, const QString& type);
    void seekRequested(int64_t positionMs);
    void clearQueueRequested();
    void regenerateRequested();
    void filesDropped(const QStringList& paths, int insertAt);
    void manageSchedulesClicked();
    void weeklyGridClicked();
    void listManagerClicked();

public slots:
    void onPositionUpdated(int64_t posMs, int64_t durMs);

private slots:
    void onMetadataReceived(const sara::TrackMetadata& meta);
    void showContextMenu(const QPoint& pos);

private:
    void setupUI();
    void recalcStartTimes();
    QString formatTime(int64_t ms) const;
    QString formatClock(const QTime& t) const;

    // Cabecera "AL AIRE"
    QLabel*       onAirLabel_;
    QLabel*       currentTrackLabel_;
    QLabel*       sourceLabel_;
    QLabel*       elapsedLabel_;
    QLabel*       remainingLabel_;
    QSlider*      progressBar_;

    // Lista de playlist (reemplaza QTableWidget)
    QListWidget*  playlistList_;

    // Context menu
    QMenu* contextMenu_;

    AudioPipeline* pipeline_ = nullptr;
    int64_t currentDurationMs_ = 0;
    int64_t currentPositionMs_ = 0;
    int     activeRow_ = -1;
    bool    seekingByUser_ = false;
    bool    streamMode_ = false;
    int64_t streamDurationMs_ = 0;
    int64_t streamElapsedMs_ = 0;
    QTimer* streamTimer_ = nullptr;
    // Transport controls
    QPushButton*  loopButton_ = nullptr;
    QPushButton*  stopAtEndButton_ = nullptr;
    QPushButton*  schedBtn_ = nullptr;
    QPushButton*  gridBtn_ = nullptr;
    QTimer*       loopBlinkTimer_ = nullptr;
    QTimer*       stopAtEndBlinkTimer_ = nullptr;
    bool          loopActive_ = false;
    bool          stopAtEndActive_ = false;

    // VU Meter
    VuMeterWidget* vuMeter_ = nullptr;

    std::function<bool(const QString&)> hasPisador_;
};

} // namespace sara

#endif
