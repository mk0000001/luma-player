#pragma once
#include <QObject>
#include <QVector>
#include <QtGui/qwindowdefs.h>
#include <memory>
namespace luma {
struct MediaItem { int id; QString label; };
struct PlaybackState {
    qint64 positionMs=0, durationMs=0;
    bool playing=false, failed=false, navigationTitle=false;
    int currentTitle=-1,currentChapter=-1;
    int currentAudio=-1,currentSubtitle=-1,decodedVideo=0,decodedAudio=0,displayedPictures=0,playedAudioBuffers=0;
    QString mediaTitle, videoCodec, audioCodec, hardwareDecoder;
    QString colorPrimaries,transferFunction,outputPrimaries,outputTransferFunction,audioOutput,runtimeVersion;
    int audioSampleRate=0,audioChannels=0,droppedVideoFrames=0,lostAudioBuffers=0;
    bool losslessAudio=false;
    QString motionStatus;
    double sourceFps=0,filteredFps=0,displayFps=0;
    QVector<MediaItem> audioTracks, subtitleTracks, chapters, titles;
};
bool isDiscSource(const QString &source);
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
    void stop();
    void pauseToggle();
    void setPaused(bool paused);
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
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void poll();
    void updateFrameInterpolation();
};
}
