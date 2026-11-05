#include <QtTest>
#include <QDir>
#include <QTemporaryDir>
#include "subtitles/runtime_environment.h"

class RuntimeEnvironmentTests:public QObject {
    Q_OBJECT
private slots:
    void absentSharedRuntimePreservesEnvironment(){
        QTemporaryDir root;QVERIFY(root.isValid());QProcessEnvironment original;
        original.insert("PATH","existing");original.insert("LUMA_OWNED_TEST","preserved");
        QCOMPARE(luma::localRuntimeEnvironment(root.path(),original),original);
    }
    void sharedRuntimePrecedesExistingPathsWithoutChangingParent(){
        QTemporaryDir root;QVERIFY(root.isValid());
        const auto runtime=root.path()+QString::fromUtf8("/owned runtime 日本語 한글");
        QVERIFY(QDir().mkpath(runtime+"/cuda"));QProcessEnvironment original;
        original.insert("PATH","existing");original.insert("LUMA_OWNED_TEST","preserved");
        const auto result=luma::localRuntimeEnvironment(runtime,original);
#ifdef Q_OS_WIN
        QCOMPARE(result.value("PATH"),QDir::toNativeSeparators(runtime+"/cuda")+";existing");
#else
        QCOMPARE(result,original);
#endif
        QCOMPARE(result.value("LUMA_OWNED_TEST"),QString("preserved"));QCOMPARE(original.value("PATH"),QString("existing"));
    }
    void repeatedPreparationDoesNotDuplicateSharedPath(){
        QTemporaryDir root;QVERIFY(root.isValid());QVERIFY(QDir().mkpath(root.path()+"/cuda"));
        QProcessEnvironment original;original.insert("PATH","existing");
        const auto first=luma::localRuntimeEnvironment(root.path(),original);
        QCOMPARE(luma::localRuntimeEnvironment(root.path(),first),first);
    }
    void emptyPathDoesNotIntroduceCurrentDirectory(){
        QTemporaryDir root;QVERIFY(root.isValid());QVERIFY(QDir().mkpath(root.path()+"/cuda"));
        const auto result=luma::localRuntimeEnvironment(root.path(),{});
#ifdef Q_OS_WIN
        QCOMPARE(result.value("PATH"),QDir::toNativeSeparators(root.path()+"/cuda"));
#else
        QVERIFY(!result.contains("PATH"));
#endif
    }
};
QTEST_GUILESS_MAIN(RuntimeEnvironmentTests)
#include "runtime_environment_tests.moc"
