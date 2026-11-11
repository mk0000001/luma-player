#pragma once
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QProcessEnvironment>
#include <QStringList>
#include <optional>
#include "ai_backend.h"
#include "core/subtitle.h"
namespace luma {
using TranslationGlossary=QMap<QString,QString>;
std::optional<TranslationGlossary> parseTranslationGlossary(const QString &text);
QString formatTranslationGlossary(const TranslationGlossary &glossary);
struct TranslationTask {quint64 generation;QString source;};
class TranslationQueue {
public:
    explicit TranslationQueue(int pendingLimit=64,int cacheLimit=1024);
    void enqueue(const QStringList &nearestFirst);
    std::optional<TranslationTask> take();
    bool complete(const TranslationTask &task,const QString &korean);
    void release(const TranslationTask &task);
    void cancel(bool clearCache=false);
    std::optional<QString> cached(const QString &source) const;
    bool retains(const QString &source)const{return source==active_ || pending_.contains(source) || cache_.contains(source);}
    quint64 generation()const{return generation_;}
    bool busy()const{return !pending_.isEmpty()||!active_.isEmpty();}
private:
    int pendingLimit_,cacheLimit_;
    quint64 generation_=1;
    QStringList pending_,cacheOrder_;
    QString active_;
    QHash<QString,QString> cache_;
};
struct TranslationBackend {QString key,program,device;bool openvino=false;};
struct TranslationLaunch {QString program;QStringList arguments;QProcessEnvironment environment;};
enum class TranslationProfile {LegacyQwenJson,HyMt2Plain};
// Only the pinned, tested dedicated model opts into its plain-output protocol.
TranslationProfile translationProfileForModel(const QString &model);
QVector<TranslationBackend> translationBackends(const QString &runtime,const AiPreferences &preferences={});
TranslationLaunch translationLaunch(const TranslationBackend &backend,const QString &model,quint16 port,const QString &token);
// Decode JSON once per protocol layer; reject damaged Unicode/model artifacts.
// Preserve proper names, combining characters and lyric line breaks.
QString parseTranslationResponse(const QByteArray &response,QString *error=nullptr,TranslationProfile profile=TranslationProfile::LegacyQwenJson,const QString &target={});
QJsonObject translationPayload(const QString &source,TranslationProfile profile=TranslationProfile::LegacyQwenJson);
// Timed keys distinguish identical phrases in different scenes.
QString translationCueKey(const SubtitleCue &cue);
// The target is immutable. Context is selected from raw original cues, never display pages.
// UTF-8 bytes provide a conservative token ceiling; an empty object means the target cannot fit.
QJsonObject translationPayload(const SubtitleCue &target,const QVector<SubtitleCue> &originals,TranslationProfile profile=TranslationProfile::LegacyQwenJson,int promptByteBudget=6144,const TranslationGlossary &glossary={});
int translationPromptBytes(const QJsonObject &payload);
QString confirmedTranslationBackend(const TranslationBackend &backend,const QString &log);
}
