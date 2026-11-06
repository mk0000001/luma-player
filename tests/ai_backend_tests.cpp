#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include "subtitles/ai_backend.h"
using namespace luma;
class AiBackendTests:public QObject {
    Q_OBJECT
private slots:
    void orderedAcceleratorsRequireEncoder(){
        QTemporaryDir root;QDir(root.path()).mkpath("ai");QDir(root.path()).mkpath("models");
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        const auto model=root.filePath("models/ggml-base.bin");
        auto plans=recognitionBackends(root.path(),model);QCOMPARE(plans.size(),1);QCOMPARE(plans[0].key,QString("CPU"));
        for(const auto &path:{root.filePath("ai/luma-whisper-worker"+suffix),root.filePath("models/ggml-base-encoder-openvino.xml"),root.filePath("models/ggml-base-encoder-openvino.bin")}){QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");}
        plans=recognitionBackends(root.path(),model);QCOMPARE(plans.size(),3);QCOMPARE(plans[0].key,QString("NPU"));QCOMPARE(plans[1].key,QString("GPU"));QCOMPARE(plans[2].key,QString("CPU"));
        plans=recognitionBackends(root.path(),root.filePath("custom.bin"));QCOMPARE(plans.size(),2);QCOMPARE(plans[0].key,QString("GPU"));
        plans=recognitionBackends(root.path(),model,{"gpu","Vulkan4"});QCOMPARE(plans.size(),2);QCOMPARE(plans[0].key,QString("GPU"));QCOMPARE(plans[0].device,QString("Vulkan4"));
        plans=recognitionBackends(root.path(),model,{"cpu",{}});QCOMPARE(plans.size(),1);QCOMPARE(plans[0].key,QString("CPU"));
        plans=recognitionBackends(root.path(),model,{"npu",{}});QCOMPARE(plans.size(),3);QCOMPARE(plans[0].key,QString("NPU"));QCOMPARE(plans[1].key,QString("GPU"));QCOMPARE(plans[2].key,QString("CPU"));
    }
    void kotobaRequiresMatchingEncoderMetadata(){
        QTemporaryDir root;QVERIFY(QDir(root.path()).mkpath("ai"));
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        const auto model=root.filePath("ggml-kotoba-whisper-v2.0-q5_0.bin");
        const auto stem=root.filePath("ggml-kotoba-whisper-v2.0-q5_0-encoder-openvino");
        for(const auto &path:{root.filePath("ai/luma-whisper-worker"+suffix),stem+".xml",stem+".bin"}){QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("fixture"),7);}
        QFile source(model);QVERIFY(source.open(QIODevice::WriteOnly));QVERIFY(source.resize(537819875));source.close();
        // Metadata only selects a candidate. The isolated worker verifies all
        // SHA-256 values before loading or acknowledging accelerated inference.
        const auto candidates=[&]{return recognitionBackends(root.path(),model,{"npu",{}});};
        auto plans=candidates();QCOMPARE(plans[0].key,QString("GPU"));QCOMPARE(plans.size(),2);
        QJsonObject metadata{{"format","luma-whisper-encoder-v1"},{"sourceModelSha256","4a3b92192b5d3578ff854a5876213e2e27af0c2d357492c2d14271e82c303658"},{"sourceModelBytes",537819875},
            {"encoderXmlBytes",7},{"encoderBinBytes",7},{"encoderXmlSha256",QString(64,'a')},{"encoderBinSha256",QString(64,'b')},
            {"inputName","mel"},{"inputShape",QJsonArray{1,128,3000}},{"outputName","output_features"},{"outputShape",QJsonArray{1,1500,1280}}};
        const auto write=[&]{QFile file(stem+".json");if(!file.open(QIODevice::WriteOnly))return false;return file.write(QJsonDocument(metadata).toJson())>0;};
        QVERIFY(write());plans=candidates();QCOMPARE(plans[0].key,QString("NPU"));QCOMPARE(plans.size(),3);
        metadata["sourceModelSha256"]=QString(64,'0');QVERIFY(write());QCOMPARE(candidates()[0].key,QString("GPU"));
        metadata["sourceModelSha256"]="4a3b92192b5d3578ff854a5876213e2e27af0c2d357492c2d14271e82c303658";
        metadata["inputShape"]=QJsonArray{1,80,3000};QVERIFY(write());QCOMPARE(candidates()[0].key,QString("GPU"));
        metadata["inputShape"]=QJsonArray{1,128,3000};metadata["encoderBinBytes"]=8;QVERIFY(write());QCOMPARE(candidates()[0].key,QString("GPU"));
        metadata["encoderBinBytes"]=7;QVERIFY(write());QVERIFY(source.open(QIODevice::WriteOnly));QVERIFY(source.resize(5));source.close();QCOMPARE(candidates()[0].key,QString("GPU"));
    }
    void actualInferenceProofRequired(){
        AiBackend npu{"NPU",{},"NPU",{}};AiBackend gpu{"GPU",{},"GPU",{}};AiBackend cpu{"CPU",{},{},{}};
        QVERIFY(!confirmsBackend(npu,"device=NPU\nfalling back to CPU\n"));
        QVERIFY(!confirmsBackend(npu,"LUMA_BACKEND=GPU\n"));
        QVERIFY(confirmsBackend(npu,"model loaded\nLUMA_BACKEND=NPU\n"));
        QVERIFY(confirmsBackend(gpu,"LUMA_BACKEND=GPU\n"));
        QVERIFY(confirmsBackend(cpu,{}));
    }
};
QTEST_GUILESS_MAIN(AiBackendTests)
#include "ai_backend_tests.moc"
