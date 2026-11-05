#include "subtitle_file.h"
#include <QFile>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTimer>
#include <QUrl>
#include <QProcess>
#include <QPointer>
#include <QTemporaryDir>
#include <QStandardPaths>

namespace luma {
namespace {
constexpr qint64 maximumBytes=8*1024*1024;
QString localPath(const QString &input){
    const QUrl url(input);const bool drive=QRegularExpression("^[A-Za-z]:[\\\\/]").match(input).hasMatch();
    if(url.scheme().compare("file",Qt::CaseInsensitive)==0)return url.isValid() && (url.host().isEmpty() || url.host().compare("localhost",Qt::CaseInsensitive)==0)?url.toLocalFile():QString();
    if(!url.scheme().isEmpty() && !drive)return {};
    if(input.startsWith("\\\\") || input.startsWith("//"))return {};
    return input;
}
bool readLocal(const QString &input,QString &path,QByteArray &bytes,QString &error){
    path=localPath(input);if(path.isEmpty()){error="로컬 자막 파일만 열 수 있습니다.";return false;}
    const QFileInfo info(path);if(!info.isFile()){error="자막 파일을 찾을 수 없습니다.";return false;}
    path=info.absoluteFilePath();QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){error="자막 파일을 읽을 수 없습니다.";return false;}
    if(file.size()>maximumBytes){error="자막 파일이 8 MiB 제한을 초과했습니다.";return false;}
    bytes=file.read(maximumBytes+1);if(bytes.size()>maximumBytes || file.error()!=QFileDevice::NoError){error="자막 파일을 완전히 읽을 수 없습니다.";return false;}return true;
}
QString decodeUnicode(QByteArray bytes,QString &encoding,QString &error){
    auto code=QStringConverter::Utf8;encoding="UTF-8";
    if(bytes.startsWith(QByteArray::fromHex("efbbbf")))bytes.remove(0,3);
    else if(bytes.startsWith(QByteArray::fromHex("fffe"))){bytes.remove(0,2);code=QStringConverter::Utf16LE;encoding="UTF-16LE";}
    else if(bytes.startsWith(QByteArray::fromHex("feff"))){bytes.remove(0,2);code=QStringConverter::Utf16BE;encoding="UTF-16BE";}
    if(code==QStringConverter::Utf16LE || code==QStringConverter::Utf16BE){
        const bool little=code==QStringConverter::Utf16LE;bool invalid=bytes.size()%2!=0;
        const auto unit=[&](qsizetype offset){const auto a=quint8(bytes[offset]),b=quint8(bytes[offset+1]);return quint16(little?a|(b<<8):(a<<8)|b);};
        for(qsizetype offset=0;!invalid && offset<bytes.size();offset+=2){
            const auto current=unit(offset);
            if(QChar::isHighSurrogate(current)){
                if(offset+3>=bytes.size() || !QChar::isLowSurrogate(unit(offset+2)))invalid=true;else offset+=2;
            }else if(QChar::isLowSurrogate(current))invalid=true;
        }
        if(invalid){error=encoding+" 자막의 문자 인코딩이 올바르지 않습니다.";return {};}
    }
    QStringDecoder decoder(code,QStringConverter::Flag::Stateless);const QString text=decoder(bytes);
    if(decoder.hasError() || text.contains(QChar::Null)){error=encoding+" 자막의 문자 인코딩이 올바르지 않습니다.";return {};}
    return text;
}
QString plainText(QString text){
    static const QRegularExpression br("<\\s*br\\s*/?\\s*>",QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags("<\\s*/?\\s*(?:font|span|ruby|lang|rt|[biuscv])(?=[\\s>./])[^>]*>",QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression ass(R"(\{\\(?:an[1-9]|[ibus][01]|[1234]?[ca]|pos|move|fad|fade|fs|fn|fr|fsc|bord|shad|blur|r|t)[^}]*\})",QRegularExpression::CaseInsensitiveOption);
    text.replace(br,"\n");text.remove(tags);text.remove(ass);
    static const QRegularExpression entity("&(amp|lt|gt|quot|apos|nbsp|#x[0-9a-f]+|#[0-9]+);",QRegularExpression::CaseInsensitiveOption);
    QString result;qsizetype previous=0;auto matches=entity.globalMatch(text);
    while(matches.hasNext()){
        const auto match=matches.next();result+=text.mid(previous,match.capturedStart()-previous);const auto name=match.captured(1).toLower();QString replacement;
        if(name=="amp")replacement="&";else if(name=="lt")replacement="<";else if(name=="gt")replacement=">";else if(name=="quot")replacement='"';else if(name=="apos")replacement='\'';else if(name=="nbsp")replacement=' ';
        else{bool ok=false;const auto value=name.startsWith("#x")?name.mid(2).toUInt(&ok,16):name.mid(1).toUInt(&ok,10);if(ok && value>0 && value<=0x10ffff && !(value>=0xd800 && value<=0xdfff)){const char32_t point=value;replacement=QString::fromUcs4(&point,1);}}
        result+=replacement.isEmpty()?match.captured():replacement;previous=match.capturedEnd();
    }
    result+=text.mid(previous);return result.trimmed();
}
SubtitleFileResult parseDecoded(QString text,const QString &encoding){
    SubtitleFileResult result;result.encoding=encoding;text.replace("\r\n","\n");text.replace('\r','\n');
    static const QRegularExpression blank("\n[\\t ]*\n"),index("^[0-9]+$");
    static const QRegularExpression timing(R"(^(\d{1,3}):([0-5]\d):([0-5]\d)[,.](\d{3})\s+-->\s+(\d{1,3}):([0-5]\d):([0-5]\d)[,.](\d{3})\s*$)");
    for(const auto &block:text.split(blank,Qt::SkipEmptyParts)){
        const auto lines=block.trimmed().split('\n');const int row=lines.isEmpty()?0:lines[0].contains("-->")?0:1;
        const auto fail=[&](){result.cues.clear();result.error="SRT 자막의 시간 또는 본문 형식이 올바르지 않습니다.";return result;};
        if(lines.size()<=row+1 || (row==1 && !index.match(lines[0].trimmed()).hasMatch()))return fail();
        const auto match=timing.match(lines[row].trimmed());if(!match.hasMatch())return fail();
        const auto time=[&](int offset){return ((match.captured(offset).toLongLong()*60+match.captured(offset+1).toLongLong())*60+match.captured(offset+2).toLongLong())*1000+match.captured(offset+3).toLongLong();};
        const qint64 start=time(1),end=time(5);if(end<=start)return fail();const auto words=plainText(lines.mid(row+1).join('\n'));
        if(!words.isEmpty())result.cues.append({start,end,words});
    }
    if(result.cues.isEmpty())result.error="표시할 SRT 자막 본문이 없습니다.";return result;
}
}
SubtitleFileResult readSrtFile(const QString &input){
    QString path,error,encoding;QByteArray bytes;if(!readLocal(input,path,bytes,error))return {{},error,{}};
    const auto text=decodeUnicode(bytes,encoding,error);if(!error.isEmpty())return {{},error,encoding};return parseDecoded(text,encoding);
}
namespace {
QString declaredCharset(const QString &text){
    static const QRegularExpression declaration(R"(charset\s*=\s*["']?\s*([a-z0-9._-]{1,40}))",QRegularExpression::CaseInsensitiveOption);
    return declaration.match(text.left(65536)).captured(1).toLower();
}
QString canonicalCharset(QString name){
    name.remove('-');name.remove('_');name.remove('.');
    if(name=="utf8")return "UTF-8";
    if(name=="cp949" || name=="windows949" || name=="euckr" || name=="ksc56011987" || name=="ksc5601" || name=="uhc")return "CP949";
    if(name=="shiftjis" || name=="sjis" || name=="cp932" || name=="windows31j" || name=="mskanji")return "CP932";
    return {};
}
QStringList legacyCharsets(const QByteArray &bytes){
    // Without a declared charset the two encodings can be ambiguous. Prefer
    // recognizable Hangul/kana pairs, then let iconv reject an invalid choice.
    const auto validKorean=[&]{
        for(qsizetype i=0;i<bytes.size();++i){
            const auto first=quint8(bytes[i]);if(first<0x80)continue;
            if(first<0x81 || first>0xfe || i+1>=bytes.size())return false;
            const auto second=quint8(bytes[++i]);
            if(first<=0xc6){if(!((second>=0x41 && second<=0x5a) || (second>=0x61 && second<=0x7a) || (second>=0x81 && second<=0xfe)))return false;}
            else if(second<0xa1 || second>0xfe)return false;
        }return true;
    };
    const auto validJapanese=[&]{
        for(qsizetype i=0;i<bytes.size();++i){
            const auto first=quint8(bytes[i]);if(first<0x80 || (first>=0xa1 && first<=0xdf))continue;
            if(!((first>=0x81 && first<=0x9f) || (first>=0xe0 && first<=0xfc)) || i+1>=bytes.size())return false;
            const auto second=quint8(bytes[++i]);if(!((second>=0x40 && second<=0x7e) || (second>=0x80 && second<=0xfc)))return false;
        }return true;
    };
    const bool koreanValid=validKorean(),japaneseValid=validJapanese();
    if(!koreanValid)return japaneseValid?QStringList{"CP932"}:QStringList{};
    if(!japaneseValid)return {"CP949"};
    int korean=0,japanese=0;
    for(qsizetype i=0;i+1<bytes.size();++i){
        const auto first=quint8(bytes[i]),second=quint8(bytes[i+1]);
        if(first>=0xb0 && first<=0xc8 && second>=0xa1 && second<=0xfe)++korean;
        if(first==0x82 && second>=0x9f && second<=0xf1)japanese+=3;
        if(first==0x83 && second>=0x40 && second<=0x96)japanese+=2;
        if(first>=0x81 && first<=0x9f && ((second>=0x40 && second<=0x7e) || (second>=0x80 && second<=0xfc)))++japanese;
    }
    return japanese>korean?QStringList{"CP932","CP949"}:QStringList{"CP949","CP932"};
}
QString ffmpegPath(const QString &runtime){
#ifdef Q_OS_WIN
    const auto bundled=QDir(runtime).filePath("tools/ffmpeg.exe");
#else
    const auto bundled=QDir(runtime).filePath("tools/ffmpeg");
#endif
    if(QFileInfo(bundled).isFile())return QFileInfo(bundled).absoluteFilePath();
#ifdef Q_OS_LINUX
    return QStandardPaths::findExecutable("ffmpeg");
#else
    return {};
#endif
}
}
struct SubtitleFileLoader::Impl {
    SubtitleFileLoader *owner;
    QString runtime,program,inputPath,format;
    QStringList charsets;
    quint64 generation=0;
    int attempt=0;
    bool active=false;
    qint64 outputBytes=0;
    QPointer<QProcess> process;
    QTimer deadline;
    std::unique_ptr<QTemporaryDir> temporary;
    QFile output;

    Impl(SubtitleFileLoader *parent,QString directory):owner(parent),runtime(std::move(directory)){
        if(runtime.isEmpty())runtime=QCoreApplication::applicationDirPath()+"/runtime";
        deadline.setSingleShot(true);
        deadline.setTimerType(Qt::PreciseTimer);
        QObject::connect(&deadline,&QTimer::timeout,owner,[this]{fail("자막 변환이 10초 안에 완료되지 않았습니다.");});
    }
    void stopProcess(){
        auto *previous=process.data();process=nullptr;
        if(previous){
            QObject::disconnect(previous,nullptr,owner,nullptr);
            if(previous->state()!=QProcess::NotRunning){previous->kill();previous->waitForFinished(250);}
            previous->deleteLater();
        }
    }
    void clear(){
        deadline.stop();stopProcess();output.close();temporary.reset();inputPath.clear();charsets.clear();outputBytes=0;active=false;
    }
    void fail(QString message){
        if(!active)return;const auto current=generation;clear();emit owner->failed(current,std::move(message));
    }
    void succeed(QVector<SubtitleCue> cues){
        if(!active)return;const auto current=generation;clear();emit owner->ready(current,std::move(cues));
    }
    void queuedFailure(QString message){
        const auto current=generation;QTimer::singleShot(0,owner,[this,current,message=std::move(message)]{if(active && generation==current)fail(message);});
    }
    void queuedSuccess(QVector<SubtitleCue> cues){
        const auto current=generation;QTimer::singleShot(0,owner,[this,current,cues=std::move(cues)]{if(active && generation==current)succeed(cues);});
    }
    bool current(QProcess *candidate,quint64 session)const{return active && generation==session && process==candidate;}
    void drain(QProcess *candidate,quint64 session){
        if(!current(candidate,session))return;
        candidate->setReadChannel(QProcess::StandardOutput);
        while(candidate->bytesAvailable()>0){
            const auto bytes=candidate->read(65536);
            if(bytes.size()>maximumBytes-outputBytes){fail("변환된 자막이 8 MiB 제한을 초과했습니다.");return;}
            if(output.write(bytes)!=bytes.size()){fail("변환된 자막을 저장할 수 없습니다.");return;}
            outputBytes+=bytes.size();
        }
    }
    void startAttempt(){
        output.close();output.setFileName(temporary->filePath("normalized.srt"));outputBytes=0;
        if(!output.open(QIODevice::WriteOnly|QIODevice::Truncate)){fail("자막 임시 파일을 만들 수 없습니다.");return;}
        auto *candidate=new QProcess(owner);process=candidate;const auto session=generation;
        candidate->setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(candidate,&QProcess::readyReadStandardOutput,owner,[this,candidate,session]{drain(candidate,session);});
        QObject::connect(candidate,&QProcess::readyReadStandardError,owner,[this,candidate,session]{
            if(!current(candidate,session))return;
            candidate->setReadChannel(QProcess::StandardError);while(candidate->bytesAvailable()>0)candidate->read(65536);candidate->setReadChannel(QProcess::StandardOutput);
        });
        QObject::connect(candidate,&QProcess::errorOccurred,owner,[this,candidate,session](QProcess::ProcessError error){
            if(current(candidate,session) && error==QProcess::FailedToStart)fail("FFmpeg 자막 변환 도구를 실행할 수 없습니다.");
        });
        QObject::connect(candidate,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),owner,[this,candidate,session](int code,QProcess::ExitStatus status){
            if(!current(candidate,session))return;drain(candidate,session);if(!current(candidate,session))return;
            output.close();SubtitleFileResult result;
            if(status==QProcess::NormalExit && code==0)result=readSrtFile(output.fileName());
            stopProcess();
            if(result.valid()){succeed(std::move(result.cues));return;}
            if(++attempt<charsets.size()){startAttempt();return;}
            fail("자막의 형식이나 문자 인코딩을 변환할 수 없습니다.");
        });
        candidate->start(program,{"-hide_banner","-loglevel","error","-nostdin","-protocol_whitelist","file,pipe","-sub_charenc",charsets[attempt],"-f",format,"-i",inputPath,"-map","0:s:0","-c:s","srt","-f","srt","pipe:1"},QIODevice::ReadOnly);
    }
    void load(const QString &input){
        active=true;QString path,error,encoding;QByteArray bytes;
        if(!readLocal(input,path,bytes,error)){queuedFailure(error);return;}
        const auto suffix=QFileInfo(path).suffix().toLower();
        if(suffix=="srt")format="srt";else if(suffix=="vtt")format="webvtt";else if(suffix=="ass" || suffix=="ssa")format="ass";else if(suffix=="smi" || suffix=="sami")format="sami";
        else{queuedFailure("지원하는 자막 형식은 SRT, ASS, SSA, VTT, SMI입니다.");return;}
        const bool bom=bytes.startsWith(QByteArray::fromHex("efbbbf")) || bytes.startsWith(QByteArray::fromHex("fffe")) || bytes.startsWith(QByteArray::fromHex("feff"));
        const auto text=decodeUnicode(bytes,encoding,error);
        if(suffix=="srt" && error.isEmpty()){
            auto result=parseDecoded(text,encoding);if(result.valid())queuedSuccess(std::move(result.cues));else queuedFailure(result.error);return;
        }
        if(!error.isEmpty() && (bom || bytes.contains('\0'))){queuedFailure(error);return;}
        const auto declaration=format=="sami"?declaredCharset(error.isEmpty()?text:QString::fromLatin1(bytes)):QString();
        if(!declaration.isEmpty()){
            const auto charset=canonicalCharset(declaration);
            if(charset.isEmpty()){queuedFailure("SMI에 지정된 문자 인코딩을 지원하지 않습니다.");return;}
            if(charset=="UTF-8" && !error.isEmpty()){queuedFailure(error);return;}
            charsets={charset};
        }else if(error.isEmpty())charsets={"UTF-8"};
        else charsets=legacyCharsets(bytes);
        if(charsets.isEmpty()){queuedFailure("자막의 문자 인코딩을 확인할 수 없습니다. UTF-8 또는 UTF-16으로 저장해 주세요.");return;}
        if(suffix=="srt" && !parseDecoded(QString::fromLatin1(bytes),"legacy").valid()){
            queuedFailure("SRT 자막의 시간 또는 본문 형식이 올바르지 않습니다.");return;
        }
        if(error.isEmpty() && (charsets[0]=="UTF-8" || encoding.startsWith("UTF-16"))){bytes=text.toUtf8();charsets={"UTF-8"};}
        if(bytes.size()>maximumBytes){queuedFailure("변환할 자막이 8 MiB 제한을 초과했습니다.");return;}
        program=ffmpegPath(runtime);if(program.isEmpty()){queuedFailure("FFmpeg 자막 변환 도구가 없습니다. 런타임을 확인해 주세요.");return;}
        temporary=std::make_unique<QTemporaryDir>();if(!temporary->isValid()){queuedFailure("자막 임시 폴더를 만들 수 없습니다.");return;}
        inputPath=temporary->filePath("input."+suffix);QFile snapshot(inputPath);
        if(!snapshot.open(QIODevice::WriteOnly) || snapshot.write(bytes)!=bytes.size()){queuedFailure("자막 임시 파일을 저장할 수 없습니다.");return;}snapshot.close();
        attempt=0;deadline.start(10000);startAttempt();
    }
};
SubtitleFileLoader::SubtitleFileLoader(QString runtime,QObject *parent):QObject(parent),d(std::make_unique<Impl>(this,std::move(runtime))){}
SubtitleFileLoader::~SubtitleFileLoader(){d->clear();}
quint64 SubtitleFileLoader::load(QString path){
    cancel();const auto current=d->generation;
    QTimer::singleShot(0,this,[this,current,path=std::move(path)]{if(current==d->generation)d->load(path);});
    return current;
}
void SubtitleFileLoader::cancel(){++d->generation;d->clear();}
quint64 SubtitleFileLoader::generation()const{return d->generation;}
}
