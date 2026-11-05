#include <QtTest>
#include <QComboBox>
#include <QCheckBox>
#include <QFile>
#include "ui/caption_overlay.h"
#include "ui/theme.h"
#include <QFontDatabase>
#include <QRawFont>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QPushButton>
#include <QLineEdit>
#include <QMenu>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>
#include <cmath>
#include <optional>
#include <algorithm>
#include "ui/main_window.h"
#include "subtitles/translation_service.h"
class UiTests:public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
private slots:
    void initTestCase(){
        QCoreApplication::setOrganizationName("LumaPlayerTests");
        QCoreApplication::setApplicationName("UiTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings_.path());
    }
    void init(){QSettings().clear();}
    void darkIsDefaultAndLightPreferenceSurvivesThemeReinstall(){
        luma::installPlayerTheme();
        QWidget dark;dark.show();QCoreApplication::processEvents();
        QVERIFY2(dark.palette().color(QPalette::Window).lightness()<64,"The default player background must be dark");
        QSettings().setValue("ui/theme","light");luma::installPlayerTheme();
        QWidget light;light.show();QCoreApplication::processEvents();
        QVERIFY(light.palette().color(QPalette::Window).lightness()>220);
        luma::installPlayerTheme();
        QCOMPARE(QSettings().value("ui/theme").toString(),QString("light"));
        QVERIFY(light.palette().color(QPalette::Window).lightness()>220);
    }
    void fullscreenTransportHidesAndRevealsWithoutCoveringVideo(){
        luma::MainWindow window;window.show();window.activateWindow();QTest::qWait(80);
        QTest::keyClick(&window,Qt::Key_F11);QCoreApplication::processEvents();
        auto *transport=window.findChild<QWidget*>("transport");
        QVERIFY2(transport,"A named fullscreen transport is required");
        QVERIFY(window.isFullScreen());QVERIFY(transport->isVisible());
        QVERIFY(transport->height()>=64 && transport->height()<=76);
        QTest::mouseMove(&window,QPoint(60,60));QTest::qWait(5300);QVERIFY(!transport->isVisible());
        QTest::mouseMove(&window,QPoint(80,80));QCoreApplication::processEvents();
        QVERIFY(transport->isVisible());QVERIFY(window.cursor().shape()!=Qt::BlankCursor);
        auto *seek=window.findChild<QSlider*>("seekSlider");QVERIFY(seek);
        seek->setSliderDown(true);QTest::qWait(5300);QVERIFY(transport->isVisible());
        seek->setSliderDown(false);
        QMenu menu(&window);menu.addAction("Owned test option");menu.popup(window.mapToGlobal(QPoint(100,100)));
        QTest::qWait(5300);QVERIFY(transport->isVisible());menu.close();
        window.activateWindow();QTest::keyClick(&window,Qt::Key_Escape);
        QVERIFY(!window.isFullScreen());QVERIFY(transport->isVisible());
    }
    void captionTogglesPreserveTheImportedOriginal(){
        QTemporaryDir path;QFile file(path.filePath("owned.srt"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("1\n00:00:00,000 --> 00:00:02,000\nOriginal caption\n");file.close();
        luma::MainWindow window;window.openSource(path.filePath("owned-fixture.mkv"));
        QVERIFY(window.loadCaptionFile(file.fileName()));
        auto *overlay=window.findChild<luma::CaptionOverlay*>();QVERIFY(overlay);
        auto *original=window.findChild<QCheckBox*>("originalEnabled");QVERIFY(original);
        auto *reading=window.findChild<QCheckBox*>("readingEnabled");QVERIFY(reading);
        auto *translation=window.findChild<QCheckBox*>("translationEnabled");QVERIFY(translation);
        reading->setChecked(false);translation->setChecked(false);original->setChecked(true);
        QCOMPARE(overlay->captionText(),QString("Original caption"));
        original->setChecked(false);QVERIFY(overlay->captionText().isEmpty());
        for(int i=0;i<3;++i){reading->setChecked(true);reading->setChecked(false);}
        original->setChecked(true);QCOMPARE(overlay->captionText(),QString("Original caption"));
        translation->setChecked(true);auto *service=window.findChild<luma::TranslationService*>();QVERIFY(service);
        const auto generation=service->generation();service->translated(generation,"Original caption","한국어 번역");
        QCOMPARE(overlay->captionText(),QString("Original caption\n한국어 번역"));
        original->setChecked(false);QCOMPARE(overlay->captionText(),QString("한국어 번역"));
        translation->setChecked(false);QVERIFY(overlay->captionText().isEmpty());
        service->translated(generation,"Original caption","오래된 결과");
        original->setChecked(true);translation->setChecked(true);
        QCOMPARE(overlay->captionText(),QString("Original caption\n한국어 번역"));
    }
    void aiFailuresKeepThePreferenceAndOfferBoundedRetry(){
        QSettings().setValue("ai/mode","cpu");
        QSettings().setValue("subtitles/aiEnabled",true);
        luma::MainWindow window;auto *enabled=window.findChild<QCheckBox*>("aiEnabled");QVERIFY(enabled);QVERIFY(enabled->isChecked());
        auto *job=window.findChild<luma::SubtitleJob*>();QVERIFY(job);
        job->failed("Owned fixture: recoverable recognition failure");
        QVERIFY(enabled->isChecked());QVERIFY(QSettings().value("subtitles/aiEnabled").toBool());
        QCOMPARE(window.diagnostics().value("aiRetryCount").toInt(),1);
        QVERIFY(window.diagnostics().value("aiRetryAt").toDouble()>QDateTime::currentMSecsSinceEpoch());
        for(int i=0;i<3;++i)job->failed("Owned fixture: repeated failure");
        QVERIFY(enabled->isChecked());QVERIFY(window.diagnostics().value("captionBufferBypass").toBool());
        QVERIFY(!window.diagnostics().value("captionBufferHolding").toBool());
        auto *retry=window.findChild<QPushButton*>("retryCaptions");QVERIFY(retry);retry->click();
        QCOMPARE(window.diagnostics().value("aiRetryCount").toInt(),0);QVERIFY(!window.diagnostics().value("captionBufferBypass").toBool());
        enabled->setChecked(false);QVERIFY(!QSettings().value("subtitles/aiEnabled").toBool());
    }
    void skippedTranslationKeepsOtherCuesAndDisplayChoicesActive(){
        QTemporaryDir folder;QSettings().setValue("ai/mode","cpu");QSettings().setValue("translation/model",folder.filePath("missing-fixture.gguf"));
        luma::MainWindow window;window.openSource(folder.filePath("synthetic-missing-video.mkv"));window.enableAutomaticSubtitles("en");
        auto *service=window.findChild<luma::TranslationService*>();auto *job=window.findChild<luma::SubtitleJob*>();auto *overlay=window.findChild<luma::CaptionOverlay*>();
        auto *ai=window.findChild<QCheckBox*>("aiEnabled");auto *original=window.findChild<QCheckBox*>("originalEnabled");auto *reading=window.findChild<QCheckBox*>("readingEnabled");auto *translation=window.findChild<QCheckBox*>("translationEnabled");
        QVERIFY(service);QVERIFY(job);QVERIFY(overlay);QVERIFY(ai);QVERIFY(original);QVERIFY(reading);QVERIFY(translation);
        const bool originalChoice=original->isChecked(),readingChoice=reading->isChecked();
        job->ready(job->generation(),{{0,1000,"Synthetic source one"},{1000,2000,"Synthetic source two"}});
        const auto generation=service->generation();service->skipped(generation,"Synthetic source one","Synthetic truncated cue");
        QCOMPARE(window.diagnostics().value("translationSkippedCount").toInt(),1);QCOMPARE(window.diagnostics().value("translationPendingCount").toInt(),1);
        QVERIFY(!window.diagnostics().value("translationFailed").toBool());QVERIFY(ai->isChecked());QVERIFY(translation->isChecked());
        QCOMPARE(original->isChecked(),originalChoice);QCOMPARE(reading->isChecked(),readingChoice);QCOMPARE(overlay->captionText(),QString("Synthetic source one"));
        service->translated(generation,"Synthetic source two","다음 합성 자막");
        QCOMPARE(window.diagnostics().value("translationCount").toInt(),1);QCOMPARE(window.diagnostics().value("translationPendingCount").toInt(),0);
        service->skipped(generation-1,"Old synthetic source","Stale synthetic error");QCOMPARE(window.diagnostics().value("translationSkippedCount").toInt(),1);
        auto *retry=window.findChild<QPushButton*>("retryCaptions");QVERIFY(retry);retry->click();QCOMPARE(window.diagnostics().value("translationSkippedCount").toInt(),0);
        service->skipped(service->generation(),"Synthetic source one","Synthetic retry failure");QCOMPARE(window.diagnostics().value("translationSkippedCount").toInt(),1);
        translation->setChecked(false);translation->setChecked(true);QCOMPARE(window.diagnostics().value("translationSkippedCount").toInt(),0);
        service->failed("Synthetic missing backend");QVERIFY(window.diagnostics().value("translationFailed").toBool());QVERIFY(ai->isChecked());QVERIFY(translation->isChecked());
    }
    void diagnosticsRequirePersistedDeveloperMode(){
        luma::MainWindow window;auto *mode=window.findChild<QCheckBox*>("developerMode");auto *details=window.findChild<QWidget*>("developerDiagnostics");
        QVERIFY(mode);QVERIFY(details);QVERIFY(!mode->isChecked());QVERIFY(details->isHidden());
        mode->setChecked(true);QVERIFY(!details->isHidden());QVERIFY(QSettings().value("ui/developerMode").toBool());
        luma::MainWindow restored;auto *restoredMode=restored.findChild<QCheckBox*>("developerMode");QVERIFY(restoredMode);QVERIFY(restoredMode->isChecked());
    }
    void motionChoicesPersistWithoutClaimingRequestedFpsAsOutput(){
        luma::MainWindow window;window.show();QTest::qWait(40);
        auto *mode=window.findChild<QComboBox*>("motionMode");auto *fps=window.findChild<QComboBox*>("motionFps");
        QVERIFY(mode);QVERIFY(fps);QCOMPARE(mode->currentData().toString(),QString("off"));
        mode->setFocus();QTest::keyClick(mode,Qt::Key_Down);QCOMPARE(QSettings().value("motion/mode").toString(),QString("display"));
        mode->setFocus();QTest::keyClick(mode,Qt::Key_Down);QCOMPARE(QSettings().value("motion/mode").toString(),QString("optical"));
        fps->setFocus();QTest::keyClick(fps,Qt::Key_End);QCOMPARE(QSettings().value("motion/fps").toInt(),360);
    }
    void protectedFolderUriRequestsCaptureExclusion(){
        QTemporaryDir folder;QVERIFY(folder.isValid());QDir root(folder.path());
        QVERIFY(root.mkpath("BDMV"));QVERIFY(root.mkpath("AACS"));
        QByteArray index(70,'\0');index.replace(0,8,"INDX0200");
        qToBigEndian<quint32>(40,reinterpret_cast<uchar*>(index.data()+8));
        qToBigEndian<quint32>(26,reinterpret_cast<uchar*>(index.data()+40));
        QFile file(root.filePath("BDMV/index.bdmv"));QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(index),qint64(index.size()));file.close();
        QUrl source=QUrl::fromLocalFile(folder.path());source.setScheme("bluray");
        luma::MainWindow window;window.openSource(source.toString(QUrl::FullyEncoded));
        const auto status=window.diagnostics();QVERIFY(status.value("discValid").toBool());QVERIFY(status.value("discProtected").toBool());
        QCOMPARE(QDir::cleanPath(status.value("discRoot").toString()),QDir::cleanPath(folder.path()));
        QVERIFY(status.value("captureRequested").toBool());
    }
    void realCaptionReadinessPreservesImmediatePauseBeforeSeek(){
        const auto media=qEnvironmentVariable("LUMA_UI_TEST_MEDIA");if(media.isEmpty())QSKIP("Set LUMA_UI_TEST_MEDIA to the owned two-audio fixture for native UI regression.");
        QVERIFY2(QFileInfo::exists(media),qPrintable(media));
        QSettings().setValue("ai/mode",qEnvironmentVariable("LUMA_UI_TEST_AI_MODE","auto"));
        if(qEnvironmentVariableIsSet("LUMA_UI_TEST_AI_GPU"))QSettings().setValue("ai/gpu",qEnvironmentVariable("LUMA_UI_TEST_AI_GPU"));
        if(qEnvironmentVariableIsSet("LUMA_UI_TEST_MODEL"))QSettings().setValue("ai/model",qEnvironmentVariable("LUMA_UI_TEST_MODEL"));
        QSettings().setValue("subtitles/translation",false);
        bool automaticReadyObserved=false,automaticSnapshotWasPaused=false,immediatePauseIntentKept=false;
        luma::MainWindow window;window.show();window.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("playing").toBool() && window.diagnostics().value("durationMs").toDouble()>=35000 && window.diagnostics().value("audioTrackCount").toInt()>=2,20000);
        auto *play=window.findChild<QPushButton*>("playButton");auto *seek=window.findChild<QSlider*>("seekSlider");auto *job=window.findChild<luma::SubtitleJob*>();
        QVERIFY(play);QVERIFY(seek);QVERIFY(job);QSignalSpy ready(job,&luma::SubtitleJob::ready),failures(job,&luma::SubtitleJob::failed);
        // Keep the user's pause, seek and AI enable in one event-loop turn: mpv has not acknowledged pause yet.
        play->click();seek->setSliderDown(true);seek->setValue(3000);seek->setSliderDown(false);window.enableAutomaticSubtitles("en");
        QVERIFY(!window.diagnostics().value("requestedPlaying").toBool());
        QElapsedTimer elapsed;elapsed.start();QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failures.isEmpty(),60000);
        QVERIFY2(failures.isEmpty(),failures.isEmpty()?"":qPrintable(failures[0][0].toString()));QVERIFY(!ready.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("captionBufferHolding").toBool() && !window.diagnostics().value("playing").toBool(),5000);
        const auto status=window.diagnostics();QVERIFY(!status.value("requestedPlaying").toBool());QVERIFY(status.value("captionBufferedAheadMs").toDouble()>=10000);
        const auto paused=status.value("positionMs").toDouble();QTest::qWait(700);QVERIFY(std::abs(window.diagnostics().value("positionMs").toDouble()-paused)<300);
        qInfo()<<"Owned UI caption readiness ms"<<elapsed.elapsed()<<"backend"<<job->selectedBackend();
        ready.clear();failures.clear();elapsed.restart();
        // The first request starts around12s; returning to0s guarantees uncached coverage even if forward prefetch finished.
        seek->setSliderDown(true);seek->setValue(0);seek->setSliderDown(false);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failures.isEmpty(),15000);
        QVERIFY2(failures.isEmpty(),failures.isEmpty()?"":qPrintable(failures[0][0].toString()));QVERIFY(!ready.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("captionBufferHolding").toBool() && !window.diagnostics().value("playing").toBool(),5000);
        QVERIFY(!window.diagnostics().value("requestedPlaying").toBool());QVERIFY(window.diagnostics().value("captionBufferedAheadMs").toDouble()>=10000);
        qInfo()<<"Owned UI warm seek readiness ms"<<elapsed.elapsed()<<"backend"<<job->selectedBackend();
        const qint64 automaticTarget=qint64(window.diagnostics().value("durationMs").toDouble())*8750/10000;
        // This connection follows MainWindow's ready handler, before mpv's next state poll acknowledges automatic resume.
        const auto automaticConnection=connect(job,&luma::SubtitleJob::ready,&window,[&,play,job,automaticTarget](quint64 generation,const QVector<luma::SubtitleCue> &){
            if(automaticReadyObserved || generation!=job->generation() || job->currentRequest().startMs<automaticTarget-750)return;
            const auto before=window.diagnostics();if(before.value("captionBufferHolding").toBool())return;
            automaticReadyObserved=true;automaticSnapshotWasPaused=!before.value("playing").toBool();
            play->click();immediatePauseIntentKept=!window.diagnostics().value("requestedPlaying").toBool();
        });
        QComboBox *language=nullptr;
        for(auto *choice:window.findChildren<QComboBox*>())if(choice->findData("en")>=0 && choice->findData("ja")>=0){language=choice;break;}
        QVERIFY(language);
        qInfo()<<"Owned UI cached ahead before language reset ms"<<window.diagnostics().value("captionBufferedAheadMs").toDouble();
        // Re-selecting the language clears coverage while retaining the warmed model, even after the full short fixture was prefetched.
        QVERIFY(QMetaObject::invokeMethod(language,"activated",Qt::DirectConnection,Q_ARG(int,language->currentIndex())));
        QCOMPARE(window.diagnostics().value("captionBufferedAheadMs").toDouble(),0.);
        QVERIFY(window.diagnostics().value("captionBufferHolding").toBool());QVERIFY(!window.diagnostics().value("requestedPlaying").toBool());
        ready.clear();failures.clear();elapsed.restart();
        play->click();seek->setSliderDown(true);seek->setValue(8750);seek->setSliderDown(false);
        QVERIFY(window.diagnostics().value("captionBufferHolding").toBool());QVERIFY(window.diagnostics().value("captionResumeRequested").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(automaticReadyObserved || !failures.isEmpty(),15000);disconnect(automaticConnection);
        QVERIFY2(failures.isEmpty(),failures.isEmpty()?"":qPrintable(failures[0][0].toString()));QVERIFY(automaticReadyObserved);
        QVERIFY2(automaticSnapshotWasPaused,"The pause click must run before the backend acknowledges automatic resume");
        QVERIFY2(immediatePauseIntentKept,"The immediate pause click must reverse the requested automatic resume");
        QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("playing").toBool() && !window.diagnostics().value("captionBufferHolding").toBool(),5000);
        const auto automaticStatus=window.diagnostics();QVERIFY(!automaticStatus.value("requestedPlaying").toBool());
        const auto remaining=automaticStatus.value("durationMs").toDouble()-automaticStatus.value("positionMs").toDouble();
        QVERIFY(automaticStatus.value("captionBufferedAheadMs").toDouble()>=std::min(10000.,remaining));
        const auto automaticPaused=automaticStatus.value("positionMs").toDouble();QTest::qWait(700);
        QVERIFY(std::abs(window.diagnostics().value("positionMs").toDouble()-automaticPaused)<300);QVERIFY(!window.diagnostics().value("requestedPlaying").toBool());
        qInfo()<<"Owned UI immediate pause after automatic resume ms"<<elapsed.elapsed();
        auto *enabled=window.findChild<QCheckBox*>("aiEnabled");QVERIFY(enabled);enabled->setChecked(false);
    }
    void realAudioSwitchUsesAcknowledgedTrackForFirstCaptionRequest(){
        const auto media=qEnvironmentVariable("LUMA_UI_TEST_MEDIA");if(media.isEmpty())QSKIP("Set LUMA_UI_TEST_MEDIA to the owned two-audio fixture for native UI regression.");
        QVERIFY2(QFileInfo::exists(media),qPrintable(media));
        QSettings().setValue("ai/mode",qEnvironmentVariable("LUMA_UI_TEST_AI_MODE","auto"));
        if(qEnvironmentVariableIsSet("LUMA_UI_TEST_AI_GPU"))QSettings().setValue("ai/gpu",qEnvironmentVariable("LUMA_UI_TEST_AI_GPU"));
        if(qEnvironmentVariableIsSet("LUMA_UI_TEST_MODEL"))QSettings().setValue("ai/model",qEnvironmentVariable("LUMA_UI_TEST_MODEL"));
        QSettings().setValue("subtitles/translation",false);
        std::optional<luma::RecognitionRequest> firstRequest;
        luma::MainWindow window;window.show();window.openSource(media);
        auto *audio=window.findChild<QComboBox*>("audioTracks");auto *job=window.findChild<luma::SubtitleJob*>();QVERIFY(audio);QVERIFY(job);
        QTRY_VERIFY_WITH_TIMEOUT(audio->count()>=2 && window.diagnostics().value("playing").toBool(),20000);
        const int firstTrack=audio->itemData(0).toInt(),secondTrack=audio->itemData(1).toInt();QVERIFY(firstTrack>=0);QVERIFY(secondTrack>=0);QVERIFY(firstTrack!=secondTrack);
        audio->setCurrentIndex(0);QVERIFY(QMetaObject::invokeMethod(audio,"activated",Qt::DirectConnection,Q_ARG(int,0)));
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("pendingAudioTrack").toInt()==-2 && window.diagnostics().value("currentAudioTrack").toInt()==firstTrack,5000);
        connect(job,&luma::SubtitleJob::progress,&window,[&]{const auto request=job->currentRequest();if(!firstRequest && request.source==media)firstRequest=request;});
        QSignalSpy failures(job,&luma::SubtitleJob::failed);
        audio->setCurrentIndex(1);QVERIFY(QMetaObject::invokeMethod(audio,"activated",Qt::DirectConnection,Q_ARG(int,1)));window.enableAutomaticSubtitles("en");
        QTRY_VERIFY_WITH_TIMEOUT(firstRequest.has_value() || !failures.isEmpty(),60000);
        QVERIFY2(failures.isEmpty(),failures.isEmpty()?"":qPrintable(failures[0][0].toString()));QVERIFY(firstRequest.has_value());
        QCOMPARE(firstRequest->audioTrackId,secondTrack);QCOMPARE(firstRequest->audioIndex,1);
        QCOMPARE(window.diagnostics().value("pendingAudioTrack").toInt(),-2);QCOMPARE(window.diagnostics().value("currentAudioTrack").toInt(),secondTrack);
        auto *enabled=window.findChild<QCheckBox*>("aiEnabled");QVERIFY(enabled);enabled->setChecked(false);
    }
    void realEndSeekDoesNotRetryCaptions(){
        const auto media=qEnvironmentVariable("LUMA_UI_TEST_MEDIA");if(media.isEmpty())QSKIP("Set LUMA_UI_TEST_MEDIA for the native end-of-file seek regression.");
        QSettings().setValue("ai/mode","cpu");QSettings().setValue("subtitles/translation",false);
        luma::MainWindow window;window.show();window.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("playing").toBool() && window.diagnostics().value("durationMs").toDouble()>1000,15000);
        auto *play=window.findChild<QPushButton*>("playButton");auto *seek=window.findChild<QSlider*>("seekSlider");auto *job=window.findChild<luma::SubtitleJob*>();QVERIFY(play);QVERIFY(seek);QVERIFY(job);
        QSignalSpy failures(job,&luma::SubtitleJob::failed);play->click();window.enableAutomaticSubtitles("en");
        seek->setSliderDown(true);seek->setValue(10000);seek->setSliderDown(false);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("positionMs").toDouble()>=window.diagnostics().value("durationMs").toDouble()-500,8000);
        QTest::qWait(2200);const auto status=window.diagnostics();
        QVERIFY2(!status.value("captionBufferHolding").toBool(),"The final decoded frame must not re-enter caption buffering");
        QVERIFY2(failures.isEmpty(),"Seeking to EOF must not generate empty-audio recognition failures");
        QCOMPARE(status.value("aiRetryCount").toInt(),0);QVERIFY(!status.value("requestedPlaying").toBool());
        auto *enabled=window.findChild<QCheckBox*>("aiEnabled");QVERIFY(enabled);QVERIFY(enabled->isChecked());enabled->setChecked(false);
    }
    void physicalFirstPlayReachesMenuWithAiEnabled(){
        const auto disc=qEnvironmentVariable("LUMA_UI_TEST_DISC");if(disc.isEmpty())QSKIP("Set LUMA_UI_TEST_DISC to the verified TUKI disc for the physical First Play regression.");
        QSettings().setValue("ai/mode",qEnvironmentVariable("LUMA_UI_TEST_AI_MODE","auto"));
        if(qEnvironmentVariableIsSet("LUMA_UI_TEST_AI_GPU"))QSettings().setValue("ai/gpu",qEnvironmentVariable("LUMA_UI_TEST_AI_GPU"));
        if(qEnvironmentVariableIsSet("LUMA_UI_TEST_MODEL"))QSettings().setValue("ai/model",qEnvironmentVariable("LUMA_UI_TEST_MODEL"));
        QSettings().setValue("subtitles/translation",false);
        luma::MainWindow window;window.show();window.enableAutomaticSubtitles("ja");
        auto *enabled=window.findChild<QCheckBox*>("aiEnabled");QVERIFY(enabled);QVERIFY(enabled->isChecked());
        QElapsedTimer elapsed;elapsed.start();window.openSource(disc);
        const auto snapshot=[&](const QJsonObject &status){
            QJsonObject result{{"elapsedMs",elapsed.elapsed()}};
            for(const auto &key:{"title","positionMs","durationMs","playing","failed","captionBufferHolding","captionResumeRequested","captionBufferedAheadMs","requestedPlaying","aiEnabled","aiRetryCount","jobBusy","pendingAudioTrack","pendingChapter","pendingTitle","audioTrackCount","currentAudioTrack","decodedVideo","decodedAudio","displayedPictures","playedAudioBuffers","captureRequested","captureApplied","navigationTitle","firstPlayTitle"}){
                const QString name=QString::fromLatin1(key);if(status.contains(name))result.insert(name,status.value(name));
            }
            return result;
        };
        bool firstPlayObserved=false,menuObserved=false,menuReady=false,aiStayedEnabled=true,firstPlayEverHeld=false;
        qint64 firstPlayMaxPosition=0,lastSample=-1000;int previousTitle=-99;bool previousHold=false;
        QJsonArray samples;QJsonObject firstPlay,finalStatus;
        while(elapsed.elapsed()<45000){
            const auto status=window.diagnostics();finalStatus=snapshot(status);
            const int title=status.value("title").toInt(-1);const auto duration=qint64(status.value("durationMs").toDouble());
            const bool held=status.value("captionBufferHolding").toBool();aiStayedEnabled=aiStayedEnabled && status.value("aiEnabled").toBool();
            if(title==3 && duration==20020){
                if(!firstPlayObserved)firstPlay=finalStatus;
                firstPlayObserved=true;firstPlayMaxPosition=std::max(firstPlayMaxPosition,qint64(status.value("positionMs").toDouble()));firstPlayEverHeld=firstPlayEverHeld || held;
            }
            if(elapsed.elapsed()-lastSample>=500 || title!=previousTitle || held!=previousHold){samples.append(finalStatus);lastSample=elapsed.elapsed();previousTitle=title;previousHold=held;}
            // Interactive menus legitimately report an unknown/zero length.
            // Require a transition after the observed First Play, not a clip duration.
            if(title==0 && firstPlayObserved){menuObserved=true;if(!held && status.value("playing").toBool() && status.value("navigationTitle").toBool() && status.value("aiEnabled").toBool()){menuReady=true;break;}}
            QTest::qWait(100);
        }
        if(!menuReady)finalStatus=snapshot(window.diagnostics());
        const auto observedElapsed=elapsed.elapsed();
        QJsonObject report{{"fixture","physical TUKI First Play to menu"},{"deadlineMs",45000},{"elapsedMs",observedElapsed},{"firstPlayObserved",firstPlayObserved},{"firstPlayMaxPositionMs",firstPlayMaxPosition},{"firstPlayEverHeld",firstPlayEverHeld},{"menuObserved",menuObserved},{"menuReady",menuReady},{"aiStayedEnabled",aiStayedEnabled},{"firstPlay",firstPlay},{"final",finalStatus},{"samples",samples}};
        const auto reportPath=qEnvironmentVariable("LUMA_UI_TEST_REPORT",QDir::tempPath()+"/luma-ui-disc-navigation-report.json");
        QVERIFY(QDir().mkpath(QFileInfo(reportPath).absolutePath()));QSaveFile output(reportPath);QVERIFY2(output.open(QIODevice::WriteOnly),qPrintable(output.errorString()));
        const auto payload=QJsonDocument(report).toJson();QCOMPARE(output.write(payload),qint64(payload.size()));QVERIFY(output.commit());
        qInfo()<<"Physical First Play to menu report"<<reportPath<<"elapsed ms"<<observedElapsed;
        QVERIFY2(firstPlayObserved,"Expected the physical disc's title3 First Play with duration20020ms");
        QVERIFY2(firstPlayMaxPosition>=19000,"First Play must advance naturally near its end before reaching the menu");
        QVERIFY2(menuReady,"AI-enabled playback must reach the menu after First Play with no caption hold within45seconds");
        QVERIFY2(!firstPlayEverHeld,"First Play must advance naturally without a caption hold");
        QVERIFY(observedElapsed<=45000);QVERIFY(!finalStatus.value("captionBufferHolding").toBool());QVERIFY(aiStayedEnabled);QVERIFY(enabled->isChecked());
        enabled->setChecked(false);
    }
    void bundledNotoSupportsBothCaptionScripts(){
        const auto runtime=qEnvironmentVariable("LUMA_FONT_RUNTIME");if(runtime.isEmpty())QSKIP("Set LUMA_FONT_RUNTIME for bundled font validation");
        QVERIFY(luma::installApplicationFonts(runtime));
        QFont japanese("Noto Sans CJK JP");japanese.setPixelSize(24);const auto raw=QRawFont::fromFont(japanese);
        QVERIFY(raw.familyName().startsWith("Noto Sans"));QVERIFY(raw.supportsCharacter(QChar(0x97f3)));QVERIFY(raw.supportsCharacter(QChar(0xace4)));
        QFont korean("Noto Sans CJK KR");korean.setPixelSize(20);QVERIFY(QRawFont::fromFont(korean).supportsCharacter(QChar(0xd55c)));
    }
    void captionsAreTimedAndInputTransparent(){
        QWidget window;auto *anchor=new QWidget(&window);anchor->setGeometry(10,20,600,400);window.resize(700,500);window.show();
        luma::CaptionOverlay overlay(anchor);overlay.setCues({{100,900,"こんにちは\n곤니치와"}});overlay.setPosition(100);
        QCOMPARE(overlay.captionText(),QString("こんにちは\n곤니치와"));
        QVERIFY(overlay.testAttribute(Qt::WA_TransparentForMouseEvents));QVERIFY(overlay.windowFlags().testFlag(Qt::WindowTransparentForInput));
        overlay.setPosition(900);QVERIFY(overlay.captionText().isEmpty());QVERIFY(!overlay.isVisible());
        overlay.setCues({{500,800,"New"}});overlay.setPosition(200);QVERIFY(overlay.captionText().isEmpty());overlay.setPosition(500);QCOMPARE(overlay.captionText(),QString("New"));
        anchor->resize(500,300);QCoreApplication::processEvents();QCOMPARE(overlay.size(),anchor->size());
        window.hide();QCoreApplication::processEvents();QVERIFY(!overlay.isVisible());
    }
    void menuShortcutsPreserveControlKeyboardInput(){
        luma::MainWindow window;window.show();QTest::qWait(80);
        QComboBox *rate=nullptr;for(auto *box:window.findChildren<QComboBox*>())if(box->findText("1×")>=0)rate=box;
        QVERIFY(rate);rate->setFocus();QCOMPARE(rate->currentIndex(),2);QTest::keyClick(rate,Qt::Key_Down);QCOMPARE(rate->currentIndex(),3);
        QLineEdit input(&window);input.show();input.setFocus();QTest::keyClicks(&input,"two words");QCOMPARE(input.text(),QString("two words"));
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
