#include "lan_playback_dialog.h"

#include "player_controls.h"
#include "subtitles/runtime_environment.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGridLayout>
#include <QHash>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTreeWidget>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#ifdef Q_OS_WIN
#include <string>
#include <windows.h>
#endif

namespace luma {
namespace {
constexpr int port = 48762;
constexpr int startupWaitMs = 10000;
constexpr int discoveryRefreshMs = 5000;
constexpr int stopWaitMs = 4000;
constexpr int pairWaitMs = 90000;
constexpr qsizetype maxReplyBytes = 64 * 1024;
constexpr qsizetype maxCaptionBytes = 1024 * 1024;
constexpr qsizetype maxChapters = 512;
constexpr int activeDeviceRole = Qt::UserRole + 1;
constexpr int rememberedDeviceRole = Qt::UserRole + 2;
constexpr int deviceOptionsRole = Qt::UserRole + 3;

class DeviceDelaySpinBox final : public QSpinBox {
public:
    using QSpinBox::QSpinBox;
protected:
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};

QByteArray messageLine(const QJsonObject &message) {
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

QString timestamp(qint64 ms) {
    const QChar zero('0');
    return QStringLiteral("%1:%2:%3.%4").arg(ms / 3600000, 2, 10, zero)
        .arg((ms / 60000) % 60, 2, 10, zero).arg((ms / 1000) % 60, 2, 10, zero).arg(ms % 1000, 3, 10, zero);
}

QLabel *plainLabel(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

#ifdef Q_OS_WIN
QString bundledFfmpeg() {
    const auto path = QDir::toNativeSeparators(QApplication::applicationDirPath() + "/runtime/tools/ffmpeg.exe").toStdWString();
    const auto file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};
    const auto close = qScopeGuard([file] { CloseHandle(file); });
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return {};
    // Qt can leave a directory junction in its canonical path on Windows.
    const auto capacity = GetFinalPathNameByHandleW(file, nullptr, 0, VOLUME_NAME_DOS);
    if (!capacity)
        return {};
    std::wstring buffer(capacity, L'\0');
    const auto length = GetFinalPathNameByHandleW(file, buffer.data(), capacity, VOLUME_NAME_DOS);
    if (length < 7 || length >= capacity)
        return {};
    const auto resolved = QString::fromWCharArray(buffer.data(), length);
    if (!resolved.startsWith(QStringLiteral("\\\\?\\")) || resolved[5] != ':' || resolved[6] != '\\')
        return {};
    return QDir::fromNativeSeparators(resolved.mid(4));
}
#endif
}

QString lanPrivateSubnet(const QHostAddress &address, int prefixLength) {
    if (address.protocol() != QAbstractSocket::IPv4Protocol || prefixLength < 1 || prefixLength > 32)
        return {};
    const quint32 ip = address.toIPv4Address();
    const bool privateIp = (ip >> 24) == 0x0a || (ip >> 20) == 0xac1 || (ip >> 16) == 0xc0a8;
    if (!privateIp)
        return {};
    const quint32 mask = ~quint32(0) << (32 - prefixLength);
    return QHostAddress(ip & mask).toString() + '/' + QString::number(prefixLength);
}

QString lanPrivatePeerSubnet(const QString &value) {
    const auto input = value.trimmed();
    const auto [address, prefix] = QHostAddress::parseSubnet(input);
    const auto canonical = prefix >= 24 ? lanPrivateSubnet(address, prefix) : QString{};
    return canonical == input ? canonical : QString{};
}

QList<LanPrivateInterface> lanPrivateInterfaces() {
    QList<LanPrivateInterface> interfaces;
    QSet<QHostAddress> listed;
    for (const auto &network : QNetworkInterface::allInterfaces()) {
        if (!network.flags().testFlag(QNetworkInterface::IsUp) || network.flags().testFlag(QNetworkInterface::IsLoopBack))
            continue;
        for (const auto &entry : network.addressEntries()) {
            const auto address = entry.ip();
            if (lanPrivateSubnet(address, entry.prefixLength()).isEmpty() || listed.contains(address))
                continue;
            listed.insert(address);
            const LanPrivateInterface adapter{network.humanReadableName(), address, entry.prefixLength(), network.index()};
            if (network.type() == QNetworkInterface::Wifi)
                interfaces.prepend(adapter);
            else
                interfaces.append(adapter);
        }
    }
    return interfaces;
}

QString lanCaptionTrack(const QVector<SubtitleCue> &cues) {
    QString result = QStringLiteral("WEBVTT\n\n");
    qsizetype bytes = result.size();
    for (const auto &cue : cues) {
        if (cue.startMs < 0 || cue.endMs <= cue.startMs)
            continue;
        QStringList lines;
        for (auto line : cue.text.split('\n')) {
            line = line.trimmed();
            line.remove(QChar::Null);
            if (line.isEmpty())
                continue;
            lines.append(line.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;"));
        }
        if (lines.isEmpty())
            continue;
        const auto entry = timestamp(cue.startMs) + " --> " + timestamp(cue.endMs) + '\n' + lines.join('\n') + "\n\n";
        bytes += entry.toUtf8().size();
        if (bytes > maxCaptionBytes)
            return {};
        result += entry;
    }
    return bytes > 8 ? result : QString{};
}

bool lanLocalFileSupported(const QString &source) {
    if (!QDir::isAbsolutePath(source) || source.startsWith("//") || source.startsWith("\\\\"))
        return false;
    static const QSet<QString> extensions{"mp4", "m4v", "mov", "mkv", "webm", "ts", "m2ts"};
    const QFileInfo file(source);
    return extensions.contains(file.suffix().toLower()) && file.isFile();
}

LanPlaybackDialog::LanPlaybackDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("lanPlaybackDialog");
    setWindowTitle("내부망 재생");
    setMinimumWidth(570);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);
    layout->addWidget(plainLabel("같은 네트워크의 Android 앱이나 다른 PC에서 이 PC를 선택하세요.", this));

    interfaces_ = new ChoiceBox(this);
    interfaces_->setObjectName("lanPrivateInterface");
    interfaces_->setAccessibleName("PC 사설 IPv4 주소");
    layout->addWidget(interfaces_);
    peerSubnet_ = new QLineEdit(this);
    peerSubnet_->setObjectName("lanPeerSubnet");
    peerSubnet_->setAccessibleName("VPN 연결 허용 범위");
    peerSubnet_->setPlaceholderText("VPN 연결 허용 범위 · 예: 10.22.0.0/24 (선택)");
    peerSubnet_->setMaxLength(18);
    layout->addWidget(peerSubnet_);

    address_ = plainLabel({}, this);
    address_->setObjectName("lanPlaybackAddress");
    layout->addWidget(address_);
    status_ = plainLabel("기기 연결을 시작할 수 있습니다.", this);
    status_->setObjectName("lanPlaybackStatus");
    layout->addWidget(status_);

