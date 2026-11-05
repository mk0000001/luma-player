#pragma once
#include <QMainWindow>
#include <QTemporaryDir>
#include <QSet>
#include <QHash>
#include <QJsonObject>
#include "core/disc.h"
#include "media/playback.h"
#include "subtitles/subtitle_job.h"
#include "subtitles/japanese_reading.h"
class QLabel;class QPushButton;class QComboBox;class QSlider;class QListWidget;class QPlainTextEdit;class QCheckBox;class QShortcut;
namespace luma {
class CaptionOverlay;class FullscreenControls;class CaptureGuard;class AiDeviceProbe;class TranslationService;
class MainWindow:public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent=nullptr);
    ~MainWindow()override;
    void openSource(const QString &source);
    void enableAutomaticSubtitles(const QString &language);
    bool loadCaptionFile(const QString &path);
    QJsonObject diagnostics()const;
protected:
    void dragEnterEvent(QDragEnterEvent *event)override;
    void dropEvent(QDropEvent *event)override;
private:
    QWidget *surface_=nullptr,*welcome_=nullptr,*sidebar_=nullptr,*panel_=nullptr,*header_=nullptr,*transport_=nullptr,*transportTitleRow_=nullptr;
    QLabel *nowTitle_=nullptr,*status_=nullptr,*clock_=nullptr,*discLabel_=nullptr,*discDetails_=nullptr,*aiStatus_=nullptr,*codec_=nullptr,*accelerator_=nullptr;
    QLabel *translationStatus_=nullptr,*translationBackend_=nullptr,*captureStatus_=nullptr,*displayStatus_=nullptr,*menuStatus_=nullptr,*bufferStatus_=nullptr,*motionStatus_=nullptr;
    QPushButton *play_=nullptr,*discButton_=nullptr,*themeButton_=nullptr,*fullButton_=nullptr;
    QSlider *seek_=nullptr,*volume_=nullptr;
    QComboBox *titles_=nullptr,*audio_=nullptr,*subs_=nullptr,*rate_=nullptr,*language_=nullptr,*aiMode_=nullptr,*aiGpu_=nullptr,*videoGpu_=nullptr,*motionMode_=nullptr,*motionFps_=nullptr;
    QListWidget *chapters_=nullptr;
    QPlainTextEdit *log_=nullptr;
    QCheckBox *aiEnabled_=nullptr,*originalEnabled_=nullptr,*readingEnabled_=nullptr,*translationEnabled_=nullptr,*captureEnabled_=nullptr;
    CaptionOverlay *captions_=nullptr;
    FullscreenControls *fullscreenControls_=nullptr;
    CaptureGuard *captureGuard_=nullptr;
    AiDeviceProbe *deviceProbe_=nullptr;
    TranslationService *translation_=nullptr;
    std::unique_ptr<PlaybackEngine> discEngine_,fileEngine_;
    PlaybackEngine *active_=nullptr;
    SubtitleJob job_;
    JapaneseReading reading_;
    QTemporaryDir subtitleCache_;
    QVector<SubtitleCue> generatedCues_,externalCues_;
    QHash<QString,QString> readings_,translations_;
    QSet<QString> translationPending_;
    QSet<QString> translationSkipped_;
    QStringList translationSkippedOrder_;
    CaptionCoverage coverage_;
    QList<QShortcut*> discShortcuts_;
    DiscInfo discInfo_,driveInfo_;
    QString source_,model_,discPath_;
    bool full_=false,bufferHolding_=false,bufferResume_=false,aiFailed_=false,translationFailed_=false,bufferBypass_=false,lastNavigation_=false;
    qint64 pendingSeek_=-1,seekRequestedAt_=0,nextAiAttemptMs_=0;
    int lastTitle_=-1,decodedSamples_=0,aiRetryCount_=0;
    int pendingAudio_=-2,pendingChapter_=-2,pendingTitle_=-2,lastAudio_=-2;
    qint64 pendingControlAt_=0;
    std::optional<bool> requestedPlaying_;
    bool wantsPlayback()const;
    bool captionTimelineAvailable()const;
    void selectChapter(int id);
    void selectAudio(int id);
    void selectTitle(int id);
    void buildUi();
    void bindEngine(PlaybackEngine *engine);
    void discoverDisc();
    void refresh();
    void appendLog(const QString &message);
    void toggleFullScreen();
    void applyTheme();
    void applyCapturePolicy();
    void updateDisplayStatus();
    void updateAiPreferences();
    void changeVideoAdapter();
    void updateMotionSettings();
    void togglePlayback();
    void seekTo(qint64 position);
    void retryCaptions();
    void holdForCaptionBuffer();
    void updateCaptionBuffer();
    void releaseCaptionBuffer(bool continuePlayback);
    void scheduleSubtitles();
    void scheduleTranslations();
    void clearTranslationSkips();
    void resetSubtitleSession();
    void cancelSubtitleWork();
    void rebuildManagedCaptions();
    QVector<SubtitleCue> originalCues()const;
    QVector<SubtitleCue> displayCues()const;
};
}
