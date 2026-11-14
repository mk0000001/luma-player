#include <QtTest>
#include <QComboBox>
#include <QCheckBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDialog>
#include <QLabel>
#include <QTimer>
#include <QFile>
#include <QFileDialog>
#include "ui/caption_overlay.h"
#include "ui/theme.h"
#include <QFontDatabase>
#include <QRawFont>
#include <QSettings>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QTabBar>
#include <QTreeWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslKey>
#include <QUrl>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScopeGuard>
#include <QtEndian>
#include <cmath>
#include <optional>
#include <algorithm>
#include "ui/main_window.h"
#include "ui/lan_playback_dialog.h"
#include "ui/performance_overlay.h"
#include "ui/video_surface.h"
#include "core/resume_history.h"
#include "subtitles/translation_service.h"
#include "subtitles/translation_devices.h"
#include "subtitles/ai_devices.h"
#include "subtitles/translation_support.h"
#include "subtitles/subtitle_file.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
class UiTests:public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    void restorePerformancePreferences(){
        QSettings settings;
        settings.setValue("ui/developerMode",true);
        settings.setValue("privacy/developerConsentVersion",1);
        settings.setValue("developer/performanceEnabled",true);
    }
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
        qputenv("LUMA_DISABLE_HOST_STORAGE_PROBES","1");
        QCoreApplication::setOrganizationName("LumaPlayerTests");
        QCoreApplication::setApplicationName("UiTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings_.path());
    }
    void init(){QSettings().clear();}
    void automaticStorageProbesStayDisabledInUiTests(){
        luma::MainWindow window;
        auto *label=window.findChild<QLabel*>("connectedDiscLabel");
        QVERIFY(label);
        QTRY_COMPARE(label->text(),QString("자동 디스크 검색 꺼짐"));
        QVERIFY(window.diagnostics().value("hostStorageProbesDisabled").toBool());
        QVERIFY(window.diagnostics().value("discRoot").toString().isEmpty());
    }
    void mobilePlaybackSettingsAllowNoOpenSource(){
        luma::MainWindow window;
        auto *share=window.findChild<QPushButton*>("openLanPlayback");
        QVERIFY(share);
        QVERIFY(share->isEnabled());
        share->click();
        auto *dialog=window.findChild<QDialog*>("lanPlaybackDialog");
        QVERIFY(dialog);
        auto *start=dialog->findChild<QPushButton*>("lanStart");
        QVERIFY(start);
        QVERIFY(!start->isEnabled());
        dialog->reject();
    }
    void mobilePlaybackOnlySelectsPrivateSubnetsAndEscapesCues(){
        QCOMPARE(luma::lanPrivateSubnet(QHostAddress("192.168.9.22"),24),QString("192.168.9.0/24"));
        QCOMPARE(luma::lanPrivateSubnet(QHostAddress("172.31.9.22"),16),QString("172.31.0.0/16"));
        QVERIFY(luma::lanPrivateSubnet(QHostAddress("8.8.8.8"),24).isEmpty());
        QVERIFY(luma::lanPrivateSubnet(QHostAddress("100.64.1.1"),24).isEmpty());
        QVERIFY(luma::lanPrivateSubnet(QHostAddress("127.0.0.1"),8).isEmpty());
        QVERIFY(luma::lanPrivateSubnet(QHostAddress("::1"),24).isEmpty());
        QVERIFY(luma::lanPrivateSubnet(QHostAddress("10.0.0.1"),0).isEmpty());
        QCOMPARE(luma::lanPrivatePeerSubnet("10.22.0.0/24"),QString("10.22.0.0/24"));
        QCOMPARE(luma::lanPrivatePeerSubnet("172.16.8.3/32"),QString("172.16.8.3/32"));
        QVERIFY(luma::lanPrivatePeerSubnet("10.22.0.0/23").isEmpty());
        QVERIFY(luma::lanPrivatePeerSubnet("10.22.0.1/24").isEmpty());
        QVERIFY(luma::lanPrivatePeerSubnet("100.64.0.0/24").isEmpty());
        QVERIFY(luma::lanPrivatePeerSubnet("203.0.113.1/32").isEmpty());
        QVERIFY(luma::lanLocalFileSupported("https://192.168.1.1/sample.mp4")==false);
        const auto track=luma::lanCaptionTrack({{1000,2750,"A < B\n\n한글 & 독음"},{0,0,"skip"}});
        QCOMPARE(track,QString("WEBVTT\n\n00:00:01.000 --> 00:00:02.750\nA &lt; B\n한글 &amp; 독음\n\n"));
    }
    void hdrAndAudioAreAvailableWithoutDeveloperMode(){
        luma::MainWindow window;
        auto *hdr=window.findChild<QComboBox*>("hdrOutputMode");
        auto *audio=window.findChild<QComboBox*>("audioOutputMode");
        auto *force=window.findChild<QCheckBox*>("developerForceHdr");
        auto *developer=window.findChild<QWidget*>("developerDiagnostics");
        QVERIFY(hdr && audio && force && developer);
        QVERIFY(!developer->isAncestorOf(hdr) && !developer->isAncestorOf(audio));
        QVERIFY(developer->isAncestorOf(force));
        QVERIFY(!window.diagnostics().value("developerMode").toBool());
        QCOMPARE(hdr->currentData().toString(),QString("auto"));
        QCOMPARE(audio->currentData().toString(),QString("system"));
        hdr->setCurrentIndex(hdr->findData("sdr"));
        QCOMPARE(QSettings().value("video/hdrOutput").toString(),QString("sdr"));
        hdr->setCurrentIndex(hdr->findData("auto"));
        QCOMPARE(QSettings().value("video/hdrOutput").toString(),QString("auto"));
        audio->setCurrentIndex(audio->findData("bitstream"));
        QCOMPARE(QSettings().value("audio/output").toString(),QString("bitstream"));
    }
    void developerPerformanceOverlayRequiresConsentAndExportsSamples(){
        QSettings().setValue("developer/performanceEnabled",true);
        luma::MainWindow window;
        window.show();
        auto *enabled=window.findChild<QCheckBox*>("performanceEnabled");
        auto *view=window.findChild<QComboBox*>("performanceView");
        auto *corner=window.findChild<QComboBox*>("performanceCorner");
        auto *developer=window.findChild<QCheckBox*>("developerMode");
        auto *copy=window.findChild<QPushButton*>("copyDiagnostics");
        auto *overlay=dynamic_cast<luma::PerformanceOverlay*>(window.findChild<QWidget*>("performanceOverlay"));
        QVERIFY(enabled && view && corner && developer && copy && overlay);
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier|Qt::ShiftModifier);
        QVERIFY(!enabled->isChecked() && !overlay->isVisible());
        QVERIFY(answerConsent(window,"developerConsentDialog",true));
        QTest::keyClick(&window,Qt::Key_D,Qt::ControlModifier|Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(overlay->summary().value("sampleCount").toInt()>=2,6000);
        QVERIFY(overlay->testAttribute(Qt::WA_TransparentForMouseEvents));
        view->setCurrentIndex(view->findData("graph"));
        corner->setCurrentIndex(corner->findData("left"));
        QCOMPARE(overlay->summary().value("view").toString(),QString("graph"));
        QCOMPARE(overlay->summary().value("position").toString(),QString("left"));
        const auto rows=overlay->csv().split('\n');
        QCOMPARE(rows[0].count(','),rows[1].count(','));
        QVERIFY(rows[0].contains("npuPercent") && rows[0].contains("discInputMiBps"));
        QVERIFY(rows[0].contains("playerCpuPercent") && rows[0].contains("playerGpuPercent") &&
                rows[0].contains("playerNpuPercent") && rows[0].contains("playerRamPercent"));
        QVERIFY(rows[0].contains("networkRxBytesPerSecond") && rows[0].contains("networkTxBytesPerSecond"));
        const auto latest=overlay->summary().value("latest").toObject();
        QVERIFY(latest.value("graphicsAdapters").isArray() && latest.value("storageTemperatures").isArray());
        const auto graphics=latest.value("graphicsAdapters").toArray();
        for(const auto &adapter:graphics){
            const auto prefix="gpu_"+adapter.toObject().value("luid").toString().toUtf8();
            QVERIFY(rows[0].contains(prefix+"_dedicatedMemoryTotalMiB"));
            QVERIFY(rows[0].contains(prefix+"_temperatureCelsius"));
        }
        copy->click();
        const auto snapshot=QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object();
        QVERIFY(snapshot.contains("performance"));
        QVERIFY(!snapshot.contains("recentMessages") && !snapshot.contains("discRoot"));
        const auto preview=qEnvironmentVariable("LUMA_UI_PERFORMANCE_PREVIEW");
        if(!preview.isEmpty()){
            view->setCurrentIndex(view->findData("both"));
            QVERIFY(overlay->grab().save(preview));
        }
        developer->setChecked(false);
        QVERIFY(!enabled->isChecked() && !overlay->isVisible());
        QVERIFY(!overlay->summary().value("enabled").toBool());
    }
    void developerTesterReportKeepsBoundedTechnicalEvidence(){
        QTemporaryDir output;
        QVERIFY(output.isValid());
        const auto path=output.filePath("diagnostics.json");
        const bool previousDialogSetting=QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        const auto restoreDialogs=qScopeGuard([previousDialogSetting]{
            QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,previousDialogSetting);
        });

        {
            luma::MainWindow gated;
            auto *blocked=gated.findChild<QPushButton*>("exportTesterDiagnostics");
            QVERIFY(blocked);
            bool opened=false;
            QTimer guard;
            guard.setSingleShot(true);
            connect(&guard,&QTimer::timeout,&gated,[&]{
                if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){
                    opened=true;
                    dialog->reject();
                }
            });
            guard.start(0);
            blocked->click();
            QVERIFY(!opened);
        }

        luma::MainWindow window;
        // The test supplies events directly and does not need queued startup probes.
        QCoreApplication::removePostedEvents(&window,QEvent::MetaCall);
        auto *save=window.findChild<QPushButton*>("exportTesterDiagnostics");
        auto *job=window.findChild<luma::SubtitleJob*>();
        auto *translation=window.findChild<luma::TranslationService*>();
        auto *overlay=dynamic_cast<luma::PerformanceOverlay*>(window.findChild<QWidget*>("performanceOverlay"));
        auto *developer=window.findChild<QCheckBox*>("developerMode");
        auto *enabled=window.findChild<QCheckBox*>("performanceEnabled");
        const auto engines=window.findChildren<luma::PlaybackEngine*>();
        QVERIFY(save && job && translation && overlay && developer && enabled && !engines.isEmpty());
        QCOMPARE(overlay->summary().value("sampleCount").toInt(),0);
        QVERIFY(answerConsent(window,"developerConsentDialog",true));
        QVERIFY(!enabled->isChecked() && !overlay->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(overlay->summary().value("sampleCount").toInt()>=2,6000);
        const auto before=overlay->summary().value("sampleCount").toInt();
        enabled->setChecked(true);
        enabled->setChecked(false);
        QVERIFY(overlay->summary().value("sampleCount").toInt()>=before);
        const QString privateData="C:/Users/private-user/test-title.mkv secret-subtitle key=private-token";
        QSettings().setValue("ai/accelerationLastInstall",QVariantMap{{"backend","vulkan"},{"result","failed"},
            {"exitCode",27},{"message",privateData}});
        job->setPreferences({"gpu","Vulkan2"});
        translation->setPreferences({"gpu","Vulkan2"});
        for(int i=0;i<270;++i)job->failed(privateData);
        job->progress("GPU 사용 불가 (가속 실행 확인 실패) · 다음 장치로 전환 중…");
        translation->backendSelected("GPU / Vulkan2 / "+privateData);
        translation->progress("Vulkan2 번역 사용 불가: 로컬 번역 추론 요청이 실패했습니다.");
        engines.front()->logMessage(privateData);
        engines.front()->videoOutputLost("영상 출력 장치를 사용할 수 없어 영상 출력을 다시 준비해야 합니다. (DXGI_ERROR_DEVICE_REMOVED, 0x887a0005)");
        engines.front()->errorOccurred("AACS 설정 파일을 찾거나 열 수 없습니다. "+privateData);

        bool dialogSeen=false;
        QTimer::singleShot(0,&window,[&]{
            if(auto *dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){
                dialogSeen=true;
                dialog->selectFile(path);
                static_cast<QDialog*>(dialog)->accept();
            }
        });
        save->click();
        QVERIFY(dialogSeen);
        QFile report(path);
        QVERIFY(report.open(QIODevice::ReadOnly));
        const auto bytes=report.readAll();
        QVERIFY(bytes.size()<=1024*1024);
        for(const auto forbidden:{"private-user","test-title.mkv","secret-subtitle","private-token","recentMessages","luid","diskNumber","sensorIndex"})
            QVERIFY2(!bytes.contains(forbidden),forbidden);
        const auto json=QJsonDocument::fromJson(bytes).object();
        QCOMPARE(json.value("schemaVersion").toInt(),2);
        const auto performance=json.value("performance").toObject();
        QCOMPARE(performance.value("sampleIntervalMs").toInt(),1000);
        QCOMPARE(performance.value("retentionMs").toInt(),300000);
        const auto samples=performance.value("samples").toArray();
        QVERIFY(samples.size()>=2 && samples.size()<=300);
        const auto timeline=json.value("timeline").toArray();
        const auto timelineEvents=json.value("events").toArray();
        QCOMPARE(timeline.size(),samples.size()+timelineEvents.size());
        qsizetype sampleIndex=0,eventIndex=0;
        qint64 previous=-1;
        for(const auto &value:timeline){
            const auto item=value.toObject();
            const auto record=item.value("record");
            const auto elapsed=record.toObject().value("elapsedMs").toInteger(-1);
            QVERIFY(elapsed>=0 && elapsed>=previous);
            previous=elapsed;
            if(item.value("kind")=="performance")QCOMPARE(record,samples.at(sampleIndex++));
            else {QCOMPARE(item.value("kind").toString(),QString("event"));QCOMPARE(record,timelineEvents.at(eventIndex++));}
        }
        const auto latest=samples.last().toObject();
        QVERIFY(latest.value("elapsedMs").isDouble());
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        QVERIFY(latest.value("ramTotalMiB").toDouble()>0);
        QVERIFY(latest.value("playerResidentMiB").toDouble()>0);
#endif
        QVERIFY(json.value("graphicsAdapters").isArray());
        for(const auto &value:json.value("graphicsAdapters").toArray()){
            const auto adapter=value.toObject();
            QVERIFY(adapter.value("vendorId").isDouble() && adapter.value("deviceId").isDouble());
            QVERIFY(adapter.value("dedicatedMemoryMiB").toDouble(-1)>=0);
            if(adapter.contains("driverVersion"))
                QVERIFY(QRegularExpression(R"(\A[0-9]{1,5}(?:\.[0-9]{1,5}){3}\z)").match(adapter.value("driverVersion").toString()).hasMatch());
        }
        QVERIFY(json.value("installedWorkers").isObject());
        const auto installer=json.value("lastAccelerationInstall").toObject();
        QCOMPARE(installer.value("backend").toString(),QString("vulkan"));
        QCOMPARE(installer.value("result").toString(),QString("failed"));
        QCOMPARE(installer.value("exitCode").toInt(),27);
        QVERIFY(!json.value("os").toObject().value("kernelVersion").toString().isEmpty());
        QCOMPARE(json.value("asr").toObject().value("requested").toObject().value("device").toString(),QString("Vulkan2"));
        QCOMPARE(json.value("translation").toObject().value("requested").toObject().value("device").toString(),QString("Vulkan2"));
        const auto events=json.value("events").toArray();
        QCOMPARE(events.size(),qsizetype(256));
        QVERIFY(json.value("droppedEvents").toInt()>0);
        const auto hasEvent=[&](const char *code,const char *reason,const char *device=nullptr){
            return std::any_of(events.begin(),events.end(),[&](const QJsonValue &entry){
                const auto event=entry.toObject();
                return event.value("code").toString()==code && event.value("reason").toString()==reason &&
                    (!device || event.value("device").toString()==device);
            });
        };
        QVERIFY(hasEvent("asr_fallback","acceleration_unconfirmed","GPU"));
        QVERIFY(hasEvent("translation_backend_confirmed","gpu_offload","Vulkan2"));
        QVERIFY(hasEvent("translation_fallback","inference_failed","Vulkan2"));
        QVERIFY(hasEvent("video_output_lost","dxgi_device_removed"));
        QVERIFY(hasEvent("video_failed","aacs_config_unavailable"));
        developer->setChecked(false);
        QCOMPARE(overlay->summary().value("sampleCount").toInt(),0);
        QVERIFY(!overlay->summary().value("collecting").toBool());
    }
    void diagnosticResourceSamplesKeepRatesAndExcludePlaybackText(){
        QWidget anchor;
        int calls=0;
        luma::PerformanceOverlay overlay(&anchor,[&]{
            luma::PerformanceInput input;
            luma::PlaybackState state;
            input.sourceGeneration=1;
            input.disc=true;
            input.captionAheadMs=15000;
            input.recognitionRuntime="vulkan";
            input.translationRuntime="raw-untrusted-backend";
            input.translationBackend="C:/Users/private-user/third-party.log";
            state.playing=true;
            state.discStatisticsAvailable=true;
            state.currentTitle=3;
            state.discInputBytes=1048576 * ++calls;
            state.discDemuxBytes=524288 * calls;
            state.sourceFps=23.976;
            state.displayFps=59.94;
            state.cacheSeconds=3.25;
            state.avSyncMs=-2.5;
            state.mediaTitle="private-title";
            state.videoCodec="private-codec";
            state.audioTracks.append({1,"private-track"});
            input.playback=state;
            return input;
        });
        overlay.setDiagnosticCollection(true);
        QTRY_VERIFY_WITH_TIMEOUT(overlay.diagnosticHistory().value("samples").toArray().size()>=2,6000);
        QVERIFY(!overlay.isVisible());
        const auto history=overlay.diagnosticHistory();
        const auto last=history.value("samples").toArray().last().toObject();
        QCOMPARE(last.value("engine").toString(),QString("libvlc"));
        QCOMPARE(last.value("asrRuntime").toString(),QString("vulkan"));
        QCOMPARE(last.value("captionAheadMs").toInt(),15000);
        QCOMPARE(last.value("sourceFps").toDouble(),23.98);
        QCOMPARE(last.value("displayHz").toDouble(),59.94);
        QCOMPARE(last.value("fileCacheSeconds").toDouble(),3.25);
        QCOMPARE(last.value("avSyncMs").toDouble(),-2.5);
        QVERIFY(last.value("discInputMiBps").toDouble()>0);
        QVERIFY(last.value("discDemuxMiBps").toDouble()>0);
        const auto encoded=QJsonDocument(history).toJson();
        for(const auto forbidden:{"private-user","third-party.log","private-title","private-codec","private-track",
            "raw-untrusted-backend","luid","diskNumber","sensorIndex","positionMs","currentTitle"})
            QVERIFY2(!encoded.contains(forbidden),forbidden);
        overlay.clearHistory();
        QVERIFY(overlay.diagnosticHistory().value("samples").toArray().isEmpty());
        overlay.setDiagnosticCollection(false);
        QVERIFY(!overlay.summary().value("collecting").toBool());
    }
    void seekTrackClickCommitsPosition(){
        luma::MainWindow window;
        window.show();
        auto *seek=window.findChild<QSlider*>("seekSlider");
        auto *transport=window.findChild<QWidget*>("transport");
        auto *surface=window.findChild<luma::VideoSurface*>("videoSurface");
        QVERIFY(seek && transport && surface);
        QTRY_VERIFY(seek->isVisible());
        QTRY_COMPARE(transport->width(),surface->width());
        QTRY_COMPARE(seek->width(),transport->contentsRect().width()-36);
        QSignalSpy released(seek,&QSlider::sliderReleased);
        QTest::mouseClick(seek,Qt::LeftButton,Qt::NoModifier,QPoint(seek->width()*3/4,seek->height()/2));
        QCOMPARE(released.size(),1);
        QVERIFY2(seek->value()>7100 && seek->value()<7900,qPrintable(QString("seek value %1 / %2, width %3").arg(seek->value()).arg(seek->maximum()).arg(seek->width())));
    }
