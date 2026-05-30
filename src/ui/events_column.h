#ifndef SARA_UI_EVENTS_COLUMN_H
#define SARA_UI_EVENTS_COLUMN_H

#include "core/types.h"
#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

namespace sara {

class EventRepository;
class AudioPipeline;
class VuMeterWidget;
class MetadataReader;

class EventsColumn : public QWidget
{
    Q_OBJECT

public:
    explicit EventsColumn(QWidget* parent = nullptr);

    void setEventRepository(EventRepository* repo);
    void setEventsPipeline(AudioPipeline* pipeline);
    void setMetadataReader(MetadataReader* reader) { metadataReader_ = reader; }
    void refresh();

    void onEventStarted(const QString& eventName);
    void onEventFinished(const QString& eventName);
    void onElementPlaying(const QString& elementName, const QString& eventName);
    void startStreamTimer(int64_t durationMs);
    void stopStreamTimer();
    void setProgrammingVisible(bool visible);
    VuMeterWidget* vuMeter() { return vuMeter_; }

signals:
    void playEventNow(const QString& eventId);
    void stopEventNow();
    void newEventClicked();
    void editEventClicked(const QString& eventId);
    void deleteEventClicked(const QString& eventId);
    void duplicateEventClicked(const QString& eventId, const QString& eventName);
    void playEventClicked(const QString& eventId);
    void playExpiredEvent(const QString& eventId);
    void inverseAssignClicked();
    void summaryClicked();

private:
    void setupUI();
    void refreshDayView();
    void refreshWeekView();
    void refreshExpiredView();
    void onPositionUpdated(int64_t posMs, int64_t durMs);
    QString currentDayName() const;
    QString formatTime(int64_t ms) const;
    int64_t getElementDuration(const EventElement& el) const;

    EventRepository* repo_ = nullptr;
    AudioPipeline*   eventsPipeline_ = nullptr;
    MetadataReader*  metadataReader_ = nullptr;
    QTabWidget*      tabs_;
    QTreeWidget*     dayTree_;
    QTreeWidget*     weekTree_;
    QTreeWidget*     expiredTree_ = nullptr;
    QPushButton*     newEventBtn_;
    QPushButton*     assignBtn_ = nullptr;
    QPushButton*     summaryBtn_ = nullptr;
    QPushButton*     clearExpBtn_ = nullptr;
    QLabel*          dayInfoLabel_;

    // Mini player
    QLabel*          playerStatusLabel_ = nullptr;
    QLabel*          playerEventLabel_ = nullptr;
    QLabel*          playerTrackLabel_ = nullptr;
    QLabel*          playerElapsedLabel_ = nullptr;
    QLabel*          playerRemainingLabel_ = nullptr;
    QSlider*         playerProgress_ = nullptr;
    VuMeterWidget*   vuMeter_ = nullptr;

    // Stream timer (para eventos de streaming)
    QTimer*          streamTimer_ = nullptr;
    int64_t          streamDurationMs_ = 0;
    int64_t          streamElapsedMs_ = 0;
    bool             streamMode_ = false;
    bool             programmingVisible_ = true;
};

} // namespace sara

#endif
