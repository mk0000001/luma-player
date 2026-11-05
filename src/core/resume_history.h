#pragma once
#include <QString>
#include <memory>
#include <optional>
class QSettings;
namespace luma {
struct DiscInfo;
struct ResumePoint {QString source;bool disc=false;int title=-1;qint64 positionMs=0,durationMs=0;};
QString resumeIdentity(const QString &source,const DiscInfo *disc=nullptr);
class ResumeHistory {
public:
    explicit ResumeHistory(const QString &settingsFile={});
    ~ResumeHistory();
    void save(const QString &identity,ResumePoint point);
    std::optional<ResumePoint> find(const QString &identity,int title=-1) const;
    std::optional<ResumePoint> latest() const;
    QString latestIdentity()const;
    void remove(const QString &identity,int title=-1);
private:
    std::unique_ptr<QSettings> settings_;
};
}