    audioMode_ = new ChoiceBox(this);
    audioMode_->setAccessibleName("Blu-ray 모바일 오디오");
    audioMode_->addItem("호환 음성 · AAC 스테레오", "compatible");
    audioMode_->addItem("원본 음성 · 지원 기기에서 Atmos/DTS", "original");
    audioMode_->hide();
    layout->addWidget(audioMode_);

    auto *shareButtons = new QHBoxLayout;
    start_ = new QPushButton("공유 시작", this);
    start_->setObjectName("lanStart");
    stop_ = new QPushButton("공유 중지", this);
    stop_->setObjectName("lanStop");
    shareButtons->addWidget(start_);
    shareButtons->addWidget(stop_);
    shareButtons->addStretch();
    auto *receive = new QPushButton("PC에서 받기", this);
    receive->setObjectName("lanReceive");
    shareButtons->addWidget(receive);
#ifdef Q_OS_WIN
    auto *wirelessDisplay = new QPushButton("Windows 무선 디스플레이", this);
    wirelessDisplay->setObjectName("lanWindowsWirelessDisplay");
    auto *wirelessMenu = new QMenu(wirelessDisplay);
    wirelessDisplay->setMenu(wirelessMenu);
    const auto addSettingAction = [this, wirelessMenu](const QString &label, const QString &name, const QUrl &url) {
        auto *action = wirelessMenu->addAction(label);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, [this, url] {
            if (!QDesktopServices::openUrl(url))
                status_->setText("Windows 무선 디스플레이 설정을 열 수 없습니다.");
        });
    };
    addSettingAction("무선 디스플레이에 보내기 (Win+K)", "lanWindowsWirelessDisplaySend", QUrl("ms-settings:display"));
    addSettingAction("이 PC를 무선 디스플레이로 사용", "lanWindowsWirelessDisplayReceive", QUrl("ms-settings:project"));
    shareButtons->addWidget(wirelessDisplay);
#endif
    layout->addLayout(shareButtons);
    remoteStart_ = new QCheckBox("연결된 기기에서 현재 미디어 재생 시작 허용", this);
    remoteStart_->setObjectName("lanAllowRemoteStart");
    layout->addWidget(remoteStart_);
    connect(remoteStart_, &QCheckBox::toggled, this, [this](bool enabled) {
        remoteStartGrant_ = enabled ? QUuid::createUuid().toString(QUuid::Id128) : QString{};
        if (ready_)
            writeMessage(QJsonObject{{"command", "allowRemoteStart"}, {"grant", remoteStartGrant_}});
    });
    remoteControl_ = new QCheckBox("연결된 기기에서 PC 플레이어 조작 허용", this);
    remoteControl_->setObjectName("lanRemoteControl");
    layout->addWidget(remoteControl_);
    connect(remoteControl_, &QCheckBox::toggled, this, [this](bool enabled) {
        remoteControlGrant_ = enabled ? QUuid::createUuid().toString(QUuid::Id128) : QString{};
        if (ready_)
            writeMessage(QJsonObject{{"command", "allowRemoteControl"}, {"grant", remoteControlGrant_}});
    });
#ifdef Q_OS_WIN
    directButton_ = new QPushButton("공유기 없이 휴대폰 연결", this);
    directButton_->setObjectName("lanWindowsDirect");
    directButton_->setVisible(WindowsDirectConnection::available());
    layout->addWidget(directButton_, 0, Qt::AlignLeft);
    directDetails_ = plainLabel({}, this);
    directDetails_->setObjectName("lanWindowsDirectStatus");
    directDetails_->hide();
    layout->addWidget(directDetails_);
    directPoll_.setInterval(200);
    connect(directButton_, &QPushButton::clicked, this, &LanPlaybackDialog::toggleDirect);
    connect(&directPoll_, &QTimer::timeout, this, &LanPlaybackDialog::pollDirect);
#endif

    devices_ = new QTreeWidget(this);
    devices_->setObjectName("lanDevices");
    devices_->setAccessibleName("연결 및 기억한 기기");
    devices_->setHeaderLabels({"기기", "주소", "상태"});
    devices_->setRootIsDecorated(false);
    devices_->setUniformRowHeights(true);
    devices_->setMinimumHeight(120);
    devices_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    devices_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(devices_);
    auto *deviceButtons = new QHBoxLayout;
    disconnectDevice_ = new QPushButton("선택 기기 연결 끊기", this);
    disconnectDevice_->setObjectName("lanDisconnectDevice");
    forgetDevice_ = new QPushButton("선택 기기 기억 삭제", this);
    forgetDevice_->setObjectName("lanForgetDevice");
    deviceButtons->addWidget(disconnectDevice_);
    deviceButtons->addWidget(forgetDevice_);
    deviceButtons->addStretch();
    layout->addLayout(deviceButtons);

    deviceOptionsBox_ = new QGroupBox("선택 기기 재생 설정", this);
    deviceOptionsBox_->setObjectName("lanDeviceOptions");
    auto *optionsLayout = new QGridLayout(deviceOptionsBox_);
    const auto addChoice = [this, optionsLayout](const QString &label, const QString &objectName, int row, int column) {
        auto *choice = new ChoiceBox(deviceOptionsBox_);
        choice->setObjectName(objectName);
        choice->setAccessibleName(label);
        choice->addItem("기기 설정 사용");
        auto *caption = new QLabel(label, deviceOptionsBox_);
        caption->setBuddy(choice);
        optionsLayout->addWidget(caption, row, column);
        optionsLayout->addWidget(choice, row, column + 1);
        return choice;
    };
    deviceProfile_ = addChoice("화질", "lanDeviceProfile", 0, 0);
    deviceProfile_->addItem("자동", "auto");
    deviceProfile_->addItem("원본", "original");
    for (const auto *profile : {"1080p", "720p", "480p"})
        deviceProfile_->addItem(profile, profile);
    deviceHdr_ = addChoice("HDR", "lanDeviceHdr", 0, 2);
    deviceHdr_->addItem("자동", "auto");
    deviceHdr_->addItem("원본 유지", "original");
    deviceHdr_->addItem("SDR", "sdr");
    deviceSkip_ = addChoice("건너뛰기", "lanDeviceSkip", 1, 0);
    for (const int seconds : {5, 10, 15, 30})
        deviceSkip_->addItem(QString::number(seconds) + "초", seconds);
    deviceFill_ = addChoice("화면", "lanDeviceFill", 1, 2);
    deviceFill_->addItem("원본 비율", false);
    deviceFill_->addItem("화면 채우기", true);
    deviceSubtitleDelayEnabled_ = new QCheckBox("자막 지연 지정", deviceOptionsBox_);
    deviceSubtitleDelayEnabled_->setObjectName("lanDeviceSubtitleDelayEnabled");
    deviceSubtitleDelay_ = new DeviceDelaySpinBox(deviceOptionsBox_);
    deviceSubtitleDelay_->setObjectName("lanDeviceSubtitleDelay");
    deviceSubtitleDelay_->setAccessibleName("자막 지연");
    deviceSubtitleDelay_->setRange(-5000, 5000);
    deviceSubtitleDelay_->setSingleStep(100);
    deviceSubtitleDelay_->setSuffix(" ms");
    optionsLayout->addWidget(deviceSubtitleDelayEnabled_, 2, 0);
    optionsLayout->addWidget(deviceSubtitleDelay_, 2, 1);
    resetOptions_ = new QPushButton("기기 기본값", deviceOptionsBox_);
    resetOptions_->setObjectName("lanDeviceResetOptions");
    applyOptions_ = new QPushButton("적용", deviceOptionsBox_);
    applyOptions_->setObjectName("lanDeviceApplyOptions");
    optionsLayout->addWidget(resetOptions_, 2, 2);
    optionsLayout->addWidget(applyOptions_, 2, 3);
    optionsLayout->setColumnStretch(1, 1);
    optionsLayout->setColumnStretch(3, 1);
    layout->addWidget(deviceOptionsBox_);

