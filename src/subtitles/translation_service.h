#pragma once
#include <QObject>
#include <QStringList>
#include <memory>
#include "ai_backend.h"
namespace luma {
// Local native translation. Call from its owning Qt event-loop thread.
// Source strings are immutable identifiers; timings/readings remain with the caller.
class TranslationService : public QObject {
    Q_OBJECT
public:
    explicit TranslationService(QString runtime={},QObject *parent=nullptr);
    ~TranslationService() override;
    void setPreferences(const AiPreferences &preferences);
    AiPreferences preferences()const;
    void setModel(QString model={});
    void request(const QStringList &nearestFirst);
    void cancel();
    quint64 generation()const;
    QString selectedBackend()const;
    bool busy()const;
    void setTimeouts(int startupMs,int requestMs);
signals:
    void translated(quint64 generation,QString source,QString korean);
    // A rejected cue leaves the generation and remaining queue active.
    void skipped(quint64 generation,QString source,QString message);
    void progress(QString message);
    // Fatal model/runtime failure; callers may stop scheduling until retry.
    void failed(QString message);
    void backendSelected(QString backend);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
