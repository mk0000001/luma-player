#include <QtTest>
#include <QTemporaryDir>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include "ui/caption_preparation_dialog.h"
#include "subtitles/translation_support.h"
using namespace luma;
class CaptionPreparationDialogTests:public QObject {
    Q_OBJECT
    CaptionPreparationResult complete(QTemporaryDir &root){
        CaptionPreparationResult result;result.request.outputDirectory=root.path();result.request.durationMs=2000;result.request.translate=true;result.originals={{0,2000,"Synthetic original"}};result.translations.insert(translationCueKey(result.originals[0]),"합성 해석");result.projectFile=root.filePath("project.json");return result;
    }
    static void put(const QString &path,const QByteArray &bytes){QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),bytes.size());}
    static QByteArray read(const QString &path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};}
    CaptionPreparationRequest fixture(QTemporaryDir &root){
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        CaptionPreparationRequest request;request.model=root.filePath("model.bin");put(request.model,"synthetic model");request.translate=false;request.source.source=root.filePath("owned.mkv");put(request.source.source,"retry-gated");
        for(const auto &folder:{"tools","whisper","ai"})if(!QDir(root.path()).mkpath(folder)){QTest::qFail("Cannot create fixture runtime",__FILE__,__LINE__);return {};}
        const auto binary=QCoreApplication::applicationDirPath()+"/caption_preparation_fixture"+suffix;
        for(const auto &tool:{"tools/ffmpeg","whisper/whisper-cli","ai/luma-whisper-worker"})if(!QFile::copy(binary,root.filePath(QString(tool)+suffix))){QTest::qFail("Cannot copy CPU-only fixture",__FILE__,__LINE__);return {};}
        qputenv("LUMA_PREPARATION_FIXTURE_LOG",QFile::encodeName(root.filePath("events.jsonl")));qputenv("LUMA_PREPARATION_FIXTURE_GATE",QFile::encodeName(root.filePath("release")));return request;
    }