    auto *connectionButtons = new QHBoxLayout;
    retry_ = new QPushButton("주소 적용 / 다시 시작", this);
    retry_->setObjectName("lanRetry");
    forget_ = new QPushButton("모든 연결 기록 지우기", this);
    forget_->setObjectName("lanForget");
    auto *close = new QPushButton("닫기", this);
    connectionButtons->addWidget(retry_);
    connectionButtons->addWidget(forget_);
    connectionButtons->addStretch();
    connectionButtons->addWidget(close);
    layout->addLayout(connectionButtons);

    watchdog_.setSingleShot(true);
    deviceRefresh_.setInterval(5000);
    connect(&deviceRefresh_, &QTimer::timeout, this, &LanPlaybackDialog::requestDevices);
    host_.setStandardErrorFile(QProcess::nullDevice());
    connect(start_, &QPushButton::clicked, this, &LanPlaybackDialog::shareRequested);
    connect(stop_, &QPushButton::clicked, this, &LanPlaybackDialog::stopSharing);
    connect(receive, &QPushButton::clicked, this, &LanPlaybackDialog::receiveRequested);
    connect(disconnectDevice_, &QPushButton::clicked, this, [this] { manageDevice(false); });
    connect(forgetDevice_, &QPushButton::clicked, this, [this] { manageDevice(true); });
    connect(devices_, &QTreeWidget::currentItemChanged, this, [this] {
        syncDeviceOptions();
        updateControls();
    });
    for (auto *choice : {deviceProfile_, deviceHdr_, deviceSkip_, deviceFill_})
        connect(choice, &QComboBox::currentIndexChanged, this, &LanPlaybackDialog::updateControls);
    connect(deviceSubtitleDelayEnabled_, &QCheckBox::toggled, this, &LanPlaybackDialog::updateControls);
    connect(deviceSubtitleDelay_, &QSpinBox::valueChanged, this, &LanPlaybackDialog::updateControls);
    connect(applyOptions_, &QPushButton::clicked, this, &LanPlaybackDialog::applyDeviceOptions);
    connect(resetOptions_, &QPushButton::clicked, this, [this] {
        loadDeviceOptions({});
        updateControls();
    });
    connect(retry_, &QPushButton::clicked, this, &LanPlaybackDialog::restartDiscovery);
    connect(peerSubnet_, &QLineEdit::returnPressed, retry_, &QPushButton::click);
    connect(forget_, &QPushButton::clicked, this, &LanPlaybackDialog::forgetDevices);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::rejected, this, [this] {
        if (pairingDialog_ && pairingDialog_->parentWidget() == this)
            pairingDialog_->reject();
    });
    connect(&host_, &QProcess::started, this, [this] {
        if (!closingHost_ && !shuttingDown_ && host_.write(startupConfiguration_) != startupConfiguration_.size())
            protocolError();
        startupConfiguration_.clear();
    });
    connect(&host_, &QProcess::readyReadStandardOutput, this, &LanPlaybackDialog::readReply);
    connect(&host_, &QProcess::stateChanged, this, &LanPlaybackDialog::updateControls);
    connect(&host_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        watchdog_.stop();
        startupConfiguration_.clear();
        status_->setText("모바일 송출 도우미를 실행할 수 없습니다.");
        updateControls();
    });
    connect(&host_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        disarmRemoteStart();
        disarmRemoteControl();
        watchdog_.stop();
        const bool stoppedCleanly = exitStatus == QProcess::NormalExit && exitCode == 0;
        const bool mediaReleased = stoppedCleanly || mediaState_ == MediaState::Preparing;
        if (ready_ && !closingHost_)
            status_->setText(mediaState_ != MediaState::Idle && !mediaReleased
                ? "모바일 송출이 비정상 종료되었습니다."
                : "휴대폰 연결이 종료되었습니다.");
        ready_ = false;
        forgetting_ = false;
        updateDevices({});
        startupConfiguration_.clear();
        reply_.clear();
        dismissPairRequest();
        finishShare(mediaReleased);
        updateControls();
    });
    connect(&watchdog_, &QTimer::timeout, this, [this] {
        if (!ready_) {
            if (!protocolFailed_)
                status_->setText("휴대폰 연결을 시작하는 데 시간이 너무 오래 걸립니다.");
            host_.kill();
        } else {
            status_->setText("모바일 송출 응답이 없어 연결을 종료합니다.");
            closeHost();
            if (!cleanupConfirmed_)
                status_->setText("모바일 송출이 비정상 종료되었습니다.");
        }
    });
    discoveryRefresh_.setInterval(discoveryRefreshMs);
    connect(&discoveryRefresh_, &QTimer::timeout, this, &LanPlaybackDialog::maintainDiscovery);
    refreshInterfaces();
    updateControls();
}

LanPlaybackDialog::~LanPlaybackDialog() {
    shuttingDown_ = true;
    discoveryRefresh_.stop();
    deviceRefresh_.stop();
#ifdef Q_OS_WIN
    directPoll_.stop();
#endif
    // Item models can emit selection changes while QWidget deletes its children.
    devices_->disconnect(this);
    for (auto *choice : {deviceProfile_, deviceHdr_, deviceSkip_, deviceFill_})
        choice->disconnect(this);
    deviceSubtitleDelayEnabled_->disconnect(this);
    deviceSubtitleDelay_->disconnect(this);
    dismissPairRequest();
    closeHost();
#ifdef Q_OS_WIN
    direct_.reset();
#endif
}

void LanPlaybackDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    requestDevices();
    deviceRefresh_.start();
}

void LanPlaybackDialog::hideEvent(QHideEvent *event) {
    deviceRefresh_.stop();
    QDialog::hideEvent(event);
}

