#include "lan_receiver_dialog.h"
#include "lan_playback_dialog.h"
#include "player_controls.h"
#include "subtitles/runtime_environment.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>
#include <QVBoxLayout>
#include <QtEndian>
#include <algorithm>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif

namespace luma {
namespace {
constexpr int port = 48762;
constexpr int scanMs = 2400;
constexpr int pairMs = 90000;
constexpr int stopMs = 2000;
constexpr qsizetype maxReplyBytes = 16384;
constexpr int maxDiscoveryBytes = 1024;

QLabel *plainLabel(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

QByteArray messageLine(const QJsonObject &message) {
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

bool selectedPrivateRoute(const QHostAddress &peer, const QHostAddress &local, int prefix, int interfaceIndex,
    bool allowDefaultRoute = false) {
    if (peer.isInSubnet(local, prefix))
        return true;
#ifdef Q_OS_WIN
    if (interfaceIndex <= 0)
        return false;
    SOCKADDR_INET source{}, destination{}, bestSource{};
    source.Ipv4.sin_family = destination.Ipv4.sin_family = AF_INET;
    source.Ipv4.sin_addr.s_addr = qToBigEndian(local.toIPv4Address());
    destination.Ipv4.sin_addr.s_addr = qToBigEndian(peer.toIPv4Address());
    MIB_IPFORWARD_ROW2 route{};
    return GetBestRoute2(nullptr, NET_IFINDEX(interfaceIndex), &source, &destination, 0, &route, &bestSource) == NO_ERROR &&
        route.InterfaceIndex == NET_IFINDEX(interfaceIndex) &&
        (allowDefaultRoute || route.DestinationPrefix.PrefixLength > 0);
#else
    Q_UNUSED(interfaceIndex);
    Q_UNUSED(allowDefaultRoute);
    return false;
#endif
}
}

LanReceiverDialog::LanReceiverDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("lanReceiverDialog");
    setWindowTitle("PC에서 받기");
    setMinimumWidth(480);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);

    auto *networkRow = new QHBoxLayout;
    interfaces_ = new ChoiceBox(this);
    interfaces_->setObjectName("lanReceiverInterface");
    interfaces_->setAccessibleName("검색할 네트워크");
    networkRow->addWidget(interfaces_, 1);
    scan_ = new QPushButton("다시 검색", this);
    scan_->setObjectName("lanReceiverScan");
    networkRow->addWidget(scan_);
    layout->addLayout(networkRow);

    auto *subnetRow = new QHBoxLayout;
    subnet_ = new QLineEdit(this);
    subnet_->setObjectName("lanReceiverSubnet");
    subnet_->setAccessibleName("사설 대역 직접 검색");
    subnet_->setPlaceholderText("사설 대역 직접 검색 · 예: 10.22.0.0/24");
    subnet_->setMaxLength(18);
    subnetRow->addWidget(subnet_, 1);
    scanSubnet_ = new QPushButton("대역 검색", this);
    scanSubnet_->setObjectName("lanReceiverScanSubnet");
    scanSubnet_->setAutoDefault(false);
    subnetRow->addWidget(scanSubnet_);
    layout->addLayout(subnetRow);

    pcs_ = new QListWidget(this);
    pcs_->setObjectName("lanReceiverPcs");
    pcs_->setAccessibleName("검색된 LumaPlayer PC");
    pcs_->setMinimumHeight(130);
    layout->addWidget(pcs_);

    address_ = new QLineEdit(this);
    address_->setObjectName("lanReceiverAddress");
    address_->setAccessibleName("PC 주소 직접 입력");
    address_->setPlaceholderText("PC 주소 직접 입력 · 192.168.0.20");
    address_->setMaxLength(24);
    layout->addWidget(address_);

    approval_ = new ChoiceBox(this);
    approval_->setObjectName("lanReceiverApproval");
    approval_->setAccessibleName("연결 승인 방식");
    approval_->addItem("기억한 승인 사용 · 새 PC는 이번만", "saved");
    approval_->addItem("매번 승인 요청", "once");
    approval_->addItem("승인 후 이 PC 기억", "remember");
    layout->addWidget(approval_);

