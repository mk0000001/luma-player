#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "subtitles/translation_support.h"
#include "subtitles/translation_devices.h"
using namespace luma;

class TranslationDeviceTests : public QObject {
    Q_OBJECT
    static void runtimeFile(const QString &root,const QString &backend){
        const auto path=QDir(root).filePath("translation/"+backend);QVERIFY(QDir().mkpath(path));
#ifdef Q_OS_WIN
        QFile file(path+"/llama-server.exe");
#else
        QFile file(path+"/llama-server");
#endif
        QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");
    }
private slots:
    void automaticUsesInstalledCudaBeforeVulkan(){
        QTemporaryDir root;QVERIFY(root.isValid());runtimeFile(root.path(),"cuda");runtimeFile(root.path(),"vulkan");
        const auto backends=translationBackends(root.path());
        QCOMPARE(backends.size(),3);QCOMPARE(backends[0].key,QString("cuda"));QCOMPARE(backends[0].device,QString("CUDA0"));
        QCOMPARE(backends[1].device,QString("Vulkan0"));QCOMPARE(backends[2].key,QString("cpu"));
        runtimeFile(root.path(),"openvino");const auto withNpu=translationBackends(root.path());
        QCOMPARE(withNpu.size(),4);QCOMPARE(withNpu[0].key,QString("npu"));QCOMPARE(withNpu[1].key,QString("cuda"));
    }
    void explicitCudaNeverFallsThroughToAnotherGpu(){
        const auto backends=translationBackends("C:/runtime",{"gpu","CUDA2"});
        QCOMPARE(backends.size(),2);QCOMPARE(backends[0].key,QString("cuda"));QCOMPARE(backends[0].device,QString("CUDA2"));
        QVERIFY(backends[0].program.contains("/cuda/"));QCOMPARE(backends[1].key,QString("cpu"));
    }
    void cudaLaunchPinsOneExactDevice(){
        QTemporaryDir root;QVERIFY(root.isValid());const auto directory=QDir(root.path()).filePath("translation/cuda");
        const auto launch=translationLaunch({"cuda",directory+"/llama-server","CUDA2",false},"owned.gguf",12345,"fixture-token");
        const auto device=launch.arguments.indexOf("--device"),split=launch.arguments.indexOf("--split-mode");
        QVERIFY(device>=0);QCOMPARE(launch.arguments[device+1],QString("CUDA2"));QVERIFY(split>=0);QCOMPARE(launch.arguments[split+1],QString("none"));
        QCOMPARE(launch.environment.value("GGML_BACKEND_PATH"),directory);
    }
    void cudaConfirmationNeedsSelectedDeviceAndActualOffload(){
        const TranslationBackend cuda{"cuda","server","CUDA2",false};
        QVERIFY(confirmedTranslationBackend(cuda,"Available devices: CUDA2: NVIDIA RTX").isEmpty());
        QVERIFY(confirmedTranslationBackend(cuda,"using device CUDA0 (wrong GPU)\noffloaded 37/37 layers to GPU").isEmpty());
        QVERIFY(confirmedTranslationBackend(cuda,"using device CUDA2 (NVIDIA GeForce RTX 4080 Laptop GPU)").isEmpty());
        QCOMPARE(confirmedTranslationBackend(cuda,"using device CUDA2 (NVIDIA GeForce RTX 4080 Laptop GPU) (0000:01:00.0) - 10000 MiB free\noffloaded 37/37 layers to GPU"),QString("GPU / CUDA2 / NVIDIA GeForce RTX 4080 Laptop GPU"));
    }
    void parsesOnlyTheRequestedTranslationRuntime(){
        const auto output=QByteArray("ggml_cuda_init: Device 9: unrelated log\nAvailable devices:\n  CUDA2: NVIDIA RTX (Laptop) (12288 MiB, 9500 MiB free)\n  Vulkan0: Intel Arc (16000 MiB, 15000 MiB free)\n  CUDA2: duplicate (12288 MiB, 9500 MiB free)\n");
        const auto cuda=parseTranslationDevices(output,"CUDA");QCOMPARE(cuda.size(),1);
        const auto entry=cuda[0].toObject();QCOMPARE(entry.value("id").toString(),QString("CUDA2"));QCOMPARE(entry.value("name").toString(),QString("NVIDIA RTX (Laptop)"));
        QCOMPARE(entry.value("backend").toString(),QString("CUDA"));QCOMPARE(entry.value("type").toString(),QString("gpu"));QCOMPARE(entry.value("freeMiB").toInt(),9500);
        QVERIFY(parseTranslationDevices("CUDA0: log without inventory (100 MiB, 50 MiB free)\n","CUDA").isEmpty());
        QVERIFY(parseTranslationDevices(QByteArray("Available devices:\n CUDA0: broken ")+char(-1)+" (100 MiB, 50 MiB free)\n","CUDA").isEmpty());
        QVERIFY(parseTranslationDevices(QByteArray("Available devices:\n CUDA0: NVIDIA (100 MiB, 50 MiB free)\n")+char(-30),"CUDA").isEmpty());
    }
    void mapsOpenVinoInventoryToItsActualEnvironmentDevice(){
        const auto devices=parseTranslationDevices("Available devices:\n OpenVINO0: GGML_OPENVINO_DEVICE=GPU.1 (selected) - NVIDIA RTX (12000 MiB, 10000 MiB free)\n OpenVINO1: GGML_OPENVINO_DEVICE=NPU - Intel AI Boost (0 MiB, 0 MiB free)\n OpenVINO2: GGML_OPENVINO_DEVICE=CPU - CPU (0 MiB, 0 MiB free)\n","OpenVINO");
        QCOMPARE(devices.size(),2);QCOMPARE(devices[0].toObject().value("id").toString(),QString("OpenVINO:GPU.1"));
        QCOMPARE(devices[1].toObject().value("type").toString(),QString("npu"));
    }
    void probesInstalledRuntimesAsynchronouslyAndSurvivesFailure(){
        QTemporaryDir root;QVERIFY(root.isValid());
        for(const auto &backend:{QString("cuda"),QString("vulkan")}){
            const auto directory=root.path()+"/translation/"+backend;QVERIFY(QDir().mkpath(directory));
#ifdef Q_OS_WIN
            const auto executable=directory+"/llama-server.exe";
#else
            const auto executable=directory+"/llama-server";
#endif
            QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),executable));
            QFile mode(directory+"/probe-mode.txt");QVERIFY(mode.open(QIODevice::WriteOnly));mode.write(backend=="cuda"?"stall":"unicode");
        }
        TranslationDeviceProbe probe(root.path());probe.setTimeout(1500);QSignalSpy ready(&probe,&TranslationDeviceProbe::devicesReady),failed(&probe,&TranslationDeviceProbe::failed);
        QVERIFY(ready.isValid());QVERIFY(failed.isValid());
        probe.start();QCOMPARE(ready.size(),0);probe.start();
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,8000);QVERIFY(failed.size()>=1);
        const auto devices=qvariant_cast<QJsonArray>(ready[0][0]);QCOMPARE(devices.size(),1);QCOMPARE(devices[0].toObject().value("name").toString(),QString::fromUtf8("Intel Arc 日本語"));
    }
    void cancellingBeforeDispatchCannotFinishANewerProbeTwice(){
        QTemporaryDir root;TranslationDeviceProbe probe(root.path());QSignalSpy ready(&probe,&TranslationDeviceProbe::devicesReady);
        probe.start();probe.cancel();probe.start();QTRY_COMPARE(ready.size(),1);QTest::qWait(30);QCOMPARE(ready.size(),1);
    }
    void failedCallbackCanCancelAndRestartWithoutStaleDispatch(){
        QTemporaryDir root;QVERIFY(root.isValid());
        QString cudaPath;
        for(const auto &backend:{QString("cuda"),QString("vulkan")}){
            const auto directory=root.path()+"/translation/"+backend;QVERIFY(QDir().mkpath(directory));
#ifdef Q_OS_WIN
            const auto executable=directory+"/llama-server.exe";
#else
            const auto executable=directory+"/llama-server";
#endif
            QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),executable));if(backend=="cuda")cudaPath=executable;
            QFile mode(directory+"/probe-mode.txt");QVERIFY(mode.open(QIODevice::WriteOnly));mode.write(backend=="cuda"?"stall":"unicode");
        }
        TranslationDeviceProbe probe(root.path());probe.setTimeout(1500);QSignalSpy ready(&probe,&TranslationDeviceProbe::devicesReady);bool restarted=false;
        connect(&probe,&TranslationDeviceProbe::failed,&probe,[&]{if(!restarted){restarted=true;probe.cancel();QVERIFY(QFile::remove(cudaPath));probe.start();}});
        probe.start();QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,8000);QVERIFY(restarted);
        const auto devices=qvariant_cast<QJsonArray>(ready[0][0]);QCOMPARE(devices.size(),1);QCOMPARE(devices[0].toObject().value("id").toString(),QString("Vulkan0"));
    }
    void nativeInstalledTranslationInventory(){
        const auto runtime=qEnvironmentVariable("LUMA_TRANSLATION_PROBE_RUNTIME");if(runtime.isEmpty())QSKIP("Set LUMA_TRANSLATION_PROBE_RUNTIME for installed native runtimes.");
        TranslationDeviceProbe probe(runtime);QSignalSpy ready(&probe,&TranslationDeviceProbe::devicesReady);
        probe.start();QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,40000);
        const auto devices=qvariant_cast<QJsonArray>(ready[0][0]);bool cuda=false;
        for(const auto &entry:devices){const auto device=entry.toObject();if(device.value("id").toString()=="CUDA0"){cuda=true;QVERIFY(device.value("name").toString().contains("NVIDIA"));QVERIFY(device.value("memoryMiB").toInt()>0);}}
        QVERIFY(cuda);
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--list-devices")){
        QFile mode(QCoreApplication::applicationDirPath()+"/probe-mode.txt");if(!mode.open(QIODevice::ReadOnly))return 2;
        if(mode.readAll()=="stall")return app.exec();
        QFile output;output.open(stdout,QIODevice::WriteOnly);output.write(QString::fromUtf8("Available devices:\n Vulkan0: Intel Arc 日本語 (16000 MiB, 15000 MiB free)\n").toUtf8());output.flush();return 0;
    }
    TranslationDeviceTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "translation_device_tests.moc"