void LanPlaybackDialog::refreshInterfaces() {
    const auto preferred = interfaces_->currentData().toStringList().value(0);
    QSignalBlocker blocked(interfaces_);
    interfaces_->clear();
#ifdef Q_OS_WIN
    bool retiredPresent = false;
#endif
    for (const auto &adapter : lanPrivateInterfaces()) {
#ifdef Q_OS_WIN
        if (retiredDirectBinding_ && quint32(adapter.interfaceIndex) == retiredDirectBinding_->interfaceIndex &&
            adapter.address.toIPv4Address() == retiredDirectBinding_->address) {
            retiredPresent = true;
            continue;
        }
#endif
        const auto address = adapter.address.toString();
        const auto subnet = lanPrivateSubnet(adapter.address, adapter.prefixLength);
        interfaces_->addItem(adapter.name + " · " + address, QStringList{address, subnet});
    }
#ifdef Q_OS_WIN
    if (!retiredPresent)
        retiredDirectBinding_.reset();
#endif
    int selected = 0;
    for (int index = 0; index < interfaces_->count(); ++index) {
        if (interfaces_->itemData(index).toStringList().value(0) == preferred) {
            selected = index;
            break;
        }
    }
    interfaces_->setCurrentIndex(interfaces_->count() ? selected : -1);
}

void LanPlaybackDialog::startDiscovery() {
    if (shuttingDown_ || closingHost_)
        return;
    if (!discoveryRefresh_.isActive())
        discoveryRefresh_.start();
    maintainDiscovery();
}

void LanPlaybackDialog::maintainDiscovery() {
    if (shuttingDown_ || closingHost_ || forgetting_ || mediaState_ != MediaState::Idle || pairingDialog_)
        return;
#ifdef Q_OS_WIN
    if (direct_)
        return;
#endif
    if (host_.state() != QProcess::NotRunning) {
        if (!ready_)
            return;
        auto adapters = lanPrivateInterfaces();
#ifdef Q_OS_WIN
        if (retiredDirectBinding_) {
            adapters.removeIf([this](const LanPrivateInterface &adapter) {
                return quint32(adapter.interfaceIndex) == retiredDirectBinding_->interfaceIndex &&
                    adapter.address.toIPv4Address() == retiredDirectBinding_->address;
            });
        }
#endif
        const auto valid = std::any_of(adapters.cbegin(), adapters.cend(), [this](const LanPrivateInterface &adapter) {
            return adapter.address.toString() == hostAddress_ &&
                lanPrivateSubnet(adapter.address, adapter.prefixLength) == hostSubnet_;
        });
        auto preferred = std::find_if(adapters.cbegin(), adapters.cend(), [this](const LanPrivateInterface &adapter) {
            return adapter.address.toString() == preferredDiscoveryAddress_;
        });
        if (preferred == adapters.cend())
            preferred = adapters.cbegin();
        if (valid && (hasActiveDevice_ || preferred == adapters.cend() || preferred->address.toString() == hostAddress_))
            return;
        closeHost();
        if (host_.state() != QProcess::NotRunning)
            return;
    }
    refreshInterfaces();
    int preferredIndex = 0;
    for (int index = 0; index < interfaces_->count(); ++index)
        if (interfaces_->itemData(index).toStringList().value(0) == preferredDiscoveryAddress_) {
            preferredIndex = index;
            break;
        }
    if (interfaces_->count() != 0)
        interfaces_->setCurrentIndex(preferredIndex);
    const auto selected = interfaces_->currentData().toStringList();
    if (selected.size() != 2) {
        status_->setText("연결된 사설 IPv4 주소가 없습니다.");
        updateControls();
        return;
    }
    startHost(selected[0], selected[1], appliedPeerSubnet_);
}

bool LanPlaybackDialog::startHost(const QString &address, const QString &subnet, const QString &peerSubnet) {
    if (shuttingDown_ || host_.state() != QProcess::NotRunning || address.isEmpty() || subnet.isEmpty())
        return false;
    const auto runtime = QApplication::applicationDirPath() + "/runtime";
#ifdef Q_OS_WIN
    const auto executable = runtime + "/tools/lumaplayer-mobile-host.exe";
#else
    const auto executable = runtime + "/tools/lumaplayer-mobile-host";
#endif
    if (!QFileInfo(executable).isFile()) {
        status_->setText("모바일 송출 도우미가 없습니다. 모바일 공유가 포함된 빌드를 사용하세요.");
        return false;
    }
    const auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataPath.isEmpty()) {
        status_->setText("이 Windows 계정의 기기 연결 저장 위치를 확인할 수 없습니다.");
        return false;
    }
    const auto name = QSysInfo::machineHostName().simplified().left(63);
    endpoint_ = address + ':' + QString::number(port);
    hostAddress_ = address;
    hostSubnet_ = subnet;
    hostPeerSubnet_ = peerSubnet;
    startupConfiguration_ = messageLine(QJsonObject{
        {"mode", "player"}, {"listen", endpoint_}, {"subnet", subnet}, {"peerSubnet", peerSubnet},
        {"name", name.isEmpty() ? QString("LumaPlayer") : name},
        {"store", QDir(dataPath).absoluteFilePath("lan-pairings.dpapi")}
    });
    protocolFailed_ = false;
    reply_.clear();
    status_->setText("휴대폰 연결을 시작하는 중…");
    host_.setProcessEnvironment(localRuntimeEnvironment(runtime));
    watchdog_.start(startupWaitMs);
    host_.start(executable, QStringList{});
    updateControls();
    return true;
}

void LanPlaybackDialog::restartDiscovery() {
    if (mediaState_ != MediaState::Idle || forgetting_)
        return;
#ifdef Q_OS_WIN
    if (direct_)
        return;
#endif
    const auto entered = peerSubnet_->text().trimmed();
    const auto peerSubnet = lanPrivatePeerSubnet(entered);
    if (!entered.isEmpty() && peerSubnet.isEmpty()) {
        status_->setText("VPN 연결 허용 범위에 사설 IPv4 네트워크 주소와 /24–/32를 입력하세요.");
        peerSubnet_->setFocus();
        return;
    }
    appliedPeerSubnet_ = peerSubnet;
    preferredDiscoveryAddress_ = interfaces_->currentData().toStringList().value(0);
    closeHost();
    startDiscovery();
}

#ifdef Q_OS_WIN
void LanPlaybackDialog::toggleDirect() {
    if (direct_) {
        stopDirect({});
        return;
    }
    if (shuttingDown_ || closingHost_ || forgetting_ || mediaState_ != MediaState::Idle ||
        !WindowsDirectConnection::available())
        return;
    closeHost();
    if (host_.state() != QProcess::NotRunning)
        return;
    direct_ = std::make_unique<WindowsDirectConnection>();
    if (!direct_->start()) {
        stopDirect("Windows에서 Wi-Fi Direct를 시작할 수 없습니다. 내부망 연결을 사용하세요.");
        return;
    }
    directDetails_->setText("공유기 없는 연결을 시작하는 중…");
    directDetails_->show();
    directStartup_.start();
    directPoll_.start();
    updateControls();
    pollDirect();
}

