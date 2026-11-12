#pragma once
#include <QDeadlineTimer>
#include <QMainWindow>
#include <QTemporaryDir>
#include <QSet>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include "core/disc.h"
#include "core/resume_history.h"
#include "media/playback.h"
#include "subtitles/subtitle_job.h"
#include "subtitles/japanese_reading.h"
#include "subtitles/caption_preparation.h"
class QLabel;class QPushButton;class QComboBox;class QSlider;class QListWidget;class QPlainTextEdit;class QCheckBox;class QUrl;class QVBoxLayout;
namespace luma {
class CaptionOverlay;class FullscreenControls;class CaptureGuard;class AiDeviceProbe;class TranslationService;class TranslationDeviceProbe;class SubtitleFileLoader;class CaptionPreparationDialog;
class PerformanceOverlay;class LanPlaybackDialog;
struct PerformanceInput;
class MainWindow:public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent=nullptr);
    ~MainWindow()override;
    void openSource(const QString &source);
    void startLanDiscovery();
    void enableAutomaticSubtitles(const QString &language);
    bool loadCaptionFile(const QString &path);
    bool loadPreparedCaptionProject(const QString &path);
    QJsonObject diagnostics()const;
protected:
    bool nativeEvent(const QByteArray &eventType,void *message,qintptr *result)override;
    bool eventFilter(QObject *object,QEvent *event)override;
    void dragEnterEvent(QDragEnterEvent *event)override;
    void dropEvent(QDropEvent *event)override;