    status_ = plainLabel({}, this);
    status_->setObjectName("lanReceiverStatus");
    layout->addWidget(status_);
    code_ = plainLabel({}, this);
    code_->setObjectName("lanReceiverCode");
    code_->setAlignment(Qt::AlignCenter);
    auto codeFont = code_->font();
    codeFont.setPointSize(27);
    codeFont.setBold(true);
    code_->setFont(codeFont);
    layout->addWidget(code_);

    auto *buttons = new QHBoxLayout;
    connect_ = new QPushButton("연결", this);
    connect_->setObjectName("lanReceiverConnect");
    connect_->setAutoDefault(false);
    confirm_ = new QPushButton("PC와 번호가 같음", this);
    confirm_->setObjectName("lanReceiverConfirm");
    confirm_->setAutoDefault(false);
    disconnect_ = new QPushButton("연결 끊기", this);
    disconnect_->setObjectName("lanReceiverDisconnect");
    auto *close = new QPushButton("닫기", this);
    buttons->addWidget(connect_);
    buttons->addWidget(confirm_);
    buttons->addWidget(disconnect_);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);

    discovery_.setProxy(QNetworkProxy::NoProxy);
    scanTimer_.setSingleShot(true);
    scanRetry_.setInterval(650);
    scanSend_.setInterval(20);
    pairingTimer_.setSingleShot(true);
    shutdownTimer_.setSingleShot(true);
    receiver_.setStandardErrorFile(QProcess::nullDevice());
    connect(scan_, &QPushButton::clicked, this, [this] {
        refreshInterfaces();
        startDiscovery();
    });
    connect(scanSubnet_, &QPushButton::clicked, this, [this] {
        refreshInterfaces();
        startDiscovery(true);
    });
    connect(subnet_, &QLineEdit::returnPressed, scanSubnet_, &QPushButton::click);
    connect(subnet_, &QLineEdit::textChanged, this, &LanReceiverDialog::updateControls);
    connect(interfaces_, &QComboBox::activated, this, [this] { startDiscovery(); });
    connect(&scanTimer_, &QTimer::timeout, this, [this] {
        stopDiscovery();
        if (phase_ == Phase::Idle)
            status_->setText(pcs_->count() ? "PC를 선택하고 연결하세요." : "검색된 PC가 없습니다. 주소를 직접 입력할 수 있습니다.");
    });
    connect(&scanRetry_, &QTimer::timeout, this, [this] {
        if (scanTimer_.isActive())
            discovery_.writeDatagram("LUMA-DISCOVER-1 " + nonce_, broadcast_, port);
    });
    connect(&scanSend_, &QTimer::timeout, this, &LanReceiverDialog::sendSubnetBatch);
    connect(&discovery_, &QUdpSocket::readyRead, this, &LanReceiverDialog::readDiscovery);
    connect(pcs_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            address_->setText(item->data(Qt::UserRole).toString());
    });
    connect(address_, &QLineEdit::textChanged, this, &LanReceiverDialog::updateControls);
    connect(address_, &QLineEdit::returnPressed, this, &LanReceiverDialog::startConnection);
    connect(connect_, &QPushButton::clicked, this, &LanReceiverDialog::startConnection);
    connect(confirm_, &QPushButton::clicked, this, &LanReceiverDialog::confirmPairing);
    connect(disconnect_, &QPushButton::clicked, this, [this] { stopConnection(); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::rejected, this, [this] { stopConnection(); });
    connect(&pairingTimer_, &QTimer::timeout, this, [this] { endConnection("PC 연결 확인 시간이 초과되었습니다."); });
    connect(&shutdownTimer_, &QTimer::timeout, &receiver_, &QProcess::kill);
    connect(&receiver_, &QProcess::started, this, [this] {
        if (phase_ == Phase::Connecting && receiver_.write(configuration_) != configuration_.size())
            endConnection("PC 연결 설정을 전달할 수 없습니다.");
        configuration_.clear();
    });
    connect(&receiver_, &QProcess::readyReadStandardOutput, this, &LanReceiverDialog::readReply);
    connect(&receiver_, &QProcess::stateChanged, this, &LanReceiverDialog::updateControls);
    connect(&receiver_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            endConnection("PC 재생 연결 도우미를 실행할 수 없습니다.");
    });
    connect(&receiver_, &QProcess::finished, this, [this] {
        shutdownTimer_.stop();
        if (phase_ != Phase::Idle)
            endConnection("PC와의 연결이 종료되었습니다.");
        updateControls();
    });
    updateControls();
}

