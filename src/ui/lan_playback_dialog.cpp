#include "lan_playback_dialog.h"

#include "player_controls.h"
#include "subtitles/runtime_environment.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTreeWidget>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#include <string>
#include <windows.h>
#endif

namespace luma {
namespace {
constexpr int port = 48762;
constexpr int startupWaitMs = 10000;
constexpr int stopWaitMs = 4000;
constexpr int pairWaitMs = 90000;
constexpr qsizetype maxReplyBytes = 64 * 1024;
constexpr qsizetype maxCaptionBytes = 1024 * 1024;
constexpr int activeDeviceRole = Qt::UserRole + 1;
constexpr int rememberedDeviceRole = Qt::UserRole + 2;

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
QString bundledDiscTool() {
    const auto path = QDir::toNativeSeparators(QApplication::applicationDirPath() + "/runtime/tools/ffmpeg.exe").toStdWString();
    const auto file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};
    const auto close = qScopeGuard([file] { CloseHandle(file); });
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
            const LanPrivateInterface adapter{network.humanReadableName(), address, entry.prefixLength()};
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
    layout->addLayout(shareButtons);

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
    host_.setStandardErrorFile(QProcess::nullDevice());
    connect(start_, &QPushButton::clicked, this, [this] {
        autoShare_ = true;
        emit shareRequested();
    });
    connect(stop_, &QPushButton::clicked, this, [this] {
        autoShare_ = false;
        stopSharing();
    });
    connect(receive, &QPushButton::clicked, this, &LanPlaybackDialog::receiveRequested);
    connect(disconnectDevice_, &QPushButton::clicked, this, [this] { manageDevice(false); });
    connect(forgetDevice_, &QPushButton::clicked, this, [this] { manageDevice(true); });
    connect(devices_, &QTreeWidget::currentItemChanged, this, &LanPlaybackDialog::updateControls);
    connect(retry_, &QPushButton::clicked, this, &LanPlaybackDialog::restartDiscovery);
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
    refreshInterfaces();
    updateControls();
}

LanPlaybackDialog::~LanPlaybackDialog() {
    shuttingDown_ = true;
    dismissPairRequest();
    closeHost();
}

void LanPlaybackDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    requestDevices();
}

