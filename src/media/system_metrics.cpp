#include "system_metrics.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <wrl/client.h>
#if __has_include(<dxcore.h>)
#include <initguid.h>
#include <dxcore.h>
#define LUMA_HAS_DXCORE 1
#endif
#endif

namespace luma {
namespace {

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;

quint64 luidKey(LUID luid) {
    return (quint64(quint32(luid.HighPart)) << 32) | luid.LowPart;
}

QSet<quint64> graphicsAdapters() {
    QSet<quint64> result;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return result;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(i, &adapter)))
            break;
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)) && !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            result.insert(luidKey(description.AdapterLuid));
    }
    return result;
}

QSet<quint64> neuralAdapters() {
    QSet<quint64> result;
#ifdef LUMA_HAS_DXCORE
    const auto library = LoadLibraryExW(L"dxcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library)
        return result;
    {
        using CreateFactory = HRESULT (WINAPI *)(REFIID, void **);
        const auto create = reinterpret_cast<CreateFactory>(GetProcAddress(library, "DXCoreCreateAdapterFactory"));
        ComPtr<IDXCoreAdapterFactory> factory;
        ComPtr<IDXCoreAdapterList> adapters;
        if (create && SUCCEEDED(create(IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateAdapterList(1, &DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU, IID_PPV_ARGS(&adapters)))) {
            for (uint32_t i = 0; i < adapters->GetAdapterCount(); ++i) {
                ComPtr<IDXCoreAdapter> adapter;
                LUID luid{};
                if (SUCCEEDED(adapters->GetAdapter(i, IID_PPV_ARGS(&adapter))) &&
                    SUCCEEDED(adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, &luid)))
                    result.insert(luidKey(luid));
            }
        }
    }
    FreeLibrary(library);
#endif
    return result;
}

void updatePeak(std::optional<double> &peak, double value) {
    peak = std::max(peak.value_or(0.0), std::clamp(value, 0.0, 100.0));
}

void sampleAccelerators(PDH_HCOUNTER counter, const QSet<quint64> &graphics,
                        const QSet<quint64> &neural, SystemMetrics &result) {
    DWORD bytes = 0, count = 0;
    if (!counter || PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != PDH_MORE_DATA ||
        bytes > 16 * 1024 * 1024)
        return;
    using Item = PDH_FMT_COUNTERVALUE_ITEM_W;
    std::vector<Item> buffer((bytes + sizeof(Item) - 1) / sizeof(Item));
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, buffer.data()) != ERROR_SUCCESS)
        return;

    static const QRegularExpression engineName(
        R"(^pid_\d+_(luid_0x([0-9a-f]+)_0x([0-9a-f]+)_phys_\d+_eng_\d+)_engtype_([^#]+))",
        QRegularExpression::CaseInsensitiveOption);
    struct Engine { quint64 adapter; bool decode; double usage; };
    QHash<QString, Engine> engines;
    for (DWORD i = 0; i < count; ++i) {
        const auto &item = buffer[i];
        const auto status = item.FmtValue.CStatus;
        const double value = item.FmtValue.doubleValue;
        if (!item.szName || (status != PDH_CSTATUS_VALID_DATA && status != PDH_CSTATUS_NEW_DATA) ||
            !std::isfinite(value) || value < 0)
            continue;
        const auto match = engineName.match(QString::fromWCharArray(item.szName));
        if (!match.hasMatch())
            continue;
        const auto adapter = (quint64(match.captured(2).toUInt(nullptr, 16)) << 32) |
                             match.captured(3).toUInt(nullptr, 16);
        if (!graphics.contains(adapter) && !neural.contains(adapter))
            continue;
        const auto id = match.captured(1);
        auto it = engines.find(id);
        if (it == engines.end())
            it = engines.insert(id, {adapter, match.captured(4).contains("VideoDecode", Qt::CaseInsensitive), 0});
        it->usage += value;
    }
    // Sum processes on each physical engine, then take the busiest engine.
    for (const auto &engine : engines) {
        if (neural.contains(engine.adapter))
            updatePeak(result.npuPercent, engine.usage);
        else {
            updatePeak(result.gpuPercent, engine.usage);
            if (engine.decode)
                updatePeak(result.gpuDecodePercent, engine.usage);
        }
    }
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
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engine = nullptr;
    const QSet<quint64> graphics = graphicsAdapters(), neural = neuralAdapters();

    State() {
        if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS)
            PdhAddEnglishCounterW(query, LR"(\GPU Engine(*)\Utilization Percentage)", 0, &engine);
    }
    ~State() {
        if (query)
            PdhCloseQuery(query);
    }
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
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user))
        state_->cpu(result, ticks(kernel) + ticks(user), ticks(idle));
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        result.memoryTotal = memory.ullTotalPhys;
        result.memoryUsed = memory.ullTotalPhys - memory.ullAvailPhys;
    }
    PROCESS_MEMORY_COUNTERS process{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &process, sizeof(process)))
        result.playerResident = process.WorkingSetSize;
    result.npuDetected = !state_->neural.isEmpty();
    if (state_->engine && PdhCollectQueryData(state_->query) == ERROR_SUCCESS)
        sampleAccelerators(state_->engine, state_->graphics, state_->neural, result);
#elif defined(Q_OS_LINUX)
    QFile cpu("/proc/stat");
    if (cpu.open(QIODevice::ReadOnly)) {
        const auto fields = cpu.readLine().simplified().split(' ');
        if (fields.size() >= 9 && fields[0] == "cpu") {
            quint64 total = 0;
            for (int i = 1; i <= 8; ++i)
                total += fields[i].toULongLong();
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
