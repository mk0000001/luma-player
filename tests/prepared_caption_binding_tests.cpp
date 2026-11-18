#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "ui/prepared_caption_binding.h"
#include "subtitles/subtitle_job.h"
#include "media/playback.h"
#include "core/disc.h"
using namespace luma;
class PreparedCaptionBindingTests:public QObject {
    Q_OBJECT
private slots:
    void fileMustMatchMediaTimelineAndAudio(){
        QTemporaryDir root;QFile source(root.filePath("owned.mkv"));QVERIFY(source.open(QIODevice::WriteOnly));source.write("fixture");source.close();
        QFile other(root.filePath("other.mkv"));QVERIFY(other.open(QIODevice::WriteOnly));other.write("other");other.close();
        RecognitionRequest request;request.source=source.fileName();request.audioIndex=0;
        PlaybackState state;state.timelineReady=true;state.durationMs=75000;state.audioTracks={{-1,"off"},{11,"Japanese"},{22,"English"}};state.currentAudio=11;
        QVERIFY(preparedCaptionTimelineMatches(request,75000,source.fileName(),state));
        QVERIFY(!preparedCaptionTimelineMatches(request,75000,other.fileName(),state));
        state.currentAudio=22;QVERIFY(!preparedCaptionTimelineMatches(request,75000,source.fileName(),state));
        request.audioIndex=1;QVERIFY(preparedCaptionTimelineMatches(request,75000,source.fileName(),state));
        state.timelineReady=false;QVERIFY(!preparedCaptionTimelineMatches(request,75000,source.fileName(),state));state.timelineReady=true;
        state.durationMs=80000;QVERIFY(!preparedCaptionTimelineMatches(request,75000,source.fileName(),state));
        state.durationMs=75000;state.failed=true;QVERIFY(!preparedCaptionTimelineMatches(request,75000,source.fileName(),state));
    }
    void bluRayRequiresAnUnambiguousSelectedPlaylist(){
        QTemporaryDir root;QVERIFY(QDir(root.path()).mkpath("BDMV"));
        DiscInfo disc;disc.valid=true;disc.root=root.path();disc.titles={{"00001",20000,{}},{"00003",75000,{}}};
        RecognitionRequest request;request.source=root.path();request.disc=true;request.playlist="00003";
        PlaybackState state;state.timelineReady=true;state.durationMs=75900;state.audioTracks={{-1,"off"},{4352,"Japanese"}};state.currentAudio=4352;
        QVERIFY(preparedCaptionTimelineMatches(request,75000,root.path(),state,&disc));
        state.navigationTitle=true;QVERIFY(!preparedCaptionTimelineMatches(request,75000,root.path(),state,&disc));state.navigationTitle=false;
        request.playlist="00001";QVERIFY(!preparedCaptionTimelineMatches(request,75000,root.path(),state,&disc));request.playlist="00003";
        disc.titles.append({"00004",75000,{}});QVERIFY(!preparedCaptionTimelineMatches(request,75000,root.path(),state,&disc));disc.titles.removeLast();
        QVERIFY(!preparedCaptionTimelineMatches(request,75000,root.path(),state));
        disc.valid=false;QVERIFY(!preparedCaptionTimelineMatches(request,75000,root.path(),state,&disc));
    }
    void invalidBoundsAndAbsentTracksNeverMatch(){
        QTemporaryDir root;QFile source(root.filePath("owned.mkv"));QVERIFY(source.open(QIODevice::WriteOnly));source.write("fixture");source.close();
        RecognitionRequest request;request.source=source.fileName();PlaybackState state;state.timelineReady=true;state.durationMs=10000;
        QVERIFY(!preparedCaptionTimelineMatches(request,10000,source.fileName(),state));
        state.audioTracks={{0,"Japanese"}};state.currentAudio=0;QVERIFY(!preparedCaptionTimelineMatches(request,0,source.fileName(),state));
        request.audioIndex=-1;QVERIFY(!preparedCaptionTimelineMatches(request,10000,source.fileName(),state));
        request.audioIndex=7;QVERIFY(!preparedCaptionTimelineMatches(request,10000,source.fileName(),state));
    }
    void audioSelectionIsOnlyAllowedOnThePreparedSourceTimeline(){
        QTemporaryDir root;QVERIFY(QDir(root.path()).mkpath("BDMV"));DiscInfo disc;disc.valid=true;disc.root=root.path();disc.titles={{"00003",75000,{}},{"00004",30000,{}}};
        RecognitionRequest request;request.source=root.path();request.disc=true;request.playlist="00003";request.audioIndex=1;
        PlaybackState state;state.timelineReady=true;state.durationMs=75000;state.audioTracks={{-1,"off"},{4352,"Japanese"},{4353,"Alternate"}};state.currentAudio=4352;
        QVERIFY(preparedCaptionSourceTimelineMatches(request,75000,root.path(),state,&disc));QVERIFY(!preparedCaptionTimelineMatches(request,75000,root.path(),state,&disc));
        state.durationMs=30000;QVERIFY(!preparedCaptionSourceTimelineMatches(request,75000,root.path(),state,&disc));state.durationMs=75000;
        disc.titles.append({"00005",75000,{}});QVERIFY(!preparedCaptionSourceTimelineMatches(request,75000,root.path(),state,&disc));
    }
};
QTEST_GUILESS_MAIN(PreparedCaptionBindingTests)
#include "prepared_caption_binding_tests.moc"