LanReceiverDialog::~LanReceiverDialog() {
    stopConnection();
}

void LanReceiverDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (phase_ == Phase::Idle && receiver_.state() == QProcess::NotRunning) {
        refreshInterfaces();
        startDiscovery();
    }
}

void LanReceiverDialog::refreshInterfaces() {
    const auto preferred = interfaces_->currentData().toStringList().value(0);
    QSignalBlocker blocked(interfaces_);
    interfaces_->clear();
    int index = 0;
    for (const auto &adapter : lanPrivateInterfaces()) {
        const auto address = adapter.address.toString();
        if (address == preferred)
            index = interfaces_->count();
        interfaces_->addItem(adapter.name + " · " + address,
            QStringList{address, QString::number(adapter.prefixLength), QString::number(adapter.interfaceIndex)});
    }
    interfaces_->setCurrentIndex(interfaces_->count() ? index : -1);
}

QString LanReceiverDialog::selectedEndpoint(const QString &input, bool allowDefaultRoute) const {
    const auto selected = interfaces_->currentData().toStringList();
    if (selected.size() != 3)
        return {};
    auto address = input.trimmed();
    const auto suffix = ':' + QString::number(port);
    if (address.endsWith(suffix))
        address.chop(suffix.size());
    const QHostAddress ip(address), local(selected[0]);
    if (ip.toString() != address || lanPrivateSubnet(ip, 32).isEmpty() ||
        !selectedPrivateRoute(ip, local, selected[1].toInt(), selected[2].toInt(), allowDefaultRoute))
        return {};
    return address + suffix;
}

void LanReceiverDialog::startDiscovery(bool specificSubnet) {
    if (phase_ != Phase::Idle || receiver_.state() != QProcess::NotRunning)
        return;
    stopDiscovery();
    pcs_->clear();
    const auto selected = interfaces_->currentData().toStringList();
    if (selected.size() != 3) {
        status_->setText("연결된 사설 IPv4 주소가 없습니다.");
        updateControls();
        return;
    }
    scanLocal_ = QHostAddress(selected[0]);
    scanPrefix_ = selected[1].toInt();
    scanInterface_ = selected[2].toInt();
    const auto enteredSubnet = specificSubnet ? lanPrivatePeerSubnet(subnet_->text()) : QString{};
    if (specificSubnet && enteredSubnet.isEmpty()) {
        status_->setText("사설 IPv4 네트워크 주소와 /24–/32를 입력하세요.");
        subnet_->setFocus();
        updateControls();
        return;
    }
    if ((!specificSubnet && (scanPrefix_ < 1 || scanPrefix_ > 30)) ||
        !discovery_.bind(scanLocal_, 0, QUdpSocket::DontShareAddress)) {
        status_->setText("자동 검색을 사용할 수 없습니다. PC 주소를 직접 입력하세요.");
        updateControls();
        return;
    }
    nonce_ = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-').toLatin1();
    if (specificSubnet) {
        const auto [network, prefix] = QHostAddress::parseSubnet(enteredSubnet);
        const quint32 count = quint32(1) << (32 - prefix);
        const quint32 first = prefix < 31 ? 1 : 0;
        const quint32 end = prefix < 31 ? count - 1 : count;
        for (quint32 offset = first; offset < end; ++offset) {
            const quint32 candidate = network.toIPv4Address() + offset;
            if (candidate != scanLocal_.toIPv4Address())
                scanCandidates_.append(candidate);
        }
        status_->setText("입력한 사설 대역에서 LumaPlayer PC를 검색하는 중…");
        scanTimer_.start(scanMs);
        scanSend_.start();
        sendSubnetBatch();
        updateControls();
        return;
    }
    const quint32 mask = ~quint32(0) << (32 - scanPrefix_);
    broadcast_ = QHostAddress(scanLocal_.toIPv4Address() | ~mask);
    if (lanPrivateSubnet(broadcast_, 32).isEmpty() ||
        discovery_.writeDatagram("LUMA-DISCOVER-1 " + nonce_, broadcast_, port) < 0) {
        stopDiscovery();
        status_->setText("자동 검색을 사용할 수 없습니다. PC 주소를 직접 입력하세요.");
        updateControls();
        return;
    }
    status_->setText("같은 네트워크에서 PC를 검색하는 중…");
    scanTimer_.start(scanMs);
    scanRetry_.start();
    updateControls();
}

