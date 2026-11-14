#pragma once
#include <QObject>
#include <QStringList>
#include <memory>
#include "ai_backend.h"
#include "translation_support.h"
#include "core/subtitle.h"
namespace luma {
// Local native translation. Call from its owning Qt event-loop thread.
// Timed cues identify repeated phrases; display timings/readings remain with the caller.
class TranslationService : public QObject {
    Q_OBJECT
public:
    explicit TranslationService(QString runtime={},QObject *parent=nullptr);
    ~TranslationService() override;
    void setPreferences(const AiPreferences &preferences);
    AiPreferences preferences()const;
    void setModel(QString model={});
    void setGlossary(const TranslationGlossary &glossary);
    TranslationGlossary glossary()const;
    void request(const QStringList &nearestFirst);
    // Replace the current session's raw source buffer, including newly available future cues.
    void setContextCues(const QVector<SubtitleCue> &originals);
    // Read-only: a completed timed result is reusable only for the current bounded payload.
    bool needsTranslation(const SubtitleCue &cue)const;
    // A playback position makes this the complete live window, replacing pending work.
    // Offline preparation omits it and retains the ordered queue.
    void requestCues(const QVector<SubtitleCue> &nearestFirst,std::optional<qint64> playbackPositionMs={});
    void warmUp();
    void shutdown();
    void cancel();
    quint64 generation()const;
    QString selectedBackend()const;
    bool selectedVulkanCompatibility()const;
    bool busy()const;
    void setTimeouts(int startupMs,int requestMs);
signals:
    void translated(quint64 generation,QString source,QString korean);
    void translatedCue(quint64 generation,QString cueKey,QString source,QString korean);
    // A rejected cue leaves the generation and remaining queue active.
    void skipped(quint64 generation,QString source,QString message);
    void skippedCue(quint64 generation,QString cueKey,QString source,QString message);
    void progress(QString message);
    // Fatal model/runtime failure; callers may stop scheduling until retry.
    void failed(QString message);
    void backendSelected(QString backend);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
