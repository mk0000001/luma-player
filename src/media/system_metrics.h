#pragma once

#include <QtGlobal>
#include <memory>
#include <optional>

namespace luma {

struct SystemMetrics {
    std::optional<double> cpuPercent, gpuPercent, npuPercent, gpuDecodePercent;
    std::optional<quint64> memoryUsed, memoryTotal, playerResident;
    bool npuDetected = false;
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
