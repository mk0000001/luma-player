#include <QtTest>
#include <QProcess>
#include <QRegularExpression>
#include <limits>
#include "media/motion_options.h"

using namespace luma;

class MotionTests : public QObject {
    Q_OBJECT
private slots:
    void lateColorMetadataCannotEnableOpticalForHdr(){
        const MotionOptions optical{MotionMode::OpticalFlow,360};
        const auto loading=motionPlanForSource(optical,true,{},{},0);QVERIFY(loading.videoFilter.isEmpty());
        const auto hdr=motionPlanForSource(optical,true,"pq","bt.2020-ncl",10);QVERIFY(hdr.videoFilter.isEmpty());QVERIFY(hdr.status.contains("HDR"));
        const auto dolby=motionPlanForSource(optical,true,"bt.1886","dolbyvision",1);QVERIFY(dolby.videoFilter.isEmpty());
        const auto sdr=motionPlanForSource(optical,true,"bt.1886","bt.709",1);QVERIFY(!sdr.videoFilter.isEmpty());
    }
    void offDoesNotRequestProcessing() {
        const auto plan=motionPlan({});
        QVERIFY(plan.supported);
        QCOMPARE(plan.options.mode,MotionMode::Off);
        QCOMPARE(plan.interpolation,QByteArray("no"));
        QCOMPARE(plan.videoSync,QByteArray("audio"));
        QVERIFY(plan.videoFilter.isEmpty());
        QVERIFY(!plan.requiresCopyBack);
    }
    void blendUsesDisplaySynchronizationInsteadOfTargetFps() {
        const auto plan=motionPlan({MotionMode::DisplayBlend,360});
        QVERIFY(plan.supported);
        QCOMPARE(plan.options.mode,MotionMode::DisplayBlend);
        QCOMPARE(plan.interpolation,QByteArray("yes"));
        QCOMPARE(plan.videoSync,QByteArray("display-resample"));
        QVERIFY(plan.videoFilter.isEmpty());
        QVERIFY(!plan.requiresCopyBack);
    }
    void opticalBoundsTheFilterRate_data() {
        QTest::addColumn<int>("requested");QTest::addColumn<int>("expected");
        QTest::newRow("negative") << -1 << 24;
        QTest::newRow("zero") << 0 << 24;
        QTest::newRow("sixty") << 60 << 60;
        QTest::newRow("fractional-refresh-preset") << 144 << 144;
        QTest::newRow("maximum") << 360 << 360;
        QTest::newRow("too-large") << std::numeric_limits<int>::max() << 360;
    }
    void opticalBoundsTheFilterRate() {
        QFETCH(int,requested);QFETCH(int,expected);
        const auto plan=motionPlan({MotionMode::OpticalFlow,requested});
        QVERIFY(plan.supported);
        QCOMPARE(plan.options.targetFps,expected);
        QVERIFY(plan.requiresCopyBack);
        QCOMPARE(plan.interpolation,QByteArray("no"));
        QCOMPARE(plan.videoSync,QByteArray("audio"));
        QCOMPARE(plan.videoFilter,QString("@luma_motion:lavfi=[minterpolate=fps=%1:mi_mode=mci:mc_mode=aobmc:me_mode=bidir:vsbmc=1]").arg(expected));
    }
    void discBackendCannotEnableInterpolation() {
        for(const auto mode:{MotionMode::DisplayBlend,MotionMode::OpticalFlow}) {
            const auto plan=motionPlan({mode,360},false);
            QVERIFY(!plan.supported);
            QCOMPARE(plan.options.mode,MotionMode::Off);
            QCOMPARE(plan.interpolation,QByteArray("no"));
            QVERIFY(plan.videoFilter.isEmpty());
            QVERIFY(!plan.requiresCopyBack);
            QVERIFY(!plan.status.isEmpty());
        }
        QVERIFY(motionPlan({},false).supported);
    }
    void invalidPersistedModeFallsBackToOff() {
        const auto plan=motionPlan({static_cast<MotionMode>(123456),120});
        QCOMPARE(plan.options.mode,MotionMode::Off);
        QVERIFY(plan.videoFilter.isEmpty());
        QCOMPARE(plan.interpolation,QByteArray("no"));
    }
    void generatedFixtureHas360FrameTimestamps() {
        const auto executable=qEnvironmentVariable("LUMA_MOTION_FFMPEG");
        if(executable.isEmpty())QSKIP("Set LUMA_MOTION_FFMPEG to a native full FFmpeg build for the synthetic filter integration check.");
        auto filter=motionPlan({MotionMode::OpticalFlow,360}).videoFilter;
        const auto graphStart=filter.indexOf('[');
        QVERIFY(graphStart>=0 && filter.endsWith(']'));
        filter=filter.mid(graphStart+1,filter.size()-graphStart-2);
        QProcess process;
        process.start(executable,{"-hide_banner","-loglevel","error","-f","lavfi","-i","testsrc2=size=96x64:rate=24:duration=0.6","-vf",filter,"-frames:v","120","-c:v","rawvideo","-f","framecrc","-"});
        QVERIFY2(process.waitForStarted(10000),qPrintable(process.errorString()));
        const auto finished=process.waitForFinished(30000);
        if(!finished){process.kill();process.waitForFinished(3000);}
        QVERIFY2(finished,"Synthetic 360 fps interpolation exceeded 30 seconds.");
        const auto error=process.readAllStandardError();
        QCOMPARE(process.exitStatus(),QProcess::NormalExit);
        QVERIFY2(process.exitCode()==0,error.constData());
        const auto output=QString::fromUtf8(process.readAllStandardOutput());
        QVERIFY2(QRegularExpression("(?m)^#tb 0: 1/360\\s*$").match(output).hasMatch(),qPrintable(output.left(1000)));
        const QRegularExpression packet("(?m)^0,\\s*(\\d+),\\s*(\\d+),\\s*(\\d+),");
        auto records=packet.globalMatch(output);int count=0;
        while(records.hasNext()) {
            const auto record=records.next();
            QCOMPARE(record.captured(1).toLongLong(),qint64(count));
            QCOMPARE(record.captured(2).toLongLong(),qint64(count));
            QCOMPARE(record.captured(3).toLongLong(),qint64(1));
            ++count;
        }
        QCOMPARE(count,120);
    }
};
QTEST_GUILESS_MAIN(MotionTests)
#include "motion_tests.moc"
