#pragma once
#include <QObject>
#include <QPair>
#include <QUrl>
#include <QVector>
#include <QtGui/qwindowdefs.h>
#include <memory>
#include <optional>
namespace luma {
struct MediaItem { int id; QString label; };
struct LanMediaSource {
    QUrl url;
    QString mimeType;
    QVector<QPair<QUrl,QString>> subtitles;
};
bool validLanMediaSource(const LanMediaSource &source);
struct VideoOverrides {
    enum class Hardware { Auto, Software, D3D11, DXVA2, VAAPI, D3D11Copy, VulkanCopy };
    enum class Codec { Auto, H264, HEVC, VP9, AV1, Dav1d };
    enum class Color { Auto, SDR, HDR10, HLG };
    enum class DolbyVision { Auto, Apply, Ignore, BaseLayer };
    Hardware hardware = Hardware::Auto;
    Codec codec = Codec::Auto;
    Color color = Color::Auto;
    DolbyVision dolby = DolbyVision::Auto;
    bool forceHdr = false;
};
struct AudioOutputOptions {
    enum class Profile { Auto, Stereo, Surround51, Surround71, Night };
    bool bitstream = false;
    Profile profile = Profile::Auto;
};
struct PlaybackState {
    qint64 positionMs=0, durationMs=0;
    bool playing=false, failed=false, navigationTitle=false, buffering=false;
    bool timelineReady=false,discMenus=true;
    bool lanSubtitleCueActive=false;
    int currentTitle=-1,currentChapter=-1;
    int currentAudio=-1,currentSubtitle=-1,decodedVideo=0,decodedAudio=0,displayedPictures=0,playedAudioBuffers=0;
    QString mediaTitle, videoCodec, audioCodec, hardwareDecoder;
    QString colorPrimaries,transferFunction,outputPrimaries,outputTransferFunction,audioOutput,runtimeVersion;
    QString sourceVideoRange = "unknown";
    QString videoDecoder, videoDecoderRequest, videoColorRequest, dolbyVisionRequest, videoOverrideStatus;
    bool videoColorForced=false;
    std::optional<int> dolbyVisionProfile;
    int audioSampleRate=0,audioChannels=0,droppedVideoFrames=0,lostAudioBuffers=0;
    bool losslessAudio=false;
    QString audioFormat, audioLayout, audioSettingsError;
    bool audioBitstreamRequested=false, audioBitstreamActive=false, audioVolumeExternal=false;
    QString motionStatus;
    double sourceFps=0,filteredFps=0,displayFps=0;
    bool discStatisticsAvailable=false, fileCacheHolding=false;
    quint32 discInputBytes=0, discDemuxBytes=0;
    std::optional<double> cacheSeconds, avSyncMs;
    QVector<MediaItem> audioTracks, subtitleTracks, chapters, titles;
    QVector<qint64> chapterOffsetsMs;
};
bool isDiscSource(const QString &source);
std::optional<QString> normalizedPlaybackSource(const QString &source, bool disc);
// A new title ID alone is insufficient: native live length must match metadata.
bool discTimelineReady(qint64 liveDurationMs,qint64 titleDurationMs,bool navigationTitle);
enum class DiscAction { Activate,Up,Down,Left,Right,Popup };
int discActionCode(DiscAction action);
class PlaybackEngine : public QObject {
    Q_OBJECT
public:
    enum class Kind { Disc, File };
    explicit PlaybackEngine(Kind kind, const QString &runtimeRoot={}, QObject *parent=nullptr);
    ~PlaybackEngine() override;
    bool available() const;
    QString lastError() const;
    QString name() const;
    bool open(const QString &source, WId surface);
    bool openLanMedia(const LanMediaSource &source, WId surface);
    bool openPreparedDisc(const QString &source,WId surface,qint64 durationMs);
    void useSoftwareDiscDecoderOnNextOpen();
    void stop();
    void pauseToggle();
    void setPaused(bool paused);
    void setPerformanceMetricsEnabled(bool enabled);
    void setVideoOverrides(VideoOverrides options);
    void setVideoColorOutput(VideoOverrides::Color color,bool forceHdr);
    void setAudioOutputOptions(AudioOutputOptions options);
    void setFrameInterpolation(const QString &mode,int targetFps);
    void seek(qint64 positionMs);
    void setVolume(int volume);
    void setRate(double rate);
    void setAudio(int id);
    void setSubtitle(int id);
    void setChapter(int id);
    void setTitle(int id);
    void navigate(int mode);
    bool loadSubtitle(const QString &path,bool managed=false);
    void clearManagedSubtitles();
    const PlaybackState &state() const;
signals:
    void updated();
    void logMessage(QString message);
    void errorOccurred(QString message);
    void videoOutputLost(QString message);
private:
    bool openInternal(const QString &source,WId surface,qint64 preparedDiscDurationMs,const LanMediaSource *lanSource=nullptr);
    struct Impl;
    std::unique_ptr<Impl> d;
    void poll();
    void updateFrameInterpolation();
};
}
