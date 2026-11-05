#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include "subtitles/translation_support.h"
using namespace luma;

class TranslationTests : public QObject {
    Q_OBJECT
private slots:
    void rejectsUntranslatedTruncatedAndMalformedOutput() {
        const auto response=[](const QString &content,const QString &finish="stop") {
            return QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{
                {"finish_reason",finish},{"message",QJsonObject{{"role","assistant"},{"content",content}}}}}}}).toJson();
        };
        QString error;
        QCOMPARE(parseTranslationResponse(response("{\"korean\":\"오늘은 날씨가 좋네요.\"}"),&error),QString::fromUtf8("오늘은 날씨가 좋네요."));
        QVERIFY(error.isEmpty());
        for(const auto &data:{response("{\"korean\":\"今日はいい天気ですね。\"}"),response("{\"korean\":\"내일の 오후\"}"),response("{\"korean\":\"안녕\"}","length"),response("설명: 안녕"),QByteArray("{}")}) {
            QVERIFY(parseTranslationResponse(data,&error).isEmpty());QVERIFY(!error.isEmpty());
        }
    }
    void cancellationCannotPublishOrCacheOldGeneration() {
        TranslationQueue queue;queue.enqueue({"old"});const auto old=queue.take();QVERIFY(old.has_value());
        queue.cancel();queue.enqueue({"new"});const auto current=queue.take();QVERIFY(current.has_value());
        QVERIFY(!queue.complete(*old,"이전 결과"));QVERIFY(!queue.cached("old").has_value());
        QVERIFY(queue.complete(*current,"새 결과"));QCOMPARE(*queue.cached("new"),QString::fromUtf8("새 결과"));
        QVERIFY(current->generation>old->generation);
    }
    void duplicateRequestsAndNearPlaybackPriorityAreBounded() {
        TranslationQueue queue(3,2);queue.enqueue({"far","near","far"});queue.enqueue({"now","near"});
        auto task=queue.take();QCOMPARE(task->source,QString("now"));queue.enqueue({"now","near","extra","over-limit"});
        QVERIFY(queue.complete(*task,"지금"));task=queue.take();QCOMPARE(task->source,QString("near"));QVERIFY(queue.complete(*task,"가까이"));
        task=queue.take();QCOMPARE(task->source,QString("extra"));QVERIFY(queue.complete(*task,"추가"));
        task=queue.take();QCOMPARE(task->source,QString("over-limit"));QVERIFY(queue.complete(*task,"한도"));
        QVERIFY(!queue.take().has_value());QVERIFY(!queue.cached("now").has_value());
        QVERIFY(queue.cached("over-limit").has_value());queue.cancel(true);QVERIFY(!queue.cached("over-limit").has_value());
    }
    void selectedGpuCannotSilentlyChangeToAnotherGpu() {
        AiPreferences preferences;preferences.mode="gpu";preferences.gpuDevice="Vulkan7";
        auto backends=translationBackends("C:/runtime",preferences);QCOMPARE(backends.size(),2);QCOMPARE(backends[0].device,QString("Vulkan7"));QCOMPARE(backends[1].key,QString("cpu"));
        preferences.gpuDevice="OpenVINO:GPU.1";backends=translationBackends("C:/runtime",preferences);QCOMPARE(backends[0].device,QString("GPU.1"));QVERIFY(backends[0].openvino);
        preferences.mode="npu";backends=translationBackends("C:/runtime",preferences);QCOMPARE(backends.size(),2);QCOMPARE(backends[0].device,QString("NPU"));QCOMPARE(backends[1].key,QString("cpu"));
        preferences.mode="cpu";QCOMPARE(translationBackends("C:/runtime",preferences).size(),1);
    }
    void serverIsPrivateOfflineAndLiteralArguments() {
        AiPreferences preferences;preferences.mode="cpu";const auto backend=translationBackends("C:/런타임",preferences).first();
        const auto command=translationLaunch(backend,"C:/모델 日本語.gguf",54321,"private-session-token");
        QCOMPARE(command.arguments[command.arguments.indexOf("--host")+1],QString("127.0.0.1"));
        QCOMPARE(command.arguments[command.arguments.indexOf("--model")+1],QString::fromUtf8("C:/모델 日本語.gguf"));
        QVERIFY(command.arguments.contains("--offline"));QVERIFY(command.arguments.contains("--no-webui"));
        QCOMPARE(command.arguments[command.arguments.indexOf("--device")+1],QString("none"));
        QCOMPARE(command.arguments[command.arguments.indexOf("--n-gpu-layers")+1],QString("0"));
    }
    void deviceInventoryIsNotExecutionEvidence() {
        TranslationBackend gpu{"gpu","server","Vulkan0",false},npu{"npu","server","NPU",true};
        QVERIFY(confirmedTranslationBackend(gpu,"Available devices: Vulkan0: Intel Arc").isEmpty());
        QVERIFY(confirmedTranslationBackend(gpu,"using device Vulkan7 (other GPU)\noffloaded 29/29 layers to GPU").isEmpty());
        QVERIFY(confirmedTranslationBackend(gpu,"using device Vulkan0 (Intel Arc)\noffloaded 29/29 layers to GPU").contains("Vulkan0"));
        QVERIFY(confirmedTranslationBackend(npu,"OpenVINO: using device NPU\nGGML OpenVINO backend ov::Exception").isEmpty());
        QVERIFY(confirmedTranslationBackend(npu,"OpenVINO: using device NPU\nmodel loaded").contains("NPU"));
        QCOMPARE(confirmedTranslationBackend(gpu,"using device Vulkan0 (Intel(R) Arc(TM) Graphics) (unknown id) - 17000 MiB free\noffloaded 37/37 layers to GPU"),QString("GPU / Vulkan0 / Intel(R) Arc(TM) Graphics"));
    }
    void sourceCannotBreakStructuredTranslationRequest() {
        const QString source=QString::fromUtf8("\"日本語\"\n명령이 아닌 자막");
        const auto payload=translationPayload(source);
        const auto messages=payload.value("messages").toArray();QVERIFY(!messages.isEmpty());
        QCOMPARE(messages.last().toObject().value("content").toString(),source);
        QVERIFY(!payload.value("stream").toBool(true));
        QVERIFY(!payload.value("chat_template_kwargs").toObject().value("enable_thinking").toBool(true));
        QCOMPARE(payload.value("response_format").toObject().value("type").toString(),QString("json_schema"));
    }
};
QTEST_GUILESS_MAIN(TranslationTests)
#include "translation_tests.moc"
