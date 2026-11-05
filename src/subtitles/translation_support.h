#pragma once
#include <QHash>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <optional>
#include "ai_backend.h"
namespace luma {
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
QVector<TranslationBackend> translationBackends(const QString &runtime,const AiPreferences &preferences={});
TranslationLaunch translationLaunch(const TranslationBackend &backend,const QString &model,quint16 port,const QString &token);
// Decode JSON once per protocol layer; reject damaged Unicode/model artifacts.
// Preserve proper names, combining characters and lyric line breaks.
QString parseTranslationResponse(const QByteArray &response,QString *error=nullptr);
QJsonObject translationPayload(const QString &source);
QString confirmedTranslationBackend(const TranslationBackend &backend,const QString &log);
}
