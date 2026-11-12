#pragma once

#include "media/playback.h"
#include <QDialog>
#include <QProcess>
#include <QTimer>
#include <QUdpSocket>

class QComboBox;
class QJsonObject;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QShowEvent;

namespace luma {

struct LanReceiverPlayback {
    LanMediaSource source;
    QString title;
    bool seekable = false;
};

class LanReceiverDialog final : public QDialog {
    Q_OBJECT
public:
    explicit LanReceiverDialog(QWidget *parent);
    ~LanReceiverDialog() override;
    void setOwnEndpoint(const QString &endpoint) { ownEndpoint_ = endpoint; }
    bool hasPlayback() const { return phase_ == Phase::Playback; }
    void stopConnection(const QString &message = {});

signals:
    void playbackReady(const LanReceiverPlayback &playback);
    void playbackEnded(const QString &message);

protected:
    void showEvent(QShowEvent *event) override;

private:
    enum class Phase { Idle, Connecting, Comparing, Confirmed, Waiting, Playback };
    void refreshInterfaces();
    void startDiscovery();
    void stopDiscovery();
    void readDiscovery();
    void startConnection();
    void confirmPairing();
    void readReply();
    void handleReply(const QJsonObject &message);
    void endConnection(const QString &message);
    void updateControls();
    QString selectedEndpoint(const QString &address) const;

    QProcess receiver_;
    QUdpSocket discovery_;
    QTimer scanTimer_, scanRetry_, pairingTimer_, shutdownTimer_;
    QByteArray configuration_, reply_, nonce_;
    QHostAddress broadcast_;
    QString ownEndpoint_, endpoint_, freshEndpoint_;
    Phase phase_ = Phase::Idle;
    bool startedFresh_ = false;
    QComboBox *interfaces_, *approval_;
    QListWidget *pcs_;
    QLineEdit *address_;
    QLabel *status_, *code_;
    QPushButton *scan_, *connect_, *confirm_, *disconnect_;
};

}
