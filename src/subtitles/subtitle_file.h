#pragma once
#include <QObject>
#include <QString>
#include <memory>
#include "core/subtitle.h"
namespace luma {
struct SubtitleFileResult {
    QVector<SubtitleCue> cues;
    QString error;
    QString encoding;
    bool valid()const{return error.isEmpty() && !cues.isEmpty();}
};
// Strict Unicode SRT reader; malformed files never return a partial cue list.
SubtitleFileResult readSrtFile(const QString &path);
class SubtitleFileLoader final:public QObject {
    Q_OBJECT
public:
    explicit SubtitleFileLoader(QString runtime={},QObject *parent=nullptr);
    ~SubtitleFileLoader()override;
    // Signals are asynchronous, including for SRT. A new load cancels the old generation.
    quint64 load(QString path);
    void cancel();
    quint64 generation()const;
signals:
    void ready(quint64 generation,QVector<luma::SubtitleCue> cues);
    void failed(quint64 generation,QString message);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
