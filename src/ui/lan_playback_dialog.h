#pragma once

#include "core/subtitle.h"
#include <QDialog>
#include <QHostAddress>
#include <QPointer>
#include <QProcess>
#include <QTimer>

class QComboBox;
class QJsonArray;
class QJsonObject;
class QLabel;
class QPushButton;
class QShowEvent;
class QTreeWidget;

namespace luma {

struct LanPlaybackMedia {
    QString source, title, playlist, subtitles;
    qint64 durationMs = 0;
    int audioIndex = -1, audioTrackId = -1;
    bool disc = false;
};

struct LanPrivateInterface {
    QString name;
    QHostAddress address;
    int prefixLength;
};

QString lanPrivateSubnet(const QHostAddress &address, int prefixLength);
QList<LanPrivateInterface> lanPrivateInterfaces();
QString lanCaptionTrack(const QVector<SubtitleCue> &cues);
bool lanLocalFileSupported(const QString &source);

class LanPlaybackDialog final : public QDialog {
    Q_OBJECT
    Q_PROPERTY(bool sharing READ isSharing NOTIFY sharingChanged)
public:
    explicit LanPlaybackDialog(QWidget *parent);
    ~LanPlaybackDialog() override;

    void startDiscovery();
    QString readyEndpoint() const { return ready_ ? endpoint_ : QString{}; }
    void setMediaAvailable(bool available, bool disc);
    bool canShare() const;
    bool isSharing() const { return sharing_; }
    bool isPreparingDiscShare() const { return mediaState_ == MediaState::Preparing; }
    void prepareDiscShare();
    void cancelDiscShare(const QString &message);
    bool shareMedia(const LanPlaybackMedia &media);
    bool stopSharingAndWait();

signals:
    void shareRequested();
    void receiveRequested();
    void deviceAuthorized();
    void shareStopped(bool cleanupConfirmed);
    void sharingChanged(bool sharing);

protected:
    void showEvent(QShowEvent *event) override;

private:
    enum class MediaState { Idle, Preparing, Starting, Sharing, Stopping };
    void refreshInterfaces();
    void restartDiscovery();
    void closeHost();
    void stopSharing();
    void forgetDevices();
    void requestDevices();
    void updateDevices(const QJsonArray &devices);
    void manageDevice(bool forget);
    bool writeMessage(const QJsonObject &message);
    void readReply();
    void handleReply(const QJsonObject &message);
    void protocolError(const QString &message = {});
    void showPairRequest(const QJsonObject &message);
    void replyToPairRequest(const QString &id, const QString &decision);
    void dismissPairRequest();
    void finishShare(bool cleanupConfirmed);
    void setSharing(bool sharing);
    void updateControls();

    QProcess host_;
    QTimer watchdog_;
    QByteArray startupConfiguration_, reply_;
    QString endpoint_, pairingId_;
    QPointer<QDialog> pairingDialog_;
    QComboBox *interfaces_, *audioMode_;
    QLabel *address_, *status_;
    QPushButton *start_, *stop_, *retry_, *forget_, *disconnectDevice_, *forgetDevice_;
    QTreeWidget *devices_;
    MediaState mediaState_ = MediaState::Idle;
    bool ready_ = false, sharing_ = false, mediaAvailable_ = false, hasActiveDevice_ = false;
    bool autoShare_ = true;
    bool forgetting_ = false, shuttingDown_ = false, closingHost_ = false, protocolFailed_ = false;
    bool cleanupConfirmed_ = true;
};

}