void LanPlaybackDialog::refreshInterfaces() {
    const auto preferred = interfaces_->currentData().toStringList().value(0);
    QSignalBlocker blocked(interfaces_);
    interfaces_->clear();
    for (const auto &adapter : lanPrivateInterfaces()) {
        const auto address = adapter.address.toString();
        const auto subnet = lanPrivateSubnet(adapter.address, adapter.prefixLength);
        interfaces_->addItem(adapter.name + " · " + address, QStringList{address, subnet});
    }
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
    if (shuttingDown_ || host_.state() != QProcess::NotRunning)
        return;
    refreshInterfaces();
    const auto selected = interfaces_->currentData().toStringList();
    if (selected.size() != 2) {
        status_->setText("연결된 사설 IPv4 주소가 없습니다.");
        updateControls();
        return;
    }
    const auto runtime = QApplication::applicationDirPath() + "/runtime";
#ifdef Q_OS_WIN
    const auto executable = runtime + "/tools/lumaplayer-mobile-host.exe";
#else
    const auto executable = runtime + "/tools/lumaplayer-mobile-host";
#endif
    if (!QFileInfo(executable).isFile()) {
        status_->setText("모바일 송출 도우미가 없습니다. 모바일 공유가 포함된 빌드를 사용하세요.");
        return;
    }
    const auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataPath.isEmpty()) {
        status_->setText("이 Windows 계정의 기기 연결 저장 위치를 확인할 수 없습니다.");
        return;
    }
    const auto name = QSysInfo::machineHostName().simplified().left(63);
    endpoint_ = selected[0] + ':' + QString::number(port);
    startupConfiguration_ = messageLine(QJsonObject{
        {"mode", "player"}, {"listen", endpoint_}, {"subnet", selected[1]},
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
}

void LanPlaybackDialog::restartDiscovery() {
    if (mediaState_ != MediaState::Idle || forgetting_)
        return;
    closeHost();
    startDiscovery();
}

void LanPlaybackDialog::closeHost() {
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

void LanPlaybackDialog::setMediaAvailable(bool available, bool disc) {
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
    QString ffmpeg;
    if (media.disc) {
#ifdef Q_OS_WIN
        ffmpeg = bundledDiscTool();
#else
        ffmpeg = QFileInfo(QStandardPaths::findExecutable("ffmpeg")).canonicalFilePath();
#endif
        if (ffmpeg.isEmpty()) {
            status_->setText("Blu-ray 모바일 공유 도구가 없습니다.");
            return false;
        }
    } else if (!lanLocalFileSupported(media.source)) {
        status_->setText("휴대폰에서 볼 로컬 파일을 확인할 수 없습니다.");
        return false;
    }
    QJsonArray subtitles;
    if (!media.subtitles.isEmpty())
        subtitles.append(QJsonObject{{"id", "player"}, {"label", "PC 자막"}, {"language", "und"}, {"vtt", media.subtitles}});
    const QJsonObject configuration{
        {"sourceKind", media.disc ? "bluray" : "file"}, {"source", media.source},
        {"playlist", media.playlist}, {"audioIndex", media.audioIndex},
        {"audioTrackId", media.audioTrackId}, {"audioMode", audioMode_->currentData().toString()},
        {"ffmpeg", ffmpeg}, {"durationMs", media.durationMs}, {"title", media.title}, {"subtitles", subtitles}
    };
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

void LanPlaybackDialog::forgetDevices() {
    if (!ready_ || forgetting_)
        return;
    if (QMessageBox::warning(this, "기기 연결 기록 지우기",
            "기억한 기기와 현재 연결을 모두 해제합니다. 다시 연결하려면 PC에서 승인해야 합니다.",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes || !ready_)
        return;
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
    static const QRegularExpression validId("^[A-Za-z0-9_-]{1,128}$");
    for (const auto &value : devices) {
        if (devices_->topLevelItemCount() >= 64)
            break;
        const auto device = value.toObject();
        const auto id = device.value("id").toString();
        const auto name = device.value("name").toString().simplified().left(128);
        const bool active = device.value("active").toBool();
        const bool remembered = device.value("remembered").toBool();
        if (!validId.match(id).hasMatch() || (!active && !remembered))
            continue;
        const QHostAddress address(device.value("address").toString());
        const auto state = active ? (remembered ? "연결됨 · 기억됨" : "연결됨") : "기억됨";
        auto *row = new QTreeWidgetItem({name.isEmpty() ? QString("기기") : name,
            lanPrivateSubnet(address, 32).isEmpty() ? QString("—") : address.toString(), state});
        row->setData(0, Qt::UserRole, id);
        row->setData(0, activeDeviceRole, active);
        row->setData(0, rememberedDeviceRole, remembered);
        devices_->addTopLevelItem(row);
        if (selected == id)
            devices_->setCurrentItem(row);
        hasActiveDevice_ |= active;
    }
    if (!devices_->currentItem() && devices_->topLevelItemCount() > 0)
        devices_->setCurrentItem(devices_->topLevelItem(0));
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
    if (ready_ && !forgetting_ && !writeMessage(QJsonObject{
            {"command", forget ? "forgetDevice" : "disconnectDevice"}, {"id", id}}))
        status_->setText("기기 연결을 변경할 수 없습니다.");
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
            else
                status_->setText(autoShare_ ? "기기 연결이 승인되었습니다." : "기기가 연결되었습니다. 공유 시작을 누르세요.");
        }
        updateControls();
        if (autoShare_)
            emit deviceAuthorized();
        requestDevices();
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
    interfaces_->setEnabled(idle && !starting && !forgetting_);
    retry_->setEnabled(idle && !starting && !forgetting_);
    audioMode_->setEnabled(idle && !forgetting_);
    start_->setEnabled(canShare());
    stop_->setEnabled(!idle && mediaState_ != MediaState::Stopping);
    forget_->setEnabled(ready_ && !forgetting_ && mediaState_ != MediaState::Stopping);
    const auto *selected = devices_->currentItem();
    const bool canManage = ready_ && !forgetting_ && mediaState_ != MediaState::Stopping && selected;
    disconnectDevice_->setEnabled(canManage && selected->data(0, activeDeviceRole).toBool());
    forgetDevice_->setEnabled(canManage && selected->data(0, rememberedDeviceRole).toBool());
    address_->setText(ready_ ? "직접 연결 주소  " + endpoint_ : QString{});
    address_->setVisible(ready_);
}

}
