#ifndef SARA_UI_HEADER_WIDGET_H
#define SARA_UI_HEADER_WIDGET_H

#include "core/types.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

namespace sara {

class AudioEngine;
class VuMeterWidget;

class HeaderWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HeaderWidget(QWidget* parent = nullptr);

    void setRadioName(const QString& name);
    void setAudioEngine(AudioEngine* engine);
    void setAutoMode(bool on);
    VuMeterWidget* vuMeter() { return vuMeter_; }

    void setLoopActive(bool on);
    void setStopAtEndActive(bool on);
    void setTalkoverLevel(int percent) { talkoverLevel_ = percent; }
    void setDefaultVolume(int percent);
    void setRecordingActive(bool on);
    void setCurrentUser(const QString& displayName, UserRole role);
    void setControlsEnabled(bool enabled);
    void setViewToggleEnabled(bool enabled);
    void setViewMode(ViewMode mode);

signals:
    void playClicked();
    void pauseClicked();
    void stopClicked();
    void skipClicked();
    void stopAtEndClicked();
    void loopToggled(bool on);
    void volumeChanged(int percent);
    void talkoverChanged(bool on);
    void settingsClicked();
    void auditClicked();
    void autoModeToggled(bool on);
    void clockClicked();
    void recordToggled(bool on);
    void userMenuClicked();
    void viewToggled();
    void eqClicked();

private slots:
    void updateClock();
    void updateLoopBlink();

private:
    void setupUI();

    QLabel*       radioNameLabel_;
    QLabel*       saraLabel_;
    QLabel*       dateLabel_ = nullptr;
    QPushButton*  clockLabel_;
    QPushButton*  playButton_ = nullptr;     // Moved to ScheduleColumn
    QPushButton*  pauseButton_ = nullptr;
    QPushButton*  stopButton_ = nullptr;
    QPushButton*  skipButton_ = nullptr;
    QPushButton*  stopAtEndButton_ = nullptr;
    QPushButton*  loopButton_ = nullptr;
    QPushButton*  talkoverButton_;
    QSlider*      volumeSlider_;
    QLabel*       volumeLabel_;
    QPushButton*  autoModeButton_;
    QPushButton*  settingsButton_;
    QPushButton*  auditButton_;
    QPushButton*  recordBtn_ = nullptr;
    QPushButton*  userBtn_ = nullptr;
    QPushButton*  viewToggleBtn_ = nullptr;
    QTimer*       clockTimer_;
    QTimer*       loopBlinkTimer_ = nullptr;  // Unused, kept for compat
    VuMeterWidget* vuMeter_;
    AudioEngine*  audioEngine_ = nullptr;
    bool          loopActive_ = false;
    bool          stopAtEndActive_ = false;
    bool          talkoverActive_ = false;
    int           talkoverLevel_ = 25;
    int           presetVolume_ = 95;    // Volumen antes del talkover
};

} // namespace sara

#endif
