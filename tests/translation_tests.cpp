#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include "subtitles/translation_support.h"
using namespace luma;

namespace {
QByteArray responseWithContent(const QString &content);
QByteArray responseWithKorean(const QString &korean);
}

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
    void rejectsDisplayCorruptionAndModelArtifacts_data(){
        QTest::addColumn<QString>("korean");
        QTest::newRow("replacement-character")<<QString::fromUtf8("안녕 �");
        for(const auto code:{0x00,0x01,0x07,0x0b,0x0c,0x1b,0x1f,0x7f,0x80,0x85,0x9b,0x9f,0x061c,0x200e,0x200f,0x202a,0x202b,0x202c,0x202d,0x202e,0x2066,0x2067,0x2068,0x2069}){
            const auto name=QString("control-%1").arg(code,4,16,QChar('0')).toLatin1();
            QTest::newRow(name.constData())<<(QString::fromUtf8("안녕")+QChar(static_cast<char16_t>(code))+QString::fromUtf8("세계"));
        }
        QTest::newRow("control-before-trimming")<<(QString(QChar(0x001f))+QString::fromUtf8("안녕"));
        for(const auto &marker:QStringList{"<think>","</think>","<ThInK>","</THINK>","< think >","</ analysis>","<reasoning>","<|im_start|>","<|im_end|>","[INST]","[/INST]","<<SYS>>"}){
            QTest::newRow(marker.toLatin1().constData())<<(QString::fromUtf8("안녕 ")+marker);
        }
        for(const auto &prefix:QStringList{"assistant:","USER:","System：","developer:","analysis:\n","final:\n","### Assistant:\n","assistant\n"}){
            QTest::newRow(prefix.toLatin1().constData())<<(prefix+QString::fromUtf8("안녕하세요."));
        }
        QTest::newRow("role-on-another-line")<<QString::fromUtf8("안녕하세요.\nassistant: 다음 답변");
        QTest::newRow("literal-unicode-escape")<<QString::fromUtf8("안녕\\uC138\\uC694");
        QTest::newRow("literal-newline-escape")<<QString::fromUtf8("안녕\\nAlice");
        QTest::newRow("literal-quoted-json")<<QString::fromUtf8("안녕 \\\"Alice\\\"");
    }
    void rejectsDisplayCorruptionAndModelArtifacts(){
        QFETCH(QString,korean);QString error;
        QVERIFY(parseTranslationResponse(responseWithKorean(korean),&error).isEmpty());QVERIFY(!error.isEmpty());
        QVERIFY(parseTranslationResponse(responseWithContent(korean),&error,TranslationProfile::HyMt2Plain).isEmpty());QVERIFY(!error.isEmpty());
    }
    void rejectsUnpairedSurrogatesBeforeUtf8Conversion_data(){
        QTest::addColumn<QByteArray>("response");
        QTest::newRow("escaped-high-surrogate")<<responseWithContent(QString::fromUtf8("{\"korean\":\"안녕\\uD800\"}"));
        QTest::newRow("escaped-low-surrogate")<<responseWithContent(QString::fromUtf8("{\"korean\":\"안녕\\uDC00\"}"));
        QTest::newRow("outer-high-surrogate")<<QByteArray("{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"{\\\"korean\\\":\\\"안녕\\ud800\\\"}\"}}]}");
        QTest::newRow("outer-low-surrogate")<<QByteArray("{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",\"content\":\"{\\\"korean\\\":\\\"안녕\\udc00\\\"}\"}}]}");
    }
    void rejectsUnpairedSurrogatesBeforeUtf8Conversion(){
        QFETCH(QByteArray,response);QString error;
        QVERIFY(parseTranslationResponse(response,&error).isEmpty());QVERIFY(!error.isEmpty());
    }
    void preservesValidNamesMusicCombiningCharactersAndJsonEscapes_data(){
        QTest::addColumn<QString>("korean");
        QTest::newRow("English-names")<<QString::fromUtf8("Alice와 Bob이 Final Fantasy를 해요. ThinkPad도 써요.");
        QTest::newRow("Han-proper-name")<<QString::fromUtf8("王小明 씨, 안녕하세요. Hello, Alice!");
        QTest::newRow("music-emoji-ZWJ-combining")<<QString::fromUtf8("함께 노래해요 ♪ ♫ ♬ 🎵 👩‍🎤 Cafe\u0301");
        QTest::newRow("decomposed-Hangul")<<QString::fromUtf8("안녕, Alice.");
        QTest::newRow("ordinary-quotes-and-backslash")<<QString::fromUtf8("Alice가 \"안녕\"이라고 해요. C:\\Users\\Alice");
    }
    void preservesValidNamesMusicCombiningCharactersAndJsonEscapes(){
        QFETCH(QString,korean);QString error;
        QCOMPARE(parseTranslationResponse(responseWithKorean(korean),&error),korean);QVERIFY(error.isEmpty());
        QCOMPARE(parseTranslationResponse(responseWithContent(korean),&error,TranslationProfile::HyMt2Plain),korean);QVERIFY(error.isEmpty());
    }
    void decodesNormalJsonUnicodeEscapesExactlyOnce(){
        const auto content=QString::fromLatin1("{\"korean\":\"\\uC548\\uB155\\uD558\\uC138\\uC694.\\nAlice \\\"Hi\\\" \\u266b \\uD83C\\uDFB5\"}");
        QString error;QCOMPARE(parseTranslationResponse(responseWithContent(content),&error),QString::fromUtf8("안녕하세요.\nAlice \"Hi\" ♫ 🎵"));QVERIFY(error.isEmpty());
    }
    void normalizesOnlyUnambiguousCaptionWhitespace(){
        QString error;
        QCOMPARE(parseTranslationResponse(responseWithKorean(QString::fromUtf8("\t 안녕\tAlice\r\n노래해요.\r마지막 \t")),&error),QString::fromUtf8("안녕 Alice\n노래해요.\n마지막"));QVERIFY(error.isEmpty());
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
    void onlyKnownDedicatedModelUsesPlainProfile(){
        QCOMPARE(translationProfileForModel("C:/models/Hy-MT2-7B-Q4_K_M.gguf"),TranslationProfile::HyMt2Plain);
        for(const auto &name:QStringList{"Qwen3-4B-Q4_K_M.gguf","custom.gguf","Hy-MT2-7B-unknown.gguf","Hy-MT2-1.8B-Q8_0.gguf"})
            QCOMPARE(translationProfileForModel(name),TranslationProfile::LegacyQwenJson);
    }
    void dedicatedModelUsesOfficialPlainTranslationInstruction(){
        const QString source=QString::fromUtf8("\"日本語\"\nこれは翻訳する原文です。");
        const auto payload=translationPayload(source,TranslationProfile::HyMt2Plain);
        const auto messages=payload.value("messages").toArray();QCOMPARE(messages.size(),1);
        QCOMPARE(messages.first().toObject().value("role").toString(),QString("user"));
        QCOMPARE(messages.first().toObject().value("content").toString(),QString("Translate the following text into Korean. Note that you should only output the translated result without any additional explanation:\n\n")+source);
        QVERIFY(!payload.contains("response_format"));QVERIFY(!payload.value("stream").toBool(true));
        QCOMPARE(payload.value("temperature").toDouble(),0.7);QCOMPARE(payload.value("top_p").toDouble(),0.6);
        QCOMPARE(payload.value("top_k").toInt(),20);QCOMPARE(payload.value("repeat_penalty").toDouble(),1.05);
        QCOMPARE(payload.value("seed").toInt(),42);QCOMPARE(payload.value("max_tokens").toInt(),512);
    }
    void plainOutputDecodesOnlyWireJsonAndDoesNotGuessOtherFormats(){
        QString error;const auto plain=QString::fromUtf8("가지 않아도 돼.\nAlice가 \"안녕\"이라고 해요. ♪");
        QCOMPARE(parseTranslationResponse(responseWithContent(plain),&error,TranslationProfile::HyMt2Plain),plain);QVERIFY(error.isEmpty());
        QVERIFY(parseTranslationResponse(responseWithContent(plain),&error).isEmpty());
        for(const auto &text:QStringList{QString(),QString::fromUtf8("日本語です。"),QString::fromUtf8("{\"korean\":\"안녕\"}"),QString::fromUtf8("```\n안녕\n```"),QString::fromUtf8("안녕\\uC138\\uC694")}){
            QVERIFY(parseTranslationResponse(responseWithContent(text),&error,TranslationProfile::HyMt2Plain).isEmpty());QVERIFY(!error.isEmpty());
        }
        const auto truncated=QByteArray("{\"choices\":[{\"finish_reason\":\"length\",\"message\":{\"role\":\"assistant\",\"content\":\"안녕\"}}]}");
        QVERIFY(parseTranslationResponse(truncated,&error,TranslationProfile::HyMt2Plain).isEmpty());
    }
};
namespace {
QByteArray responseWithContent(const QString &content){
    return QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{
        {"finish_reason","stop"},{"message",QJsonObject{{"role","assistant"},{"content",content}}}}}}}).toJson(QJsonDocument::Compact);
}
QByteArray responseWithKorean(const QString &korean){
    return responseWithContent(QString::fromUtf8(QJsonDocument(QJsonObject{{"korean",korean}}).toJson(QJsonDocument::Compact)));
}
}
QTEST_GUILESS_MAIN(TranslationTests)
#include "translation_tests.moc"
