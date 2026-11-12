#include "windows_direct.h"

#include <algorithm>

#if defined(Q_OS_WIN) && defined(_MSC_VER) && __has_include(<winrt/Windows.Devices.WiFiDirect.h>) && \
    __has_include(<winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>)
#define LUMA_HAS_DIRECT_WINRT 1
#include <QElapsedTimer>
#include <QHash>
#include <QtEndian>
#include <QSet>
#include <array>
#include <chrono>
#include <mutex>
#include <utility>
#include <vector>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <iphlpapi.h>
#include <roapi.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.WiFiDirect.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.h>
#include <winrt/Windows.Security.Cryptography.h>
#endif

namespace luma {
namespace {

bool privateHostAddress(quint32 ip, int prefix) {
    if (prefix < 1 || prefix > 30)
        return false;
    const bool privateIp = (ip >> 24) == 0x0a || (ip >> 20) == 0xac1 || (ip >> 16) == 0xc0a8;
    const quint32 hostMask = (quint32(1) << (32 - prefix)) - 1;
    return privateIp && (ip & hostMask) != 0 && (ip & hostMask) != hostMask;
}

}

std::optional<WindowsDirectBinding> newWindowsDirectBinding(const QList<WindowsDirectBinding> &before,
                                                             const QList<WindowsDirectBinding> &current) {
    std::optional<WindowsDirectBinding> result;
    for (const auto &binding : current) {
        if (!binding.adapter || !privateHostAddress(binding.address, binding.prefixLength))
            continue;
        if (std::any_of(before.cbegin(), before.cend(), [&](const auto &old) {
                return old.adapter == binding.adapter || old.address == binding.address;
            }))
            continue;
        if (result || std::count_if(current.cbegin(), current.cend(), [&](const auto &other) {
                return other.address == binding.address;
            }) != 1)
            return {};
        result = binding;
    }
    return result;
}

#ifdef LUMA_HAS_DIRECT_WINRT
namespace {

namespace bluetooth = winrt::Windows::Devices::Bluetooth;
namespace gatt = bluetooth::GenericAttributeProfile;
namespace wifi = winrt::Windows::Devices::WiFiDirect;
using winrt::Windows::Foundation::AsyncStatus;
using winrt::Windows::Foundation::IAsyncOperation;

constexpr winrt::guid serviceId{0x32f4e091, 0x3797, 0x4be3, {0xae, 0xd3, 0xd8, 0x3d, 0x58, 0x42, 0xa4, 0x01}};
constexpr winrt::guid descriptorId{0x32f4e092, 0x3797, 0x4be3, {0xae, 0xd3, 0xd8, 0x3d, 0x58, 0x42, 0xa4, 0x01}};
constexpr size_t maxPeers = 8;

template<typename Action>
void ignoreWinrtError(Action action) {
    try {
        action();
    } catch (const winrt::hresult_error &) {
    }
}

struct Apartment {
    const HRESULT status = RoInitialize(RO_INIT_SINGLETHREADED);
    ~Apartment() { if (SUCCEEDED(status)) RoUninitialize(); }
    bool ready() const { return SUCCEEDED(status) || status == RPC_E_CHANGED_MODE; }
};

QString randomAlphanumeric(int length) {
    static constexpr char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    QString result;
    result.reserve(length);
    while (result.size() < length) {
        std::array<uchar, 32> bytes{};
        if (BCryptGenRandom(nullptr, bytes.data(), ULONG(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            return {};
        for (const auto byte : bytes) {
            if (byte < 248)
                result.append(QLatin1Char(alphabet[byte % 62]));
            if (result.size() == length)
                break;
        }
    }
    return result;
}

std::optional<QList<WindowsDirectBinding>> directBindings() {
    MIB_IF_TABLE2 *rawInterfaces = nullptr;
    if (GetIfTable2(&rawInterfaces) != NO_ERROR)
        return {};
    const std::unique_ptr<MIB_IF_TABLE2, decltype(&FreeMibTable)> interfaces(rawInterfaces, FreeMibTable);
    QSet<quint64> active;
    for (ULONG index = 0; index < interfaces->NumEntries; ++index) {
        const auto &row = interfaces->Table[index];
        if (row.OperStatus == IfOperStatusUp && row.Type == IF_TYPE_IEEE80211 &&
            !row.InterfaceAndOperStatusFlags.HardwareInterface &&
            QString::fromWCharArray(row.Description).startsWith(QStringLiteral("Microsoft Wi-Fi Direct Virtual Adapter"),
                                                               Qt::CaseInsensitive))
            active.insert(row.InterfaceLuid.Value);
    }
    QList<WindowsDirectBinding> bindings;
    if (active.isEmpty())
        return bindings;

    MIB_UNICASTIPADDRESS_TABLE *rawAddresses = nullptr;
    if (GetUnicastIpAddressTable(AF_INET, &rawAddresses) != NO_ERROR)
        return {};
    const std::unique_ptr<MIB_UNICASTIPADDRESS_TABLE, decltype(&FreeMibTable)> addresses(rawAddresses, FreeMibTable);
    QHash<quint32, int> counts;
    for (ULONG index = 0; index < addresses->NumEntries; ++index) {
        const auto &row = addresses->Table[index];
        if (row.DadState != IpDadStatePreferred)
            continue;
        const auto adapter = row.InterfaceLuid.Value;
        const int prefix = row.OnLinkPrefixLength;
        const auto ip = qFromBigEndian<quint32>(&row.Address.Ipv4.sin_addr);
        ++counts[ip];
        if (active.contains(adapter) && privateHostAddress(ip, prefix))
            bindings.append({adapter, ip, prefix, row.InterfaceIndex});
    }
    bindings.removeIf([&](const auto &binding) { return counts.value(binding.address) != 1; });
    // Retain an already-active adapter even while it is obtaining an address.
    for (const auto adapter : active) {
        if (std::none_of(bindings.cbegin(), bindings.cend(), [adapter](const auto &binding) {
                return binding.adapter == adapter;
            }))
            bindings.append({adapter, 0, 0});
    }
    return bindings;
}

struct PeerRequests {
    using Clock = std::chrono::steady_clock;
    struct Pending {
        wifi::WiFiDirectConnectionRequest request;
        IAsyncOperation<wifi::WiFiDirectDevice> operation;
        Clock::time_point deadline;
    };
    std::mutex mutex;
    std::vector<Pending> pending;
    std::vector<wifi::WiFiDirectDevice> devices;
    bool closed = false;

    void accept(const wifi::WiFiDirectConnectionRequestedEventArgs &args) {
        std::lock_guard guard(mutex);
        if (closed)
            return;
        ignoreWinrtError([&] {
            const auto request = args.GetConnectionRequest();
            if (pending.size() + devices.size() >= maxPeers) {
                request.Close();
                return;
            }
            pending.push_back({request, nullptr, Clock::now() + std::chrono::seconds(12)});
        });
    }

    void poll() {
        std::lock_guard guard(mutex);
        std::erase_if(devices, [](const auto &device) {
            bool connected = false;
            ignoreWinrtError([&] { connected = device.ConnectionStatus() == wifi::WiFiDirectConnectionStatus::Connected; });
            if (!connected)
                ignoreWinrtError([&] { device.Close(); });
            return !connected;
        });
        const auto now = Clock::now();
        for (auto entry = pending.begin(); entry != pending.end();) {
            auto &peer = *entry;
            bool waiting = false;
            bool accepted = false;
            ignoreWinrtError([&] {
                if (!peer.operation) {
                    if (now < peer.deadline) {
                        peer.operation = wifi::WiFiDirectDevice::FromIdAsync(peer.request.DeviceInformation().Id());
                        waiting = true;
                    }
                    return;
                }
                const auto status = peer.operation.Status();
                if (status == AsyncStatus::Completed) {
                    if (auto device = peer.operation.GetResults()) {
                        devices.push_back(std::move(device));
                        accepted = true;
                    }
                } else if (status == AsyncStatus::Started) {
                    waiting = now < peer.deadline;
                    if (!waiting)
                        peer.operation.Cancel();
                }
            });
            if (waiting) {
                ++entry;
            } else {
                if (!accepted)
                    ignoreWinrtError([&] { peer.request.Close(); });
                entry = pending.erase(entry);
            }
        }
    }

    void close() {
        std::lock_guard guard(mutex);
        closed = true;
        for (const auto &peer : pending) {
            if (peer.operation)
                ignoreWinrtError([&] { peer.operation.Cancel(); });
            ignoreWinrtError([&] { peer.request.Close(); });
        }
        for (const auto &device : devices)
            ignoreWinrtError([&] { device.Close(); });
        pending.clear();
        devices.clear();
    }
};

}
#endif

struct WindowsDirectConnection::State {
    QString ssid, passphrase;
#ifdef LUMA_HAS_DIRECT_WINRT
    Apartment apartment;
    wifi::WiFiDirectAdvertisementPublisher publisher{nullptr};
    wifi::WiFiDirectConnectionListener listener{nullptr};
    winrt::event_token connectionRequested{};
    std::shared_ptr<PeerRequests> peers = std::make_shared<PeerRequests>();
    QList<WindowsDirectBinding> before;
    QElapsedTimer bluetoothWait;
    bool publisherRequested = false;
    IAsyncOperation<gatt::GattServiceProviderResult> serviceRequest{nullptr};
    IAsyncOperation<gatt::GattLocalCharacteristicResult> characteristicRequest{nullptr};
    gatt::GattServiceProvider service{nullptr};

    ~State() {
        if (serviceRequest)
            ignoreWinrtError([&] { serviceRequest.Cancel(); });
        if (characteristicRequest)
            ignoreWinrtError([&] { characteristicRequest.Cancel(); });
        if (service)
            ignoreWinrtError([&] { service.StopAdvertising(); });
        if (listener && connectionRequested.value)
            ignoreWinrtError([&] { listener.ConnectionRequested(connectionRequested); });
        peers->close();
        if (publisherRequested)
            ignoreWinrtError([&] { publisher.Stop(); });
    }

    void startListener() {
        listener = wifi::WiFiDirectConnectionListener();
        connectionRequested = listener.ConnectionRequested(
            [pendingPeers = std::weak_ptr<PeerRequests>(peers)](const auto &, const auto &args) {
                if (const auto owner = pendingPeers.lock())
                    owner->accept(args);
            });
    }

    bool pollBluetooth() {
        if (!bluetoothWait.isValid()) {
            bluetoothWait.start();
            serviceRequest = gatt::GattServiceProvider::CreateAsync(serviceId);
        }
        if (serviceRequest && serviceRequest.Status() != AsyncStatus::Started) {
            auto request = std::exchange(serviceRequest, nullptr);
            if (request.Status() == AsyncStatus::Completed) {
                const auto created = request.GetResults();
                if (created.Error() == bluetooth::BluetoothError::Success) {
                    service = created.ServiceProvider();
                    const auto descriptor = QByteArray("LUMA-DIRECT-1\n") + ssid.toLatin1();
                    const auto *bytes = reinterpret_cast<const uint8_t *>(descriptor.constData());
                    gatt::GattLocalCharacteristicParameters parameters;
                    parameters.CharacteristicProperties(gatt::GattCharacteristicProperties::Read);
                    parameters.ReadProtectionLevel(gatt::GattProtectionLevel::Plain);
                    parameters.StaticValue(winrt::Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(
                        {bytes, bytes + descriptor.size()}));
                    characteristicRequest = service.Service().CreateCharacteristicAsync(descriptorId, parameters);
                }
            }
        }
        if (characteristicRequest && characteristicRequest.Status() != AsyncStatus::Started) {
            auto request = std::exchange(characteristicRequest, nullptr);
            if (request.Status() == AsyncStatus::Completed && request.GetResults().Error() == bluetooth::BluetoothError::Success) {
                gatt::GattServiceProviderAdvertisingParameters parameters;
                parameters.IsConnectable(true);
                parameters.IsDiscoverable(true);
                service.StartAdvertising(parameters);
            }
        }
        if (bluetoothWait.hasExpired(5000)) {
            if (serviceRequest)
                ignoreWinrtError([&] { std::exchange(serviceRequest, nullptr).Cancel(); });
            if (characteristicRequest)
                ignoreWinrtError([&] { std::exchange(characteristicRequest, nullptr).Cancel(); });
        }
        return service && service.AdvertisementStatus() == gatt::GattServiceProviderAdvertisementStatus::Started;
    }
#endif
};

WindowsDirectConnection::WindowsDirectConnection() : state_(std::make_unique<State>()) {}
WindowsDirectConnection::~WindowsDirectConnection() = default;

bool WindowsDirectConnection::available() {
#ifdef LUMA_HAS_DIRECT_WINRT
    return true;
#else
    return false;
#endif
}

bool WindowsDirectConnection::start() {
#ifdef LUMA_HAS_DIRECT_WINRT
    if (!state_->apartment.ready() || state_->publisherRequested)
        return false;
    const auto before = directBindings();
    const auto suffix = randomAlphanumeric(10);
    state_->passphrase = randomAlphanumeric(20);
    if (!before || suffix.isEmpty() || state_->passphrase.isEmpty())
        return false;
    state_->before = *before;
    state_->ssid = QStringLiteral("DIRECT-") + suffix;
    try {
        state_->publisher = wifi::WiFiDirectAdvertisementPublisher();
        const auto advertisement = state_->publisher.Advertisement();
        advertisement.IsAutonomousGroupOwnerEnabled(true);
        advertisement.ListenStateDiscoverability(wifi::WiFiDirectAdvertisementListenStateDiscoverability::None);
        const auto legacy = advertisement.LegacySettings();
        legacy.IsEnabled(true);
        legacy.Ssid(winrt::hstring(state_->ssid.toStdWString()));
        const auto credential = legacy.Passphrase();
        credential.Password(winrt::hstring(state_->passphrase.toStdWString()));
        state_->publisherRequested = true;
        state_->publisher.Start();
        return true;
    } catch (const winrt::hresult_error &) {
    }
#endif
    return false;
}

WindowsDirectConnection::Status WindowsDirectConnection::poll() {
#ifdef LUMA_HAS_DIRECT_WINRT
    if (state_->publisherRequested) {
        try {
            switch (state_->publisher.Status()) {
            case wifi::WiFiDirectAdvertisementPublisherStatus::Created:
                return {};
            case wifi::WiFiDirectAdvertisementPublisherStatus::Started: {
                if (!state_->listener)
                    state_->startListener();
                state_->peers->poll();
                bool bluetooth = false;
                ignoreWinrtError([&] { bluetooth = state_->pollBluetooth(); });
                const auto bindings = directBindings();
                return {Phase::Started, bindings ? newWindowsDirectBinding(state_->before, *bindings) : std::nullopt, bluetooth};
            }
            default:
                break;
            }
        } catch (const winrt::hresult_error &) {
        }
    }
#endif
    return {Phase::Failed, {}, false};
}

const QString &WindowsDirectConnection::ssid() const { return state_->ssid; }
const QString &WindowsDirectConnection::passphrase() const { return state_->passphrase; }

}
