#pragma once
#include <QString>
namespace luma {
struct RecognitionRequest;struct PlaybackState;struct DiscInfo;
struct CaptionPreparationResult;struct ResumePoint;
bool preparedCaptionResumeMatches(const CaptionPreparationResult &project,const ResumePoint &point);
bool preparedCaptionSourceTimelineMatches(const RecognitionRequest &request,qint64 preparedDurationMs,const QString &currentSource,const PlaybackState &state,const DiscInfo *disc=nullptr);
bool preparedCaptionTimelineMatches(const RecognitionRequest &request,qint64 preparedDurationMs,const QString &currentSource,const PlaybackState &state,const DiscInfo *disc=nullptr);
}
