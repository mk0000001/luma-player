#include "system_metrics.h"
#include "system_metrics_p.h"

#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <winternl.h>
#include <cfgmgr32.h>
#include <d3dkmthk.h>
#include <dxgi.h>
#include <iphlpapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <initguid.h>
#include <winioctl.h>
#if __has_include(<dxcore.h>)
#include <dxcore.h>
#define LUMA_HAS_DXCORE 1
#endif
#endif
#ifdef Q_OS_LINUX
#include <ctime>
#include <unistd.h>
#endif

namespace luma {

NetworkRates NetworkRateSampler::sample(QHash<quint64, NetworkCounters> counters, qint64 timestampNs) {
    NetworkRates rates;
    const double seconds = (timestampNs - timestampNs_) / 1e9;
    // A long interval can span a hidden overlay or system sleep.
    if (seconds > 0 && seconds <= 5 && !counters.isEmpty() && !previous_.isEmpty()) {
        rates = {0.0, 0.0};
        for (auto it = counters.cbegin(); it != counters.cend(); ++it) {
            const auto previous = previous_.constFind(it.key());
            if (previous == previous_.cend()) {
                rates = {};
                break;
            }
            if (it->received < previous->received)
                rates.receivedBytesPerSecond.reset();
            else if (rates.receivedBytesPerSecond)
                *rates.receivedBytesPerSecond += double(it->received - previous->received) / seconds;
            if (it->sent < previous->sent)
                rates.sentBytesPerSecond.reset();
            else if (rates.sentBytesPerSecond)
                *rates.sentBytesPerSecond += double(it->sent - previous->sent) / seconds;
        }
    }
    previous_ = std::move(counters);
    timestampNs_ = timestampNs;
    return rates;
}

namespace {

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;

quint64 luidKey(LUID luid) {
    return (quint64(quint32(luid.HighPart)) << 32) | luid.LowPart;
}

template <typename T>
std::optional<T> adapterInformation(LUID luid, KMTQUERYADAPTERINFOTYPE type) {
    D3DKMT_OPENADAPTERFROMLUID open{};
    open.AdapterLuid = luid;
    if (D3DKMTOpenAdapterFromLuid(&open) < 0)
        return {};
    T data{};
    D3DKMT_QUERYADAPTERINFO query{open.hAdapter, type, &data, sizeof(data)};
    const auto status = D3DKMTQueryAdapterInfo(&query);
    const D3DKMT_CLOSEADAPTER close{open.hAdapter};
    D3DKMTCloseAdapter(&close);
    return status >= 0 ? std::optional<T>(data) : std::nullopt;
}

std::optional<double> adapterTemperature(LUID luid) {
    const auto performance = adapterInformation<D3DKMT_ADAPTER_PERFDATA>(luid, KMTQAITYPE_ADAPTERPERFDATA);
    if (performance && performance->Temperature > 0 && performance->Temperature <= 2000)
        return performance->Temperature / 10.0;
    return {};
}

struct GraphicsAdapter {
    LUID luid;
    GraphicsMetrics metrics;
};

QList<GraphicsAdapter> graphicsAdapters(QSet<quint64> &ids) {
    QList<GraphicsAdapter> result;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return result;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(i, &adapter)))
            break;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            continue;
        ids.insert(luidKey(description.AdapterLuid));
        const auto type = adapterInformation<D3DKMT_ADAPTERTYPE>(description.AdapterLuid, KMTQAITYPE_ADAPTERTYPE);
        // Indirect displays can enumerate additional DXGI adapters for the same physical GPU.
        if (type && (type->IndirectDisplayDevice || type->SoftwareDevice))
            continue;
        GraphicsMetrics metrics;
        metrics.adapterLuid = luidKey(description.AdapterLuid);
        metrics.name = QString::fromWCharArray(description.Description);
        if (description.DedicatedVideoMemory)
            metrics.dedicatedMemoryTotal = description.DedicatedVideoMemory;
        if (type && (type->HybridIntegrated || type->HybridDiscrete))
            metrics.integrated = type->HybridIntegrated != 0;
        result.append({description.AdapterLuid, std::move(metrics)});
    }
    return result;
}