void LanPlaybackDialog::pollDirect() {
    if (!direct_ || shuttingDown_)
        return;
    const auto result = direct_->poll();
    if (result.phase == WindowsDirectConnection::Phase::Failed ||
        (result.phase == WindowsDirectConnection::Phase::Starting && directStartup_.hasExpired(15000)) ||
        (result.phase == WindowsDirectConnection::Phase::Started && !directBinding_ &&
         !result.binding && directStartup_.hasExpired(90000))) {
        stopDirect("Wi-Fi Direct 연결을 사용할 수 없습니다. 내부망 연결로 돌아갑니다.");
        return;
    }
    if (result.phase != WindowsDirectConnection::Phase::Started)
        return;
    const auto help = result.bluetooth ? "Android에서 ‘주변 PC 직접 연결’을 열고 이 Wi-Fi를 선택한 뒤 암호를 입력하세요."
                                       : "Android에서 ‘주변 PC 직접 연결’을 열고 Wi-Fi 이름과 암호를 입력하세요.";
    directDetails_->setText(QStringLiteral("Wi-Fi 이름  %1\n암호  %2\n%3")
                                .arg(direct_->ssid(), direct_->passphrase(), QString::fromUtf8(help)));
    if (directBinding_) {
        if (result.binding != directBinding_ || host_.state() == QProcess::NotRunning)
            stopDirect("공유기 없는 연결이 종료되었습니다. 내부망 연결로 돌아갑니다.");
        return;
    }
    if (result.binding) {
        directBinding_ = result.binding;
        const QHostAddress address(directBinding_->address);
        if (!startHost(address.toString(), lanPrivateSubnet(address, directBinding_->prefixLength)))
            stopDirect("공유기 없는 송출을 시작할 수 없습니다. 내부망 연결로 돌아갑니다.");
    } else {
        status_->setText("Wi-Fi Direct의 사설 IPv4 주소를 기다리는 중입니다.");
    }
}

void LanPlaybackDialog::stopDirect(const QString &message) {
    directPoll_.stop();
    auto publisher = std::move(direct_);
    if (directBinding_)
        retiredDirectBinding_ = directBinding_;
    directBinding_.reset();
    directDetails_->setText(message);
    directDetails_->setVisible(!message.isEmpty());
    closeHost();
    publisher.reset();
    startDiscovery();
    updateControls();
}
#endif

void LanPlaybackDialog::closeHost() {
    disarmRemoteStart();
    disarmRemoteControl();
    if (host_.state() == QProcess::NotRunning)
        return;
    closingHost_ = true;
    watchdog_.stop();
    host_.closeWriteChannel();
    if (!host_.waitForFinished(stopWaitMs)) {
        host_.kill();
        host_.waitForFinished(1000);
    }
    closingHost_ = false;
}

void LanPlaybackDialog::disarmRemoteStart() {
    remoteStart_->setChecked(false);
}

void LanPlaybackDialog::disarmRemoteControl() {
    remoteControl_->setChecked(false);
}

void LanPlaybackDialog::setMediaAvailable(bool available, bool disc) {
    if (!available)
        disarmRemoteStart();
    const bool showAudio = available && disc;
    if (audioMode_->isHidden() == showAudio)
        audioMode_->setVisible(showAudio);
    if (mediaAvailable_ == available)
        return;
    mediaAvailable_ = available;
    updateControls();
}

bool LanPlaybackDialog::canShare() const {
    return ready_ && host_.state() == QProcess::Running && !closingHost_ && hasActiveDevice_ && mediaAvailable_ &&
        !forgetting_ && mediaState_ == MediaState::Idle;
}

void LanPlaybackDialog::prepareDiscShare() {
    mediaState_ = MediaState::Preparing;
    cleanupConfirmed_ = true;
    status_->setText("PC의 Blu-ray 재생을 일시정지하는 중…");
    updateControls();
}

void LanPlaybackDialog::cancelDiscShare(const QString &message) {
    if (mediaState_ != MediaState::Preparing)
        return;
    status_->setText(message);
    finishShare(true);
}

bool LanPlaybackDialog::shareMedia(const LanPlaybackMedia &media) {
    if (!canShare() && !(ready_ && mediaState_ == MediaState::Preparing))
        return false;
    if (!media.disc && !lanLocalFileSupported(media.source)) {
        status_->setText("휴대폰에서 볼 로컬 파일을 확인할 수 없습니다.");
        return false;
    }
#ifdef Q_OS_WIN
    const auto ffmpeg = bundledFfmpeg();
#else
    const auto executable = media.disc ? QStandardPaths::findExecutable("ffmpeg") : QString{};
    const auto ffmpeg = executable.isEmpty() ? QString{} : QFileInfo(executable).canonicalFilePath();
#endif
    if (media.disc && ffmpeg.isEmpty()) {
        status_->setText("Blu-ray 모바일 공유 도구가 없습니다.");
        return false;
    }
    auto chapterOffsets = media.chaptersMs;
    std::sort(chapterOffsets.begin(), chapterOffsets.end());
    const auto lastChapter = std::unique(chapterOffsets.begin(), chapterOffsets.end());
    QJsonArray chapters;
    for (auto chapter = chapterOffsets.cbegin(); chapter != lastChapter && chapters.size() < maxChapters; ++chapter) {
        if (*chapter >= 0 && *chapter < media.durationMs)
            chapters.append(*chapter);
    }
    QJsonArray subtitles;
    if (!media.subtitles.isEmpty())
        subtitles.append(QJsonObject{{"id", "player"}, {"label", "PC 자막"}, {"language", "und"}, {"vtt", media.subtitles}});
    QJsonObject configuration{
        {"sourceKind", media.disc ? "bluray" : "file"}, {"source", media.source},
        {"playlist", media.playlist}, {"audioIndex", media.audioIndex},
        {"audioTrackId", media.audioTrackId}, {"audioMode", audioMode_->currentData().toString()},
        {"ffmpeg", ffmpeg}, {"durationMs", media.durationMs}, {"title", media.title}, {"subtitles", subtitles},
        {"videoRange", media.videoRange}
    };
    if (!chapters.isEmpty())
        configuration.insert("chaptersMs", chapters);
    if (!writeMessage(QJsonObject{{"command", "share"}, {"media", configuration}})) {
        protocolError();
        return false;
    }
    mediaState_ = MediaState::Starting;
    cleanupConfirmed_ = true;
    status_->setText("현재 미디어의 모바일 공유를 준비하는 중…");
    watchdog_.start(startupWaitMs);
    updateControls();
    return true;
}

