#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <algorithm>
#include "core/resume_history.h"
#include "core/disc.h"
using namespace luma;
namespace {
QString ownedFile(const QString &root,const QString &name){const auto path=root+'/'+name;QFile file(path);if(!file.open(QIODevice::WriteOnly))return {};file.write("self-owned fixture");return path;}
DiscInfo ownedDisc(const QString &root="X:/"){DiscInfo disc;disc.valid=true;disc.root=root;disc.label="Owned Disc";disc.titles={{"00002",120000,{"00001","00002"}},{"00001",80000,{"00003"}}};return disc;}
}
class ResumeHistoryTests:public QObject {
    Q_OBJECT
private slots:
    void reloadsTwoFilesAndPreservesUnicodePaths(){
        QTemporaryDir root;const auto a=ownedFile(root.path(),QString::fromUtf8("日本語 한글 🎵.mkv")),b=ownedFile(root.path(),"other.mkv");QVERIFY(!a.isEmpty());
        const auto ida=resumeIdentity(a),idb=resumeIdentity(b);QVERIFY(!ida.isEmpty());QVERIFY(ida!=idb);
        {ResumeHistory history(root.path()+"/history.ini");history.save(ida,{a,false,-1,15000,120000});history.save(idb,{b,false,-1,25000,180000});}
        ResumeHistory reloaded(root.path()+"/history.ini");QVERIFY(reloaded.find(ida));QCOMPARE(reloaded.find(ida)->source,a);QCOMPARE(reloaded.find(ida)->positionMs,15000);
        QCOMPARE(reloaded.find(idb)->positionMs,25000);QCOMPARE(reloaded.latest()->source,b);QCOMPARE(reloaded.latestIdentity(),idb);
    }
    void fileIdentityChangesForSizeAndModificationTime(){
        QTemporaryDir root;const auto path=ownedFile(root.path(),"owned.mkv");const auto first=resumeIdentity(path);QVERIFY(!first.isEmpty());
        QFile file(path);QVERIFY(file.open(QIODevice::Append));file.write("changed");file.close();const auto second=resumeIdentity(path);QVERIFY(second!=first);
        QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addDays(-1),QFileDevice::FileModificationTime));file.close();QVERIFY(resumeIdentity(path)!=second);
        QVERIFY(resumeIdentity(root.path()+"/missing.mkv").isEmpty());QVERIFY(resumeIdentity(root.path()).isEmpty());
    }
    void discIdentityDoesNotDependOnDriveOrPlaylistEnumerationOrder(){
        auto a=ownedDisc(),b=ownedDisc("Y:/");std::reverse(b.titles.begin(),b.titles.end());
        const auto first=resumeIdentity("X:/",&a);QVERIFY(!first.isEmpty());QCOMPARE(resumeIdentity("bluray:/Y:/",&b),first);
        b.titles[0].clips.append("00004");QVERIFY(resumeIdentity("Y:/",&b)!=first);
        b=a;b.titles[0].clips={"00002","00001"};QVERIFY(resumeIdentity("Y:/",&b)!=first);
        b=a;b.valid=false;QVERIFY(resumeIdentity("Y:/",&b).isEmpty());
        a.label="X:";b=a;b.root="Y:/";b.label="Y:";QCOMPARE(resumeIdentity(a.root,&a),resumeIdentity(b.root,&b));
    }
    void separatesTwoDiscsAndMultipleMainTitles(){
        QTemporaryDir root;auto a=ownedDisc(),b=ownedDisc("Y:/");b.label="Other Disc";const auto ida=resumeIdentity(a.root,&a),idb=resumeIdentity(b.root,&b);
        ResumeHistory history(root.path()+"/history.ini");history.save(ida,{a.root,true,1,15000,120000});history.save(idb,{b.root,true,1,30000,120000});history.save(ida,{a.root,true,2,25000,120000});
        QVERIFY(history.find(ida,1));QVERIFY(history.find(ida));QVERIFY(history.find(idb));
        QCOMPARE(history.find(ida,1)->positionMs,15000);QCOMPARE(history.find(ida)->title,2);QCOMPARE(history.find(idb)->positionMs,30000);
        history.remove(ida,2);QCOMPARE(history.find(ida)->title,1);QVERIFY(history.find(idb));
    }
    void menuUnknownLengthAndInitialClockCannotEraseMainHistory(){
        QTemporaryDir root;auto disc=ownedDisc();const auto identity=resumeIdentity(disc.root,&disc);ResumeHistory history(root.path()+"/history.ini");
        history.save(identity,{disc.root,true,1,20000,120000});
        for(const auto title:{-1,0})history.save(identity,{disc.root,true,title,120000,120000});
        history.save(identity,{disc.root,true,1,0,120000});history.save(identity,{disc.root,true,1,1000,0});history.save(identity,{disc.root,true,1,-1,120000});
        history.save(identity,{disc.root,true,1,0,3000});
        QVERIFY(history.find(identity));QCOMPARE(history.find(identity)->title,1);QCOMPARE(history.find(identity)->positionMs,20000);
    }
    void approachingOrReachingEndClearsOnlyThatPoint(){
        QTemporaryDir root;const auto a=ownedFile(root.path(),"a.mkv"),b=ownedFile(root.path(),"b.mkv"),ida=resumeIdentity(a),idb=resumeIdentity(b);ResumeHistory history(root.path()+"/history.ini");
        history.save(idb,{b,false,-1,15000,120000});history.save(ida,{a,false,-1,20000,120000});history.save(ida,{a,false,-1,116000,120000});
        QVERIFY(!history.find(ida));QVERIFY(history.latest());QCOMPARE(history.latest()->source,b);
        history.save(ida,{a,false,-1,3000,120000});QVERIFY(history.find(ida));history.save(ida,{a,false,-1,120000,120000});QVERIFY(!history.find(ida));
    }
    void boundsHistoryToOneHundredRecentEntries(){
        QTemporaryDir root;ResumeHistory history(root.path()+"/history.ini");QString first,last;
        for(int i=0;i<105;++i){const auto source=ownedFile(root.path(),QString::number(i)+".mkv"),identity=resumeIdentity(source);if(!i)first=identity;last=identity;history.save(identity,{source,false,-1,3000+i,120000});}
        QVERIFY(!history.find(first));QVERIFY(history.find(last));QCOMPARE(history.latest()->positionMs,3104);
    }
    void minimumPositionAndRemainingTimeAreInclusive(){
        QTemporaryDir root;const auto source=ownedFile(root.path(),"short.mkv"),identity=resumeIdentity(source);ResumeHistory history(root.path()+"/history.ini");
        history.save(identity,{source,false,-1,2999,120000});QVERIFY(!history.find(identity));
        history.save(identity,{source,false,-1,3000,8000});QVERIFY(history.find(identity));
        history.save(identity,{source,false,-1,3001,8000});QVERIFY(!history.find(identity));
    }
    void directTitleZeroAndOptionalProjectRoundTrip(){
        QTemporaryDir root;auto disc=ownedDisc();const auto identity=resumeIdentity(disc.root,&disc),settingsFile=root.filePath("history.ini");
        const auto project=root.filePath(QString::fromUtf8("아직 없는 日本語 🎵 프로젝트.json"));QVERIFY(!QFileInfo::exists(project));
        {ResumeHistory history(settingsFile);history.save(identity,{disc.root,true,0,20000,120000,false,project});}
        ResumeHistory reloaded(settingsFile);const auto point=reloaded.find(identity,0,false);QVERIFY(point);
        QCOMPARE(point->title,0);QCOMPARE(point->positionMs,qint64(20000));QVERIFY(!point->discMenus);QCOMPARE(point->preparedProject,project);
        QVERIFY(!reloaded.find(identity));QVERIFY(reloaded.latest());QCOMPARE(reloaded.latest()->title,0);QCOMPARE(reloaded.latestIdentity(),identity);
        reloaded.save(identity,{disc.root,true,-1,25000,120000,false,project});QCOMPARE(reloaded.find(identity,0,false)->positionMs,qint64(20000));
    }
    void legacyEntriesDefaultToMenusAndFilesIgnoreModeFilter(){
        QTemporaryDir root;const auto settingsFile=root.filePath("history.ini"),discId=QString(64,'a'),fileId=QString(64,'b');
        const QJsonArray entries{QJsonObject{{"identity",discId},{"source","X:/"},{"disc",true},{"title",1},{"positionMs","15000"},{"durationMs","120000"}},
            QJsonObject{{"identity",fileId},{"source","owned.mkv"},{"disc",false},{"title",-1},{"positionMs","25000"},{"durationMs","180000"}}};
        QSettings settings(settingsFile,QSettings::IniFormat);settings.setValue("resumeHistory/v1",QJsonDocument(QJsonObject{{"version",1},{"entries",entries}}).toJson());settings.sync();
        ResumeHistory history(settingsFile);const auto legacy=history.find(discId,1);QVERIFY(legacy);QVERIFY(legacy->discMenus);QVERIFY(legacy->preparedProject.isEmpty());
        QVERIFY(!history.find(discId,1,false));QVERIFY(history.find(fileId,-1,false));QVERIFY(history.find(fileId));QVERIFY(history.find(fileId)->preparedProject.isEmpty());
        history.remove(fileId,-1,false);QVERIFY(!history.find(fileId));QVERIFY(history.find(discId));
    }
    void sameNativeTitleHasSeparateMenuAndDirectResumeRecords(){
        QTemporaryDir root;auto disc=ownedDisc();const auto identity=resumeIdentity(disc.root,&disc),settingsFile=root.filePath("history.ini");
        {ResumeHistory history(settingsFile);history.save(identity,{disc.root,true,1,10000,120000,true});history.save(identity,{disc.root,true,1,20000,120000,false,"prepared.json"});}
        ResumeHistory history(settingsFile);QVERIFY(history.find(identity,1));QVERIFY(history.find(identity,1,false));
        QCOMPARE(history.find(identity,1)->positionMs,qint64(10000));QCOMPARE(history.find(identity,1,false)->positionMs,qint64(20000));
        QVERIFY(!history.latest()->discMenus);QCOMPARE(history.latest()->preparedProject,QString("prepared.json"));
        history.remove(identity,1);QVERIFY(!history.find(identity,1));QVERIFY(history.find(identity,1,false));
        history.save(identity,{disc.root,true,1,30000,120000,true});history.save(identity,{disc.root,true,0,40000,120000,false});
        history.remove(identity,-1,false);QVERIFY(!history.find(identity,-1,false));QVERIFY(history.find(identity,1));QCOMPARE(history.latest()->positionMs,qint64(30000));QVERIFY(history.latest()->discMenus);
    }
    void approachingEndRemovesOnlyTheSavedDiscMode(){
        QTemporaryDir root;auto disc=ownedDisc();const auto identity=resumeIdentity(disc.root,&disc);ResumeHistory history(root.filePath("history.ini"));
        history.save(identity,{disc.root,true,1,20000,120000,true});history.save(identity,{disc.root,true,1,30000,120000,false,"prepared.json"});
        history.save(identity,{disc.root,true,1,116000,120000,false,"prepared.json"});QVERIFY(history.find(identity,1));QVERIFY(!history.find(identity,1,false));
        history.save(identity,{disc.root,true,0,40000,120000,false});history.save(identity,{disc.root,true,0,116000,120000,false});
        QVERIFY(!history.find(identity,0,false));QCOMPARE(history.find(identity,1)->positionMs,qint64(20000));
    }
    void fileResumeDedupeDoesNotDependOnDiscMode(){
        QTemporaryDir root;const auto source=ownedFile(root.path(),"owned.mkv"),identity=resumeIdentity(source);ResumeHistory history(root.filePath("history.ini"));
        history.save(identity,{source,false,-1,10000,120000,false,"first.json"});history.save(identity,{source,false,-1,20000,120000,true,"second.json"});
        QCOMPARE(history.find(identity)->positionMs,qint64(20000));QCOMPARE(history.find(identity,-1,false)->preparedProject,QString("second.json"));
        history.remove(identity,-1,false);QVERIFY(!history.find(identity));QVERIFY(!history.latest());
    }
    void invalidNewStoredFieldsCannotBecomeAResumeRequest_data(){
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");
        QTest::newRow("mode-string")<<QString("discMenus")<<QJsonValue("false");
        QTest::newRow("mode-number")<<QString("discMenus")<<QJsonValue(0);
        QTest::newRow("mode-null")<<QString("discMenus")<<QJsonValue(QJsonValue::Null);
        QTest::newRow("project-bool")<<QString("preparedProject")<<QJsonValue(false);
        QTest::newRow("project-number")<<QString("preparedProject")<<QJsonValue(123);
        QTest::newRow("project-null")<<QString("preparedProject")<<QJsonValue(QJsonValue::Null);
        QTest::newRow("project-array")<<QString("preparedProject")<<QJsonValue(QJsonArray{});
        QTest::newRow("project-too-long")<<QString("preparedProject")<<QJsonValue(QString(32769,'x'));
        QTest::newRow("project-nul")<<QString("preparedProject")<<QJsonValue(QString("project")+QChar(0)+".json");
        QTest::newRow("project-unpaired-surrogate")<<QString("preparedProject")<<QJsonValue(QString(QChar(0xd800))+".json");
    }
    void invalidNewStoredFieldsCannotBecomeAResumeRequest(){
        QFETCH(QString,field);QFETCH(QJsonValue,value);QTemporaryDir root;const auto settingsFile=root.filePath("history.ini");
        QJsonObject entry{{"identity",QString(64,'a')},{"source","X:/"},{"disc",true},{"title",1},{"positionMs","15000"},{"durationMs","120000"}};entry.insert(field,value);
        QSettings settings(settingsFile,QSettings::IniFormat);settings.setValue("resumeHistory/v1",QJsonDocument(QJsonObject{{"version",1},{"entries",QJsonArray{entry}}}).toJson());settings.sync();
        ResumeHistory history(settingsFile);QVERIFY(!history.latest());
    }
    void invalidProjectOnSaveCannotEraseOrReplaceAValidPoint(){
        QTemporaryDir root;auto disc=ownedDisc();const auto identity=resumeIdentity(disc.root,&disc);ResumeHistory history(root.filePath("history.ini"));
        history.save(identity,{disc.root,true,1,20000,120000});
        for(const auto &project:QStringList{QString(32769,'x'),QString(QChar(0xd800)),QString("bad")+QChar(0)}){
            history.save(identity,{disc.root,true,1,30000,120000,true,project});QCOMPARE(history.find(identity,1)->positionMs,qint64(20000));
            history.save(identity,{disc.root,true,1,116000,120000,true,project});QVERIFY(history.find(identity,1));
        }
    }
    void malformedStoredDataCannotBecomeAResumeRequest(){
        QTemporaryDir root;const auto settingsFile=root.path()+"/history.ini";QSettings settings(settingsFile,QSettings::IniFormat);
        for(const auto &payload:{QByteArray("not json"),QByteArray("[]"),QByteArray("{\"version\":999,\"entries\":[]}"),QByteArray(600000,'x')}){
            settings.setValue("resumeHistory/v1",payload);settings.sync();ResumeHistory history(settingsFile);QVERIFY(!history.latest());
        }
        const auto validId=QString(64,'a');const QJsonObject base{{"identity",validId},{"source","owned.mkv"},{"disc",false},{"title",-1},{"positionMs","15000"},{"durationMs","120000"}};
        QJsonArray invalid;
        for(const auto &entry:QList<QPair<QString,QJsonValue>>{{"identity","../escape"},{"source",QJsonValue()},{"disc","true"},{"title",1.5},{"positionMs","-1"},{"positionMs","9223372036854775808"},{"durationMs","0"},{"durationMs","16000"}}){auto value=base;value.insert(entry.first,entry.second);invalid.append(value);}
        settings.setValue("resumeHistory/v1",QJsonDocument(QJsonObject{{"version",1},{"entries",invalid}}).toJson());settings.sync();ResumeHistory history(settingsFile);QVERIFY(!history.latest());
    }
};
QTEST_GUILESS_MAIN(ResumeHistoryTests)
#include "resume_history_tests.moc"
