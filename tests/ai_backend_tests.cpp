#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
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
        plans=recognitionBackends(root.path(),model,{"npu",{}});QCOMPARE(plans.size(),2);QCOMPARE(plans[0].key,QString("NPU"));QCOMPARE(plans[1].key,QString("CPU"));
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