void LanReceiverDialog::stopDiscovery() {
    scanTimer_.stop();
    scanRetry_.stop();
    scanSend_.stop();
    discovery_.close();
    nonce_.clear();
    scanCandidates_.clear();
    scanSent_.clear();
    scanCursor_ = 0;
}

void LanReceiverDialog::sendSubnetBatch() {
    if (!scanTimer_.isActive() || !scanSend_.isActive())
        return;
    const auto request = "LUMA-DISCOVER-1 " + nonce_;
    for (int sent = 0; sent < 4 && scanCursor_ < scanCandidates_.size(); ++sent) {
        const auto candidate = scanCandidates_[scanCursor_++];
        const QHostAddress peer(candidate);
        if (selectedPrivateRoute(peer, scanLocal_, scanPrefix_, scanInterface_) &&
            discovery_.writeDatagram(request, peer, port) == request.size())
            scanSent_.insert(candidate);
    }
    if (scanCursor_ == scanCandidates_.size()) {
        scanSend_.stop();
        if (scanSent_.isEmpty()) {
            stopDiscovery();
            status_->setText("이 대역으로 연결되는 경로가 없습니다. VPN과 선택한 네트워크를 확인하세요.");
        }
    }
}

void LanReceiverDialog::readDiscovery() {
    for (int read = 0; read < 64 && discovery_.hasPendingDatagrams() && scanTimer_.isActive() && pcs_->count() < 32; ++read) {
        const auto packet = discovery_.receiveDatagram(maxDiscoveryBytes + 1);
        const auto sender = packet.senderAddress();
        if (scanCandidates_.isEmpty() ? !sender.isInSubnet(scanLocal_, scanPrefix_)
                                     : !scanSent_.contains(sender.toIPv4Address()))
            continue;
        const auto endpoint = selectedEndpoint(sender.toString());
        if (endpoint.isEmpty() || endpoint == ownEndpoint_ || packet.data().size() > maxDiscoveryBytes)
            continue;
        const auto response = QJsonDocument::fromJson(packet.data()).object();
        const auto name = response.value("name").toString().simplified();
        if (response.value("protocol").toString() != "luma-discovery-1" ||
            response.value("nonce").toString().toLatin1() != nonce_ ||
            response.value("port").toInt() != port || name.isEmpty() || name.size() > 96 ||
            std::any_of(name.begin(), name.end(), [](QChar c) {
                return c.category() == QChar::Other_Control || c.category() == QChar::Other_Format;
            }))
            continue;
        bool listed = false;
        for (int index = 0; index < pcs_->count(); ++index)
            listed |= pcs_->item(index)->data(Qt::UserRole).toString() == endpoint;
        if (!listed) {
            auto *item = new QListWidgetItem(name + " · " + endpoint, pcs_);
            item->setData(Qt::UserRole, endpoint);
        }
    }
    if (discovery_.hasPendingDatagrams() && scanTimer_.isActive() && pcs_->count() < 32)
        QTimer::singleShot(0, this, &LanReceiverDialog::readDiscovery);
}

