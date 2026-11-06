#include <QtTest>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QTimer>
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
#include <QTabBar>
#include <QScrollArea>
#include <QScrollBar>
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
#include "ui/video_surface.h"
#include "core/resume_history.h"
#include "subtitles/translation_service.h"
#include "subtitles/translation_support.h"
#include "subtitles/subtitle_file.h"
class UiTests:public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    bool answerConsent(luma::MainWindow &window,const QString &name,bool accept){
        bool seen=false;
        QTimer::singleShot(0,&window,[&]{
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog)return;
            seen=dialog->objectName()==name;
            auto *ack=dialog->findChild<QCheckBox*>("consentAcknowledged");
            auto *approve=dialog->findChild<QPushButton*>("consentApprove");
            auto *cancel=dialog->findChild<QPushButton*>("consentCancel");
            if(!seen || !ack || !approve || !cancel){dialog->reject();return;}
            QVERIFY(!approve->isEnabled());
            if(accept){ack->setChecked(true);QVERIFY(approve->isEnabled());approve->click();}else cancel->click();
        });
        auto *check=window.findChild<QCheckBox*>(name=="developerConsentDialog"?"developerMode":"captureAllowed");
        if(!check)return false;
        check->setChecked(true);QCoreApplication::processEvents();return seen;
    }