QList<LUID> neuralAdapters(QList<GraphicsAdapter> &graphics, QSet<quint64> &ids) {
    QList<LUID> result;
#ifdef LUMA_HAS_DXCORE
    const auto library = LoadLibraryExW(L"dxcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library)
        return result;
    {
        using CreateFactory = HRESULT (WINAPI *)(REFIID, void **);
        const auto create = reinterpret_cast<CreateFactory>(GetProcAddress(library, "DXCoreCreateAdapterFactory"));
        ComPtr<IDXCoreAdapterFactory> factory;
        ComPtr<IDXCoreAdapterList> adapters;
        if (create && SUCCEEDED(create(IID_PPV_ARGS(&factory)))) {
            for (auto &graphicsAdapter : graphics) {
                ComPtr<IDXCoreAdapter> adapter;
                bool integrated = false;
                if (SUCCEEDED(factory->GetAdapterByLuid(graphicsAdapter.luid, IID_PPV_ARGS(&adapter))) &&
                    SUCCEEDED(adapter->GetProperty(DXCoreAdapterProperty::IsIntegrated, &integrated)))
                    graphicsAdapter.metrics.integrated = integrated;
            }
            if (SUCCEEDED(factory->CreateAdapterList(1, &DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU, IID_PPV_ARGS(&adapters)))) {
                for (uint32_t i = 0; i < adapters->GetAdapterCount(); ++i) {
                    ComPtr<IDXCoreAdapter> adapter;
                    LUID luid{};
                    if (SUCCEEDED(adapters->GetAdapter(i, IID_PPV_ARGS(&adapter))) &&
                        SUCCEEDED(adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, &luid))) {
                        ids.insert(luidKey(luid));
                        result.append(luid);
                    }
                }
            }
        }
    }
    FreeLibrary(library);
#else
    Q_UNUSED(graphics);
    Q_UNUSED(ids);
#endif
    return result;
}

void updatePeak(std::optional<double> &peak, double value) {
    peak = std::max(peak.value_or(0.0), std::clamp(value, 0.0, 100.0));
}

QSet<DWORD> playerProcesses(bool &includesChildren) {
    QSet<DWORD> result{GetCurrentProcessId()};
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;
    QMultiHash<DWORD, DWORD> children;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        includesChildren = true;
        do {
            children.insert(entry.th32ParentProcessID, entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    QList<DWORD> pending{GetCurrentProcessId()};
    while (!pending.isEmpty()) {
        const auto parent = pending.takeLast();
        for (const auto child : children.values(parent)) {
            if (result.contains(child))
                continue;
            result.insert(child);
            pending.append(child);
        }
    }
    return result;
}

template <typename Read>
void forEachPdhValue(PDH_HCOUNTER counter, DWORD format, Read read) {
    DWORD bytes = 0, count = 0;
    if (!counter || PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, nullptr) != PDH_MORE_DATA ||
        bytes > 16 * 1024 * 1024)
        return;
    using Item = PDH_FMT_COUNTERVALUE_ITEM_W;
    std::vector<Item> buffer((bytes + sizeof(Item) - 1) / sizeof(Item));
    if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, buffer.data()) != ERROR_SUCCESS ||
        count > buffer.size())
        return;

    for (DWORD i = 0; i < count; ++i) {
        const auto &item = buffer[i];
        const auto status = item.FmtValue.CStatus;
        if (item.szName && (status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA))
            read(QString::fromWCharArray(item.szName), item.FmtValue);
    }
}