void LanPlaybackDialog::stopSharing() {
    disarmRemoteStart();
    disarmRemoteControl();
    if (mediaState_ == MediaState::Idle || mediaState_ == MediaState::Stopping)
        return;
    mediaState_ = MediaState::Stopping;
    status_->setText("모바일 공유를 중지하는 중…");
    if (!writeMessage(QJsonObject{{"command", "stop"}}))
        host_.closeWriteChannel();
    watchdog_.start(stopWaitMs);
    updateControls();
}

bool LanPlaybackDialog::stopSharingAndWait() {
    if (mediaState_ == MediaState::Idle)
        return true;
    stopSharing();
    QElapsedTimer elapsed;
    elapsed.start();
    while (mediaState_ != MediaState::Idle && host_.state() != QProcess::NotRunning) {
        const auto remaining = stopWaitMs - elapsed.elapsed();
        if (remaining <= 0 || !host_.waitForReadyRead(static_cast<int>(remaining)))
            break;
    }
    if (mediaState_ != MediaState::Idle)
        closeHost();
    return mediaState_ == MediaState::Idle && cleanupConfirmed_;
}

bool LanPlaybackDialog::releaseMediaForRemoteControl() {
    if (mediaState_ != MediaState::Sharing ||
        !writeMessage(QJsonObject{{"command", "unshare"}}))
        return false;
    mediaState_ = MediaState::Stopping;
    watchdog_.start(stopWaitMs);
    updateControls();
    return true;
}

