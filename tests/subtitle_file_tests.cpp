#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QThread>
#include <cstdio>
#include "subtitles/subtitle_file.h"
using namespace luma;
namespace {
QString save(QTemporaryDir &folder,const QString &name,const QByteArray &bytes){QFile output(folder.filePath(name));if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size())return {};return output.fileName();}
QByteArray utf16(const QString &text,bool little){
    QByteArray bytes=little?QByteArray::fromHex("fffe"):QByteArray::fromHex("feff");
    for(const auto character:text){const auto value=character.unicode();bytes.append(char(little?value&255:value>>8));bytes.append(char(little?value>>8:value&255));}return bytes;
}
QString runtimePath(){return qEnvironmentVariable("LUMA_SUBTITLE_FILE_RUNTIME",QCoreApplication::applicationDirPath()+"/runtime");}
QString ffmpegName(){
#ifdef Q_OS_WIN
    return "ffmpeg.exe";
#else
    return "ffmpeg";
#endif
}
bool nativeFfmpegAvailable(){
    if(QFileInfo::exists(runtimePath()+"/tools/"+ffmpegName()))return true;
#ifdef Q_OS_LINUX
    return !QStandardPaths::findExecutable("ffmpeg").isEmpty();
#else
    return false;
#endif
}
QByteArray smi(const QByteArray &words,const QByteArray &charset){return "<SAMI><HEAD><META http-equiv=\"Content-Type\" content=\"text/html; charset="+charset+"\"></HEAD><BODY>\n<SYNC Start=1000><P Class=KRCC><FONT Color=red>"+words+"</FONT>\n<SYNC Start=3000><P>&nbsp;\n</BODY></SAMI>\n";}
QString fixtureRuntime(QTemporaryDir &folder){
    const auto tools=folder.filePath("runtime/tools");if(!QDir().mkpath(tools))return {};
    const auto binary=tools+"/"+ffmpegName();if(!QFile::copy(QCoreApplication::applicationFilePath(),binary))return {};
    QFile::setPermissions(binary,QFile::permissions(binary)|QFileDevice::ExeOwner|QFileDevice::ExeGroup|QFileDevice::ExeOther);return folder.filePath("runtime");
}
struct Environment {
    QByteArray name,previous;bool existed;
    Environment(QByteArray key,QByteArray value):name(std::move(key)),previous(qgetenv(name.constData())),existed(qEnvironmentVariableIsSet(name.constData())){qputenv(name.constData(),value);}
    ~Environment(){if(existed)qputenv(name.constData(),previous);else qunsetenv(name.constData());}
};
int runFixture(){
    const auto args=QCoreApplication::arguments();const int input=args.indexOf("-i");if(input<0 || input+1>=args.size())return 2;
    QFile file(args[input+1]);if(!file.open(QIODevice::ReadOnly))return 2;const auto bytes=file.readAll();
    if(bytes.contains("STALL_IMPORT")){
        QFile marker(qEnvironmentVariable("LUMA_SUBTITLE_FIXTURE_STARTED"));if(marker.open(QIODevice::WriteOnly))marker.write("started");marker.close();QThread::msleep(15000);return 0;
    }
    QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 2;
    if(bytes.contains("OVERSIZE_OUTPUT")){const QByteArray chunk(128*1024,'a');for(int i=0;i<70;++i)if(output.write(chunk)!=chunk.size())return 3;output.flush();return 0;}
    if(bytes.contains("MALFORMED_OUTPUT")){output.write("1\n00:00:01,000 --> 00:00:03,000\n");output.write(QByteArray::fromHex("c0af"));output.flush();return 0;}
    output.write("1\n00:00:01,000 --> 00:00:03,000\nFixture\n");output.flush();return 0;
}
}
class SubtitleFileTests:public QObject {
    Q_OBJECT
private slots:
    void unicodeSrtPreservesLegitimateCharacters_data(){QTest::addColumn<QString>("encoding");QTest::newRow("UTF8")<<QString("UTF-8");QTest::newRow("UTF8-BOM")<<QString("UTF-8-BOM");QTest::newRow("UTF16LE")<<QString("UTF-16LE");QTest::newRow("UTF16BE")<<QString("UTF-16BE");}
    void unicodeSrtPreservesLegitimateCharacters(){
        QFETCH(QString,encoding);QTemporaryDir folder;const auto words=QString::fromUtf8("日本語 한글 が é 👩‍👩‍👧‍👦 ♪ �");const auto text="1\n00:00:01,200 --> 00:00:03,400\n"+words+"\n";
        auto bytes=encoding=="UTF-16LE"?utf16(text,true):encoding=="UTF-16BE"?utf16(text,false):text.toUtf8();if(encoding=="UTF-8-BOM")bytes.prepend(QByteArray::fromHex("efbbbf"));
        const auto result=readSrtFile(save(folder,"unicode.srt",bytes));QVERIFY2(result.valid(),qPrintable(result.error));QCOMPARE(result.cues.size(),1);QCOMPARE(result.cues[0].text,words);QCOMPARE(result.cues[0].startMs,1200);QCOMPARE(result.cues[0].endMs,3400);QVERIFY(!result.encoding.isEmpty());
    }
    void malformedUnicodeIsRejected_data(){
        QTest::addColumn<QByteArray>("text");
        QTest::newRow("invalidUTF8")<<QByteArray("1\n00:00:00,000 --> 00:00:01,000\n")+QByteArray::fromHex("c0af");
        QTest::newRow("incompleteUTF8")<<QByteArray("1\n00:00:00,000 --> 00:00:01,000\n")+QByteArray::fromHex("e381");
        QTest::newRow("oddUTF16")<<utf16("1\n00:00:00,000 --> 00:00:01,000\ntext",true)+QByteArray("x");
        QTest::newRow("unpairedUTF16")<<utf16("1\n00:00:00,000 --> 00:00:01,000\n",true)+QByteArray::fromHex("00d8");
    }
    void malformedUnicodeIsRejected(){QFETCH(QByteArray,text);QTemporaryDir folder;const auto result=readSrtFile(save(folder,"bad.srt",text));QVERIFY(!result.valid());QVERIFY(!result.error.isEmpty());QVERIFY(result.cues.isEmpty());}
    void malformedOrOversizedSrtDoesNotReturnPartialCaptions(){
        QTemporaryDir folder;const auto partial=save(folder,"partial.srt","1\n00:00:00,000 --> 00:00:01,000\nGood\n\n2\nwrong timestamp\nLost words\n");QVERIFY(!readSrtFile(partial).valid());
        QFile large(folder.filePath("large.srt"));QVERIFY(large.open(QIODevice::WriteOnly));QVERIFY(large.resize(8*1024*1024+1));large.close();QVERIFY(!readSrtFile(large.fileName()).valid());
    }
    void standardStylesBecomePlainTextWithoutLosingWords(){
        QTemporaryDir folder;const auto text=QString::fromUtf8("1\n00:00:01,000 --> 00:00:03,000\n{\\an8}<i>合成</i> &amp; &#9834; <font color=red>한글</font><br>é &lt;3 {literal} <|owned_literal|> �\n");
        const auto result=readSrtFile(save(folder,"styled.srt",text.toUtf8()));QVERIFY2(result.valid(),qPrintable(result.error));QCOMPARE(result.cues[0].text,QString::fromUtf8("合成 & ♪ 한글\né <3 {literal} <|owned_literal|> �"));
    }
    void nativeFormatsBecomePlainTimedCaptions_data(){
        QTest::addColumn<QString>("suffix");QTest::addColumn<QByteArray>("bytes");QTest::addColumn<QString>("words");
        const auto words=QString::fromUtf8("合成 한글 が 👩‍👩‍👧‍👦 ♪");
        QTest::newRow("VTT")<<QString("vtt")<<(QString("WEBVTT\n\n00:00:01.000 --> 00:00:03.000\n<i>")+words+"</i>\n").toUtf8()<<words;
        const auto ass=QString("[Script Info]\nScriptType: v4.00+\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\an8}")+words+"\\Nsecond line\n";
        QTest::newRow("ASS")<<QString("ass")<<ass.toUtf8()<<(words+"\nsecond line");
        const auto ssa=QString("[Script Info]\nScriptType: v4.00\n[Events]\nFormat: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\nDialogue: Marked=0,0:00:01.00,0:00:03.00,Default,,0,0,0,,")+words+"\n";
        QTest::newRow("SSA")<<QString("ssa")<<ssa.toUtf8()<<words;
        QTest::newRow("SMI")<<QString("smi")<<smi(words.toUtf8(),"UTF-8")<<words;
        const auto vtt=QString("WEBVTT\n\n00:00:01.000 --> 00:00:03.000\n")+words+"\n";
        QTest::newRow("UTF16-VTT")<<QString("vtt")<<utf16(vtt,true)<<words;
        const auto korean=QByteArray::fromHex("bec8b3e7c7cfbcbcbfe4"),japanese=QByteArray::fromHex("82b182f182c982bf82cd");
        QTest::newRow("CP949-SMI")<<QString("smi")<<smi(korean,"EUC-KR")<<QString::fromUtf8("안녕하세요");
        QTest::newRow("ShiftJIS-SMI")<<QString("smi")<<smi(japanese,"Shift_JIS")<<QString::fromUtf8("こんにちは");
        const auto prefix=QByteArray("1\n00:00:01,000 --> 00:00:03,000\n");
        QTest::newRow("CP949-SRT")<<QString("srt")<<(prefix+korean+"\n")<<QString::fromUtf8("안녕하세요");
        QTest::newRow("ShiftJIS-SRT")<<QString("srt")<<(prefix+japanese+"\n")<<QString::fromUtf8("こんにちは");
        QTest::newRow("ShiftJIS-kanji-SRT")<<QString("srt")<<(prefix+QByteArray::fromHex("93fa967b8cea")+"\n")<<QString::fromUtf8("日本語");
    }
    void nativeFormatsBecomePlainTimedCaptions(){
        if(!nativeFfmpegAvailable())QSKIP("Set LUMA_SUBTITLE_FILE_RUNTIME to the bundled FFmpeg runtime (Linux also supports system FFmpeg).");
        QFETCH(QString,suffix);QFETCH(QByteArray,bytes);QFETCH(QString,words);QTemporaryDir folder;SubtitleFileLoader loader(runtimePath());QSignalSpy ready(&loader,&SubtitleFileLoader::ready),failed(&loader,&SubtitleFileLoader::failed);
        const auto generation=loader.load(save(folder,"owned."+suffix,bytes));QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failed.isEmpty(),12000);QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed[0][1].toString()));QCOMPARE(ready.size(),1);QCOMPARE(ready[0][0].toULongLong(),generation);
        const auto cues=qvariant_cast<QVector<SubtitleCue>>(ready[0][1]);QCOMPARE(cues.size(),1);QCOMPARE(cues[0].startMs,1000);QCOMPARE(cues[0].endMs,3000);QCOMPARE(cues[0].text,words);
    }
    void invalidInputsFailWithoutRunningAnExternalProtocol(){
        QTemporaryDir folder;SubtitleFileLoader loader(folder.path());QSignalSpy ready(&loader,&SubtitleFileLoader::ready),failed(&loader,&SubtitleFileLoader::failed);
        const QStringList paths={"https://example.invalid/captions.srt","pipe:0",save(folder,"unknown.bin","words"),save(folder,"broken.srt","1\n00:00:01,000 --> 00:00:03,000\nGood\n\nbad block\n")};
        for(const auto &path:paths){const auto generation=loader.load(path);QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,1000);QCOMPARE(failed[0][0].toULongLong(),generation);QVERIFY(ready.isEmpty());failed.clear();}
    }
    void missingToolIsRecoverableAndSrtDoesNotNeedIt(){
        QTemporaryDir folder;Environment path("PATH",folder.path().toUtf8());SubtitleFileLoader loader(folder.path());QSignalSpy ready(&loader,&SubtitleFileLoader::ready),failed(&loader,&SubtitleFileLoader::failed);
        loader.load(save(folder,"owned.vtt","WEBVTT\n\n00:00:01.000 --> 00:00:03.000\nOwned\n"));QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,1000);
        const auto generation=loader.load(save(folder,"owned.srt","1\n00:00:01,000 --> 00:00:03,000\nOwned\n"));QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,1000);QCOMPARE(ready[0][0].toULongLong(),generation);QCOMPARE(failed.size(),1);
    }
    void cancelAndReplacementNeverPublishStaleCaptions(){
        QTemporaryDir folder;const auto runtime=fixtureRuntime(folder);QVERIFY(!runtime.isEmpty());const auto marker=folder.filePath("started");Environment env("LUMA_SUBTITLE_FIXTURE_STARTED",marker.toUtf8());SubtitleFileLoader loader(runtime);QSignalSpy ready(&loader,&SubtitleFileLoader::ready),failed(&loader,&SubtitleFileLoader::failed);
        loader.load(save(folder,"slow.vtt","STALL_IMPORT"));QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker),3000);loader.cancel();const auto cancelled=loader.generation();QTest::qWait(100);QVERIFY(ready.isEmpty());QVERIFY(failed.isEmpty());
        const auto generation=loader.load(save(folder,"new.srt","1\n00:00:01,000 --> 00:00:03,000\nNewest\n"));QVERIFY(generation>cancelled);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,1000);QCOMPARE(ready[0][0].toULongLong(),generation);QTest::qWait(100);QVERIFY(failed.isEmpty());
        ready.clear();loader.load(folder.filePath("new.srt"));loader.cancel();QTest::qWait(50);QVERIFY(ready.isEmpty());
    }
    void badOrExcessiveConverterOutputFails_data(){QTest::addColumn<QByteArray>("marker");QTest::newRow("malformedUTF8")<<QByteArray("MALFORMED_OUTPUT");QTest::newRow("outputLimit")<<QByteArray("OVERSIZE_OUTPUT");}
    void badOrExcessiveConverterOutputFails(){
        QFETCH(QByteArray,marker);QTemporaryDir folder;const auto runtime=fixtureRuntime(folder);QVERIFY(!runtime.isEmpty());SubtitleFileLoader loader(runtime);QSignalSpy ready(&loader,&SubtitleFileLoader::ready),failed(&loader,&SubtitleFileLoader::failed);
        const auto generation=loader.load(save(folder,"owned.vtt",marker));QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,5000);QCOMPARE(failed[0][0].toULongLong(),generation);QVERIFY(!failed[0][1].toString().isEmpty());QVERIFY(ready.isEmpty());
    }
    void stalledConverterTimesOutAndRecovers(){
        QTemporaryDir folder;const auto runtime=fixtureRuntime(folder);QVERIFY(!runtime.isEmpty());SubtitleFileLoader loader(runtime);QSignalSpy ready(&loader,&SubtitleFileLoader::ready),failed(&loader,&SubtitleFileLoader::failed);
        const auto generation=loader.load(save(folder,"slow.vtt","STALL_IMPORT"));QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,12000);QCOMPARE(failed[0][0].toULongLong(),generation);QVERIFY(failed[0][1].toString().contains("10"));QVERIFY(ready.isEmpty());
        loader.load(save(folder,"okay.vtt","READY_OUTPUT"));QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,3000);QCOMPARE(failed.size(),1);
    }
};
int main(int argc,char **argv){QCoreApplication application(argc,argv);if(QFileInfo(QCoreApplication::applicationFilePath()).completeBaseName()=="ffmpeg")return runFixture();SubtitleFileTests tests;return QTest::qExec(&tests,argc,argv);}
#include "subtitle_file_tests.moc"
