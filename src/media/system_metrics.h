#pragma once

#include <QList>
#include <QString>
#include <memory>
#include <optional>

namespace luma {

struct GraphicsMetrics {
    quint64 adapterLuid = 0;
    QString name;
    std::optional<double> temperatureCelsius;
    std::optional<quint64> dedicatedMemoryUsed, dedicatedMemoryTotal;
    std::optional<bool> integrated;
};

struct DiskTemperature {
    quint32 diskNumber;
    quint16 sensorIndex;
    double temperatureCelsius;
};

struct SystemMetrics {
    std::optional<double> cpuPercent, gpuPercent, npuPercent, gpuDecodePercent;
    std::optional<double> playerCpuPercent, playerGpuPercent, playerNpuPercent, playerGpuDecodePercent;
    std::optional<quint64> memoryUsed, memoryTotal, playerResident;
    std::optional<double> networkRxBytesPerSecond, networkTxBytesPerSecond, npuTemperatureCelsius;
    QList<GraphicsMetrics> graphicsAdapters;
    QList<DiskTemperature> storageTemperatures;
    bool npuDetected = false, includesChildProcesses = false;
};

class SystemMetricsReader {
public:
    SystemMetricsReader();
    ~SystemMetricsReader();
    SystemMetrics sample();

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
