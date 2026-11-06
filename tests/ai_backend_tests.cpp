#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include "subtitles/ai_backend.h"
using namespace luma;
namespace {
const QString fixtureHash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
bool writeFixtureFile(const QString &path,const QByteArray &bytes){QFile file(path);return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();}
struct NpuProfileFixture {
    QString runtime,model,stem,worker;
    QJsonObject metadata,profile;
    NpuDriverIdentity driver{"PCI\\VEN_8086&DEV_7D1D\\FIXTURE","{f01a9d53-3ff6-48d2-9f97-c8a7004be10c}\\0000","32.0.100.5540","9-1-2026","oem42.inf","pci\\ven_8086&dev_7d1d"};
    QJsonObject identity(const QString &path,const QString &name)const{
        const QFileInfo file(path);return {{"relativePath",name},{"sha256",fixtureHash},{"bytes",file.size()},{"mtimeMs",file.lastModified().toMSecsSinceEpoch()}};
    }
    bool save(){return writeFixtureFile(stem+".npu-profile.json",QJsonDocument(profile).toJson());}
    bool create(const QString &root){
        runtime=root;QDir dir(root);if(!dir.mkpath("ai"))return false;
        model=dir.filePath("ggml-kotoba-whisper-v2.0-q5_0.bin");stem=dir.filePath("ggml-kotoba-whisper-v2.0-q5_0-encoder-openvino");
#ifdef Q_OS_WIN
        worker=dir.filePath("ai/luma-whisper-worker.exe");
#else
        worker=dir.filePath("ai/luma-whisper-worker");
#endif
        if(!writeFixtureFile(worker,"abc") || !writeFixtureFile(stem+".xml","abc") || !writeFixtureFile(stem+".bin","abc"))return false;
        QFile source(model);if(!source.open(QIODevice::WriteOnly) || !source.resize(537819875))return false;source.close();
        metadata={{"format","luma-whisper-encoder-v1"},{"sourceModelSha256","4a3b92192b5d3578ff854a5876213e2e27af0c2d357492c2d14271e82c303658"},{"sourceModelBytes",537819875},
            {"encoderXmlBytes",3},{"encoderBinBytes",3},{"encoderXmlSha256",fixtureHash},{"encoderBinSha256",fixtureHash},
            {"inputName","mel"},{"inputShape",QJsonArray{1,128,3000}},{"outputName","output_features"},{"outputShape",QJsonArray{1,1500,1280}}};
        if(!writeFixtureFile(stem+".json",QJsonDocument(metadata).toJson()))return false;
        QJsonArray dependencies;
        for(const auto &name:QStringList{"whisper.dll","ggml.dll","ggml-base.dll","ggml-cpu-x64.dll","openvino.dll","openvino_ir_frontend.dll","openvino_intel_npu_plugin.dll","tbb12.dll"}){
            const auto path=dir.filePath("ai/"+name);if(!writeFixtureFile(path,"abc"))return false;dependencies.append(identity(path,name));
        }
        QJsonArray runs;for(int index=0;index<4;++index)runs.append(QJsonObject{{"index",index},{"firstInference",index==0},{"requestWallMs",index==0?2000:1400},{"audioMs",16000},{"inferenceMs",index==0?1800:1200},{"encoderTotalMs",1000},{"encoderCalls",1},{"backend","NPU"},{"device","NPU"}});
        const QFileInfo sourceInfo(model),xml(stem+".xml"),bin(stem+".bin");
        profile={{"format","luma-npu-profile-v1"},{"policy","synthetic-16s-4requests-maxwarm3000-v1"},{"eligible",true},{"inferenceVerified",true},{"reason","qualified"},
            {"testedAtUtc","2026-11-06T07:00:00Z"},{"stimulus","deterministic-pcm-v1"},{"audioMs",16000},{"requestCount",4},{"warmRequestCount",3},{"readyMs",500},{"warmMaxInferenceMs",1200},{"warmMaxWallMs",1400},{"firstInferenceMs",1800},{"workerThreads",6},{"language","ja"},
            {"source",QJsonObject{{"sha256",metadata["sourceModelSha256"]},{"bytes",537819875},{"mtimeMs",sourceInfo.lastModified().toMSecsSinceEpoch()}}},
            {"encoder",QJsonObject{{"xmlSha256",fixtureHash},{"xmlBytes",3},{"xmlMtimeMs",xml.lastModified().toMSecsSinceEpoch()},{"binSha256",fixtureHash},{"binBytes",3},{"binMtimeMs",bin.lastModified().toMSecsSinceEpoch()}}},
            {"worker",identity(worker,QFileInfo(worker).fileName())},{"runtimeFiles",dependencies},
            {"device",QJsonObject{{"pnpDeviceId",driver.pnpDeviceId},{"driverKey",driver.driverKey},{"driverVersion",driver.driverVersion},{"driverDate",driver.driverDate},{"infPath",driver.infPath},{"matchingDeviceId",driver.matchingDeviceId}}},{"runs",runs}};
        return save();
    }
};
}
class AiBackendTests:public QObject {
    Q_OBJECT
private slots:
    void qualifiedLocalProfileSelectsNpu(){
        QTemporaryDir root;NpuProfileFixture fixture;QVERIFY(fixture.create(root.path()));
        const auto status=npuCalibrationStatus(fixture.runtime,fixture.model,fixture.driver);
        QCOMPARE(status.state,NpuCalibrationState::Qualified);QVERIFY(status.eligible);QVERIFY(status.inferenceVerified);QCOMPARE(status.completedRequests,4);QCOMPARE(status.maxWarmInferenceMs,1200);QCOMPARE(status.maxWarmWallMs,1400);
        const auto plans=recognitionBackends(fixture.runtime,fixture.model,{},fixture.driver);
        QCOMPARE(plans[0].key,QString("NPU"));QCOMPARE(plans.size(),3);
        QCOMPARE(recognitionBackends(fixture.runtime,fixture.model,{"gpu",{}},fixture.driver)[0].key,QString("GPU"));
        QCOMPARE(recognitionBackends(fixture.runtime,fixture.model,{"cpu",{}},fixture.driver).size(),1);
    }
    void invalidOrStaleProfileCannotSelectNpu_data(){
        QTest::addColumn<QString>("mutation");QTest::addColumn<int>("expected");
        for(const auto &name:QStringList{"format","policy","traversal","absolute","worker-path","missing-runtime","zero-encode","wrong-device","missing-run","null-time","false-summary","readiness","unsafe-registry-key","string-eligible"})
            QTest::newRow(qPrintable(name))<<name<<int(NpuCalibrationState::Invalid);
        for(const auto &name:QStringList{"source-changed","xml-changed","bin-changed","worker-changed","runtime-changed","runtime-added","driver-changed"})
            QTest::newRow(qPrintable(name))<<name<<int(NpuCalibrationState::Stale);
    }
    void invalidOrStaleProfileCannotSelectNpu(){
        QFETCH(QString,mutation);QFETCH(int,expected);
        QTemporaryDir root;NpuProfileFixture fixture;QVERIFY(fixture.create(root.path()));
        auto setRun=[&](const QString &key,const QJsonValue &value){auto runs=fixture.profile["runs"].toArray();auto run=runs[1].toObject();run[key]=value;runs[1]=run;fixture.profile["runs"]=runs;};
        if(mutation=="format")fixture.profile["format"]="other";
        else if(mutation=="policy")fixture.profile["policy"]="old-budget";
        else if(mutation=="traversal" || mutation=="absolute"){auto files=fixture.profile["runtimeFiles"].toArray();auto file=files[0].toObject();file["relativePath"]=mutation=="traversal"?"../whisper.dll":"C:/Windows/whisper.dll";files[0]=file;fixture.profile["runtimeFiles"]=files;}
        else if(mutation=="worker-path"){auto worker=fixture.profile["worker"].toObject();worker["relativePath"]="../luma-whisper-worker.exe";fixture.profile["worker"]=worker;}
        else if(mutation=="missing-runtime")fixture.profile["runtimeFiles"]=QJsonArray{};
        else if(mutation=="zero-encode")setRun("encoderCalls",0);
        else if(mutation=="wrong-device")setRun("device","GPU.0");
        else if(mutation=="missing-run"){auto runs=fixture.profile["runs"].toArray();runs.removeLast();fixture.profile["runs"]=runs;}
        else if(mutation=="null-time")setRun("inferenceMs",QJsonValue::Null);
        else if(mutation=="false-summary")setRun("inferenceMs",3001);
        else if(mutation=="readiness")fixture.profile["readyMs"]=60001;
        else if(mutation=="unsafe-registry-key"){auto device=fixture.profile["device"].toObject();device["driverKey"]="../Class/other";fixture.profile["device"]=device;}
        else if(mutation=="string-eligible")fixture.profile["eligible"]="true";
        else if(mutation=="source-changed"){QFile file(fixture.model);QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.setFileTime(QFileInfo(file).lastModified().addSecs(2),QFileDevice::FileModificationTime));}
        else if(mutation=="xml-changed")QVERIFY(writeFixtureFile(fixture.stem+".xml","abcd"));
        else if(mutation=="bin-changed")QVERIFY(writeFixtureFile(fixture.stem+".bin","abcd"));
        else if(mutation=="worker-changed")QVERIFY(writeFixtureFile(fixture.worker,"abcd"));
        else if(mutation=="runtime-changed")QVERIFY(writeFixtureFile(root.filePath("ai/openvino.dll"),"abcd"));
        else if(mutation=="runtime-added")QVERIFY(writeFixtureFile(root.filePath("ai/openvino_intel_npu_compiler.dll"),"abc"));
        else if(mutation=="driver-changed")fixture.driver.driverVersion="32.0.100.9999";
        else QFAIL("Unknown fixture mutation");
        QVERIFY(fixture.save());const auto status=npuCalibrationStatus(fixture.runtime,fixture.model,fixture.driver);
        QCOMPARE(int(status.state),expected);QVERIFY(!status.eligible);
        const auto plans=recognitionBackends(fixture.runtime,fixture.model,{},fixture.driver);
        QCOMPARE(plans[0].key,QString("GPU"));QCOMPARE(plans.size(),2);
    }
    void genuineSlowProfileIsVerifiedButNotRecommended_data(){
        QTest::addColumn<QString>("field");QTest::addColumn<QString>("summary");
        QTest::newRow("inference-time")<<QString("inferenceMs")<<QString("warmMaxInferenceMs");
        QTest::newRow("whole-request-wall-time")<<QString("requestWallMs")<<QString("warmMaxWallMs");
    }
    void genuineSlowProfileIsVerifiedButNotRecommended(){
        QFETCH(QString,field);QFETCH(QString,summary);
        QTemporaryDir root;NpuProfileFixture fixture;QVERIFY(fixture.create(root.path()));
        auto runs=fixture.profile["runs"].toArray();auto run=runs[1].toObject();run[field]=3001;runs[1]=run;
        fixture.profile["runs"]=runs;fixture.profile["eligible"]=false;fixture.profile["reason"]="too-slow";fixture.profile[summary]=3001;QVERIFY(fixture.save());
        const auto status=npuCalibrationStatus(fixture.runtime,fixture.model,fixture.driver);
        QCOMPARE(status.state,NpuCalibrationState::Rejected);QCOMPARE(status.reason,QString("too-slow"));QVERIFY(status.inferenceVerified);QVERIFY(!status.eligible);QCOMPARE(status.completedRequests,4);
    }
    void rejectedProfileStillRejectsUnsafePaths(){
        QTemporaryDir root;NpuProfileFixture fixture;QVERIFY(fixture.create(root.path()));
        fixture.profile["eligible"]=false;fixture.profile["inferenceVerified"]=false;fixture.profile["reason"]="worker-failed";fixture.profile["runs"]=QJsonArray{};
        auto files=fixture.profile["runtimeFiles"].toArray();auto file=files[0].toObject();file["relativePath"]="../outside.dll";files[0]=file;fixture.profile["runtimeFiles"]=files;QVERIFY(fixture.save());
        QCOMPARE(npuCalibrationStatus(fixture.runtime,fixture.model,fixture.driver).state,NpuCalibrationState::Invalid);
    }
    void rejectedIncompleteRuntimeIsReportedWithoutDeviceInitialization(){
        QTemporaryDir root;NpuProfileFixture fixture;QVERIFY(fixture.create(root.path()));
        fixture.profile["eligible"]=false;fixture.profile["inferenceVerified"]=false;fixture.profile["reason"]="runtime-incomplete";fixture.profile["runs"]=QJsonArray{};fixture.profile["runtimeFiles"]=QJsonArray{};fixture.profile["device"]=QJsonValue::Null;QVERIFY(fixture.save());
        const auto status=npuCalibrationStatus(fixture.runtime,fixture.model);
        QCOMPARE(status.state,NpuCalibrationState::Rejected);QCOMPARE(status.reason,QString("runtime-incomplete"));QVERIFY(!status.eligible);QVERIFY(!status.inferenceVerified);
    }
    void dotNetTimestampAndBudgetBoundaryAreAccepted(){
        QTemporaryDir root;NpuProfileFixture fixture;QVERIFY(fixture.create(root.path()));
        fixture.profile["testedAtUtc"]="2026-11-06T07:00:00.1234567Z";fixture.profile["readyMs"]=60000;
        auto runs=fixture.profile["runs"].toArray();for(int i=1;i<4;++i){auto run=runs[i].toObject();run["inferenceMs"]=3000;run["requestWallMs"]=3000;runs[i]=run;}
        fixture.profile["runs"]=runs;fixture.profile["warmMaxInferenceMs"]=3000;fixture.profile["warmMaxWallMs"]=3000;QVERIFY(fixture.save());
        const auto status=npuCalibrationStatus(fixture.runtime,fixture.model,fixture.driver);QCOMPARE(status.state,NpuCalibrationState::Qualified);QVERIFY(status.eligible);
    }
    void kotobaAutomaticModeRequiresCalibration(){
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
        QJsonObject metadata{{"format","luma-whisper-encoder-v1"},{"sourceModelSha256","4a3b92192b5d3578ff854a5876213e2e27af0c2d357492c2d14271e82c303658"},{"sourceModelBytes",537819875},
            {"encoderXmlBytes",7},{"encoderBinBytes",7},{"encoderXmlSha256",QString(64,'a')},{"encoderBinSha256",QString(64,'b')},
            {"inputName","mel"},{"inputShape",QJsonArray{1,128,3000}},{"outputName","output_features"},{"outputShape",QJsonArray{1,1500,1280}}};
        QFile manifest(stem+".json");QVERIFY(manifest.open(QIODevice::WriteOnly));QVERIFY(manifest.write(QJsonDocument(metadata).toJson())>0);manifest.close();
        const auto automatic=recognitionBackends(root.path(),model);
        QCOMPARE(automatic[0].key,QString("GPU"));QCOMPARE(automatic.size(),2);
        const auto manual=recognitionBackends(root.path(),model,{"npu",{}});
        QCOMPARE(manual[0].key,QString("NPU"));
    }
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
    void nativeCpuRequiresItsOwnExecutionAcknowledgment(){
        QTemporaryDir root;QVERIFY(QDir(root.path()).mkpath("ai"));
#ifdef Q_OS_WIN
        const auto worker=root.filePath("ai/luma-whisper-worker.exe");
#else
        const auto worker=root.filePath("ai/luma-whisper-worker");
#endif
        QFile file(worker);QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");file.close();
        const auto backends=recognitionBackends(root.path(),root.filePath("model.bin"),{"cpu",{}});QVERIFY(backends.size()==1 || backends.size()==2);
        QCOMPARE(backends[0].program,worker);QCOMPARE(backends[0].device,QString("CPU"));
        QVERIFY(!confirmsBackend(backends[0],"LUMA_BACKEND=GPU\n"));QVERIFY(confirmsBackend(backends[0],"LUMA_BACKEND=CPU\n"));
        if(backends.size()==2){QCOMPARE(backends[1].key,QString("CPU"));QVERIFY(backends[1].device.isEmpty());QVERIFY(backends[1].program!=worker);}
        QVERIFY(QDir(root.path()).mkpath("whisper"));
#ifdef Q_OS_WIN
        const auto legacy=root.filePath("whisper/whisper-cli.exe");
#else
        const auto legacy=root.filePath("whisper/whisper-cli");
#endif
        QFile cli(legacy);QVERIFY(cli.open(QIODevice::WriteOnly));cli.write("fixture");cli.close();
        const auto withLegacy=recognitionBackends(root.path(),root.filePath("model.bin"),{"cpu",{}});QCOMPARE(withLegacy.size(),2);
        QCOMPARE(withLegacy[0].program,worker);QCOMPARE(withLegacy[1].program,legacy);QVERIFY(withLegacy[1].device.isEmpty());
    }
};
QTEST_GUILESS_MAIN(AiBackendTests)
#include "ai_backend_tests.moc"