private slots:
    void initTestCase(){
        QCoreApplication::setOrganizationName("LumaPlayerTests");
        QCoreApplication::setApplicationName("UiTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings_.path());
    }
    void init(){QSettings().clear();}
    void failedOpenDoesNotKeepPauseIntent(){
        QTemporaryDir media;luma::MainWindow window;window.show();
        auto *play=window.findChild<QPushButton*>("playButton");QVERIFY(play);
        window.openSource(media.filePath("missing.mp4"));
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["failed"].toBool(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(play->accessibleName().startsWith("재생 ·"),2000);
        QVERIFY(!window.diagnostics()["captionResumeRequested"].toBool());
    }
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
        const auto generation=service->generation();const auto cueKey=luma::translationCueKey({0,2000,"Original caption"});service->translatedCue(generation,cueKey,"Original caption","한국어 번역");
        QCOMPARE(overlay->captionText(),QString("Original caption\n한국어 번역"));
        original->setChecked(false);QCOMPARE(overlay->captionText(),QString("한국어 번역"));
        translation->setChecked(false);QVERIFY(overlay->captionText().isEmpty());
        service->translatedCue(generation,cueKey,"Original caption","오래된 결과");
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
        QVERIFY(enabled->isChecked());QVERIFY(!window.diagnostics().value("captionBufferBypass").toBool());
        QVERIFY(!window.diagnostics().value("captionBufferHolding").toBool());
        auto *continuePlay=window.findChild<QPushButton*>("continueWithoutCaptions");QVERIFY(continuePlay);continuePlay->click();QVERIFY(window.diagnostics().value("captionBufferBypass").toBool());
        auto *retry=window.findChild<QPushButton*>("retryCaptions");QVERIFY(retry);retry->click();
        QCOMPARE(window.diagnostics().value("aiRetryCount").toInt(),0);QVERIFY(!window.diagnostics().value("captionBufferBypass").toBool());
        enabled->setChecked(false);QVERIFY(!QSettings().value("subtitles/aiEnabled").toBool());
    }
    void longImportedCaptionUsesConsecutivePresentationPages(){
        QTemporaryDir folder;QFile file(folder.filePath("owned-long.srt"));QVERIFY(file.open(QIODevice::WriteOnly));
        const QString source=QString(60,'A')+QString(60,'B');file.write(("1\n00:00:00,000 --> 00:00:12,000\n"+source+"\n").toUtf8());file.close();
        QSettings().setValue("subtitles/hangul",false);QSettings().setValue("subtitles/translation",false);
        luma::MainWindow window;window.openSource(folder.filePath("owned-fixture.mkv"));QVERIFY(window.loadCaptionFile(file.fileName()));
        auto *overlay=window.findChild<luma::CaptionOverlay*>();QVERIFY(overlay);QCOMPARE(overlay->captionText(),QString(60,'A'));
        QCOMPARE(window.diagnostics().value("externalCueCount").toInt(),1);overlay->setPosition(6000);QCOMPARE(overlay->captionText(),QString(60,'B'));overlay->setPosition(12000);QVERIFY(overlay->captionText().isEmpty());
    }
    void requestedPlaybackKeysWorkAndPreserveInputWidgets(){
        luma::MainWindow window;window.show();window.activateWindow();QTest::qWait(40);
        auto *volume=window.findChild<QSlider*>("volumeSlider");QVERIFY(volume);volume->setValue(65);window.setFocus();
        QTest::keyClick(&window,Qt::Key_Up);QCOMPARE(volume->value(),70);QTest::keyClick(&window,Qt::Key_Down);QCOMPARE(volume->value(),65);
        QTest::keyClick(&window,Qt::Key_M);QCOMPARE(volume->value(),0);QTest::keyClick(&window,Qt::Key_M);QCOMPARE(volume->value(),65);
        QTest::keyClick(&window,Qt::Key_F);QVERIFY(window.isFullScreen());QTest::keyClick(&window,Qt::Key_Return);QVERIFY(!window.isFullScreen());
        QLineEdit input(&window);input.show();input.setFocus();QTest::keyClicks(&input,"fm");QCOMPARE(input.text(),QString("fm"));QCOMPARE(volume->value(),65);
    }
    void unreadableSubtitleEnablesAutomaticCaptions(){
        QSettings().setValue("ai/mode","cpu");QTemporaryDir folder;luma::MainWindow window;window.openSource(folder.filePath("owned-video.mkv"));
        QVERIFY(!window.loadCaptionFile(folder.filePath("missing.srt")));auto *enabled=window.findChild<QCheckBox*>("aiEnabled");QVERIFY(enabled);QVERIFY(enabled->isChecked());QVERIFY(QSettings().value("subtitles/aiEnabled").toBool());
    }
    void malformedImportedSubtitlesFallBackAndOldLoadsCannotPublish(){
        QTemporaryDir folder;QSettings().setValue("ai/model",folder.filePath("missing.bin"));QSettings().setValue("subtitles/translation",false);
        QFile file(folder.filePath("malformed.srt"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("1\n00:00:00,000 --> 00:00:10,000\nOwned valid first cue\n\n2\nwrong timestamp\nBroken second cue\n");file.close();
        luma::MainWindow window;window.openSource(folder.filePath("owned-video.mkv"));QVERIFY(window.loadCaptionFile(file.fileName()));
        auto *ai=window.findChild<QCheckBox*>("aiEnabled");auto *loader=window.findChild<luma::SubtitleFileLoader*>();QVERIFY(ai);QVERIFY(loader);
        QTRY_VERIFY(ai->isChecked());QCOMPARE(window.diagnostics().value("externalCueCount").toInt(),0);
        const auto oldGeneration=loader->generation();window.openSource(folder.filePath("new-video.mkv"));
        loader->ready(oldGeneration,{{0,10000,"Stale owned caption"}});QCOMPARE(window.diagnostics().value("externalCueCount").toInt(),0);
    }
    void changingRecognitionLanguagePreservesAnImportedSubtitle(){
        QSettings().setValue("subtitles/translation",false);QTemporaryDir folder;QFile file(folder.filePath("owned.srt"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("1\n00:00:00,000 --> 00:00:15,000\nOwned external original\n");file.close();
        luma::MainWindow window;window.openSource(folder.filePath("owned-video.mkv"));QVERIFY(window.loadCaptionFile(file.fileName()));
        QComboBox *language=nullptr;for(auto *box:window.findChildren<QComboBox*>())if(box->findData("ja")>=0 && box->findData("en")>=0){language=box;break;}QVERIFY(language);
        language->setCurrentIndex(language->findData("en"));QVERIFY(QMetaObject::invokeMethod(language,"activated",Qt::DirectConnection,Q_ARG(int,language->currentIndex())));
        QCOMPARE(window.diagnostics().value("externalCueCount").toInt(),1);
    }
    void tabsAndScrollAreasKeepTheirArrowKeys(){
        luma::MainWindow window;window.show();window.activateWindow();QCoreApplication::processEvents();
        auto *tabs=window.findChild<QTabBar*>();auto *volume=window.findChild<QSlider*>("volumeSlider");QVERIFY(tabs);QVERIFY(volume);
        tabs->setCurrentIndex(0);tabs->setFocus();QTest::keyClick(tabs,Qt::Key_Right);QCOMPARE(tabs->currentIndex(),1);
        QScrollArea area(&window);area.setFocusPolicy(Qt::StrongFocus);area.resize(100,100);auto *contents=new QWidget;contents->resize(200,1000);area.setWidget(contents);area.show();area.setFocus();QCoreApplication::processEvents();
        volume->setValue(50);QTest::keyClick(&area,Qt::Key_Down);QVERIFY(area.verticalScrollBar()->value()>0);QCOMPARE(volume->value(),50);
    }
    void choosingNativeSubtitleCancelsQueuedExternalImport(){
        QSettings().setValue("subtitles/aiEnabled",false);QSettings().setValue("subtitles/translation",false);QTemporaryDir folder;
        QFile file(folder.filePath("owned.vtt"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("WEBVTT\n\n00:00:00.000 --> 00:00:04.000\nOwned pending caption\n");file.close();
        luma::MainWindow window;window.openSource(folder.filePath("owned-video.mkv"));auto *loader=window.findChild<luma::SubtitleFileLoader*>();auto *subs=window.findChild<QComboBox*>("subtitleTracks");QVERIFY(loader);QVERIFY(subs);
        window.loadCaptionFile(file.fileName());const auto old=loader->generation();QVERIFY(QMetaObject::invokeMethod(subs,"activated",Qt::DirectConnection,Q_ARG(int,0)));
        QVERIFY(loader->generation()!=old);loader->ready(old,{{0,4000,"Late pending owned caption"}});QCOMPARE(window.diagnostics().value("externalCueCount").toInt(),0);
    }
    void ownedVideoAndTransportReceivePlaybackKeys(){
        luma::MainWindow window;window.show();QCoreApplication::processEvents();
        auto *surface=window.findChild<luma::VideoSurface*>("videoSurface");auto *transport=window.findChild<QWidget*>("transport");auto *volume=window.findChild<QSlider*>("volumeSlider");
        QVERIFY(surface);QVERIFY(transport);QVERIFY(volume);surface->setVideoActive(true);auto *video=surface->videoWindow();video->activateWindow();video->setFocus();QCoreApplication::processEvents();
        volume->setValue(55);QTest::keyClick(video,Qt::Key_M);QCOMPARE(volume->value(),0);QTest::keyClick(video,Qt::Key_M);QCOMPARE(volume->value(),55);
        QTest::keyClick(video,Qt::Key_F11);QVERIFY(window.isFullScreen());QTest::keyClick(video,Qt::Key_Escape);QVERIFY(!window.isFullScreen());
        QTest::keyClick(transport,Qt::Key_F);QVERIFY(window.isFullScreen());QTest::keyClick(transport,Qt::Key_Enter);QVERIFY(!window.isFullScreen());
    }
    void skippedTranslationKeepsOtherCuesAndDisplayChoicesActive(){
        QTemporaryDir folder;QSettings().setValue("ai/mode","cpu");QSettings().setValue("translation/model",folder.filePath("missing-fixture.gguf"));
        luma::MainWindow window;window.openSource(folder.filePath("synthetic-missing-video.mkv"));window.enableAutomaticSubtitles("en");
        auto *service=window.findChild<luma::TranslationService*>();auto *job=window.findChild<luma::SubtitleJob*>();auto *overlay=window.findChild<luma::CaptionOverlay*>();
        auto *ai=window.findChild<QCheckBox*>("aiEnabled");auto *original=window.findChild<QCheckBox*>("originalEnabled");auto *reading=window.findChild<QCheckBox*>("readingEnabled");auto *translation=window.findChild<QCheckBox*>("translationEnabled");
        QVERIFY(service);QVERIFY(job);QVERIFY(overlay);QVERIFY(ai);QVERIFY(original);QVERIFY(reading);QVERIFY(translation);
        const bool originalChoice=original->isChecked(),readingChoice=reading->isChecked();
        job->ready(job->generation(),{{0,1000,"Synthetic source one"},{1000,2000,"Synthetic source two"}});
        const auto generation=service->generation();const auto first=luma::translationCueKey({0,1000,"Synthetic source one"});const auto second=luma::translationCueKey({1000,2000,"Synthetic source two"});
        service->skippedCue(generation,first,"Synthetic source one","Synthetic truncated cue");
        QCOMPARE(window.diagnostics().value("translationSkippedCount").toInt(),1);QCOMPARE(window.diagnostics().value("translationPendingCount").toInt(),1);
        QVERIFY(!window.diagnostics().value("translationFailed").toBool());QVERIFY(ai->isChecked());QVERIFY(translation->isChecked());
        QCOMPARE(original->isChecked(),originalChoice);QCOMPARE(reading->isChecked(),readingChoice);QCOMPARE(overlay->captionText(),QString("Synthetic source one"));
        service->translatedCue(generation,second,"Synthetic source two","다음 합성 자막");
        QCOMPARE(window.diagnostics().value("translationCount").toInt(),1);QCOMPARE(window.diagnostics().value("translationPendingCount").toInt(),0);
        service->skippedCue(generation-1,"old-key","Old synthetic source","Stale synthetic error");QCOMPARE(window.diagnostics().value("translationSkippedCount").toInt(),1);
        auto *retry=window.findChild<QPushButton*>("retryCaptions");QVERIFY(retry);retry->click();QCOMPARE(window.diagnostics().value("translationSkippedCount").toInt(),0);
        service->skippedCue(service->generation(),first,"Synthetic source one","Synthetic retry failure");QCOMPARE(window.diagnostics().value("translationSkippedCount").toInt(),1);
        translation->setChecked(false);translation->setChecked(true);QCOMPARE(window.diagnostics().value("translationSkippedCount").toInt(),0);
        service->failed("Synthetic missing backend");QVERIFY(window.diagnostics().value("translationFailed").toBool());QVERIFY(ai->isChecked());QVERIFY(translation->isChecked());
    }
    void repeatedCaptionTextKeepsItsOwnContextualTranslation(){
        QTemporaryDir folder;QSettings().setValue("translation/model",folder.filePath("missing-fixture.gguf"));
        luma::MainWindow window;window.openSource(folder.filePath("synthetic-video.mkv"));
        QFile file(folder.filePath("owned.srt"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("1\n00:00:00,000 --> 00:00:01,000\nSame synthetic words\n\n2\n00:00:02,000 --> 00:00:03,000\nSame synthetic words\n");file.close();QVERIFY(window.loadCaptionFile(file.fileName()));
        auto *service=window.findChild<luma::TranslationService*>();auto *overlay=window.findChild<luma::CaptionOverlay*>();auto *translation=window.findChild<QCheckBox*>("translationEnabled");QVERIFY(service);QVERIFY(overlay);QVERIFY(translation);translation->setChecked(true);
        const auto first=luma::translationCueKey({0,1000,"Same synthetic words"}),second=luma::translationCueKey({2000,3000,"Same synthetic words"});
        const auto generation=service->generation();service->translatedCue(generation,first,"Same synthetic words","첫 번째 문맥");service->translatedCue(generation,second,"Same synthetic words","두 번째 문맥");
        QCOMPARE(window.diagnostics().value("translationCount").toInt(),2);
        overlay->setPosition(100);QVERIFY(overlay->captionText().contains("첫 번째 문맥"));QVERIFY(!overlay->captionText().contains("두 번째 문맥"));
        overlay->setPosition(2100);QVERIFY(overlay->captionText().contains("두 번째 문맥"));QVERIFY(!overlay->captionText().contains("첫 번째 문맥"));
        window.openSource(folder.filePath("different-synthetic-video.mkv"));service->translatedCue(generation,first,"Same synthetic words","오래된 번역");QCOMPARE(window.diagnostics().value("translationCount").toInt(),0);
    }
    void futureTranslationsRefreshAfterNewOriginalContext(){
        QTemporaryDir folder;QSettings().setValue("ai/mode","cpu");QSettings().setValue("translation/model",folder.filePath("missing-fixture.gguf"));
        luma::MainWindow window;window.openSource(folder.filePath("synthetic-video.mkv"));window.enableAutomaticSubtitles("ja");
        auto *service=window.findChild<luma::TranslationService*>();auto *job=window.findChild<luma::SubtitleJob*>();auto *overlay=window.findChild<luma::CaptionOverlay*>();auto *enabled=window.findChild<QCheckBox*>("translationEnabled");QVERIFY(service);QVERIFY(job);QVERIFY(overlay);QVERIFY(enabled);enabled->setChecked(true);
        job->ready(job->generation(),{{0,1000,"Current synthetic source"},{2000,3000,"Future synthetic source"}});
        const auto generation=service->generation();const auto current=luma::translationCueKey({0,1000,"Current synthetic source"}),future=luma::translationCueKey({2000,3000,"Future synthetic source"});
        service->translatedCue(generation,current,"Current synthetic source","현재 문장");service->translatedCue(generation,future,"Future synthetic source","문맥 전 문장");
        QCOMPARE(window.diagnostics()["translationCount"].toInt(),2);QCOMPARE(window.diagnostics()["translationPendingCount"].toInt(),0);
        job->ready(job->generation(),{{4000,5000,"New neighboring original source"}});
        QCOMPARE(window.diagnostics()["translationCount"].toInt(),1);QCOMPARE(window.diagnostics()["translationPendingCount"].toInt(),2);
        overlay->setPosition(100);QVERIFY(overlay->captionText().contains("현재 문장"));
        service->translatedCue(generation,future,"Future synthetic source","새 문맥을 반영한 문장");QCOMPARE(window.diagnostics()["translationPendingCount"].toInt(),1);
        overlay->setPosition(2100);QVERIFY(overlay->captionText().contains("새 문맥을 반영한 문장"));
    }
    void diagnosticsRequirePersistedDeveloperMode(){
        luma::MainWindow window;auto *mode=window.findChild<QCheckBox*>("developerMode");auto *details=window.findChild<QWidget*>("developerDiagnostics");
        QVERIFY(mode);QVERIFY(details);QVERIFY(!mode->isChecked());QVERIFY(details->isHidden());
        QVERIFY(answerConsent(window,"developerConsentDialog",true));QVERIFY(!details->isHidden());QVERIFY(QSettings().value("ui/developerMode").toBool());
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
    void captureAllowanceRequiresTwoSeparateAcknowledgments(){
        luma::MainWindow window;
        auto *mode=window.findChild<QCheckBox*>("developerMode");
        auto *capture=window.findChild<QCheckBox*>("captureAllowed");
        auto *panel=window.findChild<QWidget*>("developerDiagnostics");QVERIFY(mode);QVERIFY(capture);QVERIFY(panel);
        QVERIFY(panel->isAncestorOf(capture));QVERIFY(!mode->isChecked());QVERIFY(!capture->isChecked());
        QVERIFY(answerConsent(window,"developerConsentDialog",false));QVERIFY(!mode->isChecked());QVERIFY(panel->isHidden());
        QVERIFY(answerConsent(window,"developerConsentDialog",true));QVERIFY(mode->isChecked());QVERIFY(!capture->isChecked());
        QVERIFY(answerConsent(window,"captureConsentDialog",false));QVERIFY(!capture->isChecked());
        QVERIFY(answerConsent(window,"captureConsentDialog",true));QVERIFY(capture->isChecked());
        {luma::MainWindow restored;QVERIFY(restored.findChild<QCheckBox*>("developerMode")->isChecked());QVERIFY(restored.findChild<QCheckBox*>("captureAllowed")->isChecked());}
        mode->setChecked(false);QVERIFY(!capture->isChecked());QVERIFY(!QSettings().value("privacy/captureAllowed").toBool());
        QVERIFY(answerConsent(window,"developerConsentDialog",true));QVERIFY(!capture->isChecked());
    }
    void legacyDeveloperAndCaptureSettingsDoNotBypassConsent(){
        QSettings().setValue("ui/developerMode",true);QSettings().setValue("privacy/captureProtectedDisc",false);QSettings().setValue("privacy/captureAllowed",true);QSettings().setValue("privacy/captureConsentVersion",1);
        luma::MainWindow window;
        auto *mode=window.findChild<QCheckBox*>("developerMode");auto *capture=window.findChild<QCheckBox*>("captureAllowed");
        QVERIFY(mode);QVERIFY(capture);QVERIFY(!mode->isChecked());QVERIFY(!capture->isChecked());
        QVERIFY(!window.diagnostics().value("developerMode").toBool());
        QVERIFY(answerConsent(window,"developerConsentDialog",true));
        luma::MainWindow restored;QVERIFY(!restored.findChild<QCheckBox*>("captureAllowed")->isChecked());
    }
    void captureRemainsBlockedWhileConsentDialogIsPending(){
        luma::MainWindow window;QVERIFY(answerConsent(window,"developerConsentDialog",true));
        auto *capture=window.findChild<QCheckBox*>("captureAllowed");QVERIFY(capture);QTemporaryDir folder;
        bool seen=false,blocked=false,allowed=false;
        QTimer::singleShot(0,&window,[&]{
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            seen=dialog->objectName()=="captureConsentDialog";
            QUrl source=QUrl::fromLocalFile(folder.path());source.setScheme("bluray");window.openSource(source.toString(QUrl::FullyEncoded));
            blocked=window.diagnostics().value("captureRequested").toBool();allowed=window.diagnostics().value("captureAllowed").toBool();dialog->reject();
        });
        capture->setChecked(true);QVERIFY(seen);QVERIFY2(blocked,"Opening or recovering a protected source during pending consent must not disable exclusion");QVERIFY(!allowed);QVERIFY(!capture->isChecked());
    }
    void queuedOutputLossCannotReopenANewerUserSource(){
        QTemporaryDir folder;luma::MainWindow window;window.openSource(folder.filePath("first-owned.mkv"));
        luma::PlaybackEngine *engine=nullptr;for(auto *item:window.findChildren<luma::PlaybackEngine*>())if(item->name().contains("mpv",Qt::CaseInsensitive))engine=item;QVERIFY(engine);
        QPointer<luma::PlaybackEngine> original=engine;engine->videoOutputLost("Synthetic previous-session device loss");window.openSource(folder.filePath("second-owned.mkv"));QTest::qWait(100);
        QCOMPARE(window.diagnostics().value("videoRecoveryCount").toInt(),0);QVERIFY(!original.isNull());
    }
    void protectedFolderUriRequestsCaptureExclusion(){
        QSettings().setValue("privacy/captureProtectedDisc",true);
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
    void realOwnedFileResumesAfterWindowCloses(){
        const auto media=qEnvironmentVariable("LUMA_UI_RESUME_MEDIA");if(media.isEmpty())QSKIP("Set LUMA_UI_RESUME_MEDIA to an owned local video for native resume verification.");
        QSettings().setValue("subtitles/aiEnabled",false);QSettings().setValue("subtitles/translation",false);
        {
            luma::MainWindow window;window.show();window.openSource(media);
            QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("durationMs").toDouble()>=35000 && window.diagnostics().value("playing").toBool(),15000);
            auto *play=window.findChild<QPushButton*>("playButton");auto *seek=window.findChild<QSlider*>("seekSlider");QVERIFY(play);QVERIFY(seek);play->click();
            seek->setSliderDown(true);seek->setValue(4000);seek->setSliderDown(false);
            QTRY_VERIFY_WITH_TIMEOUT(std::abs(window.diagnostics().value("positionMs").toDouble()-window.diagnostics().value("durationMs").toDouble()*.4)<1000,10000);
        }
        luma::ResumeHistory history;const auto saved=history.find(luma::resumeIdentity(media));QVERIFY2(saved.has_value(),"Closing a file must persist a resumable playback position");
        luma::MainWindow restored;restored.show();restored.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(restored.diagnostics().value("positionMs").toDouble()-saved->positionMs)<1500,15000);
        auto *resume=restored.findChild<QPushButton*>("resumePlayback");QVERIFY(resume);QVERIFY(resume->isEnabled());
    }
    void realOwnedFileRecoversVideoOutputOnceAndPreservesPause_data(){
        QTest::addColumn<int>("fraction");QTest::addColumn<bool>("paused");QTest::addColumn<bool>("toggleWhileLoading");
        QTest::newRow("paused-middle")<<4000<<true<<false;QTest::newRow("playing-middle")<<4000<<false<<false;
        QTest::newRow("paused-near-end")<<9400<<true<<false;QTest::newRow("playing-near-end")<<9400<<false<<false;
        QTest::newRow("paused-then-play")<<4000<<true<<true;QTest::newRow("playing-then-pause")<<4000<<false<<true;
    }
    void realOwnedFileRecoversVideoOutputOnceAndPreservesPause(){
        QFETCH(int,fraction);QFETCH(bool,paused);QFETCH(bool,toggleWhileLoading);
        const auto media=qEnvironmentVariable("LUMA_UI_RESUME_MEDIA");if(media.isEmpty())QSKIP("Set LUMA_UI_RESUME_MEDIA to owned local video for renderer recovery verification.");
        QSettings().setValue("subtitles/aiEnabled",false);QSettings().setValue("subtitles/translation",false);
        luma::MainWindow window;window.show();window.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("durationMs").toDouble()>=35000 && window.diagnostics().value("playing").toBool(),15000);
        auto *play=window.findChild<QPushButton*>("playButton");auto *seek=window.findChild<QSlider*>("seekSlider");QVERIFY(play);QVERIFY(seek);play->click();
        seek->setSliderDown(true);seek->setValue(fraction);seek->setSliderDown(false);
        QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("playing").toBool() && std::abs(window.diagnostics().value("positionMs").toDouble()-window.diagnostics().value("durationMs").toDouble()*fraction/10000.)<1000,10000);
        const auto position=window.diagnostics().value("positionMs").toDouble();const auto initialCues=window.diagnostics().value("aiCueCount").toInt();
        luma::PlaybackEngine *engine=nullptr;for(auto *item:window.findChildren<luma::PlaybackEngine*>())if(item->name().contains("mpv",Qt::CaseInsensitive))engine=item;QVERIFY(engine);
        bool inputDuringRecovery=false;
        QPointer<luma::PlaybackEngine> previous=engine;if(!paused)play->click();engine->videoOutputLost("Synthetic device removed notification");
        if(toggleWhileLoading)QTimer::singleShot(0,&window,[&]{if(window.diagnostics().value("resumePending").toBool()){play->click();paused=!paused;inputDuringRecovery=true;}});
        QTRY_COMPARE_WITH_TIMEOUT(window.diagnostics().value("videoRecoveryCount").toInt(),1,10000);
        if(toggleWhileLoading)QVERIFY(inputDuringRecovery);
        QTRY_VERIFY_WITH_TIMEOUT(previous.isNull(),10000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("resumePending").toBool() && window.diagnostics().value("playing").toBool()!=paused && std::abs(window.diagnostics().value("positionMs").toDouble()-position)<1000,15000);
        QCOMPARE(window.diagnostics().value("aiCueCount").toInt(),initialCues);
        engine=nullptr;for(auto *item:window.findChildren<luma::PlaybackEngine*>())if(item->name().contains("mpv",Qt::CaseInsensitive))engine=item;QVERIFY(engine);
        QPointer<luma::PlaybackEngine> replacement=engine;engine->videoOutputLost("Synthetic repeated removal");QTest::qWait(400);
        QCOMPARE(window.diagnostics().value("videoRecoveryCount").toInt(),1);QVERIFY(!replacement.isNull());QVERIFY(!window.diagnostics().value("playing").toBool());
        auto *status=window.findChild<QLabel*>("status");QVERIFY(status);QVERIFY(status->text().contains("다시 열어"));
    }
    void physicalDiscRecoveryPreservesImportedCaptions(){
        const auto disc=qEnvironmentVariable("LUMA_UI_TEST_DISC");if(disc.isEmpty())QSKIP("Set LUMA_UI_TEST_DISC for native title/caption recovery verification.");
        QSettings().setValue("subtitles/aiEnabled",false);QSettings().setValue("subtitles/translation",false);QSettings().setValue("subtitles/hangul",false);
        luma::MainWindow window;window.show();window.openSource(disc);auto *titles=window.findChild<QComboBox*>("discTitles");
        if(!titles)for(auto *box:window.findChildren<QComboBox*>())if(box->objectName()=="titles")titles=box;
        QVERIFY(titles);QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("title").toInt(-1)==0 && window.diagnostics().value("playing").toBool(),45000);
        const auto overlayReady=[&window]{for(const auto &message:window.diagnostics().value("recentMessages").toArray())if(message.toString().contains("Initializing overlay"))return true;return false;};QTRY_VERIFY_WITH_TIMEOUT(overlayReady(),15000);QTest::qWait(300);
        QPushButton *activate=nullptr;for(auto *control:window.findChildren<QPushButton*>())if(control->accessibleName()==QString::fromUtf8("디스크 확인")){activate=control;break;}QVERIFY(activate);activate->click();
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("title").toInt()==1 && window.diagnostics().value("timelineReady").toBool() && window.diagnostics().value("playing").toBool(),20000);
        auto *play=window.findChild<QPushButton*>("playButton");auto *seek=window.findChild<QSlider*>("seekSlider");QVERIFY(play);QVERIFY(seek);play->click();seek->setSliderDown(true);seek->setValue(500);seek->setSliderDown(false);
        QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("playing").toBool() && std::abs(window.diagnostics().value("positionMs").toDouble()-window.diagnostics().value("durationMs").toDouble()*.05)<1500,10000);
        const auto position=window.diagnostics().value("positionMs").toDouble();QTemporaryDir folder;QFile file(folder.filePath("owned.srt"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("1\n00:00:00,000 --> 03:00:00,000\nOwned synthetic recovery subtitle\n");file.close();QVERIFY(window.loadCaptionFile(file.fileName()));
        luma::PlaybackEngine *engine=nullptr;for(auto *item:window.findChildren<luma::PlaybackEngine*>())if(item->name().contains("VLC",Qt::CaseInsensitive))engine=item;QVERIFY(engine);engine->videoOutputLost("Synthetic device loss during physical disc playback");
        QTRY_COMPARE_WITH_TIMEOUT(window.diagnostics().value("videoRecoveryCount").toInt(),1,10000);
        bool recoveredOverlay=false;
        for(auto *item:window.findChildren<luma::PlaybackEngine*>())if(item->name().contains("VLC",Qt::CaseInsensitive))connect(item,&luma::PlaybackEngine::logMessage,&window,[&](const QString &message){if(message.contains("Initializing overlay"))recoveredOverlay=true;});
        QElapsedTimer restoreTime;restoreTime.start();bool activated=false,restored=false;int lastSecond=-1;
        while(restoreTime.elapsed()<60000){
            const auto state=window.diagnostics();
            if(state.value("title").toInt(-1)==0 && recoveredOverlay && !activated){QTest::qWait(300);activate->click();activated=true;}
            restored=!state.value("resumePending").toBool() && state.value("title").toInt()==1 && std::abs(state.value("positionMs").toDouble()-position)<1500;
            if(restored)break;
            const int seconds=int(restoreTime.elapsed()/1000);if(seconds/5!=lastSecond/5){qInfo()<<"Recovery observation"<<seconds<<"title"<<state.value("title").toInt()<<"position"<<state.value("positionMs").toDouble()<<"duration"<<state.value("durationMs").toDouble()<<"playing"<<state.value("playing").toBool()<<"timeline"<<state.value("timelineReady").toBool()<<"pending"<<state.value("resumePending").toBool();lastSecond=seconds;}
            QTest::qWait(100);
        }
        QVERIFY2(restored,"Restore the physical disc position after its permitted menu action");
        QCOMPARE(window.diagnostics().value("externalCueCount").toInt(),1);QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("playing").toBool(),5000);
    }
    void realOwnedFileRepeatedLossDuringRecoveryRemainsStopped(){
        const auto media=qEnvironmentVariable("LUMA_UI_RESUME_MEDIA");if(media.isEmpty())QSKIP("Set LUMA_UI_RESUME_MEDIA for pending recovery exhaustion verification.");
        QSettings().setValue("subtitles/aiEnabled",false);QSettings().setValue("subtitles/translation",false);luma::MainWindow window;window.show();window.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("playing").toBool() && window.diagnostics().value("durationMs").toDouble()>=35000,15000);
        luma::PlaybackEngine *engine=nullptr;for(auto *item:window.findChildren<luma::PlaybackEngine*>())if(item->name().contains("mpv",Qt::CaseInsensitive))engine=item;QVERIFY(engine);
        bool injected=false,exhaustionObserved=false,pendingAtExhaustion=true;engine->videoOutputLost("Synthetic first device removal");
        QTimer::singleShot(0,&window,[&]{if(!window.diagnostics().value("resumePending").toBool())return;for(auto *item:window.findChildren<luma::PlaybackEngine*>())if(item->name().contains("mpv",Qt::CaseInsensitive)){
            injected=true;item->videoOutputLost("Synthetic replacement loss while loading");
            // Model delayed native notifications: the queued loss must stop
            // recovery before a later status update can reuse its play intent.
            item->blockSignals(true);QTimer::singleShot(60,item,[item]{item->blockSignals(false);item->updated();});break;
        }QTimer::singleShot(0,&window,[&]{exhaustionObserved=window.diagnostics().value("videoRecoveryExhausted").toBool();pendingAtExhaustion=window.diagnostics().value("resumePending").toBool();});});
        QTRY_VERIFY_WITH_TIMEOUT(injected && exhaustionObserved,2000);QVERIFY(!pendingAtExhaustion);QTest::qWait(700);
        QVERIFY(!window.diagnostics().value("resumePending").toBool());QVERIFY(!window.diagnostics().value("playing").toBool());QVERIFY(!window.diagnostics().value("requestedPlaying").toBool());QCOMPARE(window.diagnostics().value("videoRecoveryCount").toInt(),1);
    }
    void realAudioSwitchPreservesImportedCaptions(){
        const auto media=qEnvironmentVariable("LUMA_UI_TEST_MEDIA");if(media.isEmpty())QSKIP("Set LUMA_UI_TEST_MEDIA to the owned two-audio video.");
        QSettings().setValue("subtitles/aiEnabled",false);QSettings().setValue("subtitles/translation",false);QSettings().setValue("subtitles/hangul",false);
        QTemporaryDir folder;QFile file(folder.filePath("owned.srt"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("1\n00:00:00,000 --> 00:01:00,000\nOwned original stays visible\n");file.close();
        luma::MainWindow window;window.show();window.openSource(media);auto *audio=window.findChild<QComboBox*>("audioTracks");QVERIFY(audio);
        QTRY_VERIFY_WITH_TIMEOUT(audio->count()>=2,15000);QVERIFY(window.loadCaptionFile(file.fileName()));
        audio->setCurrentIndex(1);QVERIFY(QMetaObject::invokeMethod(audio,"activated",Qt::DirectConnection,Q_ARG(int,1)));
        QTRY_COMPARE_WITH_TIMEOUT(window.diagnostics().value("currentAudioTrack").toInt(),audio->itemData(1).toInt(),5000);
        QCOMPARE(window.diagnostics().value("externalCueCount").toInt(),1);QVERIFY(!window.diagnostics().value("aiEnabled").toBool());
        auto *overlay=window.findChild<luma::CaptionOverlay*>();QVERIFY(overlay);QCOMPARE(overlay->captionText(),QString("Owned original stays visible"));
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
    void physicalDiscCaptionSeekSurvivesClockCorrection(){
        const auto disc=qEnvironmentVariable("LUMA_UI_TEST_DISC");if(disc.isEmpty())QSKIP("Set LUMA_UI_TEST_DISC for physical seek/caption-clock validation.");
        QSettings().setValue("ai/mode",qEnvironmentVariable("LUMA_UI_TEST_AI_MODE","auto"));QSettings().setValue("subtitles/translation",false);
        luma::MainWindow window;window.show();window.openSource(disc);
        auto *play=window.findChild<QPushButton*>("playButton");auto *seek=window.findChild<QSlider*>("seekSlider");auto *job=window.findChild<luma::SubtitleJob*>();QVERIFY(play);QVERIFY(seek);QVERIFY(job);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("title").toInt(-1)==0 && window.diagnostics().value("playing").toBool(),45000);
        const auto overlayReady=[&window]{for(const auto &message:window.diagnostics().value("recentMessages").toArray())if(message.toString().contains("Initializing overlay"))return true;return false;};
        QTRY_VERIFY_WITH_TIMEOUT(overlayReady(),15000);QTest::qWait(300);
        QPushButton *activate=nullptr;for(auto *control:window.findChildren<QPushButton*>())if(control->accessibleName()==QString::fromUtf8("디스크 확인")){activate=control;break;}QVERIFY(activate);activate->click();
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("playing").toBool() && window.diagnostics().value("durationMs").toDouble()>700000 && !window.diagnostics().value("navigationTitle").toBool(),20000);
        play->click();QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("playing").toBool(),5000);window.enableAutomaticSubtitles("ja");
        QSignalSpy failures(job,&luma::SubtitleJob::failed);QElapsedTimer elapsed;elapsed.start();
        seek->setSliderDown(true);seek->setValue(1127);seek->setSliderDown(false);
        QVERIFY(window.diagnostics().value("captionBufferHolding").toBool());QVERIFY(!window.diagnostics().value("captionResumeRequested").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("captionBufferHolding").toBool() || !failures.isEmpty(),90000);
        QVERIFY2(failures.isEmpty(),failures.isEmpty()?"":qPrintable(failures[0][0].toString()));const auto ready=window.diagnostics();
        QVERIFY(ready.value("captionBufferedAheadMs").toDouble()>=10000);QVERIFY(!ready.value("playing").toBool());QVERIFY(!ready.value("requestedPlaying").toBool());
        qInfo()<<"Physical paused seek readiness ms"<<elapsed.elapsed()<<"actual position"<<ready.value("positionMs").toDouble()<<"ahead"<<ready.value("captionBufferedAheadMs").toDouble()<<"backend"<<ready.value("aiBackend").toString();
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("captionBufferedAheadMs").toDouble()>=60000 || !failures.isEmpty(),60000);
        QVERIFY2(failures.isEmpty(),failures.isEmpty()?"":qPrintable(failures[0][0].toString()));QVERIFY(!window.diagnostics().value("playing").toBool());
        QVERIFY(window.diagnostics().value("captionBufferedAheadMs").toDouble()>=60000);
        qInfo()<<"Physical paused sixty-second prefetch ms"<<elapsed.elapsed()<<"ahead"<<window.diagnostics().value("captionBufferedAheadMs").toDouble();
        play->click();QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("playing").toBool(),5000);
        const auto resumed=window.diagnostics().value("positionMs").toDouble();QElapsedTimer monitor;monitor.start();
        while(monitor.elapsed()<8000){QTest::qWait(100);const auto status=window.diagnostics();QVERIFY2(!status.value("captionBufferHolding").toBool(),"A small backward VLC clock correction must not cause another optical seek/caption hold");QVERIFY(status.value("aiEnabled").toBool());QCOMPARE(status.value("aiRetryCount").toInt(),0);}
        QVERIFY(window.diagnostics().value("positionMs").toDouble()>=resumed+5000);window.findChild<QCheckBox*>("aiEnabled")->setChecked(false);
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