void LanReceiverDialog::startConnection() {
    if (phase_ != Phase::Idle || receiver_.state() != QProcess::NotRunning)
        return;
    const auto endpoint = selectedEndpoint(address_->text(), true);
    if (endpoint.isEmpty() || endpoint == ownEndpoint_) {
        status_->setText(endpoint == ownEndpoint_ && !endpoint.isEmpty() ? "다른 PC의 공유 주소를 입력하세요."
                                                                       : "선택한 네트워크에서 접근할 수 있는 사설 IPv4 주소를 입력하세요.");
        address_->setFocus();
        return;
    }
    const auto runtime = QApplication::applicationDirPath() + "/runtime";
#ifdef Q_OS_WIN
    const auto executable = runtime + "/tools/lumaplayer-mobile-host.exe";
#else
    const auto executable = runtime + "/tools/lumaplayer-mobile-host";
#endif
    const auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QFileInfo(executable).isFile() || dataPath.isEmpty()) {
        status_->setText("PC 연결 도우미 또는 승인 저장 위치를 확인할 수 없습니다.");
        return;
    }
    stopDiscovery();
    endpoint_ = endpoint;
    const auto mode = approval_->currentData().toString();
    const auto name = QSysInfo::machineHostName().simplified().left(63);
    startedFresh_ = mode == "once" || freshEndpoint_ == endpoint_;
    configuration_ = messageLine(QJsonObject{
        {"mode", "receiver"}, {"endpoint", endpoint_},
        {"localAddress", interfaces_->currentData().toStringList().value(0)},
        {"name", name.isEmpty() ? QString("LumaPlayer PC") : name},
        {"store", QDir(dataPath).absoluteFilePath("lan-receiver-pairings.dpapi")},
        {"remember", mode == "remember"}, {"fresh", startedFresh_}
    });
    reply_.clear();
    phase_ = Phase::Connecting;
    status_->setText("PC에 연결하는 중…");
    receiver_.setProcessEnvironment(localRuntimeEnvironment(runtime));
    pairingTimer_.start(30000);
    receiver_.start(executable, QStringList{});
    updateControls();
}

void LanReceiverDialog::confirmPairing() {
    if (phase_ != Phase::Comparing || receiver_.state() != QProcess::Running)
        return;
    const auto encoded = messageLine(QJsonObject{{"command", "confirm"}});
    if (receiver_.write(encoded) != encoded.size()) {
        endConnection("번호 확인을 전달할 수 없습니다.");
        return;
    }
    phase_ = Phase::Confirmed;
    status_->setText("번호를 확인했습니다. 공유하는 PC의 승인을 기다리는 중입니다.");
    updateControls();
}

void LanReceiverDialog::readReply() {
    const auto incoming = receiver_.readAllStandardOutput();
    if (phase_ == Phase::Idle)
        return;
    reply_ += incoming;
    while (phase_ != Phase::Idle) {
        const auto end = reply_.indexOf('\n');
        if (end < 0)
            break;
        const auto document = end <= maxReplyBytes ? QJsonDocument::fromJson(reply_.left(end)) : QJsonDocument{};
        reply_.remove(0, end + 1);
        if (!document.isObject()) {
            endConnection("PC 재생 연결 도우미의 응답이 잘못되었습니다.");
            return;
        }
        handleReply(document.object());
    }
    if (reply_.size() > maxReplyBytes)
        endConnection("PC 재생 연결 도우미의 응답이 잘못되었습니다.");
}