private:
    QWidget *surface_=nullptr,*welcome_=nullptr,*sidebar_=nullptr,*panel_=nullptr,*header_=nullptr,*transport_=nullptr,*transportTitleRow_=nullptr;
    QLabel *nowTitle_=nullptr,*status_=nullptr,*clock_=nullptr,*discLabel_=nullptr,*discDetails_=nullptr,*aiStatus_=nullptr,*codec_=nullptr,*accelerator_=nullptr;
    QLabel *translationStatus_=nullptr,*translationBackend_=nullptr,*captureStatus_=nullptr,*displayStatus_=nullptr,*menuStatus_=nullptr,*bufferStatus_=nullptr,*motionStatus_=nullptr,*npuStatus_=nullptr;
    QPushButton *play_=nullptr,*discButton_=nullptr,*themeButton_=nullptr,*fullButton_=nullptr,*resumeButton_=nullptr;
    QPushButton *pipButton_=nullptr,*volumeButton_=nullptr;
    LanPlaybackDialog *lanPlaybackDialog_=nullptr;
    QList<QWidget *> controlsHiddenInPip_;
    QRect windowedGeometry_;
    bool windowedMaximized_=false;
    bool pictureInPicture_=false;
    QSlider *seek_=nullptr,*volume_=nullptr;
    QComboBox *titles_=nullptr,*audio_=nullptr,*subs_=nullptr,*rate_=nullptr,*language_=nullptr,*aiMode_=nullptr,*aiGpu_=nullptr,*videoGpu_=nullptr,*motionMode_=nullptr,*motionFps_=nullptr;
    QListWidget *chapters_=nullptr;
    QPlainTextEdit *log_=nullptr;
    QCheckBox *aiEnabled_=nullptr,*originalEnabled_=nullptr,*readingEnabled_=nullptr,*translationEnabled_=nullptr,*developerEnabled_=nullptr,*captureAllowed_=nullptr;
    CaptionOverlay *captions_=nullptr;
    PerformanceOverlay *performance_=nullptr;
    QCheckBox *performanceEnabled_=nullptr;
    QLabel *developerVideoStatus_=nullptr;
    QLabel *audioOutputStatus_=nullptr,*hdrPlaybackStatus_=nullptr;
    QComboBox *hdrOutputMode_=nullptr,*audioOutputMode_=nullptr,*audioProfile_=nullptr;
    VideoOverrides developerVideoOptions_;
    FullscreenControls *fullscreenControls_=nullptr;
    CaptureGuard *captureGuard_=nullptr;
    AiDeviceProbe *deviceProbe_=nullptr;
    TranslationDeviceProbe *translationDeviceProbe_=nullptr;
    QComboBox *translationMode_=nullptr,*translationGpu_=nullptr;
    TranslationService *translation_=nullptr;
    SubtitleFileLoader *subtitleFileLoader_=nullptr;
    ResumeHistory resumeHistory_;
    QString resumeIdentity_;
    std::optional<ResumePoint> pendingResume_;
    qint64 resumeSavedAt_=0;
    bool directResume_=false;
    std::unique_ptr<PlaybackEngine> discEngine_,fileEngine_;
    PlaybackEngine *active_=nullptr;
    SubtitleJob job_;
    JapaneseReading reading_;
    QTemporaryDir subtitleCache_;
    QVector<SubtitleCue> generatedCues_,externalCues_;
    QVector<SubtitleCue> presentationCues_;
    bool presentationDirty_=true;
    std::optional<CaptionPreparationResult> preparedCaptions_;
    bool preparedApplied_=false;
    QPointer<CaptionPreparationDialog> preparationDialog_;
    QHash<QString,QString> readings_,translations_;
    QSet<QString> translationPending_;
    QSet<QString> translationSkipped_;
    QStringList translationSkippedOrder_;
    CaptionCoverage coverage_;
    DiscInfo discInfo_,driveInfo_;
    QString source_,model_,discPath_;
    bool full_=false,bufferHolding_=false,bufferResume_=false,aiFailed_=false,translationFailed_=false,bufferBypass_=false,lastNavigation_=false;
    bool lanDiscSharing_=false,lanDiscAwaitingPause_=false,lanDiscResume_=false,lanSourceChanging_=false;
    QDeadlineTimer lanDiscPauseDeadline_;
    qint64 pendingSeek_=-1,seekRequestedAt_=0,nextAiAttemptMs_=0;
    int lastTitle_=-1,decodedSamples_=0,aiRetryCount_=0;
    int lastUnmutedVolume_=70;
    int pendingAudio_=-2,pendingChapter_=-2,pendingTitle_=-2,lastAudio_=-2;
    qint64 pendingControlAt_=0;
    std::optional<bool> requestedPlaying_;
    int videoRecoveryCount_=0;
    quint64 sourceGeneration_=0;
    bool developerApproved_=false,captureApproved_=false,videoRecoveryExhausted_=false;
    std::optional<bool> recoveryPlayback_;
    int recoveryAudio_=-1,recoverySubtitle_=-1;
    QVector<SubtitleCue> recoveryExternalCues_;
    bool wantsPlayback()const;
    bool liveCaptioningActive()const;
    bool captionTimelineAvailable()const;
    void selectChapter(int id);
    void selectAudio(int id);
    void selectTitle(int id);
    void buildUi();
    LanPlaybackDialog *lanPlaybackDialog();
    void showLanPlayback();
    bool lanSourceAvailable() const;
    void updateLanPlaybackAvailability();
    void startLanShare();
    void advanceLanDiscShare();
    bool sendLanMedia();
    void finishLanShare(bool cleanupConfirmed);
    bool stopLanShareForSourceChange();
    void buildDeveloperTools(QVBoxLayout *layout);
    void buildDeveloperVideoTools(QVBoxLayout *layout);
    void buildAudioSettings(QVBoxLayout *layout);
    VideoOverrides videoOutputPreferences() const;
    AudioOutputOptions audioOutputPreferences() const;
    void applyVideoOutputPreferences();
    void updateAudioOutputStatus(const PlaybackState &state);
    void updateDeveloperVideoStatus(const PlaybackState &state);
    PerformanceInput performanceInput() const;
    QJsonObject developerSnapshot() const;
    bool openSourceImpl(const QString &source,bool preparedOpen,qint64 preparedDiscDurationMs=0);
    qint64 discTargetDurationMs_=0;
    void bindEngine(PlaybackEngine *engine);
    void recoverVideoOutput(PlaybackEngine *engine,const QString &message);
    void discoverDisc();
    void refresh();
    void appendLog(const QString &message);
    void toggleFullScreen();
    void togglePictureInPicture();
    void updatePresentationLayout();
    void editTranslationGlossary();
    void applyTheme();
    void applyCapturePolicy();
    void updateDisplayStatus();
    void updateAiPreferences();
    void automaticCaptionsChanged(bool enabled);
    void updateNpuStatus();
    void updateTranslationPreferences();
    void changeVideoAdapter();
    void updateMotionSettings();
    void togglePlayback();
    void toggleMute();
    void seekTo(qint64 position);
    void retryCaptions();
    void holdForCaptionBuffer();
    void updateCaptionBuffer();
    void releaseCaptionBuffer(bool continuePlayback);
    void scheduleSubtitles();
    void scheduleTranslations();
    void clearTranslationSkips();
    void resetSubtitleSession(bool preserveExternal=false);
    void applyExternalCaptions(QVector<SubtitleCue> cues);
    void subtitleImportFailed(const QString &message);
    void savePlaybackProgress(bool force=false);
    void restorePlaybackProgress();
    void updateResumeButton();
    void resumeLatest();
    void openDroppedUrls(const QList<QUrl> &urls);
    void cancelSubtitleWork();
    void rebuildManagedCaptions();
    void showCaptionPreparation();
    bool playPreparedCaptions(CaptionPreparationResult result);
    void applyPreparedCaptions();
    QVector<SubtitleCue> originalCues()const;
    QVector<SubtitleCue> displayCues()const;
};
}
