#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>
#include <optional>

class QVBoxLayout;
class QWidget;

namespace luma {
namespace update {

struct Release {
    QString tag;
    QString version;
    qint64 manifestBytes = 0;
    QHash<QString, qint64> installers;
};

struct Manifest {
    QString version;
    QString file;
    qint64 bytes = 0;
    QByteArray sha256;
};

std::optional<Release> newestRelease(const QByteArray &json, const QString &installedVersion);
std::optional<Manifest> verifiedManifest(const QByteArray &json, const QByteArray &signature,
                                         const Release &release, const QString &installedVersion,
                                         const QByteArray &publicCoordinates);
bool installerMatches(const Manifest &manifest, qint64 bytes, const QByteArray &sha256);
bool allowedAssetUrl(const QUrl &url, const QString &tag, const QString &assetName);

} // namespace update

class UpdateManager final : public QObject {
    Q_OBJECT

public:
    explicit UpdateManager(QWidget *parent);
    ~UpdateManager() override;
    void addControls(QVBoxLayout *layout);
    void checkAtStartup();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace luma