void sampleAccelerators(PDH_HCOUNTER counter, const QSet<quint64> &graphics,
                        const QSet<quint64> &neural, const QSet<DWORD> &player, SystemMetrics &result) {
    static const QRegularExpression engineName(
        R"(^pid_(\d+)_(luid_0x([0-9a-f]+)_0x([0-9a-f]+)_phys_\d+_eng_\d+)_engtype_([^#]+))",
        QRegularExpression::CaseInsensitiveOption);
    struct Engine { quint64 adapter; bool decode; double usage, playerUsage; };
    QHash<QString, Engine> engines;
    forEachPdhValue(counter, PDH_FMT_DOUBLE, [&](const QString &name, const PDH_FMT_COUNTERVALUE &formatted) {
        const double value = formatted.doubleValue;
        if (!std::isfinite(value) || value < 0)
            return;
        const auto match = engineName.match(name);
        if (!match.hasMatch())
            return;
        const auto adapter = (quint64(match.captured(3).toUInt(nullptr, 16)) << 32) |
                             match.captured(4).toUInt(nullptr, 16);
        if (!graphics.contains(adapter) && !neural.contains(adapter))
            return;
        const auto id = match.captured(2);
        auto it = engines.find(id);
        if (it == engines.end())
            it = engines.insert(id, {adapter, match.captured(5).contains("VideoDecode", Qt::CaseInsensitive), 0, 0});
        it->usage += value;
        if (player.contains(match.captured(1).toULong()))
            it->playerUsage += value;
    });
    // Sum processes on each physical engine, then take the busiest engine.
    for (const auto &engine : engines) {
        if (neural.contains(engine.adapter)) {
            updatePeak(result.npuPercent, engine.usage);
            updatePeak(result.playerNpuPercent, engine.playerUsage);
        } else {
            updatePeak(result.gpuPercent, engine.usage);
            updatePeak(result.playerGpuPercent, engine.playerUsage);
            if (engine.decode) {
                updatePeak(result.gpuDecodePercent, engine.usage);
                updatePeak(result.playerGpuDecodePercent, engine.playerUsage);
            }
        }
    }
}

QHash<quint64, quint64> dedicatedMemoryUsage(PDH_HCOUNTER counter) {
    QHash<quint64, quint64> result;
    static const QRegularExpression adapterName(R"(^luid_0x([0-9a-f]+)_0x([0-9a-f]+)_phys_\d+$)",
                                                QRegularExpression::CaseInsensitiveOption);
    forEachPdhValue(counter, PDH_FMT_LARGE | PDH_FMT_NOCAP100, [&](const QString &name, const PDH_FMT_COUNTERVALUE &value) {
        const auto match = adapterName.match(name);
        if (match.hasMatch() && value.largeValue >= 0) {
            const auto luid = (quint64(match.captured(1).toUInt(nullptr, 16)) << 32) | match.captured(2).toUInt(nullptr, 16);
            result[luid] += quint64(value.largeValue);
        }
    });
    return result;
}

QHash<quint64, NetworkCounters> networkCounters() {
    QHash<quint64, NetworkCounters> result;
    MIB_IF_TABLE2 *interfaces = nullptr;
    if (GetIfTable2(&interfaces) != NO_ERROR)
        return result;
    const std::unique_ptr<MIB_IF_TABLE2, decltype(&FreeMibTable)> table(interfaces, FreeMibTable);
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto &row = table->Table[i];
        const auto flags = row.InterfaceAndOperStatusFlags;
        if (row.OperStatus == IfOperStatusUp && flags.HardwareInterface && !flags.FilterInterface &&
            row.Type != IF_TYPE_SOFTWARE_LOOPBACK && row.Type != IF_TYPE_TUNNEL)
            result.insert(row.InterfaceLuid.Value, {row.InOctets, row.OutOctets});
    }
    return result;
}

std::optional<DiskTemperature> storageTemperature(HANDLE device) {
    DWORD returned = 0;
    STORAGE_DEVICE_NUMBER disk{};
    if (!DeviceIoControl(device, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &disk, sizeof(disk), &returned, nullptr) ||
        returned < sizeof(disk) || disk.DeviceType != FILE_DEVICE_DISK || disk.DeviceNumber == DWORD(-1))
        return {};

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceTemperatureProperty;
    query.QueryType = PropertyStandardQuery;
    std::array<char, 1024> data{};
    constexpr auto headerSize = offsetof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR, TemperatureInfo);
    if (!DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), data.data(), DWORD(data.size()),
                         &returned, nullptr) || returned < headerSize || returned > data.size())
        return {};
    STORAGE_TEMPERATURE_DATA_DESCRIPTOR descriptor{};
    std::memcpy(&descriptor, data.data(), headerSize);
    if (descriptor.Size < headerSize || descriptor.Size > returned ||
        descriptor.InfoCount > (descriptor.Size - headerSize) / sizeof(STORAGE_TEMPERATURE_INFO))
        return {};

    std::optional<DiskTemperature> hottest;
    for (USHORT i = 0; i < descriptor.InfoCount; ++i) {
        STORAGE_TEMPERATURE_INFO sensor{};
        std::memcpy(&sensor, data.data() + headerSize + i * sizeof(sensor), sizeof(sensor));
        if (USHORT(sensor.Temperature) == STORAGE_TEMPERATURE_VALUE_NOT_REPORTED)
            continue;
        const DiskTemperature reading{disk.DeviceNumber, sensor.Index, double(sensor.Temperature)};
        if (sensor.Index == 0)
            return reading;
        if (!hottest || reading.temperatureCelsius > hottest->temperatureCelsius)
            hottest = reading;
    }
    return hottest;
}

