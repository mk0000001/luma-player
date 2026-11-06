#include "caption_preparation.h"
#include "translation_service.h"
#include "translation_support.h"
#include "core/disc.h"
#include "luma_version.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QStringDecoder>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
namespace luma {
namespace {
constexpr qint64 dayMs=86400000,chunkMs=30000,projectLimit=64*1024*1024;
constexpr qsizetype cueLimit=100000,tokenLimit=1000000;
const QString projectName="luma-caption-project.json";
QByteArray digest(const QByteArray &bytes){return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);}
QByteArray objectDigest(const QJsonObject &object){return digest(QJsonDocument(object).toJson(QJsonDocument::Compact));}
bool reject(QString &error,const QString &message){error=message;return false;}
bool unicode(const QString &text,bool breaks=true){
    for(qsizetype i=0;i<text.size();++i){char32_t point=text[i].unicode();
        if(text[i].isHighSurrogate()){if(++i>=text.size()||!text[i].isLowSurrogate())return false;point=QChar::surrogateToUcs4(text[i-1],text[i]);}else if(text[i].isLowSurrogate())return false;
        if(point==0xfffd||(point<32&&(!breaks||(point!=9&&point!=10&&point!=13)))||(point>=0x7f&&point<=0x9f)||point==0x061c||point==0x200e||point==0x200f||(point>=0x202a&&point<=0x202e)||(point>=0x2066&&point<=0x2069))return false;
    }return true;
}
bool integer(const QJsonObject &object,const QString &key,qint64 minimum,qint64 maximum,qint64 &out){
    const auto value=object.value(key);const auto number=value.toDouble(-1);
    if(!value.isDouble()||!std::isfinite(number)||number<minimum||number>maximum||std::floor(number)!=number)return false;
    out=static_cast<qint64>(number);return true;
}
bool localPath(const QString &path){return !path.isEmpty()&&path.size()<=4096&&unicode(path,false)&&QFileInfo(path).isAbsolute()&&!path.contains("://");}
std::optional<QJsonObject> fileMetadata(const QString &path,bool required,QString &error){
    const QFileInfo file(path);
    if(!file.isFile()){if(required){reject(error,"필요한 로컬 파일을 찾을 수 없습니다: "+path);return {};}return QJsonObject{{"path",file.absoluteFilePath()},{"exists",false}};}
    const auto canonical=file.canonicalFilePath();if(canonical.isEmpty())return {};
    return QJsonObject{{"path",canonical},{"exists",true},{"bytes",file.size()},{"mtime_ms",file.lastModified().toMSecsSinceEpoch()}};
}
std::optional<QJsonObject> hashedMetadata(const QString &path,qint64 limit,QString &error){
    auto record=fileMetadata(path,true,error);if(!record)return {};
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>limit){reject(error,"디스크 메타데이터를 안전하게 읽을 수 없습니다.");return {};}
    record->insert("sha256",QString::fromLatin1(digest(file.readAll()).toHex()));return record;
}
std::optional<QJsonObject> sourceMetadata(const CaptionPreparationRequest &request,QString &error){
    const auto &source=request.source;
    if(!localPath(source.source)){reject(error,"로컬 영상 파일 또는 Blu-ray 폴더를 선택하세요.");return {};}
    const QFileInfo input(source.source);const auto canonical=input.canonicalFilePath();
    if(canonical.isEmpty()){reject(error,"원본 미디어를 찾을 수 없습니다.");return {};}
    if(!source.disc){if(!input.isFile()){reject(error,"영상 원본은 로컬 파일이어야 합니다.");return {};}const auto record=fileMetadata(canonical,true,error);if(!record)return {};return QJsonObject{{"kind","file"},{"source",*record}};}
    if(!input.isDir()||!QRegularExpression("^[0-9]{5}$").match(source.playlist).hasMatch()){reject(error,"Blu-ray 폴더와 다섯 자리 플레이리스트를 확인하세요.");return {};}
    const QDir root(canonical);
    const auto index=hashedMetadata(root.filePath("BDMV/index.bdmv"),16*1024*1024,error);
    const auto playlist=hashedMetadata(root.filePath("BDMV/PLAYLIST/"+source.playlist+".mpls"),8*1024*1024,error);if(!index||!playlist)return {};
    QFile indexFile(root.filePath("BDMV/index.bdmv"));
    if(!indexFile.open(QIODevice::ReadOnly)||!indexFile.read(8).startsWith("INDX")){reject(error,"유효한 Blu-ray 인덱스가 아닙니다.");return {};}
    QFile file(root.filePath("BDMV/PLAYLIST/"+source.playlist+".mpls"));if(!file.open(QIODevice::ReadOnly))return {};
    const auto title=parsePlaylist(file.readAll(),source.playlist);
    if(title.durationMs!=request.durationMs||title.clips.isEmpty()||title.clips.size()>10000){reject(error,"선택한 Blu-ray 플레이리스트의 전체 길이와 클립을 확인하세요.");return {};}
    QJsonArray clips;QSet<QString> seen;
    for(const auto &clip:title.clips){if(seen.contains(clip))continue;seen.insert(clip);
        const auto stream=fileMetadata(root.filePath("BDMV/STREAM/"+clip+".m2ts"),true,error);
        const auto info=fileMetadata(root.filePath("BDMV/CLIPINF/"+clip+".clpi"),true,error);if(!stream||!info)return {};
        clips.append(QJsonObject{{"id",clip},{"stream",*stream},{"info",*info}});
    }return QJsonObject{{"kind","bluray"},{"path",canonical},{"playlist",source.playlist},{"index",*index},{"mpls",*playlist},{"clips",clips}};
}
QJsonObject preferencesJson(const AiPreferences &p){return {{"mode",p.mode},{"gpu_device",p.gpuDevice}};}
bool validPreferences(const AiPreferences &p){return QStringList{"auto","cpu","gpu","npu"}.contains(p.mode)&&p.gpuDevice.size()<=256&&unicode(p.gpuDevice,false);}
QString acceleratorRejection(const QString &message){
    static const QRegularExpression pattern("^(GPU|NPU) 사용 불가 \\((시간 초과|처리 실패|실행할 수 없음|가속 실행 확인 실패|자막 결과 없음|모델 준비 시간 초과)\\) · 다음 장치로 전환 중…$");
    const auto match=pattern.match(message);return match.hasMatch()?match.captured(1)+" 사용 불가 ("+match.captured(2)+')':QString{};
}
QJsonObject requestJson(const CaptionPreparationRequest &request){
    const auto &s=request.source;
    return {{"source",s.source},{"disc",s.disc},{"playlist",s.playlist},{"language",s.language},{"title",s.title},{"audio_index",s.audioIndex},{"audio_track_id",s.audioTrackId},{"duration_ms",request.durationMs},{"model",request.model},{"translation_model",request.translationModel},{"output_directory",request.outputDirectory},{"translate",request.translate},{"recognition_preferences",preferencesJson(request.recognitionPreferences)},{"translation_preferences",preferencesJson(request.translationPreferences)}};
}
bool validRequest(const CaptionPreparationRequest &request,QString &error){
    const auto &s=request.source;
    if(request.durationMs<=0||request.durationMs>dayMs||s.startMs!=0||s.audioIndex<0||s.audioIndex>1024||s.audioTrackId< -1||s.audioTrackId>65535||s.title<=0||s.title>99999)return reject(error,"전체 길이와 오디오 선택이 잘못되었습니다 (최대 24시간).");
    if(!QRegularExpression("^(?:auto|[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})?)$").match(s.language).hasMatch()||!validPreferences(request.recognitionPreferences)||!validPreferences(request.translationPreferences))return reject(error,"인식 언어 또는 장치 설정이 잘못되었습니다.");
    if((s.disc&&!QRegularExpression("^[0-9]{5}$").match(s.playlist).hasMatch())||(!s.disc&&!s.playlist.isEmpty()))return reject(error,"플레이리스트 선택이 잘못되었습니다.");
    if(!localPath(s.source)||!localPath(request.outputDirectory)||!localPath(request.model)||QFileInfo(request.model).suffix().compare("bin",Qt::CaseInsensitive)!=0)return reject(error,"원본·저장 폴더·인식 모델은 로컬 경로여야 합니다.");
    if((request.translate||!request.translationModel.isEmpty())&&(!localPath(request.translationModel)||QFileInfo(request.translationModel).suffix().compare("gguf",Qt::CaseInsensitive)!=0))return reject(error,"번역 모델 경로가 잘못되었습니다.");
    return true;
}
std::optional<CaptionPreparationRequest> readRequest(const QJsonObject &object,QString &error){
    CaptionPreparationRequest r;auto &s=r.source;qint64 title=0,index=0,track=0;
    for(const auto &key:{"source","playlist","language","model","translation_model","output_directory"})if(!object.value(key).isString())return {};
    if(!object.value("disc").isBool()||!object.value("translate").isBool()||!integer(object,"duration_ms",1,dayMs,r.durationMs)||!integer(object,"title",1,99999,title)||!integer(object,"audio_index",0,1024,index)||!integer(object,"audio_track_id",-1,65535,track))return {};
    s.source=object["source"].toString();s.disc=object["disc"].toBool();s.playlist=object["playlist"].toString();s.language=object["language"].toString();s.title=int(title);s.audioIndex=int(index);s.audioTrackId=int(track);
    r.model=object["model"].toString();r.translationModel=object["translation_model"].toString();r.outputDirectory=object["output_directory"].toString();r.translate=object["translate"].toBool();
    const auto asr=object["recognition_preferences"].toObject(),translation=object["translation_preferences"].toObject();
    r.recognitionPreferences={asr["mode"].toString(),asr["gpu_device"].toString()};r.translationPreferences={translation["mode"].toString(),translation["gpu_device"].toString()};
    if(!validRequest(r,error)||requestJson(r)!=object)return {};return r;
}
std::optional<QJsonObject> jobIdentity(const CaptionPreparationRequest &request,const QString &runtime,QString &error){
    const auto model=fileMetadata(request.model,true,error);if(!model)return {};
    QJsonObject result{{"app_version",LUMA_VERSION},{"request",requestJson(request)},{"model",*model},{"page_max_units",60},{"page_min_ms",900}};
    if(request.translate){const auto translation=fileMetadata(request.translationModel,true,error);if(!translation)return {};result.insert("translation_model",*translation);}
#ifdef Q_OS_WIN
    const QString suffix=".exe";
#else
    const QString suffix;
#endif
    QJsonArray tools;
    for(const auto &relative:{"tools/ffmpeg","whisper/whisper-cli","ai/luma-whisper-worker","translation/cuda/llama-server","translation/vulkan/llama-server","translation/openvino/llama-server"}){
        const auto record=fileMetadata(QDir(runtime).filePath(QString(relative)+suffix),false,error);if(!record)return {};tools.append(*record);
    }
    const QFileInfo asr(request.model);const auto stem=asr.dir().filePath(asr.completeBaseName()+"-encoder-openvino");
    for(const auto &extension:{".xml",".bin",".json"}){const auto record=fileMetadata(stem+extension,false,error);if(!record)return {};tools.append(*record);}
    // Canonical path/size/mtime detect ordinary runtime replacement. This is
    // bounded cache metadata, not a cryptographic integrity check of libraries.
    QJsonArray libraries;
    for(const auto &relative:{"ai","whisper","translation/cuda","translation/vulkan","translation/openvino","cuda"}){
        const auto directory=QDir(runtime).filePath(relative);QStringList names;
        QDirIterator files(directory,{"*.dll","*.so","*.so.*","*.dylib"},QDir::Files,QDirIterator::NoIteratorFlags);
        while(files.hasNext()){files.next();names.append(files.fileName());if(names.size()+libraries.size()>256){reject(error,"로컬 실행기 라이브러리 수가 안전한 검사 한도를 초과했습니다.");return {};}}
        std::sort(names.begin(),names.end());
        for(const auto &name:names){auto record=fileMetadata(QDir(directory).filePath(name),true,error);if(!record)return {};record->insert("relative",QString(relative)+'/'+name);libraries.append(*record);}
    }
    result.insert("runtime",QFileInfo(runtime).canonicalFilePath());result.insert("workers",tools);result.insert("libraries",libraries);
    if(QJsonDocument(result).toJson(QJsonDocument::Compact).size()>1024*1024){reject(error,"실행기 정체성 정보의 검사 한도를 초과했습니다.");return {};}
    return result;
}
QJsonObject cueJson(const SubtitleCue &cue){
    QJsonArray tokens;for(const auto &t:cue.tokens)tokens.append(QJsonObject{{"text_start",qint64(t.textStart)},{"text_length",qint64(t.textLength)},{"start_ms",t.startMs},{"end_ms",t.endMs}});
    return {{"start_ms",cue.startMs},{"end_ms",cue.endMs},{"text",cue.text},{"tokens",tokens}};
}
bool validCue(const SubtitleCue &cue,qint64 from,qint64 until){
    if(cue.startMs<from||cue.endMs<=cue.startMs||cue.endMs>until||cue.text.trimmed().isEmpty()||cue.text.size()>16384||!unicode(cue.text)||cue.tokens.size()>8192)return false;
    qsizetype next=0;qint64 previous=cue.startMs;
    for(const auto &t:cue.tokens){if(t.textStart!=next||t.textLength<=0||t.textLength>cue.text.size()-next||t.startMs<previous||t.endMs<=t.startMs||t.endMs>cue.endMs)return false;
        next+=t.textLength;if(next<cue.text.size()&&cue.text[next].isLowSurrogate())return false;previous=t.endMs;
    }return cue.tokens.isEmpty()||next==cue.text.size();
}
std::optional<SubtitleCue> readCue(const QJsonObject &object,qint64 until){
    SubtitleCue cue;
    if(!integer(object,"start_ms",0,until,cue.startMs)||!integer(object,"end_ms",0,until,cue.endMs)||!object.value("text").isString()||!object.value("tokens").isArray())return {};
    cue.text=object["text"].toString();const auto tokens=object["tokens"].toArray();if(tokens.size()>8192)return {};
    for(const auto &entry:tokens){if(!entry.isObject())return {};const auto token=entry.toObject();qint64 start=0,length=0,from=0,end=0;
        if(!integer(token,"text_start",0,cue.text.size(),start)||!integer(token,"text_length",1,cue.text.size(),length)||!integer(token,"start_ms",0,until,from)||!integer(token,"end_ms",0,until,end))return {};
        cue.tokens.append({qsizetype(start),qsizetype(length),from,end});
    }
    if(!validCue(cue,0,until)||cueJson(cue)!=object)return {};return cue;
}
bool validKorean(const QString &korean,const SubtitleCue &page,const QString &model){
    const auto profile=translationProfileForModel(model);
    const auto content=profile==TranslationProfile::HyMt2Plain?korean:QString::fromUtf8(QJsonDocument(QJsonObject{{"korean",korean}}).toJson(QJsonDocument::Compact));
    const auto response=QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{{"finish_reason","stop"},{"message",QJsonObject{{"content",content}}}}}}}).toJson(QJsonDocument::Compact);
    return !korean.isEmpty()&&parseTranslationResponse(response,nullptr,profile,page.text)==korean;
}
bool atomicWrite(const QString &path,const QByteArray &bytes,QString &error){
    if(QFileInfo(path).isSymLink())return reject(error,"내보내기 경로의 심볼릭 링크는 덮어쓰지 않습니다.");
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return reject(error,"자막 프로젝트를 안전하게 저장하지 못했습니다.");
    return true;
}
struct Checkpoint {
    CaptionPreparationResult result;QJsonObject source,identity;QSet<QString> skipped;qint64 next=0;
    QString phase="recognition",recognitionBackend,translationBackend;
};
QVector<SubtitleCue> koreanCues(const Checkpoint &state){
    QVector<SubtitleCue> result;
    for(const auto &page:captionPages(state.result.originals)){const auto text=state.result.translations.value(translationCueKey(page));if(!text.isEmpty())result.append({page.startMs,page.endMs,text});}
    return result;
}
void paths(Checkpoint &state,const QString &directory){
    state.result.projectFile=QDir(directory).filePath(projectName);state.result.originalSrt=QDir(directory).filePath("original.srt");
    state.result.koreanSrt=state.result.request.translate?QDir(directory).filePath("korean.srt"):QString{};state.result.skippedTranslations=int(state.skipped.size());
}
bool saveCheckpoint(Checkpoint &state,QString &error){
    paths(state,state.result.request.outputDirectory);QJsonArray originals,translations,skipped;
    for(const auto &cue:state.result.originals)originals.append(cueJson(cue));
    for(const auto &page:captionPages(state.result.originals)){const auto key=translationCueKey(page);
        if(state.result.translations.contains(key))translations.append(QJsonObject{{"key",key},{"korean",state.result.translations.value(key)}});
        if(state.skipped.contains(key))skipped.append(key);
    }
    const QJsonObject root{{"schema",1},{"format","luma-caption-project-v1"},{"complete",state.phase=="complete"},{"phase",state.phase},{"next_chunk_ms",state.next},{"request",requestJson(state.result.request)},{"source_metadata",state.source},{"source_identity",QString::fromLatin1(state.result.sourceIdentity.toHex())},{"job_identity",state.identity},{"originals",originals},{"translations",translations},{"skipped",skipped},{"recognition_backend",state.recognitionBackend},{"translation_backend",state.translationBackend}};
    const auto bytes=QJsonDocument(root).toJson(QJsonDocument::Compact);
    if(bytes.size()>projectLimit)return reject(error,"자막 프로젝트의 저장 한도를 초과했습니다.");
    if(!atomicWrite(state.result.originalSrt,writeSrt(captionPages(state.result.originals)).toUtf8(),error))return false;
    if(state.result.request.translate&&!atomicWrite(state.result.koreanSrt,writeSrt(koreanCues(state)).toUtf8(),error))return false;
    return atomicWrite(state.result.projectFile,bytes,error);
}
std::optional<Checkpoint> readCheckpoint(const QString &path,bool requireComplete,QString &error){
    const QFileInfo info(path);
    if(!localPath(path)||!info.isFile()||info.isSymLink()||info.size()<=0||info.size()>projectLimit){reject(error,"프로젝트 파일이 없거나 입력 한도를 초과했습니다.");return {};}
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    const auto bytes=file.readAll();QStringDecoder decoder(QStringDecoder::Utf8);const auto decoded=decoder(bytes);Q_UNUSED(decoded);
    if(decoder.hasError()){reject(error,"프로젝트의 UTF-8 문자가 손상되었습니다.");return {};}
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError||!document.isObject()){reject(error,"프로젝트 JSON이 손상되었습니다.");return {};}
    const auto root=document.object();Checkpoint state;qint64 schema=0;
    const QSet<QString> allowed{"schema","format","complete","phase","next_chunk_ms","request","source_metadata","source_identity","job_identity","originals","translations","skipped","recognition_backend","translation_backend"};
    const auto keys=root.keys();if(QSet<QString>(keys.cbegin(),keys.cend())!=allowed){reject(error,"알 수 없는 프로젝트 필드가 포함되어 있습니다.");return {};}
    const auto request=readRequest(root["request"].toObject(),error);if(!request){reject(error,"프로젝트 작업 설정이 잘못되었습니다.");return {};}state.result.request=*request;
    if(!integer(root,"schema",1,1,schema)||root["format"]!="luma-caption-project-v1"||!root["complete"].isBool()||!integer(root,"next_chunk_ms",0,request->durationMs,state.next)||!root["originals"].isArray()||!root["translations"].isArray()||!root["skipped"].isArray()||!root["job_identity"].isObject()||!root["source_metadata"].isObject()){reject(error,"프로젝트 형식이 잘못되었습니다.");return {};}
    state.phase=root["phase"].toString();
    if(!QStringList{"recognition","translation","complete"}.contains(state.phase)||(root["complete"].toBool()!=(state.phase=="complete"))||(state.next!=request->durationMs&&state.next%chunkMs!=0)||(state.phase!="recognition"&&state.next!=request->durationMs)){reject(error,"프로젝트 완료 범위가 잘못되었습니다.");return {};}
    const auto directory=info.dir().canonicalPath();
    if(directory.isEmpty()||QFileInfo(request->outputDirectory).canonicalFilePath()!=directory){reject(error,"프로젝트 저장 폴더가 작업 설정과 다릅니다.");return {};}
    state.identity=root["job_identity"].toObject();state.source=root["source_metadata"].toObject();const auto hex=root["source_identity"].toString();
    if(!QRegularExpression("^[a-f0-9]{64}$").match(hex).hasMatch()||state.identity["request"]!=requestJson(*request)||QJsonDocument(state.identity).toJson(QJsonDocument::Compact).size()>1024*1024){reject(error,"프로젝트 정체성 정보가 잘못되었습니다.");return {};}
    state.result.sourceIdentity=QByteArray::fromHex(hex.toLatin1());
    if(state.result.sourceIdentity!=objectDigest(state.source)){reject(error,"프로젝트 원본 정체성이 손상되었습니다.");return {};}
    const auto current=sourceMetadata(*request,error);
    if(!current||objectDigest(*current)!=state.result.sourceIdentity){reject(error,"원본 파일 또는 Blu-ray가 준비 당시와 다릅니다.");return {};}
    const auto originals=root["originals"].toArray();if(originals.size()>cueLimit)return {};
    qsizetype tokens=0;qint64 previous=-1;
    for(const auto &entry:originals){if(!entry.isObject())return {};auto cue=readCue(entry.toObject(),state.next);
        if(!cue||cue->startMs<previous){reject(error,"프로젝트 원문 시각 또는 토큰이 잘못되었습니다.");return {};}
        tokens+=cue->tokens.size();if(tokens>tokenLimit)return {};previous=cue->startMs;state.result.originals.append(std::move(*cue));
    }
    QHash<QString,SubtitleCue> pages;for(const auto &page:captionPages(state.result.originals))pages.insert(translationCueKey(page),page);if(pages.size()>cueLimit)return {};
    const auto translated=root["translations"].toArray(),skipped=root["skipped"].toArray();
    if(translated.size()+skipped.size()>pages.size()||(!request->translate&&(!translated.isEmpty()||!skipped.isEmpty()))||(state.next<request->durationMs&&(!translated.isEmpty()||!skipped.isEmpty())))return {};
    for(const auto &entry:translated){if(!entry.isObject())return {};const auto translation=entry.toObject();if(translation.size()!=2||!translation["key"].isString()||!translation["korean"].isString())return {};
        const auto key=translation["key"].toString(),korean=translation["korean"].toString();
        if(!pages.contains(key)||state.result.translations.contains(key)||!validKorean(korean,pages[key],request->translationModel)){reject(error,"프로젝트 번역 데이터가 잘못되었습니다.");return {};}
        state.result.translations.insert(key,korean);
    }
    for(const auto &entry:skipped){if(!entry.isString())return {};const auto key=entry.toString();if(!pages.contains(key)||state.result.translations.contains(key)||state.skipped.contains(key))return {};state.skipped.insert(key);}
    if(state.phase=="complete"&&request->translate&&state.result.translations.size()+state.skipped.size()!=pages.size()){reject(error,"완료 프로젝트에 처리하지 않은 번역이 남아 있습니다.");return {};}
    if(!root["recognition_backend"].isString()||!root["translation_backend"].isString())return {};
    state.recognitionBackend=root["recognition_backend"].toString();state.translationBackend=root["translation_backend"].toString();
    if(state.recognitionBackend.size()>256||state.translationBackend.size()>256||!unicode(state.recognitionBackend,false)||!unicode(state.translationBackend,false))return {};
    paths(state,directory);
    if(requireComplete){
        if(state.phase!="complete"){reject(error,"미완료 프로젝트입니다. 같은 설정으로 다시 시작해 이어서 준비하세요.");return {};}
        for(const auto &artifact:QVector<QPair<QString,QByteArray>>{{state.result.originalSrt,writeSrt(captionPages(state.result.originals)).toUtf8()},{state.result.koreanSrt,writeSrt(koreanCues(state)).toUtf8()}}){
            if(artifact.first.isEmpty())continue;QFile exported(artifact.first);
            if(QFileInfo(artifact.first).isSymLink()||!exported.open(QIODevice::ReadOnly)||exported.size()>projectLimit||exported.readAll()!=artifact.second){reject(error,"내보낸 SRT가 프로젝트와 다릅니다. 같은 작업을 다시 시작해 복구하세요.");return {};}
        }
    }return state;
}
}
struct CaptionPreparationService::Impl {
    CaptionPreparationService *q;QString runtime;quint64 generation=0,recognitionGeneration=0,translationGeneration=0;
    bool active=false,owns=false;Checkpoint state;SubtitleJob *recognition=nullptr;TranslationService *translation=nullptr;
    std::unique_ptr<QLockFile> lock;QVector<SubtitleCue> pages;QHash<QString,SubtitleCue> targets;QSet<QString> pending;
    QString recognitionNotice;bool canRetryAccelerator=false;
    Impl(QString root,CaptionPreparationService *owner):q(owner),runtime(root.isEmpty()?QCoreApplication::applicationDirPath()+"/runtime":std::move(root)){}
    ~Impl(){stop();}
    void releaseRecognition(){
        if(!recognition)return;auto *old=recognition;recognition=nullptr;QObject::disconnect(old,nullptr,q,nullptr);
        // SubtitleJob keeps its result QFiles open while emitting ready(). An
        // idle child's temporary files must survive until that signal unwinds.
        if(old->busy())old->shutdown();
        QTimer::singleShot(0,old,[old]{old->shutdown();old->deleteLater();});
    }
    void stop(){
        releaseRecognition();
        if(translation){auto *old=translation;translation=nullptr;QObject::disconnect(old,nullptr,q,nullptr);old->cancel();
            // Stop our owned subprocesses before deleting the service; its normal
            // backend fallback is intentionally blocked during final teardown.
            for(auto *process:old->findChildren<QProcess*>()){const QSignalBlocker blocked(process);if(process->state()!=QProcess::NotRunning){process->kill();process->waitForFinished(1500);}}
            old->deleteLater();}
        pending.clear();
    }
    void defer(void(Impl::*method)()){const auto id=generation;QTimer::singleShot(0,q,[this,id,method]{if(active&&generation==id)(this->*method)();});}
    bool report(){const auto id=generation;emit q->progress(id,{state.phase,state.next,state.result.request.durationMs,int(state.result.translations.size()+state.skipped.size()),state.result.request.translate?int(targets.size()):0,state.recognitionBackend,state.translationBackend,recognitionNotice,state.phase=="recognition"&&canRetryAccelerator});return active&&generation==id;}
    void fail(QString message,bool persist=true){
        if(!active)return;const auto id=generation;QString error;
        if(persist&&owns&&!saveCheckpoint(state,error))message+="\n"+error;
        active=false;stop();lock.reset();emit q->failed(id,message);
    }
    bool checkpoint(){QString error;if(saveCheckpoint(state,error))return true;fail(error,false);return false;}
    bool unchanged(){
        QString error;const auto source=sourceMetadata(state.result.request,error),identity=jobIdentity(state.result.request,runtime,error);
        if(!source||!identity||objectDigest(*source)!=state.result.sourceIdentity||*identity!=state.identity){fail("준비 도중 원본·모델·실행기 정체성이 바뀌었습니다. "+error,false);return false;}return true;
    }
    void begin(CaptionPreparationRequest request){
        QString error;
        if(request.source.source.startsWith("file:",Qt::CaseInsensitive)){const QUrl url(request.source.source);request.source.source=url.isLocalFile()?url.toLocalFile():QString{};}
        else if(request.source.disc)request.source.source=localDiscPath(request.source.source);
        if(request.model.isEmpty())request.model=QDir(runtime).filePath("models/ggml-base.bin");
        if(request.translate&&request.translationModel.isEmpty()){const auto quality=QDir(runtime).filePath("translation/models/Hy-MT2-7B-Q4_K_M.gguf");request.translationModel=QFileInfo::exists(quality)?quality:QDir(runtime).filePath("translation/models/Qwen3-4B-Q4_K_M.gguf");}
        if(!validRequest(request,error)){fail(error,false);return;}
        const auto canonicalSource=QFileInfo(request.source.source).canonicalFilePath(),canonicalModel=QFileInfo(request.model).canonicalFilePath();
        if(canonicalSource.isEmpty()||canonicalModel.isEmpty()){fail("원본과 인식 모델을 찾을 수 없습니다.",false);return;}
        request.source.source=canonicalSource;request.model=canonicalModel;request.source.durationMs=chunkMs;
        if(request.translate){const auto canonical=QFileInfo(request.translationModel).canonicalFilePath();if(canonical.isEmpty()){fail("번역 모델을 찾을 수 없습니다.",false);return;}request.translationModel=canonical;}
        else if(!request.translationModel.isEmpty())request.translationModel=QFileInfo(request.translationModel).absoluteFilePath();
        const auto source=sourceMetadata(request,error);if(!source){fail(error,false);return;}
        if(!QDir().mkpath(request.outputDirectory)){fail("프로젝트 저장 폴더를 만들 수 없습니다.",false);return;}
        request.outputDirectory=QFileInfo(request.outputDirectory).canonicalFilePath();state.result.request=request;
        const auto identity=jobIdentity(request,runtime,error);if(!identity){fail(error,false);return;}
        lock=std::make_unique<QLockFile>(QDir(request.outputDirectory).filePath("luma-caption-project.lock"));lock->setStaleLockTime(0);
        if(!lock->tryLock(0)){fail("다른 작업이 이 자막 저장 폴더를 사용 중입니다.",false);return;}
        const auto project=QDir(request.outputDirectory).filePath(projectName);
        if(QFileInfo::exists(project)){
            const auto previous=readCheckpoint(project,false,error);
            if(!previous||previous->identity!=*identity||previous->result.sourceIdentity!=objectDigest(*source)){fail("다른 원본·모델·설정의 프로젝트이거나 손상된 중간 결과입니다. "+error,false);return;}
            state=*previous;
        }else{
            const auto entries=QDir(request.outputDirectory).entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot);
            for(const auto &entry:entries)if(entry!="luma-caption-project.lock"){fail("비어 있거나 같은 작업의 프로젝트가 있는 저장 폴더를 선택하세요.",false);return;}
            state.source=*source;state.identity=*identity;state.result.sourceIdentity=objectDigest(*source);
        }
        owns=true;if(!checkpoint())return;
        if(state.next==request.durationMs){beginTranslations();return;}
        recognition=new SubtitleJob(runtime,q);recognition->setPreferences(request.recognitionPreferences);const auto id=generation;
        QObject::connect(recognition,&SubtitleJob::progress,q,[this,id](const QString &message){
            if(!active||generation!=id||state.phase!="recognition")return;
            const auto notice=acceleratorRejection(message);
            if(!notice.isEmpty()){recognitionNotice=notice;canRetryAccelerator=false;report();}
            else if(message=="CPU에서 로컬 AI 자막 생성 중…"&&!recognitionNotice.isEmpty()){
                canRetryAccelerator=state.result.request.recognitionPreferences.mode!="cpu";report();
            }
        });
        QObject::connect(recognition,&SubtitleJob::failed,q,[this,id](const QString &message){if(active&&generation==id)fail(message);});
        QObject::connect(recognition,&SubtitleJob::ready,q,[this,id](quint64 child,QVector<SubtitleCue> cues){
            if(!active||generation!=id||!recognition||child!=recognitionGeneration)return;
            const auto duration=std::min(chunkMs,state.result.request.durationMs-state.next);
            qint64 previous=state.result.originals.isEmpty()?-1:state.result.originals.last().startMs;qsizetype totalTokens=0;
            for(const auto &cue:state.result.originals)totalTokens+=cue.tokens.size();
            for(const auto &cue:cues){if(!validCue(cue,state.next,state.next+duration)||cue.startMs<previous){fail("인식 결과의 시각·원문·토큰이 잘못되었습니다.");return;}previous=cue.startMs;totalTokens+=cue.tokens.size();}
            if(state.result.originals.size()+cues.size()>cueLimit||totalTokens>tokenLimit){fail("전체 자막 수 또는 토큰 저장 한도를 초과했습니다.");return;}
            state.result.originals+=cues;state.next+=duration;state.recognitionBackend=recognition->selectedBackend();
            if(!checkpoint()||!report())return;defer(&Impl::recognizeNext);
        });
        if(report())defer(&Impl::recognizeNext);
    }
    void recognizeNext(){
        if(!unchanged())return;if(state.next==state.result.request.durationMs){beginTranslations();return;}
        auto request=state.result.request.source;request.startMs=state.next;request.durationMs=std::min(chunkMs,state.result.request.durationMs-state.next);request.readAtFullSpeed=true;
        // start() increments once on both its idle and busy/cancel paths.
        // Cached silent PCM may emit ready synchronously before start returns.
        recognitionGeneration=recognition->generation()+1;
        recognition->start(request,state.result.request.model);
    }
    void finish(){
        if(!unchanged())return;state.phase="complete";if(!checkpoint())return;
        const auto id=generation;const auto result=state.result;active=false;stop();lock.reset();
        emit q->progress(id,{"complete",state.next,state.result.request.durationMs,int(state.result.translations.size()+state.skipped.size()),state.result.request.translate?int(targets.size()):0,state.recognitionBackend,state.translationBackend,recognitionNotice,false});
        if(generation==id)emit q->ready(id,result);
    }
    void beginTranslations(){
        releaseRecognition();
        pages=captionPages(state.result.originals);if(pages.size()>cueLimit){fail("표시 자막 수가 저장 한도를 초과했습니다.");return;}
        targets.clear();for(const auto &page:pages)targets.insert(translationCueKey(page),page);
        if(!state.result.request.translate){finish();return;}
        state.phase="translation";if(!checkpoint()||!report())return;
        translation=new TranslationService(runtime,q);translation->setPreferences(state.result.request.translationPreferences);translation->setModel(state.result.request.translationModel);
        translation->setContextCues(state.result.originals);translationGeneration=translation->generation();const auto id=generation;
        QObject::connect(translation,&TranslationService::failed,q,[this,id](const QString &message){if(active&&generation==id)fail(message);});
        QObject::connect(translation,&TranslationService::translatedCue,q,[this,id](quint64 child,const QString &key,const QString &source,const QString &korean){
            if(!active||generation!=id||child!=translationGeneration||!pending.contains(key)||targets.value(key).text!=source)return;
            if(!validKorean(korean,targets[key],state.result.request.translationModel)){fail("번역 결과가 저장 형식에 맞지 않습니다.");return;}
            pending.remove(key);state.result.translations.insert(key,korean);state.translationBackend=translation->selectedBackend();
            if(!report())return;if(pending.isEmpty()&&checkpoint())defer(&Impl::translateNext);
        });
        QObject::connect(translation,&TranslationService::skippedCue,q,[this,id](quint64 child,const QString &key,const QString &source,const QString &){
            if(!active||generation!=id||child!=translationGeneration||!pending.contains(key)||targets.value(key).text!=source)return;
            pending.remove(key);state.skipped.insert(key);state.result.skippedTranslations=int(state.skipped.size());
            if(!report())return;if(pending.isEmpty()&&checkpoint())defer(&Impl::translateNext);
        });
        defer(&Impl::translateNext);
    }
    void translateNext(){
        if(!pending.isEmpty()||!unchanged())return;QVector<SubtitleCue> batch;
        for(const auto &page:pages){const auto key=translationCueKey(page);if(state.result.translations.contains(key)||state.skipped.contains(key)||pending.contains(key))continue;
            pending.insert(key);batch.append(page);if(batch.size()==16)break;
        }
        if(batch.isEmpty()){finish();return;}translation->requestCues(batch);
    }
};
CaptionPreparationService::CaptionPreparationService(QString runtime,QObject *parent):QObject(parent),d(std::make_unique<Impl>(std::move(runtime),this)){
    qRegisterMetaType<CaptionPreparationProgress>();qRegisterMetaType<CaptionPreparationResult>();
}
CaptionPreparationService::~CaptionPreparationService()=default;
quint64 CaptionPreparationService::start(const CaptionPreparationRequest &request){
    if(d->active)cancel();d->stop();d->lock.reset();d->state={};d->pages.clear();d->targets.clear();d->recognitionNotice.clear();d->canRetryAccelerator=false;d->owns=false;d->active=true;
    const auto id=++d->generation;QTimer::singleShot(0,this,[this,id,request]{if(d->active&&d->generation==id)d->begin(request);});return id;
}
void CaptionPreparationService::cancel(){
    if(!d->active)return;const auto id=d->generation;QString error;
    const bool saved=!d->owns||saveCheckpoint(d->state,error);d->active=false;++d->generation;d->stop();d->lock.reset();
    if(saved)emit canceled(id);else emit failed(id,error);
}
bool CaptionPreparationService::busy()const{return d->active;}
quint64 CaptionPreparationService::generation()const{return d->generation;}
std::optional<CaptionPreparationResult> CaptionPreparationService::loadProject(const QString &file,QString *error){
    QString reason;const auto state=readCheckpoint(file,true,reason);
    if(error)*error=state?QString{}:reason.isEmpty()?QString("프로젝트 데이터가 잘못되었습니다."):reason;
    return state?std::optional<CaptionPreparationResult>(state->result):std::nullopt;
}
bool CaptionPreparationService::sourceUnchanged(const CaptionPreparationResult &result,QString *error){
    QString reason;const auto source=sourceMetadata(result.request,reason);
    const bool same=source&&result.sourceIdentity.size()==32&&objectDigest(*source)==result.sourceIdentity;
    if(error)*error=same?QString{}:reason.isEmpty()?QString("원본 미디어가 준비 당시와 다릅니다."):reason;return same;
}
}
