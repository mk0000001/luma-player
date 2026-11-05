#pragma once
#include <QObject>
#include <QVector>
#include <QtGui/qwindowdefs.h>
#include <memory>
namespace luma {
struct MediaItem { int id; QString label; };
struct PlaybackState {
    qint64 positionMs=0, durationMs=0;
    bool playing=false, failed=false;
    int currentTitle=-1,currentChapter=-1;
    QString mediaTitle, videoCodec, audioCodec, hardwareDecoder;
    QVector<MediaItem> audioTracks, subtitleTracks, chapters, titles;
};
bool isDiscSource(const QString &source);
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
    void seek(qint64 positionMs);
    void setVolume(int volume);
    void setRate(double rate);
    void setAudio(int id);
    void setSubtitle(int id);
    void setChapter(int id);
    void setTitle(int id);
    void navigate(int mode);
    bool loadSubtitle(const QString &path);
    const PlaybackState &state() const;
signals:
    void updated();
    void logMessage(QString message);
    void errorOccurred(QString message);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void poll();
};
}
