#pragma once
#include <QObject>
#include <QStringList>
#include <memory>

namespace luma {
struct PreparationMediaInfo {
    qint64 durationMs=0;
    // Labels follow FFmpeg's audio stream order; the list index is the audio ordinal.
    QStringList audioTracks;
};
class PreparationMediaProbe : public QObject {
    Q_OBJECT
public:
    explicit PreparationMediaProbe(QString runtime={},QObject *parent=nullptr);
    ~PreparationMediaProbe() override;
    quint64 inspect(const QString &localFile);
    void cancel();
    quint64 generation()const;
    bool busy()const;
signals:
    void ready(quint64 generation,luma::PreparationMediaInfo info);
    void failed(quint64 generation,QString message);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
Q_DECLARE_METATYPE(luma::PreparationMediaInfo)