QList<DiskTemperature> sampleStorageTemperatures() {
    QList<DiskTemperature> result;
    auto diskInterface = GUID_DEVINTERFACE_DISK;
    ULONG size = 0;
    if (CM_Get_Device_Interface_List_SizeW(&size, &diskInterface, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS ||
        size < 2 || size > 64 * 1024)
        return result;
    std::vector<wchar_t> paths(size);
    if (CM_Get_Device_Interface_ListW(&diskInterface, nullptr, paths.data(), size, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
        return result;
    for (const auto *path = paths.data(); *path; path += std::wcslen(path) + 1) {
        const auto device = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (device == INVALID_HANDLE_VALUE)
            continue;
        const auto temperature = storageTemperature(device);
        CloseHandle(device);
        if (temperature)
            result.append(*temperature);
    }
    std::sort(result.begin(), result.end(), [](const DiskTemperature &a, const DiskTemperature &b) {
        return a.diskNumber < b.diskNumber;
    });
    return result;
}

quint64 ticks(FILETIME value) {
    return (quint64(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
#endif

#ifdef Q_OS_LINUX
std::optional<quint64> readKiB(const QByteArray &contents, const QByteArray &name) {
    for (const auto &line : contents.split('\n')) {
        if (!line.startsWith(name))
            continue;
        const auto fields = line.mid(name.size()).simplified().split(' ');
        bool valid = false;
        const auto value = fields.value(0).toULongLong(&valid);
        if (valid)
            return value * 1024;
    }
    return {};
}
#endif

}

struct SystemMetricsReader::State {
    quint64 total = 0, idle = 0;
#ifdef Q_OS_WIN
    struct ProcessTime { quint64 created, used; };
    QHash<DWORD, ProcessTime> previous;
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engine = nullptr, dedicatedMemory = nullptr;
    QSet<quint64> graphicsIds, neuralIds;
    QList<GraphicsAdapter> graphics = graphicsAdapters(graphicsIds);
    const QList<LUID> neural = neuralAdapters(graphics, neuralIds);
    QElapsedTimer clock;
    NetworkRateSampler network;
    qint64 storageReadMs = -1;
    QList<DiskTemperature> storage;

    State() {
        clock.start();
        if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
            PdhAddEnglishCounterW(query, LR"(\GPU Engine(*)\Utilization Percentage)", 0, &engine);
            PdhAddEnglishCounterW(query, LR"(\GPU Adapter Memory(*)\Dedicated Usage)", 0, &dedicatedMemory);
        }
    }
    ~State() {
        if (query)
            PdhCloseQuery(query);
    }
#elif defined(Q_OS_LINUX)
    quint64 playerNanoseconds = 0;
#endif

    void cpu(SystemMetrics &result, quint64 nextTotal, quint64 nextIdle) {
        if (total && nextTotal > total && nextIdle >= idle && nextIdle - idle <= nextTotal - total)
            result.cpuPercent = 100.0 * (1.0 - double(nextIdle - idle) / double(nextTotal - total));
        total = nextTotal;
        idle = nextIdle;
    }
};

SystemMetricsReader::SystemMetricsReader() : state_(std::make_unique<State>()) {}
SystemMetricsReader::~SystemMetricsReader() = default;

SystemMetrics SystemMetricsReader::sample() {
    SystemMetrics result;
#ifdef Q_OS_WIN
    auto counters = networkCounters();
    const auto network = state_->network.sample(std::move(counters), state_->clock.nsecsElapsed());
    result.networkRxBytesPerSecond = network.receivedBytesPerSecond;
    result.networkTxBytesPerSecond = network.sentBytesPerSecond;

    const auto previousTotal = state_->total;
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user))
        state_->cpu(result, ticks(kernel) + ticks(user), ticks(idle));
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        result.memoryTotal = memory.ullTotalPhys;
        result.memoryUsed = memory.ullTotalPhys - memory.ullAvailPhys;
    }
    const auto players = playerProcesses(result.includesChildProcesses);
    QHash<DWORD, State::ProcessTime> current;
    quint64 used = 0;
    for (const auto pid : players) {
        const auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!handle)
            continue;
        FILETIME created{}, exited{}, processKernel{}, processUser{};
        if (GetProcessTimes(handle, &created, &exited, &processKernel, &processUser)) {
            const State::ProcessTime value{ticks(created), ticks(processKernel) + ticks(processUser)};
            current.insert(pid, value);
            const auto old = state_->previous.constFind(pid);
            if (old != state_->previous.cend() && old->created == value.created && value.used >= old->used)
                used += value.used - old->used;
        }
        PROCESS_MEMORY_COUNTERS process{};
        if (K32GetProcessMemoryInfo(handle, &process, sizeof(process)))
            result.playerResident = result.playerResident.value_or(0) + process.WorkingSetSize;
        CloseHandle(handle);
    }
    if (previousTotal && state_->total > previousTotal && !state_->previous.isEmpty() && !current.isEmpty())
        result.playerCpuPercent = std::min(100.0, 100.0 * double(used) / double(state_->total - previousTotal));
    state_->previous = std::move(current);
    result.npuDetected = !state_->neural.isEmpty();
    QHash<quint64, quint64> dedicated;
    if (state_->query && PdhCollectQueryData(state_->query) == ERROR_SUCCESS) {
        sampleAccelerators(state_->engine, state_->graphicsIds, state_->neuralIds, players, result);
        dedicated = dedicatedMemoryUsage(state_->dedicatedMemory);
    }
    for (const auto &adapter : std::as_const(state_->graphics)) {
        auto metrics = adapter.metrics;
        metrics.temperatureCelsius = adapterTemperature(adapter.luid);
        const auto usage = dedicated.constFind(metrics.adapterLuid);
        if (metrics.dedicatedMemoryTotal && usage != dedicated.cend())
            metrics.dedicatedMemoryUsed = *usage;
        result.graphicsAdapters.append(std::move(metrics));
    }
    for (const auto luid : state_->neural) {
        const auto temperature = adapterTemperature(luid);
        if (temperature && (!result.npuTemperatureCelsius || *temperature > *result.npuTemperatureCelsius))
            result.npuTemperatureCelsius = temperature;
    }
    const auto elapsed = state_->clock.elapsed();
    if (state_->storageReadMs < 0 || elapsed - state_->storageReadMs >= 5000) {
        state_->storage = sampleStorageTemperatures();
        state_->storageReadMs = elapsed;
    }
    result.storageTemperatures = state_->storage;
#elif defined(Q_OS_LINUX)
    QFile cpu("/proc/stat");
    if (cpu.open(QIODevice::ReadOnly)) {
        const auto fields = cpu.readLine().simplified().split(' ');
        if (fields.size() >= 9 && fields[0] == "cpu") {
            quint64 total = 0;
            for (int i = 1; i <= 8; ++i)
                total += fields[i].toULongLong();
            timespec playerTime{};
            const auto ticksPerSecond = sysconf(_SC_CLK_TCK);
            if (ticksPerSecond > 0 && clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &playerTime) == 0) {
                const auto now = quint64(playerTime.tv_sec) * 1000000000 + quint64(playerTime.tv_nsec);
                if (state_->total && total > state_->total && state_->playerNanoseconds && now >= state_->playerNanoseconds)
                    result.playerCpuPercent = std::min(100.0, (now - state_->playerNanoseconds) / 10000000.0 *
                                                       ticksPerSecond / double(total - state_->total));
                state_->playerNanoseconds = now;
            }
            state_->cpu(result, total, fields[4].toULongLong() + fields[5].toULongLong());
        }
    }
    QFile memory("/proc/meminfo");
    if (memory.open(QIODevice::ReadOnly)) {
        const auto contents = memory.readAll();
        result.memoryTotal = readKiB(contents, "MemTotal:");
        const auto available = readKiB(contents, "MemAvailable:");
        if (result.memoryTotal && available && *result.memoryTotal >= *available)
            result.memoryUsed = *result.memoryTotal - *available;
    }
    QFile process("/proc/self/status");
    if (process.open(QIODevice::ReadOnly))
        result.playerResident = readKiB(process.readAll(), "VmRSS:");
#endif
    return result;
}

}
