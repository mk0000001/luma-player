#pragma once

#include "core/subtitle.h"
#include <QDialog>
#include <QHostAddress>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#ifdef Q_OS_WIN
#include "windows_direct.h"
#include <QElapsedTimer>
#endif

class QCheckBox;
class QComboBox;
class QGroupBox;
class QHideEvent;
class QJsonArray;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QSpinBox;
class QTreeWidget;

namespace luma {

struct LanPlaybackMedia {
    QString source, title, playlist, subtitles;
    QString videoRange = "unknown";
    QVector<qint64> chaptersMs;
    QStringList chapterTitles;
    qint64 durationMs = 0;
    qint64 startMs = 0;
    int audioIndex = -1, audioTrackId = -1;
    bool disc = false;
};

struct LanPrivateInterface {
    QString name;
    QHostAddress address;
    int prefixLength;
    int interfaceIndex = 0;
};

QString lanPrivateSubnet(const QHostAddress &address, int prefixLength);
QString lanPrivatePeerSubnet(const QString &value);
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
    bool remoteControlEnabled() const { return !remoteControlGrant_.isEmpty(); }
    bool isSharing() const { return sharing_; }
    bool isPreparingDiscShare() const { return mediaState_ == MediaState::Preparing; }
    void prepareDiscShare();
    void cancelDiscShare(const QString &message);
    bool shareMedia(const LanPlaybackMedia &media);
    bool stopSharingAndWait();
    bool releaseMediaForRemoteControl();
    void disarmRemoteStart();
    QString remoteFilePath(const QString &id) const;
    void sendDiscMenuFrame(const QByteArray &png);

signals:
    void shareRequested();
    void remoteControlRequested(const QString &action,const QString &fileId);
    void discMenuFrameRequested();
    void streamDiagnostic(const QString &reason,const QString &message);
    void receiveRequested();
    void shareStopped(bool cleanupConfirmed);
    void sharingChanged(bool sharing);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class MediaState { Idle, Preparing, Starting, Sharing, Stopping };
    void refreshInterfaces();
    void disarmRemoteControl();
    void maintainDiscovery();
    bool startHost(const QString &address, const QString &subnet, const QString &peerSubnet = {});
    void restartDiscovery();
    void closeHost();
    void stopSharing();
    void forgetDevices();
    void requestDevices();
    void updateDevices(const QJsonArray &devices);
    void manageDevice(bool forget);
    void syncDeviceOptions();
    void loadDeviceOptions(const QJsonObject &options);
    QJsonObject deviceOptionValues() const;
    void applyDeviceOptions();
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
    void publishRemoteFiles();
    QHash<QString,QString> remoteFiles_;
#ifdef Q_OS_WIN
    void toggleDirect();
    void pollDirect();
    void stopDirect(const QString &message);
#endif

    QProcess host_;
    QTimer watchdog_, deviceRefresh_, discoveryRefresh_;
    QByteArray startupConfiguration_, reply_;
    QString endpoint_, pairingId_, appliedPeerSubnet_, hostPeerSubnet_;
    QString hostAddress_, hostSubnet_, preferredDiscoveryAddress_;
    QString optionsDeviceId_, pendingOptionsDeviceId_;
    QJsonObject optionsSnapshot_, savedOptions_;
    qint64 pendingOptionsRevision_ = 0;
    QPointer<QDialog> pairingDialog_;
    QComboBox *interfaces_, *audioMode_;
    QLabel *address_, *status_;
    QLineEdit *peerSubnet_;
    QPushButton *start_, *stop_, *retry_, *forget_, *disconnectDevice_, *forgetDevice_;
    QTreeWidget *devices_;
    QGroupBox *deviceOptionsBox_;
    QComboBox *deviceProfile_, *deviceHdr_, *deviceSkip_, *deviceFill_;
    QCheckBox *deviceSubtitleDelayEnabled_;
    QCheckBox *remoteStart_;
    QCheckBox *remoteControl_;
    QString remoteStartGrant_;
    QString remoteControlGrant_;
    QSpinBox *deviceSubtitleDelay_;
    QPushButton *applyOptions_, *resetOptions_;
    MediaState mediaState_ = MediaState::Idle;
    bool ready_ = false, sharing_ = false, mediaAvailable_ = false, hasActiveDevice_ = false;
    bool forgetting_ = false, shuttingDown_ = false, closingHost_ = false, protocolFailed_ = false;
    bool cleanupConfirmed_ = true;
#ifdef Q_OS_WIN
    std::unique_ptr<WindowsDirectConnection> direct_;
    std::optional<WindowsDirectBinding> directBinding_, retiredDirectBinding_;
    QElapsedTimer directStartup_;
    QTimer directPoll_;
    QPushButton *directButton_ = nullptr;
    QLabel *directDetails_ = nullptr;
#endif
};

}
