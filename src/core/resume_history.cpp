#include "resume_history.h"
#include "disc.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>
namespace luma {
namespace {
constexpr auto settingsKey="resumeHistory/v1";
constexpr qsizetype maximumEntries=100,maximumBytes=512*1024;
struct Entry {QString identity;ResumePoint point;};
bool validIdentity(const QString &identity){static const QRegularExpression pattern("^[a-f0-9]{64}$");return identity.size()==64 && pattern.match(identity).hasMatch();}
bool validText(const QString &text){
    if(text.isEmpty() || text.size()>32768 || text.contains(QChar(0)))return false;
    for(qsizetype i=0;i<text.size();++i){if(text[i].isHighSurrogate()){if(++i>=text.size() || !text[i].isLowSurrogate())return false;}else if(text[i].isLowSurrogate())return false;}
    return true;
}
bool validTitle(const ResumePoint &point){return point.disc?(point.discMenus?point.title>0:point.title>=0):point.title==-1;}
bool validProject(const QString &project){return project.isEmpty() || validText(project);}
bool validPoint(const ResumePoint &point){return validText(point.source) && validTitle(point) && validProject(point.preparedProject) && point.positionMs>=3000 && point.durationMs>point.positionMs && point.durationMs-point.positionMs>=5000;}
QString entryKey(const QString &identity,const ResumePoint &point){return identity+'/'+(point.disc?(point.discMenus?"menu":"direct"):"file")+'/'+QString::number(point.title);}
std::optional<qint64> milliseconds(const QJsonValue &value){
    if(!value.isString())return {};const auto text=value.toString();static const QRegularExpression digits("\\A[0-9]{1,19}\\z");if(!digits.match(text).hasMatch())return {};
    bool ok=false;const auto number=text.toLongLong(&ok);return ok?std::optional<qint64>(number):std::nullopt;
}
QVector<Entry> readEntries(QSettings &settings){
    const auto bytes=settings.value(settingsKey).toByteArray();if(bytes.isEmpty() || bytes.size()>maximumBytes)return {};
    QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject())return {};
    const auto root=document.object();if(root.value("version").toDouble(-1)!=1 || !root.value("entries").isArray())return {};
    QVector<Entry> entries;QSet<QString> seen;
    for(const auto &value:root.value("entries").toArray()){
        if(!value.isObject())continue;const auto object=value.toObject();const auto identity=object.value("identity").toString();
        const auto title=object.value("title");const auto position=milliseconds(object.value("positionMs")),duration=milliseconds(object.value("durationMs"));
        if(!validIdentity(identity) || !object.value("source").isString() || !object.value("disc").isBool() || !title.isDouble() || !position || !duration)continue;
        const auto menus=object.value("discMenus"),project=object.value("preparedProject");
        if((!menus.isUndefined() && !menus.isBool()) || (!project.isUndefined() && !project.isString()))continue;
        const auto titleNumber=title.toDouble();if(titleNumber < -1 || titleNumber>std::numeric_limits<int>::max() || std::trunc(titleNumber)!=titleNumber)continue;
        ResumePoint point{object.value("source").toString(),object.value("disc").toBool(),int(titleNumber),*position,*duration,menus.isUndefined()?true:menus.toBool(),project.toString()};
        const auto key=entryKey(identity,point);if(!validPoint(point) || seen.contains(key))continue;
        seen.insert(key);entries.append({identity,std::move(point)});if(entries.size()>=maximumEntries)break;
    }
    return entries;
}
QByteArray serialize(const QVector<Entry> &entries){
    QJsonArray array;
    for(const auto &entry:entries){const auto &point=entry.point;array.append(QJsonObject{{"identity",entry.identity},{"source",point.source},{"disc",point.disc},{"title",point.title},{"positionMs",QString::number(point.positionMs)},{"durationMs",QString::number(point.durationMs)},{"discMenus",point.discMenus},{"preparedProject",point.preparedProject}});}
    return QJsonDocument(QJsonObject{{"version",1},{"entries",array}}).toJson(QJsonDocument::Compact);
}
void writeEntries(QSettings &settings,QVector<Entry> entries){
    if(entries.size()>maximumEntries)entries.resize(maximumEntries);
    auto bytes=serialize(entries);while(bytes.size()>maximumBytes && !entries.isEmpty()){entries.removeLast();bytes=serialize(entries);}
    settings.setValue(settingsKey,bytes);settings.sync();
}
}
QString resumeIdentity(const QString &source,const DiscInfo *disc){
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_0);
    if(disc){
        if(!disc->valid)return {};QVector<QByteArray> playlists;
        for(const auto &title:disc->titles){
            if(title.playlist.isEmpty() || title.durationMs<=0 || title.clips.isEmpty())continue;
            QByteArray encoded;QDataStream item(&encoded,QIODevice::WriteOnly);item.setVersion(QDataStream::Qt_6_0);
            item<<title.playlist.normalized(QString::NormalizationForm_C).toLower()<<title.durationMs<<title.clips;playlists.append(encoded);
        }
        if(playlists.isEmpty())return {};std::sort(playlists.begin(),playlists.end());
        auto label=disc->label.trimmed().normalized(QString::NormalizationForm_C);static const QRegularExpression drive(R"(^[A-Za-z]:[/\\]?$)");
        if(drive.match(label).hasMatch())label.clear();
        stream<<QString("disc")<<label<<playlists;
    }else{
        const QUrl url(source);const QFileInfo file(url.isLocalFile()?url.toLocalFile():source);
        if(source.isEmpty() || !file.exists() || !file.isFile() || !file.lastModified().isValid())return {};
        auto path=QDir::fromNativeSeparators(file.canonicalFilePath());if(path.isEmpty())return {};
#ifdef Q_OS_WIN
        path=path.toCaseFolded();
#endif
        stream<<QString("file")<<path<<file.size()<<file.lastModified().toMSecsSinceEpoch();
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
ResumeHistory::ResumeHistory(const QString &file):settings_(file.isEmpty()?std::make_unique<QSettings>():std::make_unique<QSettings>(file,QSettings::IniFormat)){}
ResumeHistory::~ResumeHistory()=default;
void ResumeHistory::save(const QString &identity,ResumePoint point){
    if(!point.disc)point.title=-1;
    if(!validIdentity(identity) || !validText(point.source) || !validTitle(point) || !validProject(point.preparedProject) || point.positionMs<3000 || point.durationMs<=0)return;
    if(point.positionMs>point.durationMs-5000){remove(identity,point.title,point.discMenus);return;}
    if(!validPoint(point))return;
    const auto key=entryKey(identity,point);
    auto entries=readEntries(*settings_);entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const Entry &entry){return entryKey(entry.identity,entry.point)==key;}),entries.end());
    entries.prepend({identity,std::move(point)});writeEntries(*settings_,std::move(entries));
}
std::optional<ResumePoint> ResumeHistory::find(const QString &identity,int title,bool discMenus)const{
    if(!validIdentity(identity))return {};
    for(const auto &entry:readEntries(*settings_))if(entry.identity==identity && (title<0 || entry.point.title==title) && (!entry.point.disc || entry.point.discMenus==discMenus))return entry.point;return {};
}
std::optional<ResumePoint> ResumeHistory::latest()const{const auto entries=readEntries(*settings_);return entries.isEmpty()?std::nullopt:std::optional<ResumePoint>(entries.first().point);}
QString ResumeHistory::latestIdentity()const{const auto entries=readEntries(*settings_);return entries.isEmpty()?QString{}:entries.first().identity;}
void ResumeHistory::remove(const QString &identity,int title,bool discMenus){
    if(!validIdentity(identity))return;
    auto entries=readEntries(*settings_);entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const Entry &entry){return entry.identity==identity && (title<0 || entry.point.title==title) && (!entry.point.disc || entry.point.discMenus==discMenus);}),entries.end());writeEntries(*settings_,std::move(entries));
}
}