#ifdef Q_OS_WIN
    void mobilePlaybackUsesAuthenticatedLocalHost(){
        const auto source=qEnvironmentVariable("LUMA_UI_LAN_SOURCE");
        const auto address=qEnvironmentVariable("LUMA_UI_LAN_ADDRESS");
        if(QGuiApplication::platformName()!="windows" || source.isEmpty() || address.isEmpty())
            QSKIP("Requires the staged LAN host, native playback and a selected private adapter");
        const bool disc=source.endsWith(".json",Qt::CaseInsensitive);
        QSettings().setValue("playback/volume",0);
        luma::MainWindow window;
        window.show();
        if(disc)QVERIFY(window.loadPreparedCaptionProject(source));
        else window.openSource(source);
        auto *share=window.findChild<QPushButton*>("openLanPlayback");
        QVERIFY(share);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("positionMs").toInteger()>100,30000);
        if(disc)QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("preparedCaptionsApplied").toBool(),20000);
        window.startLanDiscovery();
        share->click();
        auto *dialog=window.findChild<QDialog*>("lanPlaybackDialog");
        QVERIFY(dialog);
        const auto close=qScopeGuard([dialog]{dialog->reject();});
        auto *endpoint=dialog->findChild<QLabel*>("lanPlaybackAddress");
        auto *status=dialog->findChild<QLabel*>("lanPlaybackStatus");
        auto *start=dialog->findChild<QPushButton*>("lanStart");
        auto *stop=dialog->findChild<QPushButton*>("lanStop");
        auto *remoteControl=dialog->findChild<QCheckBox*>("lanRemoteControl");
        QVERIFY(endpoint && status && start && stop && remoteControl);
        QTRY_VERIFY2_WITH_TIMEOUT(endpoint->text().contains(address),qPrintable(endpoint->text()+" · "+status->text()),12000);
        QVERIFY(!start->isEnabled());

        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        QByteArray peerKey;
        connect(&manager,&QNetworkAccessManager::sslErrors,&manager,[&](QNetworkReply *reply,const QList<QSslError> &errors){
            const auto key=reply->sslConfiguration().peerCertificate().publicKey().toDer();
            if(key.isEmpty())return;
            if(peerKey.isEmpty() && reply->url().path()=="/v1/pair/identity")peerKey=key;
            if(key==peerKey)reply->ignoreSslErrors(errors);
        });
        const auto request=[&](const QString &path,const QByteArray &token=QByteArray{}){
            QNetworkRequest value(QUrl("https://"+address+":48762"+path));
            value.setTransferTimeout(10000);
            if(!token.isEmpty())value.setRawHeader("Authorization","Bearer "+token);
            return value;
        };
        const auto post=[&](const QString &path,const QJsonObject &body){
            auto value=request(path);
            value.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
            return manager.post(value,QJsonDocument(body).toJson(QJsonDocument::Compact));
        };
        auto *identity=manager.get(request("/v1/pair/identity"));
        QTRY_VERIFY_WITH_TIMEOUT(identity->isFinished(),12000);
        QVERIFY2(identity->error()==QNetworkReply::NoError,qPrintable(identity->errorString()));
        QVERIFY(!peerKey.isEmpty());
        auto *rejected=manager.get(request("/v1/session"));
        QTRY_VERIFY_WITH_TIMEOUT(rejected->isFinished(),12000);
        QCOMPARE(rejected->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),401);

        const auto base64Url=[](const QByteArray &bytes){
            return bytes.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals);
        };
        const auto nonce=[&](int size){
            QByteArray bytes(size,Qt::Uninitialized);
            for(auto &byte:bytes)byte=static_cast<char>(QRandomGenerator::system()->bounded(256));
            return base64Url(bytes);
        };
        const auto sha=[](const QByteArray &bytes){return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);};
        const auto input=base64Url(sha(peerKey))+'\n'+nonce(16)+'\n'+nonce(32);
        const auto fields=input.split('\n');
        auto *attempt=post("/v1/pair/start",{{"deviceId",QString::fromLatin1(fields[1])},{"device","LumaPlayer QA"},
            {"commitment",QString::fromLatin1(base64Url(sha("luma-pair-commit-v1\n"+input)))},{"remember",false}});
        QTRY_VERIFY_WITH_TIMEOUT(attempt->isFinished(),12000);
        QVERIFY2(attempt->error()==QNetworkReply::NoError,qPrintable(attempt->errorString()));
        const auto challenge=QJsonDocument::fromJson(attempt->readAll()).object();
        const auto attemptId=challenge.value("id").toString();
        const auto serverNonce=challenge.value("serverNonce").toString().toLatin1();
        QVERIFY(!attemptId.isEmpty() && serverNonce.size()==43);
        auto *reveal=post("/v1/pair/reveal",{{"id",attemptId},{"clientNonce",QString::fromLatin1(fields[2])}});
        QTRY_VERIFY_WITH_TIMEOUT(reveal->isFinished(),12000);
        QVERIFY2(reveal->error()==QNetworkReply::NoError,qPrintable(reveal->errorString()));
        QDialog *approval=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((approval=window.findChild<QDialog*>("lanPairRequest")),5000);
        auto *code=approval->findChild<QLabel*>("lanPairingCode");
        auto *approve=approval->findChild<QPushButton*>("lanPairAllowOnce");
        QVERIFY(code && approve);
        const auto codeDigest=sha("luma-pair-code-v1\n"+input+'\n'+serverNonce);
        QCOMPARE(code->text(),QString("%1").arg(qFromBigEndian<quint32>(codeDigest.constData())%1000000,6,10,QChar('0')));
        approve->click();
        QTRY_VERIFY(start->isEnabled());
        auto *poll=post("/v1/pair/poll",{{"id",attemptId}});
        QTRY_VERIFY_WITH_TIMEOUT(poll->isFinished(),12000);
        const auto approvalReply=QJsonDocument::fromJson(poll->readAll()).object();
        QCOMPARE(approvalReply.value("state").toString(),QString("approved"));
        const auto token=approvalReply.value("token").toString().toLatin1();
        QCOMPARE(token.size(),22);
        QVERIFY(approvalReply.value("rememberToken").toString().isEmpty());
        QVERIFY(remoteControl->isChecked());
        auto *menu=manager.get(request("/v1/player/disc-menu.png",token));
        QTRY_VERIFY_WITH_TIMEOUT(menu->isFinished(),12000);
        QCOMPARE(menu->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),204);
        remoteControl->click();
        auto *controlRevoked=manager.get(request("/v1/player/disc-menu.png",token));
        QTRY_VERIFY_WITH_TIMEOUT(controlRevoked->isFinished(),12000);
        QCOMPARE(controlRevoked->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),403);
        auto *beforeShare=manager.get(request("/v1/session",token));
        QTRY_VERIFY_WITH_TIMEOUT(beforeShare->isFinished(),12000);
        QCOMPARE(beforeShare->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),409);
        QVERIFY(!dialog->property("sharing").toBool());
        if(disc){
            start->click();
        }else{
            auto *remoteStart=dialog->findChild<QCheckBox*>("lanAllowRemoteStart");
            QVERIFY(remoteStart);
            QVERIFY(!remoteStart->isChecked());
            auto *denied=manager.post(request("/v1/playback/start",token),QByteArray());
            QTRY_VERIFY_WITH_TIMEOUT(denied->isFinished(),12000);
            QCOMPARE(denied->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),403);
            QVERIFY(!dialog->property("sharing").toBool());
            remoteStart->click();
            auto *requested=manager.post(request("/v1/playback/start",token),QByteArray());
            QTRY_VERIFY_WITH_TIMEOUT(requested->isFinished(),12000);
            QCOMPARE(requested->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),202);
        }
        QTRY_VERIFY2_WITH_TIMEOUT(dialog->property("sharing").toBool(),qPrintable(status->text()),15000);
        auto *session=manager.get(request("/v1/session",token));
        QTRY_VERIFY_WITH_TIMEOUT(session->isFinished(),12000);
        QVERIFY2(session->error()==QNetworkReply::NoError,qPrintable(session->errorString()));
        const auto metadata=QJsonDocument::fromJson(session->readAll()).object();
        QCOMPARE(metadata.value("sourceKind").toString(),disc?QString("bluray"):QString("file"));
        QCOMPARE(metadata.value("seekable").toBool(),!disc);
        auto media=request("/v1/media",token);
        if(!disc)media.setRawHeader("Range","bytes=0-95");
        auto *stream=manager.get(media);
        stream->setReadBufferSize(64*1024);
        QTRY_VERIFY_WITH_TIMEOUT(stream->bytesAvailable()>=4 || stream->isFinished(),15000);
        if(disc){
            QCOMPARE(stream->read(4).toHex(),QByteArray("1a45dfa3"));
            QVERIFY(!metadata.value("subtitles").toArray().isEmpty());
        }else{
            QTRY_VERIFY(stream->isFinished());
            QCOMPARE(stream->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),206);
            QFile expected(source);
            QVERIFY(expected.open(QIODevice::ReadOnly));
            QCOMPARE(stream->readAll(),expected.read(96));
        }
        stream->abort();
        stop->click();
        QTRY_VERIFY_WITH_TIMEOUT(!stop->isEnabled() && !dialog->property("sharing").toBool(),8000);
        auto *expired=manager.get(request("/v1/session",token));
        QTRY_VERIFY_WITH_TIMEOUT(expired->isFinished(),12000);
        QCOMPARE(expired->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),401);
    }

    void windowsPlayersPairAndShareOneSource(){
        const auto source=qEnvironmentVariable("LUMA_UI_LAN_PC_FIXTURE");
        const auto address=qEnvironmentVariable("LUMA_UI_LAN_ADDRESS");
        if(QGuiApplication::platformName()!="windows" || !QFileInfo(source).isFile() || address.isEmpty())
            QSKIP("Requires a synthetic local fixture, the native player, and a private adapter");
        QTemporaryDir captionDirectory;
        QVERIFY(captionDirectory.isValid());
        const auto captionPath=captionDirectory.filePath("lan-synthetic.srt");
        QFile captionFile(captionPath);
        const QByteArray cue="1\n00:00:00,000 --> 00:00:30,000\nSynthetic LAN subtitle fixture\n";
        QVERIFY(captionFile.open(QIODevice::WriteOnly));
        QCOMPARE(captionFile.write(cue),qint64(cue.size()));
        captionFile.close();
        QSettings().setValue("playback/volume",0);
        luma::MainWindow host;
        luma::MainWindow receiver;
        host.show();
        receiver.show();
        host.openSource(source);
        QTRY_VERIFY_WITH_TIMEOUT(host.diagnostics().value("positionMs").toInteger()>100,15000);
        QVERIFY(host.loadCaptionFile(captionPath));
        QTRY_COMPARE_WITH_TIMEOUT(host.diagnostics().value("externalCueCount").toInteger(),qint64(1),10000);
        host.startLanDiscovery();

        auto *showHost=host.findChild<QPushButton*>("openLanPlayback");
        auto *showReceiver=receiver.findChild<QPushButton*>("openLanPlayback");
        QVERIFY(showHost && showReceiver);
        showHost->click();
        showReceiver->click();
        auto *endpoint=host.findChild<QLabel*>("lanPlaybackAddress");
        auto *openReceiver=receiver.findChild<QPushButton*>("lanReceive");
        QVERIFY(endpoint && openReceiver);
        QTRY_VERIFY_WITH_TIMEOUT(endpoint->text().contains(address),12000);
        openReceiver->click();
        auto *receiveDialog=receiver.findChild<QDialog*>("lanReceiverDialog");
        QVERIFY(receiveDialog);
        auto *destination=receiveDialog->findChild<QLineEdit*>("lanReceiverAddress");
        auto *connectButton=receiveDialog->findChild<QPushButton*>("lanReceiverConnect");
        auto *clientCode=receiveDialog->findChild<QLabel*>("lanReceiverCode");
        auto *confirm=receiveDialog->findChild<QPushButton*>("lanReceiverConfirm");
        auto *receiverStatus=receiveDialog->findChild<QLabel*>("lanReceiverStatus");
        auto *pcs=receiveDialog->findChild<QListWidget*>("lanReceiverPcs");
        QVERIFY(destination && connectButton && clientCode && confirm && receiverStatus && pcs);
        const auto discovered=[&]() -> QListWidgetItem* {
            for(int index=0;index<pcs->count();++index){
                auto *item=pcs->item(index);
                if(item->data(Qt::UserRole).toString()==address+":48762")return item;
            }
            return nullptr;
        };
        QTRY_VERIFY2_WITH_TIMEOUT(discovered(),qPrintable(receiverStatus->text()),8000);
        pcs->setCurrentItem(discovered());
        QCOMPARE(destination->text(),address+":48762");
        destination->setText(address);
        connectButton->click();

        QDialog *approval=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((approval=host.findChild<QDialog*>("lanPairRequest")),15000);
        auto *hostCode=approval->findChild<QLabel*>("lanPairingCode");
        auto *allowOnce=approval->findChild<QPushButton*>("lanPairAllowOnce");
        QVERIFY(hostCode && allowOnce);
        QTRY_VERIFY_WITH_TIMEOUT(confirm->isEnabled() && !clientCode->text().isEmpty(),10000);
        QCOMPARE(clientCode->text().trimmed(),hostCode->text().trimmed());
        allowOnce->click();
        QVERIFY(!receiver.diagnostics().value("lanReceiving").toBool());
        confirm->click();
        auto *shareDialog=host.findChild<QDialog*>("lanPlaybackDialog");
        auto *hostStatus=host.findChild<QLabel*>("lanPlaybackStatus");
        auto *playbackStatus=receiver.findChild<QLabel*>("status");
        auto *start=host.findChild<QPushButton*>("lanStart");
        QVERIFY(shareDialog && hostStatus && playbackStatus && start);
        QTRY_VERIFY(start->isEnabled());
        QVERIFY(!shareDialog->property("sharing").toBool());
        start->click();
        QTRY_VERIFY2_WITH_TIMEOUT(shareDialog->property("sharing").toBool(),qPrintable(hostStatus->text()),15000);
        const auto failure=[&]{
            const auto state=receiver.diagnostics();
            return QString("receiving=%1 capture=%2/%3 pos=%4 failed=%5 cue=%6 · %7 · %8")
                .arg(state.value("lanReceiving").toBool()).arg(state.value("captureRequested").toBool())
                .arg(state.value("captureApplied").toBool()).arg(state.value("positionMs").toInteger())
                .arg(state.value("failed").toBool()).arg(state.value("lanSubtitleCueActive").toBool())
                .arg(receiverStatus->text(),playbackStatus->text());
        };
        QTRY_VERIFY2_WITH_TIMEOUT(receiver.diagnostics().value("lanReceiving").toBool() &&
            receiver.diagnostics().value("positionMs").toInteger()>750 &&
            receiver.diagnostics().value("lanSubtitleCueActive").toBool(),qPrintable(failure()),20000);
        const auto playback=receiver.diagnostics();
        QVERIFY(playback.value("captureRequested").toBool());
        QVERIFY(playback.value("captureApplied").toBool());
        QVERIFY(playback.value("lanReceiveSeekable").toBool());
        QVERIFY(!QJsonDocument(playback).toJson(QJsonDocument::Compact).contains("127.0.0.1"));

        auto *seek=receiver.findChild<QSlider*>("seekSlider");
        auto *pause=receiver.findChild<QPushButton*>("playButton");
        QVERIFY(seek && pause);
        const auto duration=playback.value("durationMs").toInteger();
        QVERIFY(duration>4000);
        pause->click();
        QTRY_VERIFY(!receiver.diagnostics().value("playing").toBool());
        seek->setSliderDown(true);
        seek->setValue(6500);
        seek->setSliderDown(false);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(receiver.diagnostics().value("positionMs").toDouble()-duration*.65)<1000,10000);

        auto *devices=host.findChild<QTreeWidget*>("lanDevices");
        auto *disconnect=host.findChild<QPushButton*>("lanDisconnectDevice");
        QVERIFY(devices && disconnect);
        QTRY_VERIFY_WITH_TIMEOUT(devices->topLevelItemCount()>0,8000);
        devices->setCurrentItem(devices->topLevelItem(0));
        QTRY_VERIFY_WITH_TIMEOUT(disconnect->isEnabled(),5000);
        disconnect->click();
        QTRY_VERIFY_WITH_TIMEOUT(!receiver.diagnostics().value("lanReceiving").toBool(),20000);
    }

    void hdrFollowsDisplayAndSupportsDeveloperForce(){
        const auto hdrMedia=qEnvironmentVariable("LUMA_UI_TEST_HDR_MEDIA");
        const auto sdrMedia=qEnvironmentVariable("LUMA_UI_TEST_PIP_MEDIA");
        const auto hdrName=qEnvironmentVariable("LUMA_UI_HDR_SCREEN"),sdrName=qEnvironmentVariable("LUMA_UI_SDR_SCREEN");
        if(QGuiApplication::platformName()!="windows" || hdrName.isEmpty() || sdrName.isEmpty() ||
           !QFileInfo::exists(hdrMedia) || !QFileInfo::exists(sdrMedia))QSKIP("Requires native HDR and SDR displays and owned fixtures");
        const auto findScreen=[](const QString &name)->QScreen*{
            for(auto *screen:QGuiApplication::screens())
                if(screen->name().endsWith(name,Qt::CaseInsensitive) || screen->model().compare(name,Qt::CaseInsensitive)==0)return screen;
            return nullptr;
        };
        auto *hdrScreen=findScreen(hdrName),*sdrScreen=findScreen(sdrName);
        QStringList screens;
        for(auto *screen:QGuiApplication::screens())screens.append(screen->name()+" / "+screen->model());
        QVERIFY2(hdrScreen && sdrScreen && hdrScreen!=sdrScreen,qPrintable(screens.join("; ")));
        luma::MainWindow window;
        const auto move=[&](QScreen *screen){window.setScreen(screen);window.move(screen->availableGeometry().topLeft()+QPoint(20,20));};
        move(hdrScreen);window.show();
        auto *hdr=window.findChild<QComboBox*>("hdrOutputMode");
        auto *force=window.findChild<QCheckBox*>("developerForceHdr");
        auto *developer=window.findChild<QCheckBox*>("developerMode");
        auto *displayLabel=window.findChild<QLabel*>("displayStatus");
        QVERIFY(hdr && force && developer && displayLabel);
        QTRY_VERIFY(window.diagnostics().value("display").toObject().value("hdrActive").toBool());
        window.openSource(hdrMedia);
        QTRY_COMPARE_WITH_TIMEOUT(window.diagnostics().value("videoTransfer").toString(),QString("pq"),10000);
        QCOMPARE(window.diagnostics().value("requestedVideoTransfer").toString(),QString("auto"));
        QVERIFY(!developer->isChecked());
        hdr->setCurrentIndex(hdr->findData("sdr"));
        QTRY_COMPARE_WITH_TIMEOUT(window.diagnostics().value("requestedVideoTransfer").toString(),QString("gamma2.2"),5000);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("videoTransfer").toString()!="pq",5000);
        hdr->setCurrentIndex(hdr->findData("auto"));
        QTRY_COMPARE_WITH_TIMEOUT(window.diagnostics().value("videoTransfer").toString(),QString("pq"),5000);

        move(sdrScreen);
        QTRY_VERIFY(displayLabel->text().contains("SDR 출력"));
        window.openSource(sdrMedia);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("positionMs").toInteger()>200,10000);
        QVERIFY(answerConsent(window,"developerConsentDialog",true));
        force->setChecked(true);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("forcedHdr").toBool(),5000);
        const auto status=window.diagnostics().value("videoOverrideStatus").toString();
        QVERIFY2(status.isEmpty(),qPrintable(status));
        QTRY_COMPARE_WITH_TIMEOUT(window.diagnostics().value("videoTransfer").toString(),QString("pq"),5000);
        QCOMPARE(window.diagnostics().value("videoPrimaries").toString(),QString("bt.2020"));
        QVERIFY(!window.diagnostics().value("display").toObject().value("hdrActive").toBool());
        qInfo().noquote()<<"SDR display with forced renderer:"<<displayLabel->text()<<window.diagnostics().value("videoTransfer").toString();
        developer->setChecked(false);
        QTRY_VERIFY(!window.diagnostics().value("forcedHdr").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("videoTransfer").toString()!="pq",5000);
    }
    void nativeAudioReopensWithBitstreamOrPcmFallback(){
        const auto media=qEnvironmentVariable("LUMA_UI_AUDIO_MEDIA");
        if(QGuiApplication::platformName()!="windows" || !QFileInfo::exists(media))QSKIP("Requires native output and a silent Dolby Digital Plus fixture");
        QSettings().setValue("playback/volume",0);
        luma::MainWindow window;
        window.show();
        auto *mode=window.findChild<QComboBox*>("audioOutputMode");
        auto *profile=window.findChild<QComboBox*>("audioProfile");
        auto *volume=window.findChild<QSlider*>("volumeSlider");
        QVERIFY(mode && profile && volume);
        profile->setCurrentIndex(profile->findData(int(luma::AudioOutputOptions::Profile::Night)));
        window.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("audioCodecFormat").toString().isEmpty(),10000);
        QVERIFY2(window.diagnostics().value("audioSettingsError").toString().isEmpty(),qPrintable(window.diagnostics().value("audioSettingsError").toString()));
        QVERIFY(!window.diagnostics().value("audioBitstreamRequested").toBool());
        QVERIFY(volume->isEnabled());
        mode->setCurrentIndex(mode->findData("bitstream"));
        QVERIFY(!profile->isEnabled());
        window.openSource(media);
        QCOMPARE(window.diagnostics().value("audioBitstreamRequested").toBool(),true);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("audioBitstreamRequested").toBool() &&
                                 !window.diagnostics().value("audioCodecFormat").toString().isEmpty(),10000);
        const auto bitstream=window.diagnostics();
        QVERIFY2(bitstream.value("audioSettingsError").toString().isEmpty(),qPrintable(bitstream.value("audioSettingsError").toString()));
        QCOMPARE(volume->isEnabled(),!bitstream.value("audioBitstreamActive").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("positionMs").toInteger()>300,5000);
        qInfo().noquote()<<"Audio after bitstream request:"<<bitstream.value("audioOutput").toString()<<bitstream.value("audioCodecFormat").toString();
        mode->setCurrentIndex(mode->findData("system"));
        window.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(!window.diagnostics().value("audioCodecFormat").toString().isEmpty(),10000);
        QVERIFY(!window.diagnostics().value("audioBitstreamRequested").toBool());
        QVERIFY(!window.diagnostics().value("audioBitstreamActive").toBool());
        QVERIFY(volume->isEnabled());
    }
