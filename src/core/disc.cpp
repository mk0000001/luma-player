#include "disc.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QtEndian>
#include <algorithm>
namespace luma {
namespace {
bool range(const QByteArray &b, quint64 p, quint64 n) { return p <= quint64(b.size()) && n <= quint64(b.size()) - p; }
quint32 u32(const QByteArray &b, qsizetype p) { return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + p)); }
quint16 u16(const QByteArray &b, qsizetype p) { return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(b.constData() + p)); }
QByteArray readBounded(const QString &path, qint64 limit) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly) || f.size() > limit) return {};
    return f.read(limit);
}
}
QString formatTime(qint64 ms) {
    const auto s = std::max<qint64>(0, ms) / 1000;
    const QString tail = QString("%1:%2").arg((s/60)%60,2,10,QChar('0')).arg(s%60,2,10,QChar('0'));
    return s >= 3600 ? QString("%1:").arg(s/3600,2,10,QChar('0')) + tail : tail;
}
DiscTitle parsePlaylist(const QByteArray &b, const QString &name) {
    DiscTitle title; title.playlist = name;
    if (b.size() < 20 || !b.startsWith("MPLS")) return title;
    const quint64 p = u32(b,8);
    if (!range(b,p,10)) return title;
    const quint64 sectionLength = u32(b,qsizetype(p));
    if (sectionLength < 6 || !range(b,p+4,sectionLength)) return title;
    const quint64 end = p+4+sectionLength;
    const auto count = u16(b,qsizetype(p+6));
    quint64 cursor = p+10;
    if (count > (end-cursor)/22) return title;
    for (quint16 i=0; i<count; ++i) {
        if (!range(b,cursor,2)) return DiscTitle{};
        const auto len = u16(b,qsizetype(cursor)); cursor += 2;
        if (len < 20 || cursor > end || len > end-cursor || !range(b,cursor,len)) return DiscTitle{};
        const auto in = u32(b,qsizetype(cursor+12)), out = u32(b,qsizetype(cursor+16));
        if (out < in) return DiscTitle{};
        const auto clip = b.mid(qsizetype(cursor),5);
        if (!std::all_of(clip.begin(),clip.end(),[](char c){ return c>='0' && c<='9'; })) return DiscTitle{};
        title.clips.append(QString::fromLatin1(clip));
        title.durationMs += (qint64(out)-in)*1000/45000;
        cursor += len;
    }
    return title;
}
DiscInfo inspectDisc(const QString &root) {
    DiscInfo info; info.root = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QDir dir(info.root);
    const auto index = readBounded(dir.filePath("BDMV/index.bdmv"),16*1024*1024);
    if (index.size()<44 || !index.startsWith("INDX")) { info.error = QStringLiteral("유효한 Blu-ray index.bdmv를 읽을 수 없습니다."); return info; }
    const quint64 offset = u32(index,8);
    if (!range(index,offset,4) || u32(index,qsizetype(offset))<26 || !range(index,offset+4,u32(index,qsizetype(offset)))) {
        info.error = QStringLiteral("Blu-ray 인덱스가 잘렸거나 손상되었습니다."); return info;
    }
    info.valid = true;
    info.aacs = QFileInfo::exists(dir.filePath("AACS"));
    info.bdplus = QFileInfo::exists(dir.filePath("BDSVM"));
    const QDir bdjo(dir.filePath("BDMV/BDJO"));
    info.menuKind = !bdjo.entryList({"*.bdjo"},QDir::Files).isEmpty() ? "BD-J" : "HDMV";
    info.label = QStorageInfo(info.root).name();
    if (info.label.isEmpty()) info.label = QFileInfo(info.root).fileName();
    const QDir playlists(dir.filePath("BDMV/PLAYLIST"));
    for (const auto &file : playlists.entryList({"*.mpls"},QDir::Files,QDir::Name)) {
        auto title = parsePlaylist(readBounded(playlists.filePath(file),8*1024*1024),QFileInfo(file).completeBaseName());
        if (title.durationMs>0) info.titles.append(std::move(title));
    }
    std::stable_sort(info.titles.begin(),info.titles.end(),[](const auto &a,const auto &b){ return a.durationMs>b.durationMs; });
    return info;
}
QString matchDiscPlaylist(const DiscInfo &disc,qint64 durationMs){
    if(durationMs<=0)return {};QString match;
    for(const auto &title:disc.titles){if(title.durationMs<=0 || qAbs(title.durationMs-durationMs)>200)continue;
        if(!match.isEmpty())return {};match=title.playlist;
    }
    return match;
}
}