void LanReceiverDialog::handleReply(const QJsonObject &message) {
    const auto status = message.value("status").toString();
    if (status == "pairCode" && phase_ == Phase::Connecting) {
        static const QRegularExpression validCode("^[0-9]{6}$");
        const auto code = message.value("code").toString();
        if (validCode.match(code).hasMatch()) {
            phase_ = Phase::Comparing;
            code_->setText(code);
            status_->setText("공유하는 PC에 같은 번호가 표시되는지 확인하세요.");
            pairingTimer_.start(pairMs);
            updateControls();
            return;
        }
    } else if (status == "connected" && (phase_ == Phase::Confirmed || (phase_ == Phase::Connecting && !startedFresh_))) {
        phase_ = Phase::Waiting;
        pairingTimer_.stop();
        freshEndpoint_.clear();
        status_->setText("연결되었습니다. 공유하는 PC가 미디어를 열기를 기다립니다.");
        updateControls();
        return;
    } else if (status == "playback" && (phase_ == Phase::Waiting || phase_ == Phase::Playback)) {
        LanReceiverPlayback playback;
        playback.source.url = QUrl(message.value("url").toString(), QUrl::StrictMode);
        playback.source.mimeType = message.value("mimeType").toString();
        playback.title = message.value("title").toString().simplified().left(512);
        playback.seekable = message.value("seekable").toBool();
        const auto subtitles = message.value("subtitles").toArray();
        if (subtitles.size() <= 8) {
            for (const auto &value : subtitles) {
                const auto subtitle = value.toObject();
                playback.source.subtitles.append({QUrl(subtitle.value("url").toString(), QUrl::StrictMode),
                    subtitle.value("label").toString().simplified().left(128)});
            }
            if (validLanMediaSource(playback.source)) {
                phase_ = Phase::Playback;
                status_->setText("다른 PC에서 재생하는 중입니다.");
                updateControls();
                emit playbackReady(playback);
                return;
            }
        }
    } else if (status == "pairingRequired") {
        freshEndpoint_ = endpoint_;
        endConnection("기억한 승인을 사용할 수 없습니다. 다시 연결하여 번호를 확인하세요.");
        return;
    } else if (status == "ended" || status == "error") {
        endConnection(status == "ended" ? "공유하는 PC가 연결을 종료했습니다."
                                        : "PC에 연결할 수 없습니다. 공유 상태와 네트워크를 확인하세요.");
        return;
    }
    endConnection("PC 재생 연결 도우미의 응답이 잘못되었습니다.");
}

void LanReceiverDialog::endConnection(const QString &message) {
    const bool played = phase_ == Phase::Playback;
    phase_ = Phase::Idle;
    pairingTimer_.stop();
    code_->clear();
    configuration_.clear();
    reply_.clear();
    endpoint_.clear();
    if (receiver_.state() != QProcess::NotRunning) {
        receiver_.closeWriteChannel();
        shutdownTimer_.start(stopMs);
    }
    status_->setText(message);
    updateControls();
    if (played)
        emit playbackEnded(message);
}

void LanReceiverDialog::stopConnection(const QString &message) {
    stopDiscovery();
    if (phase_ != Phase::Idle || receiver_.state() != QProcess::NotRunning)
        endConnection(message.isEmpty() ? "PC 연결을 종료했습니다." : message);
    if (receiver_.state() != QProcess::NotRunning && !receiver_.waitForFinished(stopMs)) {
        receiver_.kill();
        receiver_.waitForFinished(1000);
    }
}

void LanReceiverDialog::updateControls() {
    const bool idle = phase_ == Phase::Idle && receiver_.state() == QProcess::NotRunning;
    const bool comparing = phase_ == Phase::Comparing;
    interfaces_->setEnabled(idle);
    scan_->setEnabled(idle);
    subnet_->setEnabled(idle);
    scanSubnet_->setEnabled(idle && !subnet_->text().trimmed().isEmpty());
    pcs_->setEnabled(idle);
    address_->setEnabled(idle);
    approval_->setEnabled(idle);
    connect_->setVisible(idle);
    connect_->setEnabled(idle && !address_->text().trimmed().isEmpty());
    confirm_->setVisible(comparing);
    disconnect_->setVisible(!idle);
    code_->setVisible(comparing || phase_ == Phase::Confirmed);
}

}