void LanPlaybackDialog::forgetDevices() {
    if (!ready_ || forgetting_)
        return;
    if (QMessageBox::warning(this, "기기 연결 기록 지우기",
            "기억한 기기와 현재 연결을 모두 해제합니다. 다시 연결하려면 PC에서 승인해야 합니다.",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes || !ready_)
        return;
    disarmRemoteStart();
    disarmRemoteControl();
    forgetting_ = true;
    if (mediaState_ != MediaState::Idle)
        mediaState_ = MediaState::Stopping;
    status_->setText("기기 연결 기록을 지우는 중…");
    if (!writeMessage(QJsonObject{{"command", "forget"}}))
        host_.closeWriteChannel();
    watchdog_.start(stopWaitMs);
    updateControls();
}

void LanPlaybackDialog::requestDevices() {
    if (ready_)
        writeMessage(QJsonObject{{"command", "listDevices"}});
}

void LanPlaybackDialog::updateDevices(const QJsonArray &devices) {
    const auto selected = devices_->currentItem() ? devices_->currentItem()->data(0, Qt::UserRole).toString() : QString{};
    QSignalBlocker blocked(devices_);
    devices_->clear();
    hasActiveDevice_ = false;
    bool awaitingOptions = false;
    static const QRegularExpression validId("^[A-Za-z0-9_-]{1,128}$");
    for (const auto &value : devices) {
        if (devices_->topLevelItemCount() >= 64)
            break;
        const auto device = value.toObject();
        const auto id = device.value("id").toString();
        const auto name = device.value("name").toString().simplified().left(128);
        const bool active = device.value("active").toBool();
        const bool remembered = device.value("remembered").toBool();
        const auto options = device.value("options").toObject();
        if (!validId.match(id).hasMatch() || (!active && !remembered))
            continue;
        const QHostAddress address(device.value("address").toString());
        const auto state = active ? (remembered ? "연결됨 · 기억됨" : "연결됨") : "기억됨";
        auto *row = new QTreeWidgetItem({name.isEmpty() ? QString("기기") : name,
            lanPrivateSubnet(address, 32).isEmpty() ? QString("—") : address.toString(), state});
        row->setData(0, Qt::UserRole, id);
        row->setData(0, activeDeviceRole, active);
        row->setData(0, rememberedDeviceRole, remembered);
        row->setData(0, deviceOptionsRole, options);
        devices_->addTopLevelItem(row);
        if (selected == id)
            devices_->setCurrentItem(row);
        if (active && pendingOptionsDeviceId_ == id) {
            awaitingOptions = options.value("revision").toInteger() == pendingOptionsRevision_;
            if (!awaitingOptions)
                status_->setText("선택한 기기의 재생 설정을 적용했습니다.");
        }
        hasActiveDevice_ |= active;
    }
    if (!awaitingOptions)
        pendingOptionsDeviceId_.clear();
    if (!hasActiveDevice_)
        disarmRemoteControl();
    if (!devices_->currentItem() && devices_->topLevelItemCount() > 0)
        devices_->setCurrentItem(devices_->topLevelItem(0));
    syncDeviceOptions();
    updateControls();
}

void LanPlaybackDialog::manageDevice(bool forget) {
    const auto *device = devices_->currentItem();
    if (!ready_ || forgetting_ || !device || !device->data(0, forget ? rememberedDeviceRole : activeDeviceRole).toBool())
        return;
    const auto id = device->data(0, Qt::UserRole).toString();
    if (forget && QMessageBox::question(this, "선택 기기 기억 삭제",
            "이 기기를 목록에서 지우고 연결을 끊습니다. 다음 연결은 PC의 승인이 필요합니다.",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    disarmRemoteStart();
    disarmRemoteControl();
    if (ready_ && !forgetting_ && !writeMessage(QJsonObject{
            {"command", forget ? "forgetDevice" : "disconnectDevice"}, {"id", id}}))
        status_->setText("기기 연결을 변경할 수 없습니다.");
}

void LanPlaybackDialog::syncDeviceOptions() {
    const auto *device = devices_->currentItem();
    const bool active = device && device->data(0, activeDeviceRole).toBool();
    const auto id = active ? device->data(0, Qt::UserRole).toString() : QString{};
    const auto options = active ? device->data(0, deviceOptionsRole).toJsonObject() : QJsonObject{};
    if (optionsDeviceId_ == id && optionsSnapshot_ == options)
        return;
    optionsDeviceId_ = id;
    optionsSnapshot_ = options;
    loadDeviceOptions(options);
    savedOptions_ = deviceOptionValues();
}

void LanPlaybackDialog::loadDeviceOptions(const QJsonObject &options) {
    const auto select = [](QComboBox *choice, const QVariant &value) {
        const QSignalBlocker blocked(choice);
        choice->setCurrentIndex(std::max(0, choice->findData(value)));
    };
    select(deviceProfile_, options.value("profile").toString());
    select(deviceHdr_, options.value("hdr").toString());
    select(deviceSkip_, options.value("skipSeconds").toInt());
    const auto fill = options.value("fill");
    select(deviceFill_, fill.isBool() ? QVariant(fill.toBool()) : QVariant{});
    const auto delay = options.value("subtitleDelayMs");
    const QSignalBlocker blockEnabled(deviceSubtitleDelayEnabled_), blockDelay(deviceSubtitleDelay_);
    deviceSubtitleDelayEnabled_->setChecked(delay.isDouble());
    deviceSubtitleDelay_->setValue(delay.toInt());
}

QJsonObject LanPlaybackDialog::deviceOptionValues() const {
    QJsonObject options;
    if (deviceProfile_->currentIndex() > 0)
        options.insert("profile", deviceProfile_->currentData().toString());
    if (deviceHdr_->currentIndex() > 0)
        options.insert("hdr", deviceHdr_->currentData().toString());
    if (deviceSkip_->currentIndex() > 0)
        options.insert("skipSeconds", deviceSkip_->currentData().toInt());
    if (deviceFill_->currentIndex() > 0)
        options.insert("fill", deviceFill_->currentData().toBool());
    if (deviceSubtitleDelayEnabled_->isChecked())
        options.insert("subtitleDelayMs", deviceSubtitleDelay_->value());
    return options;
}

void LanPlaybackDialog::applyDeviceOptions() {
    if (!applyOptions_->isEnabled() || optionsDeviceId_.isEmpty())
        return;
    if (!writeMessage(QJsonObject{{"command", "setDeviceOptions"}, {"id", optionsDeviceId_}, {"options", deviceOptionValues()}})) {
        status_->setText("선택한 기기의 재생 설정을 보낼 수 없습니다.");
        return;
    }
    pendingOptionsDeviceId_ = optionsDeviceId_;
    pendingOptionsRevision_ = optionsSnapshot_.value("revision").toInteger();
    updateControls();
}

bool LanPlaybackDialog::writeMessage(const QJsonObject &message) {
    if (host_.state() != QProcess::Running || closingHost_ || shuttingDown_)
        return false;
    const auto encoded = messageLine(message);
    return host_.write(encoded) == encoded.size();
}

void LanPlaybackDialog::readReply() {
    const auto incoming = host_.readAllStandardOutput();
    if (protocolFailed_)
        return;
    reply_ += incoming;
    qsizetype end;
    while ((end = reply_.indexOf('\n')) >= 0) {
        if (end > maxReplyBytes) {
            protocolError();
            return;
        }
        const auto document = QJsonDocument::fromJson(reply_.left(end));
        reply_.remove(0, end + 1);
        if (!document.isObject()) {
            protocolError();
            return;
        }
        handleReply(document.object());
    }
    if (reply_.size() > maxReplyBytes)
        protocolError();
}

void LanPlaybackDialog::handleReply(const QJsonObject &message) {
    if (closingHost_ || shuttingDown_)
        return;
    const auto event = message.value("status").toString();
    const auto command = message.value("command").toString();
    if (!ready_) {
        if (event == "ready" && message.value("version").toInt() == 1 &&
            message.value("address").toString() == endpoint_) {
            watchdog_.stop();
            ready_ = true;
            status_->setText("기기 연결을 기다리는 중입니다.");
            updateControls();
            requestDevices();
        } else if (event == "error") {
            status_->setText("휴대폰 연결을 시작할 수 없습니다. 선택한 네트워크와 포트 사용 상태를 확인하세요.");
        } else {
            protocolError();
        }
        return;
    }

    if (event == "pairRequest") {
        showPairRequest(message);
    } else if (event == "deviceConnected" && !forgetting_) {
        hasActiveDevice_ = true;
        if (mediaState_ == MediaState::Idle) {
            if (!mediaAvailable_)
                status_->setText("기기가 연결되었습니다. PC에서 미디어를 열고 공유를 시작하세요.");
            else if (remoteStart_->isChecked())
                status_->setText("기기가 연결되었습니다. Android에서 재생을 시작할 수 있습니다.");
            else
                status_->setText("기기가 연결되었습니다. 공유 시작을 누르세요.");
        }
        updateControls();
        requestDevices();
    } else if (event == "remoteStart") {
        const auto grant = message.value("id").toString();
        if (!grant.isEmpty() && (grant == remoteStartGrant_ || grant == remoteControlGrant_) && canShare())
            emit shareRequested();
    } else if (event == "streamDiagnostic") {
        const auto reason = message.value("action").toString();
        static const QHash<QString,QString> descriptions{
            {"stream-busy","이전 송출 정리 대기 시간 초과"},
            {"startup-timeout","첫 영상 데이터 대기 시간 초과"},
            {"startup-failed","FFmpeg 시작 또는 영상 스트림 초기화 실패"},
            {"stream-read-timeout","재생 중 영상 데이터 읽기 시간 초과"}
        };
        const auto found = descriptions.constFind(reason);
        if (found != descriptions.cend())
            emit streamDiagnostic(reason,"LAN 영상 요청 · "+found.value());
    } else if (event == "remoteControl") {
        if (!remoteControlGrant_.isEmpty() && message.value("id").toString() == remoteControlGrant_ && hasActiveDevice_)
            emit remoteControlRequested(message.value("action").toString());
    } else if (event == "devices" && message.value("devices").isArray()) {
        updateDevices(message.value("devices").toArray());
    } else if (event == "sharing" && (mediaState_ == MediaState::Starting || mediaState_ == MediaState::Stopping)) {
        setSharing(true);
        if (mediaState_ == MediaState::Starting) {
            watchdog_.stop();
            mediaState_ = MediaState::Sharing;
            status_->setText("기기에 공유 중입니다. 이 창을 닫아도 공유가 계속됩니다.");
        }
        updateControls();
    } else if (event == "unshared" && mediaState_ == MediaState::Stopping) {
        status_->setText("기기 연결을 유지하고 모바일 공유를 중지했습니다.");
        finishShare(true);
    } else if (event == "stopped") {
        const bool forgot = command == "forget" || forgetting_;
        forgetting_ = false;
        updateDevices({});
        dismissPairRequest();
        status_->setText(forgot ? "기기 연결 기록을 지웠습니다." : "모바일 공유를 중지했습니다.");
        finishShare(true);
        requestDevices();
    } else if (event == "error") {
        if (command == "share") {
            if (mediaState_ == MediaState::Starting) {
                status_->setText("현재 미디어를 공유할 수 없습니다.");
                finishShare(true);
            }
        } else if (command == "stop" || command == "forget") {
            protocolError(command == "forget" ? "기기 연결 기록을 지울 수 없어 휴대폰 연결을 종료합니다."
                                              : "모바일 공유를 중지할 수 없어 휴대폰 연결을 종료합니다.");
        } else if (command == "decide" && mediaState_ == MediaState::Idle) {
            status_->setText("휴대폰 연결 요청이 만료되었습니다. 앱에서 다시 연결하세요.");
        } else if (command == "discovery" && mediaState_ == MediaState::Idle) {
            status_->setText("자동 검색을 시작할 수 없습니다. 위 주소로 직접 연결하세요.");
        } else if (command == "disconnectDevice" || command == "forgetDevice") {
            status_->setText("선택한 기기의 연결을 변경할 수 없습니다.");
            requestDevices();
        } else if (command == "setDeviceOptions") {
            pendingOptionsDeviceId_.clear();
            status_->setText("선택한 기기의 재생 설정을 적용할 수 없습니다.");
            updateControls();
            requestDevices();
        }
    }
}

void LanPlaybackDialog::protocolError(const QString &message) {
    reply_.clear();
    protocolFailed_ = true;
    ready_ = false;
    updateDevices({});
    dismissPairRequest();
    status_->setText(message.isEmpty() ? "모바일 송출 도우미의 응답이 잘못되었습니다." : message);
    host_.closeWriteChannel();
    watchdog_.start(stopWaitMs);
    updateControls();
}

void LanPlaybackDialog::showPairRequest(const QJsonObject &message) {
    static const QRegularExpression validId("^[A-Za-z0-9_-]{1,128}$");
    static const QRegularExpression validCode("^[0-9]{6}$");
    const auto id = message.value("id").toString();
    const auto code = message.value("code").toString();
    if (!validId.match(id).hasMatch() || id == pairingId_)
        return;
    if (!validCode.match(code).hasMatch() || pairingDialog_ || forgetting_ || mediaState_ == MediaState::Stopping) {
        replyToPairRequest(id, "deny");
        return;
    }

    auto *parent = QApplication::activeModalWidget();
    if (!parent)
        parent = isVisible() ? this : parentWidget();
    auto *dialog = new QDialog(parent);
    pairingDialog_ = dialog;
    pairingId_ = id;
    dialog->setObjectName("lanPairRequest");
    dialog->setWindowTitle("기기 연결 승인");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setMinimumWidth(370);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(22, 20, 22, 20);
    const auto name = message.value("device").toString().simplified().left(128);
    layout->addWidget(plainLabel(name.isEmpty() ? "Android 기기" : name, dialog));
    layout->addWidget(plainLabel("연결하려는 기기에 같은 번호가 보이면 승인하세요.", dialog));
    auto *number = plainLabel(code, dialog);
    number->setObjectName("lanPairingCode");
    number->setAlignment(Qt::AlignCenter);
    auto font = number->font();
    font.setPointSize(27);
    font.setBold(true);
    number->setFont(font);
    layout->addWidget(number);

    constexpr int rememberResult = 2;
    auto *buttons = new QHBoxLayout;
    auto *deny = new QPushButton("거절", dialog);
    deny->setObjectName("lanPairDeny");
    deny->setDefault(true);
    buttons->addWidget(deny);
    buttons->addStretch();
    auto *once = new QPushButton("이번만 허용", dialog);
    once->setObjectName("lanPairAllowOnce");
    buttons->addWidget(once);
    connect(deny, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(once, &QPushButton::clicked, dialog, &QDialog::accept);
    if (message.value("remember").toBool()) {
        auto *remember = new QPushButton("이 기기 기억", dialog);
        remember->setObjectName("lanPairAllowRemember");
        buttons->addWidget(remember);
        connect(remember, &QPushButton::clicked, dialog, [dialog] { dialog->done(rememberResult); });
    }
    layout->addLayout(buttons);
    deny->setFocus();
    connect(dialog, &QDialog::finished, this, [this, id](int result) {
        if (id != pairingId_)
            return;
        pairingId_.clear();
        pairingDialog_.clear();
        replyToPairRequest(id, result == rememberResult ? "remember" : result == QDialog::Accepted ? "once" : "deny");
    });
    connect(dialog, &QObject::destroyed, this, [this, id] {
        if (id == pairingId_) {
            pairingId_.clear();
            replyToPairRequest(id, "deny");
        }
    });
    QTimer::singleShot(pairWaitMs, dialog, &QDialog::reject);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void LanPlaybackDialog::replyToPairRequest(const QString &id, const QString &decision) {
    writeMessage(QJsonObject{{"command", "decide"}, {"id", id}, {"decision", decision}});
}

void LanPlaybackDialog::dismissPairRequest() {
    pairingId_.clear();
    if (pairingDialog_)
        pairingDialog_->reject();
    pairingDialog_.clear();
}

void LanPlaybackDialog::finishShare(bool cleanupConfirmed) {
    watchdog_.stop();
    const bool wasActive = mediaState_ != MediaState::Idle;
    mediaState_ = MediaState::Idle;
    setSharing(false);
    if (wasActive) {
        cleanupConfirmed_ = cleanupConfirmed;
        if (!shuttingDown_)
            emit shareStopped(cleanupConfirmed_);
    }
    updateControls();
}

void LanPlaybackDialog::setSharing(bool sharing) {
    if (sharing_ == sharing)
        return;
    sharing_ = sharing;
    emit sharingChanged(sharing_);
}

void LanPlaybackDialog::updateControls() {
    const bool idle = mediaState_ == MediaState::Idle;
    const bool starting = host_.state() == QProcess::Starting || (host_.state() == QProcess::Running && !ready_);
    bool direct = false;
#ifdef Q_OS_WIN
    direct = bool(direct_);
    directButton_->setEnabled(!shuttingDown_ && !closingHost_ && !forgetting_ && (direct || (idle && !starting)));
    directButton_->setText(direct ? "공유기 없는 연결 종료" : "공유기 없이 휴대폰 연결");
#endif
    interfaces_->setEnabled(idle && !starting && !forgetting_ && !direct);
    retry_->setEnabled(idle && !starting && !forgetting_ && !direct);
    audioMode_->setEnabled(idle && !forgetting_);
    start_->setEnabled(canShare());
    remoteStart_->setEnabled(ready_ && mediaAvailable_ && !forgetting_ && mediaState_ == MediaState::Idle);
    remoteControl_->setEnabled(ready_ && hasActiveDevice_ && !forgetting_ && mediaState_ != MediaState::Stopping);
    stop_->setEnabled(!idle && mediaState_ != MediaState::Stopping);
    forget_->setEnabled(ready_ && !forgetting_ && mediaState_ != MediaState::Stopping);
    const auto *selected = devices_->currentItem();
    const bool canManage = ready_ && !forgetting_ && mediaState_ != MediaState::Stopping && selected;
    const bool connected = canManage && selected->data(0, activeDeviceRole).toBool();
    disconnectDevice_->setEnabled(connected);
    forgetDevice_->setEnabled(canManage && selected->data(0, rememberedDeviceRole).toBool());
    const bool canEditOptions = connected && pendingOptionsDeviceId_.isEmpty();
    deviceOptionsBox_->setEnabled(canEditOptions);
    deviceSubtitleDelay_->setEnabled(deviceSubtitleDelayEnabled_->isChecked());
    const auto options = deviceOptionValues();
    resetOptions_->setEnabled(!options.isEmpty());
    applyOptions_->setEnabled(options != savedOptions_);
    peerSubnet_->setEnabled(interfaces_->isEnabled());
    auto addressText = ready_ ? "직접 연결 주소  " + endpoint_ : QString{};
    if (ready_ && !hostPeerSubnet_.isEmpty())
        addressText += "  ·  VPN 허용 " + hostPeerSubnet_;
    address_->setText(addressText);
    address_->setVisible(ready_);
}

}
