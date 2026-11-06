#pragma once
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <memory>
#include <optional>
#include "subtitle_job.h"
namespace luma {
struct CaptionPreparationRequest {
    RecognitionRequest source;
    qint64 durationMs=0;
    QString model,translationModel,outputDirectory;
    AiPreferences recognitionPreferences,translationPreferences;
    bool translate=true;
};
struct CaptionPreparationResult {
    CaptionPreparationRequest request;
    QVector<SubtitleCue> originals;
    QHash<QString,QString> translations;
    QString projectFile,originalSrt,koreanSrt;
    int skippedTranslations=0;
    QByteArray sourceIdentity;
};
struct CaptionPreparationProgress {
    QString phase;
    qint64 recognizedMs=0,totalMs=0;
    int completedTranslations=0,totalTranslations=0;
    QString recognitionBackend,translationBackend;
};
// Headless full-source recognition, then bounded original-context translation.
// Call from the owning Qt event-loop thread. Projects contain data, never tools.
class CaptionPreparationService:public QObject {
    Q_OBJECT
public:
    explicit CaptionPreparationService(QString runtime={},QObject *parent=nullptr);
    ~CaptionPreparationService() override;
    quint64 start(const CaptionPreparationRequest &request);
    void cancel();
    bool busy()const;
    quint64 generation()const;
    static std::optional<CaptionPreparationResult> loadProject(const QString &file,QString *error=nullptr);
    static bool sourceUnchanged(const CaptionPreparationResult &result,QString *error=nullptr);
signals:
    void progress(quint64 generation,luma::CaptionPreparationProgress progress);
    void ready(quint64 generation,luma::CaptionPreparationResult result);
    void failed(quint64 generation,QString message);
    void canceled(quint64 generation);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
Q_DECLARE_METATYPE(luma::CaptionPreparationRequest)
Q_DECLARE_METATYPE(luma::CaptionPreparationResult)
Q_DECLARE_METATYPE(luma::CaptionPreparationProgress)