#ifdef Q_OS_WIN
    void automaticAiChoosesDetectedRadeonWithoutOverwritingSavedChoices(){
        luma::MainWindow window;
        auto *asr=window.findChild<luma::AiDeviceProbe*>();
        auto *translation=window.findChild<luma::TranslationDeviceProbe*>();
        auto *job=window.findChild<luma::SubtitleJob*>();
        auto *service=window.findChild<luma::TranslationService*>();
        QVERIFY(asr && translation && job && service);
        const auto gpu=[](const QString &id,const QString &name,const QString &type,const QString &backend){
            return QJsonObject{{"id",id},{"name",name},{"type",type},{"backend",backend}};
        };
        const auto intel=gpu("Vulkan0","Intel Arc","gpu","Vulkan");
        const auto radeon=gpu("Vulkan1","AMD Radeon RX fixture","gpu","Vulkan");
        asr->devicesReady(QJsonArray{intel,radeon});
        translation->devicesReady(QJsonArray{radeon,intel});
        QCOMPARE(job->preferences().gpuDevice,QString("Vulkan1"));
        QCOMPARE(service->preferences().gpuDevice,QString("Vulkan1"));
        QVERIFY(QSettings().value("ai/gpu").toString().isEmpty());
        QSettings().setValue("translation/gpu","Vulkan0");
        translation->devicesReady(QJsonArray{radeon,intel});
        QCOMPARE(service->preferences().gpuDevice,QString("Vulkan0"));
    }
    void radeonProfileUsesIndependentVulkanDevices(){
        QSettings settings;
        luma::MainWindow window;
        QVERIFY(answerConsent(window,"developerConsentDialog",true));
        auto *profile=window.findChild<QComboBox*>("hardwareTestProfile");
        auto *apply=window.findChild<QPushButton*>("applyHardwareTestProfile");
        auto *video=window.findChild<QComboBox*>("videoGpu");
        auto *asr=window.findChild<QComboBox*>("aiGpu");
        auto *translation=window.findChild<QComboBox*>("translationGpu");
        QVERIFY(profile && apply && video && asr && translation);
        video->clear();video->addItem("자동",QString{});video->addItem("AMD Radeon RX fixture","AMD Radeon RX fixture");
        asr->clear();asr->addItem("자동",QString{});asr->addItem("AMD Radeon RX fixture · Vulkan","Vulkan2");
        translation->clear();translation->addItem("자동",QString{});translation->addItem("Vulkan · AMD Radeon RX fixture","Vulkan0");
        translation->addItem("Vulkan · Intel Arc fixture","Vulkan2");

        profile->setCurrentIndex(profile->findData("radeon-all"));
        apply->click();
        QCOMPARE(settings.value("video/adapter").toString(),QString("AMD Radeon RX fixture"));
        QCOMPARE(settings.value("ai/gpu").toString(),QString("Vulkan2"));
        QCOMPARE(settings.value("translation/gpu").toString(),QString("Vulkan0"));
        QCOMPARE(settings.value("ai/mode").toString(),QString("gpu"));
        QCOMPARE(settings.value("translation/mode").toString(),QString("gpu"));

        profile->setCurrentIndex(profile->findData("cpu"));
        apply->click();
        QCOMPARE(settings.value("ai/mode").toString(),QString("cpu"));
        QCOMPARE(settings.value("translation/mode").toString(),QString("cpu"));
        QCOMPARE(settings.value("developer/developerHardware").toInt(),int(luma::VideoOverrides::Hardware::Software));

        translation->setCurrentIndex(0);
        translation->addItem("Vulkan · AMD Radeon 2 fixture","Vulkan1");
        profile->setCurrentIndex(profile->findData("radeon-translation"));
        apply->click();
        QCOMPARE(settings.value("translation/mode").toString(),QString("cpu"));
        translation->setCurrentIndex(translation->findData("Vulkan1"));
        apply->click();
        QCOMPARE(settings.value("translation/gpu").toString(),QString("Vulkan1"));
        QCOMPARE(settings.value("translation/mode").toString(),QString("gpu"));
        QCOMPARE(settings.value("ai/mode").toString(),QString("cpu"));
    }
