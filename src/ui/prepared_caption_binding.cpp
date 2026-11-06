#include "prepared_caption_binding.h"
#include "subtitles/subtitle_job.h"
#include "media/playback.h"
#include "core/disc.h"
#include <QFileInfo>
#include <QDir>
#include <algorithm>
namespace luma {
namespace {
QString canonicalSource(const QString &source,bool disc){return QFileInfo(disc?localDiscPath(source):source).canonicalFilePath();}
bool samePath(const QString &left,const QString &right){
    if(left.isEmpty() || right.isEmpty())return false;
#ifdef Q_OS_WIN
    return left.compare(right,Qt::CaseInsensitive)==0;
#else
    return left==right;
#endif
}
}
bool preparedCaptionSourceTimelineMatches(const RecognitionRequest &request,qint64 preparedDurationMs,const QString &currentSource,const PlaybackState &state,const DiscInfo *disc){
    constexpr qint64 maximumDuration=24LL*60*60*1000;
    if(state.failed || !state.timelineReady || state.navigationTitle || preparedDurationMs<=0 || preparedDurationMs>maximumDuration || state.durationMs<=0 || state.durationMs>maximumDuration || std::abs(state.durationMs-preparedDurationMs)>1000 || request.disc!=(disc!=nullptr) || request.audioIndex<0)return false;
    const auto expected=canonicalSource(request.source,request.disc),actual=canonicalSource(currentSource,request.disc);
    if(!samePath(expected,actual))return false;
    const QFileInfo source(expected);if(request.disc?!source.isDir():!source.isFile())return false;
    QVector<int> audio;for(const auto &track:state.audioTracks)if(track.id>=0)audio.append(track.id);
    if(request.audioIndex>=audio.size() || (request.audioTrackId>=0 && audio[request.audioIndex]!=request.audioTrackId))return false;
    if(!request.disc)return true;
    if(!disc->valid || !samePath(actual,canonicalSource(disc->root,true)))return false;
    QString candidate;int count=0;
    for(const auto &title:disc->titles)if(title.durationMs>0 && std::abs(title.durationMs-state.durationMs)<=1000){candidate=title.playlist;++count;}
    return count==1 && candidate==request.playlist;
}
bool preparedCaptionTimelineMatches(const RecognitionRequest &request,qint64 duration,const QString &source,const PlaybackState &state,const DiscInfo *disc){
    if(!preparedCaptionSourceTimelineMatches(request,duration,source,state,disc))return false;
    int index=0;for(const auto &track:state.audioTracks)if(track.id>=0){if(index++==request.audioIndex)return state.currentAudio==track.id;}
    return false;
}
}
