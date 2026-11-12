#pragma once

#include "media/playback.h"
#include "media/system_metrics.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <array>
#include <functional>

namespace luma {

struct PerformanceInput {
    std::optional<PlaybackState> playback;
    quint64 sourceGeneration = 0;
    qint64 captionAheadMs = 0;
    int recoveries = 0;
    bool disc = false, captionWaiting = false;
    QString recognitionBackend, translationBackend;
    QString recognitionRuntime, translationRuntime;
};

class PerformanceOverlay : public QWidget {
public:
    enum class View { Text, Graph, Both };
    explicit PerformanceOverlay(QWidget *anchor, std::function<PerformanceInput()> input);
    ~PerformanceOverlay() override;

    void setMonitoring(bool enabled);
    void setDiagnosticCollection(bool enabled);
    void setView(View view);
    void setLeftAligned(bool left);
    void clearHistory();
    QJsonObject summary() const;
    QJsonObject diagnosticHistory() const;
    qint64 elapsedMs() const;
    QByteArray csv() const;
    bool hasSamples() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    enum Metric { Cpu, Gpu, Npu, Ram, Fps, DiscRate, NetworkRx, NetworkTx, MetricCount };
#ifdef Q_OS_WIN
    static constexpr int visibleMetricCount = MetricCount;
#else
    static constexpr int visibleMetricCount = DiscRate + 1;
#endif
    struct ContentLayout {
        bool compact, chart;
        int metricHeight, hardwareRows, height;
    };
    struct Sample {
        qint64 elapsedMs = 0;
        std::array<std::optional<double>, MetricCount> values;
        std::array<std::optional<double>, MetricCount> playerValues;
        SystemMetrics system;
        PerformanceInput input;
        std::optional<double> inputMiBps, demuxMiBps, presentedFps;
    };
    static constexpr int sampleIntervalMs = 1000;
    static constexpr qsizetype overlayHistoryLimit = 90;
    static constexpr qsizetype diagnosticHistoryLimit = 300;
    static constexpr qint64 diagnosticRetentionMs = sampleIntervalMs * diagnosticHistoryLimit;
    static QJsonObject sampleJson(const Sample &sample);
    static QJsonObject diagnosticSample(const Sample &sample);
    void syncSampling();
    bool canSample() const;
    QList<Sample>::const_iterator overlaySamplesBegin() const;
    void requestSample();
    void appendSample(SystemMetrics system);
    std::optional<double> presentedRate(const Sample &sample) const;
    ContentLayout contentLayout(int anchorHeight) const;
    void syncGeometry();
    double graphMaximum(Metric metric) const;
    void paintGraph(QPainter &painter, const QRectF &area, Metric metric, const QColor &color) const;

    QPointer<QWidget> anchor_;
    std::function<PerformanceInput()> input_;
    QThread thread_;
    QObject *worker_ = nullptr;
    std::unique_ptr<SystemMetricsReader> reader_;
    QTimer timer_;
    QElapsedTimer clock_;
    QList<Sample> history_;
    quint64 generation_ = 0, workerGeneration_ = 0;
    bool monitoring_ = false, diagnosticCollection_ = false, pending_ = false, leftAligned_ = false;
    View view_ = View::Both;
};

}