#endif
    void developerVideoOverridesReachFileEngine(){
        const auto media=qEnvironmentVariable("LUMA_UI_TEST_PIP_MEDIA");
        if(QGuiApplication::platformName()!="windows" || !QFileInfo::exists(media))
            QSKIP("Requires native Windows output and a local H.264 fixture");
        restorePerformancePreferences();
        luma::MainWindow window;
        window.show();
        auto *hardware=window.findChild<QComboBox*>("developerHardware");
        auto *codec=window.findChild<QComboBox*>("developerCodec");
        auto *color=window.findChild<QComboBox*>("developerColor");
        auto *dolby=window.findChild<QComboBox*>("developerDolby");
        auto *reset=window.findChild<QPushButton*>("resetDeveloperVideo");
        QVERIFY(hardware && codec && color && dolby && reset);
        const auto select=[](auto *box, auto value){box->setCurrentIndex(box->findData(int(value)));};
        const auto snapshot=[&window]{
            const auto state=window.diagnostics();
            return QJsonObject{{"requestedDecoder",state.value("requestedVideoDecoder")},
                {"requestedTransfer",state.value("requestedVideoTransfer")},{"requestedDolbyVision",state.value("requestedDolbyVision")},
                {"inputTransfer",state.value("videoInputTransfer")},{"inputPrimaries",state.value("videoInputPrimaries")},
                {"outputTransfer",state.value("videoTransfer")},{"overrideStatus",state.value("videoOverrideStatus")},
                {"videoCodec",state.value("videoCodec")},{"videoDecoder",state.value("videoDecoder")}};
        };
        using Options=luma::VideoOverrides;
        select(hardware,Options::Hardware::Software);
        select(codec,Options::Codec::H264);
        select(color,Options::Color::SDR);
        select(dolby,Options::DolbyVision::Ignore);
        window.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("timelineReady").toBool(),10000);
        auto actual=snapshot();
        QVERIFY2(actual.value("overrideStatus").toString().isEmpty(),qPrintable(actual.value("overrideStatus").toString()));
        QCOMPARE(actual.value("requestedDecoder").toString(),QString("no / h264"));
        QCOMPARE(actual.value("requestedTransfer").toString(),QString("gamma2.2"));
        QCOMPARE(actual.value("requestedDolbyVision").toString(),QString("ignore"));
        QVERIFY(actual.value("videoCodec").toString().contains("H.264",Qt::CaseInsensitive));
        QTRY_VERIFY_WITH_TIMEOUT(!snapshot().value("outputTransfer").toString().isEmpty(),5000);
        const auto position=window.diagnostics().value("positionMs").toInteger();
        window.openSource("https://");
        QTRY_VERIFY(window.diagnostics().value("positionMs").toInteger()>position);

        select(color,Options::Color::HDR10);
        select(dolby,Options::DolbyVision::BaseLayer);
        const auto hdr=qEnvironmentVariable("LUMA_UI_TEST_HDR_MEDIA");
        const bool hdrFixture=QFileInfo::exists(hdr);
        select(codec,hdrFixture?Options::Codec::HEVC:Options::Codec::H264);
        window.openSource(hdrFixture?hdr:media);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("timelineReady").toBool(),10000);
        actual=snapshot();
        QVERIFY2(actual.value("overrideStatus").toString().isEmpty(),qPrintable(actual.value("overrideStatus").toString()));
        QCOMPARE(actual.value("requestedTransfer").toString(),QString("pq"));
        QCOMPARE(actual.value("requestedDolbyVision").toString(),QString("base-layer"));
        if(hdrFixture){
            QCOMPARE(actual.value("inputTransfer").toString(),QString("pq"));
            QCOMPARE(actual.value("inputPrimaries").toString(),QString("bt.2020"));
        }
        QTRY_VERIFY_WITH_TIMEOUT(!snapshot().value("outputTransfer").toString().isEmpty(),5000);
        qInfo().noquote()<<"HDR target and reported output:"<<actual.value("requestedTransfer").toString()<<snapshot().value("outputTransfer").toString();

        const auto av1=qEnvironmentVariable("LUMA_UI_TEST_AV1_MEDIA");
        if(QFileInfo::exists(av1)){
            select(codec,Options::Codec::Dav1d);
            select(color,Options::Color::SDR);
            select(dolby,Options::DolbyVision::Apply);
            window.openSource(av1);
            QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("timelineReady").toBool(),10000);
            actual=snapshot();
            QVERIFY2(actual.value("overrideStatus").toString().isEmpty(),qPrintable(actual.value("overrideStatus").toString()));
            QCOMPARE(actual.value("requestedDecoder").toString(),QString("no / libdav1d"));
            QTRY_COMPARE_WITH_TIMEOUT(snapshot().value("videoDecoder").toString(),QString("libdav1d"),5000);
        }

        reset->click();
        for(auto *box:{hardware,codec,color,dolby})QCOMPARE(box->currentIndex(),0);
        window.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("timelineReady").toBool(),10000);
        actual=snapshot();
        QVERIFY2(actual.value("overrideStatus").toString().isEmpty(),qPrintable(actual.value("overrideStatus").toString()));
        QCOMPARE(actual.value("requestedDecoder").toString(),QString("auto-safe"));
        QCOMPARE(actual.value("requestedTransfer").toString(),QString("auto"));
        QCOMPARE(actual.value("requestedDolbyVision").toString(),QString("auto"));

        for(const auto &[mode,request]:QList<QPair<Options::Hardware,QString>>{
                {Options::Hardware::D3D11Copy,"d3d11va-copy"},
                {Options::Hardware::DXVA2,"dxva2-copy"},
                {Options::Hardware::VulkanCopy,"vulkan-copy"}}){
            select(hardware,mode);
            window.openSource(media);
            QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("timelineReady").toBool(),10000);
            QTRY_COMPARE(snapshot().value("requestedDecoder").toString(),request);
            actual=snapshot();
            QVERIFY2(actual.value("overrideStatus").toString().isEmpty(),qPrintable(actual.value("overrideStatus").toString()));
            QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("positionMs").toInteger()>300,5000);
        }
    }
    void pictureInPictureKeepsVideoAndRestoresWindow(){
        const auto media=qEnvironmentVariable("LUMA_UI_TEST_PIP_MEDIA");
        if(QGuiApplication::platformName()!="windows" || !QFileInfo::exists(media))
            QSKIP("Requires the native Windows Qt backend and a local media fixture");
        restorePerformancePreferences();
        luma::MainWindow window;
        window.show();
        auto *surface=window.findChild<luma::VideoSurface*>("videoSurface");
        auto *pip=window.findChild<QPushButton*>("pictureInPicture");
        auto *overlay=window.findChild<QWidget*>("performanceOverlay");
        QVERIFY(surface && pip && overlay);
        QTRY_VERIFY(window.isVisible());
        window.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("positionMs").toInteger()>300,10000);
        const auto geometry=window.geometry();
        const auto videoId=surface->videoWindowId();
        const auto handle=reinterpret_cast<HWND>(window.winId());
        const auto position=window.diagnostics().value("positionMs").toInteger();
        QTest::keyClick(surface,Qt::Key_P,Qt::ControlModifier|Qt::ShiftModifier);
        QVERIFY(window.diagnostics().value("pictureInPicture").toBool());
        QTRY_VERIFY(GetWindowLongPtrW(handle,GWL_EXSTYLE)&WS_EX_TOPMOST);
        QVERIFY(window.width()<geometry.width());
        QCOMPARE(surface->videoWindowId(),videoId);
        QTRY_VERIFY(window.diagnostics().value("positionMs").toInteger()>position+300);
        QTRY_VERIFY(overlay->isVisible() && QRect(surface->mapToGlobal(QPoint()),surface->size()).contains(overlay->geometry()));
        QTest::keyClick(surface,Qt::Key_Escape);
        QVERIFY(!window.diagnostics().value("pictureInPicture").toBool());
        QTRY_VERIFY(!(GetWindowLongPtrW(handle,GWL_EXSTYLE)&WS_EX_TOPMOST));
        QCOMPARE(window.geometry(),geometry);
        QCOMPARE(surface->videoWindowId(),videoId);
    }
    void nativeDragMovesPlaybackWindowsBeforeReturning(){
        if(QGuiApplication::platformName()!="windows")QSKIP("Requires the native Windows Qt backend");
        BOOL fullDrag=FALSE;
        if(!SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS,0,&fullDrag,0)||!fullDrag)
            QSKIP("Requires Windows to show contents while dragging");
        restorePerformancePreferences();
        luma::MainWindow window;
        window.show();
        auto *surface=window.findChild<luma::VideoSurface*>("videoSurface");
        auto *caption=window.findChild<luma::CaptionOverlay*>("captionOverlay");
        auto *transport=window.findChild<QWidget*>("transport");
        auto *overlay=window.findChild<QWidget*>("performanceOverlay");
        QVERIFY(surface && caption && transport && overlay);
        surface->setVideoActive(true);
        caption->setCues({{0,1000,"Synthetic caption"}});
        caption->setPosition(200);
        QTRY_VERIFY(transport->isVisible());
        QTRY_VERIFY(overlay->isVisible());
        auto *video=surface->videoWindow();
        QVERIFY(video->isVisible() && caption->isVisible());
        const std::array<QWidget*,5> group{&window,video,caption,transport,overlay};
        std::array<RECT,5> before{};
        for(size_t i=0;i<group.size();++i)
            QVERIFY(GetWindowRect(reinterpret_cast<HWND>(group[i]->winId()),&before[i]));
        RECT target=before[0];
        OffsetRect(&target,37,29);
        SendMessageW(reinterpret_cast<HWND>(window.winId()),WM_MOVING,0,reinterpret_cast<LPARAM>(&target));
        for(size_t i=0;i<group.size();++i){
            RECT after{};
            QVERIFY(GetWindowRect(reinterpret_cast<HWND>(group[i]->winId()),&after));
            QCOMPARE(after.left-before[i].left,37L);
            QCOMPARE(after.top-before[i].top,29L);
            QCOMPARE(after.right-before[i].right,37L);
            QCOMPARE(after.bottom-before[i].bottom,29L);
        }
    }
