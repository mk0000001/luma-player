#pragma once
#include <QString>
#include <QVector>
namespace luma {
struct DiscTitle { QString playlist; qint64 durationMs = 0; QStringList clips; };
struct DiscInfo {
    QString root, label, menuKind, error;
    bool valid = false, aacs = false, bdplus = false;
    QVector<DiscTitle> titles;
};
DiscTitle parsePlaylist(const QByteArray &data, const QString &name);
DiscInfo inspectDisc(const QString &root);
QString formatTime(qint64 milliseconds);
}
