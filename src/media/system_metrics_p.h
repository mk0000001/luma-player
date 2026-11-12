#pragma once

#include <QHash>
#include <optional>

namespace luma {

struct NetworkCounters {
    quint64 received, sent;
};

struct NetworkRates {
    std::optional<double> receivedBytesPerSecond, sentBytesPerSecond;
};

class NetworkRateSampler {
public:
    NetworkRates sample(QHash<quint64, NetworkCounters> counters, qint64 timestampNs);

private:
    QHash<quint64, NetworkCounters> previous_;
    qint64 timestampNs_ = 0;
};

}