#endif
    void discGlossaryIsSelectedOnlyForItsDisc(){
        const QString firstDisc(64,'a'),secondDisc(64,'b');
        QSettings settings;
        settings.setValue(luma::translationGlossarySettingsKey(),"青空 → 푸른 하늘");
        settings.setValue(luma::translationGlossarySettingsKey(firstDisc),"青空 → 파란 하늘");
        QCOMPARE(luma::configuredTranslationGlossary(firstDisc).value("青空"),QString("파란 하늘"));
        QCOMPARE(luma::configuredTranslationGlossary(secondDisc).value("青空"),QString("푸른 하늘"));
        QCOMPARE(luma::configuredTranslationGlossary().value("青空"),QString("푸른 하늘"));
        settings.setValue(luma::translationGlossarySettingsKey(firstDisc),QString{});
        QVERIFY(luma::configuredTranslationGlossary(firstDisc).isEmpty());
    }
    void captionAppearanceControlsRestoreTheirValues(){
        QString selectedFont;
        {
            luma::MainWindow window;
            auto *font=window.findChild<QComboBox*>("captionFont");
            auto *size=window.findChild<QSpinBox*>("captionSize");
            auto *text=window.findChild<QSpinBox*>("captionTextOpacity");
            auto *background=window.findChild<QSpinBox*>("captionBackgroundOpacity");
            QVERIFY(font && size && text && background);
            QVERIFY(font->count()>1);
            QCOMPARE(font->currentIndex(),0);
            QCOMPARE(size->value(),100);
            QCOMPARE(text->value(),100);
            QCOMPARE(background->value(),71);
            font->setCurrentIndex(1);
            selectedFont=font->currentData().toString();
            size->setValue(135);
            text->setValue(80);
            background->setValue(0);
        }
        luma::MainWindow restored;
        QCOMPARE(restored.findChild<QComboBox*>("captionFont")->currentData().toString(),selectedFont);
        QCOMPARE(restored.findChild<QSpinBox*>("captionSize")->value(),135);
        QCOMPARE(restored.findChild<QSpinBox*>("captionTextOpacity")->value(),80);
        QCOMPARE(restored.findChild<QSpinBox*>("captionBackgroundOpacity")->value(),0);
    }
    void preparationEntryDoesNotStartPlayback(){
        luma::MainWindow window;window.show();auto *prepare=window.findChild<QPushButton*>("prepareCaptions");QVERIFY(prepare);
        QVERIFY(!window.diagnostics().contains("engine"));bool sawDialog=false;
        QTimer::singleShot(0,&window,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);sawDialog=dialog->objectName()=="captionPreparationDialog";QVERIFY(sawDialog);QVERIFY(!window.diagnostics().contains("engine"));auto *play=dialog->findChild<QPushButton*>("preparePlay");QVERIFY(play);QVERIFY(!play->isEnabled());auto *close=dialog->findChild<QPushButton*>("prepareCancel");QVERIFY(close);close->click();});
        prepare->click();QVERIFY(sawDialog);QVERIFY(!window.diagnostics().contains("engine"));
    }
    void preparedNativePlaybackPreservesAiWithCompanionSubtitle(){
        const auto path=qEnvironmentVariable("LUMA_UI_PREPARATION_PROJECT");if(path.isEmpty())QSKIP("Set a completed owned-file preparation project for native UI validation.");
        QString error;const auto project=luma::CaptionPreparationService::loadProject(path,&error);QVERIFY2(project.has_value(),qPrintable(error));QVERIFY(!project->request.source.disc);
        const QFileInfo media(project->request.source.source);const auto companion=media.dir().filePath(media.completeBaseName()+".srt");if(QFileInfo::exists(companion))QSKIP("Use a fresh owned fixture without a companion subtitle.");
        QSaveFile file(companion);QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write("1\n00:00:00,000 --> 00:00:02,000\nOther companion\n")>0);QVERIFY(file.commit());const auto cleanup=qScopeGuard([companion]{QFile::remove(companion);});
        QSettings().setValue("subtitles/aiEnabled",true);QSettings().setValue("ai/model",project->request.model);QSettings().setValue("translation/model",project->request.translationModel);
        luma::MainWindow window;window.show();QVERIFY(window.loadPreparedCaptionProject(path));
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["preparedCaptionsApplied"].toBool(),20000);
        QVERIFY(window.diagnostics()["aiEnabled"].toBool());QVERIFY(QSettings().value("subtitles/aiEnabled").toBool());
        QCOMPARE(window.diagnostics()["translationCount"].toInt(),project->translations.size());QVERIFY(!window.diagnostics()["jobBusy"].toBool());
    }
    void physicalPreparedDiscOpensThePreparedPlaylist(){
        const auto path=qEnvironmentVariable("LUMA_UI_PREPARED_DISC_PROJECT");if(path.isEmpty())QSKIP("Set a completed private physical-disc project for selected-playlist validation.");
        QString error;const auto project=luma::CaptionPreparationService::loadProject(path,&error);QVERIFY2(project.has_value(),qPrintable(error));QVERIFY(project->request.source.disc);
        QSettings().setValue("subtitles/aiEnabled",true);QSettings().setValue("ai/model",project->request.model);QSettings().setValue("translation/model",project->request.translationModel);
        const bool verifyPerformance=qEnvironmentVariableIsSet("LUMA_UI_DISC_OVERLAY");
        if(verifyPerformance)restorePerformancePreferences();
        luma::MainWindow window;window.show();QVERIFY(window.loadPreparedCaptionProject(path));
        const auto report=qScopeGuard([&]{const auto output=qEnvironmentVariable("LUMA_UI_PREPARED_DISC_REPORT");if(!output.isEmpty()){QSaveFile file(output);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(window.diagnostics()).toJson());file.commit();}}});
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["preparedCaptionsApplied"].toBool(),25000);
        QCOMPARE(window.diagnostics()["durationMs"].toInteger(),project->request.durationMs);QVERIFY(!window.diagnostics()["navigationTitle"].toBool());QVERIFY(window.diagnostics().contains("discMenus"));QVERIFY(!window.diagnostics()["discMenus"].toBool());QCOMPARE(window.diagnostics()["translationCount"].toInt(),project->translations.size());QVERIFY(!window.diagnostics()["captionBufferHolding"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["playing"].toBool(),10000);const auto start=window.diagnostics()["positionMs"].toInteger();
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["positionMs"].toInteger()>=start+1000,5000);QVERIFY(window.diagnostics()["displayedPictures"].toInt()>0);
        if(verifyPerformance){
            const auto sample=[&window]{return window.diagnostics()["performanceOverlay"].toObject()["latest"].toObject();};
            const auto normalFps=[&sample]{const auto fps=sample().value("presentedFps").toDouble();return fps>=18 && fps<=30;};
            QTRY_VERIFY_WITH_TIMEOUT(normalFps(),12000);
            qInfo().noquote()<<QJsonDocument(sample()).toJson(QJsonDocument::Compact);
        }
        auto *surface=window.findChild<QWidget*>("videoSurface");QVERIFY(surface);surface->setFocus();const auto beforeSeek=window.diagnostics()["positionMs"].toInteger();QTest::keyClick(surface,Qt::Key_Right);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["positionMs"].toInteger()>=beforeSeek+8000,10000);QVERIFY(window.diagnostics()["preparedCaptionsApplied"].toBool());QVERIFY(!window.diagnostics()["captionBufferHolding"].toBool());
    }
    void preparedNativeSwitchKeepsItsProjectInHistory(){
        const auto path=qEnvironmentVariable("LUMA_UI_PREPARATION_PROJECT");if(path.isEmpty())QSKIP("Set a completed owned-file preparation project for native UI validation.");
        QString error;const auto project=luma::CaptionPreparationService::loadProject(path,&error);QVERIFY2(project.has_value(),qPrintable(error));
        QSettings().setValue("subtitles/aiEnabled",true);QSettings().setValue("ai/model",project->request.model);QSettings().setValue("translation/model",project->request.translationModel);
        luma::MainWindow window;window.show();QVERIFY(window.loadPreparedCaptionProject(path));QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["preparedCaptionsApplied"].toBool(),20000);
        auto *surface=window.findChild<QWidget*>("videoSurface");QVERIFY(surface);surface->setFocus();QTest::keyClick(surface,Qt::Key_Right);QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["positionMs"].toInteger()>=9000,10000);QTest::qWait(600);
        QTemporaryDir missing;window.openSource(missing.filePath("other-missing.mkv"));luma::ResumeHistory history;const auto saved=history.latest();QVERIFY(saved.has_value());QCOMPARE(saved->preparedProject,project->projectFile);
    }
    void preparedNativeChangedSourceRecoveryFailsWithoutCrash(){
        const auto path=qEnvironmentVariable("LUMA_UI_PREPARATION_PROJECT");if(path.isEmpty())QSKIP("Set a completed owned-file preparation project for native UI validation.");
        QString error;const auto project=luma::CaptionPreparationService::loadProject(path,&error);QVERIFY2(project.has_value(),qPrintable(error));
        QSettings().setValue("subtitles/aiEnabled",true);QSettings().setValue("ai/model",project->request.model);QSettings().setValue("translation/model",project->request.translationModel);
        luma::MainWindow window;window.show();QVERIFY(window.loadPreparedCaptionProject(path));QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["preparedCaptionsApplied"].toBool(),20000);
        const auto source=project->request.source.source;const auto original=QFileInfo(source).lastModified();QFile media(source);QVERIFY(media.open(QIODevice::ReadWrite));
        const auto restore=qScopeGuard([&]{media.setFileTime(original,QFileDevice::FileModificationTime);});QVERIFY(media.setFileTime(original.addSecs(2),QFileDevice::FileModificationTime));
        luma::PlaybackEngine *engine=nullptr;for(auto *candidate:window.findChildren<luma::PlaybackEngine*>())if(candidate->name().contains("mpv"))engine=candidate;QVERIFY(engine);QMetaObject::invokeMethod(engine,"videoOutputLost",Qt::DirectConnection,Q_ARG(QString,"Owned changed-source recovery fixture"));
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["videoRecoveryExhausted"].toBool(),5000);QVERIFY(!window.diagnostics().value("playing").toBool());
    }
    void preparedNativeRecoveryPreservesFrozenProject(){
        const auto path=qEnvironmentVariable("LUMA_UI_PREPARATION_PROJECT");if(path.isEmpty())QSKIP("Set a completed owned-file preparation project for native UI validation.");
        QString error;const auto project=luma::CaptionPreparationService::loadProject(path,&error);QVERIFY2(project.has_value(),qPrintable(error));QVERIFY(!project->request.source.disc);
        QSettings().setValue("subtitles/aiEnabled",true);QSettings().setValue("ai/model",project->request.model);QSettings().setValue("translation/model",project->request.translationModel);
        luma::MainWindow window;window.show();QVERIFY(window.loadPreparedCaptionProject(path));QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["preparedCaptionsApplied"].toBool(),20000);
        luma::PlaybackEngine *engine=nullptr;for(auto *candidate:window.findChildren<luma::PlaybackEngine*>())if(candidate->name().contains("mpv"))engine=candidate;QVERIFY(engine);
        QMetaObject::invokeMethod(engine,"videoOutputLost",Qt::DirectConnection,Q_ARG(QString,"Owned prepared recovery fixture"));
        QTRY_COMPARE_WITH_TIMEOUT(window.diagnostics()["videoRecoveryCount"].toInt(),1,5000);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["preparedCaptionsApplied"].toBool(),20000);QCOMPARE(window.diagnostics()["translationCount"].toInt(),project->translations.size());QVERIFY(window.diagnostics()["aiEnabled"].toBool());QVERIFY(!window.diagnostics()["captionBufferHolding"].toBool());
    }
    void preparedNativeSeekAndChapterDoNotWaitForLiveBuffer(){
        const auto path=qEnvironmentVariable("LUMA_UI_PREPARATION_PROJECT");if(path.isEmpty())QSKIP("Set a completed owned-file preparation project for native UI validation.");
        QString error;const auto project=luma::CaptionPreparationService::loadProject(path,&error);QVERIFY2(project.has_value(),qPrintable(error));QVERIFY(!project->request.source.disc);
        QSettings().setValue("subtitles/aiEnabled",true);QSettings().setValue("ai/model",project->request.model);QSettings().setValue("translation/model",project->request.translationModel);
        luma::MainWindow window;window.show();QVERIFY(window.loadPreparedCaptionProject(path));QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["preparedCaptionsApplied"].toBool(),20000);QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics()["playing"].toBool(),5000);
        const auto count=window.diagnostics()["translationCount"].toInt();auto *surface=window.findChild<QWidget*>("videoSurface");QVERIFY(surface);surface->setFocus();QTest::keyClick(surface,Qt::Key_Right);QTest::qWait(500);
        QVERIFY(!window.diagnostics()["captionBufferHolding"].toBool());QVERIFY(window.diagnostics()["aiEnabled"].toBool());QVERIFY(!window.diagnostics()["jobBusy"].toBool());QCOMPARE(window.diagnostics()["translationCount"].toInt(),count);
        if(window.diagnostics()["chapterCount"].toInt()>1){QPushButton *next=nullptr;for(auto *candidate:window.findChildren<QPushButton*>())if(candidate->accessibleName()=="다음 챕터"){next=candidate;break;}QVERIFY(next);next->click();QTest::qWait(500);QVERIFY(!window.diagnostics()["captionBufferHolding"].toBool());QCOMPARE(window.diagnostics()["translationCount"].toInt(),count);}
        QTemporaryDir missing;QVERIFY(!window.loadCaptionFile(missing.filePath("missing.srt")));QVERIFY(window.diagnostics()["aiEnabled"].toBool());QCOMPARE(window.diagnostics()["externalCueCount"].toInt(),0);QVERIFY(!window.diagnostics()["preparedCaptionsApplied"].toBool());
    }
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
    void discPlayQueuesPreparationAndAFileOpenCancelsIt(){
        luma::MainWindow window;
        QPushButton *discPlay=nullptr;
        for(auto *button:window.findChildren<QPushButton*>())if(button->text()=="Blu-ray 재생"){discPlay=button;break;}
        auto *status=window.findChild<QLabel*>("status");
        QVERIFY(discPlay);QVERIFY(status);
        discPlay->click();
        QVERIFY(!discPlay->isEnabled());
        QVERIFY(status->text().contains("Blu-ray 드라이브가 준비되기를 기다리는 중"));
        QTemporaryDir folder;QVERIFY(folder.isValid());
        window.openSource(folder.filePath("synthetic-cancel-disc.mkv"));
        QVERIFY(discPlay->isEnabled());
        QVERIFY(!status->text().contains("Blu-ray 드라이브가 준비되기를 기다리는 중"));
        discPlay->click();
        QVERIFY(!discPlay->isEnabled());
        QVERIFY(window.close());
        QVERIFY(discPlay->isEnabled());
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
        auto *tabs=window.findChild<QTabWidget*>("settingsTabs");QVERIFY(tabs);
        const auto settingsTab=tabs->count()-1;
        QVERIFY(capture->isHidden());
        for(int i=0;i<4;++i)tabs->tabBarClicked(settingsTab);
        QVERIFY(capture->isHidden());
        tabs->tabBarClicked(settingsTab);
        QVERIFY(!capture->isHidden());QVERIFY(!capture->isChecked());
        QVERIFY(answerConsent(window,"captureConsentDialog",false));QVERIFY(!capture->isChecked());
        QVERIFY(answerConsent(window,"captureConsentDialog",true));QVERIFY(capture->isChecked());
        {luma::MainWindow restored;QVERIFY(restored.findChild<QCheckBox*>("developerMode")->isChecked());QVERIFY(restored.findChild<QCheckBox*>("captureAllowed")->isChecked());}
        mode->setChecked(false);QVERIFY(!capture->isChecked());QVERIFY(!QSettings().value("privacy/captureAllowed").toBool());
        QVERIFY(answerConsent(window,"developerConsentDialog",true));QVERIFY(!capture->isChecked());QVERIFY(capture->isHidden());
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
    void realOwnedFileUsesSelectedIntelAdapter(){
        const auto media=qEnvironmentVariable("LUMA_UI_INTEL_VIDEO");
        const auto adapter=qEnvironmentVariable("LUMA_UI_INTEL_ADAPTER");
        if(media.isEmpty() || adapter.isEmpty())QSKIP("Set LUMA_UI_INTEL_VIDEO and LUMA_UI_INTEL_ADAPTER for native Intel decoding verification.");
        QSettings().setValue("video/adapter",adapter);
        luma::MainWindow window;
        auto *videoGpu=window.findChild<QComboBox*>("videoGpu");
        QVERIFY(videoGpu);
        QCOMPARE(videoGpu->currentData().toString(),adapter);
        window.show();
        window.openSource(media);
        QTRY_VERIFY_WITH_TIMEOUT(window.diagnostics().value("positionMs").toDouble()>=500,10000);
        const auto decoder=window.diagnostics().value("hardwareDecoder").toString();
        QVERIFY2(decoder.startsWith("d3d11va"),qPrintable(decoder));
        qInfo()<<"Adapter"<<adapter<<"decoder"<<decoder;
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