private slots:
    void init(){QTest::failOnWarning(QRegularExpression("QProcess: Destroyed while process.*|QTemporaryDir: Unable to remove.*"));}
    void stoppedGenerationCannotPublishCompletion_data(){QTest::addColumn<bool>("cancel");QTest::newRow("canceled")<<true;QTest::newRow("failed")<<false;}
    void stoppedGenerationCannotPublishCompletion(){
        QFETCH(bool,cancel);QTemporaryDir root;CaptionPreparationDialog dialog({}, {}, {},nullptr,root.path());auto *service=dialog.findChild<CaptionPreparationService*>();QVERIFY(service);const auto old=service->generation();
        if(cancel)service->canceled(old);else service->failed(old,"Synthetic runtime failure");
        service->progress(old,{"recognition",65000,65000,0,0,"GPU",{},"stale progress",true});service->ready(old,complete(root));
        QVERIFY(!dialog.prepared());QVERIFY(!dialog.findChild<QPushButton*>("preparePlay")->isEnabled());
    }
    void acceleratorRetryPreservesCheckpointAndRejectsOldEvents(){
        QTemporaryDir root;const auto request=fixture(root);const auto cleanup=qScopeGuard([]{qunsetenv("LUMA_PREPARATION_FIXTURE_LOG");qunsetenv("LUMA_PREPARATION_FIXTURE_GATE");});
        CaptionPreparationDialog dialog(request,{},request.source.source,nullptr,root.path());auto *start=dialog.findChild<QPushButton*>("prepareStart");auto *retry=dialog.findChild<QPushButton*>("prepareRetryAccelerator");auto *service=dialog.findChild<CaptionPreparationService*>();QVERIFY(start);QVERIFY(retry);QVERIFY(service);
        QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(),4000);dialog.findChild<QLineEdit*>("prepareOutputDirectory")->setText(root.filePath("captions"));start->click();
        QTRY_VERIFY_WITH_TIMEOUT(dialog.diagnostics()["recognizedMs"].toInteger()==30000&&retry->isEnabled(),6000);
        auto *status=dialog.findChild<QLabel*>("preparationStatus");QVERIFY(status);QVERIFY(status->text().contains("GPU 사용 불가 (처리 실패)"));
        QPointer<SubtitleJob> oldChild=service->findChild<SubtitleJob*>();QVERIFY(oldChild);QTRY_VERIFY_WITH_TIMEOUT(oldChild->currentRequest().startMs==30000&&oldChild->busy(),3000);
        QTRY_VERIFY_WITH_TIMEOUT(read(root.filePath("events.jsonl")).contains("\"event\":\"cpu-waiting\""),4000);
        const auto project=root.filePath("captions/luma-caption-project.json");const auto before=QJsonDocument::fromJson(read(project)).object();const auto old=service->generation();QSignalSpy ready(service,&CaptionPreparationService::ready),failed(service,&CaptionPreparationService::failed);
        retry->click();QVERIFY(service->generation()>old);QVERIFY(dialog.diagnostics()["busy"].toBool());QVERIFY(!retry->isEnabled());
        if(oldChild){oldChild->progress("GPU 사용 불가 (시간 초과) · 다음 장치로 전환 중…");oldChild->failed("stale child error");}
        service->progress(old,{"recognition",65000,65000,0,0,"GPU",{},"stale progress",true});service->failed(old,"stale failure");service->canceled(old);service->ready(old,complete(root));
        QVERIFY(dialog.diagnostics()["busy"].toBool());QVERIFY(!dialog.prepared());QVERIFY(!status->text().contains("stale"));
        QCOMPARE(ready.count(),1);QCOMPARE(failed.count(),1);ready.clear();failed.clear();
        put(root.filePath("release"),"continue");QTRY_VERIFY_WITH_TIMEOUT(dialog.prepared().has_value(),10000);QCOMPARE(ready.count(),1);QCOMPARE(failed.count(),0);QVERIFY(!dialog.diagnostics()["busy"].toBool());
        const auto after=QJsonDocument::fromJson(read(project)).object();QCOMPARE(after["request"],before["request"]);QCOMPARE(after["job_identity"],before["job_identity"]);QCOMPARE(after["source_identity"],before["source_identity"]);QCOMPARE(dialog.prepared()->originals.size(),3);
        QVector<qint64> starts;int gpuAttempts=0;for(const auto &line:read(root.filePath("events.jsonl")).split('\n')){const auto event=QJsonDocument::fromJson(line).object();if(event["event"]=="extract")starts.append(event["start_ms"].toInteger());if(event["event"]=="gpu-rejected")++gpuAttempts;}
        QCOMPARE(starts,QVector<qint64>({0,30000,30000,60000}));QCOMPARE(gpuAttempts,2);
    }
    void canceledDeviceLabelIdentifiesTheLastUsedDevice(){
        QTemporaryDir root;CaptionPreparationDialog dialog({}, {}, {},nullptr,root.path());auto *service=dialog.findChild<CaptionPreparationService*>();QVERIFY(service);
        service->progress(service->generation(),{"recognition",30000,65000,0,0,"CPU",{}});service->canceled(service->generation());
        bool historical=false;for(auto *label:dialog.findChildren<QLabel*>())if(label->text().contains("CPU")&&label->text().contains("마지막 사용 장치"))historical=true;
        QVERIFY(historical);
    }
    void acceleratorRetryHasAnExplicitPreparationAction(){
        QTemporaryDir root;CaptionPreparationDialog dialog({}, {}, {},nullptr,root.path());auto *retry=dialog.findChild<QPushButton*>("prepareRetryAccelerator");
        QVERIFY(retry);QVERIFY(!retry->isEnabled());
    }
    void changedGenerationInputsInvalidateCompletedPlayback_data(){QTest::addColumn<QString>("control");QTest::newRow("audio")<<QString("audio");QTest::newRow("translation")<<QString("translation");QTest::newRow("output")<<QString("output");}
    void changedGenerationInputsInvalidateCompletedPlayback(){
        QFETCH(QString,control);QTemporaryDir root;CaptionPreparationRequest defaults;defaults.translate=true;CaptionPreparationDialog dialog(defaults,{}, {},nullptr,root.path());
        auto *service=dialog.findChild<CaptionPreparationService*>();auto *play=dialog.findChild<QPushButton*>("preparePlay");auto *audio=dialog.findChild<QComboBox*>("prepareFileAudio");QVERIFY(service);QVERIFY(play);QVERIFY(audio);audio->addItems({"Japanese","English"});
        service->ready(service->generation(),complete(root));QVERIFY(play->isEnabled());QVERIFY(dialog.prepared());QVERIFY(QFileInfo::exists(root.filePath("display.srt")));
        if(control=="audio")audio->setCurrentIndex(1);else if(control=="translation")dialog.findChild<QCheckBox*>("prepareKorean")->setChecked(false);else dialog.findChild<QLineEdit*>("prepareOutputDirectory")->setText(root.filePath("different"));
        QVERIFY(!play->isEnabled());QVERIFY(!dialog.prepared());QVERIFY(QFileInfo::exists(root.filePath("display.srt")));
        service->ready(service->generation(),complete(root));QVERIFY(!dialog.prepared());QVERIFY(!play->isEnabled());
    }
    void staleCompletionCannotEnablePlayback(){QTemporaryDir root;CaptionPreparationDialog dialog({}, {}, {},nullptr,root.path());auto *service=dialog.findChild<CaptionPreparationService*>();QVERIFY(service);service->ready(service->generation()+99,complete(root));QVERIFY(!dialog.prepared());QVERIFY(!dialog.findChild<QPushButton*>("preparePlay")->isEnabled());}
};
QTEST_MAIN(CaptionPreparationDialogTests)
#include "caption_preparation_dialog_tests.moc"
