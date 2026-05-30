#ifndef SARA_UI_LIVE_ASSIST_COLUMN_H
#define SARA_UI_LIVE_ASSIST_COLUMN_H

#include "data/instant_play_repo.h"
#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVector>
#include <QTimer>
#include <QMimeData>
#include <QUrl>

namespace sara {

class DraggableListWidget : public QListWidget
{
public:
    using QListWidget::QListWidget;
protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        auto* mime = new QMimeData();
        QList<QUrl> urls;
        for (auto* item : items) {
            QString path = item->data(Qt::UserRole).toString();
            if (!path.isEmpty()) urls << QUrl::fromLocalFile(path);
        }
        mime->setUrls(urls);
        return mime;
    }
};

class AudioPipeline;
class MetadataReader;
class Database;
class InstantPlayRepo;
class StreamPresetRepo;

class LiveAssistColumn : public QWidget
{
    Q_OBJECT
public:
    explicit LiveAssistColumn(QWidget* parent = nullptr);

    void setInstantPlayRepo(InstantPlayRepo* repo);
    void setInstantPipeline(AudioPipeline* pipeline);
    void setMusicFolder(const QString& path);
    void setRadioFolder(const QString& path);
    void setOperationMode(bool on) { operationMode_ = on; }
    void setStreamPresetRepo(StreamPresetRepo* repo);
    void setMetadataReader(MetadataReader* reader) { metadataReader_ = reader; }

signals:
    void fileDoubleClicked(const QString& filePath);
    void addToQueueRequested(const QString& filePath);
    void streamToQueueRequested(const QString& url, const QString& name, int64_t durationMs);
    void cuePreviewRequested(const QString& filePath);
    void pisadorAssignRequested(const QString& filePath, const QString& type);
    void instantPlayClicked(int slot);

private slots:
    void onButtonClicked(int slot);
    void onButtonRightClicked(int slot, const QPoint& globalPos);
    void onPresetChanged(int index);
    void onManagePresets();
    void onExplorerContextMenu(const QPoint& pos);
    void updateProgress();
    void onInstantTrackFinished();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupInstantPlay(QWidget* container);
    void setupFileExplorer(QWidget* container);
    void refreshButtons();
    void playSlot(int slot);
    void stopPlayback();
    void setActiveButton(int slot);
    void performSearch(const QString& query);
    void onSearchResultContextMenu(const QPoint& pos);
    void refreshStreamList();
    void showStreamView();
    void showFileView();
    void onStreamContextMenu(const QPoint& pos);
    void openAddStreamDialog();
    void addStreamToQueue(const QString& url, const QString& name);

    InstantPlayRepo*      repo_ = nullptr;
    AudioPipeline*        pipeline_ = nullptr;
    StreamPresetRepo*     streamRepo_ = nullptr;
    QString               currentPresetId_;
    int                   activeSlot_ = -1;
    bool                  operationMode_ = false;

    QVector<QPushButton*> instantButtons_;
    QComboBox*            presetCombo_;
    QPushButton*          manageBtn_;

    QLabel*               nowPlayingLabel_;
    QLabel*               onAirLabel_ = nullptr;
    QLabel*               remainLabel_ = nullptr;
    QProgressBar*         progressBar_;
    QLabel*               timeLabel_;
    QPushButton*          stopBtn_;
    QCheckBox*            loopCheck_;
    QPushButton*          loopBtn_ = nullptr;
    QTimer*               loopBlinkTimer_ = nullptr;
    bool                  loopActive_ = false;
    QTimer*               progressTimer_;

    QTreeView*            fileTree_;
    QFileSystemModel*     fileModel_;
    QLabel*               fileInfoLabel_;
    QPushButton*          navRadioBtn_ = nullptr;
    QPushButton*          navHomeBtn_;
    QPushButton*          navMusicBtn_;
    QPushButton*          navStreamsBtn_ = nullptr;
    QString               radioFolder_;
    QString               musicFolder_;
    MetadataReader*       metadataReader_ = nullptr;

    QLineEdit*            searchEdit_;
    DraggableListWidget*  searchResults_;
    QTimer*               searchDebounce_;

    QListWidget*          streamList_ = nullptr;
};

} // namespace sara
#endif
