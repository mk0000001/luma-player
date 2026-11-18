#pragma once
#include <QString>
namespace luma {
struct RecognitionRequest;struct PlaybackState;struct DiscInfo;
bool preparedCaptionSourceTimelineMatches(const RecognitionRequest &request,qint64 preparedDurationMs,const QString &currentSource,const PlaybackState &state,const DiscInfo *disc=nullptr);
bool preparedCaptionTimelineMatches(const RecognitionRequest &request,qint64 preparedDurationMs,const QString &currentSource,const PlaybackState &state,const DiscInfo *disc=nullptr);
}
